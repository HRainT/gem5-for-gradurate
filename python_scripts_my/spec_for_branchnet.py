#!/usr/bin/env python3
import os, re, subprocess
from concurrent.futures import ThreadPoolExecutor

# --------------------------------------------------
# 路径配置
# --------------------------------------------------
base_dir               = "/Data3/xiaohan.zhang/workspace/SPECint2006_NEMU_G_Zicond_Zba_Zbb/473.astar"

gem5_output_base_dir   = "/Data3/yutong.han/My_G5Project/graduate-for-gem5-vector/out/spec/0617_BranchNet_star"  # --outdir 用
branch_log_base_dir    = "/Data3/yutong.han/My_G5Project/BranchNet/riscv_logs"                                    # 分支日志专用

gem5_exec              = "/Data3/yutong.han/My_G5Project/graduate-for-gem5-vector/build/RISCV/gem5.opt"
config_file            = "/Data3/yutong.han/My_G5Project/graduate-for-gem5-vector/configs/example/fs.py"
gcpt_restorer          = "/Data3/suwei.ye/workspace/nexus-am/appsrxu/template/simpoint_case/spec2006_xssimpoint_vector/dir/gcpt_restore/build/gcpt.bin"

max_parallel_jobs      = 30
branch_log_env         = "BRANCH_LOG"      # 与 C++ 部分保持一致
# --------------------------------------------------

common_args = [
    "--xiangshan-system",
    "--cpu-type=O3CPU",
    "--mem-size=12GB",
    "--caches", "--cacheline_size=64",
    "--l1i_size=128kB", "--l1i_assoc=8",
    "--l1d_size=128kB", "--l1d_assoc=8",
    "--l2cache", "--l2_size=2MB", "--l2_assoc=16",
    "--l1d-hwp-type=XSCompositePrefetcher",
    "--short-stride-thres=0",
    "--l1-to-l2-pf-hint",
    "--l2-hwp-type=WorkerPrefetcher",
    "--l3cache", "--l3_size=32MB", "--l3_assoc=16",
    "--cpu-clock=2GHz",
    "--TAGE",
    "--mem-type=SimpleMemory",
    "--warmup-insts-no-switch=20000000",
    "--maxinsts=40000000",
    "--rotating"
]

# --------------------------------------------------
# 1. 搜索所有 *.gz，抽取 benchmark / input / simpoint
# --------------------------------------------------
def discover_tasks(root_dir):
    tasks = []
    for root, _, files in os.walk(root_dir):
        for fname in files:
            if not fname.endswith(".gz") or fname == "simpoint_bbv.gz":
                continue

            full_path = os.path.join(root, fname)
            parts = root.split(os.sep)

            # 找形如 473.astar 的目录
            bench_idx = next((i for i, p in enumerate(parts)
                              if re.match(r"^\d+\.\w+$", p)), -1)
            if bench_idx == -1:
                continue

            benchmark = parts[bench_idx]                 # 473.astar
            sub = parts[bench_idx + 1:]                  # 例如 [BigLakes, 247] 或 [220]

            if len(sub) == 1:
                input_name  = "default"
                simpoint_id = sub[0]
            else:
                input_name  = sub[0]
                simpoint_id = sub[-1]

            tasks.append((full_path, benchmark, input_name, simpoint_id))
    return tasks

# --------------------------------------------------
# 2. 跑单个任务
# --------------------------------------------------
def run_task(gz_path, benchmark, input_name, simpoint_id):
    # --outdir：保持唯一即可
    outdir_name = f"{benchmark}_{input_name}_{simpoint_id}"
    gem5_outdir = os.path.join(gem5_output_base_dir, outdir_name)
    os.makedirs(gem5_outdir, exist_ok=True)

    # 分支日志：严格遵守命名规则
    branch_log = os.path.join(
        branch_log_base_dir,
        f"{benchmark}_{input_name}_simpoint{simpoint_id}.log"
    )
    os.makedirs(branch_log_base_dir, exist_ok=True)

    cmd = [
        gem5_exec,
        f"--outdir={gem5_outdir}",
        "--stats-file",  "stats.txt",
        "--stdout-file", "out.txt", "-r",
        config_file,
        *common_args,
        f"--generic-rv-cpt={gz_path}",
        f"--gcpt-restorer={gcpt_restorer}"
    ]

    env = os.environ.copy()
    env[branch_log_env] = branch_log

    print(f"[run] {os.path.basename(gz_path)}")
    print(f"      BRANCH_LOG -> {branch_log}")
    subprocess.run(cmd, env=env)

# --------------------------------------------------
# 3. 主函数
# --------------------------------------------------
def main():
    tasks = discover_tasks(base_dir)
    if not tasks:
        print("No .gz files found, check directory.")
        return

    with ThreadPoolExecutor(max_workers=max_parallel_jobs) as pool:
        pool.map(lambda t: run_task(*t), tasks)

if __name__ == "__main__":
    main()
