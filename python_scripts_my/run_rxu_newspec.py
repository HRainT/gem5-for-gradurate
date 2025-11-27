import os
import subprocess
import re
from concurrent.futures import ThreadPoolExecutor

# 配置部分
base_dir = "/Data2/xiaohan.zhang/spec06_NEMU_GZ_V0.1"  # 根目录
output_base_dir = "/Data2/yutong.han/graduate/1128_sr+rxu"  # 输出基础目录
gem5_exec = "/Data3/yutong.han/My_G5Project/graduate-for-gem5-vector/build/RISCV/gem5.opt"
config_file = "/Data3/yutong.han/My_G5Project/graduate-for-gem5-vector/configs/example/fs.py"
gcpt_restorer = "/Data3/suwei.ye/workspace/nexus-am/appsrxu/template/simpoint_case/spec2006_xssimpoint_vector/dir/gcpt_restore/build/gcpt.bin"
max_parallel_jobs = 30  # 最大并行任务数

# 通用参数
common_args = [
    "--xiangshan-system",
    "--mem-size=4GB",
    "--caches", "--cacheline_size=64", "--l1i_size=128kB", "--l1i_assoc=8", "--l1d_size=128kB", "--l1d_assoc=8",
    "--l2cache", "--l2_size=2MB", "--l2_assoc=16",
    "--mem-size=12GB",
    "--cpu-type=RxuO3CPU",
    "--l1d-hwp-type=XSCompositePrefetcher",
    "--short-stride-thres=0",
    "--l1-to-l2-pf-hint",
    "--l2-hwp-type=WorkerPrefetcher",
    "--short-stride-thres=0",
    "--l3cache","--l3_size=32MB","--l3_assoc=16",
    "--l2-to-l3-pf-hint", 
    "--l3-hwp-type=WorkerPrefetcher",
    "--cpu-clock=2GHz",
    "--TAGE",
    "--mem-type=SimpleMemory",
    "--warmup-insts-no-switch=20000000",
    "--maxinsts=40000000",
    "--rotating",
    "--rxu-rename",
    "--sr"
]


def find_gz_files(base_dir):
    """
    遍历目录，找到所有的 .gz 文件（忽略 simpoint_bbv.gz），并返回文件路径及其对应的输出目录名。
    输出目录名格式为：关键字母目录_中间的目录_数字目录
    """
    gz_files = []
    for root, dirs, files in os.walk(base_dir):
        for file in files:
            if file.endswith(".gz") and file != "simpoint_bbv.gz":  # 忽略 simpoint_bbv.gz
                # 分割路径为目录列表
                path_parts = root.split(os.sep)
                
                # 找到类似 473.astar 的关键目录
                key_dir = None
                for part in path_parts:
                    if re.match(r'^\d+\.\w+', part):  # 匹配形如 473.astar 的目录
                        key_dir = re.sub(r'^\d+\.', '', part)  # 提取字母部分（如 astar）
                        break
                
                if key_dir:
                    # 提取从关键目录到 .gz 文件之间的所有目录
                    key_index = path_parts.index(part)
                    middle_dirs = path_parts[key_index+1:]  # 获取关键目录之后的路径部分
                    middle_dirs_str = "_".join(middle_dirs)  # 拼接中间的目录
                    
                    # 最终输出格式：关键字母目录_中间的目录
                    output_name = f"{key_dir}_{middle_dirs_str}"
                    gz_files.append((os.path.join(root, file), output_name))
    
    return gz_files

def run_gem5(gz_file, output_name):
    """
    执行 gem5 命令的函数
    """
    # 创建输出目录
    output_dir = os.path.join(output_base_dir, output_name)
    os.makedirs(output_dir, exist_ok=True)

    # 构造命令行，严格按照指定顺序
    cmd = [
        gem5_exec,                            # gem5 执行文件
        f"--outdir={output_dir}",             # 输出目录
        "--stats-file", "stats.txt",          # stats 文件
        "--stdout-file","out.txt","-r",        #output 文件
        # "--debug-flags=CommitInst",           # 调试标志
        # "--debug-file=rxu.log",               # 调试日志文件
        config_file,                          # 配置文件
    ] + common_args + [
        f"--generic-rv-cpt={gz_file}",        # 指定 .gz 文件
        f"--gcpt-restorer={gcpt_restorer}",   # gcpt-restorer 文件
    ]

    # 打印命令以供调试
    print(f"Running command: {' '.join(cmd)}")

    # 执行命令
    subprocess.run(cmd)

def main():
    # 查找所有 .gz 文件及其对应的输出目录名
    gz_files = find_gz_files(base_dir)

    # 使用线程池并行处理
    with ThreadPoolExecutor(max_workers=max_parallel_jobs) as executor:
        # 将 gz 文件和输出目录名传递给 run_gem5
        executor.map(lambda args: run_gem5(*args), gz_files)

if __name__ == "__main__":
    main()

