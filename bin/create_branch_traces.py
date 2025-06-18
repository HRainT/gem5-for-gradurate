#!/usr/bin/env python3

import common
from common import PATHS, BENCHMARKS_INFO
import os
import bz2
import struct

# 不再需要Pin工具路径
TARGET_BENCHMARKS = ['473.astar']  # 改为你的基准测试名称
NUM_THREADS = 24
PROJECT_ROOT = PATHS['project_root']
INPUT_LOG_DIR = PATHS['riscv_logs_dir']
OUTPUT_TRACE_DIR = PATHS['branch_traces_dir']
# 分支类型映射（根据你的RISC-V分支类型调整）
BRANCH_TYPE_MAP = {
    "NOT_BR": 0,    
    "COND_DIRECT": 1,       # 条件直接跳转 BR
    "COND_INDIRECT": 2,        #无此类指令
    "UNCOND_DIRECT": 3,           # 无条件直接跳转 直接JUMP
    "UNCOND_INDIRECT": 4,       # 无条件间接跳转（非返回） 间接JUMP
    "CALL": 5,
    "RET": 6
}

def load_riscv_branch_data(benchmark, input_name, simpoint_id):
    """
    加载RISC-V分支数据
    参数:
        benchmark: 基准测试名称 (如 'deepsjeng')
        input_name: 输入集名称 (如 'default')
        simpoint_id: simpoint ID (如 0)
    返回:
        list of tuples: [(pc, target, taken, type), ...]
    """
    # 构建日志文件路径 - 根据你的实际路径修改
    log_path = f"{INPUT_LOG_DIR}/{benchmark}_{input_name}_simpoint{simpoint_id}.log"
    
    branches = []
    line_count = 0
    valid_count = 0
    
    try:
        with open(log_path, 'r') as f:
            for line in f:
                line_count += 1
                
                # 跳过空行和注释行
                if not line.strip() or line.startswith('#'):
                    continue
                
                # 分割行内容
                parts = line.split()
                
                # 验证格式
                if len(parts) < 4:
                    print(f"Warning: Invalid format at line {line_count}: {line.strip()}")
                    continue
                
                try:
                    # 解析PC地址 (十六进制转十进制)
                    pc = int(parts[0], 16)
                    
                    # 解析目标地址
                    target = int(parts[1], 16)
                    
                    # 解析跳转结果
                    taken = int(parts[2])
                    if taken not in (0, 1):
                        print(f"Warning: Invalid taken value at line {line_count}: {parts[2]}")
                        continue
                    
                    # 解析分支类型
                    type_str = parts[3].upper()
                    if type_str not in BRANCH_TYPE_MAP:
                        print(f"Warning: Unknown branch type at line {line_count}: {type_str}")
                        br_type = BRANCH_TYPE_MAP["OTHER"]  # 默认为其他类型
                    else:
                        br_type = BRANCH_TYPE_MAP[type_str]
                    
                    # 添加到结果列表
                    branches.append((pc, target, taken, br_type))
                    valid_count += 1
                    
                except ValueError as e:
                    print(f"Error parsing line {line_count}: {line.strip()} - {str(e)}")
    
    except FileNotFoundError:
        print(f"Error: Log file not found: {log_path}")
        return []
    
    print(f"Loaded {valid_count} valid branches from {log_path} (total lines: {line_count})")
    return branches

def save_branch_trace(trace_path, branches):
    """保存分支数据为Pin兼容格式"""
    os.makedirs(os.path.dirname(trace_path), exist_ok=True)
    
    with bz2.BZ2File(trace_path, "wb") as f:
        # 批量写入提高性能
        buffer = bytearray()
        for pc, target, taken, br_type in branches:
            # 转换为Pin格式：uint64 pc, uint64 target, uint8 taken, uint8 type
            buffer += struct.pack('<QQBB', pc, target, taken, br_type)
        f.write(buffer)
    
    print(f"Saved {len(branches)} branches to {trace_path}")

def main():
    for benchmark in TARGET_BENCHMARKS:
        output_dir = f"{OUTPUT_TRACE_DIR}/{benchmark}"
        os.makedirs(output_dir, exist_ok=True)
        
        for inp_info in BENCHMARKS_INFO[benchmark]['inputs']:
            input_name = inp_info['name']
            
            for simpoint_info in inp_info['simpoints']:
                simpoint_id = simpoint_info['id']
                trace_path = f"{output_dir}/{benchmark}_{input_name}_simpoint{simpoint_id}_brtrace.bz2"
                
                # 跳过已存在的文件
                if os.path.exists(trace_path):
                    print(f"Skipping existing trace: {trace_path}")
                    continue
                
                try:
                    # 加载RISC-V分支数据
                    branches = load_riscv_branch_data(benchmark, input_name, simpoint_id)
                    
                    if not branches:
                        print(f"Warning: No branch data loaded for {benchmark}-{input_name}-simpoint{simpoint_id}")
                        continue
                    
                    # 保存为分支轨迹文件
                    save_branch_trace(trace_path, branches)
                except Exception as e:
                    print(f"Error processing {benchmark}-{input_name}-simpoint{simpoint_id}: {str(e)}")

if __name__ == '__main__':
    main()
