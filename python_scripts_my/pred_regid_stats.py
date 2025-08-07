import re
import csv
import os
from collections import defaultdict

def analyze_branch_instructions(input_file, output_file):
    # 初始化统计数据结构
    total_pair_count = defaultdict(int)  # 寄存器组合总次数
    instruction_pair_count = {inst: defaultdict(int) for inst in 
                            ['beq', 'bne', 'blt', 'bge', 'bltu', 'bgeu']}  # 各指令下的寄存器组合次数
    
    # 正则表达式匹配分支指令和寄存器
    pattern = r'\b(beq|bne|blt|bge|bltu|bgeu)\s+(\w+)\s*,\s*(\w+)'
    
    # 读取并处理文件
    with open(input_file, 'r') as f:
        for line in f:
            match = re.search(pattern, line)
            if match:
                inst = match.group(1)
                reg1 = match.group(2)
                reg2 = match.group(3)
                reg_pair = f"{reg1},{reg2}"
                
                # 更新统计
                total_pair_count[reg_pair] += 1
                instruction_pair_count[inst][reg_pair] += 1
    
    # 计算总次数用于占比计算
    total_occurrences = sum(total_pair_count.values())
    
    # 写入CSV文件
    with open(output_file, 'w', newline='') as csvfile:
        writer = csv.writer(csvfile)
        
        # 写入表头
        header = ['寄存器组合', '总出现次数', '总占比(%)']
        for inst in instruction_pair_count.keys():
            header.extend([f'{inst}次数', f'{inst}占比(%)'])
        writer.writerow(header)
        
        # 写入数据行（按总次数降序排序）
        for reg_pair, total_count in sorted(total_pair_count.items(), 
                                          key=lambda x: x[1], reverse=True):
            # 计算总占比
            total_percent = (total_count / total_occurrences) * 100 if total_occurrences > 0 else 0
            
            row = [reg_pair, total_count, f"{total_percent:.2f}"]
            
            # 添加各指令的统计和占比
            for inst in instruction_pair_count.keys():
                inst_count = instruction_pair_count[inst][reg_pair]
                inst_percent = (inst_count / total_count) * 100 if total_count > 0 else 0
                row.extend([inst_count, f"{inst_percent:.2f}"])
            
            writer.writerow(row)
    
    print(f"分析完成！结果已保存至: {os.path.abspath(output_file)}")

# 使用示例
if __name__ == "__main__":
    input_log = "/Data3/yutong.han/My_G5Project/graduate-for-gem5-vector/out/473.astar/BigLakes/397/CommitInsts.log"  # 替换为你的输入文件路径
    output_csv = "/Data3/yutong.han/My_G5Project/graduate-for-gem5-vector/out/predinst_regid/473.astar_BigLakes_397.csv"  # 替换为你的输出文件路径
    analyze_branch_instructions(input_log, output_csv)