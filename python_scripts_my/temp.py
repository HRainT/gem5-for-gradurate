#!/usr/bin/env python3
# -*- coding: utf-8 -*-

def extract_second_last_column(input_file, output_file):
    """
    从输入文件中读取每一行，提取倒数第二块数据，并写入输出文件
    
    Args:
        input_file (str): 输入文件名（任意文本文件）
        output_file (str): 输出文件名（任意文本文件）
    """
    try:
        with open(input_file, 'r', encoding='utf-8') as f_in:
            with open(output_file, 'w', encoding='utf-8') as f_out:
                for line_num, line in enumerate(f_in, 1):
                    # 去除行尾的换行符并按空格分割
                    line = line.strip()
                    if not line:  # 跳过空行
                        continue
                        
                    parts = line.split()
                    
                    # 确保有足够的块数（至少4块才能有倒数第二块）
                    if len(parts) >= 4:
                        # 获取倒数第二块数据（索引为-2）
                        second_last = parts[-2]
                        f_out.write(second_last + '\n')
                    else:
                        print(f"警告: 第 {line_num} 行数据不完整，跳过: {line}")
        
        print(f"处理完成！从 {input_file} 提取的数据已保存到 {output_file}")
        
    except FileNotFoundError:
        print(f"错误: 找不到文件 {input_file}")
    except Exception as e:
        print(f"处理过程中出现错误: {e}")

def main():
    # 获取用户输入的文件名
    input_filename = input("请输入输入文件名: ").strip()
    output_filename = input("请输入输出文件名: ").strip()
    
    # 执行提取操作
    extract_second_last_column(input_filename, output_filename)

if __name__ == "__main__":
    main()