#!/usr/bin/env python3

import os
import re
import sys
import time
import sh
from multiprocessing import Pool
import itertools

benchmark = sys.argv[1]

gem5_home = "/Data3/yutong.han/riscv/rxu-gem5"
# gem5_bin = "/Data/longting.du/work/spec-gem5"

checkpoint_dir = "/Data3/xiaohan.zhang/work/rv64gv_gcc14/libquantum"

mem_size = 2
label = "libquantum_Rxu_0402"

spec2006 = {
    "perlbench": ["checkspam", "splitmail", "diffmail"],
    "astar": ["biglakers2048", "rivers"],
    "gcc": ["166", "200", "c-typeck", "cp-decl", "expr", "expr2", "g23", "s04", "scilab"],
    "bzip2": ["chick2n_jpg", "input_combined", "input_program", "input_source", "liberty_jpg", "text_html"],
    "gobmk": ["13x13", "nngs", "score2", "trevorc", "trevord"],
    "h264ref": ["foreman_baseline", "foreman_main", "sss"],
    "hmmer": ["retro", "nph3"],
    "mcf": [], "xalan": [], "sjeng": [], "omnetpp": [], "libquantum": []
}
    
tasks = []
if benchmark == "int" or benchmark == "all":
    tasks = list(spec2006.keys())
    program = None
    print("run all int program...")
    print("benchmark:", " ".join(tasks))
else:
    assert benchmark in list(spec2006.keys())
    
    if len(spec2006[benchmark]) > 0:
        program = sys.argv[2]
        assert program in spec2006[benchmark]
    else:
        program = None
        
    tasks = [benchmark]
    print("run single program: ", benchmark)


def check_string_in_file(file_name, search_string):
    try:
        with open(file_name, 'r') as file:
            for line in file.readlines():
                if line.startswith(search_string):
                    return True
        return False
    except FileNotFoundError:
        return False


def restore_cpt(task_cpt):
    
    task, cpt = task_cpt.split("_")[0], int(task_cpt.split("_")[1])
        
    exe = "/Data/yuchen.hu/work/spec/spec2006/benchspec/CPU2006/462.libquantum/exe/{}_base.rv64gv_gcc14".format(task)
    print("load elf:", exe)
    
    dir = "{}/{}".format(checkpoint_dir, task)
    if program:
        cpt_dir = dir + "/{}".format(program)
    else:
        cpt_dir = dir

    print("using cpt dir:", cpt_dir)
    os.chdir(dir)

    dir_names = [x for x in os.listdir(cpt_dir) if x.startswith("cpt")]
    num_cpt = len(dir_names)
    
    outdir_b = gem5_home + "/spec_2006/{}/{}".format(label, task)
    print("output dir:", outdir_b)
    
    gem5 = sh.Command("{}/build/RISCV/gem5.opt".format(gem5_home))
    
    weight = dir_names[cpt - 1].split("_")[5]
    
    if program:
        outdir = "{}_{}_{}".format(outdir_b, program, cpt)
    else:
        outdir = "{}_{}".format(outdir_b, cpt)
    if not os.path.exists(outdir):
        os.makedirs(outdir)
        
    if os.path.exists(os.path.join(outdir, "completed")):
        print("\n{} successed! pass\n".format(outdir))
        # print(f"{outdir} complete")
        # os.system(f"touch {outdir}/completed")
        return
    
    options = [
        "--outdir={}".format(outdir),
        "--stats-file=stats.txt",
        # "--debug-flag=CommitInsts",
        # "--debug-file=trace.log",
        "{}/configs/deprecated/example/se.py".format(gem5_home),
        "--cmd={}".format(exe),
        "--mem-size={}GB".format(mem_size),
        "--restore-simpoint-checkpoint",
        "--checkpoint-restore={}".format(cpt),
        "--checkpoint-dir={}".format(cpt_dir),
        "--cpu-type=RxuO3CPU",
        "--cpu-clock=2GHz",
        "--caches",
        "--l2cache",
        "--rxu-rename",
        "--rotating",
        "--maxinsts=40000000",
        "--warmup-insts-no-switch=20000000",
        "--enable-arch-db",
        "--uop-cache",
    ]
    
    prefetch_options = [
        "--l1i_size=128kB",
        "--l1i_assoc=8",
        "--l1d_size=128kB",
        "--l1d_assoc=8",
        "--mem-type=DDR5_8400_4x8",
        "--l1d-hwp-type=XSCompositePrefetcher",
        "--l1-to-l2-pf-hint",
        "--l2-hwp-type=WorkerPrefetcher",
        "--short-stride-thres=0",
        "--l2_size=2MB", "--l2_assoc=16",
        "--l3cache","--l3_size=32MB","--l3_assoc=16", 
        "--l2-to-l3-pf-hint", 
        "--l3-hwp-type=WorkerPrefetcher"
    ]
    
    options.extend(prefetch_options)
    
    # print(gem5)
    print(options)
    
    try:
        abort_file = os.path.join(outdir, "aborted")
        if os.path.exists(abort_file):
            os.remove(abort_file)
        
        gem5(
            _out=os.path.join(outdir_b, '{}/gem5_out.txt'.format(outdir)),
            _err=os.path.join(outdir_b, '{}/gem5_err.txt'.format(outdir)),
            *options
        )
        print(f"{outdir} complete")
        os.system(f"touch {outdir}/completed")
        
    except Exception as e:
        print(f"{outdir} failed: {e}")
        os.system(f"touch {outdir}/aborted")


def run_task(task):
    
    num_process = 30
    
    dir = "{}/{}".format(checkpoint_dir, task)
    if program:
        cpt_dir = dir + "/{}".format(program)
    else:
        cpt_dir = dir

    os.chdir(dir)

    dir_names = [x for x in os.listdir(cpt_dir) if x.startswith("cpt")]
    dir_names.sort()
    print("\n".join(dir_names))

    num_cpt = len(dir_names)
    print(f"find {num_cpt} checkpoints in cpt dir")
    
    tasks = [task + "_" + str(x) for x in range(1, num_cpt + 1)]
    
    pool = Pool(processes=num_process)
    
    pool.map(restore_cpt, tasks)
    
    pool.close()
    pool.join()
        

assert len(tasks) > 0

for task in tasks:
    run_task(task)