#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import os, re, csv

ROOT_DIR    = '/Data3/yutong.han/riscv/rxu-gem5/out/spec/l2cache_0516_mmu16entry_load_noprefetch_v1'
OUTPUT_DIR  = '/Data3/yutong.han/riscv/rxu-gem5/out/spec'
OUTPUT_FILE = 'latency_summary_0516_mmu16entry_load_noprefetch_v1.csv'

# ---------- 正则 ----------
p_req = re.compile(
    r'^\s*system\.cpu\.([^.]+)\.'
    r'(Hit|Miss)Req'
    r'(Num|LatencyAll)\s+([0-9.eE+-]+)'
)
p_l2u = re.compile(
    r'^\s*system\.cpu\.[^.]+\.(loadToUse_cycle|loadToUse_num)\s+([0-9.eE+-]+)'
)
p_tlb = re.compile(
    r'^\s*system\.cpu\.[^.]+\.(instToTlb_num|instToTlb_cycle)\s+([0-9.eE+-]+)'
)
p_zero = re.compile(  # 新增：匹配ZeroCycle_num
    r'^\s*system\.cpu\.[^.]+\.(ZeroCycle_num)\s+([0-9.eE+-]+)'
)

cases_data, all_cols = {}, set()

# ----------- 处理所有 case 的数据 ----------
for path, _, files in os.walk(ROOT_DIR):
    if 'stats.txt' not in files:
        continue
    case = os.path.relpath(path, ROOT_DIR)

    with open(os.path.join(path, 'stats.txt'), 'r', encoding='utf-8', errors='ignore') as f:
        for line in f:
            # 原有的 Hit/MissReqNum, Hit/MissReqLatencyAll
            m = p_req.match(line)
            if m:
                src, hm, met, raw = m.groups()
                col = f'{src}_{hm}Req{met}'
                val = float(raw)
                if met == 'LatencyAll':
                    val /= 500.0
                cases_data.setdefault(case, {})[col] = val
                all_cols.add(col)
                continue

            # 原有的 loadToUse_cycle / loadToUse_num
            m = p_l2u.match(line)
            if m:
                met, raw = m.groups()
                val = float(raw)
                cases_data.setdefault(case, {})[met] = val
                all_cols.add(met)
                continue

            # instToTlb_cycle / instToTlb_num
            m = p_tlb.match(line)
            if m:
                met, raw = m.groups()
                val = float(raw)
                cases_data.setdefault(case, {})[met] = val
                all_cols.add(met)
                continue

            # 新增：ZeroCycle_num
            m = p_zero.match(line)
            if m:
                met, raw = m.groups()
                val = float(raw)
                cases_data.setdefault(case, {})[met] = val
                all_cols.add(met)
                continue

# ------------- 统计列集合 -------------
num_cols     = [c for c in all_cols if c.endswith('ReqNum')]  # ZeroCycle_num 不计入
lat_cols     = [c for c in all_cols if c.endswith('ReqLatencyAll')]
sorted_cols  = sorted(all_cols) + ['TotalNum', 'TotalLatencyAll']

# ------------- 计算总和 -------------
col_sum = {c: 0.0 for c in all_cols}
for data in cases_data.values():
    for col in all_cols:
        col_sum[col] += data.get(col, 0.0)

total_num_sum = sum(col_sum[c] for c in num_cols)
total_lat_sum = sum(col_sum[c] for c in lat_cols)

# ------------- 输出 CSV -------------
os.makedirs(OUTPUT_DIR, exist_ok=True)
csv_path = os.path.join(OUTPUT_DIR, OUTPUT_FILE)
with open(csv_path, 'w', newline='') as csvf:
    w = csv.writer(csvf)
    w.writerow(['Case'] + sorted_cols)

    # Sum 行
    sum_row = ['Sum']
    for col in sorted_cols:
        if col == 'TotalNum':
            sum_row.append(f'{int(total_num_sum)}')
        elif col == 'TotalLatencyAll':
            sum_row.append(f'{total_lat_sum:.6f}')
        elif col.endswith('ReqLatencyAll'):
            sum_row.append(f'{col_sum[col]:.6f}')
        else:
            sum_row.append(f'{int(col_sum[col])}' if col != 'ZeroCycle_num' else f'{int(col_sum[col])}')
    w.writerow(sum_row)

    # 各 case 行
    for case in sorted(cases_data):
        data = cases_data[case]
        row = [case]
        for col in sorted_cols:
            if col == 'TotalNum':
                row.append(f'{int(sum(data.get(c, 0) for c in num_cols))}')
            elif col == 'TotalLatencyAll':
                row.append(f'{sum(data.get(c, 0) for c in lat_cols):.6f}')
            elif col.endswith('ReqLatencyAll'):
                row.append(f'{data.get(col, 0):.6f}')
            else:
                row.append(f'{int(data.get(col, 0))}')
        w.writerow(row)

print(f'✅ saved to {csv_path}')