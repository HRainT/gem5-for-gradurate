time \
/Data3/yutong.han/riscv/rxu-gem5/build/RISCV/gem5.fast \
--outdir=/Data3/yutong.han/riscv/rxu-gem5/out/ITAGE/test  \
--stats-file 1125.stat \
/Data3/yutong.han/riscv/rxu-gem5/configs/example/riscv/fs_linux.py \
--bare-metal \
--rotating \
--kernel /Data3/yutong.han/riscv/nexus-am/appsrxu/coremark_zicond/coremark_zicondzbazbb/coremark_loop50_zicond_zba_zbb_testcase/build/coremark_loop50_zicond_zba_zbb_testcase-riscv64-xs.elf \
--dtb-file None \
--cpu-type RxuO3CPU \
--caches \
--cpu-clock 2GHz \
--abs-max-tick 15000000000000000 \
--loop-pc 0x8000015e 