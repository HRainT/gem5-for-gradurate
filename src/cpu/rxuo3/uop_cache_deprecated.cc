#include "cpu/rxuo3/uop_cache_copy.hh"

#include <algorithm>

#include "arch/generic/pcstate.hh"
#include "base/trace.hh"
#include "cpu/inst_seq.hh"
#include "cpu/rxuo3/dyn_inst.hh"
#include "cpu/rxuo3/limits.hh"
#include "debug/RxuActivity.hh"
#include "debug/RxuUC.hh"
#include "debug/RxuO3PipeView.hh"
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
        _useUopCache(params.system->useUopCache()),
        useHashing(params.useHashing),
        fetchToUopCacheDelay(1),
        predisqToUopCacheDelay(1),
        ucWidth(params.system->ucWidth()),
        predisqWidth(params.predisqWidth),
        stats(_cpu)
{
    if (ucWidth > MaxWidth)
        fatal("uop cache Width (%d) is larger than compiled limit (%d),\n"
             "\tincrease MaxWidth in src/cpu/rxuo3/limits.hh\n",
             ucWidth, static_cast<int>(MaxWidth));
    skidBufferMax = (1 + 1) *  ucWidth;
    for (int tid = 0; tid < MaxThreads; tid++) {
        stalls[tid] = {false};
        uc_pc[tid].reset(params.isa[0]->newPCState());
        ucStatus[tid] = Idle;
        bdelayDoneSeqNum[tid] = 0;
        squashInst[tid] = nullptr;
        squashAfterDelaySlot[tid] = 0;
    }
    cache = new UopCacheEntry[sets * assoc];
    // setBPU(cpu->fetch.getBPU());
    _status = Inactive;
    sentInstSeqNum = 0;
}

void
UopCache::startupStage()
{
    resetStage();
}

void
UopCache::clearStates(ThreadID tid)
{
    ucStatus[tid] = Idle;
    stalls[tid].predisq = false;
}

void
UopCache::resetStage()
{
    _status = Inactive;
    // Setup status, make sure stall signals are clear.
    for (ThreadID tid = 0; tid < numThreads; ++tid) {
        ucStatus[tid] = Idle;
        stalls[tid].predisq = false;
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
                "Stat for total number of uop cache loopup"),
      ADD_STAT(hitRate, statistics::units::Ratio::get(),
                "Stat for uop cache hit rate")          
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
}

void
UopCache::setTimeBuffer(TimeBuffer<TimeStruct> *tb_ptr)
{
    timeBuffer = tb_ptr;

    toFetch = timeBuffer->getWire(0);
    fromPredisq = timeBuffer->getWire(-predisqToUopCacheDelay);
    fromCommit = timeBuffer->getWire(-1);
    fromDecode = timeBuffer->getWire(-1);
}

void
UopCache::setUCQueue(TimeBuffer<DecodeStruct> *dq_ptr)
{
    ucQueue = dq_ptr;

    toPredisq = ucQueue->getWire(0);
}

void
UopCache::setFetchQueue(TimeBuffer<UCStruct> *fq_ptr)
{
    fetchQueue = fq_ptr;

    fromFetch_u = fetchQueue->getWire(-2);
}

void
UopCache::setPCQueue(TimeBuffer<PCStruct> *pq_ptr) 
{
    fromFetch = pq_ptr->getWire(-1);
}

void
UopCache::setActiveThreads(std::list<ThreadID> *at_ptr)
{
    activeThreads = at_ptr;
}

void
UopCache::drainSanityCheck() const
{
    for (ThreadID tid = 0; tid < numThreads; ++tid) {
        assert(insts.empty());
        assert(skidBuffer[tid].empty());
    }
}

bool
UopCache::isDrained() const
{
    for (ThreadID tid = 0; tid < numThreads; ++tid) {
        if (!insts.empty() || !skidBuffer[tid].empty() ||
                (ucStatus[tid] != Running && ucStatus[tid] != Idle))
            return false;
    }
    return true;
}

bool
UopCache::checkStall(ThreadID tid) const
{
    bool ret_val = false;

    if (stalls[tid].predisq) {
        DPRINTF(RxuUC,"[tid:%i] Stall from Predisq stage detected.\n", tid);
        ret_val = true;
    }

    return ret_val;
}

