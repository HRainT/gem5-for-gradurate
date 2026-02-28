#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import os
import re
import csv
import math
import argparse
from pathlib import Path
from typing import Dict, List, Optional, Tuple

# --- 配置区域 ---

# 1. 定义 frontbandwidth 需要的两个 Key
KEY_DECODE_PRIMARY = "system.cpu.IBandLB.toDecodeInsts"
KEY_DECODE_FALLBACK = "system.cpu.fetch.toDecodeInsts" # 新增后备 Key

GROUP1_KEYS = [
    "system.cpu.commit.commitSquashedInsts",
]

GROUP2_KEYS = [
    KEY_DECODE_PRIMARY,
    KEY_DECODE_FALLBACK, # 确保加入搜索列表
    "system.cpu.numCycles",
]

ALL_KEYS = GROUP1_KEYS + GROUP2_KEYS
METRIC_NAMES = ["recover rate", "frontbandwidth"]

# --- 正则与解析逻辑 ---

def compile_key_patterns(keys: List[str]) -> Dict[str, re.Pattern]:
    patterns = {}
    for k in keys:
        # 例：^\s*system.cpu.commit.commitSquashedInsts\s+([-+]?\d+(?:\.\d+)?(?:[eE][-+]?\d+)?)
        pat = re.compile(
            r"^\s*" + re.escape(k) + r"\s+([-+]?\d+(?:\.\d+)?(?:[eE][-+]?\d+)?)\b"
        )
        patterns[k] = pat
    return patterns

KEY_PATTERNS = compile_key_patterns(ALL_KEYS)

def find_stats_files(root: Path) -> List[Path]:
    files = []
    for dirpath, _, filenames in os.walk(root):
        for fn in filenames:
            if fn == "stats.txt":
                files.append(Path(dirpath) / fn)
    return files

def parse_stats_file(stats_path: Path, keys: List[str]) -> Dict[str, float]:
    values: Dict[str, float] = {k: 0.0 for k in keys}  # 缺失默认 0
    
    def _read_lines(p: Path):
        for enc in ("utf-8", "latin-1"):
            try:
                with p.open("r", encoding=enc, errors="replace") as f:
                    for line in f:
                        yield line
                return
            except Exception:
                continue
        with p.open("r", errors="replace") as f:
            for line in f:
                yield line

    for line in _read_lines(stats_path):
        if not line.strip():
            continue
        for k in keys:
            m = KEY_PATTERNS[k].match(line)
            if m:
                try:
                    val = float(m.group(1))
                except ValueError:
                    continue
                values[k] = val
                break
    return values

def safe_div(numer: float, denom: float) -> Optional[float]:
    if denom is None:
        return None
    if denom == 0:
        return None
    return numer / denom

# --- 修改后的计算逻辑 ---

def get_decode_insts(data_row: Dict[str, float]) -> float:
    """
    获取解码指令数。
    逻辑：尝试取 IBandLB 和 fetch 两个 key。
    由于 parse_stats_file 中缺失的 key 默认为 0.0，
    如果 stats.txt 中只存在其中一个，相加即可得到正确值。
    """
    v1 = data_row.get(KEY_DECODE_PRIMARY, 0.0)
    v2 = data_row.get(KEY_DECODE_FALLBACK, 0.0)
    return v1 + v2

def compute_metrics(row: Dict[str, float]) -> Dict[str, Optional[float]]:
    """计算单个benchmark的指标"""
    # recover rate
    recover_rate = safe_div(row["system.cpu.commit.commitSquashedInsts"], 20000000.0)

    # frontbandwidth
    # 修改：使用 get_decode_insts 获取分子（支持 fallback）
    decode_insts = get_decode_insts(row)
    frontbandwidth = safe_div(
        decode_insts, 
        row["system.cpu.numCycles"]
    )

    return {
        "recover rate": recover_rate,
        "frontbandwidth": frontbandwidth,
    }

def compute_category_metrics(category_vals: Dict[str, float], num_in_category: int) -> Dict[str, Optional[float]]:
    """计算分类（如astar）的指标"""
    
    # recover rate
    category_recover_rate = safe_div(
        category_vals["system.cpu.commit.commitSquashedInsts"], 
        20000000.0 * num_in_category
    )
    
    # frontbandwidth
    # 修改：同样使用 helper 函数或逻辑相加获取总 decode insts
    total_decode_insts = get_decode_insts(category_vals)
    
    category_frontbandwidth = safe_div(
        total_decode_insts,
        category_vals["system.cpu.numCycles"]
    )
    
    return {
        "recover rate": category_recover_rate,
        "frontbandwidth": category_frontbandwidth,
    }

# --- 以下为辅助与输出逻辑 (基本保持不变) ---

