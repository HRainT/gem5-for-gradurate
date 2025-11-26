#include "cpu/rxuo3/bpu0.hh"

#include "arch/generic/pcstate.hh"
#include "base/trace.hh"
#include "cpu/inst_seq.hh"
#include "cpu/rxuo3/dyn_inst.hh"
#include "cpu/rxuo3/limits.hh"
#include "debug/RxuActivity.hh"
#include "debug/RxuBpu0.hh"
#include "debug/RxuBPU.hh"
#include "debug/RxuO3PipeView.hh"
#include "params/BaseRxuO3CPU.hh"
#include "sim/full_system.hh"
#include "arch/riscv/pcstate.hh"

// clang complains about std::set being overloaded with Packet::set if
// we open up the entire namespace std
using std::list;

namespace gem5
{

namespace rxuo3
{

Bpu0::Bpu0(CPU *_cpu, const BaseRxuO3CPUParams &params)
    : cpu(_cpu),
      bpu1ToBpu0Delay(params.bpu1ToBpu0Delay),
      decodeToBpu0Delay(params.decodeToBpu0Delay),
      commitToBpu0Delay(params.commitToBpu0Delay),
      fetchToBpu0Delay(params.fetchToBpu0Delay),
      bpu0Width(params.bpu0Width),
      numThreads(params.numThreads),
      stats(_cpu)
{
    if (bpu0Width > MaxWidth)
        fatal("bpu0Width (%d) is larger than compiled limit (%d),\n"
             "\tincrease MaxWidth in src/cpu/rxuo3/limits.hh\n",
             bpu0Width, static_cast<int>(MaxWidth));

    for (int tid = 0; tid < MaxThreads; tid++) {
        stalls[tid] = {false, false};
        bpu0Status[tid] = Idle;
        pc[tid].reset(params.isa[0]->newPCState());
        lookupUopCacheFlag = true;
        lookupCamFlag = true;
    }

    l0btb.clear();
}

void
Bpu0::startupStage()
{
    resetStage();
}

void
Bpu0::clearStates(ThreadID tid)
{
    bpu0Status[tid] = Idle;
    stalls[tid] = {false, false};
    set(pc[tid], cpu->pcState(tid));
    lookupUopCacheFlag = true;
    lookupCamFlag = true;
    l0btb.clear();
}

void
Bpu0::resetStage()
{
    _status = Inactive;

    // Setup status, make sure stall signals are clear.
    for (ThreadID tid = 0; tid < numThreads; ++tid) {
        bpu0Status[tid] = Idle;

        stalls[tid] = {false, false};

        set(pc[tid], cpu->pcState(tid));
        lookupUopCacheFlag = true;
        lookupCamFlag = true;
        l0btb.clear();
    }
}

std::string
Bpu0::name() const
{
    return cpu->name() + ".bpu0";
}

Bpu0::Bpu0Stats::Bpu0Stats(CPU *cpu)
    : statistics::Group(cpu, "bpu0"),
      ADD_STAT(idleCycles, statistics::units::Cycle::get(),
               "Number of cycles bpu0 is idle"),
      ADD_STAT(blockedCycles, statistics::units::Cycle::get(),
               "Number of cycles bpu0 is blocked"),
      ADD_STAT(runCycles, statistics::units::Cycle::get(),
               "Number of cycles bpu0 is running"),
      ADD_STAT(unblockCycles, statistics::units::Cycle::get(),
               "Number of cycles bpu0 is unblocking"),
      ADD_STAT(squashCycles, statistics::units::Cycle::get(),
               "Number of cycles bpu0 is squashing"),
      ADD_STAT(bpu0edInsts, statistics::units::Count::get(),
               "Number of instructions handled by bpu0"),
      ADD_STAT(squashedInsts, statistics::units::Count::get(),
               "Number of squashed instructions handled by bpu0"),
      ADD_STAT(L0BTB_conflict, statistics::units::Count::get(),
               "Times of L0BTB conflict"),
      ADD_STAT(L0BTB_branchInsts, statistics::units::Count::get(),
               "Number of branch instructions handled by L0BTB"),
      ADD_STAT(L0BTB_missInsts, statistics::units::Count::get(),
               "Number of miss branch instructions handled by L0BTB"),
      ADD_STAT(L0BTB_missRate, statistics::units::Rate<
                    statistics::units::Count, statistics::units::Count>::get(),
               "L0BTB_missRate: L0BTB_missInsts / L0BTB_branchInsts"),
      ADD_STAT(L0BTB_hit_nohaveInsts, statistics::units::Count::get(),
               "Number of hit but no have insts branch instructions handled by L0BTB"),
      ADD_STAT(cachelineOutInsts, "Distribution of number of instructions sent by bpu0"),
      ADD_STAT(sendCachelineCycles, statistics::units::Cycle::get(),
               "Number of cycles L0BTB can send a cacheline"),
      ADD_STAT(noCachelineCycles, statistics::units::Cycle::get(),
               "Number of cycles L0BTB can't send a cacheline due to bubble"),
      ADD_STAT(stallCycles, statistics::units::Cycle::get(),
               "Number of cycles L0BTB can't send a cacheline due to stall/squash"),
      ADD_STAT(sendCachelineRate, statistics::units::Rate<
                    statistics::units::Count, statistics::units::Count>::get(),
               "Rate of L0BTB can send a cacheline"),
      ADD_STAT(noCachelineRate, statistics::units::Rate<
                    statistics::units::Count, statistics::units::Count>::get(),
               "Rate of L0BTB can't send a cacheline due to bubble"),
      ADD_STAT(stallRate, statistics::units::Rate<
                    statistics::units::Count, statistics::units::Count>::get(),
               "Rate of L0BTB can't send a cacheline due to stall/squash"),
      ADD_STAT(bubbleCount, "Distribution of number of bubble detected by bpu0"),
      ADD_STAT(bubbleInstsCount, "Distribution of number of bubble insts detected by bpu0"),

      ADD_STAT(Return_cam_missInsts, statistics::units::Count::get(),
               "Number of missing return instructions handled by Return_cam"),
      ADD_STAT(correctByBpu, statistics::units::Count::get(),
               "Number of branch instructions corrected by Bpu"),
      ADD_STAT(L0BTB_Used, statistics::units::Count::get(),
               "Number of entries of L0BTB"),
      ADD_STAT(L0BTB_Util_Rate, statistics::units::Ratio::get(), "L2BTB Util Ratio",
               L0BTB_Used / 128),

