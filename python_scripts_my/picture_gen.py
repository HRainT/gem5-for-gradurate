import matplotlib.pyplot as plt
import numpy as np
import os

# ==========================================
# 1. 基础配置
# ==========================================

# 根据图片更新的 12 个 Benchmark 顺序
benchmarks = [
    'astar', 'bzip2', 'gcc', 'gobmk', 'h264ref', 
    'hmmer', 'libquantum', 'mcf', 'omnetpp', 
    'perlbench', 'sjeng', 'xalancbmk'
]

# 5种配置名称 (Legend Labels) - 用于定义全局颜色顺序
all_configs_list = [
    "Base: O3", 
    "Opt1: TAGE_SC_L", 
    "Opt2: Opt1 + L0BTB", 
    "Opt3: Opt1 + RVC", 
    "Opt4: All (Opt1+2+3)"
]

# ==========================================
# 2. 数据准备
# ==========================================

all_data = {
    # ---------------- Metric 1 ----------------
    "Miss Rate": {
        "Base: O3": [
            0.123293, 0.050364, 0.035929, 0.108327, 0.026247, 
            0.020315, 0.039698, 0.073494, 0.03409, 0.030534, 
            0.096807, 0.02461
        ], 
        "Opt1: TAGE_SC_L": [
            0.093418, 0.042241, 0.013368, 0.069168, 0.016041, 
            0.012457, 0.000061, 0.039294, 0.020478, 0.023833, 
            0.050037, 0.019706
        ],
        "Opt2: Opt1 + L0BTB": [
            0.0933, 0.04223, 0.012, 0.06667, 0.01557, 
            0.01251, 0.00021, 0.03935, 0.01819, 0.02109, 
            0.04668, 0.0182
        ],
        "Opt3: Opt1 + RVC": [
            0.09314, 0.04161, 0.01341, 0.06917, 0.01501, 
            0.0122, 0.00006, 0.03921, 0.02007, 0.0239, 
            0.04879, 0.01961
        ],
        "Opt4: All (Opt1+2+3)": [
            0.0929, 0.0414, 0.0118, 0.0664, 0.01545, 
            0.0121, 0.00006, 0.0391, 0.01805, 0.0208, 
            0.0461, 0.0181
        ]
    },
    
    # ---------------- Metric 2 ----------------
    "Recover Rate": {
        "Base: O3":             [0.86, 0.24, 0.22, 0.4, 0.04, 0.02, 0.71, 0.9, 0.43, 0.15, 0.4, 0.15],
        "Opt1: TAGE_SC_L":      [0.68, 0.21, 0.07, 0.25, 0.03, 0.02, 0, 0.56, 0.26, 0.12, 0.19, 0.11],
        "Opt2: Opt1 + L0BTB":   [0.73, 0.45, 0.13, 0.38, 0.06, 0.04, 0, 0.63, 0.37, 0.14, 0.21, 0.17],
        "Opt3: Opt1 + RVC":     [0.65, 0.19, 0.09, 0.23, 0.02, 0.02, 0, 0.51, 0.19, 0.13, 0.17, 0.1],
        "Opt4: All (Opt1+2+3)": [0.68, 0.4, 0.16, 0.35, 0.04, 0.04, 0, 0.61, 0.33, 0.15, 0.19, 0.12]
    },

    # ---------------- Metric 3 ----------------
    # 这里只保留了前三个配置的数据
    "Front Bandwidth": {
        "Base: O3":             [1.37, 1.48, 0.98, 1.95, 3.28, 4.22, 0.76, 0.22, 0.59, 1.51, 1.36, 1.21],
        "Opt1: TAGE_SC_L":      [1.26, 1.45, 0.86, 1.84, 3.28, 4.22, 0.43, 0.18, 0.53, 1.47, 1.19, 1.17],
        "Opt2: Opt1 + L0BTB":   [1.81, 2.16, 1.16, 2.35, 4.3, 4.41, 0.62, 0.2, 0.66, 2.11, 1.44, 1.32]
        # Opt3 和 Opt4 已被移除
    },

    # ---------------- Metric 4 ----------------
    "MPKI": {
        "Base: O3":             [20.757452, 7.368573, 6.518508, 15.509947, 1.407883, 0.444171, 6.874195, 15.700074, 6.581063, 4.355559, 15.992912, 4.947123],
        "Opt1: TAGE_SC_L":      [15.723258, 6.17959, 2.174944, 9.473481, 0.838055, 0.266082, 0.009543, 8.388048, 3.744183, 3.234652, 7.917571, 3.806527],
        "Opt2: Opt1 + L0BTB":   [15.7, 6.18, 2.12, 9.47, 0.82, 0.27, 0.034, 8.4, 3.31, 2.7, 7.08, 3.56],
        "Opt3: Opt1 + RVC":     [15.1, 5.56, 2.19, 9.27, 0.81, 0.25, 0.01, 7.97, 3.44, 3.26, 7.05, 3.65],
        "Opt4: All (Opt1+2+3)": [15, 5.5, 2.19, 9.21, 0.81, 0.25, 0.01, 7.96, 3.44, 2.8, 7.05, 3.65]
    },

    # ---------------- Metric 5 ----------------
    "Score": {
        "Base: O3":             [5.91, 5.49, 6.32, 9.22, 14.92, 14.17, 4.07, 4.63, 4.58, 15.6, 8.14, 12.88],
        "Opt1: TAGE_SC_L":      [6.29, 5.64, 6.72, 10.72, 15.37, 14.24, 4.13, 4.72, 4.84, 15.94, 10.24, 13.05],
        "Opt2: Opt1 + L0BTB":   [6.87, 6.62, 8.13, 11.59, 17.12, 14.44, 4.61, 4.92, 5.23, 22.87, 11.28, 13.96],
        "Opt3: Opt1 + RVC":     [6.35, 5.81, 6.69, 10.72, 15.49, 14.45, 4.13, 4.77, 4.94, 15.84, 10.5, 13.18],
        "Opt4: All (Opt1+2+3)": [6.9, 6.62, 8.1, 11.61, 17.22, 14.48, 4.62, 4.95, 5.23, 22.83, 11.32, 13.98]
    }
}

