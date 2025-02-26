def extract_address(line):
    # 提取括号内的内容
    start = line.find('(') + 1
    end = line.find(')')
    if start > 0 and end > 0:
        return line[start:end]
    return None

def compare_files(file1, file2):
    with open(file1, 'r') as f1, open(file2, 'r') as f2:
        next(f1)  # 跳过第一行
        next(f2)  # 跳过第一行
        
        line_number = 2  # 从第二行开始计数
        for line1, line2 in zip(f1, f2):
            address1 = extract_address(line1)
            address2 = extract_address(line2)
            
            if address1 != address2:
                print(f"First difference found at line {line_number}:")
                print(f"File 1: {line1.strip()}")
                print(f"File 2: {line2.strip()}")
                return  # 找到第一个不同后停止
            
            line_number += 1

        print("No differences found.")

# 使用示例
file_path1 = '/Data3/yutong.han/riscv/rxu-gem5/out/vector/vluxe-test/vluxe_v0.log'
file_path2 = '/Data3/yutong.han/riscv/rxu-gem5/out/vector/vluxe-test/vluxe_O3_v0.log'
compare_files(file_path1, file_path2)