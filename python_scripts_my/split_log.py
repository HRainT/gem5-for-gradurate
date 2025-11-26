import os

def split_log_by_size_parts(input_file, output_dir, num_parts):
    """
    将日志文件按文件大小均分为指定的份数。
    
    参数:
        input_file (str): 输入的日志文件路径
        output_dir (str): 输出文件的目录
        num_parts (int): 切分的份数
    """
    if not os.path.exists(input_file):
        print(f"错误: 输入文件 {input_file} 不存在！")
        return

    if not os.path.exists(output_dir):
        os.makedirs(output_dir)

    # 获取文件总大小（字节）
    total_size = os.path.getsize(input_file)
    part_size = total_size // num_parts  # 每份的目标大小（字节）

    print(f"文件总大小: {total_size / (1024 * 1024):.2f} MB, 每份目标大小: {part_size / (1024 * 1024):.2f} MB, 切分份数: {num_parts}")

    file_index = 1
    current_size = 0
    current_lines = []

    # 按行读取文件并切分
    with open(input_file, 'r', encoding='utf-8') as f:
        for line in f:
            line_size = len(line.encode('utf-8'))  # 计算行的字节大小
            current_size += line_size
            current_lines.append(line)

            # 如果当前文件大小超过目标大小，写入文件并重置
            if current_size >= part_size and file_index < num_parts:
                output_file = os.path.join(output_dir, f"log_part_{file_index}.log")
                with open(output_file, 'w', encoding='utf-8') as out_f:
                    out_f.writelines(current_lines)
                print(f"已生成: {output_file} ({current_size / (1024 * 1024):.2f} MB)")
                file_index += 1
                current_size = 0
                current_lines = []

        # 写入最后一部分文件（如果有剩余内容）
        if current_lines:
            output_file = os.path.join(output_dir, f"log_part_{file_index}.log")
            with open(output_file, 'w', encoding='utf-8') as out_f:
                out_f.writelines(current_lines)
            print(f"已生成: {output_file} ({current_size / (1024 * 1024):.2f} MB)")

    print("日志文件切分完成！")


if __name__ == "__main__":
    import argparse

    # 定义命令行参数
    parser = argparse.ArgumentParser(description="将日志文件按文件大小均分为几等分")
    parser.add_argument("input_file", type=str, help="输入的日志文件路径")
    parser.add_argument("output_dir", type=str, help="输出文件的目录")
    parser.add_argument("num_parts", type=int, help="切分的份数")

    args = parser.parse_args()

    # 调用函数进行日志切分
    split_log_by_size_parts(args.input_file, args.output_dir, args.num_parts)
