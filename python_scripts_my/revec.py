import os
import subprocess
import pandas as pd
import re
from datetime import datetime

# ANSI escape codes for colored output
COLOR_GREEN = "\033[32m"
COLOR_RED = "\033[31m"
COLOR_RESET = "\033[0m"

# 默认参数字典
default_params = {
    'gem5_path': './build/RISCV/gem5.opt',
    'config_script': './configs/example/riscv/fs_linux.py',
    'dtb_file': 'None',
    'cpu_clock': '2GHz',
    'abs_max_tick': '5000000000000',
    'maxinsts': '500000000',
    'loop_pc': '0x70003304',
}

# 默认的输出根目录
today = datetime.now().strftime("%Y%m%d")  # 获取当前日期，格式为YYYYMMDD
default_output_root  = os.path.join("./out", "regression-vec", today)  # 构建目录路径

# 模拟实例参数的列表，每个字典代表一个实例的配置
simulation_cases = [
    {
        'kernel': '/Data/xiaohan.zhang/nexus-am/appsrxu/vector_smoke/build/vector_smoke-riscv64-xs.elf',
        # 其他需要覆盖的参数...
    },
    {
        'kernel': '/Data/xiaohan.zhang/nexus-am/appsrxu/vector_sg2042/build/vector_sg2042-riscv64-xs.elf',
        # 其他需要覆盖的参数...
    },
    {
        'kernel': '/Data/xiaohan.zhang/nexus-am/appsrxu/lmul_2/build/lmul_2-riscv64-xs.elf',
        # 其他需要覆盖的参数...
    },
    {
        'kernel': '/Data/xiaohan.zhang/nexus-am/appsrxu/template/linpack_test/build/linpack_test-riscv64-xs.elf',
        # 其他需要覆盖的参数...
    },
    {
        'kernel': '/Data3/xiaohan.zhang/nexus-am/appsrxu/template/vec_memcpy/build/vec_memcpy-riscv64-xs.elf',
        # 其他需要覆盖的参数...
    },
    {
        'kernel': '/Data3/xiaohan.zhang/nexus-am/appsrxu/template/vec-daxpy/build/vec-daxpy-riscv64-xs.elf',
        # 其他需要覆盖的参数...
    },
    {
        'kernel': '/Data3/xiaohan.zhang/nexus-am/appsrxu/template/vec-sgemm/build/vec-sgemm-riscv64-xs.elf',
        # 其他需要覆盖的参数...
    },
    {
        'kernel': '/Data3/xiaohan.zhang/nexus-am/appsrxu/template/vec_srtcmp/build/vec_srtcmp-riscv64-xs.elf',
        # 其他需要覆盖的参数...
    },
    {
        'kernel': '/Data3/xiaohan.zhang/nexus-am/appsrxu/template/vmxr-test/build/vmxr-test-riscv64-xs.elf',
        # 其他需要覆盖的参数...
    },
    {
        'kernel': '/Data3/xiaohan.zhang/nexus-am/appsrxu/template/vluxe-test/build/vluxe-test-riscv64-xs.elf',
        # 其他需要覆盖的参数...
    },
    {
        'kernel': '/Data3/xiaohan.zhang/nexus-am/appsrxu/template/vsuxei/build/vsuxei-riscv64-xs.elf',
        # 其他需要覆盖的参数...
    },
    {
        'kernel': '/Data3/xiaohan.zhang/nexus-am/appsrxu/template/vlse/build/vlse-riscv64-xs.elf',
        # 其他需要覆盖的参数...
    },
    {
        'kernel': '/Data3/xiaohan.zhang/nexus-am/appsrxu/template/vsse/build/vsse-riscv64-xs.elf',
        # 其他需要覆盖的参数...
    },
    {
        'kernel': '/Data3/yutong.han/riscv/nexus-am/appsrxu/template/vlsseg/build/vlsseg-riscv64-xs.elf',
        # 其他需要覆盖的参数...
    },    
    {
        'kernel': '/Data3/yutong.han/riscv/nexus-am/appsrxu/template/vluxseg/build/vluxseg-riscv64-xs.elf',
        # 其他需要覆盖的参数...
    },
        {
        'kernel': '/Data3/yutong.han/riscv/nexus-am/appsrxu/template/vloxei/build/vloxei-riscv64-xs.elf',
        # 其他需要覆盖的参数...
    },
        {
        'kernel': '/Data3/yutong.han/riscv/nexus-am/appsrxu/template/vloxseg/build/vloxseg-riscv64-xs.elf',
        # 其他需要覆盖的参数...
    },
        {
        'kernel': '/Data3/yutong.han/riscv/nexus-am/appsrxu/template/vsoxei/build/vsoxei-riscv64-xs.elf',
        # 其他需要覆盖的参数...
    },
        {
        'kernel': '/Data3/lian.wang/nexus-am/appsrxu/template/vsoxseg/vsuxseg/build/vsuxseg-riscv64-xs.elf',
        # 其他需要覆盖的参数...
    },
        {
        'kernel': '/Data3/hanfu.xing/nexus-am/appsrxu/template/vcompress/build/vcompress-riscv64-xs.elf',
        # 其他需要覆盖的参数...
    },
]

