#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import re
import sys
import argparse
from collections import defaultdict, deque, namedtuple
import math
import csv

# 可选前缀："timestamp: component: "，如 "13610000: system.cpu.branchPred.statistical_corrector: "
PREFIX = r'^\s*(?:\d+:\s+[^:]+:\s+)?'

BeginPat   = re.compile(PREFIX + r'pc\s+([0-9a-fA-F]+)\s+Begin;\s+tage pred:(\d+),\s+lsum:([+-]?\d+)\s*$')
ContribPat = re.compile(PREFIX + r'pc\s+([0-9a-fA-F]+),\s+([A-Za-z]+):([+-]?\d+)\s*$')
EndPat     = re.compile(PREFIX + r'pc\s+([0-9a-fA-F]+)\s+End;\s+tage pred:(\d+),\s+lsum:([+-]?\d+),\s+thres:([+-]?\d+)\s*$')
UpdatePat  = re.compile(PREFIX + r'pc\s+([0-9a-fA-F]+)\s+Update;\s+taken\s*=\s*(\d+)\s*$')

Block = namedtuple('Block', [
    'id', 'pc', 'tage_pred_begin', 'lsum_begin',
    'contribs', 'tage_pred_end', 'lsum_end', 'thres'
])

def tage_to_taken(tage_pred_val):
    # tage pred: 0 => taken, 1 => not taken
    return 1 if int(tage_pred_val) == 0 else 0

def bucket_region(abs_lsum, thres):
    if thres is None or thres <= 0:
        return -1
    if abs_lsum < thres / 4.0:
        return 0
    elif abs_lsum < thres / 2.0:
        return 1
    else:
        return 2

def make_sample(blk, taken):
    contribs = dict(blk.contribs)
    sr_val = contribs.get('SR', 0)
    lsum_with_sr = blk.lsum_end
    lsum_no_sr = lsum_with_sr - sr_val
    sum_abs_others = sum(abs(v) for k, v in contribs.items() if k != 'SR')

    return {
        'id': blk.id,
        'pc': blk.pc,
        'taken': taken,
        'tage_taken_end': tage_to_taken(blk.tage_pred_end) if blk.tage_pred_end is not None else None,
        'tage_taken_begin': tage_to_taken(blk.tage_pred_begin) if blk.tage_pred_begin is not None else None,
        'lsum_begin': blk.lsum_begin,
        'lsum_end': lsum_with_sr,
        'thres': blk.thres,
        'sr_val': sr_val,
        'lsum_no_sr': lsum_no_sr,
        'sum_abs_others': sum_abs_others,
        'contribs': contribs
    }

def parse_log(fp, verbose=False):
    pending_blocks = defaultdict(deque)   # pc -> deque[Block]
    pending_updates = defaultdict(deque)  # pc -> deque[taken]
    collected = []

    # 允许“无 Begin”的临时块缓存
    cur_block_map = {}  # pc -> dict
    block_id_seq = 0

    # 诊断计数
    diag = {
        'begin_lines': 0,
        'contrib_lines': 0,
        'end_lines': 0,
        'update_lines': 0,
        'blocks_emitted': 0,
        'samples_collected': 0,
        'per_pc_end': defaultdict(int),
        'per_pc_update': defaultdict(int),
    }

    for raw in fp:
        line = raw.rstrip('\n')

        m = BeginPat.match(line)
        if m:
            diag['begin_lines'] += 1
            pc, tage_pred, lsum_begin = m.group(1), int(m.group(2)), int(m.group(3))
            cur_block_map[pc] = {
                'pc': pc,
                'tage_pred_begin': tage_pred,
                'lsum_begin': lsum_begin,
                'contribs': {},
                'tage_pred_end': None,
                'lsum_end': None,
                'thres': None,
            }
            continue

        m = ContribPat.match(line)
        if m:
            diag['contrib_lines'] += 1
            pc, key, val = m.group(1), m.group(2), int(m.group(3))
            # 若尚无 Begin，也开一个临时块
            if pc not in cur_block_map:
                cur_block_map[pc] = {
                    'pc': pc,
                    'tage_pred_begin': None,
                    'lsum_begin': None,
                    'contribs': {},
                    'tage_pred_end': None,
                    'lsum_end': None,
                    'thres': None,
                }
            # 叠加贡献（同一模块可能多次出现）
            cur_block_map[pc]['contribs'][key] = cur_block_map[pc]['contribs'].get(key, 0) + val
            continue

        m = EndPat.match(line)
        if m:
            diag['end_lines'] += 1
            pc, tage_pred_end, lsum_end, thres = m.group(1), int(m.group(2)), int(m.group(3)), int(m.group(4))
            diag['per_pc_end'][pc] += 1
            # 若无当前块，也创建一个最小块
            if pc not in cur_block_map:
                cur_block_map[pc] = {
                    'pc': pc,
                    'tage_pred_begin': None,
                    'lsum_begin': None,
                    'contribs': {},
                    'tage_pred_end': None,
                    'lsum_end': None,
                    'thres': None,
                }
            b = cur_block_map.pop(pc)
            b['tage_pred_end'] = tage_pred_end
            b['lsum_end'] = lsum_end
            b['thres'] = thres

            block_id_seq += 1
            blk = Block(
                id=block_id_seq,
                pc=pc,
                tage_pred_begin=b['tage_pred_begin'],
                lsum_begin=b['lsum_begin'],
                contribs=b['contribs'],
                tage_pred_end=b['tage_pred_end'],
                lsum_end=b['lsum_end'],
                thres=b['thres']
            )

            pending_blocks[pc].append(blk)
            diag['blocks_emitted'] += 1

            # 若已存在对应 pc 的 Update，立刻配对
            if pending_updates[pc]:
                taken = pending_updates[pc].popleft()
                blk2 = pending_blocks[pc].popleft()
                collected.append(make_sample(blk2, taken))
                diag['samples_collected'] += 1
            continue

        m = UpdatePat.match(line)
        if m:
            diag['update_lines'] += 1
            pc, taken = m.group(1), int(m.group(2))
            diag['per_pc_update'][pc] += 1
            if pending_blocks[pc]:
                blk = pending_blocks[pc].popleft()
                collected.append(make_sample(blk, taken))
                diag['samples_collected'] += 1
            else:
                pending_updates[pc].append(taken)
            continue

        # 其他行忽略

    return collected, diag

