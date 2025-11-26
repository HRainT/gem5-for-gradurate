#!/bin/bash

mode=8
if [ $1 ]; then
    mode=$1
fi

# kernel=/Data/xiaohan.zhang/nexus-am/appsrxu/vector_smoke/build/vector_smoke-riscv64-xs.elf
# kernel=/Data/xiaohan.zhang/nexus-am/appsrxu/template/linpack_test/build/linpack_test-riscv64-xs.elf
# kernel=/Data3/xiaohan.zhang/nexus-am/appsrxu/template/linpack_test/build/linpack_test-riscv64-xs.elf
# kernel=/Data3/xiaohan.zhang/nexus-am/appsrxu/template/vec_srtcmp/build/vec_srtcmp-riscv64-xs.elf
# kernel=/Data3/xiaohan.zhang/nexus-am/appsrxu/template/vec_memcpy/build/vec_memcpy-riscv64-xs.elf
# kernel=/Data3/xiaohan.zhang/nexus-am/appsrxu/template/vec-daxpy/build/vec-daxpy-riscv64-xs.elf
# kernel=/Data3/xiaohan.zhang/nexus-am/appsrxu/template/vluxe-test/build/vluxe-test-riscv64-xs.elf
# kernel=/Data3/hanfu.xing/work/vluxe/vluxe-test-riscv64-xs.elf
# kernel=/Data3/suwei.ye/workspace/nexus-am/appsrxu/randomxv1_hard_nonce5/build/randomxv1_hard_nonce5-riscv64-xs.elf
# kernel=/Data3/xiaohan.zhang/nexus-am/appsrxu/template/vec-sgemm/build/vec-sgemm-riscv64-xs.elf
# kernel=/Data3/xiaohan.zhang/nexus-am/appsrxu/template/vslide/build/vslide-riscv64-xs.elf
# kernel=/Data3/hanfu.xing/nexus-am/appsrxu/template/vrgather/build/vrgather-riscv64-xs.elf
# kernel=/Data3/hanfu.xing/nexus-am/appsrxu/template/vcompress/build/vcompress-riscv64-xs.elf
# kernel=/Data3/hanfu.xing/nexus-am/appsrxu/template/vreduction/build/vreduction-riscv64-xs.elf
# kernel=/Data3/hanfu.xing/nexus-am/appsrxu/template/vfwadd_sub/build/vfwadd_sub-riscv64-xs.elf
# kernel=/Data3/yutong.han/riscv/nexus-am/appsrxu/template/vloxei/build/vloxei-riscv64-xs.elf
# kernel=/Data3/lian.wang/nexus-am/appsrxu/template/vsoxseg/vsuxseg/build/vsuxseg-riscv64-xs.elf

out=./out/vslide
gem5=./build/RISCV/gem5.opt

if [ $mode = 1 ]; then
    time \
    $gem5 \
    --outdir=$out  \
    --stats-file stats.txt \
    --debug-flag CommitInsts \
    --debug-file trace.log \
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
elif [ $mode = 2 ]; then
    time \
    $gem5 \
    --outdir=$out  \
    --stats-file stats.txt \
    --debug-flag RxuO3CPUAll \
    --debug-file debug2.log \
    --debug-start 0 \
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
elif [ $mode = 3 ]; then
    time \
    $gem5 \
    --outdir=$out  \
    --stats-file stats.txt \
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
elif [ $mode = 4 ]; then
    time \
    $gem5 \
    --outdir=$out  \
    --stats-file stats.txt \
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
elif [ $mode = 5 ]; then
    time \
    $gem5 \
    --outdir=$out  \
    --stats-file stats.txt \
    --debug-file trace-o3.log \
    --debug-flag CommitInsts \
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
elif [ $mode = 6 ]; then
    time \
    $gem5 \
    --outdir=$out  \
    --stats-file stats.txt \
    --debug-file debug.log \
    --debug-flag RxuO3CPUAll \
    --debug-start 195000000 \
    --debug-end 205000000 \
    ./configs/example/riscv/fs_linux.py \
    --bare-metal \
    --kernel=$kernel \
    --data-set=/Data3/suwei.ye/workspace/tars/dataset_b.bin \
    --caches \
    --cpu-type RxuO3CPU \
    --cpu-clock 2GHz \
    --abs-max-tick 15000000000000000 \
    --mem-size=8GB \
    --mem-type=SimpleMemory \
    --maxinsts=40000000 \
    --rxu-rename \
    --rotating \
    --TAGE
