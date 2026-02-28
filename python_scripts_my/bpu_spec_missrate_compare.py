import os
import re
import sys
from collections import defaultdict

def parse_stats_file(file_path):
    """解析stats.txt文件,提取所需数据"""
    data = {}
    # 正则匹配 bpu1MissRate
    patterns = {
        'miss_rate': r'^system\.cpu\.bpu1MissRate\s+([\d.]+)'
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

def get_dir_miss_rates(input_dir):
    """
    遍历指定目录，提取所有子文件夹中的miss rate
    返回字典: {folder_name: miss_rate}
    """
    results = {}
    
    if not os.path.exists(input_dir):
        print(f"Warning: Directory {input_dir} does not exist.")
        return results

    print(f"Scanning directory: {input_dir} ...")
    
    for folder in os.listdir(input_dir):
        folder_path = os.path.join(input_dir, folder)
        if not os.path.isdir(folder_path):
            continue
        
        stats_file = os.path.join(folder_path, 'stats.txt')
        if not os.path.exists(stats_file):
            continue
            
        data = parse_stats_file(stats_file)
        if data and 'miss_rate' in data:
            results[folder] = data['miss_rate']
            
    print(f"  Found {len(results)} valid benchmarks.")
    return results

def main(dir_a, dir_b, output_file):
    # 1. 获取数据 (进度信息保留在终端打印)
    rates_a = get_dir_miss_rates(dir_a)
    rates_b = get_dir_miss_rates(dir_b)
    
    # 2. 找出共有的Benchmarks
    common_benchmarks = set(rates_a.keys()) & set(rates_b.keys())
    
    if not common_benchmarks:
        print("\nError: No common benchmarks found. Nothing to write.")
        return

    print(f"Comparing {len(common_benchmarks)} common benchmarks...")
    
    # 准备数据
    comparisons = []
    for name in common_benchmarks:
        r_a = rates_a[name]
        r_b = rates_b[name]
        diff = r_a - r_b # diff < 0 means A is better
        comparisons.append({
            'name': name,
            'rate_a': r_a,
            'rate_b': r_b,
            'diff': diff
        })

    # 排序
    # A 优于 B (diff 为负且越小越好)
    a_better = sorted([c for c in comparisons if c['diff'] < 0], key=lambda x: x['diff'])
    # B 优于 A (diff 为正且越大越好)
    b_better = sorted([c for c in comparisons if c['diff'] > 0], key=lambda x: x['diff'], reverse=True)

    # 3. 将结果写入文件
    try:
        with open(output_file, 'w') as f:
            # 写入头部信息
            f.write(f"Comparison Report\n")
            f.write(f"=================\n")
            f.write(f"Result A (Dir): {dir_a}\n")
            f.write(f"Result B (Dir): {dir_b}\n")
            f.write(f"Total Common Benchmarks: {len(common_benchmarks)}\n\n")

            # --- A 比 B 好的部分 ---
            f.write("-" * 90 + "\n")
            f.write(f"Top 5 Benchmarks where Result A is better than Result B (Lower Miss Rate)\n")
            f.write(f"{'Benchmark':<35} | {'Rate A':<12} | {'Rate B':<12} | {'Improvement (B-A)':<12}\n")
            f.write("-" * 90 + "\n")
            
            if not a_better:
                f.write("None found.\n")
            else:
                for item in a_better[:5]:
                    improvement = item['rate_b'] - item['rate_a']
                    f.write(f"{item['name']:<35} | {item['rate_a']:.6f}     | {item['rate_b']:.6f}     | -{improvement:.6f}\n")
            f.write("\n")

            # --- B 比 A 好的部分 ---
            f.write("-" * 90 + "\n")
            f.write(f"Top 5 Benchmarks where Result B is better than Result A (Lower Miss Rate)\n")
            f.write(f"{'Benchmark':<35} | {'Rate A':<12} | {'Rate B':<12} | {'Improvement (A-B)':<12}\n")
            f.write("-" * 90 + "\n")
            
            if not b_better:
                f.write("None found.\n")
            else:
                for item in b_better[:5]:
                    improvement = item['rate_a'] - item['rate_b']
                    f.write(f"{item['name']:<35} | {item['rate_a']:.6f}     | {item['rate_b']:.6f}     | -{improvement:.6f}\n")
            
            f.write("-" * 90 + "\n")

        print(f"\nSuccess! Results have been saved to: {output_file}")

    except IOError as e:
        print(f"Error writing to file {output_file}: {e}")

if __name__ == "__main__":
    # 参数检查
    if len(sys.argv) < 3 or len(sys.argv) > 4:
        print("Usage: python process_compare.py <dir_A> <dir_B> [output_filename]")
        print("Example: python process_compare.py ./run1 ./run2 my_report.txt")
        sys.exit(1)
    
    dir_a = sys.argv[1]
    dir_b = sys.argv[2]
    
    # 获取输出文件名，如果没提供则使用默认值
    output_filename = "comparison_results.txt"
    if len(sys.argv) == 4:
        output_filename = sys.argv[3]
    
    if not os.path.isdir(dir_a) or not os.path.isdir(dir_b):
        print("Error: One or both input paths are not valid directories.")
        sys.exit(1)
    
    main(dir_a, dir_b, output_filename)