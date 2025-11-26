/**
 * Created by longting.du
 */

#include "cpu/rxuo3/uop_cache.hh"

#include <algorithm>
#include <random>

#include "arch/generic/pcstate.hh"
#include "arch/riscv/pcstate.hh"
#include "base/trace.hh"
#include "cpu/inst_seq.hh"
#include "cpu/rxuo3/dyn_inst.hh"
#include "cpu/rxuo3/limits.hh"
#include "debug/RxuActivity.hh"
#include "debug/RxuUC.hh"
#include "debug/RxuO3PipeView.hh"
#include "debug/RxuUopCacheConflict.hh"
#include "debug/RxuAcc.hh"
#include "params/BaseRxuO3CPU.hh"
#include "sim/full_system.hh"
#include "sim/sim_object.hh"

// clang complains about std::set being overloaded with Packet::set if
// we open up the entire namespace std
using std::list;

namespace gem5
{

namespace rxuo3
{

UopCache::UopCache(CPU *_cpu, const BaseRxuO3CPUParams &params)
    :   cpu(_cpu),
        logSize(params.logSize),
        tagBits(params.tagBits),
        setBits(params.setBits),
        assocBits(params.assocBits),
        numInstsEntry(params.numInstsEntry),
        sets(1 << setBits),
        assoc(1 << assocBits),
        setMask((1 << setBits) - 1),
        tagMask((1 << tagBits) - 1),
        initAge(params.initAge),
        shift(params.shift),
        addrMask(numInstsEntry * 4 - 1),
        _useUopCache(params.system->useUopCache()),
        useHashing(params.useHashing),
        ucWidth(params.system->ucWidth()),
        stats(_cpu)
{
    if (ucWidth > MaxWidth)
        fatal("uop cache Width (%d) is larger than compiled limit (%d),\n"
             "\tincrease MaxWidth in src/cpu/rxuo3/limits.hh\n",
             ucWidth, static_cast<int>(MaxWidth));
    for (int tid = 0; tid < MaxThreads; tid++) {
        uc_pc[tid].reset(params.isa[0]->newPCState());
    }
    cache = new UopCacheEntry[sets * assoc];

    if (params.system->ucRP() == "random") {
        rp = Random;
    } else {
        rp = LRU;
    }

}

std::string
UopCache::name() const
{
    return cpu->name() + ".UopCache";
}

UopCache::UopCacheStats::UopCacheStats(CPU *cpu)
    : statistics::Group(cpu, "UopCache"),
      ADD_STAT(idleCycles, statistics::units::Cycle::get(),
               "Number of cycles uc is idle"),
      ADD_STAT(blockedCycles, statistics::units::Cycle::get(),
               "Number of cycles uc is blocked"),
      ADD_STAT(runCycles, statistics::units::Cycle::get(),
               "Number of cycles uc is running"),
      ADD_STAT(unblockCycles, statistics::units::Cycle::get(),
               "Number of cycles uc is unblocking"),
      ADD_STAT(squashCycles, statistics::units::Cycle::get(),
               "Number of cycles uc is squashing"),
      ADD_STAT(branchResolved, statistics::units::Count::get(),
               "Number of times uc resolved a branch"),
      ADD_STAT(branchMispred, statistics::units::Count::get(),
               "Number of times uc detected a branch misprediction"),
      ADD_STAT(controlMispred, statistics::units::Count::get(),
               "Number of times uc detected an instruction incorrectly "
               "predicted as a control"),
      ADD_STAT(decodedInsts, statistics::units::Count::get(),
               "Number of instructions handled by uc"),
      ADD_STAT(squashedInsts, statistics::units::Count::get(),
               "Number of squashed instructions handled by uc"),
      ADD_STAT(bpuMissUopCacheCount, statistics::units::Count::get(),
               "Number of miss times due to BPU_predict in uc"),
      ADD_STAT(hitCycles, statistics::units::Cycle::get(), 
                "Stat for total number of hit cycles"),
      ADD_STAT(missCycles, statistics::units::Cycle::get(), 
                "Stat for total number of miss cycles"),
      ADD_STAT(cacheFull, statistics::units::Count::get(),
                "Stat for total number of cache full"),
      ADD_STAT(lookupCount, statistics::units::Count::get(),
                "Stat for total number of uop cache lookup"),
      ADD_STAT(hitRate, statistics::units::Ratio::get(),
                "Stat for uop cache hit rate"),
      ADD_STAT(cl0_hit_count, statistics::units::Count::get(),
                "Stat for total times of lookup of cacheline 0"),
      ADD_STAT(cl1_hit_count, statistics::units::Count::get(),
                "Stat for total times of lookup of cacheline 1"),   
      ADD_STAT(cl2_hit_count, statistics::units::Count::get(),
                "Stat for total times of lookup of cacheline 2"),   
      ADD_STAT(cl3_hit_count, statistics::units::Count::get(),
                "Stat for total times of lookup of cacheline 3"),   
      ADD_STAT(lookup_count, statistics::units::Count::get(),
                "Stat for total times of lookup of uop cache"),
      ADD_STAT(cl0_hitRate, statistics::units::Ratio::get(),
                "Stat for uop cache hit rate of cacheline 0"),
      ADD_STAT(cl1_hitRate, statistics::units::Ratio::get(),
                "Stat for uop cache hit rate of cacheline 1"),
      ADD_STAT(cl2_hitRate, statistics::units::Ratio::get(),
                "Stat for uop cache hit rate of cacheline 2"),
      ADD_STAT(cl3_hitRate, statistics::units::Ratio::get(),
                "Stat for uop cache hit rate of cacheline 3"),
      ADD_STAT(postJointPre, statistics::units::Count::get(),
                "Stat fot post-cacheline join in pre-cacheline"),
      ADD_STAT(postJointPreRate, statistics::units::Ratio::get(),
                "Stat for uop-cache post-joint-pre")
{
    idleCycles.prereq(idleCycles);
    blockedCycles.prereq(blockedCycles);
    runCycles.prereq(runCycles);
    unblockCycles.prereq(unblockCycles);
    squashCycles.prereq(squashCycles);
    branchResolved.prereq(branchResolved);
    branchMispred.prereq(branchMispred);
    controlMispred.prereq(controlMispred);
    decodedInsts.prereq(decodedInsts);
    squashedInsts.prereq(squashedInsts);
    bpuMissUopCacheCount.prereq(bpuMissUopCacheCount);
    hitCycles.prereq(hitCycles);
    missCycles.prereq(missCycles);
    cacheFull.prereq(cacheFull);
    lookupCount.prereq(lookupCount);

    hitRate.precision(6);
    hitRate = (hitCycles) / lookupCount;

    cl0_hit_count.prereq(cl0_hit_count);
    cl0_hit_count.prereq(cl0_hit_count);
    cl0_hit_count.prereq(cl0_hit_count);
    cl0_hit_count.prereq(cl0_hit_count);
    lookup_count.prereq(lookup_count);

    cl0_hitRate.precision(6);
    cl0_hitRate = cl0_hit_count / lookup_count;

    cl1_hitRate.precision(6);
    cl1_hitRate = cl1_hit_count / lookup_count;

    cl2_hitRate.precision(6);
    cl2_hitRate = cl2_hit_count / lookup_count;

    cl3_hitRate.precision(6);
    cl3_hitRate = cl3_hit_count / lookup_count;

    postJointPre.prereq(postJointPre);
    
    postJointPreRate.precision(6);
    postJointPreRate = postJointPre / lookupCount;
}

void
UopCache::squash(const DynInstPtr &inst, ThreadID tid)
{
    DPRINTF(RxuUC, "[tid:%i] [sn:%#x] Squashing due to incorrect branch "
            "prediction detected at uop cache.\n", tid, inst->seqNum);

    InstSeqNum squash_seq_num = inst->seqNum;

    insts.clear();

    // Squash instructions up until this one
    cpu->removeInstsUntil(squash_seq_num, tid);
}

void
UopCache::squash(ThreadID tid)
{
    DPRINTF(RxuUC, "[tid:%i] Squashing.\n",tid);

    insts.clear();
}

void
UopCache::flushUopCache() 
{      
    free(cache);
    cache = new UopCacheEntry[sets * assoc];
}

int
UopCache::lindex(Addr pc_in, unsigned instShiftAmt) 
{
    Addr pc = pc_in >> instShiftAmt;
    if (useHashing) {
        pc ^= pc_in;
        return ((pc & setMask) << assocBits);
    }
    return ((pc >> 4)& setMask);
}

int 
UopCache::finallindex(int lindex, int lowPcBits, int way) 
{
    return (useHashing ? (lindex ^ ((lowPcBits >> way) << assocBits)) :
                         (lindex << 2))
           + way;
}

bool 
UopCache::lookup(Addr pc)
{
    if (_useUopCache == false ) {
        return false;
    }

    stats.lookupCount++;

    Addr apc = alignPC(pc);
    DPRINTF(RxuAcc, "Lookup PC: %#x, Align PC: %#x.\n", pc, apc);
    if (apc > align64(pc)) {
        stats.postJointPre++;
    }

    unsigned indexB = 0;
    Addr tag;
    if (useHashing) {
        unsigned pcShift = logSize - assocBits;
        indexB = (apc >> pcShift) & setMask;
        tag = (apc >> pcShift) ^ (apc >> (pcShift + tagBits));
        tag &= tagMask;
    } else {
        unsigned pcShift = 12 ; 
        tag = apc >> pcShift;
    }

    int index = lindex(apc, shift);
    for (int i = 0; i < assoc; i++) {

        int idx = finallindex(index, indexB, i);

        if (cache[idx].valid && cache[idx].tag == tag && cache[idx].age > 0) {

            stats.hitCycles++;
            cache[idx].age++;

            list<StaticInstPtr>::iterator inst;
            list<Addr>::iterator pc_it = cache[idx].pc_list.begin();
            for (inst = cache[idx].insts.begin(); 
                 inst != cache[idx].insts.end() && pc_it != cache[idx].pc_list.end(); 
                 inst++) {
                Addr ipc = *pc_it;
                pc_it++;
                if (ipc == pc) {
                    insts.push_back(*inst);
                    pc_insts.push_back(ipc);
                    pc = *pc_it;
                }
            }
            DPRINTF(RxuAcc, "Hit, index: %#x, tag: %#x.\n", idx, tag);
            return true;
        }
    }
    stats.missCycles++;
    DPRINTF(RxuAcc, "Miss.\n");
    return false;
}

UopCache::Info 
UopCache::lookupOnly(Addr pc)
{   
    if (_useUopCache == false ) {
        return {0,0};
    }

    Addr apc = alignPC(pc);

    unsigned indexB = 0;
    Addr tag;
    if (useHashing) {
        unsigned pcShift = logSize - assocBits;
        indexB = (apc >> pcShift) & setMask;
        tag = (apc >> pcShift) ^ (apc >> (pcShift + tagBits));
        tag &= tagMask;
    } else {
        unsigned pcShift = 12 ; 
        tag = apc >> pcShift;
    }

    unsigned numInsts = 0;
    Addr tpc = 0;
    int index = lindex(apc, shift);
    for (int i = 0; i < assoc; i++) {

        int idx = finallindex(index, indexB, i);

        if (cache[idx].valid && cache[idx].tag == tag && cache[idx].age > 0) {

            list<StaticInstPtr>::iterator inst;
            list<Addr>::iterator pc_it = cache[idx].pc_list.begin();
            Addr ipc = pc;
            bool compress = false;
            for (inst = cache[idx].insts.begin(); 
                 inst != cache[idx].insts.end(); 
                 inst++) {
                ipc= *pc_it;
                pc_it++;
                if (ipc == pc) {
                    numInsts++;
                }
                compress = (*inst)->isCompressed();
            }
            tpc = ipc + (compress ? 2 : 4);
            return {numInsts, tpc};
        }
    }
    return {numInsts, tpc};
}

bool 
UopCache::update(Addr pc, 
                std::list<StaticInstPtr> &cacheLineData, 
                std::list<Addr> &pc_list)
{
    Addr apc = alignPC(pc);

    unsigned num_insts = cacheLineData.size();

    int index = lindex(apc, shift);
    unsigned indexB = 0;
    Addr tag;
    if (useHashing) {
        unsigned pcShift = logSize - assocBits;
        indexB = (apc >> pcShift) & setMask;
        tag = (apc >> pcShift) ^ (apc >> (pcShift + tagBits));
        tag &= tagMask;
    } else {
        unsigned pcShift = 12 ; 
        tag = apc >> pcShift;
    }

    int min_age_idx = 0;
    int min_age = 1024;

    for (int i = 0; i < assoc; i++) {
        int idx = finallindex(index, indexB, i);
        if (!cache[idx].valid || 
                cache[idx].age <= 0) {
            cache[idx].clear();
            cache[idx].tag = tag;
            cache[idx].valid = true;
            assert(cacheLineData.size() == pc_list.size());
            while (!cacheLineData.empty()) {
                StaticInstPtr inst = cacheLineData.front();
                Addr curPC = pc_list.front();
                cache[idx].insts.push_back(inst);
                cache[idx].pc_list.push_back(curPC);
                cache[idx].numInsts++;
                cacheLineData.pop_front();
                pc_list.pop_front();
            }
            cache[idx].age = initAge;
            cache[idx].pc = apc;

            DPRINTF(RxuUC, "[%i] insts from icache saved into ucache line [%#x] (%#x) with Index: %#x, Tag: %#x\n"
                    , cache[idx].numInsts, pc, alignPC(pc), idx, tag);
            DPRINTF(RxuAcc, "Save PC: %#x (%#x), index: %#x, tag: %#x.\n", pc, alignPC(pc), idx, tag);

            return true;
        } else if (cache[idx].valid &&
                    cache[idx].age > 0 &&
                    cache[idx].tag == tag) {
            cache[idx].age++;
            DPRINTF(RxuUC, "PC startswith %#x (%#x) is already saved in uop cache.\n", pc, apc);
            if (num_insts > cache[idx].numInsts) {
                assert(cacheLineData.size() == pc_list.size());
                cache[idx].insts.clear();
                cache[idx].pc_list.clear();
                while (!cacheLineData.empty()) {
                    StaticInstPtr inst = cacheLineData.front();
                    Addr curPC = pc_list.front();
                    cache[idx].insts.push_back(inst);
                    cache[idx].pc_list.push_back(curPC);
                    cache[idx].numInsts++;
                    cacheLineData.pop_front();
                    pc_list.pop_front();
                }
            }
            DPRINTF(RxuAcc, "A longer cache line is saved into PC: %#x (%#x), index: %#x, tag: %#x.\n", pc, alignPC(pc), idx, tag);
            return true;
        } else {
            cache[idx].age--;
            if (min_age > cache[idx].age) {
                min_age = cache[idx].age;
                min_age_idx = idx;
            }
        }
    }
    DPRINTF(RxuUC, "[%i] insts tried to save into ucache line [%#x]. UCache is full.\n"
                    , cacheLineData.size(), apc);
    ++stats.cacheFull;

    if (rp == Random) {
        DPRINTF(RxuUC, "Ramdom Replace Policy.\n");
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> distrib(0, assoc - 1);
        int way = distrib(gen);
        assert(way < assoc && way >= 0);
        int idx = finallindex(index, indexB, way);

        DPRINTF(RxuUopCacheConflict, "%#x replaced by %#x.\n", alignPC(cache[idx].pc), apc);

        cache[idx].clear();
        cache[idx].tag = tag;
        cache[idx].valid = true;
        cache[idx].pc = apc;
        while (!cacheLineData.empty()) {
            StaticInstPtr inst = cacheLineData.front();
            Addr curPC = pc_list.front();
            cache[idx].insts.push_back(inst);
            cache[idx].pc_list.push_back(curPC);
            cache[idx].numInsts++;
            cacheLineData.pop_front();
            pc_list.pop_front();
        }
        cache[idx].age = initAge;

        DPRINTF(RxuUC, "[%i] insts from icache saved into ucache line [%#x] (%#x) with Index: %#x, Way: %i, Tag: %#x\n"
                , cache[idx].numInsts, pc, alignPC(pc), idx, way, tag);
        DPRINTF(RxuAcc, "Save PC: %#x (%#x), index: %#x, tag: %#x.\n", pc, alignPC(pc), idx, tag);

        return true;
    } else {
        DPRINTF(RxuUC, "LRU Replace Policy.\n");

        DPRINTF(RxuUopCacheConflict, "%#x replaced by %#x.\n", cache[min_age_idx].pc, apc);

        cache[min_age_idx].clear();
        cache[min_age_idx].tag = tag;
        cache[min_age_idx].valid = true;
        cache[min_age_idx].pc = apc;
        while (!cacheLineData.empty()) {
            StaticInstPtr inst = cacheLineData.front();
            Addr curPC = pc_list.front();
            cache[min_age_idx].insts.push_back(inst);
            cache[min_age_idx].pc_list.push_back(curPC);
            cache[min_age_idx].numInsts++;
            cacheLineData.pop_front();
            pc_list.pop_front();
        }
        cache[min_age_idx].age = initAge;

        DPRINTF(RxuUC, "[%i] insts from icache saved into ucache line [%#x] (%#x) with lru index: %#x, Tag: %#x\n"
                , cache[min_age_idx].numInsts, pc, alignPC(pc), min_age_idx, tag);
        DPRINTF(RxuAcc, "Save PC: %#x (%#x), index: %#x, tag: %#x.\n", pc, alignPC(pc), min_age_idx, tag);

        return true;
    }

    return false;
}

DynInstPtr
UopCache::buildInst(ThreadID tid, StaticInstPtr staticInst,
        StaticInstPtr curMacroop, const PCStateBase &this_pc,
        const PCStateBase &next_pc, bool trace)
{
    // Get a sequence number.
    // InstSeqNum seq = cpu->getAndIncrementInstSeq();
    InstSeqNum seq = this_pc.instAddr();

    DynInst::Arrays arrays;
    /** for dyninst initial. */
    if (staticInst->isSpecialVector()) {
        arrays.numSrcs = 28;
    } else {
        arrays.numSrcs = staticInst->numSrcRegs();
    }
    arrays.numDests = staticInst->numDestRegs();

    // Create a new DynInst from the instruction fetched.
    DynInstPtr instruction = new (arrays) DynInst(
            arrays, staticInst, curMacroop, this_pc, next_pc, seq, cpu);
    instruction->setTid(tid);

    instruction->setThreadState(cpu->thread[tid]);

    DPRINTF(RxuUC, "[tid:%i] Instruction PC %s created [sn:%#x].\n",
            tid, this_pc, seq);

    DPRINTF(RxuUC, "[tid:%i] Instruction is: %s\n", tid,
            instruction->staticInst->disassemble(this_pc.instAddr()));

#if TRACING_ON
    if (trace) {
        instruction->traceData =
            cpu->getTracer()->getInstRecord(curTick(), cpu->tcBase(tid),
                    instruction->staticInst, this_pc, curMacroop);
    }
#else
    instruction->traceData = NULL;
#endif

    // Add instruction to the CPU's list of instructions.
    // instruction->setInstListIt(cpu->addInst(instruction));

    return instruction;
}

bool
UopCache::lookupAndUpdateNextPC(const DynInstPtr &inst, PCStateBase &next_pc)
{
    // inst->staticInst->advancePC(next_pc);
    if (inst->staticInst->isCompressed()) {
        next_pc.set(inst->pcState().instAddr() + 2);
    } else {
        next_pc.set(inst->pcState().instAddr() + 4);
    }

    inst->setPredTarg(next_pc);
    inst->setPredTaken(false);
    return false;
}

UopCache::Info 
UopCache::process(PCStateBase &this_pc)
{
    if (!lookup(this_pc.instAddr())) {
        DPRINTF(RxuUC, "Uop cache miss in PC %#x.\n",
                    this_pc.instAddr());
        return {0, 0};
    }
    if (insts.empty()) {
        DPRINTF(RxuUC, "Uop cache hit in PC %#x, but no valid instruction. Fault\n",
                    this_pc.instAddr());
        // panic("Uop cache hit in PC %#x, but no valid instruction.\n",
        //             this_pc.instAddr());
        return {0, 0};
    }
    DPRINTF(RxuUC, "Uop cache hit in PC %#x. "
                    "Building instruction.\n",
                    this_pc.instAddr());

    StaticInstPtr curMacroop = nullptr;
    StaticInstPtr staticInst = nullptr;
    Addr pc_inst;
    bool predictedBranch = false;
    unsigned numInsts = 0;
    Addr tpc = 0;

    while (!insts.empty()) {
        staticInst = std::move(insts.front());
        pc_inst = std::move(pc_insts.front());

        if (pc_inst != this_pc.instAddr()) {
            
            DPRINTF(RxuUC, "correct PC is %#x (not %s).\n", pc_inst, this_pc);
            this_pc.advance(pc_inst - this_pc.instAddr());
        }
        std::unique_ptr<PCStateBase> next_pc(this_pc.clone());

        auto &_this_pc = this_pc.as<RiscvISA::PCState>();
        if (staticInst->isCompressed()) {
            _this_pc.npc(_this_pc.instAddr() + 2);
            _this_pc.compressed(true);
        } else {
            _this_pc.npc(_this_pc.instAddr() + 4);
            _this_pc.compressed(false);
        }

        DynInstPtr instruction = buildInst(
                    0, staticInst, curMacroop, this_pc, *next_pc, true);

        DPRINTF(RxuUC, "Processing instruction [sn:%#x] with "
            "PC %s\n", instruction->seqNum, instruction->pcState());

        if (instruction->numSrcRegs() == 0) {
            instruction->setCanIssue();
        }

        set(next_pc, this_pc);
        predictedBranch = this_pc.branching();
        predictedBranch |= lookupAndUpdateNextPC(instruction, *next_pc);

        if (staticInst->isMacroop()) { 
            curMacroop = staticInst;
        }

        cpu->fetch.instsFromUC.push_back(instruction);

        numInsts++;
        tpc = next_pc->instAddr();
        insts.pop_front();
        pc_insts.pop_front();
        stats.decodedInsts++;

        DPRINTF(RxuUC, "Queue size: %i.\n", cpu->fetch.instsFromUC.size());

        set(this_pc, *next_pc);
    }

    return {numInsts, tpc};
}

UopCache::HitInfo 
UopCache::lookupTwo(Addr _pc)
{
    stats.lookup_count++;

    UopCache::HitInfo hit;
    for (int i = 0; i < 2; ++i) {
        UopCache::Info info = lookupOnly(_pc);
        if (info.first > 0) {
            hit.hit |= (1 << i);
            hit.info.push_back(info);
        } else {
            break;
        }
        
        hit.win_size++;
        _pc = (info.first > 0 ? _pc + pcShift(info, _pc) : alignPC(_pc) + 64);
    }


    if (hit.hit & (1 << 0)) {
        stats.cl0_hit_count++;
    }

    if (hit.hit & (1 << 1)) {
        stats.cl1_hit_count++;
    }

    if (hit.hit & (1 << 2)) {
        stats.cl2_hit_count++;
    }

    if (hit.hit & (1 << 3)) {
        stats.cl3_hit_count++;
    }

    return hit;
}

UopCache::HitInfo
UopCache::access(PCStateBase &pc, int num)
{
    UopCache::HitInfo hit;
    // auto resp = pc.instAddr();
    for (int cl = 0; cl < num; ++cl) {
        UopCache::Info info = process(pc);
        hit.win_size++;
        if (info.first > 0) {
            hit.hit |= (1 << cl);
            hit.info.push_back(info);
            // if (cl == 0) {
            //     cpu->fetch.cam->update();
            // }
        } else {
            // Addr addr = pc.instAddr();
            // pc.advance(64 - (addr - alignPC(addr)));
            return hit;
        }
    }
    DPRINTF(RxuUC, "reached max window size 2\n");
    return hit;
}

} // namespace rxuo3
} // namespace gem5
