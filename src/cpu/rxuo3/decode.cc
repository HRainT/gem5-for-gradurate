#include "cpu/rxuo3/decode.hh"

#include "arch/generic/pcstate.hh"
#include "base/trace.hh"
#include "cpu/inst_seq.hh"
#include "cpu/rxuo3/dyn_inst.hh"
#include "cpu/rxuo3/limits.hh"
#include "debug/RxuActivity.hh"
#include "debug/RxuDecode.hh"
#include "debug/RxuO3PipeView.hh"
#include "debug/RxuMisPredPC.hh"
#include "params/BaseRxuO3CPU.hh"
#include "sim/full_system.hh"

// clang complains about std::set being overloaded with Packet::set if
// we open up the entire namespace std
using std::list;

namespace gem5
{

namespace rxuo3
{

Decode::Decode(CPU *_cpu, const BaseRxuO3CPUParams &params)
    : cpu(_cpu),
      // -- add by hongfei.liu ----------------------------
      predisqToDecodeDelay(params.predisqToDecodeDelay),
      // --------------------------------------------------
    //   renameToDecodeDelay(params.renameToDecodeDelay),
      ewToDecodeDelay(params.ewToDecodeDelay),
      commitToDecodeDelay(params.commitToDecodeDelay),
      IBandLBToDecodeDelay(params.IBandLBToDecodeDelay),
      decodeWidth(params.decodeWidth),
      numThreads(params.numThreads),
      stats(_cpu)
{
    if (decodeWidth > MaxWidth)
        fatal("decodeWidth (%d) is larger than compiled limit (%d),\n"
             "\tincrease MaxWidth in src/cpu/rxuo3/limits.hh\n",
             decodeWidth, static_cast<int>(MaxWidth));

    // @todo: Make into a parameter
    skidBufferMax = (IBandLBToDecodeDelay + 1) *  params.decodeWidth;
    for (int tid = 0; tid < MaxThreads; tid++) {
        stalls[tid] = {false};
        decodeStatus[tid] = Idle;
        bdelayDoneSeqNum[tid] = 0;
        squashInst[tid] = nullptr;
        squashAfterDelaySlot[tid] = 0;
    }
}

void
Decode::startupStage()
{
    resetStage();
}

void
Decode::clearStates(ThreadID tid)
{
    decodeStatus[tid] = Idle;
    // -- add by hongfei.liu ----------
    // stalls[tid].rename = false;
    stalls[tid].predisq = false;
    // --------------------------------
}

void
Decode::resetStage()
{
    _status = Inactive;

    // Setup status, make sure stall signals are clear.
    for (ThreadID tid = 0; tid < numThreads; ++tid) {
        decodeStatus[tid] = Idle;

        // -- add by hongfei.liu ----------
        // stalls[tid].rename = false;
        stalls[tid].predisq = false;
        // --------------------------------
    }
}

std::string
Decode::name() const
{
    return cpu->name() + ".decode";
}

Decode::DecodeStats::DecodeStats(CPU *cpu)
    : statistics::Group(cpu, "decode"),
      ADD_STAT(idleCycles, statistics::units::Cycle::get(),
               "Number of cycles decode is idle"),
      ADD_STAT(blockedCycles, statistics::units::Cycle::get(),
               "Number of cycles decode is blocked"),
      ADD_STAT(runCycles, statistics::units::Cycle::get(),
               "Number of cycles decode is running"),
      ADD_STAT(unblockCycles, statistics::units::Cycle::get(),
               "Number of cycles decode is unblocking"),
      ADD_STAT(squashCycles, statistics::units::Cycle::get(),
               "Number of cycles decode is squashing"),
      ADD_STAT(branchResolved, statistics::units::Count::get(),
               "Number of times decode resolved a branch"),
      ADD_STAT(branchMispred, statistics::units::Count::get(),
               "Number of times decode detected a branch misprediction"),
      ADD_STAT(controlMispred, statistics::units::Count::get(),
               "Number of times decode detected an instruction incorrectly "
               "predicted as a control"),
      ADD_STAT(decodedInsts, statistics::units::Count::get(),
               "Number of instructions handled by decode"),
      ADD_STAT(squashedInsts, statistics::units::Count::get(),
               "Number of squashed instructions handled by decode")
    //   ADD_STAT(bpuMissDecodeCount, statistics::units::Count::get(),
    //            "Number of squash times due to BPU_miss in decode")
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
    // bpuMissDecodeCount.prereq(bpuMissDecodeCount);
}

void
Decode::setTimeBuffer(TimeBuffer<TimeStruct> *tb_ptr)
{
    timeBuffer = tb_ptr;

    // Setup wire to write information back to IBandLB.
    toIBandLB = timeBuffer->getWire(0);

    // Create wires to get information from proper places in time buffer.
    // -- add by hongfei.liu -------------------------------
    fromPredisq = timeBuffer->getWire(-predisqToDecodeDelay);
    // -----------------------------------------------------
    // fromRename = timeBuffer->getWire(-renameToDecodeDelay);
    fromEW = timeBuffer->getWire(-ewToDecodeDelay);
    fromCommit = timeBuffer->getWire(-commitToDecodeDelay);
}

void
Decode::setDecodeQueue(TimeBuffer<DecodeStruct> *dq_ptr)
{
    decodeQueue = dq_ptr;

    // Setup wire to write information to proper place in decode queue.
    // -- add by hongfei.liu --------------
    // toRename = decodeQueue->getWire(0);
    toPredisq = decodeQueue->getWire(0);
    // ------------------------------------
}

void
Decode::setIBandLBQueue(TimeBuffer<IBandLBStruct> *iq_ptr)
{
    IBandLBQueue = iq_ptr;

    // Setup wire to read information from IBandLB queue.
    fromIBandLB = IBandLBQueue->getWire(-IBandLBToDecodeDelay);
}

void
Decode::setActiveThreads(std::list<ThreadID> *at_ptr)
{
    activeThreads = at_ptr;
}

void
Decode::drainSanityCheck() const
{
    for (ThreadID tid = 0; tid < numThreads; ++tid) {
        assert(insts[tid].empty());
        assert(skidBuffer[tid].empty());
    }
}

bool
Decode::isDrained() const
{
    for (ThreadID tid = 0; tid < numThreads; ++tid) {
        if (!insts[tid].empty() || !skidBuffer[tid].empty() ||
                (decodeStatus[tid] != Running && decodeStatus[tid] != Idle))
            return false;
    }
    return true;
}

bool
Decode::checkStall(ThreadID tid) const
{
    bool ret_val = false;

    // -- add by hongfei.liu -------------------------------------------------------
    // if (stalls[tid].rename) {
    //     DPRINTF(RxuDecode,"[tid:%i] Stall fom Rename stage detected.\n", tid);
    //     ret_val = true;
    // }
    if (stalls[tid].predisq) {
        DPRINTF(RxuDecode,"[tid:%i] Stall from Predisq stage detected.\n", tid);
        ret_val = true;
    }
    // -----------------------------------------------------------------------------

    return ret_val;
}

bool
Decode::IBandLBInstsValid()
{
    return fromIBandLB->size > 0;
}

bool
Decode::block(ThreadID tid)
{
    DPRINTF(RxuDecode, "[tid:%i] Blocking.\n", tid);

    // Add the current inputs to the skid buffer so they can be
    // reprocessed when this stage unblocks.
    skidInsert(tid);

    // If the decode status is blocked or unblocking then decode has not yet
    // signalled IBandLB to unblock. In that case, there is no need to tell
    // IBandLB to block.
    if (decodeStatus[tid] != Blocked) {
        // Set the status to Blocked.
        decodeStatus[tid] = Blocked;

        if (toIBandLB->decodeUnblock[tid]) {
            toIBandLB->decodeUnblock[tid] = false;
        } else {
            toIBandLB->decodeBlock[tid] = true;
            wroteToTimeBuffer = true;
        }

        return true;
    }

    return false;
}

bool
Decode::unblock(ThreadID tid)
{
    // Decode is done unblocking only if the skid buffer is empty.
    if (skidBuffer[tid].empty()) {
        DPRINTF(RxuDecode, "[tid:%i] Done unblocking.\n", tid);
        toIBandLB->decodeUnblock[tid] = true;
        wroteToTimeBuffer = true;

        decodeStatus[tid] = Running;
        return true;
    }

    DPRINTF(RxuDecode, "[tid:%i] Currently unblocking.\n", tid);

    return false;
}

void
Decode::squash(const DynInstPtr &inst, ThreadID tid)
{
    DPRINTF(RxuDecode, "[tid:%i] [sn:%llu] Squashing due to incorrect branch "
            "prediction detected at decode.\n", tid, inst->seqNum);
    cpu->loopStats[cpu->loopIndex]->bpu1MissDecodeCount++;
    cpu->loopStats[cpu->loopxhIndex]->xhbpuMissDecodeCount++;
    cpu->baseStats.bpu1MissDecodeCount++;

    if (cpu->loopIndex >= 1 && cpu->loopIndex <= 100) {
        ++cpu->baseStats.bpu1MissDecodeCount1_100loop;
    } else if (cpu->loopIndex >= 101 && cpu->loopIndex <= 200) {
        ++cpu->baseStats.bpu1MissDecodeCount101_200loop;
    } else if (cpu->loopIndex >= 201 && cpu->loopIndex <= 300) {
        ++cpu->baseStats.bpu1MissDecodeCount201_300loop;
    } else if (cpu->loopIndex >= 301 && cpu->loopIndex <= 400) {
        ++cpu->baseStats.bpu1MissDecodeCount301_400loop;
    } else if (cpu->loopIndex >= 401 && cpu->loopIndex <= 500) {
        ++cpu->baseStats.bpu1MissDecodeCount401_500loop;
    }

    if (cpu->loopxhIndex >= 1 && cpu->loopxhIndex <= 110) {
        ++cpu->baseStats.xhbpuMissDecodeCount1_110loop;
    }
    if (cpu->loopxhIndex >= 1 && cpu->loopxhIndex <= 30) {
        ++cpu->baseStats.xhbpuMissDecodeCount1_30loop;
    }
    if (cpu->loopxhIndex >= 1 && cpu->loopxhIndex <= 64) {
        ++cpu->baseStats.xhbpuMissDecodeCount1_64loop;
    }
    if (cpu->loopxhIndex >= 1 && cpu->loopxhIndex <= 134) {
        ++cpu->baseStats.xhbpuMissDecodeCount1_134loop;
    }
    if (cpu->loopxhIndex >= 1 && cpu->loopxhIndex <= 1024) {
        ++cpu->baseStats.xhbpuMissDecodeCount1_1024loop;
    }
    if (cpu->loopxhIndex >= 50 && cpu->loopxhIndex <= 110) {
        ++cpu->baseStats.xhbpuMissDecodeCount50_110loop;
    }
    if (cpu->loopxhIndex >= 20 && cpu->loopxhIndex <= 30) {
        ++cpu->baseStats.xhbpuMissDecodeCount20_30loop;
    }
    if (cpu->loopxhIndex >= 30 && cpu->loopxhIndex <= 64) {
        ++cpu->baseStats.xhbpuMissDecodeCount30_64loop;
    }
    if (cpu->loopxhIndex >= 60 && cpu->loopxhIndex <= 134) {
        ++cpu->baseStats.xhbpuMissDecodeCount60_134loop;
    }
    if (cpu->loopxhIndex >= 500 && cpu->loopxhIndex <= 1024) {
        ++cpu->baseStats.xhbpuMissDecodeCount500_1024loop;
    }
    if (cpu->loopxhIndex >= 1000 && cpu->loopxhIndex <= 1024) {
        ++cpu->baseStats.xhbpuMissDecodeCount1000_1024loop;
    }

    if (cpu->loopxhIndex >= 1 && cpu->loopxhIndex <= 110) {
        ++cpu->baseStats.xhbpuMissDecodeCount1_110loop;
    }
    if (cpu->loopxhIndex >= 1 && cpu->loopxhIndex <= 30) {
        ++cpu->baseStats.xhbpuMissDecodeCount1_30loop;
    }
    if (cpu->loopxhIndex >= 1 && cpu->loopxhIndex <= 64) {
        ++cpu->baseStats.xhbpuMissDecodeCount1_64loop;
    }
    if (cpu->loopxhIndex >= 1 && cpu->loopxhIndex <= 134) {
        ++cpu->baseStats.xhbpuMissDecodeCount1_134loop;
    }
    if (cpu->loopxhIndex >= 1 && cpu->loopxhIndex <= 1024) {
        ++cpu->baseStats.xhbpuMissDecodeCount1_1024loop;
    }
    if (cpu->loopxhIndex >= 50 && cpu->loopxhIndex <= 110) {
        ++cpu->baseStats.xhbpuMissDecodeCount50_110loop;
    }
    if (cpu->loopxhIndex >= 20 && cpu->loopxhIndex <= 30) {
        ++cpu->baseStats.xhbpuMissDecodeCount20_30loop;
    }
    if (cpu->loopxhIndex >= 30 && cpu->loopxhIndex <= 64) {
        ++cpu->baseStats.xhbpuMissDecodeCount30_64loop;
    }
    if (cpu->loopxhIndex >= 60 && cpu->loopxhIndex <= 134) {
        ++cpu->baseStats.xhbpuMissDecodeCount60_134loop;
    }
    if (cpu->loopxhIndex >= 500 && cpu->loopxhIndex <= 1024) {
        ++cpu->baseStats.xhbpuMissDecodeCount500_1024loop;
    }
    if (cpu->loopxhIndex >= 1000 && cpu->loopxhIndex <= 1024) {
        ++cpu->baseStats.xhbpuMissDecodeCount1000_1024loop;
    }

    DPRINTF(RxuMisPredPC,
        "BranchMisPred PC:%#x\n", inst->pcState().instAddr());

    // Send back mispredict information.
    toIBandLB->decodeInfo[tid].branchMispredict = true;
    toIBandLB->decodeInfo[tid].predIncorrect = true;
    toIBandLB->decodeInfo[tid].mispredictInst = inst;
    toIBandLB->decodeInfo[tid].squash = true;
    toIBandLB->decodeInfo[tid].doneSeqNum = inst->seqNum;
    set(toIBandLB->decodeInfo[tid].nextPC, *inst->branchTarget());

    // Looking at inst->pcState().branching()
    // may yield unexpected results if the branch
    // was predicted taken but aliased in the BTB
    // with a branch jumping to the next instruction (mistarget)
    // Using PCState::branching()  will send execution on the
    // fallthrough and this will not be caught at execution (since
    // branch was correctly predicted taken)
    toIBandLB->decodeInfo[tid].branchTaken = inst->readPredTaken() ||
                                           inst->isUncondCtrl();

    toIBandLB->decodeInfo[tid].squashInst = inst;

    InstSeqNum squash_seq_num = inst->seqNum;

    // Might have to tell IBandLB to unblock.
    if (decodeStatus[tid] == Blocked ||
        decodeStatus[tid] == Unblocking) {
        toIBandLB->decodeUnblock[tid] = 1;
    }

    // Set status to squashing.
    decodeStatus[tid] = Squashing;

    for (int i=0; i<fromIBandLB->size; i++) {
        if (fromIBandLB->insts[i]->threadNumber == tid &&
            fromIBandLB->insts[i]->seqNum > squash_seq_num) {
            fromIBandLB->insts[i]->setSquashed();
        }
    }

    // Clear the instruction list and skid buffer in case they have any
    // insts in them.
    while (!insts[tid].empty()) {
        insts[tid].front()->setSquashed();
        insts[tid].pop_front();
    }

    while (!skidBuffer[tid].empty()) {
        skidBuffer[tid].front()->setSquashed();
        skidBuffer[tid].pop_front();
    }

    // Squash instructions up until this one
    cpu->removeInstsUntil(squash_seq_num, tid);
}

unsigned
Decode::squash(ThreadID tid)
{
    DPRINTF(RxuDecode, "[tid:%i] Squashing.\n",tid);

    if (decodeStatus[tid] == Blocked ||
        decodeStatus[tid] == Unblocking) {
        if (FullSystem) {
            toIBandLB->decodeUnblock[tid] = 1;
        } else {
            // In syscall emulation, we can have both a block and a squash due
            // to a syscall in the same cycle.  This would cause both signals
            // to be high.  This shouldn't happen in full system.
            // @todo: Determine if this still happens.
            if (toIBandLB->decodeBlock[tid])
                toIBandLB->decodeBlock[tid] = 0;
            else
                toIBandLB->decodeUnblock[tid] = 1;
        }
    }

    // Set status to squashing.
    decodeStatus[tid] = Squashing;

    // Go through incoming instructions from IBandLB and squash them.
    unsigned squash_count = 0;

    for (int i=0; i<fromIBandLB->size; i++) {
        if (fromIBandLB->insts[i]->threadNumber == tid) {
            fromIBandLB->insts[i]->setSquashed();
            squash_count++;
        }
    }

    // Clear the instruction list and skid buffer in case they have any
    // insts in them.
    while (!insts[tid].empty()) {
        insts[tid].front()->setSquashed();
        insts[tid].pop_front();
    }

    while (!skidBuffer[tid].empty()) {
        skidBuffer[tid].front()->setSquashed();
        skidBuffer[tid].pop_front();
    }

    return squash_count;
}

void
Decode::skidInsert(ThreadID tid)
{
    DynInstPtr inst = NULL;

    while (!insts[tid].empty()) {
        inst = insts[tid].front();

        insts[tid].pop_front();

        assert(tid == inst->threadNumber);

        skidBuffer[tid].push_back(inst);

        DPRINTF(RxuDecode, "Inserting [tid:%d][sn:%lli] PC: %s into decode "
                "skidBuffer %i\n", inst->threadNumber, inst->seqNum,
                inst->pcState(), skidBuffer[tid].size());
    }

    // @todo: Eventually need to enforce this by not letting a thread
    // IBandLB past its skidbuffer
    assert(skidBuffer[tid].size() <= skidBufferMax);
}

bool
Decode::skidsEmpty()
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
Decode::updateStatus()
{
    bool any_unblocking = false;

    list<ThreadID>::iterator threads = activeThreads->begin();
    list<ThreadID>::iterator end = activeThreads->end();

    while (threads != end) {
        ThreadID tid = *threads++;

        if (decodeStatus[tid] == Unblocking) {
            any_unblocking = true;
            break;
        }
    }

    // Decode will have activity if it's unblocking.
    if (any_unblocking) {
        if (_status == Inactive) {
            _status = Active;

            DPRINTF(RxuActivity, "Activating stage.\n");

            cpu->activateStage(CPU::DecodeIdx);
        }
    } else {
        // If it's not unblocking, then decode will not have any internal
        // activity.  Switch it to inactive.
        if (_status == Active) {
            _status = Inactive;
            DPRINTF(RxuActivity, "Deactivating stage.\n");

            cpu->deactivateStage(CPU::DecodeIdx);
        }
    }
}

void
Decode::sortInsts()
{
    int insts_from_IBandLB = fromIBandLB->size;
    for (int i = 0; i < insts_from_IBandLB; ++i) {
        insts[fromIBandLB->insts[i]->threadNumber].push_back(fromIBandLB->insts[i]);
    }
}

void
Decode::readStallSignals(ThreadID tid)
{
    // -- add by hongfei.liu -------------------
    // if (fromRename->renameBlock[tid]) {
    //     stalls[tid].rename = true;
    // }

    // if (fromRename->renameUnblock[tid]) {
    //     assert(stalls[tid].rename);
    //     stalls[tid].rename = false;
    // }
    // if (fromPredisq->predisqBlock[tid]) {
    //     stalls[tid].predisq = true;
    // }

    // if (fromPredisq->predisqUnblock[tid]) {
    //     // -- add by hongfei.liu -------------
    //     // assert(stalls[tid].predisq);
    //     // -----------------------------------
    //     stalls[tid].predisq = false;
    // }

    if (cpu->predisq.compressFlag) {
        if (int(fromPredisq->PredisqInfo->freeEntries 
                - cpu->predisq.fromDecode->size) <= 0)
            stalls[tid].predisq = true;
        else stalls[tid].predisq = false;
    } else {
        int decode_to_predisq_inflight_group
            = cpu->predisq.fromDecode->size > 0 ? 1 : 0;
        if (int(fromPredisq->PredisqInfo->freeGroups 
            - decode_to_predisq_inflight_group) <= 0)
            stalls[tid].predisq = true;
        else stalls[tid].predisq = false;
    }
    // -----------------------------------------
}

bool
Decode::checkSignalsAndUpdate(ThreadID tid)
{
    // Check if there's a squash signal, squash if there is.
    // Check stall signals, block if necessary.
    // If status was blocked
    //     Check if stall conditions have passed
    //         if so then go to unblocking
    // If status was Squashing
    //     check if squashing is not high.  Switch to running this cycle.

    // Update the per thread stall statuses.
    readStallSignals(tid);

    // Check squash signals from commit.
    if (fromCommit->commitInfo[tid].squash) {

        DPRINTF(RxuDecode, "[tid:%i] Squashing instructions due to squash "
                "from commit.\n", tid);

        squash(tid);

        return true;
    }

    if (checkStall(tid)) {
        return block(tid);
    }

    if (decodeStatus[tid] == Blocked) {
        DPRINTF(RxuDecode, "[tid:%i] Done blocking, switching to unblocking.\n",
                tid);

        decodeStatus[tid] = Unblocking;

        unblock(tid);

        return true;
    }

    if (decodeStatus[tid] == Squashing) {
        // Switch status to running if decode isn't being told to block or
        // squash this cycle.
        DPRINTF(RxuDecode, "[tid:%i] Done squashing, switching to running.\n",
                tid);

        decodeStatus[tid] = Running;

        return false;
    }

    // If we've reached this point, we have not gotten any signals that
    // cause decode to change its status.  Decode remains the same as before.
    return false;
}

void
Decode::tick()
{
    wroteToTimeBuffer = false;

    bool status_change = false;

    // -- add by hongfei.liu -------
    // toRenameIndex = 0;
    toPredisqIndex = 0;
    // -----------------------------

    list<ThreadID>::iterator threads = activeThreads->begin();
    list<ThreadID>::iterator end = activeThreads->end();

    sortInsts();

    //Check stall and squash signals.
    while (threads != end) {
        ThreadID tid = *threads++;

        DPRINTF(RxuDecode,"Processing [tid:%i]\n",tid);
        status_change =  checkSignalsAndUpdate(tid) || status_change;

        decode(status_change, tid);
    }

    if (status_change) {
        updateStatus();
    }

    if (wroteToTimeBuffer) {
        DPRINTF(RxuActivity, "Activity this cycle.\n");

        cpu->activityThisCycle();
    }
}

void
Decode::decode(bool &status_change, ThreadID tid)
{
    // If status is Running or idle,
    //     call decodeInsts()
    // If status is Unblocking,
    //     buffer any instructions coming from IBandLB
    //     continue trying to empty skid buffer
    //     check if stall conditions have passed

    if (decodeStatus[tid] == Blocked) {
        ++stats.blockedCycles;
    } else if (decodeStatus[tid] == Squashing) {
        ++stats.squashCycles;
    }

    // Decode should try to decode as many instructions as its bandwidth
    // will allow, as long as it is not currently blocked.
    if (decodeStatus[tid] == Running ||
        decodeStatus[tid] == Idle) {
        DPRINTF(RxuDecode, "[tid:%i] Not blocked, so attempting to run "
                "stage.\n",tid);

        decodeInsts(tid);
    } else if (decodeStatus[tid] == Unblocking) {
        // Make sure that the skid buffer has something in it if the
        // status is unblocking.
        assert(!skidsEmpty());

        // If the status was unblocking, then instructions from the skid
        // buffer were used.  Remove those instructions and handle
        // the rest of unblocking.
        decodeInsts(tid);

        if (IBandLBInstsValid()) {
            // Add the current inputs to the skid buffer so they can be
            // reprocessed when this stage unblocks.
            skidInsert(tid);
        }

        status_change = unblock(tid) || status_change;
    }
}

void
Decode::decodeInsts(ThreadID tid)
{
    // Instructions can come either from the skid buffer or the list of
    // instructions coming from IBandLB, depending on decode's status.
    int insts_available = decodeStatus[tid] == Unblocking ?
        skidBuffer[tid].size() : insts[tid].size();

    if (insts_available == 0) {
        DPRINTF(RxuDecode, "[tid:%i] Nothing to do, breaking out"
                " early.\n",tid);
        // Should I change the status to idle?
        ++stats.idleCycles;
        return;
    } else if (decodeStatus[tid] == Unblocking) {
        DPRINTF(RxuDecode, "[tid:%i] Unblocking, removing insts from skid "
                "buffer.\n",tid);
        ++stats.unblockCycles;
    } else if (decodeStatus[tid] == Running) {
        ++stats.runCycles;
    }

    std::list<DynInstPtr>
        &insts_to_decode = decodeStatus[tid] == Unblocking ?
        skidBuffer[tid] : insts[tid];

    // -- add by hongfei.liu ---------------------------------------------
    // DPRINTF(RxuDecode, "[tid:%i] Sending instruction to rename.\n",tid);

    // while (insts_available > 0 && toRenameIndex < decodeWidth) {
    DPRINTF(RxuDecode, "[tid:%i] Sending instruction to predisq.\n",tid);

    while (insts_available > 0 && toPredisqIndex < decodeWidth 
            && toPredisqIndex < (fromPredisq->PredisqInfo->freeEntries 
                - cpu->predisq.fromDecode->size)) {
    // -------------------------------------------------------------------
        assert(!insts_to_decode.empty());

        DynInstPtr inst = std::move(insts_to_decode.front());

        insts_to_decode.pop_front();

        DPRINTF(RxuDecode, "[tid:%i] Processing instruction [sn:%lli] with "
                "PC %s\n", tid, inst->seqNum, inst->pcState());

        if (inst->isSquashed()) {
            DPRINTF(RxuDecode, "[tid:%i] Instruction %i with PC %s is "
                    "squashed, skipping.\n",
                    tid, inst->seqNum, inst->pcState());

            ++stats.squashedInsts;

            --insts_available;

            continue;
        }

        // Also check if instructions have no source registers.  Mark
        // them as ready to issue at any time.  Not sure if this check
        // should exist here or at a later stage; however it doesn't matter
        // too much for function correctness.
        if (inst->numSrcRegs() == 0) {
            inst->setCanIssue();
        }

        // This current instruction is valid, so add it into the decode
        // queue.  The next instruction may not be valid, so check to
        // see if branches were predicted correctly.
        // -- add by hongfei.liu --------------------
        // toRename->insts[toRenameIndex] = inst;

        // ++(toRename->size);
        // ++toRenameIndex;
        toPredisq->insts[toPredisqIndex] = inst;

        ++(toPredisq->size);
        ++toPredisqIndex;
        // ------------------------------------------
        ++stats.decodedInsts;
        --insts_available;

#if TRACING_ON
        if (debug::RxuO3PipeView) {
            inst->decodeTick = curTick() - inst->fetchTick;
        }
#endif

        if (inst->readPredTaken0() && !inst->isControl()) {
            cpu->baseStats.bpu0MissDecodeCount++;
            cpu->loopStats[cpu->loopIndex]->bpu0MissDecodeCount++;
            if (cpu->loopIndex >= 1 && cpu->loopIndex <= 100) {
                ++cpu->baseStats.bpu0MissDecodeCount1_100loop;
            } else if (cpu->loopIndex >= 101 && cpu->loopIndex <= 200) {
                ++cpu->baseStats.bpu0MissDecodeCount101_200loop;
            } else if (cpu->loopIndex >= 201 && cpu->loopIndex <= 300) {
                ++cpu->baseStats.bpu0MissDecodeCount201_300loop;
            } else if (cpu->loopIndex >= 301 && cpu->loopIndex <= 400) {
                ++cpu->baseStats.bpu0MissDecodeCount301_400loop;
            } else if (cpu->loopIndex >= 401 && cpu->loopIndex <= 500) {
                ++cpu->baseStats.bpu0MissDecodeCount401_500loop;
            }
        }
        if (inst->readPredTaken() && !inst->isControl()) {
            cpu->baseStats.bpu1MissDecodeCount++;
            cpu->loopStats[cpu->loopIndex]->bpu1MissDecodeCount++;
            if (cpu->loopIndex >= 1 && cpu->loopIndex <= 100) {
                ++cpu->baseStats.bpu1MissDecodeCount1_100loop;
            } else if (cpu->loopIndex >= 101 && cpu->loopIndex <= 200) {
                ++cpu->baseStats.bpu1MissDecodeCount101_200loop;
            } else if (cpu->loopIndex >= 201 && cpu->loopIndex <= 300) {
                ++cpu->baseStats.bpu1MissDecodeCount201_300loop;
            } else if (cpu->loopIndex >= 301 && cpu->loopIndex <= 400) {
                ++cpu->baseStats.bpu1MissDecodeCount301_400loop;
            } else if (cpu->loopIndex >= 401 && cpu->loopIndex <= 500) {
                ++cpu->baseStats.bpu1MissDecodeCount401_500loop;
            }
        }

        // Ensure that if it was predicted as a branch, it really is a
        // branch.
        if (inst->readPredTaken() && !inst->isControl()) {
            panic("Instruction predicted as a branch!");

            ++stats.controlMispred;

            // Might want to set some sort of boolean and just do
            // a check at the end
            squash(inst, inst->threadNumber);

            break;
        }

        // Go ahead and compute any PC-relative branches.
        // This includes direct unconditional control and
        // direct conditional control that is predicted taken.
        if (inst->isDirectCtrl() &&
           (inst->isUncondCtrl() || inst->readPredTaken()))
        {
            ++stats.branchResolved;

            std::unique_ptr<PCStateBase> target = inst->branchTarget();
            if (cpu->vsetBranch && *target != inst->readPredTarg() || 
               !cpu->vsetBranch && (((*target)._pc != inst->readPredTarg()._pc) && !inst->isVset()
               || (*target != inst->readPredTarg()) && inst->isVset())) {

                ++stats.branchMispred;

                // Might want to set some sort of boolean and just do
                // a check at the end
                squash(inst, inst->threadNumber);

                DPRINTF(RxuDecode,
                        "[tid:%i] [sn:%llu] "
                        "Updating predictions: Wrong predicted target: %s \
                        PredPC: %s\n",
                        tid, inst->seqNum, inst->readPredTarg(), *target);
                //The micro pc after an instruction level branch should be 0
                inst->setPredTarg(*target);
                break;
            }
        }
    }

    // If we didn't process all instructions, then we will need to block
    // and put all those instructions into the skid buffer.
    if (!insts_to_decode.empty()) {
        block(tid);
    }

    // Record that decode has written to the time buffer for activity
    // tracking.
    // -- add by hongfei.liu ---------
    // if (toRenameIndex) {
    if (toPredisqIndex) {        
    // -------------------------------
        wroteToTimeBuffer = true;
    }
}

} // namespace rxuo3
} // namespace gem5