# ==========================================
# 3. 绘图逻辑
# ==========================================

def plot_charts():
    # 设置通用字体
    plt.rcParams['font.sans-serif'] = ['Arial', 'SimHei', 'DejaVu Sans']
    plt.rcParams['axes.unicode_minus'] = False 

    # 定义5种配置的颜色，对应 all_configs_list 的顺序
    colors_map = {
        "Base: O3":             '#808080', # Grey
        "Opt1: TAGE_SC_L":      '#4c72b0', # Blue
        "Opt2: Opt1 + L0BTB":   '#f28e2b', # Orange
        "Opt3: Opt1 + RVC":     '#55a868', # Green
        "Opt4: All (Opt1+2+3)": '#c44e52'  # Red
    }

    for metric_name, values in all_data.items():
        
        # 1. 确定当前 metric 包含哪些 config
        # 按照全局顺序 (all_configs_list) 筛选，以保证图例和柱子顺序一致
        current_configs = [c for c in all_configs_list if c in values]
        
        if not current_configs:
            continue

        # 2. 创建画布
        fig, ax = plt.subplots(figsize=(18, 7))
        
        x = np.arange(len(benchmarks))
        total_width = 0.85
        n = len(current_configs) # 当前图表有多少根柱子 (3 或 5)
        width = total_width / n
        
        # 3. 绘制柱状图
        for i, config in enumerate(current_configs):
            # 动态计算偏移量，确保 n 根柱子整体居中
            offset = (i - (n - 1) / 2) * width
            
            data_list = values[config]
            
            # 简单校验
            if len(data_list) != len(benchmarks):
                print(f"⚠️ 警告: {metric_name} - {config} 数据长度为 {len(data_list)}，但需要 {len(benchmarks)} 个！")
            
            # 获取对应的颜色
            bar_color = colors_map.get(config, '#333333')
            
            ax.bar(x + offset, data_list, width, label=config, color=bar_color, edgecolor='white', linewidth=0.5)

        # 4. 设置标签和标题
        ax.set_ylabel(metric_name) 
        ax.set_title(f'{metric_name} Comparison', fontsize=16, fontweight='bold', pad=20)
        ax.set_xticks(x)
        ax.set_xticklabels(benchmarks, rotation=30, ha='right', fontsize=11)
        
        # 5. 图例放在底部
        ax.legend(fontsize=10, loc='upper center', bbox_to_anchor=(0.5, -0.15), ncol=n, frameon=False) 
        
        ax.grid(axis='y', linestyle='--', alpha=0.4)
        plt.tight_layout()
        
        # 6. 保存
        safe_name = metric_name.replace(" ", "_")
        filename = f"Chart_{safe_name}.png"
        save_path = os.path.join(os.getcwd(), filename)
        plt.savefig(filename, dpi=150, bbox_inches='tight') 
        print(f"✅ 图表已保存: {save_path}")
        
        plt.close()

if __name__ == "__main__":
    print("开始生成图表...")
    plot_charts()
    print("所有图表生成完毕。")