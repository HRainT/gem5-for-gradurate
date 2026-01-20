#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import os
import re
import csv
import math
import argparse
from pathlib import Path
from typing import Dict, List, Optional, Tuple, Set

# 两组关键字（按题目给定顺序）
GROUP1_KEYS = [
    "system.cpu.commit.commitSquashedInsts",
]
GROUP2_KEYS = [
    "system.cpu.fetch.toDecodeInsts",
    "system.cpu.numCycles",
]

ALL_KEYS = GROUP1_KEYS + GROUP2_KEYS
METRIC_NAMES = ["recover rate", "frontbandwidth"]

# 为每个 key 准备一个正则：行首若干空白 + key + 至少一个空白 + 数字（支持科学计数法）
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
    # 逐行匹配，若出现多次，以最后一次为准
    # 使用宽容编码读取
    def _read_lines(p: Path):
        # 尝试 utf-8，其次 latin-1
        for enc in ("utf-8", "latin-1"):
            try:
                with p.open("r", encoding=enc, errors="replace") as f:
                    for line in f:
                        yield line
                return
            except Exception:
                continue
        # 最后一次再尝试系统默认
        with p.open("r", errors="replace") as f:
            for line in f:
                yield line

    for line in _read_lines(stats_path):
        # 跳过空行或注释行
        if not line.strip():
            continue
        for k in keys:
            m = KEY_PATTERNS[k].match(line)
            if m:
                try:
                    val = float(m.group(1))
                except ValueError:
                    # 非法数字，忽略
                    continue
                values[k] = val
                # 不 break：理论上同一行只匹配一个 key，break 也可
                break
    return values

def safe_div(numer: float, denom: float) -> Optional[float]:
    if denom is None:
        return None
    if denom == 0:
        return None
    return numer / denom

def compute_metrics(row: Dict[str, float]) -> Dict[str, Optional[float]]:
    """计算单个benchmark的指标"""
    # recover rate = commitSquashedInsts / 40000000
    recover_rate = safe_div(row["system.cpu.commit.commitSquashedInsts"], 40000000.0)

    # frontbandwidth = toDecodeInsts / numCycles
    frontbandwidth = safe_div(
        row["system.cpu.fetch.toDecodeInsts"], 
        row["system.cpu.numCycles"]
    )

    return {
        "recover rate": recover_rate,
        "frontbandwidth": frontbandwidth,
    }

def compute_category_metrics(category_vals: Dict[str, float], num_in_category: int) -> Dict[str, Optional[float]]:
    """计算分类（如astar）的指标"""
    # 分类的recover rate = 分类内所有benchmark的commitSquashedInsts累加 / (40000000 * 分类内benchmark数)
    category_recover_rate = safe_div(
        category_vals["system.cpu.commit.commitSquashedInsts"], 
        40000000.0 * num_in_category
    )
    
    # 分类的frontbandwidth = 分类内所有benchmark的toDecodeInsts累加 / 分类内所有benchmark的numCycles累加
    category_frontbandwidth = safe_div(
        category_vals["system.cpu.fetch.toDecodeInsts"],
        category_vals["system.cpu.numCycles"]
    )
    
    return {
        "recover rate": category_recover_rate,
        "frontbandwidth": category_frontbandwidth,
    }

def extract_category_name(benchmark_name: str) -> str:
    """从benchmark名称中提取分类前缀"""
    # 常见的benchmark命名模式：astar_1, astar_2, bzip2_1, bzip2_2等
    # 提取第一个下划线前的部分作为分类名，如果没有下划线则使用整个名称
    if '_' in benchmark_name:
        return benchmark_name.split('_')[0]
    else:
        # 如果没有下划线，尝试提取数字前的部分
        # 例如：astar1 -> astar, bzip22 -> bzip2
        match = re.match(r'^([a-zA-Z]+)', benchmark_name)
        if match:
            return match.group(1)
        return benchmark_name

def group_benchmarks_by_category(rows: List[Tuple[str, Dict[str, float]]]) -> Dict[str, List[Tuple[str, Dict[str, float]]]]:
    """将benchmark按分类分组"""
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
    # 若为目录或无后缀，视为目录
    if out_path.exists() and out_path.is_dir():
        out_path.mkdir(parents=True, exist_ok=True)
        return out_path / "summary.csv"
    # 若路径不存在且无 .csv 后缀，创建为目录并用默认文件名
    if not out_path.suffix:
        out_path.mkdir(parents=True, exist_ok=True)
        return out_path / "summary.csv"
    # 其他情况：仍按文件对待（若后缀不是 .csv 也允许）
    out_path.parent.mkdir(parents=True, exist_ok=True)
    return out_path

