import os
import re
import sys
from collections import defaultdict

def parse_stats_file(file_path):
    """解析stats.txt文件，提取所需数据"""
    data = {}
    patterns = {
        'decode': r'^system\.cpu\.bpuMissDecodeCount\s+(\d+)',
        'commit': r'^system\.cpu\.bpuMissCommitCount\s+(\d+)',
        'retired': r'^system\.cpu\.retiredBranchInsts\s+(\d+)',
        'miss_rate': r'^system\.cpu\.bpuMissRate\s+([\d.]+)'
    }
    
    try:
        with open(file_path, 'r') as f:
            for line in f:
                line = line.strip()
                for key, pattern in patterns.items():
                    match = re.match(pattern, line)
                    if match:
                        value = float(match.group(1))
                        data[key] = value
                        break
        return data
    except Exception as e:
        print(f"Error processing {file_path}: {str(e)}")
        return None

def main(input_dir):
    # 存储分组数据 {group_name: [decode_sum, commit_sum, retired_sum]}
    group_data = defaultdict(lambda: [0, 0, 0])
    # 存储单个文件夹的miss rate数据 [(folder, miss_rate)]
    individual_miss_rates = []
    
    # 全局累加变量
    total_decode = 0
    total_commit = 0
    total_retired = 0

    # 遍历输入目录
    for folder in os.listdir(input_dir):
        folder_path = os.path.join(input_dir, folder)
        if not os.path.isdir(folder_path):
            continue
            
        # 提取组名前缀（第一个_前的部分）
        parts = folder.split('_')
        if len(parts) < 2:  # 只需要至少有一个下划线
            print(f"Skipping folder with invalid format: {folder}")
            continue
            
        group_name = parts[0]
        
        # 解析stats文件
        stats_file = os.path.join(folder_path, 'stats.txt')
        if not os.path.exists(stats_file):
            print(f"Stats file missing in {folder}")
            continue
            
        data = parse_stats_file(stats_file)
        if not data:
            print(f"Failed to parse stats in {folder}")
            continue
            
        # 更新分组数据（需要全部三个字段）
        if 'decode' in data and 'commit' in data and 'retired' in data:
            # 分组累加
            group_data[group_name][0] += data['decode']
            group_data[group_name][1] += data['commit']
            group_data[group_name][2] += data['retired']
            
            # 全局累加
            total_decode += data['decode']
            total_commit += data['commit']
            total_retired += data['retired']
        else:
            print(f"Missing required fields in {folder}")
        
        # 存储单个文件夹的miss rate（只需要miss_rate字段）
        if 'miss_rate' in data:
            individual_miss_rates.append((folder, data['miss_rate']))
        else:
            print(f"Missing bpuMissRate in {folder}")
    
    # 计算并输出总体bpuMissRate
    if total_retired > 0:
        overall_miss_rate = (total_decode + total_commit) / total_retired
    else:
        overall_miss_rate = 0.0
    
    print(f"\nOverall bpuMissRate (calculated from all folders): {overall_miss_rate:.6f}")
    
    # 任务1：计算并打印各组的bpuMissRate
    print("\nGroup bpuMissRate (calculated from sums):")
    for group, (decode_sum, commit_sum, retired_sum) in group_data.items():
        if retired_sum == 0:
            miss_rate = 0.0
        else:
            miss_rate = (decode_sum + commit_sum) / retired_sum
        print(f"{group}: {miss_rate:.6f}")
    
    # 任务2：打印最大的10个bpuMissRate
    if not individual_miss_rates:
        print("\nNo valid bpuMissRate data found")
        return
        
    print("\nTop 10 highest bpuMissRate (directly from stats.txt):")
    individual_miss_rates.sort(key=lambda x: x[1], reverse=True)
    for i, (folder, rate) in enumerate(individual_miss_rates[:10], 1):
        print(f"{i}. {folder}: {rate}")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python process_data.py <input_directory>")
        sys.exit(1)
    
    input_directory = sys.argv[1]
    if not os.path.isdir(input_directory):
        print(f"Error: {input_directory} is not a valid directory")
        sys.exit(1)
    
    main(input_directory)
