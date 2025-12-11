#include "cpu/rxuo3/bpu1.hh"

#include "arch/generic/pcstate.hh"
#include "base/trace.hh"
#include "cpu/inst_seq.hh"
#include "cpu/rxuo3/dyn_inst.hh"
#include "cpu/rxuo3/limits.hh"
#include "debug/RxuActivity.hh"
#include "debug/RxuBpu1.hh"
#include "debug/RxuO3PipeView.hh"
#include "debug/RxuUC.hh"
#include "debug/RxuBPU.hh"
#include "params/BaseRxuO3CPU.hh"
#include "sim/full_system.hh"

// clang complains about std::set being overloaded with Packet::set if
// we open up the entire namespace std
using std::list;

namespace gem5
{

namespace rxuo3
{

Bpu1::Bpu1(CPU *_cpu, const BaseRxuO3CPUParams &params)
    : cpu(_cpu),
      IBandLBToBpu1Delay(params.IBandLBToBpu1Delay),
      decodeToBpu1Delay(params.decodeToBpu1Delay),
      commitToBpu1Delay(params.commitToBpu1Delay),
      bpu0ToBpu1Delay(params.bpu0ToBpu1Delay),
      bpu1Width(params.bpu1Width),
      numThreads(params.numThreads),
      stats(_cpu), 
      sr_on(params.system->sr())
{
    if (bpu1Width > MaxWidth)
        fatal("bpu1Width (%d) is larger than compiled limit (%d),\n"
             "\tincrease MaxWidth in src/cpu/rxuo3/limits.hh\n",
             bpu1Width, static_cast<int>(MaxWidth));

    // @todo: Make into a parameter
    for (int tid = 0; tid < MaxThreads; tid++) {
        stalls[tid] = {false};
        bpu1Status[tid] = Idle;
    }

    if (params.system->useLtage()) {
        // branchPred = params.branchPredLTAGE;
    } else if (params.system->useTournamentBP()){
        branchPred = params.branchPredTournamentBP;
    } else if (params.system->useLocalBP()) {
        branchPred = params.branchPredLocalBP;
    } else if (params.system->useBiModeBP()) {
        branchPred = params.branchPredBiModeBP;
    } else if (params.system->useTAGE()) {
        branchPred = params.branchPredTAGE;
    } else {
        branchPred = params.branchPredTAGE;
    }

    ucBlockStoreAddr = 0;
}

void
Bpu1::startupStage()
{
    resetStage();
}

void
Bpu1::clearStates(ThreadID tid)
{
    bpu1Status[tid] = Idle;
    stalls[tid].IBandLB = false;
}

void
Bpu1::resetStage()
{
    _status = Inactive;

    // Setup status, make sure stall signals are clear.
    for (ThreadID tid = 0; tid < numThreads; ++tid) {
        bpu1Status[tid] = Idle;

        stalls[tid].IBandLB = false;
    }
}

std::string
Bpu1::name() const
{
    return cpu->name() + ".bpu1";
}

Bpu1::Bpu1Stats::Bpu1Stats(CPU *cpu)
    : statistics::Group(cpu, "bpu1"),
      ADD_STAT(idleCycles, statistics::units::Cycle::get(),
               "Number of cycles bpu1 is idle"),
      ADD_STAT(blockedCycles, statistics::units::Cycle::get(),
               "Number of cycles bpu1 is blocked"),
      ADD_STAT(runCycles, statistics::units::Cycle::get(),
               "Number of cycles bpu1 is running"),
      ADD_STAT(unblockCycles, statistics::units::Cycle::get(),
               "Number of cycles bpu1 is unblocking"),
      ADD_STAT(squashCycles, statistics::units::Cycle::get(),
               "Number of cycles bpu1 is squashing"),
      ADD_STAT(bpu1edInsts, statistics::units::Count::get(),
               "Number of instructions handled by bpu1"),
      ADD_STAT(squashedInsts, statistics::units::Count::get(),
               "Number of squashed instructions handled by bpu1"),
      ADD_STAT(bpu1OutInsts, "Distribution of number of instructions sent by fetch"),
      ADD_STAT(bpu1OutInsts_includeZero, "Distribution of number of instructions sent by fetch")
{
    idleCycles.prereq(idleCycles);
    blockedCycles.prereq(blockedCycles);
    runCycles.prereq(runCycles);
    unblockCycles.prereq(unblockCycles);
    squashCycles.prereq(squashCycles);
    bpu1edInsts.prereq(bpu1edInsts);
    squashedInsts.prereq(squashedInsts);

    bpu1OutInsts
        .init(1, 16, 1)
        .flags(statistics::nozero);
    bpu1OutInsts_includeZero
        .init(0, 16, 1)
        .flags(statistics::nozero);
}

void
Bpu1::setTimeBuffer(TimeBuffer<TimeStruct> *tb_ptr)
{
    timeBuffer = tb_ptr;

    // Setup wire to write information back to Bpu0.
    toBpu0 = timeBuffer->getWire(0);

    // Create wires to get information from proper places in time buffer.
    fromIBandLB = timeBuffer->getWire(-IBandLBToBpu1Delay);
    fromDecode = timeBuffer->getWire(-decodeToBpu1Delay);
    fromCommit = timeBuffer->getWire(-commitToBpu1Delay);
}

void
Bpu1::setBpu1Queue(TimeBuffer<Bpu1Struct> *b1q_ptr)
{
    bpu1Queue = b1q_ptr;

    // Setup wire to write information to proper place in bpu1 queue.
    toIBandLB = bpu1Queue->getWire(0);
}

void
Bpu1::setBpu0Queue(TimeBuffer<Bpu0Struct> *b0q_ptr)
{
    bpu0Queue = b0q_ptr;

    // Setup wire to read information from bpu0 queue.
    fromBpu0 = bpu0Queue->getWire(-bpu0ToBpu1Delay);
}

void
Bpu1::setActiveThreads(std::list<ThreadID> *at_ptr)
{
    activeThreads = at_ptr;
}

void
Bpu1::drainSanityCheck() const
{
    for (ThreadID tid = 0; tid < numThreads; ++tid) {
        assert(insts[tid].empty());
    }

    branchPred->drainSanityCheck();
}

bool
Bpu1::isDrained() const
{
    for (ThreadID tid = 0; tid < numThreads; ++tid) {
        if (!insts[tid].empty() ||
                (bpu1Status[tid] != Running && bpu1Status[tid] != Idle))
            return false;
    }
    return true;
}

bool
Bpu1::checkStall(ThreadID tid) const
{
    bool ret_val = false;

    if (stalls[tid].IBandLB) {
        DPRINTF(RxuBpu1,"[tid:%i] Stall from IBandLB stage detected.\n", tid);
        ret_val = true;
    }

    return ret_val;
}

bool
Bpu1::bpu0InstsValid()
{
    return fromBpu0->size > 0;
}

bool
Bpu1::block(ThreadID tid)
{
    DPRINTF(RxuBpu1, "[tid:%i] Blocking.\n", tid);

    // If the bpu1 status is blocked or unblocking then bpu1 has not yet
    // signalled bpu0 to unblock. In that case, there is no need to tell
    // bpu0 to block.
    if (bpu1Status[tid] != Blocked) {
        // Set the status to Blocked.
        bpu1Status[tid] = Blocked;

        if (toBpu0->bpu1Block[tid]) {
            toBpu0->bpu1Unblock[tid] = false;
        } else {
            toBpu0->bpu1Block[tid] = true;
            wroteToTimeBuffer = true;
        }

        return true;
    }

    return false;
}

bool
Bpu1::unblock(ThreadID tid)
{
    DPRINTF(RxuBpu1, "[tid:%i] Trying to unblock.\n", tid);

    if (insts[tid].size() < bpu1Width) {

        DPRINTF(RxuBpu1, "[tid:%i] Done unblocking.\n", tid);

        toBpu0->bpu1Unblock[tid] = true;
        wroteToTimeBuffer = true;

        bpu1Status[tid] = Running;
        return true;
    }

    return false;
}

void
Bpu1::squashFromBpu1(const DynInstPtr &inst, ThreadID tid)
{
    DPRINTF(RxuBpu1, "[tid:%i] [sn:%llu] Squashing due to a taken branch "
            "prediction detected at bpu1.\n", tid, inst->seqNum);

    // Set status to squashing.
    bpu1Status[tid] = Squashing;

    for (int i = 0; i < fromBpu0->size; i++) {
        if (fromBpu0->insts[i]->threadNumber == tid &&
            fromBpu0->insts[i]->seqNum > inst->seqNum) {
            if (!fromBpu0->insts[i]->isSquashed()) {
                fromBpu0->insts[i]->setSquashed();
                DPRINTF(RxuBpu1,
                        "[tid:%i] "
                        "instruction %i with PC %s is squashed.\n",
                        tid, fromBpu0->insts[i]->seqNum,
                        fromBpu0->insts[i]->pcState());
            } else {
                DPRINTF(RxuBpu1,
                        "[tid:%i] "
                        "instruction %i with PC %s has already been squashed before.\n",
                        tid, fromBpu0->insts[i]->seqNum,
                        fromBpu0->insts[i]->pcState());
            }
        }
    }

    while (!insts[tid].empty() && 
            insts[tid].back()->seqNum > inst->seqNum) {
        insts[tid].back()->setSquashed();
        DPRINTF(RxuBpu1,
                "[tid:%i] "
                "instruction %i with PC %s is squashed.\n",
                tid, insts[tid].back()->seqNum,
                insts[tid].back()->pcState());
        insts[tid].pop_back();
    }
}

void
Bpu1::squash(ThreadID tid, InstSeqNum squashedSeqNum)
{
    DPRINTF(RxuBpu1, "[tid:%i] [squash sn:%llu] Squashing instructions.\n",
        tid, squashedSeqNum);

    // Set status to squashing.
    bpu1Status[tid] = Squashing;

    if (bpu1Status[tid] == Blocked ||
        bpu1Status[tid] == Unblocking) {
        if (FullSystem) {
            toBpu0->bpu1Unblock[tid] = 1;
        } else {
            if (toBpu0->bpu1Block[tid])
                toBpu0->bpu1Block[tid] = 0;
            else
                toBpu0->bpu1Unblock[tid] = 1;
        }
    }


    for (int i = 0; i < fromBpu0->size; i++) {
        if (fromBpu0->insts[i]->threadNumber == tid &&
            fromBpu0->insts[i]->seqNum > squashedSeqNum) {
            if (!fromBpu0->insts[i]->isSquashed()) {
                fromBpu0->insts[i]->setSquashed();
                DPRINTF(RxuBpu1,
                        "[tid:%i] "
                        "instruction %i with PC %s is squashed.\n",
                        tid, fromBpu0->insts[i]->seqNum,
                        fromBpu0->insts[i]->pcState());
            } else {
                DPRINTF(RxuBpu1,
                        "[tid:%i] "
                        "instruction %i with PC %s has already been squashed before.\n",
                        tid, fromBpu0->insts[i]->seqNum,
                        fromBpu0->insts[i]->pcState());
            }
        }
    }

    while (!insts[tid].empty() && 
            insts[tid].back()->seqNum > squashedSeqNum) {
        insts[tid].back()->setSquashed();
        DPRINTF(RxuBpu1,
                "[tid:%i] "
                "instruction %i with PC %s is squashed.\n",
                tid, insts[tid].back()->seqNum,
                insts[tid].back()->pcState());
        insts[tid].pop_back();
    }
}

void
Bpu1::updateStatus()
{
    bool any_unblocking = false;

    list<ThreadID>::iterator threads = activeThreads->begin();
    list<ThreadID>::iterator end = activeThreads->end();

    while (threads != end) {
        ThreadID tid = *threads++;

        if (bpu1Status[tid] == Unblocking) {
            any_unblocking = true;
            break;
        }
    }

    // Bpu1 will have activity if it's unblocking.
    if (any_unblocking) {
        if (_status == Inactive) {
            _status = Active;

            DPRINTF(RxuActivity, "Activating stage.\n");

            cpu->activateStage(CPU::Bpu1Idx);
        }
    } else {
        // If it's not unblocking, then bpu1 will not have any internal
        // activity.  Switch it to inactive.
        if (_status == Active) {
            _status = Inactive;
            DPRINTF(RxuActivity, "Deactivating stage.\n");

            cpu->deactivateStage(CPU::Bpu1Idx);
        }
    }
}

void
Bpu1::sortInsts()
{
    int insts_from_Bpu0 = fromBpu0->size;
    for (int i = 0; i < insts_from_Bpu0; ++i) {
        insts[fromBpu0->insts[i]->threadNumber].push_back(fromBpu0->insts[i]);
    }
}

void
Bpu1::readStallSignals(ThreadID tid)
{
    if (fromIBandLB->IBandLBBlock[tid]) {
        stalls[tid].IBandLB = true;
    }

    if (fromIBandLB->IBandLBUnblock[tid]) {
        // assert(stalls[tid].IBandLB);
        stalls[tid].IBandLB = false;
    }
}

bool
Bpu1::checkSignalsAndUpdate(ThreadID tid)
{
    readStallSignals(tid);

    // Check squash signals from commit.
    if (fromCommit->commitInfo[tid].squash) {

        DPRINTF(RxuBpu1, "[tid:%i] Squashing instructions due to squash "
                "from commit.\n", tid);

        squash(tid, fromCommit->commitInfo[tid].doneSeqNum);

        if (fromCommit->commitInfo[tid].mispredictInst &&
            fromCommit->commitInfo[tid].mispredictInst->isControl()) {
            branchPred->squash(fromCommit->commitInfo[tid].doneSeqNum,
                    *fromCommit->commitInfo[tid].pc,
                    fromCommit->commitInfo[tid].branchTaken, tid);
        } else {
            branchPred->squash(fromCommit->commitInfo[tid].doneSeqNum,
                              tid);
        }

        return true;
    } else if (fromCommit->commitInfo[tid].doneSeqNum) {
        DPRINTF(RxuBPU, "update [sn:%i]\n", fromCommit->commitInfo[tid].doneSeqNum);
        branchPred->update(fromCommit->commitInfo[tid].doneSeqNum, tid);     
    }

    if (fromDecode->decodeInfo[tid].squash) {
        DPRINTF(RxuBpu1, "[tid:%i] Squashing instructions due to squash "
                "from decode.\n",tid);

        // Update the branch predictor.
        if (fromDecode->decodeInfo[tid].branchMispredict) {
            branchPred->squash(fromDecode->decodeInfo[tid].doneSeqNum,
                    *fromDecode->decodeInfo[tid].nextPC,
                    fromDecode->decodeInfo[tid].branchTaken, tid);
        } else {
            branchPred->squash(fromDecode->decodeInfo[tid].doneSeqNum,
                              tid);
        }

        if (bpu1Status[tid] != Squashing) {

            DPRINTF(RxuBpu1, "Squashing from decode with PC = %s\n",
                *fromDecode->decodeInfo[tid].nextPC);
            squash(tid, fromDecode->decodeInfo[tid].doneSeqNum);

            return true;
        }
    }

    if (checkStall(tid)) {
        return block(tid);
    }

    if (bpu1Status[tid] == Blocked) {
        DPRINTF(RxuBpu1, "[tid:%i] Done blocking, switching to unblocking.\n",
                tid);

        bpu1Status[tid] = Unblocking;

        unblock(tid);

        return true;
    }

    if (bpu1Status[tid] == Squashing) {
        DPRINTF(RxuBpu1, "[tid:%i] Done squashing, switching to running.\n",
                tid);

        bpu1Status[tid] = Running;

        return false;
    }

    // If we've reached this point, we have not gotten any signals that
    // cause bpu1 to change its status.  Bpu1 remains the same as before.
    return false;
}

void
Bpu1::tick()
{
    wroteToTimeBuffer = false;

    bool status_change = false;

    toIBandLBIndex = 0;

    list<ThreadID>::iterator threads = activeThreads->begin();
    list<ThreadID>::iterator end = activeThreads->end();

    if (bpu0InstsValid())
        sortInsts();

    //Check stall and squash signals.
    while (threads != end) {
        ThreadID tid = *threads++;

        DPRINTF(RxuBpu1,"Processing [tid:%i]\n",tid);
        status_change =  checkSignalsAndUpdate(tid) || status_change;

        bpu1(status_change, tid);
    }

    if (status_change) {
        updateStatus();
    }

    if (wroteToTimeBuffer) {
        DPRINTF(RxuActivity, "Activity this cycle.\n");

        cpu->activityThisCycle();
    }

    if (ucBlockStoreAddr > 0 && !ucacheLineData.empty()) {
        DPRINTF(RxuUC, "PC starts with %#x can be saved to uop cache.\n",
                ucBlockStoreAddr);
        cpu->uc.update(ucBlockStoreAddr, ucacheLineData, pc_insts);
        ucBlockStoreAddr = 0;
        ucacheLineData.clear();
        pc_insts.clear();    
    }

    if (camBlockStoreAddr > 0 && !camcacheLineData.empty()) {
        cpu->fetch.cam->update(camBlockStoreAddr, camcacheLineData, cam_pc_insts);
        camBlockStoreAddr = 0;
        camcacheLineData.clear();
        cam_pc_insts.clear();
    }  
}

void
Bpu1::bpu1(bool &status_change, ThreadID tid)
{
    if (bpu1Status[tid] == Blocked) {
        ++stats.blockedCycles;
    } else if (bpu1Status[tid] == Squashing) {
        ++stats.squashCycles;
    }

    if (bpu1Status[tid] == Running ||
        bpu1Status[tid] == Idle) {
        DPRINTF(RxuBpu1, "[tid:%i] Not blocked, so attempting to run bpu1"
                "stage.\n",tid);

        processInsts(tid);
    } else if (bpu1Status[tid] == Unblocking) {
        DPRINTF(RxuBpu1, "[tid:%i] Unblocking status.\n",tid);
        processInsts(tid);

        status_change = unblock(tid) || status_change;
    }
}

void
Bpu1::processInsts(ThreadID tid)
{
    int insts_available = insts[tid].size();

    if (insts_available == 0) {
        DPRINTF(RxuBpu1, "[tid:%i] Nothing to do, breaking out"
                " early.\n",tid);
        // Should I change the status to idle?
        ++stats.idleCycles;
        // return;
    } else if (bpu1Status[tid] == Unblocking) {
        DPRINTF(RxuBpu1, "[tid:%i] Unblocking status.\n",tid);
        ++stats.unblockCycles;
    } else if (bpu1Status[tid] == Running) {
        DPRINTF(RxuBpu1, "[tid:%i] Running status.\n",tid);
        ++stats.runCycles;
    }

    std::list<DynInstPtr>
        &insts_to_IBandLB = insts[tid];

    DPRINTF(RxuBpu1, "[tid:%i] Starting to process instructions "
            "in bpu1 stage.\n",tid);

    bool cancelTaken = false;
    bool has_taken = false;

    bool uc_save = false;

    bool cam_save = false;

    int numInst = 0;

    while (insts_available > 0 && toIBandLBIndex < bpu1Width) {
        assert(!insts_to_IBandLB.empty());

        DynInstPtr inst = std::move(insts_to_IBandLB.front());

        insts_to_IBandLB.pop_front();

        DPRINTF(RxuBpu1, "[tid:%i] Processing instruction [sn:%lli] with "
                "PC %s\n", tid, inst->seqNum, inst->pcState());

        if (inst->isSquashed()) {
            DPRINTF(RxuBpu1, "[tid:%i] Instruction %i with PC %s is "
                    "squashed, skipping.\n",
                    tid, inst->seqNum, inst->pcState());

            ++stats.squashedInsts;

            --insts_available;

            continue;
        }

        PCStateBase &this_PC = *(inst->pc);
        std::unique_ptr<PCStateBase> bpu1_TPC(this_PC.clone());
        std::unique_ptr<PCStateBase> bpu0_TPC(inst->readPredTarg0().clone());
        bool bpu0_taken = inst->readPredTaken0();
        if (inst->isReturn()) {
            set(bpu0_TPC, inst->readPredTarg());
            bpu0_taken = inst->readPredTaken();     
        }
        bool bpu1_taken = false;

        bpu1_taken |= inst->readPredTaken();

        set(bpu1_TPC, inst->readPredTarg());

        has_taken |= bpu1_taken;

        toIBandLB->insts[toIBandLBIndex] = inst;

        ++(toIBandLB->size);
        ++toIBandLBIndex;
        ++stats.bpu1edInsts;
        --insts_available;

        if (!uc_save) {
            uc_save = true;
            ucBlockStoreAddr = fromBpu0->pc;
            for (int j = 0; j < fromBpu0->static_size; j++) {
                ucacheLineData.push_back(fromBpu0->static_insts[j]);
                pc_insts.push_back(fromBpu0->pc_insts[j]);
                numInst++;
            }
        }

        if (!cam_save) {
            cam_save = true;
            camBlockStoreAddr = fromBpu0->pc;
            for (int j = 0; j < fromBpu0->static_size; j++) {
                camcacheLineData.push_back(fromBpu0->static_insts[j]);
                cam_pc_insts.push_back(fromBpu0->pc_insts[j]);
            }
        }

#if TRACING_ON
        if (debug::RxuO3PipeView) {
            inst->bpu1Tick = curTick() - inst->fetchTick;
        }
#endif

        if (inst->isControl()) {
            if (((!bpu0_taken && bpu1_taken)
                || (bpu0_taken && bpu1_taken && bpu0_TPC->instAddr() != bpu1_TPC->instAddr())) || (bpu0_taken && !bpu1_taken)) {
                }

            if ((!bpu0_taken && bpu1_taken)
                    || (bpu0_taken && bpu1_taken && bpu0_TPC->instAddr() != bpu1_TPC->instAddr())) {
                DPRINTF(RxuBpu1, "Done processing, branch misprediction detected with PC = %s by bpu1.\n", this_PC);

                DPRINTF(RxuBpu1, "bpu0_taken: (%i), bpu1_taken: (%i).\n", bpu0_taken, bpu1_taken);
                DPRINTF(RxuBpu1, "bpu0_TPC: (%s), bpu1_TPC: (%s).\n", *bpu0_TPC, *bpu1_TPC);

                DPRINTF(RxuBpu1, "Sending next PC (%s) to bpu0 and fetch.\n", *bpu1_TPC);
                squashFromBpu1(inst, inst->threadNumber);

                cpu->fetch.doSquash(inst->readPredTarg(), inst, tid);
                cpu->fetch.toBP0->size = 0;
                cpu->fetch.lookupUopCache(inst->readPredTarg(),true);
                cpu->bpu0.waitBpuSquash = false;

                set(cpu->bpu0.pc[tid],  inst->readPredTarg());
                cpu->bpu0.lookupUopCacheFlag = true;
                cpu->bpu0.lookupCamFlag = true;
                cpu->bpu0.squash(tid, inst->seqNum);

                cpu->removeInstsUntil(inst->seqNum, inst->threadNumber);
                break;
            } else if (bpu0_taken && bpu1_taken) {
                while (!insts[tid].empty()) {
                    DynInstPtr remove = insts[tid].front();
                    insts[tid].pop_front();
                    cpu->removeInst(remove->seqNum, remove->threadNumber);
                }
                break;
            } else if (bpu0_taken && !bpu1_taken) {
                DPRINTF(RxuBpu1, "Done processing, branch misprediction detected with PC = %s by bpu1.\n", this_PC);

                DPRINTF(RxuBpu1, "bpu0_taken: (%i), bpu1_taken: (%i).\n", bpu0_taken, bpu1_taken);
                DPRINTF(RxuBpu1, "bpu0_TPC: (%s), bpu1_TPC: (%s).\n", *bpu0_TPC, *bpu1_TPC);

                DPRINTF(RxuBpu1, "Sending next PC (%s) to bpu0 and fetch.\n", *bpu1_TPC);
                squashFromBpu1(inst, inst->threadNumber);

                cpu->fetch.doSquash(inst->readPredTarg(), inst, tid);
                cpu->fetch.toBP0->size = 0;
                cpu->fetch.lookupUopCache(inst->readPredTarg(),true);
                cpu->bpu0.waitBpuSquash = false;

                set(cpu->bpu0.pc[tid],  inst->readPredTarg());
                cpu->bpu0.lookupUopCacheFlag = true;
                cpu->bpu0.lookupCamFlag = true;
                cpu->bpu0.squash(tid, inst->seqNum);

                cpu->removeInstsUntil(inst->seqNum, inst->threadNumber);

                cancelTaken = true;
            }
        }
    }

    if (cancelTaken) {}
    stats.bpu1OutInsts_includeZero.sample(toIBandLBIndex);

    if (toIBandLBIndex) {
        stats.bpu1OutInsts.sample(toIBandLBIndex);
        wroteToTimeBuffer = true;
    }
}

bool
Bpu1::lookupAndUpdateNextPC(const DynInstPtr &inst, PCStateBase &next_pc)
{
    bool predict_taken;
    ThreadID tid = inst->threadNumber;
    if (!inst->isControl()) {
        inst->staticInst->advancePC(next_pc);
        inst->setPredTarg(next_pc);
        inst->setPredTaken(false);
        DPRINTF(RxuBpu1, "[tid:%i] [sn:%llu] \"%s\" not Control\n",
                tid, inst->seqNum, inst->staticInst->disassemble(inst->pcState().instAddr()));
        return false;
    }
    DPRINTF(RxuBpu1, "[tid:%i] [sn:%llu] \"%s\" is Control\n",
            tid, inst->seqNum, inst->staticInst->disassemble(inst->pcState().instAddr()));
    for (int i=0; i<32; ++i) {
        bool vis = cpu->regtable[i];
        if (vis && cpu->RegSnMap[i] > inst->seqNum) {
            vis = false; // 未来写入，禁止 SR 使用
        }
        inst->staticInst->setRegTable(i, vis);
        inst->staticInst->setDigestMap(i, vis ? cpu->digestMap[i] : 0);
    }
    predict_taken = branchPred->predict(inst->staticInst, inst->seqNum,
                                        next_pc, tid, inst->pred_weak, inst->pred_ctr, L2BTBDelay, sr_on);

    if (inst->isNonSpeculative() && inst->isReturn() && inst->isControl()) predict_taken = false;

    if (inst->pred_ctr == 3 || inst->pred_ctr == 4) {
        DPRINTF(RxuBpu1, "[tid:%i] [sn:%llu] Branch at PC %#x "
                "predicted weak\n",
                tid, inst->seqNum, inst->pcState().instAddr());
    }

    DPRINTF(RxuBpu1, "[tid:%i] [sn:%llu] Branch at PC %#x "
            "pred_ctr = %i\n",
            tid, inst->seqNum, inst->pcState().instAddr(), inst->pred_ctr);

    DPRINTF(RxuBPU, "predict_taken: [%d]\n", predict_taken);

    if (predict_taken) {
        DPRINTF(RxuBpu1, "[tid:%i] [sn:%llu] Branch at PC %#x "
                "predicted to be taken to %s by Bpu1\n",
                tid, inst->seqNum, inst->pcState().instAddr(), next_pc);
    } else {
        DPRINTF(RxuBpu1, "[tid:%i] [sn:%llu] Branch at PC %#x "
                "predicted to be not taken by Bpu1\n",
                tid, inst->seqNum, inst->pcState().instAddr());
    }

    DPRINTF(RxuBpu1, "[tid:%i] [sn:%llu] Branch at PC %#x "
            "predicted to go to %s by Bpu1\n",
            tid, inst->seqNum, inst->pcState().instAddr(), next_pc);
    inst->setPredTarg(next_pc);
    inst->setPredTaken(predict_taken);

    return predict_taken;
}

} // namespace rxuo3
} // namespace gem5