bool
UopCache::fetchInstsValid()
{
    return fromFetch->valid;
}

bool
UopCache::block(ThreadID tid)
{
    DPRINTF(RxuUC, "[tid:%i] Blocking.\n", tid);
    if (ucStatus[tid] != Blocked) {
        // Set the status to Blocked.
        ucStatus[tid] = Blocked;
        if (toFetch->ucUnblock[tid]) {
            toFetch->ucUnblock[tid] = false;
        } else {
            toFetch->ucBlock[tid] = true;
            wroteToTimeBuffer = true;
        }
        return true;
    }
    return false;
}

bool
UopCache::unblock(ThreadID tid)
{
    DPRINTF(RxuUC, "[tid:%i] Done unblocking.\n", tid);
    toFetch->ucUnblock[tid] = true;
    wroteToTimeBuffer = true;
    ucStatus[tid] = Running;
    return true;
}

void
UopCache::squash(const DynInstPtr &inst, ThreadID tid)
{
    DPRINTF(RxuUC, "[tid:%i] [sn:%llu] Squashing due to incorrect branch "
            "prediction detected at uop cache.\n", tid, inst->seqNum);

    // Send back mispredict information.
    toFetch->decodeInfo[tid].branchMispredict = true;   
    toFetch->decodeInfo[tid].predIncorrect = true;
    toFetch->decodeInfo[tid].mispredictInst = inst;
    toFetch->decodeInfo[tid].squash = true;
    toFetch->decodeInfo[tid].doneSeqNum = inst->seqNum;
    set(toFetch->decodeInfo[tid].nextPC, *inst->branchTarget());

    // Looking at inst->pcState().branching()
    // may yield unexpected results if the branch
    // was predicted taken but aliased in the BTB
    // with a branch jumping to the next instruction (mistarget)
    // Using PCState::branching()  will send execution on the
    // fallthrough and this will not be caught at execution (since
    // branch was correctly predicted taken)
    toFetch->decodeInfo[tid].branchTaken = inst->readPredTaken() ||
                                           inst->isUncondCtrl();

    toFetch->decodeInfo[tid].squashInst = inst;

    InstSeqNum squash_seq_num = inst->seqNum;

    // Clear the instruction list and skid buffer in case they have any
    // insts in them.
    while (!insts.empty()) {
        insts.pop();
    }

    while (!skidBuffer[tid].empty()) {
        skidBuffer[tid].pop();
    }

    // Squash instructions up until this one
    cpu->removeInstsUntil(squash_seq_num, tid);
}

unsigned
UopCache::squash(ThreadID tid)
{
    DPRINTF(RxuUC, "[tid:%i] Squashing.\n",tid);

    if (ucStatus[tid] == Blocked ||
        ucStatus[tid] == Unblocking) {
        if (FullSystem) {
            toFetch->ucUnblock[tid] = 1;
        } else {
            // In syscall emulation, we can have both a block and a squash due
            // to a syscall in the same cycle.  This would cause both signals
            // to be high.  This shouldn't happen in full system.
            // @todo: Determine if this still happens.
            if (toFetch->ucBlock[tid])
                toFetch->ucBlock[tid] = 0;
            else
                toFetch->ucUnblock[tid] = 1;
        }
    }

    // Set status to squashing.
    ucStatus[tid] = Squashing;

    // Go through incoming instructions from fetch and squash them.
    unsigned squash_count = 0;

    // Clear the instruction list and skid buffer in case they have any
    // insts in them.
    while (!insts.empty()) {
        insts.pop();
    }

    while (!skidBuffer[tid].empty()) {
        skidBuffer[tid].pop();
    }

    return squash_count;
}

void
UopCache::skidInsert(ThreadID tid)
{
}

bool
UopCache::skidsEmpty()
{
    list<ThreadID>::iterator threads = activeThreads->begin();
    list<ThreadID>::iterator end = activeThreads->end();
    while (threads != end) {
        ThreadID tid = *threads++;
        if (!skidBuffer[tid].empty())
            return false;
    }
    return true;
}

void
UopCache::updateStatus()
{
}

void
UopCache::sortInsts()
{
}

