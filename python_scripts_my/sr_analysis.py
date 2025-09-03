#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import re
import sys
import argparse
from collections import defaultdict, deque, namedtuple
import math
import csv

# 可选前缀："timestamp: component: "
PREFIX = r'^\s*(?:\d+:\s+[^:]+:\s+)?'

BeginPat   = re.compile(PREFIX + r'pc\s+([0-9a-fA-F]+)\s+Begin;\s+tage pred:(\d+),\s+lsum:([+-]?\d+)\s*$')
ContribPat = re.compile(PREFIX + r'pc\s+([0-9a-fA-F]+),\s+([A-Za-z]+):([+-]?\d+)\s*$')
EndPat     = re.compile(PREFIX + r'pc\s+([0-9a-fA-F]+)\s+End;\s+tage pred:(\d+),\s+lsum:([+-]?\d+),\s+thres:([+-]?\d+)\s*$')
ConfPat    = re.compile(PREFIX + r'pc\s+([0-9a-fA-F]+),\s+highConf:(\d+),\s+medConf:(\d+),\s+lowConf:(\d+),\s+firstH:([+-]?\d+),\s+secondH:([+-]?\d+)\s*$')
FinalPat   = re.compile(PREFIX + r'pc\s+([0-9a-fA-F]+)\s+Final Pred;\s+taken\s*=\s*(\d+),\s+useScPred\s*=\s*(\d+)\s*$')
UpdatePat  = re.compile(PREFIX + r'pc\s+([0-9a-fA-F]+)\s+Update;\s+taken\s*=\s*(\d+)\s*$')

def tage_to_taken(tage_pred_val):
    # gem5 日志：tage pred: 0 => taken, 1 => not taken
    return 1 if int(tage_pred_val) == 0 else 0

def sc_pred_from_lsum(lsum):
    # 统一约定：lsum>0 记为 1，否则 0（与之前脚本一致）
    return 1 if lsum > 0 else 0

def bucket_region(abs_lsum, thres):
    if thres is None or thres <= 0:
        return -1
    if abs_lsum < thres / 4.0:
        return 0
    elif abs_lsum < thres / 2.0:
        return 1
    else:
        return 2

class Block:
    __slots__ = (
        'id', 'pc',
        'tage_pred_begin', 'lsum_begin',
        'contribs',
        'tage_pred_end', 'lsum_end', 'thres',
        'highConf', 'medConf', 'lowConf',
        'firstH', 'secondH',
        'final_pred', 'use_sc'
    )
    def __init__(self, id, pc):
        self.id = id
        self.pc = pc
        self.tage_pred_begin = None
        self.lsum_begin = None
        self.contribs = {}
        self.tage_pred_end = None
        self.lsum_end = None
        self.thres = None
        self.highConf = None
        self.medConf = None
        self.lowConf = None
        self.firstH = None
        self.secondH = None
        self.final_pred = None   # chooser 最终预测位（日志中的 "Final Pred; taken"）
        self.use_sc = None       # 日志中的 useScPred

def make_sample(blk, taken_true):
    contribs = dict(blk.contribs)
    sr_val = contribs.get('SR', 0)
    lsum_with_sr = blk.lsum_end
    lsum_no_sr = (lsum_with_sr - sr_val) if lsum_with_sr is not None else None
    sum_abs_others = sum(abs(v) for k,v in contribs.items() if k != 'SR')

    tage_taken_end = tage_to_taken(blk.tage_pred_end) if blk.tage_pred_end is not None else None
    sc_with_sr = sc_pred_from_lsum(lsum_with_sr) if lsum_with_sr is not None else None
    sc_no_sr = sc_pred_from_lsum(lsum_no_sr) if lsum_no_sr is not None else None

    return {
        'id': blk.id,
        'pc': blk.pc,
        'taken_true': taken_true,               # Update 的真实 outcome
        'tage_taken_end': tage_taken_end,       # TAGE 决策（End 行）
        'sc_pred_with_sr': sc_with_sr,          # 统计矫正器输出（含 SR）
        'sc_pred_no_sr': sc_no_sr,              # 去掉 SR 后的 SC 预测
        'final_pred_logged': blk.final_pred,    # chooser 最终预测（日志）
        'use_sc_logged': blk.use_sc,            # 日志 useScPred
        'lsum_begin': blk.lsum_begin,
        'lsum_end': lsum_with_sr,
        'lsum_no_sr': lsum_no_sr,
        'thres': blk.thres,
        'sr_val': sr_val,
        'sum_abs_others': sum_abs_others,
        'highConf': blk.highConf,
        'medConf': blk.medConf,
        'lowConf': blk.lowConf,
        'firstH': blk.firstH,
        'secondH': blk.secondH,
        'contribs': contribs
    }

