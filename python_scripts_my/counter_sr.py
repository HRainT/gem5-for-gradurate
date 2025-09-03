#!/usr/bin/env python3
import sys
import os

def is_text_file(path, blocksize=1024):
    # 简单启发式：尝试以utf-8读取一小块，不行则当作二进制跳过
    try:
        with open(path, 'r', encoding='utf-8') as f:
            f.read(blocksize)
        return True
    except Exception:
        return False

def count_in_file(path):
    try:
        with open(path, 'r', encoding='utf-8', errors='ignore') as f:
            data = f.read()
        count_sr0 = data.count("SR:0")
        count_sr  = data.count("SR")
        return count_sr, count_sr0
    except Exception:
        return 0, 0

def main():
    if len(sys.argv) != 2:
        print("用法: python count_sr.py <目录或文件路径>")
        sys.exit(1)

    target = sys.argv[1]
    total_sr = 0
    total_sr0 = 0

    if os.path.isfile(target):
        sr, sr0 = count_in_file(target)
        total_sr += sr
        total_sr0 += sr0
    else:
        for root, dirs, files in os.walk(target):
            for name in files:
                path = os.path.join(root, name)
                if is_text_file(path):
                    sr, sr0 = count_in_file(path)
                    total_sr += sr
                    total_sr0 += sr0

    print(f"SR 总次数: {total_sr}")
    print(f"SR:0 总次数: {total_sr0}")
    if total_sr > 0:
        ratio = total_sr0 / total_sr
        print(f"SR:0 占 SR 的比例: {ratio:.6f}")
    else:
        print("SR:0 占 SR 的比例: 无法计算（SR 次数为 0）")

if __name__ == "__main__":
    main()
