time \
build/RISCV/gem5.debug \
--outdir=./out/memleak \
--stats-file ceshi1.txt \
configs/example/riscv/fs_linux.py \
--bare-metal \
--rotating \
--kernel=/Data3/xiaohan.zhang/workspace/elf/cpu2006_build_gz/astar \
--cpu-type O3CPU \
--rxu-rename \
--caches \
--cpu-clock 2GHz \
--l1i_size=128kB \
--l1i_assoc=8 \
--l1d_size=128kB \
--l1d_assoc=8 \
--l2cache --l2_size=2MB --l2_assoc=16 \

# --loop-pc 0x700055a0 \
# valgrind \
# --leak-check=full \
# --track-origins=yes \
# --show-leak-kinds=all \
# --log-file=./out/memleak/valgrind-out_400000_modified_NEW.txt \