void
UopCache::readStallSignals(ThreadID tid)
{
    if (cpu->predisq.compressFlag) {
        if (int(fromPredisq->PredisqInfo->freeEntries 
                - cpu->predisq.fromUC->size) < ucWidth)
            stalls[tid].predisq = true;
        else stalls[tid].predisq = false;
    } else {
        int uc_to_predisq_inflight_group
            = cpu->predisq.fromUC->size > 0 ? 1 : 0;
        if (int(fromPredisq->PredisqInfo->freeGroups 
            - uc_to_predisq_inflight_group) < ucWidth)
            stalls[tid].predisq = true;
        else stalls[tid].predisq = false;
    }
}

bool
UopCache::checkSignalsAndUpdate(ThreadID tid)
{
    // if (_status == Inactive && fromFetch->valid) {
    //     set(uc_pc[tid], fromFetch->pc);
    // }

    if (!fromFetch_u->insts.empty()) {
        update(fromFetch_u->ucBlockPC, fromFetch_u->insts);
    }

    // if (fromFetch->squash) {
    //     set(uc_pc[tid], fromFetch->spc);
    //     return true;
    // }

    if (fromCommit->commitInfo[tid].squash) {
        DPRINTF(RxuUC, "Squashing instructions due to squash from commit.\n");
        int size = instQueue.size();
        while (size > 0) {
            DynInstPtr inst = instQueue.front();
            if (inst->isSquashed()) {
                DPRINTF(RxuUC,
                        "[tid:%i] "
                        "instruction %i with PC %s has already been squashed before.\n",
                        tid, inst->seqNum, inst->pcState());
            } else {
                if (inst->seqNum > fromCommit->commitInfo[tid].doneSeqNum) {
                    inst->setSquashed();
                    DPRINTF(RxuUC, "Squashing instruction, [sn:%llu] PC (%s).\n", inst->seqNum, inst->pcState());
                    ++stats.squashedInsts;
                } else {
                    instQueue.push_back(inst);
                }
            }
            instQueue.pop_front();
            size--;
        }
        ucStatus[tid] = Squashing;
        return true;
    }

    if (fromDecode->decodeInfo[tid].squash) {
        DPRINTF(RxuUC, "Squashing instructions due to squash from decode.\n");
        int size = instQueue.size();
        while (size--) {
            DynInstPtr inst = instQueue.front();
            if (inst->isSquashed()) {
                DPRINTF(RxuUC,
                        "[tid:%i] "
                        "instruction %i with PC %s has already been squashed before.\n",
                        tid, inst->seqNum, inst->pcState());
            } else {
                if (inst->seqNum > fromDecode->decodeInfo[tid].doneSeqNum) {
                    inst->setSquashed();
                    DPRINTF(RxuUC, "Squashing instruction, [sn:%llu] PC (%s).\n", inst->seqNum, inst->pcState());
                    ++stats.squashedInsts;
                } else {
                    instQueue.push_back(inst);
                }
            }
            instQueue.pop_front();
        }
        ucStatus[tid] = Squashing;
        return true;
    }

    // Update the per thread stall statuses.
    readStallSignals(tid);

    if (checkStall(tid)) {
        return block(tid);
    }

    if (ucStatus[tid] == Blocked) {
        DPRINTF(RxuUC, "[tid:%i] Done blocking, switching to unblocking.\n",
                tid);
        ucStatus[tid] = Unblocking;
        unblock(tid);
        return true;
    }
    if (ucStatus[tid] == Squashing) {
        // Switch status to running if uc isn't being told to block or
        // squash this cycle.
        DPRINTF(RxuUC, "[tid:%i] Done squashing, switching to running.\n",
                tid);
        ucStatus[tid] = Running;
        return false;
    }
    return false;
}

int
UopCache::lindex(Addr pc_in, unsigned instShiftAmt) 
{
    Addr pc = pc_in >> instShiftAmt;
    // Addr setMask = (1ULL << setBits) - 1;
    if (useHashing) {
        pc ^= pc_in;
    }
    return ((pc & setMask) << assocBits);
}

int 
UopCache::finallindex(int lindex, int lowPcBits, int way) 
{
    return (useHashing ? (lindex ^ ((lowPcBits >> way) << assocBits)) :
                         (lindex))
           + way;
}