def format_float(x: Optional[float]) -> str:
    if x is None or (isinstance(x, float) and (math.isnan(x) or math.isinf(x))):
        return ""
    # 对整数值避免多余小数，对浮点保留合适精度
    if abs(x - int(x)) < 1e-12:
        return str(int(x))
    return f"{x:.6g}"  # 最多 6 有效位，便于阅读

def write_csv(
    out_file: Path,
    rows: List[Tuple[str, Dict[str, float]]],
) -> None:
    # 按分类分组
    categories = group_benchmarks_by_category(rows)
    
    # 计算总体total
    total_vals = {k: 0.0 for k in ALL_KEYS}
    total_benchmark_count = len(rows)
    
    # 计算每个分类的汇总
    category_summaries: Dict[str, Dict] = {}
    for category, category_rows in categories.items():
        category_vals = {k: 0.0 for k in ALL_KEYS}
        for _, vals in category_rows:
            for k in ALL_KEYS:
                category_vals[k] += vals.get(k, 0.0)
                total_vals[k] += vals.get(k, 0.0)  # 同时累加到总体total
        
        # 计算分类指标
        category_metrics = compute_category_metrics(category_vals, len(category_rows))
        category_summaries[category] = {
            'values': category_vals,
            'metrics': category_metrics,
            'count': len(category_rows)
        }
    
    # 计算总体total指标
    total_metrics = compute_category_metrics(total_vals, total_benchmark_count)
    
    # 输出列次序：category + ALL_KEYS + METRICS + count
    header = ["category"] + ALL_KEYS + METRIC_NAMES + ["benchmark_count"]

    with out_file.open("w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(header)

        # 写总体total行（在最顶层）
        total_row = ["TOTAL"] + [format_float(total_vals[k]) for k in ALL_KEYS] + [
            format_float(total_metrics[m]) for m in METRIC_NAMES
        ] + [str(total_benchmark_count)]
        writer.writerow(total_row)
        
        writer.writerow([])  # 空行分隔
        
        # 写每个分类的汇总行（按字母顺序排序）
        for category in sorted(category_summaries.keys()):
            summary = category_summaries[category]
            category_row = [category] + [format_float(summary['values'][k]) for k in ALL_KEYS] + [
                format_float(summary['metrics'][m]) for m in METRIC_NAMES
            ] + [str(summary['count'])]
            writer.writerow(category_row)
        
        # 可选：如果需要详细数据，可以添加一个分隔部分
        # writer.writerow([])
        # writer.writerow(["DETAILED BENCHMARKS"])
        # for bench, vals in rows:
        #     metrics = compute_metrics(vals)
        #     line = [bench] + [format_float(vals[k]) for k in ALL_KEYS] + [
        #         format_float(metrics[m]) for m in METRIC_NAMES
        #     ] + ["1"]
        #     writer.writerow(line)

def main():
    parser = argparse.ArgumentParser(
        description="递归抓取 stats.txt 中指定键，按分类计算 recover rate 和 frontbandwidth 并汇总输出 CSV。"
    )
    parser.add_argument("-i", "--input-root", required=True, help="输入根目录，递归查找所有 stats.txt")
    parser.add_argument("-o", "--output", required=True, help="输出路径（.csv 文件或目录）")
    parser.add_argument("--detailed", action="store_true", help="输出详细的每个benchmark数据")
    args = parser.parse_args()

    root = Path(args.input_root).resolve()
    if not root.exists() or not root.is_dir():
        raise SystemExit(f"输入目录不存在或不可用：{root}")

    out_path = resolve_output_path(Path(args.output).resolve())

    stats_files = find_stats_files(root)
    if not stats_files:
        print("未找到任何 stats.txt 文件。")
        # 仍然输出一个只有表头和 total=0 的 CSV 以便流程完整
        write_csv(out_path, [])
        print(f"已写出空汇总：{out_path}")
        return

    rows: List[Tuple[str, Dict[str, float]]] = []
    for p in sorted(stats_files):
        # benchmark = stats.txt 的上一层文件夹名
        bench = p.parent.name
        vals = parse_stats_file(p, ALL_KEYS)
        rows.append((bench, vals))

    write_csv(out_path, rows)

    print(f"共处理 {len(rows)} 个 stats.txt")
    print(f"汇总结果已写出：{out_path}")
    
    # 打印分类统计信息
    categories = group_benchmarks_by_category(rows)
    print(f"发现 {len(categories)} 个分类:")
    for category in sorted(categories.keys()):
        print(f"  {category}: {len(categories[category])} 个benchmark")

if __name__ == "__main__":
    main()
