#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
统计失败分支记录中
1) tage 正确、reg 错误
2) tage 错误、reg 正确
两类情况的出现次数及 |tage_ctr+reg_ctr| 的 10 递增分布

改进：
  • 当 reg_ctr == 0 时，reg 预测器没有参与预测 → 该条记录整体跳过
"""

import argparse
import re
from collections import defaultdict

# ------- 正则匹配 ---------
LINE_RE = re.compile(
    r"taken\?:\s*(-?\d+),\s*"
    r"tage_pred\?:\s*(-?\d+),\s*"
    r"tage_ctr:\s*(-?\d+),\s*"
    r"reg_pred\?:\s*(-?\d+),\s*"
    r"reg_ctr:\s*(-?\d+)"
)

def bucket(abs_val, width=10):
    """把绝对值映射到 0,1,2… 号桶；宽度默认 10"""
    return abs_val // width

# --------------------------

def main(log_path):
    # 计数与直方图
    cnt_tage_ok_reg_bad = 0
    cnt_tage_bad_reg_ok = 0
    hist_tage_ok_reg_bad = defaultdict(int)
    hist_tage_bad_reg_ok = defaultdict(int)

    with open(log_path, 'r', encoding='utf-8', errors='ignore') as fp:
        for line in fp:
            if "Squash for pc" not in line:
                continue

            m = LINE_RE.search(line)
            if not m:
                continue      # 格式异常

            taken, tage_pred, tage_ctr, reg_pred, reg_ctr = map(int, m.groups())

            # ---------- 新增过滤条件 ----------
            if reg_ctr == 0:
                # reg 预测器没出力，不参与比较
                continue
            # ----------------------------------

            tage_right = (tage_pred == taken)
            reg_right  = (reg_pred  == taken)

            abs_conf = abs(tage_ctr + reg_ctr)
            b = bucket(abs_conf)

            if tage_right and (not reg_right):
                cnt_tage_ok_reg_bad += 1
                hist_tage_ok_reg_bad[b] += 1
            elif reg_right and (not tage_right):
                cnt_tage_bad_reg_ok += 1
                hist_tage_bad_reg_ok[b] += 1
            # 两者都对 / 都错的情况忽略

    # ---------- 打印结果 ----------
    print("========= 统计结果 =========")
    print(f"1) tage 正确、reg 错误 : {cnt_tage_ok_reg_bad}")
    print(f"2) tage 错误、reg 正确 : {cnt_tage_bad_reg_ok}\n")

    def dump(title, hist):
        if not hist:
            print(f"{title} : 无数据\n")
            return
        print(title)
        for k in sorted(hist):
            rng = f"[{k*10:>3} ~ {k*10+9:>3}]"
            print(f"  {rng}: {hist[k]}")
        print()

    dump("tage 正确 / reg 错误 时 |tage_ctr + reg_ctr| 分布", hist_tage_ok_reg_bad)
    dump("tage 错误 / reg 正确 时 |tage_ctr + reg_ctr| 分布", hist_tage_bad_reg_ok)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description="Branch Predictor 失败日志统计 (v2)")
    parser.add_argument("logfile", help=".log 文件路径")
    args = parser.parse_args()
    main(args.logfile)