def parse_log(fp, verbose=False):
    # 当前还未 finalize 的块（等待 FinalPred 或 Update）
    cur_block_map = {}  # pc -> Block
    block_id = 0

    # 待与 Update 配对的 finalized 块（End/Conf/Final 已齐或部分齐）
    pending_blocks = defaultdict(deque)   # pc -> deque[Block]
    pending_updates = defaultdict(deque)  # pc -> deque[int]
    samples = []

    diag = {
        'begin_lines': 0, 'contrib_lines': 0, 'end_lines': 0,
        'conf_lines': 0, 'final_lines': 0, 'update_lines': 0,
        'blocks_emitted': 0, 'samples_collected': 0,
        'per_pc_end': defaultdict(int), 'per_pc_update': defaultdict(int),
        'final_missing': 0
    }

    def ensure_block(pc):
        nonlocal block_id
        if pc not in cur_block_map:
            block_id += 1
            cur_block_map[pc] = Block(block_id, pc)
        return cur_block_map[pc]

    def finalize_block(pc):
        # 将 cur_block_map[pc] 推入 pending_blocks，并清除当前块
        if pc in cur_block_map:
            pending_blocks[pc].append(cur_block_map.pop(pc))
            diag['blocks_emitted'] += 1

    for raw in fp:
        line = raw.rstrip('\n')

        m = BeginPat.match(line)
        if m:
            diag['begin_lines'] += 1
            pc, tage_pred, lsum_begin = m.group(1), int(m.group(2)), int(m.group(3))
            b = ensure_block(pc)
            b.tage_pred_begin = tage_pred
            b.lsum_begin = lsum_begin
            continue

        m = ContribPat.match(line)
        if m:
            diag['contrib_lines'] += 1
            pc, key, val = m.group(1), m.group(2), int(m.group(3))
            b = ensure_block(pc)
            b.contribs[key] = b.contribs.get(key, 0) + val
            continue

        m = EndPat.match(line)
        if m:
            diag['end_lines'] += 1
            pc, tage_pred_end, lsum_end, thres = m.group(1), int(m.group(2)), int(m.group(3)), int(m.group(4))
            diag['per_pc_end'][pc] += 1
            b = ensure_block(pc)
            b.tage_pred_end = tage_pred_end
            b.lsum_end = lsum_end
            b.thres = thres
            # 先不 finalize，等待 Conf/Final 行；若后面没有 Final 行，也能在 Update 时补 finalize
            continue

        m = ConfPat.match(line)
        if m:
            diag['conf_lines'] += 1
            pc = m.group(1)
            b = ensure_block(pc)
            b.highConf = int(m.group(2))
            b.medConf  = int(m.group(3))
            b.lowConf  = int(m.group(4))
            b.firstH   = int(m.group(5))
            b.secondH  = int(m.group(6))
            continue

        m = FinalPat.match(line)
        if m:
            diag['final_lines'] += 1
            pc, final_pred, use_sc = m.group(1), int(m.group(2)), int(m.group(3))
            b = ensure_block(pc)
            b.final_pred = final_pred
            b.use_sc = use_sc
            # 此时信息齐全，可以 finalize
            finalize_block(pc)
            # 若已有对应 pc 的 Update，立刻配对
            if pending_updates[pc]:
                taken_true = pending_updates[pc].popleft()
                blk = pending_blocks[pc].popleft()
                samples.append(make_sample(blk, taken_true))
                diag['samples_collected'] += 1
            continue

        m = UpdatePat.match(line)
        if m:
            diag['update_lines'] += 1
            pc, taken_true = m.group(1), int(m.group(2))
            diag['per_pc_update'][pc] += 1
            # 若当前块尚未 finalize（比如没等到 Final 行），此处先 finalize
            if pc in cur_block_map:
                finalize_block(pc)
            if pending_blocks[pc]:
                blk = pending_blocks[pc].popleft()
                samples.append(make_sample(blk, taken_true))
                diag['samples_collected'] += 1
            else:
                pending_updates[pc].append(taken_true)
            continue

        # 其他行忽略

    # 统计“End 存在但 Final 缺失”的情况（只能在 Update 处强行 finalize 过）
    for pc, dq in pending_blocks.items():
        for blk in dq:
            if blk.final_pred is None:
                diag['final_missing'] += 1

    return samples, diag

