import os
import subprocess
import pandas as pd
import re
from datetime import datetime

# 默认参数字典
default_params = {
    'gem5_path': './build/RISCV/gem5.fast',
    'config_script': './configs/example/riscv/fs_linux.py',
    'dtb_file': 'None',
    'cpu_clock': '2GHz',
    'abs_max_tick': '5000000000000',
    'maxinsts': '500000000',
    'loop_pc': '0x70003304',
}

# 默认的输出根目录
today = datetime.now().strftime("%Y%m%d")  # 获取当前日期，格式为YYYYMMDD
default_output_root  = os.path.join("./out", "dhrystone_coremark_lingpack", today+'')  # 构建目录路径

# 模拟实例参数的列表，每个字典代表一个实例的配置
simulation_cases = [
    # {
    #     'kernel': '/Data/longting.du/work/case/sifive-dhrystone_loop500_publictoolchains_testcase/sifive-dhrystone_loop500_publictoolchains_testcase.elf',
    #     'loop_pc': '0x70003304',
    #     # 其他需要覆盖的参数...
    # },
    # {
    #     'kernel': '/Data/longting.du/work/case/dhry/dhrystone_onefile_testcase/dhrystone_onefile_testcase.elf',
    #     'loop_pc': '0x700005e8',
    #     # 其他需要覆盖的参数...
    # },
    # {
    #     'kernel': '/Data/longting.du/work/case/dhry/dhrystone_xiangshan_loop500_testcase/dhrystone_xiangshan_loop500_testcase.elf',
    #     'loop_pc': '0x70002094',
    #     # 其他需要覆盖的参数...
    # },
    # {
    #     'kernel': '/Data/longting.du/work/case/dhry/dhrystone_loop500_p670_testcase/dhrystone_loop500_p670_testcase.elf',
    #     'loop_pc': '0x70002020',
    #     # 其他需要覆盖的参数...
    # },
    # {
    #     'kernel': '/Data/longting.du/work/case/whetstone/whetstone_loop_10_10_testcase/whetstone_loop_10_10_testcase.elf',
    #     'loop_pc': '0x70001e68',
    #     # 其他需要覆盖的参数...
    # },
    {
        "kernel": "/Data/longting.du/case/sifive-dhrystone_loop500_publictoolchains_testcase/sifive-dhrystone_loop500_publictoolchains_testcase.elf",
        "loop_pc": "0x70003304",
        # 其他需要覆盖的参数...
    },
    {
        "kernel": "/Data/longting.du/case/align_dhry/sifive-dhrystone_loop500_publictoolchains_testcase_align/sifive-dhrystone_loop500_publictoolchains_testcase_align.elf",
        "loop_pc": "0x70001598",
        # 其他需要覆盖的参数...
    },
    {
        "kernel": "/Data/longting.du/case/dhry/dhrystone_loop500_p670_testcase/dhrystone_loop500_p670_testcase.elf",
        "loop_pc": "0x70002020",
        # 其他需要覆盖的参数...
    },
    {
        "kernel": "/Data/longting.du/case/align_dhry/dhrystone_loop500_p670_testcase_align/dhrystone_loop500_p670_testcase_align.elf",
        "loop_pc": "0x70001558",
        # 其他需要覆盖的参数...
    },
    {
        "kernel": "/Data/longting.du/case/dhry/dhrystone_xiangshan_loop500_testcase/dhrystone_xiangshan_loop500_testcase.elf",
        "loop_pc": "0x70002094",
        # 其他需要覆盖的参数...
    },
    {
        "kernel": "/Data/longting.du/case/align_dhry/dhrystone_xiangshan_loop500_testcase_align/dhrystone_xiangshan_loop500_testcase_align.elf",
        "loop_pc": "0x700012bc",
        # 其他需要覆盖的参数...
    },
    {
        "kernel": "/Data/longting.du/case/dhry/dhrystone_onefile_testcase/dhrystone_onefile_testcase.elf",
        "loop_pc": "0x700005e8",
        # 其他需要覆盖的参数...
    },
    {
        "kernel": "/Data/longting.du/case/align_dhry/dhrystone_onefile_testcase_align/dhrystone_onefile_testcase_align.elf",
        "loop_pc": "0x700012bc",
        # 其他需要覆盖的参数...
    },
    # {
    #     'kernel': '/Data/longting.du/case/coremark/coremark_loop1_testcase/coremark_loop1_testcase.elf',
    #     # 其他需要覆盖的参数...
    # },
    # {
    #     'kernel': '/Data/longting.du/case/coremark/coremark_loop3_testcase/coremark_loop3_testcase.elf',
    #     'loop_pc': '0x70004e20',
    #     # 其他需要覆盖的参数...
    # },
    # {
    #     'kernel': '/Data/longting.du/case/coremark_70002000/coremark_loop50_testcase/coremark_loop50_testcase.elf',
    #     'loop_pc': '0x700055a0',
    #     # 其他需要覆盖的参数...
    # },
    # {
    #     'kernel': '/Data/longting.du/case/nexus-am/apps/coremark_loop50_compress_testcase/build/coremark_loop50_compress_testcase-riscv64-xs.elf',
    #     'loop_pc': '0x8000015e',
    #     # 其他需要覆盖的参数...
    # },
    # {
    #     'kernel': '/Data/longting.du/case/nexus-am/apps/coremark_loop50_zbb_zba_zbs_testcase/build/coremark_loop50_zba_zbb_testcase-riscv64-xs.elf',
    #     'loop_pc': '0x80000170',
    #     # 其他需要覆盖的参数...
    # },
    # {
    #     'kernel': '/Data/hanfu.xing/nexus-am/appsrxu/coremark_zicond/coremark_loop50_gc_zicond_zbb_testcase/build/coremark_loop50_zicond_zbb_testcase-riscv64-xs.elf',
    #     'loop_pc': '0x80000480',
    #     # 其他需要覆盖的参数...
    # },
    # {
    #     'kernel': '/Data/longting.du/case/linpack/linpack_size100x100_loop5_testcase_rolling/linpack_size100x100_loop5_testcase.elf',
    #     # 其他需要覆盖的参数...
    # },
    # {
    #     'kernel': '/Data/longting.du/work/case/coremark/coremark_loop10_testcase/coremark_loop10_testcase.elf',
    #     'loop_pc': '0x70004eb0',
    #     # 其他需要覆盖的参数...
    # },
    # {
    #     'kernel': '/Data/longting.du/work/case/coremark/coremark_loop500_testcase/coremark_loop500_testcase.elf',
    #     'loop_pc': '0x70004c44',
    #     # 其他需要覆盖的参数...
    # },
    # {
    #     'kernel': '/Data/longting.du/work/case/randomx/randomx_rmxext_program0_seed1_iter2048_02_testcase/randomx_rmxext_program0_seed1_iter2048_02_testcase.elf',
    #     'loop_pc': '0x70da555c',
    #     'cpu_clock': '0.27GHz',
    #     # 其他需要覆盖的参数...
    # },
    # {
    #     'kernel': '/Data/longting.du/work/case/randomx/randomx_rmxext_program0_seed2_iter2048_02_testcase/randomx_rmxext_program0_seed2_iter2048_02_testcase.elf',
    #     'loop_pc': '0x70da264c',
    #     'cpu_clock': '0.27GHz',
    #     # 其他需要覆盖的参数...
    # },
    # {
    #     'kernel': '/Data/longting.du/work/case/randomx/randomx_rmxext_program0_seed2_iter200_02_onlyl1_testcase/randomx_rmxext_program0_seed2_iter200_02_onlyl1_testcase.elf',
    #     'loop_pc': '0x70da2d6c',
    #     'cpu_clock': '0.27GHz',
    #     # 其他需要覆盖的参数...
    # },
    # {
    #     'kernel': '/Data/longting.du/work/case/randomx-slic/randomx_slic8k_program0_iter2048_testcase.elf',
    #     'loop_pc': '0x70e02ad4',
    #     'cpu_clock': '0.27GHz',
    #     # 其他需要覆盖的参数...
    # },
    # {
    #     'kernel': '/Data/longting.du/work/case/randomx-slic/randomx_slic8k_program1_iter2048_testcase.elf',
    #     'loop_pc': '0x70e02ad4',
    #     'cpu_clock': '0.27GHz',
    #     # 其他需要覆盖的参数...
    # },
    # {
    #     'kernel': '/Data/longting.du/work/case/randomx-slic/randomx_slic8k_program2_iter2048_testcase.elf',
    #     'loop_pc': '0x70e02ad4',
    #     'cpu_clock': '0.27GHz',
    #     # 其他需要覆盖的参数...
    # },
    # {
    #     'kernel': '/Data/longting.du/work/case/randomx-slic/randomx_slic8k_program3_iter2048_testcase.elf',
    #     'loop_pc': '0x70e02ad4',
    #     'cpu_clock': '0.27GHz',
    #     # 其他需要覆盖的参数...
    # },
    # {
    #     'kernel': '/Data/longting.du/work/case/randomx-slic/randomx_slic8k_program4_iter2048_testcase.elf',
    #     'loop_pc': '0x70e02ad4',
    #     'cpu_clock': '0.27GHz',
    #     # 其他需要覆盖的参数...
    # },
    # {
    #     'kernel': '/Data/longting.du/work/case/randomx-slic/randomx_slic8k_program5_iter2048_testcase.elf',
    #     'loop_pc': '0x70e02ad4',
    #     'cpu_clock': '0.27GHz',
    #     # 其他需要覆盖的参数...
    # },
    # {
    #     'kernel': '/Data/longting.du/work/case/randomx-slic/randomx_slic8k_program6_iter2048_testcase.elf',
    #     'loop_pc': '0x70e02ad4',
    #     'cpu_clock': '0.27GHz',
    #     # 其他需要覆盖的参数...
    # },
    # {
    #     'kernel': '/Data/longting.du/work/case/randomx-slic/randomx_slic8k_program7_iter2048_testcase.elf',
    #     'loop_pc': '0x70e02ad4',
    #     'cpu_clock': '0.27GHz',
    #     # 其他需要覆盖的参数...
    # },
    # {
    #     'kernel': '/Data/longting.du/work/case/randomx-slic/randomx_slic8k_program8_iter2048_testcase.elf',
    #     'loop_pc': '0x70e02ad4',
    #     'cpu_clock': '0.27GHz',
    #     # 其他需要覆盖的参数...
    # },
    # {
    #     'kernel': '/Data/longting.du/work/case/randomx-slic/randomx_slic8k_program9_iter2048_testcase.elf',
    #     'loop_pc': '0x70e02ad4',
    #     'cpu_clock': '0.27GHz',
    #     # 其他需要覆盖的参数...
    # },
    # {
    #     'kernel': '/Data/longting.du/work/nexus-am/apps/linpack/linpack_size200_unrolling/build/linpack_size200_unrolling-riscv64-xs.elf',
    #     # 其他需要覆盖的参数...
    # },
    # {
    #     'kernel': '/Data/longting.du/work/nexus-am/apps/linpack/linpack_size200_rolling/build/linpack_size200_rolling-riscv64-xs.elf',
    #     # 其他需要覆盖的参数...
    # },
    # {
    #     'kernel': '/Data/longting.du/work/nexus-am/apps/linpack/linpack_size300_unrolling/build/linpack_size300_unrolling-riscv64-xs.elf',
    #     # 其他需要覆盖的参数...
    # },
    # {
    #     'kernel': '/Data/longting.du/work/nexus-am/apps/linpack/linpack_size300_rolling/build/linpack_size300_rolling-riscv64-xs.elf',
    #     # 其他需要覆盖的参数...
    # },
    # {
    #     'kernel': '/Data/longting.du/work/nexus-am/apps/linpack/linpack_size400_unrolling/build/linpack_size400_unrolling-riscv64-xs.elf',
    #     # 其他需要覆盖的参数...
    # },
    # {
    #     'kernel': '/Data/longting.du/work/nexus-am/apps/linpack/linpack_size400_rolling/build/linpack_size400_rolling-riscv64-xs.elf',
    #     # 其他需要覆盖的参数...
    # },
    # 添加更多case配置...
]

