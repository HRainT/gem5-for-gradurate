#!/bin/bash

mode=10
if [ $1 ]; then
    mode=$1
fi

# kernel=/Data/xiaohan.zhang/nexus-am/appsrxu/vector_smoke/build/vector_smoke-riscv64-xs.elf
# kernel=/Data/xiaohan.zhang/nexus-am/appsrxu/template/linpack_test/build/linpack_test-riscv64-xs.elf
# kernel=/Data3/xiaohan.zhang/nexus-am/appsrxu/template/linpack_50x50_loop100/build/linpack_50x50_loop100-riscv64-xs.elf
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
kernel=/Data/longting.du/case/dhry/dhrystone_loop500_p670_testcase/dhrystone_loop500_p670_testcase.elf

out=./out/decoupled_front
gem5=./build/RISCV/gem5.opt

if [ $mode = 1 ]; then
    time \
    $gem5 \
    --outdir=$out  \
    --stats-file stats.txt \
    --debug-flag RxuO3CPUAll \
    --debug-file debug.log \
    ./configs/example/riscv/fs_linux.py \
    --bare-metal \
    --kernel=$kernel \
    --cpu-type RxuO3CPU \
    --caches \
    --l2cache --l2_size=2MB --l2_assoc=16 \
    --cpu-clock 2GHz \
    --mem-size 8GB \
    --mem-type=DDR5_8400_4x8 \
    --abs-max-tick 15000000000000000 \
    --rxu-rename \
    --rotating \
    --TAGE
elif [ $mode = 2 ]; then
    time \
    $gem5 \
    --outdir=$out  \
    --stats-file stats.txt \
    ./configs/example/riscv/fs_linux.py \
    --bare-metal \
    --kernel=$kernel \
    --cpu-type RxuO3CPU \
    --caches \
    --l2cache --l2_size=2MB --l2_assoc=16 \
    --cpu-clock 2GHz \
    --mem-size 8GB \
    --mem-type=DDR5_8400_4x8 \
    --abs-max-tick 15000000000000000 \
    --rxu-rename \
    --rotating \
    --TAGE
elif [ $mode = 3 ]; then
    time \
    $gem5 \
    --outdir=$out  \
    --stats-file stats.txt \
    --debug-flag CacheAll,RxuO3CPUAll,CacheTrace,CoherentXBar \
    --debug-file cache_all.log \
    ./configs/example/riscv/fs_linux.py \
    --bare-metal \
    --kernel=$kernel \
    --cpu-type RxuO3CPU \
    --caches \
    --l2cache --l2_size=2MB --l2_assoc=16 \
    --cpu-clock 2GHz \
    --mem-size 8GB \
    --mem-type=DDR5_8400_4x8 \
    --abs-max-tick 15000000000000000 \
    --rxu-rename \
    --rotating \
    --TAGE
elif [ $mode = 4 ]; then
    time \
    $gem5 \
    --outdir=$out  \
    --stats-file stats.txt \
    --debug-flag CacheAll \
    --debug-flag CacheTrace \
    --debug-flag CoherentXBar \
    --debug-file cache.log \
    ./configs/example/riscv/fs_linux.py \
    --bare-metal \
    --kernel=$kernel \
    --cpu-type RxuO3CPU \
    --caches \
    --l2cache --l2_size=2MB --l2_assoc=16 \
    --cpu-clock 2GHz \
    --mem-size 8GB \
    --mem-type=DDR5_8400_4x8 \
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
    build/RISCV/gem5.opt \
    --outdir=out/gcc/166/1862 \
    --stats-file stats.txt \
    configs/example/fs.py \
    --generic-rv-cpt=/Data3/xiaohan.zhang/workspace/SPECint2006_NEMU_GV_Zba_Zbb/403.gcc/scilab/1049/_1049_0.027778_memory_.gz \
    --gcpt-restorer=/Data3/suwei.ye/workspace/nexus-am/appsrxu/template/simpoint_case/spec2006_xssimpoint_timer/dir/gcpt_restore/build/gcpt.bin \
    --xiangshan-system \
    --cpu-type=RxuO3CPU \
    --mem-size=12GB \
    --caches \
    --cacheline_size=64 \
    --l1i_size=128kB \
    --l1i_assoc=8 \
    --l1d_size=128kB \
    --l1d_assoc=8 \
    --l2cache \
    --l2_size=2MB \
    --l2_assoc=16 \
    --l1d-hwp-type=XSCompositePrefetcher \
    --short-stride-thres=0 \
    --l3cache \
    --l3_size=32MB \
    --l3_assoc=16 \
    --l1-to-l2-pf-hint \
    --l2-hwp-type=WorkerPrefetcher \
    --l2-to-l3-pf-hint \
    --l3-hwp-type=WorkerPrefetcher \
    --cpu-clock=2GHz \
    --TAGE \
    --uop-cache \
    --mem-type=DDR5_8400_4x8 \
    --warmup-insts-no-switch=20000000 \
    --maxinsts=40000000 \
    --rotating \
    --rxu-rename
