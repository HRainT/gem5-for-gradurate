#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gem5 stats 汇总脚本（区间计数 + 百分比）
--------------------------------------
• l2.L1MissReqInterval::0-10 / ::overflows
  – 计数列：0IntervalSum … 10IntervalSum, overflowsIntervalSum
  – 百分比列：0IntervalPct … 10IntervalPct, overflowsIntervalPct
• “Sum” 行：计数列写总数，百分比列写总百分比
• 生成折线图 L1MissReqInterval_pct.png（Sum 行百分比）
"""
import os, re, csv
import matplotlib.pyplot as plt

# ------------------ 路径 ------------------
ROOT_DIR='/Data/feiyu.ding/rxu-gem5/out/spec/0703L2prefetch'
OUTPUT_DIR='/Data3/yutong.han/riscv/rxu-gem5/out/test'
OUTPUT_FILE='0703L2prefetch.csv'
FIG_FILE='0703L2prefetch.png'

# ------------------ 正则 ------------------
p_req   = re.compile(r'^\s*system\.cpu\.([^.]+)\.(Hit|Miss)Req(Num|LatencyAll)\s+([0-9.eE+-]+)')
p_l2u   = re.compile(r'^\s*system\.cpu\.[^.]+\.(loadToUse_cycle|loadToUse_num)\s+([0-9.eE+-]+)')
p_tlb   = re.compile(r'^\s*system\.cpu\.[^.]+\.(instToTlb_num|instToTlb_cycle)\s+([0-9.eE+-]+)')
p_zero  = re.compile(r'^\s*system\.cpu\.[^.]+\.(ZeroCycle_num)\s+([0-9.eE+-]+)')
p_tlbmiss = re.compile(r'^\s*system\.cpu\.mmu\.dtb\.(l[12]tlbMiss_(?:paralel_reqs|paralel_cycles|cycles))\s+([0-9.eE+-]+)')
p_dtb_misses = re.compile(r'^\s*system\.cpu\.mmu\.dtb\.misses\s+([0-9.eE+-]+)')
p_l2tlb_reqs = re.compile(r'^\s*system\.cpu\.mmu\.dtb\.l2tlbMiss_reqs\s+([0-9.eE+-]+)')

p_intv_samples = re.compile(r'^\s*(?:system\.)?l2\.L1MissReqInterval::samples\s+([0-9.eE+-]+)')
p_intv_digit   = re.compile(r'^\s*(?:system\.)?l2\.L1MissReqInterval::([0-9]+)\s+([0-9.eE+-]+)')
p_intv_ovfl    = re.compile(r'^\s*(?:system\.)?l2\.L1MissReqInterval::overflows\s+([0-9.eE+-]+)')
# ------------------ 正则 ------------------
p_lsumshr = re.compile(
    r'^\s*system\.cpu\.dcache\.LSUMSHRarrive(16entryNum|32entryNum)\s+([0-9.eE+-]+)'
)

p_overall_miss = re.compile(
    r'^\s*system\.cpu\.dcache\.overallMshrMisses::total\s+([0-9.eE+-]+)'
)

p_overall_hit = re.compile(
    r'^\s*system\.cpu\.dcache\.overallMshrHits::total\s+([0-9.eE+-]+)'
)
# ------------------ 列名 ------------------
cnt_cols  = [f'{i}IntervalSum'    for i in range(11)] + ['overflowsIntervalSum']
pct_cols  = [f'{i}IntervalPct'    for i in range(11)] + ['overflowsIntervalPct']

cases_data, all_cols = {}, set()                # all_cols 只放“计数列和其它原指标”，不放 pct

# ------------------ 遍历 ------------------
for path, _, files in os.walk(ROOT_DIR):
    if 'stats.txt' not in files:
        continue
    case = os.path.relpath(path, ROOT_DIR)
    with open(os.path.join(path, 'stats.txt'), 'r', encoding='utf-8', errors='ignore') as f:
        for line in f:
            # 1) Hit / Miss
            m = p_req.match(line)
            if m:
                src, hm, met, raw = m.groups()
                col = f'{src}_{hm}Req{met}'
                val = float(raw) / 500.0 if met == 'LatencyAll' else float(raw)
                cases_data.setdefault(case, {})[col] = val
                all_cols.add(col)
                continue

            # 2) loadToUse
            m = p_l2u.match(line)
            if m:
                met, raw = m.groups()
                cases_data.setdefault(case, {})[met] = float(raw)
                all_cols.add(met)
                continue

            # 3) instToTlb
            m = p_tlb.match(line)
            if m:
                met, raw = m.groups()
                cases_data.setdefault(case, {})[met] = float(raw)
                all_cols.add(met)
                continue

            # 4) ZeroCycle
            m = p_zero.match(line)
            if m:
                met, raw = m.groups()
                cases_data.setdefault(case, {})[met] = float(raw)
                all_cols.add(met)
                continue

            # 5) parallel tlb miss
            m = p_tlbmiss.match(line)
            if m:
                met, raw = m.groups()
                cases_data.setdefault(case, {})[met] = float(raw)
                all_cols.add(met)
                continue

            # 6) dtb misses
            m = p_dtb_misses.match(line)
            if m:
                cases_data.setdefault(case, {})['dtb_misses'] = float(m.group(1))
                all_cols.add('dtb_misses')
                continue

            # 7) l2tlbMiss_reqs
            m = p_l2tlb_reqs.match(line)
            if m:
                cases_data.setdefault(case, {})['l2tlbMiss_reqs'] = float(m.group(1))
                all_cols.add('l2tlbMiss_reqs')
                continue

            # 8) samples
            m = p_intv_samples.match(line)
            if m:
                cases_data.setdefault(case, {})['ReqIntervalSum'] = float(m.group(1))
                all_cols.add('ReqIntervalSum')
                continue

            # 9) 0-10
            m = p_intv_digit.match(line)
            if m:
                idx, raw = m.groups()
                idx = int(idx)
                if 0 <= idx <= 10:
                    col = f'{idx}IntervalSum'
                    cases_data.setdefault(case, {})[col] = float(raw)
                    all_cols.add(col)
                continue

            #10) overflows
            m = p_intv_ovfl.match(line)
            if m:
                cases_data.setdefault(case, {})['overflowsIntervalSum'] = float(m.group(1))
                all_cols.add('overflowsIntervalSum')
                continue
            #11) LSUMSHR arrive counters (16 / 32 entry)
            m = p_lsumshr.match(line)
            if m:
                suffix, raw = m.groups()          # suffix = '16entryNum' 或 '32entryNum'
                col = f'LSUMSHRarrive{suffix}'    # → 'LSUMSHRarrive16entryNum'
                cases_data.setdefault(case, {})[col] = float(raw)
                all_cols.add(col)                 # 让它自动出现在表头
                continue
            #12) dcache overallMisses::total
            m = p_overall_miss.match(line)
            if m:
                col = 'dcacheMSHRMisses_total'       # 列名自定义，也可去掉前缀
                cases_data.setdefault(case, {})[col] = float(m.group(1))
                all_cols.add(col)
                continue
            #13) dcache overallHits::total
            m = p_overall_hit.match(line)
            if m:
                col = 'dcacheMSHRHits_total'       # 列名自定义，也可去掉前缀
                cases_data.setdefault(case, {})[col] = float(m.group(1))
                all_cols.add(col)
                continue

# ------------ 其它列分类 ------------
num_cols  = [c for c in all_cols if c.endswith('ReqNum')]
lat_cols  = [c for c in all_cols if c.endswith('ReqLatencyAll')]

# 总表头：原指标 + 计数列 + 百分比列 + TotalNum / TotalLatencyAll
metric_cols = sorted(all_cols - set(cnt_cols))      # 原指标（不含 interval 计数）
header = metric_cols + cnt_cols + pct_cols + ['TotalNum', 'TotalLatencyAll']

# ------------ 汇总 ------------
col_sum = {c: 0.0 for c in all_cols}
for data in cases_data.values():
    for c in all_cols:
        col_sum[c] += data.get(c, 0.0)

samples_total = col_sum.get('ReqIntervalSum', 0.0)
total_num_sum = sum(col_sum[c] for c in num_cols)
total_lat_sum = sum(col_sum[c] for c in lat_cols)

# ------------ 写 CSV ------------
os.makedirs(OUTPUT_DIR, exist_ok=True)
csv_path = os.path.join(OUTPUT_DIR, OUTPUT_FILE)
with open(csv_path, 'w', newline='') as f:
    w = csv.writer(f)
    w.writerow(['Case'] + header)

    # --- Sum 行 ---
    sum_row = ['Sum']
    for col in header:
        if col in pct_cols:
            base_col = cnt_cols[pct_cols.index(col)]
            pct = (col_sum[base_col] / samples_total * 100) if samples_total else 0
            sum_row.append(f'{pct:.2f}%')
        elif col == 'TotalNum':
            sum_row.append(f'{int(total_num_sum)}')
        elif col == 'TotalLatencyAll':
            sum_row.append(f'{total_lat_sum:.6f}')
        elif col.endswith('ReqLatencyAll'):
            sum_row.append(f'{col_sum[col]:.6f}')
        else:                     # 计数 or 其它
            sum_row.append(f'{int(col_sum[col])}')
    w.writerow(sum_row)

    # --- 每 case 行 ---
    for case in sorted(cases_data):
        data = cases_data[case]
        row = [case]
        samples_case = data.get('ReqIntervalSum', 0.0)
        for col in header:
            if col in pct_cols:
                base_col = cnt_cols[pct_cols.index(col)]
                pct = (data.get(base_col, 0.0) / samples_case * 100) if samples_case else 0
                row.append(f'{pct:.2f}%')
            elif col == 'TotalNum':
                row.append(f'{int(sum(data.get(c,0) for c in num_cols))}')
            elif col == 'TotalLatencyAll':
                row.append(f'{sum(data.get(c,0) for c in lat_cols):.6f}')
            elif col.endswith('ReqLatencyAll'):
                row.append(f'{data.get(col,0):.6f}')
            else:
                row.append(f'{int(data.get(col,0))}')
        w.writerow(row)

print('✅ CSV saved:', csv_path)

# ------------ 饼状图（Sum 行百分比） ------------
labels = [str(i) for i in range(11)] + ['overflow']
pct_vals = [(col_sum[cnt_cols[i]] / samples_total * 100 if samples_total else 0)
            for i in range(12)]

plt.figure(figsize=(6,6))
plt.pie(
    pct_vals,
    labels = labels,
    autopct = '%1.1f%%',
    startangle = 90,
    counterclock = False
)
plt.title('Distribution of L1MissReqInterval (L1prefetch)')
plt.tight_layout()

fig_path = os.path.join(OUTPUT_DIR, FIG_FILE.replace('.png','_pie.png'))
plt.savefig(fig_path, dpi=200)
plt.close()
print('✅ Pie chart saved:', fig_path)

