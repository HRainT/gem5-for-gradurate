import re
import sys
from collections import defaultdict

def parse_log_line(line):
    """
    解析单行日志，支持负数 wr
    """
    # 更新正则以支持负数 wr: wr\s*=\s*(-?\d+)
    pattern = r"final_pred\s*=\s*(\d+),\s*sr_pred\s*=\s*(\d+),\s*wr\s*=\s*(-?\d+),\s*result\s*=\s*(\d+)"
    match = re.search(pattern, line)
    
    if match:
        return {
            'final': int(match.group(1)),
            'sr': int(match.group(2)),
            'wr': int(match.group(3)), # 保持原始的有符号整数
            'result': int(match.group(4))
        }
    return None

def analyze_raw_wr(file_path):
    # 使用 wr 值作为 key
    stats = defaultdict(lambda: {
        'total': 0,
        'sr_correct': 0,
        'sr_wrong': 0,
        'opportunity': 0, # SR对，Final错 (SR本可以救场)
        'destructive': 0  # SR错，Final错 (SR可能是罪魁祸首)
    })

    try:
        with open(file_path, 'r') as f:
            for line in f:
                data = parse_log_line(line)
                if not data:
                    continue
                
                wr = data['wr']
                group = stats[wr]
                
                group['total'] += 1
                
                is_sr_correct = (data['sr'] == data['result'])
                is_final_correct = (data['final'] == data['result'])
                
                if is_sr_correct:
                    group['sr_correct'] += 1
                    if not is_final_correct:
                        group['opportunity'] += 1
                else:
                    group['sr_wrong'] += 1
                    if not is_final_correct:
                        group['destructive'] += 1
                        
        return stats

    except FileNotFoundError:
        print(f"Error: File {file_path} not found.")
        return None

def suggest_weight(accuracy, total_samples):
    """
    根据准确率建议权重策略
    样本过少时不给强建议
    """
    if total_samples < 50: # 样本太少，统计学意义不大
        return "Keep 0 (Low Data)"
    
    # 策略逻辑：
    if accuracy >= 0.90: return "Weight ++ (Strong)"   # 极高置信度，给大权重
    if accuracy >= 0.70: return "Weight +  (Normal)"   # 正常置信度
    if accuracy >= 0.55: return "Weight 1  (Weak)"     # 勉强可信
    if 0.45 < accuracy < 0.55: return "Weight 0  (Noise)"    # 纯噪声，直接屏蔽
    if accuracy <= 0.10: return "Invert ++ (Strong)"   # 总是反的，强力取反
    if accuracy <= 0.30: return "Invert +  (Normal)"   # 经常反的
    if accuracy <= 0.45: return "Invert 1  (Weak)"     # 稍微有点反
    
    return "Weight 0 (Unsure)"

def print_analysis(stats):
    # 获取排序后的 wr 列表
    sorted_wrs = sorted(stats.keys())
    
    print("-" * 100)
    print(f"{'Raw WR':^8} | {'Samples':^8} | {'SR Acc':^8} | {'Bias Analysis':^20} | {'Suggestion':^20} | {'Net Gain'}")
    print("-" * 100)
    
    # 用于生成代码建议的数据
    mapping_data = []

    for wr in sorted_wrs:
        s = stats[wr]
        if s['total'] == 0: continue
        
        acc = s['sr_correct'] / s['total']
        net_gain = s['opportunity'] - s['destructive'] 
        # Net Gain > 0 说明在这个 wr 值下，SR 经常是对的而 Final 是错的 -> 应该加大权重
        
        # 可视化 Bias (偏差)
        # 0.5 是中心，0.0 是全错(反向)，1.0 是全对(正向)
        # 使用 ASCII 条形图展示
        bar_len = 10
        pos = int(acc * bar_len)
        bar = [" "] * (bar_len + 1)
        if 0 <= pos <= bar_len:
            bar[pos] = "O"
        # 标记 50% 的位置
        mid_marker = "|"
        visual = f"[{''.join(bar[:5])}{mid_marker}{''.join(bar[5:])}]"
        
        suggestion = suggest_weight(acc, s['total'])
        
        print(f"{wr:^8} | {s['total']:^8} | {acc:6.2%}   | {visual:^20} | {suggestion:^20} | {net_gain:+d}")
        
        mapping_data.append((wr, acc, s['total']))

    print("-" * 100)
    print("\n[Analysis Guide]")
    print("1. SR Acc > 50%: Positive correlation. WR maps to positive weight.")
    print("2. SR Acc < 50%: Negative correlation. SR is reliably wrong. WR maps to NEGATIVE weight (Invert SR).")
    print("3. SR Acc ≈ 50%: Noise. SR adds no value. WR maps to 0 (Disable SR).")
    
    return mapping_data

def generate_code_snippet(mapping_data):
    """
    根据统计结果生成简单的 C++ switch-case 代码框架
    """
    print("\n=== Auto-Generated C++ Logic Suggestion ===")
    print("// Based on log analysis, map raw_wr to applied_weight:\n")
    print("int get_sr_weight(int raw_wr) {")
    print("    // Default fallback")
    print("    int weight = 0;") 
    print("    switch(raw_wr) {")
    
    # 简单的聚类逻辑
    for wr, acc, count in mapping_data:
        if count < 50: continue # 忽略低样本
        
        w_val = 0
        if acc > 0.85: w_val = 4
        elif acc > 0.70: w_val = 2
        elif acc > 0.55: w_val = 1
        elif acc < 0.15: w_val = -4 # 强力取反
        elif acc < 0.30: w_val = -2
        elif acc < 0.45: w_val = -1
        
        if w_val != 0:
            print(f"        case {wr}: weight = {w_val}; break; // Acc: {acc:.2f}")
            
    print("    }")
    print("    return weight;")
    print("}")
    print("\n// Usage: result += sr_pred * get_sr_weight(wr[idx]);")
    print("// Note: If weight is negative, logic implies inverting prediction or subtracting score.")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python analyze_wr_raw.py <log_file>")
        # 生成测试数据
        with open("dummy_raw.log", "w") as f:
            lines = []
            # 模拟：wr为负数时 SR 准确率极低（应该取反），wr为正数时准确率高
            for i in range(100): lines.append("final=0, sr=1, wr=-5, result=0") # wr=-5, acc=0% (Invert!)
            for i in range(100): lines.append("final=0, sr=1, wr=-2, result=1") # wr=-2, acc=50% (Noise)
            for i in range(100): lines.append("final=0, sr=1, wr=5, result=1")  # wr=5, acc=100% (High weight)
            f.write("\n".join(lines))
        
        print("Generating dummy data for demo...\n")
        stats = analyze_raw_wr("dummy_raw.log")
        mapping = print_analysis(stats)
        generate_code_snippet(mapping)
        os.remove("dummy_raw.log")
    else:
        stats = analyze_raw_wr(sys.argv[1])
        if stats:
            mapping = print_analysis(stats)
            generate_code_snippet(mapping)