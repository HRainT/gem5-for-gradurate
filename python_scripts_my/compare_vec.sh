#!/bin/bash

just_compare=$1
# 定义内核文件数组
kernels=(
    # "/Data/xiaohan.zhang/nexus-am/appsrxu/vector_smoke/build/vector_smoke-riscv64-xs.elf"
    # "/Data/xiaohan.zhang/nexus-am/appsrxu/vector_sg2042/build/vector_sg2042-riscv64-xs.elf"
    # "/Data/xiaohan.zhang/nexus-am/appsrxu/lmul_2/build/lmul_2-riscv64-xs.elf"
    # "/Data/xiaohan.zhang/nexus-am/appsrxu/template/linpack_test/build/linpack_test-riscv64-xs.elf"
    # "/Data3/xiaohan.zhang/nexus-am/appsrxu/template/vec-sgemm/build/vec-sgemm-riscv64-xs.elf"
    # "/Data3/xiaohan.zhang/nexus-am/appsrxu/template/vec_memcpy/build/vec_memcpy-riscv64-xs.elf"
    # "/Data3/xiaohan.zhang/nexus-am/appsrxu/template/vec-daxpy/build/vec-daxpy-riscv64-xs.elf"
    # "/Data3/xiaohan.zhang/nexus-am/appsrxu/template/vec_srtcmp/build/vec_srtcmp-riscv64-xs.elf"
    # "/Data3/xiaohan.zhang/nexus-am/appsrxu/template/vmxr-test/build/vmxr-test-riscv64-xs.elf"
    # "/Data3/xiaohan.zhang/nexus-am/appsrxu/template/vluxe-test/build/vluxe-test-riscv64-xs.elf"
    # "/Data3/xiaohan.zhang/nexus-am/appsrxu/template/vsuxei/build/vsuxei-riscv64-xs.elf"
    # "/Data3/xiaohan.zhang/nexus-am/appsrxu/template/vlse/build/vlse-riscv64-xs.elf"
    # "/Data3/xiaohan.zhang/nexus-am/appsrxu/template/vsse/build/vsse-riscv64-xs.elf"
    "/Data3/xiaohan.zhang/nexus-am/appsrxu/template/vlseg/build/vlseg-riscv64-xs.elf"
)

# 创建一个临时文件来保存比较结果
results_file=$(mktemp)

# 检查是否需要进行比较
if [ "$just_compare" != "0" ]; then
    # 迭代所有内核文件
    for kernel in "${kernels[@]}"; do
        echo "Testing with kernel: $kernel"
        kernel_base=$(basename "$kernel")

        # 运行第一个配置
        ./build/RISCV/gem5.debug \
        --outdir=./out/vec-compare-9/$kernel_base \
        --stats-file stats.txt \
        --debug-flag CommitInsts \
        --debug-file trace-$kernel_base.log \
        ./configs/example/riscv/fs_linux.py \
        --bare-metal \
        --kernel=$kernel \
        --caches \
        --cpu-type RxuO3CPU \
        --cpu-clock 2GHz \
        --abs-max-tick 15000000000000000 \
        --rxu-rename \
        --rotating \
        --TAGE

        # if [ -e "out/vec-compare-2/$kernel_base/trace-o3-$kernel_base.log" ]; then
        #     echo " "
        # else
            # 运行第二个配置
            ./build/RISCV/gem5.debug \
            --outdir=./out/vec-compare-9/$kernel_base \
            --stats-file stats.txt \
            --debug-flag CommitInsts \
            --debug-file trace-o3-$kernel_base.log \
            ./configs/example/riscv/fs_linux.py \
            --bare-metal \
            --kernel=$kernel \
            --caches \
            --cpu-type O3CPU \
            --cpu-clock 2GHz \
            --abs-max-tick 15000000000000000 \
            --rxu-rename \
            --rotating \
            --TAGE
        # fi
    done
fi

# 比较结果并将输出保存到临时文件
echo ""
for kernel in "${kernels[@]}"; do
    kernel_base=$(basename "$kernel")
    echo "== Comparing results for kernel: $kernel_base =======================" >> $results_file

    python3 \
    python_scripts_my/compare_args.py \
    "out/vec-compare-9/$kernel_base/trace-o3-$kernel_base.log" \
    "out/vec-compare-9/$kernel_base/trace-$kernel_base.log" >> $results_file
done

# 在所有比较完成后显示结果
cat $results_file
# 清理临时文件
rm $results_file