void
UopCache::get(Addr pc, ThreadID tid) 
{
    unsigned indexB;
    unsigned tag;
    if (useHashing) {
        unsigned pcShift = logSize - assocBits;
        indexB = (pc >> pcShift) & setMask;
        tag = (pc >> pcShift) ^ (pc >> (pcShift + tagBits));
        tag &= tagMask;
    } else {
        unsigned pcShift = shift + logSize - assocBits;
        tag = (pc >> pcShift) & tagMask;
    }
    int index = lindex(pc, shift);
    for (int i = 0; i < assoc; i++) {
        int idx = finallindex(index, indexB, i);
        if (cache[idx].valid && cache[idx].tag == tag && cache[idx].age > 0) {
            cache[idx].age++;
            list<StaticInstPtr>::iterator inst;
            for (inst = cache[idx].insts.begin(); 
                 inst != cache[idx].insts.end(); 
                 inst++) {
                insts.push(*inst);
            }
        }
    }
}

bool 
UopCache::lookup(Addr pc)
{
    stats.lookupCount++;

    unsigned indexB;
    unsigned tag;
    if (useHashing) {
        unsigned pcShift = logSize - assocBits;
        indexB = (pc >> pcShift) & setMask;
        tag = (pc >> pcShift) ^ (pc >> (pcShift + tagBits));
        tag &= tagMask;
    } else {
        unsigned pcShift = shift + logSize - assocBits;
        tag = (pc >> pcShift) & tagMask;
    }
    int index = lindex(pc, shift);
    for (int i = 0; i < assoc; i++) {
        int idx = finallindex(index, indexB, i);
        if (cache[idx].valid && cache[idx].tag == tag && cache[idx].age > 0) {
            stats.hitCycles++;
            return true;
        }
    }
    stats.missCycles++;
    return false;
}

bool 
UopCache::update(Addr pc, std::list<StaticInstPtr> insts)
{
    int index = lindex(pc, shift);
    unsigned indexB;
    unsigned tag;
    if (useHashing) {
        unsigned pcShift = logSize - assocBits;
        indexB = (pc >> pcShift) & setMask;
        tag = (pc >> pcShift) ^ (pc >> (pcShift + tagBits));
        tag &= tagMask;
    } else {
        unsigned pcShift = shift + logSize - assocBits;
        tag = (pc >> pcShift) & tagMask;
    }
    for (int i = 0; i < assoc; i++) {
        int idx = finallindex(index, indexB, i);
        if (!cache[idx].valid || 
                cache[idx].age <= 0) {
            cache[idx].clear();
            cache[idx].tag = tag;
            cache[idx].valid = true;
            std::list<StaticInstPtr>::iterator inst;
            for (inst = insts.begin(); inst != insts.end(); inst++) {
                cache[idx].insts.push_back(*inst);
                cache[idx].numInsts++;
            }
            cache[idx].age = initAge;

            DPRINTF(RxuUC, "[%i] insts from decode saved into ucache line [%#x] with Index: %i, Tag: %#x\n"
                    , insts.size(), pc, idx, tag);

            return true;
        } else if (cache[idx].valid &&
                    cache[idx].age > 0 &&
                    cache[idx].tag == tag) {
            cache[idx].age++;
            return true;
        } else {
            cache[idx].age--;
            if (cache[idx].age <= 0) {
                cache[idx].clear();
            }
        }
    }
    DPRINTF(RxuUC, "[%i] insts from decode failed to save into ucache line [%#x]. UCache line is full.\n"
                    , insts.size(), pc);
    ++stats.cacheFull;

    return false;
}