for case in simulation_cases:
    # 将默认参数与case参数合并，case参数优先
    params = {**default_params, **case}
    kernel_name = os.path.splitext(os.path.basename(params['kernel']))[0]
    outdir = os.path.join(default_output_root, kernel_name)
    stats_file = f"{kernel_name}.stat"

    # 确保输出目录存在
    os.makedirs(outdir, exist_ok=True)

    command = [
        "time", params['gem5_path'],
        "--outdir=" + outdir,
        "--stats-file", stats_file,
        params['config_script'],
        "--bare-metal",
        "--rotating",
        "--kernel", params['kernel'],
        "--dtb-file", params['dtb_file'],
        "--caches",
        "--rxu-rename",
        "--l1i_size=128kB",
        "--l1i_assoc=8",
        "--l1d_size=128kB",
        "--l1d_assoc=8",
        "--cpu-clock", params['cpu_clock'],
        "--abs-max-tick", params['abs_max_tick'],
        "--maxinsts", params['maxinsts'],
        "--loop-pc", params['loop_pc'],
    ]
    #  "--l1d-hwp-type=XSCompositePrefetcher",
    #     "--short-stride-thres=0",
    #     "--l2cache",
    #     "--l2_size=2MB",
    #     "--l2_assoc=16",
    #     "--l3cache",
    #     "--l3_size=32MB",
    #     "--l3_assoc=16",
    #     "--l1-to-l2-pf-hint",
    #     "--l2-hwp-type=WorkerPrefetcher",
    #     "--l2-to-l3-pf-hint",
    #     "--l3-hwp-type=WorkerPrefetcher",
    #     "--mem-type=DRAMsim3",
    #     "--dramsim3-ini=/Data/zhuohao.zhang/prefetch_cp/rxu-gem5/ext/dramsim3/xiangshan_configs/xiangshan_DDR4_8Gb_x8_3200_2ch.ini",

    print(f"Running gem5 simulation for {params['kernel']}...")
    subprocess.run(command)
    print(f"Finished simulation for {params['kernel']}. Output is in the directory {outdir}")

