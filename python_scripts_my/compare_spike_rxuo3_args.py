import sys

def parse_log(file_path, start_keyword="mret"):
    """
    解析日志文件，提取PC值，从第一次出现指定关键字开始。
    """
    pc_values = []
    start_parsing = False

    with open(file_path, 'r') as file:
        for line in file:
            if not start_parsing and start_keyword in line:
                start_parsing = True
            
            if start_parsing:
                # 提取PC值
                parts = line.split()
                for part in parts:
                    if part.startswith("0x"):
                        try:
                            pc = int(part, 16)
                            pc_values.append(pc)
                            break
                        except ValueError:
                            continue

    return pc_values

def compare_pc_values(file1, file2):
    """
    比较两个文件的PC值，忽略重复打印。
    """
    pc_values1 = parse_log(file1)
    pc_values2 = parse_log(file2)

    # 从两个文件提取的PC值列表中去重
    unique_pc_values1 = []
    unique_pc_values2 = []

    for i in range(len(pc_values1)):
        if i == 0 or pc_values1[i] != pc_values1[i - 1]:
            unique_pc_values1.append(pc_values1[i])

    for i in range(len(pc_values2)):
        if i == 0 or pc_values2[i] != pc_values2[i - 1]:
            unique_pc_values2.append(pc_values2[i])

    # 比较两个文件的PC值
    for pc1, pc2 in zip(unique_pc_values1, unique_pc_values2):
        if pc1 != pc2:
            print(f"Mismatch: File1 PC={hex(pc1)}, File2 PC={hex(pc2)}")
        else:
            print(f"Match: PC={hex(pc1)}")

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python compare_logs.py <log_file1> <log_file2>")
        sys.exit(1)

    log_file1 = sys.argv[1]
    log_file2 = sys.argv[2]

    compare_pc_values(log_file1, log_file2)
