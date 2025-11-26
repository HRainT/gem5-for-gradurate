import re
from collections import defaultdict

def analyze_log(file_path):
    # 使用字典记录每个SN的+1和-1操作次数
    sn_counts = defaultdict(lambda: {'+': 0, '-': 0})
    
    # 正则匹配模式：提取操作类型（+/-）和SN
    pattern = re.compile(r'storesToWB (\+|\-) 1.*?\[sn:(\d+)\]')
    
    with open(file_path, 'r') as file:
        for line in file:
            match = pattern.search(line)
            if match:
                op, sn = match.groups()
                sn_counts[sn][op] += 1
    
    # 筛选不符合条件的SN（+1和-1次数不等或未成对）
    invalid_sns = {
        sn: {'+': counts['+'], '-': counts['-'], 'total': sum(counts.values())}
        for sn, counts in sn_counts.items()
        if counts['+'] != counts['-'] or 0 in counts.values()
    }
    
    # 打印结果
    if invalid_sns:
        print("SNs with unbalanced '+1' and '-1' operations:")
        for sn, data in invalid_sns.items():
            print(f"SN {sn}: +1={data['+']}, -1={data['-']}, Total={data['total']}")
    else:
        print("All SNs have balanced '+1' and '-1' operations.")

if __name__ == "__main__":
    import sys
    if len(sys.argv) != 2:
        print("Usage: python script.py <log_file>")
        sys.exit(1)
    analyze_log(sys.argv[1])