print("All simulations are done.")

# 映射字典，键是完整的变量名，值是简写
stat_mappings = {
        "system.cpu.numCycles": "total_cycles",
    "simInsts": "total_insts",
    "system.cpu.ipc": 'IPC',
    "system.cpu.ipc301_400loop": "IPC",
    "system.cpu.bpu1MissRate": "bpuMissRate",
    "system.cpu.lsq0.loadToUse::mean": "loadToUse",
    "system.cpu.loopStats1.commitRetiredInsts": "Loop_insts",
    "system.cpu.commit.rbkCount": "rbk",
    "system.cpu.decode.branchMispred": "decode_Mispreds",
    "system.cpu.commit.branchMispredicts": "commit_Mispreds",
    "system.cpu.ew.squashCycles": "flushCycles",
    #   'system.cpu.commit.totalBpuMissRate': 'bpuMissRate',
    "system.cpu.commit.retiredBranchInsts": "branchInsts",
    "system.cpu.commit.totalRecoverRate": "recoverRate",
    "system.cpu.dispipe0.renameStallLackRegs": "rename_LackRegs_Stall",
    "system.cpu.dispipe0.renameStallRecover": "rename_Recover_Stall",
    "system.cpu.dispipe0.renameStallSizeOver56": "rename_Over56_Stall",
    "system.cpu.predisq.predisqFullEvents": "predisq_full_times",
    "system.cpu.dispipe3.wtbFullEvents": "wtb_full_times",
    "system.cpu.dispipe3.ibuffer0FullEvents": "wtb_intNormal_full_times",
    "system.cpu.ibuffer0_UtilizationRate": "intNormal_UtilizationRate",
    "system.cpu.dispipe3.ibuffer2FullEvents": "wtb_intSpecial_full_times",
    "system.cpu.ibuffer2_UtilizationRate": "intSpecial_UtilizationRate",
    "system.cpu.dispipe1.DisqFullEvents": "disq_full_times",
    "system.cpu.predisq.Out8Rate": "Out8Rate",
    "system.cpu.predisq.Out5_7Rate": "Out5-7Rate",
    "system.cpu.predisq.Out1_4Rate": "Out1-4Rate",
    "system.cpu.predisq.Out0StallRate": "Out0StallRate",
    "system.cpu.predisq.Out0NoStallRate": "Out0NoStallRate",
    "system.cpu.predisq.Out8": "Out8_times",
    "system.cpu.predisq.Out5_7": "Out5-7_times",
    "system.cpu.predisq.Out1_4": "Out1-4_times",
    "system.cpu.predisq.Out0Stall": "Out0Stall_times",
    "system.cpu.predisq.Out0NoStall": "Out0NoStall_times",
    # 添加更多的映射...
}