      ADD_STAT(L0BTB_CAM_Hit_Count,statistics::units::Count::get(),"L0BTB/CAM Hit Count"),
      ADD_STAT(L0BTB_CAM_Miss_Count,statistics::units::Count::get(),"L0BTB/CAM Miss Count"),

      ADD_STAT(bubbleInsts, statistics::units::Count::get(),"Number of bubble inst")

{
    idleCycles.prereq(idleCycles);
    blockedCycles.prereq(blockedCycles);
    runCycles.prereq(runCycles);
    unblockCycles.prereq(unblockCycles);
    squashCycles.prereq(squashCycles);
    bpu0edInsts.prereq(bpu0edInsts);
    squashedInsts.prereq(squashedInsts);

    L0BTB_conflict.prereq(L0BTB_conflict);
    L0BTB_branchInsts.prereq(L0BTB_branchInsts);
    L0BTB_missInsts.prereq(L0BTB_missInsts);
    L0BTB_missRate
        .precision(6);
    L0BTB_missRate = L0BTB_missInsts / L0BTB_branchInsts;

    L0BTB_hit_nohaveInsts.prereq(L0BTB_hit_nohaveInsts);

    cachelineOutInsts
        .init(1, 16, 1)
        .flags(statistics::nozero);

    sendCachelineCycles.prereq(sendCachelineCycles);
    noCachelineCycles.prereq(noCachelineCycles);
    stallCycles.prereq(stallCycles);

    sendCachelineRate
        .precision(6);
    sendCachelineRate = sendCachelineCycles / (sendCachelineCycles + noCachelineCycles + stallCycles);

    noCachelineRate
        .precision(6);
    noCachelineRate = noCachelineCycles / (sendCachelineCycles + noCachelineCycles + stallCycles);

    stallRate
        .precision(6);
    stallRate = stallCycles / (sendCachelineCycles + noCachelineCycles + stallCycles);

    bubbleCount
        .init(0, 1000, 1)
        .flags(statistics::nozero);

    bubbleInstsCount
        .init(0, 1000, 1)
        .flags(statistics::nozero);

    Return_cam_missInsts.prereq(Return_cam_missInsts);

    correctByBpu.prereq(correctByBpu);

    L0BTB_Used.prereq(L0BTB_Used);
    L0BTB_Util_Rate.precision(6);

    L0BTB_CAM_Hit_Count.prereq(L0BTB_CAM_Hit_Count);
    L0BTB_CAM_Miss_Count.prereq(L0BTB_CAM_Miss_Count);

    bubbleInsts.prereq(bubbleInsts);
}

void
Bpu0::setTimeBuffer(TimeBuffer<TimeStruct> *tb_ptr)
{
    timeBuffer = tb_ptr;

    // Setup wire to write information back to Bpu0.
    toFetch = timeBuffer->getWire(0);

    // Create wires to get information from proper places in time buffer.
    fromBpu1 = timeBuffer->getWire(-bpu1ToBpu0Delay);
    fromDecode = timeBuffer->getWire(-decodeToBpu0Delay);
    fromCommit = timeBuffer->getWire(-commitToBpu0Delay);
}

void
Bpu0::setBpu0Queue(TimeBuffer<Bpu0Struct> *b0q_ptr)
{
    bpu0Queue = b0q_ptr;

    // Setup wire to write information to proper place in bpu0 queue.
    toBpu1 = bpu0Queue->getWire(0);
}

void
Bpu0::setFetchQueue(TimeBuffer<FetchStruct> *fq_ptr)
{
    fetchQueue = fq_ptr;

    // Setup wire to read information from fetch queue.
    fromFetch = fetchQueue->getWire(-fetchToBpu0Delay);
}

void
Bpu0::setActiveThreads(std::list<ThreadID> *at_ptr)
{
    activeThreads = at_ptr;
}

void
Bpu0::drainSanityCheck() const
{
    for (ThreadID tid = 0; tid < numThreads; ++tid) {
        assert(insts[tid].empty());
    }
}

bool
Bpu0::isDrained() const
{
    for (ThreadID tid = 0; tid < numThreads; ++tid) {
        if (!insts[tid].empty() ||
                (bpu0Status[tid] != Running && bpu0Status[tid] != Idle))
            return false;
    }
    return true;
}

bool
Bpu0::checkStall(ThreadID tid) const
{
    bool ret_val = false;

    if (stalls[tid].IBandLB) {
        DPRINTF(RxuBpu0,"[tid:%i] Stall from IBandLB stage detected.\n", tid);
        ret_val = true;
    } 
    // else if (stalls[tid].bpu1) {
    //     DPRINTF(RxuBpu0,"[tid:%i] Stall from Bpu1 stage detected.\n", tid);
    //     ret_val = true;
    // }

    return ret_val;
}

bool
Bpu0::fetchInstsValid()
{
    return fromFetch->size > 0;
}

bool
Bpu0::block(ThreadID tid)
{
    DPRINTF(RxuBpu0, "[tid:%i] Blocking.\n", tid);

    // If the bpu0 status is blocked or unblocking then bpu0 has not yet
    // signalled fetch to unblock. In that case, there is no need to tell
    // fetch to block.
    if (bpu0Status[tid] != Blocked) {
        // Set the status to Blocked.
        bpu0Status[tid] = Blocked;

        if (toFetch->bpu0Block[tid]) {
            toFetch->bpu0Unblock[tid] = false;
        } else {
            toFetch->bpu0Block[tid] = true;
            wroteToTimeBuffer = true;
        }

        return true;
    }

    return false;
}

bool
Bpu0::unblock(ThreadID tid)
{
    DPRINTF(RxuBpu0, "[tid:%i] Trying to unblock.\n", tid);

    if (insts[tid].empty()) {

        DPRINTF(RxuBpu0, "[tid:%i] Done unblocking.\n", tid);

        toFetch->bpu0Unblock[tid] = true;
        wroteToTimeBuffer = true;

        bpu0Status[tid] = Running;
        return true;
    }

    return false;
}

void
Bpu0::squash(ThreadID tid, InstSeqNum squashedSeqNum)
{
    DPRINTF(RxuBpu0, "[tid:%i] [squash sn:%llu] Squashing instructions.\n",
        tid, squashedSeqNum);

    // Set status to squashing.
    bpu0Status[tid] = Squashing;

    if (bpu0Status[tid] == Blocked ||
        bpu0Status[tid] == Unblocking) {
        if (FullSystem) {
            toFetch->bpu0Unblock[tid] = 1;
        } else {
            if (toFetch->bpu0Block[tid])
                toFetch->bpu0Block[tid] = 0;
            else
                toFetch->bpu0Unblock[tid] = 1;
        }
    }

    for (int i = 0; i < fromFetch->size; i++) {
        if (!fromFetch->insts[i]->isSquashed()) {
            fromFetch->insts[i]->setSquashed();
            DPRINTF(RxuBpu0,
                    "[tid:%i] "
                    "instruction %i with PC %s is squashed.\n",
                    tid, fromFetch->insts[i]->seqNum,
                    fromFetch->insts[i]->pcState());
        } else {
            DPRINTF(RxuBpu0,
                    "[tid:%i] "
                    "instruction %i with PC %s has already been squashed before.\n",
                    tid, fromFetch->insts[i]->seqNum,
                    fromFetch->insts[i]->pcState());
        }
    }

    insts[tid].clear();

    waitSendInsts[tid].clear();
}

void
Bpu0::updateStatus()
{
    bool any_unblocking = false;

    list<ThreadID>::iterator threads = activeThreads->begin();
    list<ThreadID>::iterator end = activeThreads->end();

    while (threads != end) {
        ThreadID tid = *threads++;

        if (bpu0Status[tid] == Unblocking) {
            any_unblocking = true;
            break;
        }
    }

    // Bpu0 will have activity if it's unblocking.
    if (any_unblocking) {
        if (_status == Inactive) {
            _status = Active;

            DPRINTF(RxuActivity, "Activating stage.\n");

            cpu->activateStage(CPU::Bpu0Idx);
        }
    } else {
        // If it's not unblocking, then bpu0 will not have any internal
        // activity.  Switch it to inactive.
        if (_status == Active) {
            _status = Inactive;
            DPRINTF(RxuActivity, "Deactivating stage.\n");

            cpu->deactivateStage(CPU::Bpu0Idx);
        }
    }
}

void
Bpu0::sortInsts()
{
    int insts_from_fetch = fromFetch->size;
    fetchCacheline fcl;
    fcl.first_pc = fromFetch->insts[0]->pc->instAddr();
    StaticInstPtr instructions[32];
    PCStateBase *insts_PC[32] = {nullptr};
    DPRINTF(RxuBpu0, "A cacheline from fetch, first_PC: (%#x)------------------ \n",
            fcl.first_pc);
    for (int i = 0; i < insts_from_fetch; ++i) {
        fcl.size++;
        fcl.insts[i] = fromFetch->insts[i];
        instructions[i] = fcl.insts[i]->staticInst;
        set(insts_PC[i], fcl.insts[i]->pc);
        DPRINTF(RxuBpu0, "Instruction is: %s, with PC (%s).\n",
                fcl.insts[i]->staticInst->disassemble(fcl.insts[i]->pcState().instAddr()),
                fcl.insts[i]->pcState());
    }
    DPRINTF(RxuBpu0, "----------------------------- end ------------------------ \n");

    for (int j = 0; j < fromFetch->static_size; j++) {
        fcl.static_size++;
        fcl.static_insts[j] = fromFetch->static_insts[j];
        fcl.pc_insts[j] = fromFetch->pc_insts[j];
    }
    fcl.pc = fromFetch->pc;

    insts[fromFetch->insts[0]->threadNumber].push_back(fcl);

    if (mapper_l0btb.hasMapping()) {
        auto pcs = mapper_l0btb.getPcsForTpcAndRemoveTpc(fcl.first_pc);
        for (const auto& pc : pcs) {
            l0btb.saveInstructions(pc, instructions, insts_PC, insts_from_fetch);
            DPRINTF(RxuBpu0, "br_PC: [%#x], TPC: [%#x] instructions saved into L0BTB\n", pc, fcl.first_pc);
        }
    }

    bool saved = return_Cam.saveInstructions(fcl.first_pc, instructions, insts_PC, insts_from_fetch);
    if (saved)
        DPRINTF(RxuBpu0, "TPC: [%#x] instructions saved into return_Cam\n", fcl.first_pc);

    for (int i = 0; i < insts_from_fetch; ++i) {
        if (insts_PC[i]) {
            delete insts_PC[i];
            insts_PC[i] = nullptr;
        }
    }
}

void
Bpu0::readStallSignals(ThreadID tid)
{
    if (fromBpu1->bpu1Block[tid]) {
        stalls[tid].bpu1 = true;
    }

    if (fromBpu1->bpu1Unblock[tid]) {
        // assert(stalls[tid].bpu1);
        stalls[tid].bpu1 = false;
    }

    if (fromBpu1->IBandLBBlock[tid]) {
        stalls[tid].IBandLB = true;
    }

    if (fromBpu1->IBandLBUnblock[tid]) {
        // assert(stalls[tid].IBandLB);
        stalls[tid].IBandLB = false;
    }
}

bool
Bpu0::checkSignalsAndUpdate(ThreadID tid)
{
    readStallSignals(tid);

    // Check squash signals from commit.
    if (fromCommit->commitInfo[tid].squash) {
        waitBpuSquash = false;

        DPRINTF(RxuBpu0, "[tid:%i] Squashing instructions until [sn:%llu] due to squash "
                "from commit.\n", tid, fromCommit->commitInfo[tid].doneSeqNum);

        squash(tid, fromCommit->commitInfo[tid].doneSeqNum);

        set(pc[tid], *fromCommit->commitInfo[tid].pc);
        lookupUopCacheFlag = true;
        lookupCamFlag = true;

        if (fromCommit->commitInfo[tid].mispredictInst &&
            fromCommit->commitInfo[tid].mispredictInst->isControl()) {
            auto result = l0btb.lookup(fromCommit->commitInfo[tid].mispredictInst->pcState().instAddr());
            if (result.hit && result.entry->TPC != (*fromCommit->commitInfo[tid].pc).instAddr()) {
                if (fromCommit->commitInfo[tid].branchTaken) {
                    std::unique_ptr<PCStateBase> tpc((*fromCommit->commitInfo[tid].pc).clone());
                    l0btb.updateTPC(fromCommit->commitInfo[tid].mispredictInst->pcState().instAddr(), 
                        (*fromCommit->commitInfo[tid].pc).instAddr(), std::move(tpc));

                    DPRINTF(RxuBpu0, "br_PC: [%#x], TPC: [%#x] -> new_TPC: [%#x] due to squash from commit.\n"
                            , fromCommit->commitInfo[tid].mispredictInst->pcState().instAddr(), result.entry->TPC,
                            (*fromCommit->commitInfo[tid].pc).instAddr());

                    mapper_l0btb.addMapping((*fromCommit->commitInfo[tid].pc).instAddr(), fromCommit->commitInfo[tid].mispredictInst->pcState().instAddr());
                } else {
                    l0btb.invalidateEntry(fromCommit->commitInfo[tid].mispredictInst->pcState().instAddr());

                    DPRINTF(RxuBpu0, "br_PC: [%#x], TPC: [%#x] invalidated from L0BTB due to not taken.\n"
                            , fromCommit->commitInfo[tid].mispredictInst->pcState().instAddr(), result.entry->TPC);

                    mapper_l0btb.removeMapping(result.entry->TPC, fromCommit->commitInfo[tid].mispredictInst->pcState().instAddr());
                }
            }
        }

        return true;
    }

    if (fromDecode->decodeInfo[tid].squash) {
        waitBpuSquash = false;
        DPRINTF(RxuBpu0, "[tid:%i] Squashing instructions until [sn:%llu] due to squash "
                "from decode.\n",tid, fromDecode->decodeInfo[tid].doneSeqNum);

        if (fromDecode->decodeInfo[tid].mispredictInst &&
            fromDecode->decodeInfo[tid].mispredictInst->isControl()) {
            auto result = l0btb.lookup(fromDecode->decodeInfo[tid].mispredictInst->pcState().instAddr());
            if (result.hit && result.entry->TPC != (*fromDecode->decodeInfo[tid].nextPC).instAddr()) {
                if (fromDecode->decodeInfo[tid].branchTaken) {
                    std::unique_ptr<PCStateBase> tpc((*fromDecode->decodeInfo[tid].nextPC).clone());
                    l0btb.updateTPC(fromDecode->decodeInfo[tid].mispredictInst->pcState().instAddr(),
                    (*fromDecode->decodeInfo[tid].nextPC).instAddr(), std::move(tpc));

                    DPRINTF(RxuBpu0, "br_PC: [%#x], TPC: [%#x] -> new_TPC: [%#x] due to squash from decode.\n"
                            , fromDecode->decodeInfo[tid].mispredictInst->pcState().instAddr(), result.entry->TPC,
                            (*fromDecode->decodeInfo[tid].nextPC).instAddr());

                    mapper_l0btb.addMapping((*fromDecode->decodeInfo[tid].nextPC).instAddr(), fromDecode->decodeInfo[tid].mispredictInst->pcState().instAddr());
                } else {
                    l0btb.invalidateEntry(fromDecode->decodeInfo[tid].mispredictInst->pcState().instAddr());

                    DPRINTF(RxuBpu0, "br_PC: [%#x], TPC: [%#x] invalidated from L0BTB due to not taken.\n"
                            , fromDecode->decodeInfo[tid].mispredictInst->pcState().instAddr(), result.entry->TPC);

                    mapper_l0btb.removeMapping(result.entry->TPC, fromDecode->decodeInfo[tid].mispredictInst->pcState().instAddr());
                }

            }
        }

        set(pc[tid], *fromDecode->decodeInfo[tid].nextPC);
        lookupUopCacheFlag = true;
        lookupCamFlag = true;

        if (bpu0Status[tid] != Squashing) {

            DPRINTF(RxuBpu0, "Squashing from decode with PC = %s\n",
                *fromDecode->decodeInfo[tid].nextPC);
            squash(tid, fromDecode->decodeInfo[tid].doneSeqNum);

            return true;
        }
    }

    if (fromBpu1->bpu1Info[tid].squash) {
        waitBpuSquash = false;
        DPRINTF(RxuBpu0, "[tid:%i] Squashing instructions until [sn:%llu] due to squash "
                "from bpu1.\n",tid, fromBpu1->bpu1Info[tid].doneSeqNum);

        // if (fromBpu1->bpu1Info[tid].branchInst &&
        //     fromBpu1->bpu1Info[tid].branchInst->isControl()) {
        //     auto result = l0btb.lookup(fromBpu1->bpu1Info[tid].branchInst->pcState().instAddr());
        //     if (result.hit && result.entry.TPC != (*fromBpu1->bpu1Info[tid].pc).instAddr()) {
        //         if (fromBpu1->bpu1Info[tid].branchTaken) {
        //             l0btb.updateTPC(fromBpu1->bpu1Info[tid].branchInst->pcState().instAddr(), 
        //             (*fromBpu1->bpu1Info[tid].pc).instAddr());

        //             DPRINTF(RxuBpu0, "br_PC: [%#x], TPC: [%#x] -> new_TPC: [%#x] due to squash from bpu1.\n"
        //                     , fromBpu1->bpu1Info[tid].branchInst->pcState().instAddr(), result.entry.TPC,
        //                     (*fromBpu1->bpu1Info[tid].pc).instAddr());

        //             if (!result.instructionsSaved) {
        //                 needSavedPC.erase(result.entry.TPC);
        //                 if (needSavedPC.empty()) {
        //                     needSaved = false;
        //                 }
        //             }

        //             needSaved = true;
        //             needSavedPC[(*fromBpu1->bpu1Info[tid].pc).instAddr()] = fromBpu1->bpu1Info[tid].branchInst->pcState().instAddr();
        //         } else {
        //             l0btb.invalidateEntry(fromBpu1->bpu1Info[tid].branchInst->pcState().instAddr());

        //             DPRINTF(RxuBpu0, "br_PC: [%#x], TPC: [%#x] invalidated from L0BTB due to not taken.\n"
        //                     , fromBpu1->bpu1Info[tid].branchInst->pcState().instAddr(), result.entry.TPC);

        //             if (!result.instructionsSaved) {
        //                 needSavedPC.erase(result.entry.TPC);
        //                 if (needSavedPC.empty()) {
        //                     needSaved = false;
        //                 }
        //             }
        //         }
        //     }
        // }

        set(pc[tid], *fromBpu1->bpu1Info[tid].pc);
        lookupUopCacheFlag = true;
        lookupCamFlag = true;

        if (bpu0Status[tid] != Squashing) {

            DPRINTF(RxuBpu0, "Squashing from bpu1 with PC = %s\n",
                *fromBpu1->bpu1Info[tid].pc);
            squash(tid, fromBpu1->bpu1Info[tid].doneSeqNum);

            bpu0Status[tid] = Running;

            return true;
        }
    }

    if (checkStall(tid)) {
        return block(tid);
    }

    if (bpu0Status[tid] == Blocked) {
        DPRINTF(RxuBpu0, "[tid:%i] Done blocking, switching to unblocking.\n",
                tid);

        bpu0Status[tid] = Unblocking;

        unblock(tid);

        return true;
    }

    if (bpu0Status[tid] == Squashing) {
        DPRINTF(RxuBpu0, "[tid:%i] Done squashing, switching to running.\n",
                tid);

        bpu0Status[tid] = Running;

        return false;
    }

    // If we've reached this point, we have not gotten any signals that
    // cause bpu1 to change its status.  Bpu1 remains the same as before.
    return false;
}

void
Bpu0::tick()
{
    DynInstPtr inst;

    wroteToTimeBuffer = false;

    bool status_change = false;

    toBpu1Index = 0;

    list<ThreadID>::iterator threads = activeThreads->begin();
    list<ThreadID>::iterator end = activeThreads->end();

    if (fetchInstsValid())
        sortInsts();

    //Check stall and squash signals.
    while (threads != end) {
        ThreadID tid = *threads++;

        DPRINTF(RxuBpu0,"%i cacheline in insts[%i].\n", insts[tid].size(), tid);
        if (!insts[tid].empty()) {
            DPRINTF(RxuBpu0,"head of insts[%i] is [sn:%llu] with PC %s\n", tid,
                    insts[tid].front().insts[0]->seqNum, insts[tid].front().insts[0]->pcState());
        }

        DPRINTF(RxuBpu0,"Processing [tid:%i]\n",tid);
        status_change =  checkSignalsAndUpdate(tid) || status_change;

        bpu0(status_change, tid);
    }

    if (status_change) {
        updateStatus();
    }

    if (toBpu1Index) {
        stats.sendCachelineCycles++;
    } else if (bpu0Status[0] != Squashing && bpu0Status[0] != Blocked) {
        stats.noCachelineCycles++;
    } else {
        stats.stallCycles++;
    }

    if (bpu0Status[0] != Squashing && bpu0Status[0] != Blocked) {
        if (toBpu1Index) {
            DPRINTF(RxuBpu0,"Current bubbles: [%i].\n", bubbles);
            stats.bubbleCount.sample(bubbles);
            for(int i = 0; i < toBpu1Index; i++){
                toBpu1->insts[i]->bubbles = bubbles;
            }
            for(int i = 0; i < toBpu1Index; i++) {
                stats.bubbleInstsCount.sample(bubbles);
            }
            bubbles = 0;
        } else {
            bubbles++;
        }
    }

    if (toBpu1Index)
        stats.cachelineOutInsts.sample(toBpu1Index);

    if (wroteToTimeBuffer) {
        DPRINTF(RxuActivity, "Activity this cycle.\n");

        cpu->activityThisCycle();
    }
}

void
Bpu0::bpu0(bool &status_change, ThreadID tid)
{
    if (bpu0Status[tid] == Blocked) {
        ++stats.blockedCycles;
    } else if (bpu0Status[tid] == Squashing) {
        ++stats.squashCycles;
    }

    if (bpu0Status[tid] == Running ||
        bpu0Status[tid] == Idle) {
        DPRINTF(RxuBpu0, "[tid:%i] Not blocked, so attempting to run bpu1 "
                "stage.\n",tid);

        processInsts(tid);
    } else if (bpu0Status[tid] == Unblocking) {
        DPRINTF(RxuBpu0, "[tid:%i] Unblocking status.\n",tid);
        processInsts(tid);

        status_change = unblock(tid) || status_change;
    }
}

void
Bpu0::processInsts(ThreadID tid)
{
    if (waitBpuSquash) {
        waitSendInsts[tid].clear();
        insts[tid].clear();
        return;
    }

    int insts_available = 0;
    std::unique_ptr<PCStateBase> this_PC;
    std::unique_ptr<PCStateBase> next_PC;
    set(this_PC, *pc[tid]);
    set(next_PC, *pc[tid]);
    std::unique_ptr<PCStateBase> bpu1_TPC(this_PC->clone());
    bool l0btb_hit = false;
    bool l0btb_hit_nohave_insts = false;
    bool return_cam_hit = false;
    bool return_hit_nohave_insts = false;
    bool bpu1_taken = false;
    bool taken = false;
    bool fetch_valid = false;

    if (!waitSendInsts[tid].empty() && !insts[tid].empty()) {
        insts[tid].clear();
    }

    if (!waitSendInsts[tid].empty()) {
        insts_available = waitSendInsts[tid].size();
        std::list<DynInstPtr> &insts_to_bpu1 = waitSendInsts[tid];

        DPRINTF(RxuBpu0, "[tid:%i] Starting to send instructions "
                "to bpu1 from L0Buffer/Return_Cam.\n",tid);
        
        ++stats.L0BTB_CAM_Hit_Count;

        while (!insts_to_bpu1.empty() && toBpu1Index < bpu0Width) {

            DynInstPtr inst = insts_to_bpu1.front();

            insts_to_bpu1.pop_front();

            // Add instruction to the CPU's list of instructions.
            inst->setInstListIt(cpu->addInst(inst));

            DPRINTF(RxuBpu0, "[tid:%i] Processing instruction [sn:%lli] with "
                    "PC %s\n", tid, inst->seqNum, inst->pcState());

            if (inst->isSquashed()) {
                DPRINTF(RxuBpu0, "[tid:%i] Instruction %i with PC %s is "
                        "squashed, skipping.\n",
                        tid, inst->seqNum, inst->pcState());

                ++stats.squashedInsts;
                continue;
            }

            set(this_PC, *(inst->pc));
            set(next_PC, this_PC);

            toBpu1->insts[toBpu1Index] = inst;
            ++(toBpu1->size);
            ++toBpu1Index;
            if  (toBpu1Index == 1) {
                inst->fromWhichContainer[0] = true;
            }
            ++stats.bpu0edInsts;

#if TRACING_ON
            if (debug::RxuO3PipeView) {
                inst->bpu0Tick = curTick() - inst->fetchTick;
            }
#endif

            bool bpu1_used = false;

            if (!bpu1_taken) {
                set(bpu1_TPC, inst->pc);
                if (inst->isControl())
                    DPRINTF(RxuBPU, "[sn:%i] is looking up BPU.\n",
                            inst->seqNum);
                bpu1_taken |= cpu->bpu1.lookupAndUpdateNextPC(inst, *bpu1_TPC);
                bpu1_used = true;
            }

            if (inst->isControl()) {
                if (!inst->isReturn())
                    ++stats.L0BTB_branchInsts;
                auto l0btb_result = l0btb.lookup(this_PC->instAddr());

                if (l0btb_result.hit) {
                    l0btb_hit = true;
                    waitSendInsts[tid].clear();
                    insts[tid].clear();

                    std::unique_ptr<PCStateBase> bpu0_TPC;
                    set(bpu0_TPC, *l0btb_result.entry->tpc);
                    inst->setPredTarg0(*bpu0_TPC);
                    inst->setPredTaken0(true);

                    if (l0btb_result.instructionsSaved) {
                        set(this_PC, bpu0_TPC);
                        set(next_PC, this_PC);
                        inst->staticInst->advancePC(*next_PC);
                        for (int i = 0; i < l0btb_result.entry->size; i++) {
                            set(this_PC, l0btb_result.entry->insts_pc[i]);
                            set(next_PC, this_PC);
                            this_PC->as<RiscvISA::PCState>().vl(inst->mutablePCState().getVl());
                            this_PC->as<RiscvISA::PCState>().vtype(inst->mutablePCState().getVtype());
                            next_PC->as<RiscvISA::PCState>().vl(inst->mutablePCState().getVl());
                            next_PC->as<RiscvISA::PCState>().vtype(inst->mutablePCState().getVtype());
                            DynInstPtr instruction = buildInst(0, l0btb_result.entry->insts[i], NULL, *this_PC, *next_PC, true);
                            instruction->staticInst->advancePC(*this_PC);
                            instruction->staticInst->advancePC(*next_PC);
                        }
                    } else {
                        ++stats.L0BTB_hit_nohaveInsts;
                        
                        l0btb_hit_nohave_insts = true;
                        set(this_PC, bpu0_TPC);
                        set(next_PC, this_PC);
                        inst->staticInst->advancePC(*next_PC);
                    }
                } else {
                    if (!inst->isReturn())
                        ++stats.L0BTB_missInsts;

                    inst->staticInst->advancePC(*this_PC);
                    inst->staticInst->advancePC(*next_PC);
                    inst->setPredTarg0(*next_PC);
                    inst->setPredTaken0(false);
                    set(pc[tid], *this_PC);
                }
            } else {
                inst->staticInst->advancePC(*this_PC);
                inst->staticInst->advancePC(*next_PC);
                inst->setPredTarg0(*next_PC);
                inst->setPredTaken0(false);
                set(pc[tid], *this_PC);
            }

            if (bpu1_used && inst->isReturn() && inst->readPredTaken()) {
                DPRINTF(RxuBpu0, "Done processing, bpu0 and bpu1 not equal detected with PC = (%#x) by BPU.\n", inst->pcState().instAddr());
                DPRINTF(RxuBpu0,
                        "BPU0 PredTaken: %d, BPU1 PredTaken: %d; BPU0 TPC: (%#x), BPU1 TPC: (%#x)",
                        inst->readPredTaken0(),
                        inst->readPredTaken(),
                        inst->readPredTarg0().instAddr(),
                        inst->readPredTarg0().instAddr());
                DPRINTF(RxuBpu0, "TPC = (%#x) by RAS.\n", inst->readPredTarg().instAddr());

                std::unique_ptr<PCStateBase> tpc(inst->readPredTarg().clone());
                auto return_Cam_result = return_Cam.query(inst->readPredTarg().instAddr(), std::move(tpc));
                
                if (return_Cam_result.hit) {
                    return_cam_hit = true;
                    waitSendInsts[tid].clear();
                    insts[tid].clear();

                    std::unique_ptr<PCStateBase> Return_TPC;
                    set(Return_TPC, *return_Cam_result.entry->tpc);
                    inst->setPredTarg0(*Return_TPC);
                    inst->setPredTaken0(true);

                    if (return_Cam_result.instructionsSaved) {
                        set(this_PC, Return_TPC);
                        set(next_PC, this_PC);
                        inst->staticInst->advancePC(*next_PC);
                        for (int i = 0; i < return_Cam_result.entry->size; i++) {
                            set(this_PC, return_Cam_result.entry->insts_pc[i]);
                            set(next_PC, this_PC);
                            this_PC->as<RiscvISA::PCState>().vl(inst->mutablePCState().getVl());
                            this_PC->as<RiscvISA::PCState>().vtype(inst->mutablePCState().getVtype());
                            next_PC->as<RiscvISA::PCState>().vl(inst->mutablePCState().getVl());
                            next_PC->as<RiscvISA::PCState>().vtype(inst->mutablePCState().getVtype());
                            DynInstPtr instruction = buildInst(0, return_Cam_result.entry->insts[i], NULL, *this_PC, *next_PC, true);
                            instruction->staticInst->advancePC(*this_PC);
                            instruction->staticInst->advancePC(*next_PC);
                        }
                    } else {     
                        ++stats.Return_cam_missInsts;
                        return_hit_nohave_insts = true;
                        set(this_PC, Return_TPC);
                        set(next_PC, this_PC);
                        inst->staticInst->advancePC(*next_PC);
                    }
                } else {
                    ++stats.Return_cam_missInsts;
                    DPRINTF(RxuBpu0, "Sending TPC: (%#x) to fetch due to Return_Cam missing.\n", inst->readPredTarg().instAddr());
                    waitSendInsts[tid].clear();
                    insts[tid].clear();
                    set(pc[tid], inst->readPredTarg());
                    taken = true;
                    break;
                }
            }

            if (bpu1_used && (inst->readPredTaken() != inst->readPredTaken0() || inst->readPredTarg().instAddr() != inst->readPredTarg0().instAddr())) {
                ++stats.correctByBpu;
                DPRINTF(RxuBpu0, "Done processing, taken branch instruction detected with PC = (%s) by BPU.\n", inst->pcState());
                DPRINTF(RxuBpu0, "Waiting Bpu1's squash.\n");
                waitSendInsts[tid].clear();
                insts[tid].clear();
                waitBpuSquash = true;
                break;
            }
            
            if (l0btb_hit) {
                DPRINTF(RxuBpu0, "Done processing, branch detected with PC = (%#x) by L0BTB.\n", inst->pcState().instAddr());
                if (!l0btb_hit_nohave_insts) {
                    DPRINTF(RxuBpu0, "L0Buffer hits! sending TPC+64: (%#x) to fetch.\n", this_PC->instAddr());
                } else {
                    DPRINTF(RxuBpu0, "L0Buffer misses! sending TPC: (%#x) to fetch.\n", this_PC->instAddr());
                }
                set(pc[tid], *this_PC);
                break;
            }

            if (return_cam_hit) {
                if (!return_hit_nohave_insts) {
                    DPRINTF(RxuBpu0, "Return_Cam hits! sending TPC+64: (%#x) to fetch.\n", this_PC->instAddr());
                } else {
                    DPRINTF(RxuBpu0, "Return_Cam misses! sending TPC: (%#x) to fetch.\n", this_PC->instAddr());
                }
                set(pc[tid], *this_PC);
                break;
            }
        }
    } else if (!insts[tid].empty()) {
        insts_available = insts[tid].front().size;
        fetchCacheline fcl_to_bpu1 = insts[tid].front();
        insts[tid].pop_front();

        DPRINTF(RxuBpu0, "[tid:%i] Starting to send instructions "
                "to bpu1 from fetch.\n",tid);

        ++stats.L0BTB_CAM_Miss_Count;

        for (int i = 0; i < fcl_to_bpu1.size; i++) {

            DynInstPtr inst = fcl_to_bpu1.insts[i];

            inst->seqNum = cpu->getAndIncrementInstSeq(incrementVector, incrementNum);
            incrementVector = false;

            if (inst->isSplitMacro()) {
                incrementVector = true;
                incrementNum = 200;
            } else if (inst->isVector()) {
                incrementVector = true;
                incrementNum = 40;
            }

            // Add instruction to the CPU's list of instructions.
            inst->setInstListIt(cpu->addInst(inst));

            DPRINTF(RxuBpu0, "[tid:%i] Processing instruction [sn:%lli] with "
                    "PC %s\n", tid, inst->seqNum, inst->pcState());

            if (inst->isSquashed()) {
                DPRINTF(RxuBpu0, "[tid:%i] Instruction %i with PC %s is "
                        "squashed, skipping.\n",
                        tid, inst->seqNum, inst->pcState());

                ++stats.squashedInsts;
                continue;
            }

            set(this_PC, *(inst->pc));
            set(next_PC, this_PC);

            toBpu1->insts[toBpu1Index] = inst;

            ++(toBpu1->size);
            ++toBpu1Index;
            ++stats.bpu0edInsts;

            if (!fetch_valid) {
                fetch_valid = true;
                for (int j = 0; j < fcl_to_bpu1.static_size; j++) {
                    toBpu1->static_insts[j] = fcl_to_bpu1.static_insts[j];
                    toBpu1->pc_insts[j] = fcl_to_bpu1.pc_insts[j];
                    toBpu1->static_size++;
                }
                toBpu1->pc = fcl_to_bpu1.pc;
            }

#if TRACING_ON
            if (debug::RxuO3PipeView) {
                inst->bpu0Tick = curTick() - inst->fetchTick;
            }
#endif

            bool bpu1_used = false;

            if (!bpu1_taken) {
                set(bpu1_TPC, inst->pc);
                if (inst->isControl())
                    DPRINTF(RxuBPU, "[sn:%i] is looking up BPU.\n",
                            inst->seqNum);
                bpu1_taken |= cpu->bpu1.lookupAndUpdateNextPC(inst, *bpu1_TPC);
                bpu1_used = true;
            }

            if (inst->isControl()) {
                if (!inst->isReturn())
                    ++stats.L0BTB_branchInsts;
                auto l0btb_result = l0btb.lookup(this_PC->instAddr());

                if (l0btb_result.hit) {
                    l0btb_hit = true;
                    waitSendInsts[tid].clear();
                    insts[tid].clear();

                    std::unique_ptr<PCStateBase> bpu0_TPC;
                    set(bpu0_TPC, *l0btb_result.entry->tpc);
                    inst->setPredTarg0(*bpu0_TPC);
                    inst->setPredTaken0(true);

                    if (l0btb_result.instructionsSaved) {
                        set(this_PC, bpu0_TPC);
                        set(next_PC, this_PC);
                        inst->staticInst->advancePC(*next_PC);
                        for (int i = 0; i < l0btb_result.entry->size; i++) {
                            set(this_PC, l0btb_result.entry->insts_pc[i]);
                            set(next_PC, this_PC);
                            this_PC->as<RiscvISA::PCState>().vl(inst->mutablePCState().getVl());
                            this_PC->as<RiscvISA::PCState>().vtype(inst->mutablePCState().getVtype());
                            next_PC->as<RiscvISA::PCState>().vl(inst->mutablePCState().getVl());
                            next_PC->as<RiscvISA::PCState>().vtype(inst->mutablePCState().getVtype());
                            DynInstPtr instruction = buildInst(0, l0btb_result.entry->insts[i], NULL, *this_PC, *next_PC, true);
                            instruction->staticInst->advancePC(*this_PC);
                            instruction->staticInst->advancePC(*next_PC);
                        }
                    } else {
                        ++stats.L0BTB_hit_nohaveInsts;
                        
                        l0btb_hit_nohave_insts = true;
                        set(this_PC, bpu0_TPC);
                        set(next_PC, this_PC);
                        inst->staticInst->advancePC(*next_PC);
                    }
                } else {
                    if (!inst->isReturn())
                        ++stats.L0BTB_missInsts;

                    inst->staticInst->advancePC(*this_PC);
                    inst->staticInst->advancePC(*next_PC);
                    inst->setPredTarg0(*next_PC);
                    inst->setPredTaken0(false);
                    set(pc[tid], *this_PC);
                }
            } else {
                inst->staticInst->advancePC(*this_PC);
                inst->staticInst->advancePC(*next_PC);
                inst->setPredTarg0(*next_PC);
                inst->setPredTaken0(false);
                set(pc[tid], *this_PC);
            }

            if (bpu1_used && inst->isReturn() && inst->readPredTaken()) {
                DPRINTF(RxuBpu0, "Done processing, return instruction detected with PC = (%#x) by RAS.\n", inst->pcState().instAddr());
                DPRINTF(RxuBpu0, "TPC = (%#x) by RAS.\n", inst->readPredTarg().instAddr());

                std::unique_ptr<PCStateBase> tpc(inst->readPredTarg().clone());
                auto return_Cam_result = return_Cam.query(inst->readPredTarg().instAddr(), std::move(tpc));
                
                if (return_Cam_result.hit) {
                    return_cam_hit = true;
                    waitSendInsts[tid].clear();
                    insts[tid].clear();

                    std::unique_ptr<PCStateBase> Return_TPC;
                    set(Return_TPC, *return_Cam_result.entry->tpc);
                    inst->setPredTarg0(*Return_TPC);
                    inst->setPredTaken0(true);

                    if (return_Cam_result.instructionsSaved) {
                        set(this_PC, Return_TPC);
                        set(next_PC, this_PC);
                        inst->staticInst->advancePC(*next_PC);
                        for (int i = 0; i < return_Cam_result.entry->size; i++) {
                            set(this_PC, return_Cam_result.entry->insts_pc[i]);
                            set(next_PC, this_PC);
                            this_PC->as<RiscvISA::PCState>().vl(inst->mutablePCState().getVl());
                            this_PC->as<RiscvISA::PCState>().vtype(inst->mutablePCState().getVtype());
                            next_PC->as<RiscvISA::PCState>().vl(inst->mutablePCState().getVl());
                            next_PC->as<RiscvISA::PCState>().vtype(inst->mutablePCState().getVtype());
                            DynInstPtr instruction = buildInst(0, return_Cam_result.entry->insts[i], NULL, *this_PC, *next_PC, true);
                            instruction->staticInst->advancePC(*this_PC);
                            instruction->staticInst->advancePC(*next_PC);
                        }
                    } else {      
                        ++stats.Return_cam_missInsts;               
                        return_hit_nohave_insts = true;
                        set(this_PC, Return_TPC);
                        set(next_PC, this_PC);
                        inst->staticInst->advancePC(*next_PC);
                    }
                } else {
                    ++stats.Return_cam_missInsts;
                    DPRINTF(RxuBpu0, "Sending TPC: (%#x) to fetch due to Return_Cam missing.\n", inst->readPredTarg().instAddr());
                    waitSendInsts[tid].clear();
                    insts[tid].clear();
                    set(pc[tid], inst->readPredTarg());
                    taken = true;
                    break;
                }
            }

            if (bpu1_used && (inst->readPredTaken() != inst->readPredTaken0() || inst->readPredTarg().instAddr() != inst->readPredTarg0().instAddr())) {
                ++stats.correctByBpu;
                DPRINTF(RxuBpu0, "Done processing, taken branch instruction detected with PC = (%s) by BPU.\n", inst->pcState());
                DPRINTF(RxuBpu0, "Waiting Bpu1's squash.\n");
                waitSendInsts[tid].clear();
                insts[tid].clear();
                waitBpuSquash = true;
                break;
            }
            
            if (l0btb_hit) {
                DPRINTF(RxuBpu0, "Done processing, branch detected with PC = (%#x) by L0BTB.\n", inst->pcState().instAddr());
                if (!l0btb_hit_nohave_insts) {
                    DPRINTF(RxuBpu0, "L0Buffer hits! sending TPC+64: (%#x) to fetch.\n", this_PC->instAddr());
                } else {
                    DPRINTF(RxuBpu0, "L0Buffer misses! sending TPC: (%#x) to fetch.\n", this_PC->instAddr());
                }
                set(pc[tid], *this_PC);
                break;
            }

            if (return_cam_hit) {
                if (!return_hit_nohave_insts) {
                    DPRINTF(RxuBpu0, "Return_Cam hits! sending TPC+64: (%#x) to fetch.\n", this_PC->instAddr());
                } else {
                    DPRINTF(RxuBpu0, "Return_Cam misses! sending TPC: (%#x) to fetch.\n", this_PC->instAddr());
                }
                set(pc[tid], *this_PC);
                break;
            }
        }
    } else {
        insts_available = 0;
    }

    if (insts_available == 0) {
        DPRINTF(RxuBpu0, "[tid:%i] Nothing to do, breaking out"
                " early.\n",tid);
        // Should I change the status to idle?
        ++stats.idleCycles;
    } else if (bpu0Status[tid] == Unblocking) {
        DPRINTF(RxuBpu0, "[tid:%i] Unblocking status.\n",tid);
        ++stats.unblockCycles;
    } else if (bpu0Status[tid] == Running) {
        DPRINTF(RxuBpu0, "[tid:%i] Running status.\n",tid);
        ++stats.runCycles;
    }

    if (toBpu1Index) {
        wroteToTimeBuffer = true;
        lookupUopCacheFlag = true;
        lookupCamFlag = true;
    }

    if (l0btb_hit) {
        insts[tid].clear();
        lookupUopCacheFlag = true;
        lookupCamFlag = true;
        taken = true;
    }

    if (return_cam_hit) {
        insts[tid].clear();
        lookupUopCacheFlag = true;
        lookupCamFlag = true;
        taken = true;
    }

    if (lookupCamFlag && !waitBpuSquash) {
        DPRINTF(RxuBpu0, "Sending next PC: (%#x) to Cam fetch.\n", pc[tid]->instAddr());
        cpu->fetch.lookupIsCam(*pc[tid], taken);
        lookupCamFlag = false;
    }

    if ((cpu->fetch.camsend == false)&&lookupUopCacheFlag && !waitBpuSquash) {
        DPRINTF(RxuBpu0, "CAM not look, Sending next PC: (%#x) to cam fetch.\n", pc[tid]->instAddr());
        cpu->fetch.lookupUopCache(*pc[tid], taken);
        lookupUopCacheFlag = false;
    } else if (cpu->fetch.camsend == true) {
        cpu->fetch.clearUC();
    }
    cpu->fetch.camsend = false;
}

DynInstPtr
Bpu0::buildInst(ThreadID tid, StaticInstPtr staticInst,
        StaticInstPtr curMacroop, const PCStateBase &this_pc,
        const PCStateBase &next_pc, bool trace)
{
    // Get a sequence number.
    InstSeqNum seq = cpu->getAndIncrementInstSeq(incrementVector, incrementNum);
    incrementVector = false;

    DynInst::Arrays arrays;
    /** for dyninst initial. */
    if (staticInst->isSpecialVector()) {
        arrays.numSrcs = 28;
    } else {
        arrays.numSrcs = staticInst->numSrcRegs();
    }
    arrays.numDests = staticInst->numDestRegs();

    // Create a new DynInst
    DynInstPtr instruction = new (arrays) DynInst(
            arrays, staticInst, curMacroop, this_pc, next_pc, seq, cpu);
    instruction->setTid(tid);

    if (instruction->isSplitMacro()) {
        incrementVector = true;
        incrementNum = 200;
    } else if (instruction->isVector()) {
        incrementVector = true;
        incrementNum = 40;
    }

    if (staticInst->isCompressed()) instruction->pc->as<RiscvISA::PCState>().compressed(true);

    instruction->setThreadState(cpu->thread[tid]);

    DPRINTF(RxuBpu0, "[tid:%i] Instruction PC %s created [sn:%lli] by L0BTB/Return_Cam. compressed: %d\n",
            tid, this_pc, seq, instruction->pcState().as<RiscvISA::PCState>().compressed());

    DPRINTF(RxuBpu0, "[tid:%i] Instruction is: %s\n", tid,
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

    waitSendInsts[tid].push_back(instruction);

    return instruction;
}

} // namespace rxuo3
} // namespace gem5
