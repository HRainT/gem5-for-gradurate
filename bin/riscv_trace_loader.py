import bz2
import struct
import os
from environment_setup import paths

def save_custom_trace(benchmark, input_name, simpoint_id, branches):
    """将RISC-V分支数据保存为原始Pin格式"""
    trace_dir = paths.branch_trace_dir
    os.makedirs(f"{trace_dir}/{benchmark}", exist_ok=True)
    tr_path = f"{trace_dir}/{benchmark}/{benchmark}_{input_name}_simpoint{simpoint_id}_brtrace.bz2"
    
    with bz2.BZ2File(tr_path, "wb") as f:
        for pc, target, taken, br_type in branches:
            # 转换为Pin格式：uint64 pc, uint64 target, uint8 taken, uint8 type
            f.write(struct.pack('<Q', pc))      # PC
            f.write(struct.pack('<Q', target))  # 目标地址
            f.write(struct.pack('B', taken))    # 跳转方向 (0 or 1)
            f.write(struct.pack('B', br_type))  # 分支类型
    
    print(f"Saved {len(branches)} branches to {tr_path}")
    return tr_path
