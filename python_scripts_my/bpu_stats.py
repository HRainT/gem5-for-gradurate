#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import os
import re
import csv
import math
import argparse
from pathlib import Path
from typing import Dict, List, Optional, Tuple

# 三组关键字（按题目给定顺序）
GROUP1_KEYS = [
    "system.cpu.commit.branchMispredictsFromDecode",
    "system.cpu.commit.branchMispredictsFromEXE",
    "system.cpu.commit.retiredBranchInsts",
]
GROUP2_KEYS = [
    "system.cpu.commit.commitSquashedInsts",
    "system.cpu.commit.commitRetiredInsts",
]
GROUP3_KEYS = [
    "system.cpu.IBandLB.toDecodeInsts",
    "system.cpu.IBandLB.idleCycles",
    "system.cpu.IBandLB.runCycles",
    "system.cpu.IBandLB.squashCycles",
]

ALL_KEYS = GROUP1_KEYS + GROUP2_KEYS + GROUP3_KEYS
METRIC_NAMES = ["miss rate", "recover rate", "frontbandwidth"]

# 为每个 key 准备一个正则：行首若干空白 + key + 至少一个空白 + 数字（支持科学计数法）
def compile_key_patterns(keys: List[str]) -> Dict[str, re.Pattern]:
    patterns = {}
    for k in keys:
        # 例：^\s*system.cpu.commit.retiredBranchInsts\s+([-+]?\d+(?:\.\d+)?(?:[eE][-+]?\d+)?)
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
    # miss rate = (Decode+EXE) / retiredBranchInsts
    mr_num = row["system.cpu.commit.branchMispredictsFromDecode"] + row["system.cpu.commit.branchMispredictsFromEXE"]
    mr_den = row["system.cpu.commit.retiredBranchInsts"]
    miss_rate = safe_div(mr_num, mr_den)

    # recover rate = commitSquashedInsts / (commitSquashedInsts + commitRetiredInsts)
    rec_num = row["system.cpu.commit.commitSquashedInsts"]
    rec_den = row["system.cpu.commit.commitSquashedInsts"] + row["system.cpu.commit.commitRetiredInsts"]
    recover_rate = safe_div(rec_num, rec_den)

    # frontbandwidth = toDecodeInsts / (idleCycles + runCycles + squashCycles)
    fb_num = row["system.cpu.IBandLB.toDecodeInsts"]
    fb_den = row["system.cpu.IBandLB.idleCycles"] + row["system.cpu.IBandLB.runCycles"] + row["system.cpu.IBandLB.squashCycles"]
    frontbandwidth = safe_div(fb_num, fb_den)

    return {
        "miss rate": miss_rate,
        "recover rate": recover_rate,
        "frontbandwidth": frontbandwidth,
    }

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
    # 计算 total（对 ALL_KEYS 求和），再根据 total 计算三项比率
    total_vals = {k: 0.0 for k in ALL_KEYS}
    for _, vals in rows:
        for k in ALL_KEYS:
            total_vals[k] += vals.get(k, 0.0)

    total_metrics = compute_metrics(total_vals)

    # 输出列次序：benchmark + ALL_KEYS + METRICS
    header = ["benchmark"] + ALL_KEYS + METRIC_NAMES

    with out_file.open("w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(header)

        # 写 total 行（在最顶层）
        total_row = ["total"] + [format_float(total_vals[k]) for k in ALL_KEYS] + [
            format_float(total_metrics[m]) for m in METRIC_NAMES
        ]
        writer.writerow(total_row)

        # 写每个 benchmark 行（按发现顺序）
        for bench, vals in rows:
            metrics = compute_metrics(vals)
            line = [bench] + [format_float(vals[k]) for k in ALL_KEYS] + [
                format_float(metrics[m]) for m in METRIC_NAMES
            ]
            writer.writerow(line)

def main():
    parser = argparse.ArgumentParser(
        description="递归抓取 stats.txt 中指定键，计算 miss/recover/frontbandwidth 并汇总输出 CSV。"
    )
    parser.add_argument("-i", "--input-root", required=True, help="输入根目录，递归查找所有 stats.txt")
    parser.add_argument("-o", "--output", required=True, help="输出路径（.csv 文件或目录）")
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

if __name__ == "__main__":
    main()