def extract_category_name(benchmark_name: str) -> str:
    if '_' in benchmark_name:
        return benchmark_name.split('_')[0]
    else:
        match = re.match(r'^([a-zA-Z]+)', benchmark_name)
        if match:
            return match.group(1)
        return benchmark_name

def group_benchmarks_by_category(rows: List[Tuple[str, Dict[str, float]]]) -> Dict[str, List[Tuple[str, Dict[str, float]]]]:
    categories: Dict[str, List[Tuple[str, Dict[str, float]]]] = {}
    for bench, vals in rows:
        category = extract_category_name(bench)
        if category not in categories:
            categories[category] = []
        categories[category].append((bench, vals))
    return categories

def resolve_output_path(out_path: Path) -> Path:
    if out_path.suffix.lower() == ".csv":
        out_path.parent.mkdir(parents=True, exist_ok=True)
        return out_path
    if out_path.exists() and out_path.is_dir():
        out_path.mkdir(parents=True, exist_ok=True)
        return out_path / "summary.csv"
    if not out_path.suffix:
        out_path.mkdir(parents=True, exist_ok=True)
        return out_path / "summary.csv"
    out_path.parent.mkdir(parents=True, exist_ok=True)
    return out_path

def format_float(x: Optional[float]) -> str:
    if x is None or (isinstance(x, float) and (math.isnan(x) or math.isinf(x))):
        return ""
    if abs(x - int(x)) < 1e-12:
        return str(int(x))
    return f"{x:.6g}"

def write_csv(
    out_file: Path,
    rows: List[Tuple[str, Dict[str, float]]],
) -> None:
    categories = group_benchmarks_by_category(rows)
    
    total_vals = {k: 0.0 for k in ALL_KEYS}
    total_benchmark_count = len(rows)
    
    category_summaries: Dict[str, Dict] = {}
    for category, category_rows in categories.items():
        category_vals = {k: 0.0 for k in ALL_KEYS}
        for _, vals in category_rows:
            for k in ALL_KEYS:
                category_vals[k] += vals.get(k, 0.0)
                total_vals[k] += vals.get(k, 0.0)
        
        category_metrics = compute_category_metrics(category_vals, len(category_rows))
        category_summaries[category] = {
            'values': category_vals,
            'metrics': category_metrics,
            'count': len(category_rows)
        }
    
    total_metrics = compute_category_metrics(total_vals, total_benchmark_count)
    
    header = ["category"] + ALL_KEYS + METRIC_NAMES + ["benchmark_count"]

    with out_file.open("w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(header)

        # TOTAL 行
        total_row = ["TOTAL"] + [format_float(total_vals[k]) for k in ALL_KEYS] + [
            format_float(total_metrics[m]) for m in METRIC_NAMES
        ] + [str(total_benchmark_count)]
        writer.writerow(total_row)
        
        writer.writerow([])
        
        # 分类汇总行
        for category in sorted(category_summaries.keys()):
            summary = category_summaries[category]
            category_row = [category] + [format_float(summary['values'][k]) for k in ALL_KEYS] + [
                format_float(summary['metrics'][m]) for m in METRIC_NAMES
            ] + [str(summary['count'])]
            writer.writerow(category_row)

        if any(r for r in rows): # 如果有数据且开启了detailed (虽然脚本里detailed args只解析没用上，这里保留原逻辑)
             pass 

def main():
    parser = argparse.ArgumentParser(
        description="递归抓取 stats.txt 中指定键，按分类计算 recover rate 和 frontbandwidth (支持 Key 自动 fallback) 并汇总输出 CSV。"
    )
    parser.add_argument("-i", "--input-root", required=True, help="输入根目录")
    parser.add_argument("-o", "--output", required=True, help="输出路径")
    parser.add_argument("--detailed", action="store_true", help="输出详细数据")
    args = parser.parse_args()

    root = Path(args.input_root).resolve()
    if not root.exists() or not root.is_dir():
        raise SystemExit(f"输入目录不存在：{root}")

    out_path = resolve_output_path(Path(args.output).resolve())

    stats_files = find_stats_files(root)
    if not stats_files:
        print("未找到任何 stats.txt 文件。")
        write_csv(out_path, [])
        return

    rows: List[Tuple[str, Dict[str, float]]] = []
    for p in sorted(stats_files):
        bench = p.parent.name
        vals = parse_stats_file(p, ALL_KEYS)
        rows.append((bench, vals))

    write_csv(out_path, rows)

    print(f"共处理 {len(rows)} 个 stats.txt")
    print(f"汇总结果已写出：{out_path}")
    
    categories = group_benchmarks_by_category(rows)
    print(f"发现 {len(categories)} 个分类。")

if __name__ == "__main__":
    main()