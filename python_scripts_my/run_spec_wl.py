import os, subprocess, re, itertools
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import datetime

# 配置部分
base_dir = "/Data2/xiaohan.zhang/spec06_NEMU_GZ_V0.1"  # 根目录
# base_dir = "/Data2/xiaohan.zhang/SPECint2006_NEMU_GVZ_ultimate"  # 根目录
today = datetime.now().strftime("%Y%m%d")
output_base_dir = "/Data2/yutong.han/FE_out" + today + "-decouple_xs_btb_1024flush"  # 输出基础目录
gem5_exec = "/Data3/yutong.han/riscv/rxu-gem5/build/RISCV/gem5.opt"
config_file = "/Data3/yutong.han/riscv/rxu-gem5/configs/example/fs.py"
gcpt_restorer = "/Data2/suwei.ye/workspace/specxiangshanflow/NEMU/resource/gcpt_restore/build/gcpt.bin"
max_parallel_jobs = 20  # 最大并行任务数

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
    "--bp-type=DecoupledBPUWithBTB"
]

# ============= 1. 找 .gz 文件并生成输出名 =============
def find_gz_files(rootdir):
    results = []
    for root, dirs, files in os.walk(rootdir):
        for f in files:
            if f.endswith(".gz") and f != "simpoint_bbv.gz":
                parts = root.split(os.sep)
                key_dir = next((p for p in parts if re.match(r'^\d+\.\w+', p)), None)
                if key_dir:
                    key = re.sub(r'^\d+\.', '', key_dir)
                    mid = "_".join(parts[parts.index(key_dir)+1:])
                    out_name = f"{key}_{mid}"
                    results.append((os.path.join(root, f), out_name))
    return results

# ============= 2. 具体跑 gem5 =============
def run_gem5(gz_file, out_name):
    outdir = os.path.join(output_base_dir, out_name)
    os.makedirs(outdir, exist_ok=True)

    cmd = [
        gem5_exec,
        f"--outdir={outdir}",
        "--stats-file", "stats.txt",
        "--stdout-file", "out.txt", "-r",
        config_file,
    ] + common_args + [
        f"--generic-rv-cpt={gz_file}",
        f"--gcpt-restorer={gcpt_restorer}",
    ]

    print(f"[LAUNCH] {out_name}")              # 可选：启动提示
    res = subprocess.run(cmd)                  # 同步阻塞
    if res.returncode != 0:
        raise RuntimeError(f"{out_name} failed")

# ============= 3. 主函数，带进度条 =============
def main():
    tasks = find_gz_files(base_dir)
    total = len(tasks)
    print(f"Total slices to run: {total}")

    if total == 0:
        return

    # ----------- 方式 A：tqdm 进度条（推荐） -----------
    try:
        from tqdm import tqdm
        with ThreadPoolExecutor(max_workers=max_parallel_jobs) as pool:
            futures = [pool.submit(run_gem5, *t) for t in tasks]
            for _ in tqdm(as_completed(futures), total=total, unit="slice"):
                pass   # tqdm 会自动刷新
    except ModuleNotFoundError:
        # ----------- 方式 B：不用任何第三方库 -----------
        finished = itertools.count(1)  # 线程安全自增器
        with ThreadPoolExecutor(max_workers=max_parallel_jobs) as pool:
            futures = {pool.submit(run_gem5, *t): t[1] for t in tasks}
            for fut in as_completed(futures):
                idx = next(finished)
                name = futures[fut]
                try:
                    fut.result()
                    print(f"[{idx}/{total}] DONE  {name}")
                except Exception as e:
                    print(f"[{idx}/{total}] FAIL  {name}: {e}")

if __name__ == "__main__":
    main()