elif [ $mode = 7 ]; then
    time \
    $gem5 \
    --outdir=$out  \
    --stats-file randomxv1_hard_nonce5_0208.stat \
    ./configs/example/riscv/fs_linux.py \
    --bare-metal \
    --kernel /Data3/suwei.ye/workspace/nexus-am/appsrxu/randomxv1_hard_nonce5/build/randomxv1_hard_nonce5-riscv64-xs.elf \
    --data-set=/Data3/suwei.ye/workspace/tars/dataset_b.bin \
    --dtb-file None \
    --cpu-type RxuO3CPU \
    --caches \
    --l2cache --l2_size=2MB --l2_assoc=16 \
    --l3cache --l3_size=32MB --l3_assoc=16 \
    --cpu-clock 2GHz \
    --mem-size 8GB \
    --mem-type=DDR5_8400_4x8 \
    --TAGE \
    --maxinsts=100000000 \
    --abs-max-tick 10000000000000 \
    --rotating \
    --rxu-rename
elif [ $mode = 8 ]; then
    time \
    $gem5 \
    --outdir=./out/mcf \
    --stats-file stats.txt \
    --debug-file 3_rxu_all.log \
    --debug-flag RxuO3CPUAll \
    ./configs/deprecated/example/se.py \
    --cmd=/Data/yuchen.hu/work/spec/spec2006/benchspec/CPU2006/429.mcf/exe/mcf_base.rv64gv_gcc14 \
    --mem-size=2GB \
    --restore-simpoint-checkpoint \
    --checkpoint-restore=3 \
    --checkpoint-dir=/Data3/xiaohan.zhang/work/rv64gv_gcc14/mcf/mcf \
    --cpu-type RxuO3CPU \
    --cpu-clock 2GHz \
    --rxu-rename \
    --uop-cache \
    --rotating \
    --TAGE \
    --caches \
    --l2cache \
    --maxinsts=40000000 \
    --enable-arch-db \
    --l1i_size=128kB \
    --l1i_assoc=8 \
    --l1d_size=128kB \
    --l1d_assoc=8 \
    --mem-type=DDR5_8400_4x8 \
    --l1d-hwp-type=XSCompositePrefetcher \
    --l1-to-l2-pf-hint \
    --l2-hwp-type=WorkerPrefetcher \
    --short-stride-thres=0 \
    --l2_size=2MB \
    --l2_assoc=16 \
    --l3cache \
    --l3_size=32MB \
    --l3_assoc=16 \
    --l2-to-l3-pf-hint \
    --l3-hwp-type=WorkerPrefetcher
elif [ $mode = 9 ]; then
    time \
    $gem5 \
    --outdir=./out/mcf \
    --stats-file stats.txt \
    --debug-flag RxuO3CPUAll \
    --debug-file 1_all.log \
    ./configs/deprecated/example/se.py \
    --cmd=/Data/yuchen.hu/work/spec/spec2006/benchspec/CPU2006/429.mcf/exe/mcf_base.rv64gv_gcc14 \
    --mem-size=2GB \
    --restore-simpoint-checkpoint \
    --checkpoint-restore=1 \
    --checkpoint-dir=/Data3/xiaohan.zhang/work/rv64gv_gcc14/mcf/mcf \
    --cpu-type RxuO3CPU \
    --cpu-clock 2GHz \
    --rxu-rename \
    --uop-cache \
    --rotating \
    --TAGE \
    --caches \
    --l2cache \
    --maxinsts=40000000 \
    --enable-arch-db \
    --l1i_size=128kB \
    --l1i_assoc=8 \
    --l1d_size=128kB \
    --l1d_assoc=8 \
    --mem-type=DDR5_8400_4x8 \
    --l1d-hwp-type=XSCompositePrefetcher \
    --l1-to-l2-pf-hint \
    --l2-hwp-type=WorkerPrefetcher \
    --short-stride-thres=0 \
    --l2_size=2MB \
    --l2_assoc=16 \
    --l3cache \
    --l3_size=32MB \
    --l3_assoc=16 \
    --l2-to-l3-pf-hint \
    --l3-hwp-type=WorkerPrefetcher 
else
    echo "Invalid mode."
fi