import re
import sys
from collections import defaultdict

def extract_pc_counts(log_file):
    # 定义新的正则表达式模式
    branch_net_pattern = r"Use BranchNet for BN PC (0x[0-9a-fA-F]+)"
    tage_pattern = r"Use Tage for BN PC (0x[0-9a-fA-F]+)"
    mispredict_patterns = {
        'execute': r"PC: (0x[0-9a-fA-F]+) Execute: BranchNet mispredict\.",
        'decode': r"PC: (0x[0-9a-fA-F]+) Decode: BranchNet mispredict\."
    }
    
    # 初始化计数器
    branch_net_counts = defaultdict(int)
    tage_counts = defaultdict(int)
    mispredict_counts = defaultdict(lambda: defaultdict(int))
    
    try:
        with open(log_file, 'r') as file:
            for line in file:
                # 匹配BranchNet预测
                bn_match = re.search(branch_net_pattern, line)
                if bn_match:
                    pc = bn_match.group(1).lower()
                    branch_net_counts[pc] += 1
                
                # 匹配Tage预测
                tage_match = re.search(tage_pattern, line)
                if tage_match:
                    pc = tage_match.group(1).lower()
                    tage_counts[pc] += 1
                
                # 匹配不同类型的mispredict
                for m_type, pattern in mispredict_patterns.items():
                    mispred_match = re.search(pattern, line)
                    if mispred_match:
                        pc = mispred_match.group(1).lower()
                        mispredict_counts[pc][m_type] += 1
                        mispredict_counts[pc]['total'] += 1
                    
        return branch_net_counts, tage_counts, mispredict_counts
        
    except FileNotFoundError:
        print(f"错误：文件 '{log_file}' 未找到")
        sys.exit(1)

def print_results(bn_counts, tage_counts, mispred_counts):
    # 打印BranchNet预测统计
    print("BranchNet预测 PC 统计:")
    if not bn_counts:
        print("  无匹配数据")
    else:
        for pc, count in sorted(bn_counts.items()):
            print(f"  {pc}: {count}次")
    
    # 打印Tage预测统计
    print("\nTage预测 PC 统计:")
    if not tage_counts:
        print("  无匹配数据")
    else:
        for pc, count in sorted(tage_counts.items()):
            print(f"  {pc}: {count}次")
    
    # 打印mispredict统计
    print("\nBranchNet mispredict PC 统计:")
    if not mispred_counts:
        print("  无匹配数据")
    else:
        # 计算全局总数
        total_exec = sum(m['execute'] for m in mispred_counts.values())
        total_decode = sum(m['decode'] for m in mispred_counts.values())
        total_mispred = total_exec + total_decode
        
        # 打印全局统计
        print(f"全局统计: Execute mispredicts = {total_exec} ({total_exec/total_mispred:.2%}), "
              f"Decode mispredicts = {total_decode} ({total_decode/total_mispred:.2%})")
        
        # 打印每个PC的统计
        for pc, counts in sorted(mispred_counts.items()):
            total = counts['total']
            exec_ratio = counts['execute'] / total if total > 0 else 0
            decode_ratio = counts['decode'] / total if total > 0 else 0
            
            details = []
            if counts['execute'] > 0:
                details.append(f"Execute: {counts['execute']}次 ({exec_ratio:.2%})")
            if counts['decode'] > 0:
                details.append(f"Decode: {counts['decode']}次 ({decode_ratio:.2%})")
            
            print(f"  {pc}: {total}次 ({', '.join(details)})")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("用法: python script.py <log文件路径>")
        sys.exit(1)
    
    log_path = sys.argv[1]
    bn_counts, tage_counts, mispred_counts = extract_pc_counts(log_path)
    print_results(bn_counts, tage_counts, mispred_counts)