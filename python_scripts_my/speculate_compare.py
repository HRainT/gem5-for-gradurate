import os
import re
import random
import argparse
import matplotlib.pyplot as plt
import numpy as np

def extract_bpu_miss_rate(file_path):
    """从stats.txt文件中提取bpu1MissRate值"""
    try:
        with open(file_path, 'r') as f:
            for line in f:
                if 'system.cpu.bpu1MissRate' in line:
                    match = re.search(r'system\.cpu\.bpu1MissRate\s+([\d.eE+-]+)', line)
                    if match:
                        return float(match.group(1))
    except Exception as e:
        print(f"Error reading {file_path}: {e}")
    return None

def collect_benchmark_data(folder):
    """收集文件夹中所有benchmark的数据（每个benchmark随机选一个代表）"""
    benchmark_data = {}
    
    for root, dirs, files in os.walk(folder):
        if 'stats.txt' in files:
            parent_dir = os.path.basename(root)
            benchmark_name = re.sub(r'_\d+$', '', parent_dir)
            
            if benchmark_name not in benchmark_data:
                file_path = os.path.join(root, 'stats.txt')
                miss_rate = extract_bpu_miss_rate(file_path)
                if miss_rate is not None:
                    benchmark_data[benchmark_name] = miss_rate
    return benchmark_data

def plot_comparison(data1, data2, label1, label2):
    """绘制两个数据集的对比柱状图"""
    common_benchmarks = sorted(set(data1.keys()) & set(data2.keys()))
    
    if not common_benchmarks:
        print("No common benchmarks found!")
        return
    
    values1 = [data1[bm] for bm in common_benchmarks]
    values2 = [data2[bm] for bm in common_benchmarks]
    differences = [v2 - v1 for v1, v2 in zip(values1, values2)]
    
    # 设置图形
    plt.figure(figsize=(14, 8))
    bar_width = 0.35
    index = np.arange(len(common_benchmarks))
    
    # 绘制柱状图
    bars1 = plt.bar(index, values1, bar_width, label=label1)
    bars2 = plt.bar(index + bar_width, values2, bar_width, label=label2)
    
    # 计算更紧凑的y轴范围
    max_value = max(max(values1), max(values2))
    min_value = min(min(values1), min(values2))
    max_diff = max(abs(d) for d in differences)
    
    # 动态计算y轴范围 - 更紧凑的布局
    # 基础范围：数据最大值 + 10%的缓冲空间
    # 额外空间：考虑差异标签高度（最大差异的50%）
    y_upper = max_value * 1.15 + max_diff * 0.3
    # 确保图表不会太紧凑，保留最小空间
    if y_upper < max_value * 1.25:
        y_upper = max_value * 1.25
    
    # 设置y轴范围
    plt.ylim(bottom=min_value*0.9, top=y_upper)
    
    # 添加数值标签（确保标签在图表区域内）
    for i, (v1, v2) in enumerate(zip(values1, values2)):
        diff = v2 - v1
        # 确定标签位置（在较高柱子顶部上方）
        label_y = max(v1, v2) + max_diff * 0.05
        
        # 如果标签位置接近图表顶部，调整到柱子内部
        if label_y > y_upper * 0.95:
            label_y = max(v1, v2) * 0.98  # 放在柱子顶部下方
        
        # 使用白色背景提高可读性
        plt.text(index[i] + bar_width/2, label_y, f"{diff:+.3f}", 
                 ha='center', fontsize=9, color='red',
                 bbox=dict(facecolor='white', alpha=0.8, edgecolor='none', pad=1))
    
    # 美化图表
    plt.xlabel('Benchmarks')
    plt.ylabel('BPU Miss Rate')
    plt.title('BPU Miss Rate Comparison')
    plt.xticks(index + bar_width/2, common_benchmarks, rotation=45, ha='right')
    plt.legend()
    plt.grid(axis='y', linestyle='--', alpha=0.7)
    plt.tight_layout()
    
    # 保存并显示
    plt.savefig('bpu_miss_rate_comparison.png', dpi=300, bbox_inches='tight')
    plt.show()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Compare BPU Miss Rates between two folders')
    parser.add_argument('folder1', help='First input folder path')
    parser.add_argument('folder2', help='Second input folder path')
    args = parser.parse_args()
    
    # 收集数据
    data1 = collect_benchmark_data(args.folder1)
    data2 = collect_benchmark_data(args.folder2)
    
    # 打印统计数据
    print(f"Found {len(data1)} benchmarks in {args.folder1}")
    print(f"Found {len(data2)} benchmarks in {args.folder2}")
    print(f"Common benchmarks: {len(set(data1.keys()) & set(data2.keys()))}")
    
    # 绘制对比图
    plot_comparison(data1, data2, 
                   os.path.basename(args.folder1), 
                   os.path.basename(args.folder2))
