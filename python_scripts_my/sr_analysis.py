#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import re
import sys
import argparse
from collections import defaultdict, Counter

# Regex patterns (新增 SC Begin 和 bias)
pat_status = re.compile(r'pc ([0-9a-fA-F]+), highConf:(\d), medConf:(\d), lowConf:(\d), firstH:(-?\d+), secondH:(-?\d+)')
pat_final  = re.compile(r'pc ([0-9a-fA-F]+) Final Pred; taken = (\d), useScPred = (\d)')
pat_begin  = re.compile(r'pc ([0-9a-fA-F]+) Begin; tage pred:(-?\d+), lsum:(-?\d+)')
pat_sc_beg = re.compile(r'pc ([0-9a-fA-F]+) SC Begin;')
pat_bias   = re.compile(r'pc ([0-9a-fA-F]+),\sbias:\s(-?\d+)')
pat_mod    = re.compile(r'pc ([0-9a-fA-F]+), (bwm|wp|lm|sm|tm|imm|wi|SR):(-?\d+)')
pat_end    = re.compile(r'pc ([0-9a-fA-F]+) End; tage pred:(-?\d+), lsum:(-?\d+), thres:(-?\d+)')
pat_update = re.compile(r'pc ([0-9a-fA-F]+) Update; taken = (\d)')

MODULE_KEYS = ["bwm","wp","lm","sm","tm","imm","wi","SR"]  # 增量模块；bias 已改为绝对量

class Sample:
    __slots__ = ("pc",
                 "begin_seen","tage_pred","lsum_begin",
                 "sc_begin_seen","bias_abs","mods",
                 "status_seen","high","med","low","firstH","secondH",
                 "end_seen","lsum_end","thres",
                 "final_seen","final_pred","used_sc",
                 "update_seen","taken","lines")
    def __init__(self, pc):
        self.pc = pc
        self.begin_seen = False
        self.tage_pred = None
        self.lsum_begin = None
        self.sc_begin_seen = False
        self.bias_abs = None         # bias 行打印的是“绝对 lsum”
        self.mods = {}               # 其他模块增量：bwm/wp/lm/sm/tm/imm/wi/SR
        self.status_seen = False
        self.high = self.med = self.low = None
        self.firstH = self.secondH = None
        self.end_seen = False
        self.lsum_end = None
        self.thres = None
        self.final_seen = False
        self.final_pred = None
        self.used_sc = None
        self.update_seen = False
        self.taken = None
        self.lines = []

