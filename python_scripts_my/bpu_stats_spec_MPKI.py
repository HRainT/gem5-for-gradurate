import os
import re
import sys
from collections import defaultdict

def parse_stats_file(file_path):
    """
    解析stats.txt文件。
    查找 system.cpu.branchPred.condIncorrect 或 system.cpu.bpu1MissCommitCount。
    找到其中任意一个即返回数值。
    """
    target_keys = [
        "system.cpu.branchPred.condIncorrect",
        "system.cpu.bpu1MissCommitCount"
    ]
    
    try:
        with open(file_path, 'r') as f:
            for line in f:
                line = line.strip()
                for key in target_keys:
                    # 构造正则：关键字 + 空白 + 数字
                    escaped_key = re.escape(key)
                    pattern = rf'^{escaped_key}\s+(\d+)'
                    
                    match = re.search(pattern, line)
                    if match:
                        return int(match.group(1))
        return None
    except Exception as e:
        print(f"Error processing {file_path}: {str(e)}")
        return None

def main(input_dir):
    # 存储分组数据 {group_name: {'sum': 0, 'count': 0}}
    group_data = defaultdict(lambda: {'sum': 0, 'count': 0})
    # 存储单个文件夹的MPKI数据 [(folder, mpki)]
    individual_mpkis = []
    
    # 全局累加器 (用于计算 Overall MPKI)
    total_global_sum = 0
    total_global_count = 0
    
    # 定义分母常量
    DENOMINATOR_UNIT = 20000

    # 遍历输入目录
    for folder in os.listdir(input_dir):
        folder_path = os.path.join(input_dir, folder)
        if not os.path.isdir(folder_path):
            continue
            
        # 提取组名前缀（第一个_前的部分）
        parts = folder.split('_')
        if len(parts) < 2:
            # print(f"Skipping folder with invalid format: {folder}")
            continue
            
        group_name = parts[0]
        
        # 解析stats文件
        stats_file = os.path.join(folder_path, 'stats.txt')
        if not os.path.exists(stats_file):
            print(f"Stats file missing in {folder}")
            continue
            
        # 获取关键字数值
        incorrect_count = parse_stats_file(stats_file)
        
        if incorrect_count is None:
            print(f"Target keywords not found in {folder}")
            continue
            
        # 1. 计算单个 Benchmark 的 MPKI 并存储
        mpki = incorrect_count / DENOMINATOR_UNIT
        individual_mpkis.append((folder, mpki))
        
        # 2. 累加分组数据
        group_data[group_name]['sum'] += incorrect_count
        group_data[group_name]['count'] += 1
        
        # 3. 累加全局数据
        total_global_sum += incorrect_count
        total_global_count += 1
    
    if total_global_count == 0:
        print("No valid benchmark data found.")
        return

    # --- 输出部分 ---

    # 任务0：计算并输出 Overall MPKI
    # 公式：所有值之和 / (总数量 * 40000)
    overall_mpki = total_global_sum / (total_global_count * DENOMINATOR_UNIT)
    print(f"\nOverall MPKI: {overall_mpki:.6f}")
    print(f"(Calculated from {total_global_count} benchmarks)")

    # 任务1：各组的 MPKI
    print("\nGroup MPKI (Group Sum / (Group Count * 20000)):")
    sorted_groups = sorted(group_data.keys())
    for group in sorted_groups:
        data = group_data[group]
        total_val = data['sum']
        count = data['count']
        
        if count > 0:
            group_mpki = total_val / (count * DENOMINATOR_UNIT)
        else:
            group_mpki = 0.0
            
        print(f"{group}: {group_mpki:.6f}")
    
    # 任务2：打印最大的10个 MPKI
    print("\nTop 10 highest MPKI:")
    individual_mpkis.sort(key=lambda x: x[1], reverse=True)
    for i, (folder, val) in enumerate(individual_mpkis[:10], 1):
        print(f"{i}. {folder}: {val:.6f}")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python process_data.py <input_directory>")
        sys.exit(1)
    
    input_directory = sys.argv[1]
    if not os.path.isdir(input_directory):
        print(f"Error: {input_directory} is not a valid directory")
        sys.exit(1)
    
    main(input_directory)