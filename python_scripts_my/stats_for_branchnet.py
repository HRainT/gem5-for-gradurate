import re
import sys
from collections import defaultdict

def extract_pc_counts(log_file):
    # 定义正则表达式模式
    prediction_pattern = r"BranchNet prediction for PC (0x[0-9a-fA-F]+)"
    mispredict_pattern = r"PC: (0x[0-9a-fA-F]+) Execute: BranchNet mispredict\."
    
    # 初始化计数器
    prediction_counts = defaultdict(int)
    mispredict_counts = defaultdict(int)
    
    try:
        with open(log_file, 'r') as file:
            for line in file:
                # 匹配BranchNet prediction
                pred_match = re.search(prediction_pattern, line)
                if pred_match:
                    pc = pred_match.group(1).lower()  # 统一转为小写
                    prediction_counts[pc] += 1
                
                # 匹配BranchNet mispredict
                mispred_match = re.search(mispredict_pattern, line)
                if mispred_match:
                    pc = mispred_match.group(1).lower()  # 统一转为小写
                    mispredict_counts[pc] += 1
                    
        return prediction_counts, mispredict_counts
        
    except FileNotFoundError:
        print(f"错误：文件 '{log_file}' 未找到")
        sys.exit(1)

def print_results(pred_counts, mispred_counts):
    print("BranchNet prediction PC 统计:")
    if not pred_counts:
        print("  无匹配数据")
    else:
        for pc, count in sorted(pred_counts.items()):
            print(f"  {pc}: {count}次")
    
    print("\nBranchNet mispredict PC 统计:")
    if not mispred_counts:
        print("  无匹配数据")
    else:
        for pc, count in sorted(mispred_counts.items()):
            print(f"  {pc}: {count}次")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("用法: python script.py <log文件路径>")
        sys.exit(1)
    
    log_path = sys.argv[1]
    pred_counts, mispred_counts = extract_pc_counts(log_path)
    print_results(pred_counts, mispred_counts)