# 使用正则表达式解析.stat文件并提取数据
def parse_stat_file(stat_file_path, stat_mappings):
    data = {key: "None" for key in stat_mappings}  # 初始化所有键值为"None"
    pattern = re.compile(r'(\S+)\s+([\d\.]+|nan)\s+.*')  # 匹配数字或字符串"nan"
    with open(stat_file_path) as f:
        for line in f:
            match = pattern.match(line)
            if match:
                var_name, var_value = match.groups()
                if var_name in stat_mappings and data[var_name] == "None":
                    # 如果值是字符串"nan"，则填入字符串"nan"
                    # 如果值是数字，则填入该数字
                    # 只有当该键的值还没有被设置时（即为"None"），才设置它
                    data[var_name] = var_value if var_value.lower() != "nan" else "nan"
    return data

# 创建表格并保存为CSV和XLSX格式
def generate_tables(simulation_cases, default_output_root, stat_mappings):
    # 准备pandas DataFrame的数据
    data_for_df = []

    for case in simulation_cases:
        kernel_name = os.path.splitext(os.path.basename(case['kernel']))[0]
        outdir = os.path.join(default_output_root, kernel_name)
        stats_file = os.path.join(outdir, f"{kernel_name}.stat")

        if os.path.getsize(stats_file) > 0:
            data = parse_stat_file(stats_file, stat_mappings)
            data_for_df.append([kernel_name] + [data.get(key) for key in stat_mappings.keys()])
        else:
            print(f"Error: The .stat file for {kernel_name} is empty.")

    # 创建DataFrame
    df = pd.DataFrame(data_for_df, columns=['Kernel'] + list(stat_mappings.values()))

    # 确保输出目录存在
    output_dir = os.path.join(default_output_root, 'tables')
    os.makedirs(output_dir, exist_ok=True)

    # 保存CSV和XLSX文件
    csv_file_path = os.path.join(output_dir, 'summary.csv')
    xlsx_file_path = os.path.join(output_dir, 'summary.xlsx')

    df.to_csv(csv_file_path, index=False)
    df.to_excel(xlsx_file_path, index=False, engine='openpyxl')

    print(f"Tables saved in {output_dir}")

# 在所有模拟执行完毕之后调用生成表格的函数
generate_tables(simulation_cases, default_output_root, stat_mappings)

# 使用方法：修改文件后执行以下命令
# python3 regression.py