def finalize_sample(s: Sample, stats: Counter, opts):
    # 必须 End + Update + TAGE 三者齐
    if not (s.end_seen and s.update_seen and s.tage_pred is not None):
        stats["incomplete"] += 1
        return

    # SC(with SR) 直接用 End 的 lsum
    lsum_with = s.lsum_end

    # SC(no SR) = End.lsum - SR 增量
    has_sr = ("SR" in s.mods)
    if has_sr:
        sr_val = s.mods["SR"]
        lsum_no = lsum_with - sr_val
    else:
        sr_val = None
        lsum_no = None
        stats["sr_missing"] += 1

    # unknown_delta 诊断
    # 若 bias_abs 存在：unknown = End - (bias_abs + Σ增量模块)
    # 若 bias_abs 不存在：降级为 End - (Begin + Σ模块)，兼容老日志
    if s.bias_abs is not None:
        sum_known = s.bias_abs + sum(v for k, v in s.mods.items())
    else:
        sum_known = (s.lsum_begin or 0) + sum(v for k, v in s.mods.items())
    unknown_delta = s.lsum_end - sum_known
    if unknown_delta != 0:
        stats["unknown_delta_cnt"] += 1
        stats["unknown_delta_sum"] += unknown_delta
        if abs(unknown_delta) <= 2:   stats["unknown_delta_small"] += 1
        elif abs(unknown_delta) <= 16: stats["unknown_delta_mid"] += 1
        else:                         stats["unknown_delta_large"] += 1

    # 基础判定
    sc_with = 1 if lsum_with >= 0 else 0
    sc_no   = (1 if (lsum_no is not None and lsum_no >= 0) else None)
    tage    = 1 if int(s.tage_pred) != 0 else 0

    # Final（chooser）
    if s.final_seen and s.final_pred is not None:
        ok = (int(s.final_pred) == int(s.taken))
        stats["final_total"] += 1
        stats["final_correct"] += int(ok)
        if s.used_sc is not None:
            if s.used_sc:
                stats["final_used_sc"] += 1
                stats["final_used_sc_ok"] += int(ok)
            else:
                stats["final_not_sc"] += 1
                stats["final_not_sc_ok"] += int(ok)

    # TAGE-only
    stats["tage_total"] += 1
    stats["tage_correct"] += int(tage == s.taken)

    # SC(with SR)
    stats["sc_with_total"] += 1
    stats["sc_with_correct"] += int(sc_with == s.taken)

    # SC(no SR) + flip 统计
    if sc_no is not None:
        stats["sc_no_total"] += 1
        stats["sc_no_correct"] += int(sc_no == s.taken)

        if sc_with != sc_no:
            if sc_with == s.taken:
                stats["flip_help"] += 1
                if s.used_sc: stats["flip_help_useSc"] += 1
            else:
                stats["flip_harm"] += 1
                if s.used_sc: stats["flip_harm_useSc"] += 1

        # SR 方向相关性（仅诊断）
        stats["sr_sign_total"] += 1
        stats["sr_sign_correct"] += int((1 if sr_val >= 0 else 0) == s.taken)
        # 强度比
        stats["sr_ratio_sum"] += abs(sr_val) / (abs(lsum_no) + 1e-9)
        stats["sr_ratio_cnt"] += 1

        # |lsum_with| vs thres 分桶
        if s.thres is not None and s.thres != 0:
            lw = abs(lsum_with); T = abs(s.thres)
            b = "q3"
            if lw < T/4: b = "q1"
            elif lw < T/2: b = "q2"
            stats[f"bucket_{b}_total"] += 1
            if sc_with != sc_no:
                if sc_with == s.taken: stats[f"bucket_{b}_help"] += 1
                else:                  stats[f"bucket_{b}_harm"] += 1

    # 置信度分桶（flip 统计）
    if s.status_seen:
        if s.high == 1:
            stats["conf_high_total"] += 1
            if sc_no is not None and sc_with != sc_no:
                stats["conf_high_help"] += int(sc_with == s.taken)
                stats["conf_high_harm"] += int(sc_with != s.taken)
        if s.med == 1:
            stats["conf_med_total"] += 1
            if sc_no is not None and sc_with != sc_no:
                stats["conf_med_help"] += int(sc_with == s.taken)
                stats["conf_med_harm"] += int(sc_with != s.taken)
        if s.low == 1:
            stats["conf_low_total"] += 1
            if sc_no is not None and sc_with != sc_no:
                stats["conf_low_help"] += int(sc_with == s.taken)
                stats["conf_low_harm"] += int(sc_with != s.taken)

    # harmful PCs（useSc=1 & flip）
    if (s.used_sc == 1) and (sc_no is not None) and (sc_with != sc_no):
        stats["pc_net"][s.pc] += (1 if sc_with != s.taken else -1)

    # Bias-only 统计（新增）
    if s.bias_abs is not None:
        stats["bias_total"] += 1
        stats["bias_correct"] += int((1 if s.bias_abs >= 0 else 0) == s.taken)
        if s.thres is not None and s.thres != 0:
            lb = abs(s.bias_abs); T = abs(s.thres)
            bb = "bq3"
            if lb < T/4: bb = "bq1"
            elif lb < T/2: bb = "bq2"
            stats[f"{bb}_tot"] += 1
            stats[f"{bb}_ok"] += int((1 if s.bias_abs >= 0 else 0) == s.taken)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("logfile")
    ap.add_argument("--alpha-scan", action="store_true", dest="alpha_scan")
    ap.add_argument("--policies", action="store_true", dest="policies")
    args = ap.parse_args()

    state = {}
    stats = Counter()
    stats["pc_net"] = Counter()

    with open(args.logfile, "r", errors="ignore") as f:
        for ln, line in enumerate(f, 1):
            line = line.strip()
            if not line: continue

            m = pat_begin.search(line)
            if m:
                pc = m.group(1).lower()
                s = state.get(pc)
                if s is None:
                    s = Sample(pc); state[pc] = s
                else:
                    if s.end_seen or s.update_seen:
                        finalize_sample(s, stats, args)
                    state[pc] = Sample(pc); s = state[pc]
                s.begin_seen = True
                s.tage_pred = int(m.group(2))
                s.lsum_begin = int(m.group(3))
                s.lines.append(ln); continue

            m = pat_sc_beg.search(line)
            if m:
                pc = m.group(1).lower()
                s = state.get(pc); s = s or Sample(pc)
                state[pc] = s
                s.sc_begin_seen = True
                s.lines.append(ln); continue

            m = pat_bias.search(line)
            if m:
                pc = m.group(1).lower()
                s = state.get(pc); s = s or Sample(pc)
                state[pc] = s
                s.bias_abs = int(m.group(2))  # 绝对值
                s.lines.append(ln); continue

            m = pat_mod.search(line)
            if m:
                pc = m.group(1).lower()
                s = state.get(pc); s = s or Sample(pc)
                state[pc] = s
                mod = m.group(2); val = int(m.group(3))
                s.mods[mod] = val          # 增量值
                s.lines.append(ln); continue

            m = pat_end.search(line)
            if m:
                pc = m.group(1).lower()
                s = state.get(pc); s = s or Sample(pc)
                state[pc] = s
                s.end_seen = True
                s.lsum_end = int(m.group(3))
                s.thres = int(m.group(4))
                s.lines.append(ln); continue

            m = pat_status.search(line)
            if m:
                pc = m.group(1).lower()
                s = state.get(pc); s = s or Sample(pc)
                state[pc] = s
                s.status_seen = True
                s.high = int(m.group(2)); s.med = int(m.group(3)); s.low = int(m.group(4))
                s.firstH = int(m.group(5)); s.secondH = int(m.group(6))
                s.lines.append(ln); continue

            m = pat_final.search(line)
            if m:
                pc = m.group(1).lower()
                s = state.get(pc); s = s or Sample(pc)
                state[pc] = s
                s.final_seen = True
                s.final_pred = int(m.group(2))
                s.used_sc    = int(m.group(3))
                s.lines.append(ln); continue

            m = pat_update.search(line)
            if m:
                pc = m.group(1).lower()
                s = state.get(pc)
                if s is None: continue
                s.update_seen = True
                s.taken = int(m.group(2))
                s.lines.append(ln)
                finalize_sample(s, stats, args)
                state.pop(pc, None)
                continue

    # 残留样本
    for pc, s in list(state.items()):
        finalize_sample(s, stats, args)

    def acc(ok, tot): return (ok/tot if tot>0 else 0.0)
    total = stats["sc_with_total"]

    print(f"Parsed samples: {total}")
    print("=== Baseline summary ===")
    print(f"Total samples: {total}")
    print(f"TAGE-only accuracy: {acc(stats['tage_correct'], stats['tage_total']):.4f}")
    print(f"SC accuracy (with SR): {acc(stats['sc_with_correct'], stats['sc_with_total']):.4f}")
    print(f"SC accuracy (no  SR):  {acc(stats['sc_no_correct'], stats['sc_no_total']):.4f}")
    print(f"Final (chooser) accuracy: {acc(stats['final_correct'], stats['final_total']):.4f}")
    print(f"  Final acc | useSc=1: {acc(stats['final_used_sc_ok'], stats['final_used_sc']):.4f} ({stats['final_used_sc']})")
    print(f"  Final acc | useSc=0: {acc(stats['final_not_sc_ok'], stats['final_not_sc']):.4f} ({stats['final_not_sc']})")

    print("SR flip effects (SC decision only):")
    print(f"  flip_help={stats['flip_help']}, flip_harm={stats['flip_harm']}")
    print(f"  (when useSc=1) help={stats['flip_help_useSc']}, harm={stats['flip_harm_useSc']}")

    print("SR quality:")
    print(f"  SR missing: {stats['sr_missing']}")
    print(f"  SR sign-only accuracy: {acc(stats['sr_sign_correct'], stats['sr_sign_total']):.4f}")
    avg_ratio = (stats['sr_ratio_sum']/stats['sr_ratio_cnt']) if stats['sr_ratio_cnt']>0 else 0.0
    print(f"  Avg |SR| / |lsum_noSR|: {avg_ratio:.4f}")

    print("Buckets by |lsum_with_SR| vs thres:")
    for b, name in [("q1","|lsum|<thres/4"),("q2","thres/4≤|lsum|<thres/2"),("q3","≥thres/2")]:
        tot = stats[f"bucket_{b}_total"]; h = stats[f"bucket_{b}_help"]; r = stats[f"bucket_{b}_harm"]
        print(f"  {name}: total={tot}, help={h}, harm={r}")

    print("Confidence buckets (flip stats):")
    print(f"  high: total={stats['conf_high_total']}, help={stats['conf_high_help']}, harm={stats['conf_high_harm']}")
    print(f"   med: total={stats['conf_med_total']}, help={stats['conf_med_help']}, harm={stats['conf_med_harm']}")
    print(f"   low: total={stats['conf_low_total']}, help={stats['conf_low_help']}, harm={stats['conf_low_harm']}")

    # Bias-only
    print("Bias-only accuracy:")
    print(f"  bias-only acc: {acc(stats['bias_correct'], stats['bias_total']):.4f} ({stats['bias_total']})")
    print("  Bias buckets (by |bias| vs thres):")
    for b, name in [("bq1","|bias|<thres/4"),("bq2","thres/4≤|bias|<thres/2"),("bq3","≥thres/2")]:
        print(f"    {name}: acc={acc(stats[f'{b}_ok'], stats[f'{b}_tot']):.4f} ({stats[f'{b}_tot']})")

    # harmful PCs
    pc_net = stats["pc_net"]
    if pc_net:
        top = pc_net.most_common(10)
        print("Top-10 harmful PCs (useSc=1 & SR flips, net_harm):")
        for pc, harm in top:
            print(f"  pc={pc}, net_harm={harm}")

    # unknown-delta
    if stats["unknown_delta_cnt"]>0:
        print("Unknown-delta diagnostics (End.lsum - (bias_abs + Σmods) or fallback):")
        print(f"  count={stats['unknown_delta_cnt']}, sum={stats['unknown_delta_sum']}")
        print(f"  abs<=2: {stats['unknown_delta_small']}, abs<=16: {stats['unknown_delta_mid']}, abs>16: {stats['unknown_delta_large']}")

if __name__ == "__main__":
    main()