DynInstPtr
UopCache::buildInst(ThreadID tid, StaticInstPtr staticInst,
        StaticInstPtr curMacroop, const PCStateBase &this_pc,
        const PCStateBase &next_pc, bool trace)
{
    // Get a sequence number.
    InstSeqNum seq = cpu->getAndIncrementInstSeq();

    DynInst::Arrays arrays;
    arrays.numSrcs = staticInst->numSrcRegs();
    arrays.numDests = staticInst->numDestRegs();

    // Create a new DynInst from the instruction fetched.
    DynInstPtr instruction = new (arrays) DynInst(
            arrays, staticInst, curMacroop, this_pc, next_pc, seq, cpu);
    instruction->setTid(tid);

    instruction->setThreadState(cpu->thread[tid]);

    DPRINTF(RxuUC, "[tid:%i] Instruction PC %s created [sn:%lli].\n",
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
    instruction->setInstListIt(cpu->addInst(instruction));

    return instruction;
}

bool
UopCache::lookupAndUpdateNextPC(const DynInstPtr &inst, PCStateBase &next_pc)
{
    // Do branch prediction check here.
    // A bit of a misnomer...next_PC is actually the current PC until
    // this function updates it.
    bool predict_taken;

    if (!inst->isControl()) {
        inst->staticInst->advancePC(next_pc);
        inst->setPredTarg(next_pc);
        inst->setPredTaken(false);
        return false;
    }

    ThreadID tid = inst->threadNumber;
    predict_taken = cpu->fetch.branchPred->predict(inst->staticInst, inst->seqNum,
                                        next_pc, tid);
 
    if (predict_taken) {
        DPRINTF(RxuUC, "[tid:%i] [sn:%llu] Branch at PC %#x "
                "predicted to be taken to %s\n",
                tid, inst->seqNum, inst->pcState().instAddr(), next_pc);
    } else {
        DPRINTF(RxuUC, "[tid:%i] [sn:%llu] Branch at PC %#x "
                "predicted to be not taken\n",
                tid, inst->seqNum, inst->pcState().instAddr());
    }

    DPRINTF(RxuUC, "[tid:%i] [sn:%llu] Branch at PC %#x "
            "predicted to go to %s\n",
            tid, inst->seqNum, inst->pcState().instAddr(), next_pc);
    inst->setPredTarg(next_pc);
    inst->setPredTaken(predict_taken);

    cpu->fetchStats[tid]->numBranches++;

    if (predict_taken) {
        // ++fetchStats.predictedBranches;
    }

    return predict_taken;
}

void
UopCache::tick()
{
    wroteToTimeBuffer = false;

    bool status_change = false;

    toPredisqIndex = 0;

    list<ThreadID>::iterator threads = activeThreads->begin();
    list<ThreadID>::iterator end = activeThreads->end();

    //Check stall and squash signals.
    while (threads != end) {
        ThreadID tid = *threads++;

        DPRINTF(RxuUC,"Processing [tid:%i]\n",tid);
        status_change =  checkSignalsAndUpdate(tid) || status_change;

        uc(status_change, tid);
    }

    if (wroteToTimeBuffer) {
        DPRINTF(RxuActivity, "Activity this cycle.\n");
        cpu->activityThisCycle();
    }
}

void
UopCache::uc(bool &status_change, ThreadID tid)
{
    if (ucStatus[tid] == Blocked) {
        ++stats.blockedCycles;
    } else if (ucStatus[tid] == Squashing) {
        ++stats.squashCycles;
    }

    // UopCache should try to cache as many instructions as its bandwidth
    // will allow, as long as it is not currently blocked.
    if (ucStatus[tid] == Running ||
        ucStatus[tid] == Idle) {
        DPRINTF(RxuUC, "[tid:%i] Not blocked, so attempting to run stage.\n",tid);
        process(tid);
    } else if (ucStatus[tid] == Unblocking) {
        status_change = unblock(tid) || status_change;
        process(tid);
    }
}

bool 
UopCache::fetchCanSend(InstSeqNum sn) { 
    if (instQueue.empty()) return true;
    return (sn > sentInstSeqNum) && (sn < instQueue.front()->seqNum);
}

bool
UopCache::checkCanSend(InstSeqNum sn)
{
    InstSeqNum psn = -1;
    std::list<DynInstPtr>::iterator iter = cpu->instList.end();
    while (--iter != cpu->instList.begin()) {
        if ((*iter)->isSquashed()) {
            continue;
        }
        if ((*iter)->seqNum < sn) {
            psn = (*iter)->seqNum;
            break;
        }
    }
    if (iter == cpu->instList.begin() && !(*iter)->isSquashed()) {
        if ((*iter)->seqNum < sn) psn = (*iter)->seqNum;
    }
    bool can = (psn <= cpu->predisq.msn) || (psn == -1) || (psn <= sentInstSeqNum);
    if (!can)
        DPRINTF(RxuUC, "[sn:%llu] not sent to predisq, "
                "[sn:%llu] - [sn:%llu] should wait.\n", 
                psn, instQueue.front()->seqNum, instQueue.back()->seqNum);
    // else
    //     DPRINTF(RxuUC, "[sn:%llu] already in predisq, "
    //                     "[sn:%llu] can send to predisq.\n", 
    //                     psn, sn);
    return can;
}

void 
UopCache::process(ThreadID tid)
{
    if (stalls[tid].predisq) {
        return;
    }
    if (instQueue.empty())  {
        return;
    }
    int numInst = 0;
    toPredisq->size = 0;
    while (!instQueue.empty() && numInst < ucWidth && !stalls[tid].predisq) {
        auto &inst = instQueue.front();
        if (inst->isSquashed()) continue;
        if (!checkCanSend(inst->seqNum)) {
            break;
        }
        sentInstSeqNum = inst->seqNum;
        DPRINTF(RxuUC, "[tid:%i] [sn:%llu] Sending instruction to predisq "
                "from uc. Uc queue size: %i.\n",
                0, inst->seqNum, instQueue.size()); 
        instQueue.pop_front();
        toPredisq->insts[toPredisqIndex++] = inst;
        toPredisq->size++;
        numInst++;
        wroteToTimeBuffer = true;
    }

    if (toPredisqIndex) {
        toPredisq->active = true;
    }
}

void 
UopCache::process(PCStateBase &this_pc)
{
    if (ucStatus[0] == Blocked) {
        DPRINTF(RxuUC, "predisq is full. Stopping looking up uop cache.\n");
        return;
    }
    // stats.hitCycles++;

    DPRINTF(RxuUC, "Uop cache hit in PC %#x. "
                    "Building instruction for predisq.\n",
                    this_pc.instAddr());

    get(this_pc.instAddr(), 0);

    StaticInstPtr curMacroop = nullptr;
    StaticInstPtr staticInst = nullptr;
    bool predictedBranch = false;

    while (!insts.empty()) {
        staticInst = std::move(insts.front());
        std::unique_ptr<PCStateBase> next_pc(this_pc.clone());

        DynInstPtr instruction = buildInst(
                    0, staticInst, curMacroop, this_pc, *next_pc, true);

        DPRINTF(RxuUC, "Processing instruction [sn:%lli] with "
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

        set(this_pc, *next_pc);

        instQueue.push_back(instruction);
        insts.pop();
        stats.decodedInsts++;

        if (predictedBranch) {
            break;
        }

        // Ensure that if it was predicted as a branch, it really is a
        // branch.
        if (instruction->readPredTaken() && !instruction->isControl()) {
            panic("Instruction predicted as a branch!");

            ++stats.controlMispred;

            // Might want to set some sort of boolean and just do
            // a check at the end
            squash(instruction, instruction->threadNumber);

            break;
        }

        // Go ahead and compute any PC-relative branches.
        // This includes direct unconditional control and
        // direct conditional control that is predicted taken.
        if (instruction->isDirectCtrl() &&
        (instruction->isUncondCtrl() || instruction->readPredTaken()))
        {
            ++stats.branchResolved;

            std::unique_ptr<PCStateBase> target = instruction->branchTarget();
            if (*target != instruction->readPredTarg()) {
                ++stats.branchMispred;

                // Might want to set some sort of boolean and just do
                // a check at the end
                squash(instruction, instruction->threadNumber);

                DPRINTF(RxuUC,
                        "[tid:%i] [sn:%llu] "
                        "Updating predictions: Wrong predicted target: %s \
                        PredPC: %s\n",
                        0, instruction->seqNum, instruction->readPredTarg(), *target);
                //The micro pc after an instruction level branch should be 0
                instruction->setPredTarg(*target);
                set(this_pc, *target);
                break;
            }
        }
    }

    if (!lookup(this_pc.instAddr())) {
        if (_status == Active) _status = Inactive;
        toFetch->uopCacheActive = false;

        stats.bpuMissUopCacheCount++;
        DPRINTF(RxuUC, "Uop cache miss in PC %#x, but BPU predict it.\n", this_pc.instAddr());
        DPRINTF(RxuUC, "Deactivating uop cache.\n");
    } else {
        _status = Active;
        toFetch->uopCacheActive = true;

        stats.runCycles++;
        // stats.hitCycles++;
        DPRINTF(RxuUC, "Uop cache hit in PC %#x, and BPU predicted it.\n", this_pc.instAddr());
        DPRINTF(RxuUC, "Activating uop cache.\n");
    }

    clearInsts();
}

} // namespace rxuo3
} // namespace gem5