def evaluate(samples, alphas):
    out = {}
    def sc_pred(lsum):
        return 1 if lsum > 0 else 0

    metrics = {
        'n': 0,
        'sr_missing': 0,
        'acc_with_sr': 0,
        'acc_no_sr': 0,
        'flip_help': 0,
        'flip_harm': 0,
        'flip_neutral': 0,
        'sr_sign_correct': 0,
        'sr_sign_acc': None,
        'tage_acc_end': 0,
        'tage_correct_then_sc_wrong_end': 0,
        'region_help': [0,0,0],
        'region_harm': [0,0,0],
        'region_total': [0,0,0],
        'avg_sr_abs_ratio': 0.0,
    }

    eps = 1e-9
    for s in samples:
        metrics['n'] += 1
        if s['sr_val'] == 0:
            metrics['sr_missing'] += 1

        with_sr = sc_pred(s['lsum_end'])
        no_sr = sc_pred(s['lsum_no_sr'])
        t = s['taken']

        metrics['acc_with_sr'] += 1 if with_sr == t else 0
        metrics['acc_no_sr']  += 1 if no_sr == t else 0

        if with_sr != no_sr:
            if with_sr == t and no_sr != t:
                metrics['flip_help'] += 1
            elif with_sr != t and no_sr == t:
                metrics['flip_harm'] += 1
            else:
                metrics['flip_neutral'] += 1

        if s['sr_val'] != 0:
            sr_vote = 1 if s['sr_val'] > 0 else 0
            metrics['sr_sign_correct'] += 1 if sr_vote == t else 0

        if s['tage_taken_end'] is not None:
            metrics['tage_acc_end'] += 1 if s['tage_taken_end'] == t else 0
            if s['tage_taken_end'] == t and with_sr != t:
                metrics['tage_correct_then_sc_wrong_end'] += 1

        reg = bucket_region(abs(s['lsum_end']), s['thres'])
        if reg >= 0:
            metrics['region_total'][reg] += 1
            if with_sr != no_sr:
                if with_sr == t and no_sr != t:
                    metrics['region_help'][reg] += 1
                elif with_sr != t and no_sr == t:
                    metrics['region_harm'][reg] += 1

        metrics['avg_sr_abs_ratio'] += abs(s['sr_val']) / (abs(s['sum_abs_others']) + eps)

    if metrics['n'] > 0:
        n = metrics['n']
        metrics['acc_with_sr'] /= n
        metrics['acc_no_sr']  /= n
        metrics['tage_acc_end'] /= n
        denom_sr = n - metrics['sr_missing']
        metrics['sr_sign_acc'] = (metrics['sr_sign_correct'] / denom_sr) if denom_sr > 0 else None
        metrics['avg_sr_abs_ratio'] /= n

    out['metrics_alpha_1'] = metrics

    # α 扫描
    alpha_result = []
    for alpha in alphas:
        corr = 0
        for s in samples:
            pred = 1 if (s['lsum_no_sr'] + alpha * s['sr_val']) > 0 else 0
            if pred == s['taken']:
                corr += 1
        acc = corr / len(samples) if samples else 0.0
        alpha_result.append((alpha, acc))
    alpha_result.sort(key=lambda x: (-x[1], x[0]))
    out['alpha_scan'] = alpha_result
    return out