elif [ $mode = 9 ]; then
    time \
    build/RISCV/gem5.fast \
    --outdir=out/test \
    --stats-file stats_v4.txt \
    configs/example/fs.py \
    --generic-rv-cpt=/Data2/xiaohan.zhang/spec06_NEMU_GZ_V0.1/473.astar/rivers/28315/_28315_0.062681_memory_.gz \
    --gcpt-restorer=/Data3/suwei.ye/workspace/nexus-am/appsrxu/template/simpoint_case/spec2006_xssimpoint_timer/dir/gcpt_restore/build/gcpt.bin \
    --xiangshan-system \
    --cpu-type=RxuO3CPU \
    --mem-size=12GB \
    --caches \
    --cacheline_size=64 \
    --l1i_size=128kB \
    --l1i_assoc=8 \
    --l1d_size=128kB \
    --l1d_assoc=8 \
    --l2cache \
    --l2_size=2MB \
    --l2_assoc=16 \
    --l1d-hwp-type=XSCompositePrefetcher \
    --short-stride-thres=0 \
    --l3cache \
    --l3_size=32MB \
    --l3_assoc=16 \
    --l1-to-l2-pf-hint \
    --l2-hwp-type=WorkerPrefetcher \
    --l2-to-l3-pf-hint \
    --l3-hwp-type=WorkerPrefetcher \
    --cpu-clock=2GHz \
    --TAGE \
    --uop-cache \
    --mem-type=DDR5_8400_4x8 \
    --warmup-insts-no-switch=20000000 \
    --maxinsts=40000000 \
    --rotating \
    --rxu-rename \
    --sr
elif [ $mode = 10 ]; then
    time \
    build/RISCV/gem5.fast \
    --outdir=out/test \
    --stats-file stats_standard.txt \
    configs/example/fs.py \
    --generic-rv-cpt=/Data2/xiaohan.zhang/spec06_NEMU_GZ_V0.1/473.astar/rivers/28315/_28315_0.062681_memory_.gz \
    --gcpt-restorer=/Data3/suwei.ye/workspace/nexus-am/appsrxu/template/simpoint_case/spec2006_xssimpoint_timer/dir/gcpt_restore/build/gcpt.bin \
    --xiangshan-system \
    --cpu-type=RxuO3CPU \
    --mem-size=12GB \
    --caches \
    --cacheline_size=64 \
    --l1i_size=128kB \
    --l1i_assoc=8 \
    --l1d_size=128kB \
    --l1d_assoc=8 \
    --l2cache \
    --l2_size=2MB \
    --l2_assoc=16 \
    --l1d-hwp-type=XSCompositePrefetcher \
    --short-stride-thres=0 \
    --l3cache \
    --l3_size=32MB \
    --l3_assoc=16 \
    --l1-to-l2-pf-hint \
    --l2-hwp-type=WorkerPrefetcher \
    --l2-to-l3-pf-hint \
    --l3-hwp-type=WorkerPrefetcher \
    --cpu-clock=2GHz \
    --TAGE \
    --uop-cache \
    --mem-type=DDR5_8400_4x8 \
    --warmup-insts-no-switch=20000000 \
    --maxinsts=40000000 \
    --rotating \
    --rxu-rename 
elif [ $mode = 11 ]; then
    time \
    build/RISCV/gem5.opt \
    --outdir=out/473.astar/rivers/7395 \
    --stats-file stats.txt \
    configs/example/fs.py \
    --generic-rv-cpt=/Data3/xiaohan.zhang/workspace/SPECint2006_NEMU_G_Zicond_Zba_Zbb/473.astar/rivers/7395/_7395_0.051197_memory_.gz \
    --gcpt-restorer=/Data3/suwei.ye/workspace/nexus-am/appsrxu/template/simpoint_case/spec2006_xssimpoint_timer/dir/gcpt_restore/build/gcpt.bin \
    --xiangshan-system \
    --cpu-type=O3CPU \
    --mem-size=12GB \
    --caches \
    --cacheline_size=64 \
    --l1i_size=128kB \
    --l1i_assoc=8 \
    --l1d_size=128kB \
    --l1d_assoc=8 \
    --l2cache \
    --l2_size=2MB \
    --l2_assoc=16 \
    --l1d-hwp-type=XSCompositePrefetcher \
    --short-stride-thres=0 \
    --l3cache \
    --l3_size=32MB \
    --l3_assoc=16 \
    --l1-to-l2-pf-hint \
    --l2-hwp-type=WorkerPrefetcher \
    --l2-to-l3-pf-hint \
    --l3-hwp-type=WorkerPrefetcher \
    --cpu-clock=2GHz \
    --TAGE \
    --uop-cache \
    --mem-type=DDR5_8400_4x8 \
    --warmup-insts-no-switch=20000000 \
    --maxinsts=40000000 \
    --rotating \
    --rxu-rename
else
    echo "Invalid mode."
fi