# ---------- 评估与策略模拟 ----------

def eval_basics(samples):
    out = {}
    n = len(samples)
    if n == 0:
        out['n'] = 0
        return out

    # 基础准确率
    acc_tage = 0
    acc_sc_with = 0
    acc_sc_no = 0
    acc_final = 0
    cnt_tage = 0
    cnt_sc_with = 0
    cnt_sc_no = 0
    cnt_final = 0

    use_sc_1 = 0
    final_acc_use_sc_1 = 0
    use_sc_0 = 0
    final_acc_use_sc_0 = 0

    # SR 效果（只在 sc 可计算的样本上）
    flip_help = flip_harm = flip_neutral = 0

    # 置信度与量级桶
    reg_total = [0,0,0]
    reg_help = [0,0,0]
    reg_harm = [0,0,0]

    conf_total = {'high':0, 'med':0, 'low':0}
    conf_help  = {'high':0, 'med':0, 'low':0}
    conf_harm  = {'high':0, 'med':0, 'low':0}

    # 仅在 useScPred=1 的作用面上评 SR
    use1_help = use1_harm = use1_total = 0

    # PC 级净伤害（useScPred=1 且 SR 触发 flip）
    pc_net_harm = defaultdict(int)

    avg_sr_abs_ratio = 0.0
    sr_missing = 0
    sr_sign_correct = 0
    sr_sign_total = 0

    for s in samples:
        t = s['taken_true']

        # TAGE
        if s['tage_taken_end'] is not None:
            cnt_tage += 1
            acc_tage += 1 if s['tage_taken_end'] == t else 0

        # SC with/no SR
        if s['sc_pred_with_sr'] is not None:
            cnt_sc_with += 1
            acc_sc_with += 1 if s['sc_pred_with_sr'] == t else 0
        if s['sc_pred_no_sr'] is not None:
            cnt_sc_no += 1
            acc_sc_no += 1 if s['sc_pred_no_sr'] == t else 0

        # Final（chooser 后）
        if s['final_pred_logged'] is not None:
            cnt_final += 1
            acc_final += 1 if s['final_pred_logged'] == t else 0

        # useSc 分开统计最终准确率
        if s['final_pred_logged'] is not None and s['use_sc_logged'] is not None:
            if s['use_sc_logged'] == 1:
                use_sc_1 += 1
                final_acc_use_sc_1 += 1 if s['final_pred_logged'] == t else 0
            else:
                use_sc_0 += 1
                final_acc_use_sc_0 += 1 if s['final_pred_logged'] == t else 0

        # SR flip 统计
        if s['sc_pred_with_sr'] is not None and s['sc_pred_no_sr'] is not None:
            if s['sr_val'] == 0:
                sr_missing += 1
            else:
                sr_sign_total += 1
                sr_vote = 1 if s['sr_val'] > 0 else 0
                sr_sign_correct += 1 if sr_vote == t else 0

            if s['sc_pred_with_sr'] != s['sc_pred_no_sr']:
                if s['sc_pred_with_sr'] == t and s['sc_pred_no_sr'] != t:
                    flip_help += 1
                    # 若 chooser 实际采用 sc，这个帮助才会反映到最终预测
                    if s['use_sc_logged'] == 1:
                        use1_help += 1
                        pc_net_harm[s['pc']] -= 1
                elif s['sc_pred_with_sr'] != t and s['sc_pred_no_sr'] == t:
                    flip_harm += 1
                    if s['use_sc_logged'] == 1:
                        use1_harm += 1
                        pc_net_harm[s['pc']] += 1
                else:
                    flip_neutral += 1

        # 量级与置信度分桶
        if s['lsum_end'] is not None and s['thres'] is not None:
            reg = bucket_region(abs(s['lsum_end']), s['thres'])
            if reg >= 0:
                reg_total[reg] += 1
                if s['sc_pred_with_sr'] is not None and s['sc_pred_no_sr'] is not None:
                    if s['sc_pred_with_sr'] != s['sc_pred_no_sr']:
                        if s['sc_pred_with_sr'] == t and s['sc_pred_no_sr'] != t:
                            reg_help[reg] += 1
                        elif s['sc_pred_with_sr'] != t and s['sc_pred_no_sr'] == t:
                            reg_harm[reg] += 1

        # 置信度三档（以单独标志为准）
        def conf_key(s):
            if s['highConf'] == 1:
                return 'high'
            if s['lowConf'] == 1:
                return 'low'
            if s['medConf'] == 1:
                return 'med'
            return None
        ck = conf_key(s)
        if ck is not None:
            conf_total[ck] += 1
            if s['sc_pred_with_sr'] is not None and s['sc_pred_no_sr'] is not None:
                if s['sc_pred_with_sr'] != s['sc_pred_no_sr']:
                    if s['sc_pred_with_sr'] == t and s['sc_pred_no_sr'] != t:
                        conf_help[ck] += 1
                    elif s['sc_pred_with_sr'] != t and s['sc_pred_no_sr'] == t:
                        conf_harm[ck] += 1

        # |SR| 相对其它贡献
        denom = abs(s['sum_abs_others']) if s['sum_abs_others'] is not None else 0.0
        avg_sr_abs_ratio += (abs(s['sr_val']) / (denom + 1e-9))

    out['n'] = n
    out['acc'] = {
        'tage_end': (acc_tage / cnt_tage) if cnt_tage else None,
        'sc_with_sr': (acc_sc_with / cnt_sc_with) if cnt_sc_with else None,
        'sc_no_sr': (acc_sc_no / cnt_sc_no) if cnt_sc_no else None,
        'final_logged': (acc_final / cnt_final) if cnt_final else None,
        'final_use_sc_1': (final_acc_use_sc_1 / use_sc_1) if use_sc_1 else None,
        'final_use_sc_0': (final_acc_use_sc_0 / use_sc_0) if use_sc_0 else None,
    }
    out['counts'] = {
        'cnt_tage': cnt_tage, 'cnt_sc_with': cnt_sc_with, 'cnt_sc_no': cnt_sc_no,
        'cnt_final': cnt_final, 'use_sc_1': use_sc_1, 'use_sc_0': use_sc_0
    }
    out['sr_effect'] = {
        'flip_help': flip_help, 'flip_harm': flip_harm, 'flip_neutral': flip_neutral,
        'use1_help': use1_help, 'use1_harm': use1_harm
    }
    out['buckets'] = {
        'region_total': reg_total, 'region_help': reg_help, 'region_harm': reg_harm,
        'conf_total': conf_total, 'conf_help': conf_help, 'conf_harm': conf_harm
    }
    out['sr_quality'] = {
        'sr_missing': sr_missing,
        'sr_sign_acc': (sr_sign_correct / sr_sign_total) if sr_sign_total else None,
        'avg_sr_abs_ratio': (avg_sr_abs_ratio / n) if n else None
    }
    out['pc_net_harm'] = pc_net_harm
    return out

