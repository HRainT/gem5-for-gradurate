import os
import re
import sys
from collections import defaultdict

def parse_stats_file(file_path):
    """
    解析stats.txt文件,提取所需数据。
    如果找不到主要的BPU关键字，则回退查找branchPred关键字并进行转换。
    """
    data = {}
    # 定义两组匹配模式
    patterns = {
        # 第一组关键字 (Primary)
        'decode': r'^system\.cpu\.bpu1MissDecodeCount\s+(\d+)',
        'commit': r'^system\.cpu\.bpu1MissCommitCount\s+(\d+)',
        'retired': r'^system\.cpu\.retiredBranchInsts\s+(\d+)',
        'miss_rate': r'^system\.cpu\.bpu1MissRate\s+([\d.]+)',
        
        # 第二组关键字 (Secondary / Fallback)
        'condIncorrect': r'^system\.cpu\.branchPred\.condIncorrect\s+(\d+)',
        'condPredicted': r'^system\.cpu\.branchPred\.condPredicted\s+(\d+)'
    }
    
    try:
        with open(file_path, 'r') as f:
            for line in f:
                line = line.strip()
                # 遍历所有正则进行匹配
                for key, pattern in patterns.items():
                    if key not in data:
                        match = re.match(pattern, line)
                        if match:
                            value = float(match.group(1))
                            data[key] = value

        # --- 数据归一化逻辑 ---
        
        # 1. 处理 Retired 指令数 (分母)
        if 'retired' not in data and 'condPredicted' in data:
            data['retired'] = data['condPredicted']

        # 2. 处理 Decode 和 Commit 错误数 (分子 = decode + commit)
        # 如果主要关键字缺失，但有 condIncorrect，将其赋值给 decode，commit 设为 0
        if ('decode' not in data or 'commit' not in data) and 'condIncorrect' in data:
            data['decode'] = data['condIncorrect']
            data['commit'] = 0.0

        # 3. 处理 Miss Rate
        if 'miss_rate' not in data:
            if 'condIncorrect' in data and 'condPredicted' in data:
                if data['condPredicted'] > 0:
                    data['miss_rate'] = data['condIncorrect'] / data['condPredicted']
                else:
                    data['miss_rate'] = 0.0

        return data
    except Exception as e:
        print(f"Error processing {file_path}: {str(e)}")
        return None

def main(input_dir):
    # 存储分组数据 {group_name: [decode_sum, commit_sum, retired_sum]}
    group_data = defaultdict(lambda: [0, 0, 0])
    # 存储单个文件夹的miss rate数据
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
            
        parts = folder.split('_')
        if len(parts) < 2: 
            # print(f"Skipping folder with invalid format: {folder}") 
            continue
            
        group_name = parts[0]
        
        stats_file = os.path.join(folder_path, 'stats.txt')
        if not os.path.exists(stats_file):
            print(f"Stats file missing in {folder}")
            continue
            
        data = parse_stats_file(stats_file)
        if not data:
            continue
            
        # 更新分组数据
        if 'decode' in data and 'commit' in data and 'retired' in data:
            group_data[group_name][0] += data['decode']
            group_data[group_name][1] += data['commit']
            group_data[group_name][2] += data['retired']
            
            total_decode += data['decode']
            total_commit += data['commit']
            total_retired += data['retired']
        
        # 存储单个miss rate
        if 'miss_rate' in data:
            individual_miss_rates.append((folder, data['miss_rate']))
    
    # --- 打印部分修改 ---

    # 1. 计算总体 Miss Rate
    total_misses_global = total_decode + total_commit
    if total_retired > 0:
        overall_miss_rate = total_misses_global / total_retired
    else:
        overall_miss_rate = 0.0
    
    print(f"\n{'='*80}")
    print(f"Overall Statistics")
    print(f"{'='*80}")
    print(f"Total Misses   : {int(total_misses_global)}")
    print(f"Total Branches : {int(total_retired)}")
    print(f"Overall Rate   : {overall_miss_rate:.6f}")
    
    # 2. 打印各组详细数据 (带分子分母)
    print(f"\n{'='*80}")
    print(f"Group Statistics (Summed per benchmark type)")
    print(f"{'='*80}")
    # 打印表头
    print(f"{'Benchmark Group':<20} | {'Miss Rate':<10} | {'Total Misses':<15} | {'Total Branches':<15}")
    print(f"{'-'*20} | {'-'*10} | {'-'*15} | {'-'*15}")

    for group in sorted(group_data.keys()):
        decode_sum, commit_sum, retired_sum = group_data[group]
        
        # 分子 = Decode错误 + Commit错误
        current_misses = decode_sum + commit_sum
        
        if retired_sum == 0:
            miss_rate = 0.0
        else:
            miss_rate = current_misses / retired_sum
            
        # 打印一行数据
        print(f"{group:<20} | {miss_rate:.6f}   | {int(current_misses):<15} | {int(retired_sum):<15}")
    
    # 3. 打印Top 10 (保持原样，只打印rate)
    if individual_miss_rates:
        print(f"\n{'='*80}")
        print("Top 10 highest bpuMissRate (Individual Folders)")
        print(f"{'='*80}")
        individual_miss_rates.sort(key=lambda x: x[1], reverse=True)
        for i, (folder, rate) in enumerate(individual_miss_rates[:10], 1):
            print(f"{i}. {folder:<30} : {rate:.6f}")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python process_data.py <input_directory>")
        sys.exit(1)
    
    input_directory = sys.argv[1]
    if not os.path.isdir(input_directory):
        print(f"Error: {input_directory} is not a valid directory")
        sys.exit(1)
    
    main(input_directory)