import os
import subprocess
import re
from concurrent.futures import ThreadPoolExecutor, as_completed

# 配置部分
base_dir = "/Data3/xiaohan.zhang/workspace/SPECint2006_NEMU_GV_Zba_Zbb/445.gobmk/trevord"
output_base_dir = "/Data3/yutong.han/riscv/rxu-gem5/out/445.gobmk/trevord"
gem5_exec = "/Data3/yutong.han/riscv/rxu-gem5/build/RISCV/gem5.opt"
config_file = "/Data3/yutong.han/riscv/rxu-gem5/configs/example/fs.py"
gcpt_restorer = "/Data2/suwei.ye/workspace/specxiangshanflow/NEMU/resource/gcpt_restore/build/gcpt.bin"
max_parallel_jobs = 30

# 通用参数
common_args = [
    "--xiangshan-system",
    "--cpu-type=RxuO3CPU",
    "--mem-size=12GB",
    "--caches", "--cacheline_size=64", "--l1i_size=128kB", "--l1i_assoc=8", "--l1d_size=128kB", "--l1d_assoc=8",
    "--l2cache", "--l2_size=2MB", "--l2_assoc=16",
    "--l1d-hwp-type=XSCompositePrefetcher",
    "--short-stride-thres=0",
    "--l3cache","--l3_size=32MB", "--l3_assoc=16",
    "--l1-to-l2-pf-hint",
    "--l2-hwp-type=WorkerPrefetcher",
    "--l2-to-l3-pf-hint",
    "--l3-hwp-type=WorkerPrefetcher",
    "--cpu-clock=2GHz",
    "--TAGE",
    "--uop-cache",
    "--mem-type=DDR5_8400_4x8",
    "--warmup-insts-no-switch=20000000",
    "--maxinsts=40000000",
    "--rotating",
    "--rxu-rename"
]

def find_gz_files(base_dir):
    """
    遍历目录找到所有符合条件的.gz文件
    返回格式: [(文件路径, 输出名称), ...]
    """
    gz_files = []
    base_dir = os.path.normpath(base_dir)
    
    for root, dirs, files in os.walk(base_dir):
        for file in files:
            if file.endswith(".gz") and file != "simpoint_bbv.gz":
                file_path = os.path.join(root, file)
                rel_path = os.path.relpath(file_path, base_dir)
                path_parts = rel_path.split(os.sep)
                
                if len(path_parts) == 0 or not path_parts[0].isdigit():
                    continue
                
                dir_num = path_parts[0]
                match = re.search(r'_(\d+)_', file)
                if not match:
                    print(f"Warning: 无法从文件名 {file} 中提取数字，跳过")
                    continue
                
                file_num = match.group(1)
                output_name = f"{dir_num}_{file_num}"
                gz_files.append((file_path, output_name))
    
    return gz_files

def run_gem5(gz_file, output_name):
    """执行gem5命令并返回执行结果"""
    output_dir = os.path.join(output_base_dir, output_name)
    os.makedirs(output_dir, exist_ok=True)

    cmd = [
        gem5_exec,
        f"--outdir={output_dir}",
        "--stats-file", "stats.txt",
        config_file,
    ] + common_args + [
        f"--generic-rv-cpt={gz_file}",
        f"--gcpt-restorer={gcpt_restorer}",
    ]

    print(f"Running command: {' '.join(cmd)}")
    
    try:
        result = subprocess.run(
            cmd,
            check=True,
            stderr=subprocess.PIPE,
            universal_newlines=True
        )
        return (output_name, True)
    except subprocess.CalledProcessError as e:
        error_msg = f"任务 {output_name} 执行失败（退出码 {e.returncode}）\n错误信息：{e.stderr}"
        print(error_msg)
        return (output_name, False)
    except Exception as e:
        error_msg = f"任务 {output_name} 发生意外错误：{str(e)}"
        print(error_msg)
        return (output_name, False)

def main():
    gz_files = find_gz_files(base_dir)
    failed_dirs = set()

    with ThreadPoolExecutor(max_workers=max_parallel_jobs) as executor:
        # 提交所有任务
        futures = [executor.submit(run_gem5, fp, on) for fp, on in gz_files]
        
        # 实时处理完成的任务
        for future in as_completed(futures):
            try:
                output_name, success = future.result()
                if not success:
                    dir_num = output_name.split('_')[0]
                    failed_dirs.add(dir_num)
            except Exception as e:
                print(f"任务结果处理异常: {str(e)}")

    # 结果输出
    print("\n" + "="*50)
    if failed_dirs:
        sorted_dirs = sorted(failed_dirs, key=lambda x: int(x))
        print("未成功运行的目录数字部分：")
        print(", ".join(sorted_dirs))
        print(f"总失败目录数: {len(failed_dirs)}")
    else:
        print("所有任务均成功完成！")
        
    print(f"总任务数: {len(gz_files)}, 成功数: {len(gz_files)-len(failed_dirs)}, 失败数: {len(failed_dirs)}")

if __name__ == "__main__":
    main()