# 门控策略：返回 (use_sc_boolean) 的函数
def policy_baseline(s):
    # 使用日志中 useScPred 的决策；若缺失，则默认当做 chooser 不用 sc
    return bool(s['use_sc_logged'] == 1)

def policy_only_lowconf(s):
    return bool(s['lowConf'] == 1)

def policy_lowconf_thres_q1(s):
    if s['lowConf'] != 1 or s['thres'] in (None, 0) or s['lsum_no_sr'] is None:
        return False
    return abs(s['lsum_no_sr']) < (s['thres'] / 4.0)

def policy_lowconf_sr_align(s):
    # SR 与无 SR 的和同号 => SR 将 lsum 推离 0，视为“方向一致”
    if s['lowConf'] != 1 or s['lsum_no_sr'] is None:
        return False
    return (s['sr_val'] * s['lsum_no_sr']) > 0

POLICIES = {
    'baseline': policy_baseline,
    'only_lowconf': policy_only_lowconf,
    'lowconf_thresQ1': policy_lowconf_thres_q1,
    'lowconf_sr_align': policy_lowconf_sr_align,
}

def simulate_policy(samples, policy_name, alpha=1.0):
    pol = POLICIES[policy_name]
    total = 0
    corr = 0
    use_sc_cnt = 0
    use_sc_corr = 0
    # 记录在策略作用面上，SR 帮助/伤害（仅当 sc 决策会被采用时才有意义）
    help_cnt = harm_cnt = 0

    for s in samples:
        t = s['taken_true']
        if s['tage_taken_end'] is None or s['lsum_no_sr'] is None or s['lsum_end'] is None:
            continue

        total += 1
        # 应用策略：是否采用 sc
        use_sc = pol(s)
        # alpha 缩放后的 sc 预测（可用于扫描）
        sc_alpha_pred = 1 if (s['lsum_no_sr'] + alpha * s['sr_val']) > 0 else 0

        if use_sc:
            use_sc_cnt += 1
            pred = sc_alpha_pred
            use_sc_corr += 1 if pred == t else 0

            # 记录 SR 的 flip 帮助/伤害（与 alpha 保持一致）
            sc_no = 1 if s['lsum_no_sr'] > 0 else 0
            if sc_alpha_pred != sc_no:
                if sc_alpha_pred == t and sc_no != t:
                    help_cnt += 1
                elif sc_alpha_pred != t and sc_no == t:
                    harm_cnt += 1
        else:
            pred = s['tage_taken_end']

        corr += 1 if pred == t else 0

    acc = (corr / total) if total else None
    use_acc = (use_sc_corr / use_sc_cnt) if use_sc_cnt else None
    return {
        'policy': policy_name,
        'alpha': alpha,
        'total': total,
        'acc': acc,
        'use_sc_cnt': use_sc_cnt,
        'use_sc_acc': use_acc,
        'help': help_cnt,
        'harm': harm_cnt
    }

