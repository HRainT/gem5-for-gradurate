import matplotlib.pyplot as plt
import numpy as np

def plot_wt_tradeoff_updated_v2():
    # 1. 数据准备
    # X轴: 子表(Sub-table)的数量 (1到5)
    num_tables = np.array([1, 2, 3, 4, 5])
    
    # Y轴数据1: 资源开销 (Bytes)
    # 更新：单个子表大小为 372 Bytes
    unit_cost = 372
    resource_cost = num_tables * unit_cost
    
    # Y轴数据2: 预测准确率
    accuracy_gain = np.array([0.961, 0.973, 0.980, 0.983, 0.984])

    # 2. 创建图表
    fig, ax1 = plt.subplots(figsize=(10, 6))

    # 设置标题和网格
    plt.title('Design Space Exploration: Register Selection Table (RST) Sub-tables Count', fontsize=14, pad=20)
    plt.grid(True, linestyle='--', alpha=0.3)

    # 3. 绘制资源开销 (柱状图 - 右轴)
    ax2 = ax1.twinx()
    color_bar = 'lightgray'
    bars = ax2.bar(num_tables, resource_cost, color=color_bar, alpha=0.6, width=0.5, label='Hardware Cost (Bytes)')
    ax2.set_ylabel('Resource Cost (Bytes)', color='gray', fontsize=12)
    ax2.tick_params(axis='y', labelcolor='gray')
    
    # 调整上限以适应数据范围 (5 * 372 = 1860, so 2200 is safe)
    ax2.set_ylim(0, 2200)

    # 在柱子上标注具体字节数
    for bar in bars:
        height = bar.get_height()
        ax2.text(bar.get_x() + bar.get_width()/2., height,
                f'{int(height)}B',
                ha='center', va='bottom', color='gray', fontsize=9)

    # 4. 绘制准确率 (折线图 - 左轴)
    color_line = '#1f77b4' # 蓝色
    line = ax1.plot(num_tables, accuracy_gain, color=color_line, marker='o', linewidth=2.5, markersize=8, label='Prediction Accuracy')
    
    ax1.set_xlabel('Number of Sub-tables (Arrays)', fontsize=12)
    ax1.set_ylabel('Prediction Accuracy', color=color_line, fontsize=12)
    ax1.tick_params(axis='y', labelcolor=color_line)
    
    # 动态调整Y轴范围
    ax1.set_ylim(0.955, 0.990)
    ax1.set_xticks(num_tables)

    # 在每个数据点上方标注准确率数值
    for x, y in zip(num_tables, accuracy_gain):
        ax1.text(x, y + 0.001, f'{y:.3f}', ha='center', va='bottom', color=color_line, fontsize=9, fontweight='bold')

    # 5. 标注 "Optimal Point" (N=4)
    target_idx = 3 # N=4 的索引
    ax1.annotate('Optimal Point (N=4)\nHigh Accuracy / Acceptable Cost', 
                 xy=(num_tables[target_idx], accuracy_gain[target_idx]), 
                 xytext=(num_tables[target_idx], accuracy_gain[target_idx] - 0.008),
                 arrowprops=dict(facecolor='black', shrink=0.05, width=1.5),
                 fontsize=10, fontweight='bold', ha='center',
                 bbox=dict(boxstyle="round,pad=0.3", fc="#fffeee", ec="black", lw=1))

    # --- 调整图层顺序 ---
    ax1.set_zorder(10)
    ax2.set_zorder(1)
    ax1.patch.set_visible(False)

    # 6. 添加图例
    lines, labels = ax1.get_legend_handles_labels()
    bars_legend, bars_labels = ax2.get_legend_handles_labels()
    
    leg = ax1.legend(lines + bars_legend, labels + bars_labels, loc='lower right', frameon=True)
    leg.set_zorder(100)

    plt.tight_layout()
    
    # 保存图片
    plt.savefig('wt_design_tradeoff_updated_v2.png', dpi=300)
    print("图表已更新并保存为 'wt_design_tradeoff_updated_v2.png'")

if __name__ == "__main__":
    plot_wt_tradeoff_updated_v2()