import sys

def parse_line(line):
    """解析日志行，提取PC值和目的寄存器值"""
    parts = line.strip().split(',')
    if len(parts) < 3:
        raise ValueError("日志行格式错误，无法解析")
    pc_value = parts[1].strip().split()[0]
    register_value = parts[2].strip().split()[0]
    return pc_value, register_value

def compare_logs(file1, file2):
    """比较两个日志文件"""
    green = '\033[32m'
    red   = "\033[31m"
    reset = '\033[0m'
    success = True
    
    with open(file1, 'r') as f1, open(file2, 'r') as f2:
        # 跳过第一行
        next(f1)
        next(f2)
        
        lines1 = list(f1)
        lines2 = list(f2)
        
        # 检查指令数量是否一致
        if len(lines1) != len(lines2):
            success = False
            print(f"{red}文件o3和文件2的指令数量不一致：{reset}")
            print(f"文件o3有 {len(lines1)} 条指令")
            print(f"文件2 有 {len(lines2)} 条指令")
        
        line_number = 1  # 从第二行开始计数
        for line1, line2 in zip(lines1, lines2):
            line_number += 1
            try:
                pc1, reg1 = parse_line(line1)
                pc2, reg2 = parse_line(line2)
            except ValueError as e:
                print(f"错误：{e} 在第 {line_number} 行")
                success = False
                break

            if pc1 != pc2 or reg1 != reg2:
                success = False
                print(f"{red}文件o3和文件2在第 {line_number} 行不一致：{reset}")
                print(f"文件o3: {line1.strip()}")
                print(f"文件2 : {line2.strip()}")
                break
    
    if success:
        print(f"{green}Compare success!{reset}")

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("使用方法: python script.py <路径到日志文件1> <路径到日志文件2>")
        sys.exit(1)
    
    file_o3 = sys.argv[1]  # 从命令行获取第一个日志文件路径
    file_path2 = sys.argv[2]  # 从命令行获取第二个日志文件路径
    compare_logs(file_o3, file_path2)