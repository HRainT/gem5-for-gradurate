import os
from collections import Counter

def count_benchmarks(directory_path):
    # 检查路径是否存在
    if not os.path.exists(directory_path):
        print(f"错误: 找不到路径 '{directory_path}'")
        return

    # 初始化计数器
    benchmark_counts = Counter()
    total_folders = 0

    print(f"正在扫描目录: {directory_path} ...\n")

    try:
        # 获取目录下所有内容
        items = os.listdir(directory_path)

        for item in items:
            full_path = os.path.join(directory_path, item)

            # 只处理文件夹
            if os.path.isdir(full_path):
                # 核心逻辑：
                # 假设文件夹名为 'astar_BigLakes_93'
                # 使用 split('_')[0] 获取第一个下划线前的部分，即 'astar'
                # 如果文件夹名中没有下划线（例如 'gcc'），它会返回整个名字
                bench_name = item.split('_')[0]
                
                benchmark_counts[bench_name] += 1
                total_folders += 1

        # 打印结果表格
        print(f"{'Benchmark 名称':<20} | {'数量':<10}")
        print("-" * 35)
        
        # 按名称字母顺序排序输出
        for name, count in sorted(benchmark_counts.items()):
            print(f"{name:<20} | {count:<10}")
            
        print("-" * 35)
        print(f"总计文件夹数量: {total_folders}")

    except Exception as e:
        print(f"发生错误: {e}")

# --- 使用说明 ---
# 请将下面的路径修改为你实际的文件夹路径
# 如果脚本放在和 'tage_sc_l_speculative' 同级目录下，可以直接填文件夹名
target_directory = '/Data2/yutong.han/graduate/tage_sc_l_speculative' 

if __name__ == "__main__":
    count_benchmarks(target_directory)