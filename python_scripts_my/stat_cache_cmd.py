#!/usr/bin/env python3
import re
from collections import defaultdict

# 定义 cmd 数字与名称的对应关系
cmd_mapping = {
    0: "InvalidCmd",
    1: "ReadReq",
    2: "ReadResp",
    3: "ReadRespWithInvalidate",
    4: "WriteReq",
    5: "WriteResp",
    6: "WriteCompleteResp",
    7: "WritebackDirty",
    8: "WritebackClean",
    9: "WriteClean",
    10: "CleanEvict",
    11: "SoftPFReq",
    12: "SoftPFExReq",
    13: "HardPFReq",
    14: "SoftPFResp",
    15: "HardPFResp",
    16: "WriteLineReq",
    17: "UpgradeReq",
    18: "SCUpgradeReq",
    19: "UpgradeResp",
    20: "SCUpgradeFailReq",
    21: "UpgradeFailResp",
    22: "ReadExReq",
    23: "ReadExResp",
    24: "ReadCleanReq",
    25: "ReadSharedReq",
    26: "LoadLockedReq",
    27: "StoreCondReq",
    28: "StoreCondFailReq",
    29: "StoreCondResp",
    30: "LockedRMWReadReq",
    31: "LockedRMWReadResp",
    32: "LockedRMWWriteReq",
    33: "LockedRMWWriteResp",
    34: "SwapReq",
    35: "SwapResp",
    38: "MemFenceReq",  # 根据 enum 定义，MemFenceReq = SwapResp + 3，所以 35 + 3 = 38
    39: "MemSyncReq",
    40: "MemSyncResp",
    41: "MemFenceResp",
    42: "CleanSharedReq",
    43: "CleanSharedResp",
    44: "CleanInvalidReq",
    45: "CleanInvalidResp",
    46: "InvalidDestError",
    47: "BadAddressError",
    48: "ReadError",
    49: "WriteError",
    50: "FunctionalReadError",
    51: "FunctionalWriteError",
    52: "PrintReq",
    53: "FlushReq",
    54: "InvalidateReq",
    55: "InvalidateResp",
    56: "HTMReq",
    57: "HTMReqResp",
    58: "HTMAbort",
    59: "TlbiExtSync",
    60: "StorePFTrain",
    61: "RxuIcacheRefill",
    62: "RxuIcacheHit",
    63: "RxuIcachePrefetch",
    64: "RxuIcachePrefetchResp"
}

def analyze_file(log_file):
    """
    分析单个日志文件。
    统计两类信息：
      1. 带有 cacheLevel 与 Req/Resp 信息的统计结果 (detailed_results)
         结构：detailed_results[cacheLevel]["Req" or "Resp"][cmd名称] = 次数
      2. 其它只包含 cmd 信息的统计结果 (generic_results)
         结构：generic_results[cmd名称] = 次数
    返回：
        detailed_results, generic_results, match_detailed, match_generic
        match_detailed 与 match_generic 分别表示是否匹配到相应信息。
    """
    detailed_results = defaultdict(lambda: {"Req": defaultdict(int), "Resp": defaultdict(int)})
    generic_results = defaultdict(int)
    # 匹配包含 cacheLevel 及 Req/Resp 的日志行，例如: "cacheLevel: 1 Req cmd: 1"
    pattern_detailed = re.compile(r"cacheLevel:\s*(\d+)\s+(Req|Resp)\s+cmd:\s*(\d+)")
    # 匹配普通的 cmd 信息，例如: "cmd: 12"
    pattern_generic = re.compile(r"cmd:\s*(\d+)")
    match_detailed = False
    match_generic = False

    try:
        with open(log_file, "r") as f:
            for line in f:
                m = pattern_detailed.search(line)
                if m:
                    match_detailed = True
                    level_str, op_type, cmd_num_str = m.groups()
                    level = int(level_str)
                    cmd_num = int(cmd_num_str)
                    cmd_name = cmd_mapping.get(cmd_num, f"UnknownCmd({cmd_num})")
                    detailed_results[level][op_type][cmd_name] += 1
                else:
                    # 若不满足详细匹配条件，则尝试通用匹配
                    m2 = pattern_generic.search(line)
                    if m2:
                        match_generic = True
                        cmd_num_str = m2.group(1)
                        cmd_num = int(cmd_num_str)
                        cmd_name = cmd_mapping.get(cmd_num, f"UnknownCmd({cmd_num})")
                        generic_results[cmd_name] += 1
    except Exception as e:
        print(f"读取文件 {log_file} 时出现错误: {e}")
    
    return detailed_results, generic_results, match_detailed, match_generic

def print_results(log_file, detailed, generic, match_detailed, match_generic):
    print(f"\n日志文件: {log_file}")
    if not match_detailed and not match_generic:
        print("  没有匹配到任何 cmd 的关键信息。")
        return

    # 打印按 cacheLevel 分组的统计结果
    if match_detailed:
        print("  按 cacheLevel 分组的统计结果:")
        for level in sorted(detailed.keys()):
            print(f"    Cache Level {level}:")
            for op_type in ["Req", "Resp"]:
                if detailed[level][op_type]:
                    print(f"      {op_type}:")
                    for cmd_name, count in detailed[level][op_type].items():
                        print(f"        {cmd_name}: {count} 次")
                else:
                    print(f"      {op_type}: 无记录")
    else:
        print("  没有匹配到包含 cacheLevel 及 Req/Resp 信息的日志行。")
    
    # 打印其它未包含 cacheLevel/Req/Resp 信息的统计结果
    if match_generic:
        print("  其它日志 (不包含 cacheLevel/Req/Resp 信息) 的统计结果:")
        for cmd_name, count in generic.items():
            print(f"    {cmd_name}: {count} 次")

def main():
    # 定义待分析的两个日志文件路径，可根据实际情况调整
    log_files = [
        "/Data3/yutong.han/riscv/rxu-gem5/out/cache/cmd_log_send_spec.txt",
        "/Data3/yutong.han/riscv/rxu-gem5/out/cache/cmd_log_spec.txt"
    ]
    
    for log_file in log_files:
        detailed, generic, match_detailed, match_generic = analyze_file(log_file)
        print_results(log_file, detailed, generic, match_detailed, match_generic)
        print("-" * 60)

if __name__ == '__main__':
    main()
