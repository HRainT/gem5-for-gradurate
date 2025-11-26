time \
./build/RISCV/gem5.fast \
--outdir=./out/inst_compare-old \
--stats-file o3_inst.txt \
./configs/example/fs.py \
--xiangshan-system \
--cpu-type=O3CPU \
--mem-size=4GB \
--caches --cacheline_size=64 --l1i_size=128kB --l1i_assoc=8 --l1d_size=128kB --l1d_assoc=8 \
--TAGE \
--generic-rv-cpt=/Data/longting.du/case/spec06-cpt-gz/spec06_rv64gcb_20m_llvm_peak/checkpoint-0-0-0/xalancbmk/27108/_27108_0.003178_.gz \
--gcpt-restorer=/Data/longting.du/gem5/xs_release/gcpt-restorer-231016.bin \
--warmup-insts-no-switch=20000000 \
--maxinsts=40000000 \
--rotating \
--rxu-rename