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
kernel=/Data3/hanfu.xing/nexus-am/appsrxu/template/random_mem_test/build/random_mem_test-riscv64-xs.elf

out=./out/cache/random_mem_test
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
    --outdir=out/cpu2006_build_gz/astar \
    --debug-flag O3CPUAll \
    --debug-file debug.log \
    --stats-file stats.txt \
    configs/example/fs.py \
    --generic-rv-cpt=/Data3/xiaohan.zhang/workspace/elf/cpu2006_build_gz/astar \
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
elif [ $mode = 9 ]; then
    time \
    build/RISCV/gem5.opt \
    --outdir=out/473.astar/BigLakes/397 \
    --stats-file stats_0713_branchnet.txt \
    configs/example/fs.py \
    --generic-rv-cpt=/Data2/xiaohan.zhang/SPECint2006_NEMU_G_Zicond_Zba_Zbb/473.astar/BigLakes/397/_397_0.008642_memory_.gz \
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
elif [ $mode = 10 ]; then
    time \
    build/RISCV/gem5.debug \
    --outdir=out/473.astar/BigLakes/397 \
    --debug-flag Debug \
    --debug-file trace-branchnet_0713_Tage_v1.log \
    --stats-file stats_0713_Tage_v1.txt \
    configs/example/fs.py \
    --generic-rv-cpt=/Data2/xiaohan.zhang/SPECint2006_NEMU_G_Zicond_Zba_Zbb/473.astar/BigLakes/397/_397_0.008642_memory_.gz \
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
elif [ $mode = 11 ]; then
    time \
    build/RISCV/gem5.opt \
    --outdir=out/473.astar/rivers/7395 \
    --debug-start=2610232500 \
    --debug-end=2610453500 \
    --debug-flag O3CPUAll,Branch,Tage \
    --debug-file debug.log \
    --stats-file stats.txt \
    configs/example/fs.py \
    --generic-rv-cpt=/Data2/xiaohan.zhang/SPECint2006_NEMU_G_Zicond_Zba_Zbb/473.astar/BigLakes/397/_397_0.008642_memory_.gz \
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
    --mem-type=DDR5_8400_4x8 \
    --warmup-insts-no-switch=20000000 \
    --maxinsts=40000000 \
    --rotating 
else
    echo "Invalid mode."
fi