def alpha_scan_policy(samples, policy_name, alphas):
    res = []
    for a in alphas:
        res.append(simulate_policy(samples, policy_name, alpha=a))
    # 按准确率降序、其次按 alpha 升序
    res.sort(key=lambda x: (-(x['acc'] if x['acc'] is not None else -1), x['alpha']))
    return res

def write_csv(samples, path):
    # 导出诊断友好的 CSV
    keys = [
        'id','pc','taken_true',
        'tage_taken_end','final_pred_logged','use_sc_logged',
        'sc_pred_with_sr','sc_pred_no_sr',
        'lsum_begin','lsum_end','lsum_no_sr','thres',
        'sr_val','sum_abs_others',
        'highConf','medConf','lowConf','firstH','secondH'
    ]
    # 收集所有 contrib 项作为列
    all_keys = set()
    for s in samples:
        all_keys |= set(s['contribs'].keys())
    contrib_cols = sorted(all_keys)

    with open(path, 'w', newline='') as f:
        w = csv.writer(f)
        w.writerow(keys + [f'c_{k}' for k in contrib_cols])
        for s in samples:
            row = [s.get(k, '') for k in keys]
            row += [s['contribs'].get(k, 0) for k in contrib_cols]
            w.writerow(row)

def main():
    ap = argparse.ArgumentParser(description='Analyze SR corrector impact with chooser/confidence.')
    ap.add_argument('logfile', help='path to .log')
    ap.add_argument('--alphas', default='0,0.25,0.5,0.75,1.0,1.25,1.5,2.0', help='comma-separated alphas for SR scaling')
    ap.add_argument('--csv', default='', help='optional CSV output path')
    ap.add_argument('--diag', action='store_true', help='print parsing diagnostics')
    ap.add_argument('--policies', default='baseline,only_lowconf,lowconf_thresQ1,lowconf_sr_align', help='comma-separated policies to simulate')
    ap.add_argument('--topN', type=int, default=10, help='top-N harmful PCs to list')
    args = ap.parse_args()

    alphas = [float(x) for x in args.alphas.split(',')]
    with open(args.logfile, 'r', encoding='utf-8', errors='ignore') as f:
        samples, diag = parse_log(f, verbose=args.diag)

    print(f'Parsed samples (paired by pc End/Final + Update): {len(samples)}')
    if args.diag:
        print('--- Parse diagnostics ---')
        print(f"Begin lines: {diag['begin_lines']}")
        print(f"Contrib lines: {diag['contrib_lines']}")
        print(f"End lines: {diag['end_lines']}")
        print(f"Conf lines: {diag['conf_lines']}")
        print(f"Final lines: {diag['final_lines']}")
        print(f"Update lines: {diag['update_lines']}")
        print(f"Blocks emitted: {diag['blocks_emitted']}")
        print(f"Samples collected: {diag['samples_collected']}")
        print(f"Blocks with missing Final: {diag['final_missing']}")
        bad_pcs = []
        for pc, cnt_end in diag['per_pc_end'].items():
            if cnt_end > 0 and diag['per_pc_update'].get(pc, 0) == 0:
                bad_pcs.append(pc)
        if bad_pcs:
            print(f"PCs with End but no Update (top 10): {bad_pcs[:10]}")

    if not samples:
        return

    if args.csv:
        write_csv(samples, args.csv)
        print(f'Wrote CSV to {args.csv}')

    # 基础评估
    base = eval_basics(samples)
    m_acc = base['acc']
    m_cnt = base['counts']
    sr_eff = base['sr_effect']
    buckets = base['buckets']
    srq = base['sr_quality']

    print('=== Baseline summary ===')
    print(f"Total samples: {base['n']}")
    print(f"TAGE end accuracy: {m_acc['tage_end']:.4f}" if m_acc['tage_end'] is not None else "TAGE end accuracy: N/A")
    print(f"SC accuracy (with SR): {m_acc['sc_with_sr']:.4f}" if m_acc['sc_with_sr'] is not None else "SC accuracy (with SR): N/A")
    print(f"SC accuracy (no SR):  {m_acc['sc_no_sr']:.4f}" if m_acc['sc_no_sr'] is not None else "SC accuracy (no SR): N/A")
    print(f"Final (chooser) accuracy: {m_acc['final_logged']:.4f}" if m_acc['final_logged'] is not None else "Final (chooser) accuracy: N/A")
    print(f"  Final acc | useSc=1: {m_acc['final_use_sc_1']:.4f} ({m_cnt['use_sc_1']})" if m_acc['final_use_sc_1'] is not None else "  Final acc | useSc=1: N/A")
    print(f"  Final acc | useSc=0: {m_acc['final_use_sc_0']:.4f} ({m_cnt['use_sc_0']})" if m_acc['final_use_sc_0'] is not None else "  Final acc | useSc=0: N/A")

    print('SR flip effects (SC decision only):')
    print(f"  flip_help={sr_eff['flip_help']}, flip_harm={sr_eff['flip_harm']}, neutral={sr_eff['flip_neutral']}")
    print(f"  (when useSc=1) help={sr_eff['use1_help']}, harm={sr_eff['use1_harm']}")

    print('SR quality:')
    print(f"  SR missing: {srq['sr_missing']}")
    if srq['sr_sign_acc'] is not None:
        print(f"  SR sign-only accuracy: {srq['sr_sign_acc']:.4f}")
    if srq['avg_sr_abs_ratio'] is not None:
        print(f"  Avg |SR| / |others|: {srq['avg_sr_abs_ratio']:.4f}")

    print('Buckets by |lsum_with_SR| vs thres:')
    names = ['|lsum|<thres/4', 'thres/4≤|lsum|<thres/2', '≥thres/2']
    for i, name in enumerate(names):
        tot = buckets['region_total'][i]
        helpc = buckets['region_help'][i]
        harmc = buckets['region_harm'][i]
        print(f"  {name}: total={tot}, help={helpc}, harm={harmc}")

    print('Confidence buckets:')
    for k in ['high','med','low']:
        tot = buckets['conf_total'][k]
        helpc = buckets['conf_help'][k]
        harmc = buckets['conf_harm'][k]
        print(f"  {k:>4}: total={tot}, help={helpc}, harm={harmc}")

    # 有害 PC Top-N
    harm_list = sorted(base['pc_net_harm'].items(), key=lambda x: (-x[1], x[0]))
    topN = args.topN if args.topN > 0 else 10
    if harm_list:
        print(f'Top-{topN} harmful PCs (useSc=1 & SR flips):')
        for pc, score in harm_list[:topN]:
            print(f'  pc={pc}, net_harm={score}')

    # 门控策略模拟 + α 扫描
    policies = [p.strip() for p in args.policies.split(',') if p.strip()]
    print('=== Policy simulations (alpha=1.0) ===')
    for p in policies:
        r = simulate_policy(samples, p, alpha=1.0)
        if r['acc'] is None:
            print(f'  {p}: N/A')
        else:
            print(f"  {p}: acc={r['acc']:.4f}, used_sc={r['use_sc_cnt']}/{r['total']}, used_sc_acc={r['use_sc_acc']:.4f} help={r['help']} harm={r['harm']}")

    print('=== Alpha scan under baseline policy (best first) ===')
    alpha_res = alpha_scan_policy(samples, 'baseline', alphas)
    for item in alpha_res[:10]:
        print(f"  alpha={item['alpha']:.3f} -> acc={item['acc']:.4f} (used_sc={item['use_sc_cnt']})")

    # 在“仅 lowconf”策略下也扫一遍，看看更贴近弱区时最优 α
    print('=== Alpha scan under only_lowconf policy (best first) ===')
    alpha_res2 = alpha_scan_policy(samples, 'only_lowconf', alphas)
    for item in alpha_res2[:10]:
        print(f"  alpha={item['alpha']:.3f} -> acc={item['acc']:.4f} (used_sc={item['use_sc_cnt']})")

if __name__ == '__main__':
    main()
