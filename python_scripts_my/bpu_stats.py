#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import os, re, csv

ROOT_DIR    = '/Data3/yutong.han/riscv/rxu-gem5/out/spec/l2cache_0516_mmu16entry_load_noprefetch_v1'
OUTPUT_DIR  = '/Data3/yutong.han/riscv/rxu-gem5/out/spec'
OUTPUT_FILE = 'latency_summary_0516_mmu16entry_load_noprefetch_v1.csv'

# ---------- 新增的正则表达式 ----------
p_new_stats = re.compile(
    r'^\s*(system\.cpu\.(?:bpuMissRate|retiredBranchInsts|bpuMissCommitCount|bpuMissDecodeCount))\s+([0-9.eE+-]+)'
)

# 定义需要抓取的四个关键字
TARGET_STATS = [
    'system.cpu.bpuMissRate',
    'system.cpu.retiredBranchInsts',
    'system.cpu.bpuMissCommitCount',
    'system.cpu.bpuMissDecodeCount'
]

cases_data, all_cols = {}, set(TARGET_STATS)  # 只关注这四个关键字

# ----------- 处理所有 case 的数据 ----------
for path, _, files in os.walk(ROOT_DIR):
    if 'stats.txt' not in files:
        continue
    case = os.path.relpath(path, ROOT_DIR)

    with open(os.path.join(path, 'stats.txt'), 'r', encoding='utf-8', errors='ignore') as f:
        for line in f:
            # 只处理目标关键字
            m = p_new_stats.match(line)
            if m:
                stat_name, raw_val = m.groups()
                if stat_name in TARGET_STATS:
                    val = float(raw_val)
                    cases_data.setdefault(case, {})[stat_name] = val

# ------------- 计算总和 -------------
col_sum = {stat: 0.0 for stat in TARGET_STATS}
for data in cases_data.values():
    for stat in TARGET_STATS:
        col_sum[stat] += data.get(stat, 0.0)

# ------------- 输出 CSV -------------
os.makedirs(OUTPUT_DIR, exist_ok=True)
csv_path = os.path.join(OUTPUT_DIR, OUTPUT_FILE)
with open(csv_path, 'w', newline='') as csvf:
    w = csv.writer(csvf)
    w.writerow(['Case'] + TARGET_STATS)  # 使用固定顺序的表头

    # Sum 行 - 第一行显示四个数据的总和
    sum_row = ['Sum'] + [f'{col_sum[stat]:.6f}' for stat in TARGET_STATS]
    w.writerow(sum_row)

    # 各 case 行
    for case in sorted(cases_data):
        data = cases_data[case]
        row = [case] + [f'{data.get(stat, 0):.6f}' for stat in TARGET_STATS]
        w.writerow(row)

print(f'✅ saved to {csv_path}')
