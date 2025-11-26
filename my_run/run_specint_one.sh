time \
/Data3/yutong.han/riscv/rxu-gem5/build/RISCV/gem5.opt \
--outdir=/Data3/yutong.han/riscv/rxu-gem5/out/445.gobmk/score2/1647 \
--stats-file stats.txt \
/Data3/yutong.han/riscv/rxu-gem5/configs/example/fs.py \
--xiangshan-system \
--cpu-type=RxuO3CPU \
--mem-size=12GB \
--caches --cacheline_size=64 --l1i_size=128kB --l1i_assoc=8 --l1d_size=128kB --l1d_assoc=8 \
--l2cache --l2_size=2MB --l2_assoc=16 \
--l3cache --l3_size=32MB --l3_assoc=16 \
--l1d-hwp-type=XSCompositePrefetcher \
--l1-to-l2-pf-hint \
--l2-hwp-type=WorkerPrefetcher \
--l2-to-l3-pf-hint \
--l3-hwp-type=WorkerPrefetcher \
--short-stride-thres=0 \
--uop-cache \
--TAGE \
--generic-rv-cpt=/Data3/xiaohan.zhang/workspace/SPECint2006_NEMU_GV_Zba_Zbb/445.gobmk/score2/1647/_1647_0.025595_memory_.gz \
--gcpt-restorer=/Data2/suwei.ye/workspace/specxiangshanflow/NEMU/resource/gcpt_restore/build/gcpt.bin \
--mem-type=DDR5_8400_4x8 \
--rotating \
--rxu-rename \
--warmup-insts-no-switch=20000000 \
--maxinsts=40000000 \
--cpu-clock=2GHz \
# --debug-file=rxu.log \
# --debug-flags=RxuO3CPUAll \
# --debug-start=4885421500 \
# --debug-end=160000000 \
# --debug-start=1921410000 \
# --debug-flags=CommitInsts \
# --debug-file=rxu.log \
# /Data3/lian.wang/public/rxu-gem5/build/RISCV/gem5.opt \
# --outdir=/Data3/lian.wang/public/rxu-gem5/out/omnetpp_16482_r \
# --stats-file stats.txt \
# --debug-flags=CommitInsts \
# --debug-file=rxu.log \
# /Data3/lian.wang/public/rxu-gem5/configs/example/fs.py \
# --xiangshan-system \
# --cpu-type=RxuO3CPU \
# --mem-size=4GB \
# --caches --cacheline_size=64 --l1i_size=128kB --l1i_assoc=8 --l1d_size=128kB --l1d_assoc=8 \
# --l2cache --l2_size=2MB --l2_assoc=16 \
# --TAGE \
# --generic-rv-cpt=/Data/longting.du/case/spec06-cpt-gz/spec06_rv64gcb_O3_20m_gcc12.2.0-intFpcOff-jeMalloc/checkpoint-0-0-0/omnetpp/16482/_16482_0.269784_.gz \
# --gcpt-restorer=/Data/longting.du/gem5/xs_release/gcpt-restorer-231016.bin \
# --warmup-insts-no-switch=20000000 \
# --maxinsts=40000000 \
# --rotating \
# --rxu-rename \
# --debug-start=684816500 \
# --debug-end=686816500 \
# --abs-max-tick 20000000
# --debug-flags=RxuO3CPUAll \
# --debug-file=rxu.log \
# --debug-flags=CommitInst \
# --debug-file=rxu.log \
# --debug-start=783005000 \
# --debug-end=784145000 \
# --debug-flags=RxuO3CPUAll \
# --debug-file=ra2.log \
# --debug-start=1915967500 \
# --debug-end=1917967500 \


# /Data/longting.du/work/case/spec06-cpt-gz/spec06_rv64gcb_20m_llvm_peak/checkpoint-0-0-0/gcc_expr/1221/_1221_0.095615_.gz   hash_it
# /Data/longting.du/work/case/spec06-cpt-gz/spec06_rv64gcb_O3_20m_gcc12.2.0-intFpcOff-jeMalloc/checkpoint-0-0-0/gcc_166/1055/_1055_0.001354_.gz
# /Data/longting.du/work/case/spec06-cpt-gz/spec06_rv64gcb_20m_llvm_peak/checkpoint-0-0-0/hmmer_nph3/0/_0_0.000030_.gz
# --enable-difftest \
# --difftest-ref-so /Data/longting.du/work/OpenXiangShan/NEMU/build/riscv64-nemu-interpreter.so \
# /Data/longting.du/work/case/spec06-cpt-gz/spec06_rv64gcb_O3_20m_gcc12.2.0-intFpcOff-jeMalloc/checkpoint-0-0-0/soplex_ref/815/_815_0.019152_.gz  regfile.hh panic: Unsupported register class type -1.
# /Data/longting.du/work/case/spec06-cpt-gz/spec06_rv64gcb_O3_20m_gcc12.2.0-intFpcOff-jeMalloc/checkpoint-0-0-0/omnetpp/23614/_23614_0.000677_.gz  segment fault
# /Data/longting.du/work/case/spec06-cpt-gz/spec06_rv64gcb_O3_20m_gcc12.2.0-intFpcOff-jeMalloc/checkpoint-0-0-0/mcf/13940/_13940_0.009296_.gz
# /Data/longting.du/work/case/spec06-cpt-gz/spec06_rv64gcb_O3_20m_gcc12.2.0-intFpcOff-jeMalloc/checkpoint-0-0-0/omnetpp/443/_443_0.023529_.gz

# --debug-flags RxuO3CPUAll \
# --debug-file debug.txt \