# 列表来记录每个case的执行状态
simulation_results = []

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
        "--kernel", params['kernel'],
        "--dtb-file", params['dtb_file'],
        "--caches",
        "--cpu-type=RxuO3CPU",
        "--cpu-clock", params['cpu_clock'],
        "--abs-max-tick", params['abs_max_tick'],
        "--maxinsts", params['maxinsts'],
        "--loop-pc", params['loop_pc'],
        "--rotating",
        "--rxu-rename",
        "--TAGE"
    ]

    print(f"\nRunning gem5 simulation for {params['kernel']}...")
    
    try:
        # 运行子进程，实时显示输出
        result = subprocess.run(command, stdout=None, stderr=None)
        
        if result.returncode == 0:
            print(f"Finished simulation for {params['kernel']}. Output is in the directory {outdir}")
            simulation_results.append((kernel_name, 'pass'))
        else:
            print(f"Fault in simulation for {params['kernel']}. Return code: {result.returncode}")
            simulation_results.append((kernel_name, 'fault'))
    except Exception as e:
        print(f"Exception occurred while running simulation for {params['kernel']}: {e}")
        simulation_results.append((kernel_name, 'fault'))

print("\nAll simulations are done.\n")

# 映射字典，键是完整的变量名，值是简写
stat_mappings = {
    'system.cpu.ipc': 'IPC',
    'system.cpu.loopStats1.commitRetiredInsts': 'Loop_insts',
    'simInsts': 'total_insts',
    'system.cpu.numCycles': 'total_cycles',
    'system.cpu.commit.rbkCount': 'rbk',
    'system.cpu.decode.branchMispred': 'decode_Mispreds',
    'system.cpu.commit.branchMispredicts': 'commit_Mispreds',
    'system.cpu.ew.squashCycles': 'flushCycles',
    'system.cpu.commit.totalBpuMissRate': 'bpuMissRate',
    'system.cpu.commit.retiredBranchInsts': 'branchInsts',
    'system.cpu.commit.totalRecoverRate': 'recoverRate',
    'system.cpu.dispipe0.renameStallLackRegs': 'rename_LackRegs_Stall',
    'system.cpu.dispipe0.renameStallRecover': 'rename_Recover_Stall',
    'system.cpu.dispipe0.renameStallSizeOver56': 'rename_Over56_Stall',
    'system.cpu.predisq.predisqFullEvents': 'predisq_full_times',
    'system.cpu.dispipe3.wtbFullEvents': 'wtb_full_times',
    'system.cpu.dispipe3.ibuffer0FullEvents': 'wtb_intNormal_full_times',
    'system.cpu.ibuffer0_UtilizationRate': 'intNormal_UtilizationRate',
    'system.cpu.dispipe3.ibuffer2FullEvents': 'wtb_intSpecial_full_times',
    'system.cpu.ibuffer2_UtilizationRate': 'intSpecial_UtilizationRate',
    'system.cpu.dispipe1.DisqFullEvents': 'disq_full_times',
    'system.cpu.predisq.Out8Rate': 'Out8Rate',
    'system.cpu.predisq.Out5_7Rate': 'Out5-7Rate',
    'system.cpu.predisq.Out1_4Rate': 'Out1-4Rate',
    'system.cpu.predisq.Out0StallRate': 'Out0StallRate',
    'system.cpu.predisq.Out0NoStallRate': 'Out0NoStallRate',
    'system.cpu.predisq.Out8': 'Out8_times',
    'system.cpu.predisq.Out5_7': 'Out5-7_times',
    'system.cpu.predisq.Out1_4': 'Out1-4_times',
    'system.cpu.predisq.Out0Stall': 'Out0Stall_times',
    'system.cpu.predisq.Out0NoStall': 'Out0NoStall_times',
    # 添加更多的映射...
}