def write_csv(samples, path):
    keys = [
        'id','pc','taken','tage_taken_begin','tage_taken_end',
        'lsum_begin','lsum_end','thres','sr_val','lsum_no_sr',
        'sum_abs_others'
    ]
    all_keys = set()
    for s in samples:
        all_keys |= set(s['contribs'].keys())
    contrib_cols = sorted(all_keys)
    with open(path, 'w', newline='') as f:
        import csv
        w = csv.writer(f)
        w.writerow(keys + [f'c_{k}' for k in contrib_cols])
        for s in samples:
            row = [s.get(k, '') for k in keys]
            row += [s['contribs'].get(k, 0) for k in contrib_cols]
            w.writerow(row)

def main():
    ap = argparse.ArgumentParser(description='Analyze SR weight impact from predictor logs.')
    ap.add_argument('logfile', help='path to .log')
    ap.add_argument('--alphas', default='0,0.25,0.5,0.75,1.0,1.25,1.5,2.0',
                    help='comma-separated alphas to scan for SR weight scaling')
    ap.add_argument('--csv', default='', help='optional path to write per-sample CSV')
    ap.add_argument('--diag', action='store_true', help='print parsing diagnostics')
    args = ap.parse_args()

    alphas = [float(x) for x in args.alphas.split(',')]
    with open(args.logfile, 'r', encoding='utf-8', errors='ignore') as f:
        samples, diag = parse_log(f, verbose=args.diag)

    print(f'Parsed samples (paired End+Update): {len(samples)}')
    if args.diag:
        print('--- Parse diagnostics ---')
        print(f"Begin lines: {diag['begin_lines']}")
        print(f"Contrib lines: {diag['contrib_lines']}")
        print(f"End lines: {diag['end_lines']}")
        print(f"Update lines: {diag['update_lines']}")
        print(f"Blocks emitted (End seen): {diag['blocks_emitted']}")
        print(f"Samples collected (paired): {diag['samples_collected']}")
        # 找出 End 多但 Update 少的 pc，帮助定位“配不到”的主因
        bad_pcs = []
        for pc, cnt_end in diag['per_pc_end'].items():
            cnt_upd = diag['per_pc_update'].get(pc, 0)
            if cnt_end > 0 and cnt_upd == 0:
                bad_pcs.append(pc)
        if bad_pcs:
            print(f"PCs with End but no Update (top 10): {bad_pcs[:10]}")

    if not samples:
        return

    if args.csv:
        write_csv(samples, args.csv)
        print(f'Wrote CSV to {args.csv}')

    res = evaluate(samples, alphas)
    m = res['metrics_alpha_1']
    print('=== Summary @ current weight (alpha=1.0) ===')
    print(f'Total samples: {m['n']}')
    print(f'SR missing (no SR contrib line): {m["sr_missing"]}')
    print(f'Accuracy with SR (scPred by lsum_end sign): {m["acc_with_sr"]:.4f}')
    print(f'Accuracy without SR (remove SR from lsum): {m["acc_no_sr"]:.4f}')
    print(f'Net delta (with - without): {m["acc_with_sr"] - m["acc_no_sr"]:+.4f}')
    print(f'Flip help count (SR fixes): {m["flip_help"]}')
    print(f'Flip harm count (SR breaks): {m["flip_harm"]}')
    print(f'Flip neutral count (flip but both right/wrong): {m["flip_neutral"]}')
    print(f'TAGE end accuracy: {m["tage_acc_end"]:.4f}')
    print(f'Risk: TAGE end correct but scPred(with SR) wrong: {m["tage_correct_then_sc_wrong_end"]}')
    if m["sr_sign_acc"] is not None:
        print(f'SR sign-only accuracy (sr_val sign vs taken): {m["sr_sign_acc"]:.4f}')
    print(f'Avg |SR| / |others|: {m["avg_sr_abs_ratio"]:.4f}')

    print('Chooser-aware buckets by |lsum_with_SR| vs thres:')
    buckets = ['|lsum|<thres/4', 'thres/4≤|lsum|<thres/2', '≥thres/2']
    for i, name in enumerate(buckets):
        tot = m['region_total'][i]
        helpc = m['region_help'][i]
        harmc = m['region_harm'][i]
        print(f'  {name}: total={tot}, help={helpc}, harm={harmc}')

    print('=== Alpha scan (best first) ===')
    for alpha, acc in res['alpha_scan'][:10]:
        print(f'alpha={alpha:.3f} -> acc={acc:.4f}')

if __name__ == '__main__':
    main()
