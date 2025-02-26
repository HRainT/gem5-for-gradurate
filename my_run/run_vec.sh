#!/bin/bash

mode=1

if [ $mode = 1 ]; then
    time \
    ./build/RISCV/gem5.opt \
    --outdir=./out/vector/vlsseg-test  \
    --stats-file stats.txt \
    --debug-file vlsseg_v1.log \
    --debug-flags CommitInsts \
    ./configs/example/riscv/fs_linux.py \
    --bare-metal \
    --mem-size=4GB \
    --kernel=/Data3/xiaohan.zhang/nexus-am/appsrxu/template/vlsseg/build/vlsseg-riscv64-xs.elf \
    --caches \
    --cpu-type=RxuO3CPU \
    --cpu-clock 2GHz \
    --abs-max-tick 15000000000000000 \
    --rxu-rename \
    --rotating \
    --TAGE
elif [ $mode = 2 ]; then
    time \
    ./build/RISCV/gem5.opt \
    --outdir=./out/vector/vlsse-test  \
    --stats-file stats.txt \
    --debug-file vlsse_v1_all.log \
    --debug-flags RxuO3CPUAll \
    ./configs/example/riscv/fs_linux.py \
    --bare-metal \
    --mem-size=4GB \
    --kernel=/Data3/xiaohan.zhang/nexus-am/appsrxu/template/vlsseg/build/vlsseg-riscv64-xs.elf \
    --caches \
    --cpu-type=RxuO3CPU \
    --cpu-clock 2GHz \
    --abs-max-tick 15000000000000000 \
    --rxu-rename \
    --rotating \
    --TAGE
else
    echo "Invalid mode."
fi
# =======
# time \
# ./build/RISCV/gem5.opt \
# --outdir=./out/vector_eop  \
# --debug-flags RxuO3CPUAll \
# --debug-file eop_ori_all.log \
# --stats-file eop.txt \
# ./configs/example/riscv/fs_linux.py \
# --bare-metal \
# --kernel=/Data/xiaohan.zhang/nexus-am/appsrxu/template/linpack_test/build/linpack_test-riscv64-xs.elf \
# --caches \
# --cpu-type=RxuO3CPU \
# --cpu-clock 2GHz \
# --abs-max-tick 15000000000000000 \
# --rxu-rename \
# --rotating \
# --TAGE
    # time \
    # ./build/RISCV/gem5.debug \
    # --outdir=./out/vector_eop  \
    # --stats-file stats.txt \
    # --debug-flags CommitInsts \
    # --debug-file add_ld_v1.log \
    # ./configs/example/riscv/fs_linux.py \
    # --bare-metal \
    # --kernel=/Data/xiaohan.zhang/nexus-am/appsrxu/template/linpack_test/build/linpack_test-riscv64-xs.elf \
    # --caches \
    # --cpu-type=RxuO3CPU \
    # --cpu-clock 2GHz \
    # --abs-max-tick 15000000000000000 \
    # --rxu-rename \
    # --rotating \
    # --TAGE
# time \
# ./build/RISCV/gem5.opt \
# --outdir=./out/vec-daxpy  \
# --stats-file stats.txt \
# --debug-flag CommitInsts \
# --debug-file trace3.log \
# ./configs/example/riscv/fs_linux.py \
# --bare-metal \
# --kernel=/Data3/xiaohan.zhang/nexus-am/appsrxu/template/vec-daxpy/build/vec-daxpy-riscv64-xs.elf \
# --caches \
# --cpu-type RxuO3CPU \
# --cpu-clock 2GHz \
# --abs-max-tick 15000000000000000 \
# --rxu-rename \
# --rotating \
# <<<<<<< Updated upstream
# # --TAGE
# >>>>>>> Stashed changes
# =======
# # --TAGE
# >>>>>>> Stashed changes