# 使用正则表达式解析.stat文件并提取数据
def parse_stat_file(stat_file_path, stat_mappings):
    data = {key: "None" for key in stat_mappings}  # 初始化所有键值为"None"
    pattern = re.compile(r'(\S+)\s+([\d\.]+|nan)\s+.*')  # 匹配数字或字符串"nan"
    try:
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
    except FileNotFoundError:
        print(f"Warning: Stat file {stat_file_path} not found.")
    except Exception as e:
        print(f"Error parsing stat file {stat_file_path}: {e}")
    return data

# 创建表格并保存为CSV和XLSX格式
def generate_tables(simulation_cases, default_output_root, stat_mappings):
    # 准备pandas DataFrame的数据
    data_for_df = []

    for case in simulation_cases:
        kernel_name = os.path.splitext(os.path.basename(case['kernel']))[0]
        outdir = os.path.join(default_output_root, kernel_name)
        stats_file = os.path.join(outdir, f"{kernel_name}.stat")

        if os.path.exists(stats_file) and os.path.getsize(stats_file) > 0:
            data = parse_stat_file(stats_file, stat_mappings)
            data_for_df.append([kernel_name] + [data.get(key) for key in stat_mappings.keys()])
        else:
            print(f"Error: The .stat file for {kernel_name} is missing or empty.")
            # Append "None" or appropriate default values for missing data
            data_for_df.append([kernel_name] + ["None"] * len(stat_mappings))

    # 创建DataFrame
    df = pd.DataFrame(data_for_df, columns=['Kernel'] + list(stat_mappings.values()))

    # 确保输出目录存在
    output_dir = os.path.join(default_output_root, 'tables')
    os.makedirs(output_dir, exist_ok=True)

    # 保存CSV和XLSX文件
    csv_file_path = os.path.join(output_dir, 'summary.csv')
    xlsx_file_path = os.path.join(output_dir, 'summary.xlsx')

    df.to_csv(csv_file_path, index=False)
    try:
        df.to_excel(xlsx_file_path, index=False, engine='openpyxl')
        print(f"Tables saved in {output_dir}")
    except ImportError:
        print("Error: openpyxl is not installed. Cannot save Excel file.")

# 在所有模拟执行完毕之后调用生成表格的函数
generate_tables(simulation_cases, default_output_root, stat_mappings)

# 打印模拟结果的总结
print("\nSimulation Summary:")
for kernel_name, status in simulation_results:
    if status == 'pass':
        color = COLOR_GREEN
    else:
        color = COLOR_RED
    print(f"{color}{kernel_name}: {status.upper()}{COLOR_RESET}")

# 使用方法：修改文件后执行以下命令
# python3 regression.py