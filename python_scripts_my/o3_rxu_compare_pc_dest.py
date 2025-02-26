def parse_line(line):
    """解析日志行,提取PC值和目的寄存器值"""
    parts = line.strip().split(',')
    if len(parts) < 3:
        raise ValueError("日志行格式错误，无法解析")
    pc_value = parts[1].strip().split()[0]
    register_value = parts[2].strip().split()[0]
    return pc_value, register_value

def compare_logs(file1, file2):
    """比较两个日志文件"""
    with open(file1, 'r') as f1, open(file2, 'r') as f2:
        # 跳过第一行
        next(f1)
        next(f2)
        
        line_number = 1  # 从第二行开始计数
        for line1, line2 in zip(f1, f2):
            line_number += 1
            try:
                pc1, reg1 = parse_line(line1)
                pc2, reg2 = parse_line(line2)
            except ValueError as e:
                print(f"错误：{e} 在第 {line_number} 行")
                break

            if pc1 != pc2 or reg1 != reg2:
                print(f"文件1和文件2在第 {line_number} 行不一致：")
                print(f"文件1: {line1.strip()}")
                print(f"文件2: {line2.strip()}")
                break

if __name__ == "__main__":
    file_path1 = '/Data3/yutong.han/riscv/rxu-gem5/out/vector/eop/merge_eop/vector_sg2042_O3.log'  # 第一个日志文件路径
    file_path2 = '/Data3/yutong.han/riscv/rxu-gem5/out/vector/eop/merge_eop/vector_sg2042.log'  # 第二个日志文件路径
    compare_logs(file_path1, file_path2)