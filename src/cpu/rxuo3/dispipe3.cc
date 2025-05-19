// -- add by hongfei.liu ---------------------------------------------
#include "cpu/rxuo3/dispipe3.hh"

#include <queue>

#include "cpu/checker/cpu.hh"
#include "cpu/rxuo3/dyn_inst.hh"
#include "cpu/rxuo3/limits.hh"
#include "cpu/timebuf.hh"
#include "debug/RxuActivity.hh"
#include "debug/Drain.hh"
#include "debug/RxuDispipe3.hh"
#include "debug/RxuLDST.hh"
#include "debug/Rxucun.hh"
#include "debug/RxuO3PipeView.hh"
#include "debug/RxuLDSTQ.hh"
#include "params/BaseRxuO3CPU.hh"

namespace gem5
{

namespace rxuo3
{

Dispipe3::Dispipe3(CPU *_cpu, const BaseRxuO3CPUParams &params)
    : cpu(_cpu),
      wtb(_cpu, this, params),
      ldstQueue(_cpu, this, params),
      dispipe2ToDispipe3Delay(params.dispipe2ToDispipe3Delay),
      ewToDispipe3Delay(params.ewToDispipe3Delay),
      commitToDispipe3Delay(params.commitToDispipe3Delay),
      dispipe3Width(params.dispipe3Width),
      numThreads(params.numThreads),
      stats(_cpu)
{

    if (dispipe3Width > MaxWidth)
    fatal("dispipe3Width (%d) is larger than compiled limit (%d),\n"
            "\tincrease MaxWidth in src/cpu/rxuo3/limits.hh\n",
            dispipe3Width, static_cast<int>(MaxWidth));

    _status = Active;

    for (ThreadID tid = 0; tid < MaxThreads; tid++) {
        dispipe3Status[tid] = Idle;
        stalls[tid] = {false};
    }
    ldstdisq.resize(20);
    for (int i = 0; i < 20; ++i)
        ldstdisq[i] = true;
    ldstdisq_in.resize(20);
    for (int i = 0; i < 6; ++i)
        ldstdisq_in[i] = false;
    ldstdisq_in[6] = true;
    ldstdisq_in[7] = true;
     for (int i = 14; i < 20; ++i)
        ldstdisq_in[i] = true;

    for (ThreadID tid = 0; tid < numThreads; tid++) {
        decoder[tid] = (RiscvISA::Decoder*)params.decoder[tid];
        decoder[tid]->use_rxuo3_cpu = true;
    }
    
}

std::string
Dispipe3::name() const
{
    return cpu->name() + ".dispipe3";
}

void
Dispipe3::regProbePoints()
{
    ppDispatch = new ProbePointArg<DynInstPtr>(
            cpu->getProbeManager(), "Dispatch");
}

Dispipe3::Dispipe3Stats::Dispipe3Stats(CPU *cpu)
    : statistics::Group(cpu, "dispipe3"),
    ADD_STAT(idleCycles, statistics::units::Cycle::get(),
             "Number of cycles dispipe3 is idle"),
    ADD_STAT(squashCycles, statistics::units::Cycle::get(),
             "Number of cycles dispipe3 is squashing"),
    ADD_STAT(blockCycles, statistics::units::Cycle::get(),
             "Number of cycles dispipe3 is blocking"),
    ADD_STAT(unblockCycles, statistics::units::Cycle::get(),
             "Number of cycles dispipe3 is unblocking"),
    ADD_STAT(runCycles, statistics::units::Cycle::get(),
             "Number of cycles dispipe3 is running"),
    ADD_STAT(dispipe3Insts, statistics::units::Count::get(),
             "Number of instructions dispipe3 send"),
    ADD_STAT(SquashedInsts, statistics::units::Count::get(),
             "Number of squashed instructions skipped by dispipe3"),
    ADD_STAT(dispLoadInsts, statistics::units::Count::get(),
             "Number of dispatched load instructions"),
    ADD_STAT(dispStoreInsts, statistics::units::Count::get(),
             "Number of dispatched store instructions"),
    ADD_STAT(dispNonSpecInsts, statistics::units::Count::get(),
             "Number of dispatched non-speculative instructions"),
    ADD_STAT(wtbFullEvents, statistics::units::Count::get(),
             "Number of times the WTB has become full, causing a stall"),

    ADD_STAT(ibuffer0FullEvents, statistics::units::Count::get(),
             "Number of times the ibuffer0 (int Normal) has become full, causing a stall"),
    ADD_STAT(ibuffer1FullEvents, statistics::units::Count::get(),
             "Number of times the ibuffer1 (int Normal) has become full, causing a stall"),
    ADD_STAT(ibuffer4FullEvents, statistics::units::Count::get(),
             "Number of times the ibuffer4 (int Normal) has become full, causing a stall"),
    ADD_STAT(ibuffer5FullEvents, statistics::units::Count::get(),
             "Number of times the ibuffer5 (int Normal) has become full, causing a stall"),

    ADD_STAT(ibuffer2FullEvents, statistics::units::Count::get(),
             "Number of times the ibuffer2 (int Special) has become full, causing a stall"),
    ADD_STAT(ibuffer3FullEvents, statistics::units::Count::get(),
             "Number of times the ibuffer3 (int Special) has become full, causing a stall"),
    ADD_STAT(ibuffer20FullEvents, statistics::units::Count::get(),
             "Number of times the ibuffer20 (branch) has become full, causing a stall"),
    ADD_STAT(ibuffer21FullEvents, statistics::units::Count::get(),
             "Number of times the ibuffer21 (branch) has become full, causing a stall"),
    ADD_STAT(ibuffer22FullEvents, statistics::units::Count::get(),
             "Number of times the ibuffer22 (branch) has become full, causing a stall"),
    ADD_STAT(ibuffer23FullEvents, statistics::units::Count::get(),
             "Number of times the ibuffer23 (branch) has become full, causing a stall"),

    ADD_STAT(ibuffer6FullEvents, statistics::units::Count::get(),
             "Number of times the ibuffer6 (Load/Store) has become full, causing a stall"),
    ADD_STAT(ibuffer7FullEvents, statistics::units::Count::get(),
             "Number of times the ibuffer7 (Load/Store) has become full, causing a stall"),
    ADD_STAT(ibuffer14FullEvents, statistics::units::Count::get(),
             "Number of times the ibuffer14 (Load/Store) has become full, causing a stall"),
    ADD_STAT(ibuffer15FullEvents, statistics::units::Count::get(),
             "Number of times the ibuffer15 (Load/Store) has become full, causing a stall"),
    ADD_STAT(ibuffer16FullEvents, statistics::units::Count::get(),
             "Number of times the ibuffer16 (Load/Store) has become full, causing a stall"),
    ADD_STAT(ibuffer17FullEvents, statistics::units::Count::get(),
             "Number of times the ibuffer17 (Load/Store) has become full, causing a stall"),
    ADD_STAT(ibuffer18FullEvents, statistics::units::Count::get(),
             "Number of times the ibuffer18 (Load/Store) has become full, causing a stall"),
    ADD_STAT(ibuffer19FullEvents, statistics::units::Count::get(),
             "Number of times the ibuffer19 (Load/Store) has become full, causing a stall"),

    ADD_STAT(ibuffer8FullEvents, statistics::units::Count::get(),
             "Number of times the ibuffer8 (float Normal) has become full, causing a stall"),
    ADD_STAT(ibuffer9FullEvents, statistics::units::Count::get(),
             "Number of times the ibuffer9 (float Normal) has become full, causing a stall"),
    ADD_STAT(ibuffer10FullEvents, statistics::units::Count::get(),
             "Number of times the ibuffer10 (float Normal) has become full, causing a stall"),
    ADD_STAT(ibuffer13FullEvents, statistics::units::Count::get(),
             "Number of times the ibuffer13 (float Normal) has become full, causing a stall"),

    ADD_STAT(ibuffer11FullEvents, statistics::units::Count::get(),
             "Number of times the ibuffer11 (float Special) has become full, causing a stall"),
    ADD_STAT(ibuffer12FullEvents, statistics::units::Count::get(),
             "Number of times the ibuffer12 (float Special) has become full, causing a stall"),

    ADD_STAT(lsqFullEvents, statistics::units::Count::get(),
             "Number of times the LSQ has become full, causing a stall"),

    ADD_STAT(ibuffer0Insts, statistics::units::Count::get(),
             "Number of instructions into ibuffer0 (int normal)"),
    ADD_STAT(ibuffer1Insts, statistics::units::Count::get(),
             "Number of instructions into ibuffer1 (int normal)"),
    ADD_STAT(ibuffer4Insts, statistics::units::Count::get(),
             "Number of instructions into ibuffer4 (int normal)"),
    ADD_STAT(ibuffer5Insts, statistics::units::Count::get(),
             "Number of instructions into ibuffer5 (int normal)"),

    ADD_STAT(ibuffer2Insts, statistics::units::Count::get(),
             "Number of instructions into ibuffer2 (int special)"),
    ADD_STAT(ibuffer3Insts, statistics::units::Count::get(),
             "Number of instructions into ibuffer3 (int special)"),
    ADD_STAT(ibuffer20Insts, statistics::units::Count::get(),
             "Number of instructions into ibuffer20 (branch)"),
    ADD_STAT(ibuffer21Insts, statistics::units::Count::get(),
             "Number of instructions into ibuffer21 (branch)"),
    ADD_STAT(ibuffer22Insts, statistics::units::Count::get(),
             "Number of instructions into ibuffer22 (branch)"),
    ADD_STAT(ibuffer23Insts, statistics::units::Count::get(),
             "Number of instructions into ibuffer23 (branch)"),

    ADD_STAT(ibuffer6Insts, statistics::units::Count::get(),
             "Number of instructions into ibuffer6 (load store)"),
    ADD_STAT(ibuffer7Insts, statistics::units::Count::get(),
             "Number of instructions into ibuffer7 (load store)"),
    ADD_STAT(ibuffer14Insts, statistics::units::Count::get(),
             "Number of instructions into ibuffer14 (load store)"),
    ADD_STAT(ibuffer15Insts, statistics::units::Count::get(),
             "Number of instructions into ibuffer15 (load store)"),
    ADD_STAT(ibuffer16Insts, statistics::units::Count::get(),
             "Number of instructions into ibuffer16 (load store)"),
    ADD_STAT(ibuffer17Insts, statistics::units::Count::get(),
             "Number of instructions into ibuffer17 (load store)"),
    ADD_STAT(ibuffer18Insts, statistics::units::Count::get(),
             "Number of instructions into ibuffer18 (load store)"),
    ADD_STAT(ibuffer19Insts, statistics::units::Count::get(),
             "Number of instructions into ibuffer19 (load store)"),

    ADD_STAT(ibuffer8Insts, statistics::units::Count::get(),
             "Number of instructions into ibuffer8 (fp normal)"),
    ADD_STAT(ibuffer9Insts, statistics::units::Count::get(),
             "Number of instructions into ibuffer9 (fp normal)"),
    ADD_STAT(ibuffer10Insts, statistics::units::Count::get(),
             "Number of instructions into ibuffer10 (fp normal)"),
    ADD_STAT(ibuffer13Insts, statistics::units::Count::get(),
             "Number of instructions into ibuffer13 (fp normal)"),
    ADD_STAT(ibuffer11Insts, statistics::units::Count::get(),
             "Number of instructions into ibuffer11 (fp special)"),
    ADD_STAT(ibuffer12Insts, statistics::units::Count::get(),
             "Number of instructions into ibuffer12 (fp special)"),
    ADD_STAT(vectorLDST_full, statistics::units::Count::get(),
             "Number of vectorLDSTQueue  full ")
{
    idleCycles.prereq(idleCycles);
    squashCycles.prereq(squashCycles);
    blockCycles.prereq(blockCycles);
    unblockCycles.prereq(unblockCycles);
    runCycles.prereq(runCycles);
    dispipe3Insts.prereq(dispipe3Insts);
    SquashedInsts.prereq(SquashedInsts);
    dispLoadInsts.prereq(dispLoadInsts);
    dispStoreInsts.prereq(dispStoreInsts);
    dispNonSpecInsts.prereq(dispNonSpecInsts);

    wtbFullEvents.prereq(wtbFullEvents);

    ibuffer0FullEvents.prereq(ibuffer0FullEvents);
    ibuffer1FullEvents.prereq(ibuffer1FullEvents);
    ibuffer4FullEvents.prereq(ibuffer4FullEvents);
    ibuffer5FullEvents.prereq(ibuffer5FullEvents);

    ibuffer2FullEvents.prereq(ibuffer2FullEvents);
    ibuffer3FullEvents.prereq(ibuffer3FullEvents);
    ibuffer20FullEvents.prereq(ibuffer20FullEvents);
    ibuffer21FullEvents.prereq(ibuffer21FullEvents);
    ibuffer22FullEvents.prereq(ibuffer22FullEvents);
    ibuffer23FullEvents.prereq(ibuffer23FullEvents);

    ibuffer6FullEvents.prereq(ibuffer6FullEvents);
    ibuffer7FullEvents.prereq(ibuffer7FullEvents);
    ibuffer14FullEvents.prereq(ibuffer14FullEvents);
    ibuffer15FullEvents.prereq(ibuffer15FullEvents);
    ibuffer16FullEvents.prereq(ibuffer16FullEvents);
    ibuffer17FullEvents.prereq(ibuffer17FullEvents);
    ibuffer18FullEvents.prereq(ibuffer18FullEvents);
    ibuffer19FullEvents.prereq(ibuffer19FullEvents);

    ibuffer8FullEvents.prereq(ibuffer8FullEvents);
    ibuffer9FullEvents.prereq(ibuffer9FullEvents);
    ibuffer10FullEvents.prereq(ibuffer10FullEvents);
    ibuffer13FullEvents.prereq(ibuffer13FullEvents);

    ibuffer11FullEvents.prereq(ibuffer11FullEvents);
    ibuffer12FullEvents.prereq(ibuffer12FullEvents);

    lsqFullEvents.prereq(lsqFullEvents);

    ibuffer0Insts.prereq(ibuffer0Insts);
    ibuffer1Insts.prereq(ibuffer1Insts);
    ibuffer4Insts.prereq(ibuffer4Insts);
    ibuffer5Insts.prereq(ibuffer5Insts);

    ibuffer2Insts.prereq(ibuffer2Insts);
    ibuffer3Insts.prereq(ibuffer3Insts);
    ibuffer20Insts.prereq(ibuffer20Insts);
    ibuffer21Insts.prereq(ibuffer21Insts);
    ibuffer22Insts.prereq(ibuffer22Insts);
    ibuffer23Insts.prereq(ibuffer23Insts);

    ibuffer6Insts.prereq(ibuffer6Insts);
    ibuffer7Insts.prereq(ibuffer7Insts);
    ibuffer14Insts.prereq(ibuffer14Insts);
    ibuffer15Insts.prereq(ibuffer15Insts);
    ibuffer16Insts.prereq(ibuffer16Insts);
    ibuffer17Insts.prereq(ibuffer17Insts);
    ibuffer18Insts.prereq(ibuffer18Insts);
    ibuffer19Insts.prereq(ibuffer19Insts);

    ibuffer8Insts.prereq(ibuffer8Insts);
    ibuffer9Insts.prereq(ibuffer9Insts);
    ibuffer10Insts.prereq(ibuffer10Insts);
    ibuffer13Insts.prereq(ibuffer13Insts);

    ibuffer11Insts.prereq(ibuffer11Insts);
    ibuffer12Insts.prereq(ibuffer12Insts);
    vectorLDST_full.prereq(vectorLDST_full);
}

void
Dispipe3::startupStage()
{

    // Initialize the checker's dcache port here
    if (cpu->checker) {
        cpu->checker->setDcachePort(&ldstQueue.getDataPort());
    }

    cpu->activateStage(CPU::Dispipe3Idx);
}

void
Dispipe3::clearStates(ThreadID tid)
{
    dispipe3Status[tid] = Idle;

    stalls[tid] = {false};
}

void
Dispipe3::setTimeBuffer(TimeBuffer<TimeStruct> *tb_ptr)
{
    timeBuffer = tb_ptr;

    // Setup wire to read information from time buffer, from commit.
    fromCommit = timeBuffer->getWire(-commitToDispipe3Delay);

    // Setup wire to read information from time buffer, from ew.
    fromEw = timeBuffer->getWire(-ewToDispipe3Delay);

    // Setup wire to write information back to previous stages.
    toDispipe2 = timeBuffer->getWire(0);
}

void
Dispipe3::setDispipe2Queue(TimeBuffer<Dispipe2Struct> *p2q_ptr)
{
    dispipe2Queue = p2q_ptr;

    // Setup wire to read information from dispipe2 queue.
    fromDispipe2 = dispipe2Queue->getWire(-dispipe2ToDispipe3Delay);
}

void
Dispipe3::setWakeQueue(TimeBuffer<WakeStruct> *wq_ptr)
{
    wakeQueue = wq_ptr;

    // Setup wire to write information to future stages.
    toEw = wakeQueue->getWire(0);
}

void
Dispipe3::setActiveThreads(std::list<ThreadID> *at_ptr)
{
    activeThreads = at_ptr;

    ldstQueue.setActiveThreads(at_ptr);
    wtb.setActiveThreads(at_ptr);
}

bool
Dispipe3::isDrained() const
{
    for (ThreadID tid = 0; tid < numThreads; tid++) {
        if (!insts[tid].empty() ||
            (dispipe3Status[tid] != Idle && dispipe3Status[tid] != Running))
            return false;
        if (!insts_final[tid].empty() ||
            (dispipe3Status[tid] != Idle && dispipe3Status[tid] != Running))
            return false;
    }

    bool drained = ldstQueue.isDrained() && wtb.isDrained();

    return drained;
}

void
Dispipe3::drainSanityCheck() const
{
    assert(isDrained());

    wtb.drainSanityCheck();
    ldstQueue.drainSanityCheck();
}

void
Dispipe3::takeOverFrom()
{
    // Reset all state.
    _status = Active;

    wtb.takeOverFrom();
    ldstQueue.takeOverFrom();

    startupStage();
    cpu->activityThisCycle();

    for (ThreadID tid = 0; tid < numThreads; tid++) {
        dispipe3Status[tid] = Running;
    }
}

void
Dispipe3::squash(ThreadID tid)
{
    DPRINTF(RxuDispipe3, "[tid:%i] Squashing all instructions.\n", tid);
    // next_group = true;
    // fifo_cnt = 0;
    //encounter_flush = true;


    // Tell the WTB to start squashing.
    wtb.squash(tid, fromCommit->commitInfo[tid].doneSeqNum);

    wtb.vSpecialBuffer->squash(fromCommit->commitInfo[tid].doneSeqNum);

    // Tell the LDSTQ to start squashing.
    ldstQueue.squash(fromCommit->commitInfo[tid].doneSeqNum, tid);

    DPRINTF(RxuDispipe3,
            "Removing insts instructions until "
            "[sn:%llu] [tid:%i]\n",
            fromCommit->commitInfo[tid].doneSeqNum, tid);

    DynInstPtr inst;

    int inst_in_num = insts_final[tid].size();
    for (int i = 0; i < inst_in_num; i++) {

        inst = insts_final[tid].front();
        insts_final[tid].pop_front();
        if (inst->seqNum > fromCommit->commitInfo[tid].doneSeqNum) {
            // if(inst->isMemRef()){
            //     if(cpu->dispipe3.ldstdisq_in[inst->ibuffer_id]){
            //         cpu->dispipe3.ldstdisq_in[inst->ibuffer_id] = false;
            //         cpu->dispipe3.fifo_cnt++;
            //     }
            // }

            if (inst->isSquashed()) {
                ++stats.SquashedInsts;
                DPRINTF(RxuDispipe3,
                        "[tid:%i] "
                        "instruction %i with PC %s has already been squashed before.\n",
                        tid, inst->seqNum, inst->pcState());
            } else {
                inst->setSquashed();
                rmu->dependGraph.clrrem_base(inst);
                // rmu->dependGraph.clear_base(inst);
                ++stats.SquashedInsts;
                DPRINTF(RxuDispipe3,
                        "[tid:%i] "
                        "instruction %i with PC %s is squashed.\n",
                        tid, inst->seqNum, inst->pcState());
            }
            if((inst->isMemRef() || inst->isReadBarrier() || inst->isWriteBarrier())){
                insts_final[tid].push_back(inst);
            }
        } else {
            insts_final[tid].push_back(inst);
        }
    }

    int instNums;
    instNums = insts[tid].size();

    for (int i = 0; i < instNums; i++) {

        inst = insts[tid].front();
        insts[tid].pop_front();
        if (inst->seqNum > fromCommit->commitInfo[tid].doneSeqNum) {
        //    if(inst->isMemRef()){
        //         if(cpu->dispipe3.ldstdisq_in[inst->ibuffer_id]){
        //             cpu->dispipe3.ldstdisq_in[inst->ibuffer_id] = false;
        //             cpu->dispipe3.fifo_cnt++;
        //         }
        //     }
            if (inst->isSquashed()) {
                ++stats.SquashedInsts;
                DPRINTF(RxuDispipe3,
                        "[tid:%i] "
                        "instruction %i with PC %s has already been squashed before.\n",
                        tid, inst->seqNum, inst->pcState());
            } else {
                inst->setSquashed();
                rmu->dependGraph.clrrem_base(inst);
                // rmu->dependGraph.clear_base(inst);
                ++stats.SquashedInsts;
                DPRINTF(RxuDispipe3,
                        "[tid:%i] "
                        "instruction %i with PC %s is squashed.\n",
                        tid, inst->seqNum, inst->pcState());
            }
            if((inst->isMemRef() || inst->isReadBarrier() || inst->isWriteBarrier())){
                insts[tid].push_back(inst);
            }
        } else {
            insts[tid].push_back(inst);
        }
    }


    for (auto it = vector_ldstQueue.begin(); it != vector_ldstQueue.end(); ) {
        bool toErase = false; 
        
        if((*it).first > fromCommit->commitInfo[tid].doneSeqNum){
            toErase = true;
        }
        if (toErase) {
            it = vector_ldstQueue.erase(it); 
        } else {
            ++it; 
        }
    }

}

void
Dispipe3::block(ThreadID tid)
{
    DPRINTF(RxuDispipe3, "[tid:%i] Blocking.\n", tid);

    if (dispipe3Status[tid] != Blocked &&
        dispipe3Status[tid] != Unblocking) {
        toDispipe2->dispipe3Block[tid] = true;
        wroteToTimeBuffer = true;
    }

    dispipe3Status[tid] = Blocked;
}

void
Dispipe3::unblock(ThreadID tid)
{
    DPRINTF(RxuDispipe3, "[tid:%i] Trying to unblock.\n", tid);

    if (insts[tid].empty() && insts_final[tid].empty()) {

        toDispipe2->dispipe3Unblock[tid] = true;

        wroteToTimeBuffer = true;

        DPRINTF(RxuDispipe3, "[tid:%i] Done unblocking.\n",tid);

        dispipe3Status[tid] = Running;
    }
}

void
Dispipe3::rescheduleMemInst(const DynInstPtr& inst)
{
    wtb.rescheduleMemInst(inst);
}

void
Dispipe3::replayMemInst(const DynInstPtr& inst)
{
    wtb.replayMemInst(inst);
}

void
Dispipe3::blockMemInst(const DynInstPtr& inst)
{
    wtb.blockMemInst(inst);
}

void
Dispipe3::cacheUnblocked()
{
    wtb.cacheUnblocked();
}

void
Dispipe3::updateStatus()
{
    bool any_unblocking = false;

    std::list<ThreadID>::iterator threads = activeThreads->begin();
    std::list<ThreadID>::iterator end = activeThreads->end();

    while (threads != end) {
        ThreadID tid = *threads++;

        if (dispipe3Status[tid] == Unblocking) {
            any_unblocking = true;
            break;
        }
    }

    if (_status == Active && !wtb.hasReadyInsts() && !any_unblocking) {
        DPRINTF(RxuDispipe3, "Dispipe3 switching to idle\n");

        //deactivateStage();
        activateStage();

        _status = Inactive;
    } else if (_status == Inactive && (wtb.hasReadyInsts() ||
                                       any_unblocking)) {
        DPRINTF(RxuDispipe3, "Dispipe3 switching to active\n");

        activateStage();

        _status = Active;
    }
}

bool
Dispipe3::checkStall(ThreadID tid)
{
    bool ret_val(false);

    if (stalls[tid].ew) {
        DPRINTF(RxuDispipe3,"[tid:%i] Stall from EW stage detected.\n", tid);
        ret_val = true;
    }

    return ret_val;
}

void
Dispipe3::checkSignalsAndUpdate(ThreadID tid)
{
    if (fromCommit->commitInfo[tid].squash) {
        squash(tid);

        if (dispipe3Status[tid] == Blocked ||
            dispipe3Status[tid] == Unblocking) {
            if (!wtb.isFull(tid)) {
                toDispipe2->dispipe3Unblock[tid] = true;
                wroteToTimeBuffer = true;
            }
        }

        dispipe3Status[tid] = Squashing;
        return;
    }

    if (checkStall(tid)) {

        block(tid);
        dispipe3Status[tid] = Blocked;
        return;

    }

    if (dispipe3Status[tid] == Blocked) {
        // Status from previous cycle was blocked, but there are no more stall
        // conditions.  Switch over to unblocking.
        DPRINTF(RxuDispipe3, "[tid:%i] Done blocking, switching to unblocking.\n",
                tid);

        dispipe3Status[tid] = Unblocking;

        unblock(tid);

        return;
    }

    if (dispipe3Status[tid] == Squashing) {

        DPRINTF(RxuDispipe3, "[tid:%i] Done squashing, switching to running.\n",
                tid);

        dispipe3Status[tid] = Running;

        return;
    }
}

void
Dispipe3::sortInsts()
{
    int insts_from_dispipe2 = fromDispipe2->size;
    for (int i = 0; i < insts_from_dispipe2; ++i) {
        DynInstPtr inst = fromDispipe2->insts[i];
        if (inst->isMicroVector() && inst->isMemRef()) {
            if (inst->isSquashed()) continue;
            vector_ldstQueue[inst->ori_inst->seqNum].push_back(inst);
            DPRINTF(RxuDispipe3,
            "[sn:%i] had add to vec_ldstQ,size = %i,uopNum = %i\n", 
            inst->seqNum,vector_ldstQueue[inst->ori_inst->seqNum].size(),inst->ori_inst->uopNumber);
            if ((vector_ldstQueue[inst->ori_inst->seqNum].size() == inst->ori_inst->uopNumber) && !inst->needEop()) {
                inst->ori_inst->allUopInVector = true;
            }
        } else {
            insts[fromDispipe2->insts[i]->threadNumber].push_back(inst);
        }
    }
}

void
Dispipe3::wakeCPU()
{
    cpu->wakeCPU();
}

void
Dispipe3::activityThisCycle()
{
    DPRINTF(RxuActivity, "Activity this cycle.\n");
    cpu->activityThisCycle();
}

void
Dispipe3::activateStage()
{
    DPRINTF(RxuActivity, "Activating stage.\n");
    cpu->activateStage(CPU::Dispipe3Idx);
}

void
Dispipe3::deactivateStage()
{
    DPRINTF(RxuActivity, "Deactivating stage.\n");
    cpu->deactivateStage(CPU::Dispipe3Idx);
}

void
Dispipe3::dispipe3(ThreadID tid)
{
    DPRINTF(Rxucun,"readySTdata0:%i,readySTdata1:%i,readyLDSTodd0:%i,readyLDSTeven0:%i,stq:%i,ldq:%i,ldstdep:%i,requestVector:%i\n",
            wtb.readyLDSTeven0.size(),wtb.readyLDSTodd0.size(),wtb.readySTdata0.size(),wtb.readySTdata1.size(),
    wtb.memDepUnit->stq.size(),wtb.memDepUnit->ldq.size(),wtb.memDepUnit->ldstdep.size(),ldstQueue.requestVector.size());
    wtb.need_readR_again = true;
    if (dispipe3Status[tid] == Blocked) {
        ++stats.blockCycles;
        sendInsts(tid);
        handle_inst_noRport(tid);
        return;
    } else if (dispipe3Status[tid] == Squashing) {
        ++stats.squashCycles;
        return;
    }
    if (dispipe3Status[tid] == Running ||
        dispipe3Status[tid] == Idle) {
        if (dispipe3Status[tid] == Running) {
            ++stats.runCycles;
        } else {
            ++stats.idleCycles;
        }
        DPRINTF(RxuDispipe3,
                "[tid:%i] Starting to send instructions to ew"
                " in running/idle status.\n", tid);
        sendInsts(tid);
    } else if (dispipe3Status[tid] == Unblocking) {
        DPRINTF(RxuDispipe3,
                "[tid:%i] Starting to send instructions to ew"
                " in unblocking status.\n", tid);
        ++stats.unblockCycles;
        sendInsts(tid);
        unblock(tid);
    }

    DPRINTF(RxuDispipe3,
            "[tid:%i] Inserting instructions into WTB.\n", tid);
    insertInsts(tid);
}

void
Dispipe3::sendInsts(ThreadID tid)
{
    wtb.scheduleReadyInsts();
    DynInstPtr v_special_inst = wtb.vSpecialBuffer->selectInst();
    if (v_special_inst) {
        toEw->insts[toEw->size] = v_special_inst;
        toEw->size++;
        DPRINTF(RxuDispipe3,
                "[tid:%i] Sending special Macro instruction [sn:%i] to ew.\n",
                tid, v_special_inst->seqNum);
    }
}

void
Dispipe3::handle_inst_noRport(ThreadID tid)
{
    wtb.handle_inst_noRport();
}
bool
Dispipe3::canSplit(const InstQueue& uopqueue){
    int cnt = 0;
    for (size_t i = 0; i < uopqueue.size(); ++i){
        PhysRegIdPtr src_reg1 = uopqueue[i]->renamedSrcIdx(1);
        PhysRegIdPtr src_reg2 = uopqueue[i]->renamedSrcIdx(2);
        if(!uopqueue[i]->isEop() && (
           uopqueue[i]->isVluxeix() && cpu->rmu.regScoreboard[src_reg1->flatIndex()]
        || uopqueue[i]->isVsuxeix() && cpu->rmu.regScoreboard[src_reg2->flatIndex()] && cpu->rmu.regScoreboard[src_reg1->flatIndex()]
        || uopqueue[i]->isVlsSeg()
        || uopqueue[i]->isVlseg()
        || uopqueue[i]->isVluxSeg() && cpu->rmu.regScoreboard[src_reg1->flatIndex()]
        || uopqueue[i]->isVlse()
        || uopqueue[i]->isVsse() && cpu->rmu.regScoreboard[src_reg2->flatIndex()]
        || uopqueue[i]->isVssseg() && cpu->rmu.regScoreboard[src_reg2->flatIndex()]
        || uopqueue[i]->isVsuxseg() && cpu->rmu.regScoreboard[src_reg2->flatIndex()] && cpu->rmu.regScoreboard[src_reg1->flatIndex()]))
        {
            DPRINTF(RxuDispipe3,
                "CanSplit: mac_sn: %lli, uop = %lli\n", uopqueue[i]->ori_inst->seqNum, uopqueue[i]->seqNum);
            cnt++;
        }
    }
    if (uopqueue[0]->ori_inst->isSquashed()) 
        {
            DPRINTF(RxuDispipe3,
                "CanSplit: mac_sn: %lli can't split becasue mac had been squash\n", uopqueue[0]->ori_inst->seqNum);
            return false;
        }
    if((cnt == uopqueue[0]->ori_inst->actualUopNumber) && (cnt != 0)){
        DPRINTF(RxuDispipe3,
            "CanSplit: mac_sn: %lli can split\n", uopqueue[0]->ori_inst->seqNum);
        return true;
    }
    else{
        DPRINTF(RxuDispipe3,
            "CanSplit: mac_sn: %lli can't split becasue uops have not all ready or needn't to split\n", uopqueue[0]->ori_inst->seqNum);
        return false;
    }
}
std::vector<DynInstPtr>
Dispipe3::eopReplace(DynInstPtr& inst)
{
    auto *dec_ptr = decoder[0];
    inst->pc->uReset();
    unsigned eop_num = 1;
    uint32_t             VdRegIdx  = inst->staticInst->getVdRegIdx();
    std::deque<uint32_t> VdElmIdx  = inst->staticInst->getVdElmIdx();
    uint32_t             Vs2RegIdx = inst->staticInst->getVs2RegIdx();
    std::deque<uint32_t> Vs2ElmIdx = inst->staticInst->getVs2ElmIdx();
    uint32_t             Vs3RegIdx = inst->staticInst->getVs3RegIdx();
    std::deque<uint32_t> Vs3ElmIdx = inst->staticInst->getVs3ElmIdx();
    std::deque<uint32_t> MicroIdx  = inst->staticInst->getMicroIdx();
    uint32_t             RegIdx    = inst->staticInst->getRegIdx();
    uint32_t             MicroVl   = inst->staticInst->getMicroVl();
    uint32_t             MicroIdx_uop = inst->staticInst->getMicroIdx_uop();
    std::deque<uint32_t> ElemIdx   = inst->staticInst->getElemIdx();
    uint32_t             Field     = inst->staticInst->getField();
    uint32_t             numMicroops = inst->staticInst->getNumMicroops();
    uint32_t             numFields = inst->staticInst->getNumFields();
    std::deque<uint32_t> Seg_regidx= inst->staticInst->getSeg_regidx();
    std::deque<uint32_t> Seg_elemidx= inst->staticInst->getSeg_elemidx();

    dec_ptr->setVdRegIdx(VdRegIdx);
    dec_ptr->setVdElemIdx(VdElmIdx);
    dec_ptr->setVs2RegIdx(Vs2RegIdx);
    dec_ptr->setVs2ElemIdx(Vs2ElmIdx);
    dec_ptr->setVs3RegIdx(Vs3RegIdx);
    dec_ptr->setVs3ElemIdx(Vs3ElmIdx);
    dec_ptr->setMicroIdx(MicroIdx);
    dec_ptr->setRegIdx(RegIdx);
    dec_ptr->setMicroVl(MicroVl);
    dec_ptr->setSeqNum(inst->seqNum);
    dec_ptr->setMicroIdx_uop(MicroIdx_uop);
    dec_ptr->setElemIdx(ElemIdx);
    dec_ptr->setField(Field);
    dec_ptr->setNumMicroops(numMicroops);
    dec_ptr->setNumFields(numFields);
    dec_ptr->setSegRegIdx(Seg_regidx);
    dec_ptr->setSegElemIdx(Seg_elemidx);
    inst->staticInst->machInst.uop = 1;

    uop = dec_ptr->decodeVector(inst->staticInst->machInst, inst->pc->instAddr());
    std::unique_ptr<PCStateBase> next_PC;
    set(next_PC, *inst->pc);
    next_PC->set(inst->pcState().instAddr() + 4);
    std::vector<DynInstPtr> EopBuffer;
    DPRINTF(RxuDispipe3,
    "[sn:%i] Begin split eop.\n", inst->seqNum);
        while (1) {
        eop = uop->fetchMicroop(inst->pc->microPC());
        DynInstPtr instruction = cpu->dispipe0.buildInst(
            0, eop, uop, *inst->pc, *inst->pc, true, inst->seqNum + eop_num);
        instruction->setOriInst(inst);
        int8_t total_src_regs = inst->numSrcRegs();
        for (int src_reg_idx = 0;src_reg_idx < total_src_regs;src_reg_idx++){
            cpu->rename.microInheritMacroRename(instruction,src_reg_idx);
        }
        if(instruction->isOrderInst()){

        }
        instruction->ibuffer_id = inst->ibuffer_id;
        instruction->setPredTarg(*next_PC);
        instruction->setPredTaken(false);
        instruction->setPredTarg0(*next_PC);
        instruction->setPredTaken0(false);
        eop_num++;

        inst->ori_inst->uopNumber_notWtb++;
        inst->ori_inst->EopNotIssue++;
        DPRINTF(RxuDispipe3,
        "Uop[sn:%i]: Eop [sn:%i] had been split,uopNumber = %i,uopNumber_notWtb = %i,eopNumber_notissue = %i .\n", 
        inst->seqNum,instruction->seqNum,inst->ori_inst->uopNumber,inst->ori_inst->uopNumber_notWtb,inst->ori_inst->EopNotIssue);
        EopBuffer.push_back(instruction);

        // instruction->setInstListIt(cpu->addMicroInst(instruction));

        if (eop->isLastMicroop()) {
            (*inst->pc).as<RiscvISA::PCState>().uEndEop();
            break;
        } else {
            (*inst->pc).as<RiscvISA::PCState>().uAdvance();
        }
    }
    inst->hadSplitEop = true;
    DPRINTF(RxuDispipe3,
    "[sn:%i] End split eop.\n", inst->seqNum);
    return EopBuffer;
}
void
Dispipe3::checkAndSplitEop()
{
    for (auto it = vector_ldstQueue.begin(); it != vector_ldstQueue.end(); ++it) {
        int sn = it->first;
        InstQueue& uopqueue = it->second;
        DPRINTF(RxuDispipe3,
            "mac_sn: %lli, uopqueue'size = %i\n", sn, uopqueue.size());
        if (canSplit(uopqueue)) {
            assert(uopqueue.size() != 0);
            for (size_t i = 0; i < uopqueue.size(); ++i) {
                DPRINTF(RxuDispipe3,
                    "mac_sn: %lli ,uop_sn: %lli has ready to split\n", sn, uopqueue[i]->seqNum);
                    InstQueue eops = eopReplace(uopqueue[i]);
                    uopqueue[i]->ori_inst->allEopInVector = true;
                    uopqueue[i]->ori_inst->uopNumber_notWtb--;
                    if(uopqueue[i]->isVIndexORStrideLoad()){
                        cpu->mergeBuffer.addEntry(uopqueue[i]->seqNum, uopqueue[i].get(), eops.size());
                    }
                    if(uopqueue[i]->isVIndexORStrideStore()){
                        uopqueue[i]->EopNotExe_Cnt = eops.size();
                        // cpu->commit.UopList.push_back(uopqueue[i]);
                    }
                    uopqueue.erase(uopqueue.begin() + i);
                    uopqueue.insert(uopqueue.begin() + i, eops.begin(), eops.end());
                    i += eops.size() - 1;
            }
        }
        // else if(!canSplit(uopqueue)){
        //     for (size_t i = 0; i < uopqueue.size(); ++i){
        //         if(uopqueue[i]->needEop()){
        //             uopqueue[i]->waitSplitEop = true;
        //         }
        //     }
        // }
    }
}
bool
Dispipe3::insertLdStUop(DynInstPtr inst){
    bool stall = false;
    stall = ((128-ldstQueue.getCountstore(0)) <= 8) || ((204-ldstQueue.getCountload(0)) <= 8);
    if(stall){
        DPRINTF(RxuDispipe3, "fail to add PC %s [sn:%lli] [tid:%i] to "
            "WTB, ibuffer_id = %i.\n",inst->pcState(), inst->seqNum, 
            inst->threadNumber, inst->ibuffer_id);
        return false;
    }

    DPRINTF(RxuDispipe3, "Try to add PC %s [sn:%lli] [tid:%i] to "
                "WTB, ibuffer_id = %i.\n",inst->pcState(), inst->seqNum, 
                inst->threadNumber, inst->ibuffer_id);

    inst->tryInWtb = true;
    bool add_to_wtb = false;

    if(!inst->isSquashed() && !inst->inst_had_insertq){
        if(inst->isAtomic() || inst->isStore()){
            if(inst->isEop()){
                DPRINTF(RxuDispipe3, "add st eop to memdeps PC %s [sn:%lli].\n",
                    inst->pcState(), inst->seqNum);
                wtb.memDepUnit->stq.emplace(inst->seqNum,inst);  
                wtb.memDepUnit->memhashadd(inst);
                if(inst->isOrderInst()){
                    ldstOrderMap[inst->ori_inst->ori_inst->seqNum].push(inst);
                }
            }
            DPRINTF(RxuLDSTQ, "insert store/atomic inst into storequeue seqnum[sn:%i]\n",inst->seqNum);
            ldstQueue.insertStore(inst);
        }
        if(inst->isLoad()){
            if(inst->isEop()){
                DPRINTF(RxuDispipe3, "add ld eop to memdeps PC %s [sn:%lli].\n",
                    inst->pcState(), inst->seqNum);
                wtb.memDepUnit->memhashadd(inst);
                if(inst->isOrderInst()){
                    ldstOrderMap[inst->ori_inst->ori_inst->seqNum].push(inst);
                }
            }
            DPRINTF(RxuLDSTQ, "insert load inst into loadqueue seqnum from insertLdStUop[sn:%i]\n",inst->seqNum);
            ldstQueue.insertLoad(inst);
        }
        inst->inst_had_insertq = true;
    }

    bool isStore = inst->isStore() || inst->isAtomic();
    bool memRef = inst->isMemRef();

        // Check for full conditions.
    if (wtb.isFull(0, inst->ibuffer_id,isStore,memRef)) {
        DPRINTF(RxuDispipe3, "ibuffer[%i] is full.\n", inst->ibuffer_id);
        if(inst->ibuffer_id == 22 || inst->ibuffer_id == 23) {
            ldst_wtb_full = true;
        }
        // block(0);
        // toDispipe2->dispipe3Unblock[0] = false;
        return false;
    }

    //         // hardware transactional memory
    //     // CPU needs to track transactional state in program order.
    // const int numHtmStarts = ldstQueue.numHtmStarts(0);
    // const int numHtmStops = ldstQueue.numHtmStops(0);
    // const int htmDepth = numHtmStarts - numHtmStops;

    // if (htmDepth > 0) {
    //     inst->setHtmTransactionalState(ldstQueue.getLatestHtmUid(0),
    //                                     htmDepth);
    // } else {
    //     inst->clearHtmTransactionalState();
    // }


        // Otherwise insert the instruction just fine.
    if (inst->isAtomic()) {
        DPRINTF(RxuDispipe3, "WTB insert: Memory instruction "
                "encountered, adding to LSQ.\n");

        // AMOs need to be set as "canCommit()"
        // so that commit can process them when they reach the
        // head of commit.
        if(!inst->isSquashed()){
            inst->setCanCommit();
            wtb.insertNonSpec(inst);
        }
        DPRINTF(RxuLDST, "atomic ibufferid:%i  insert in wtb this group,seqnum[sn:%i]\n",inst->ibuffer_id,inst->seqNum);
        ++stats.dispNonSpecInsts;
    } else if (inst->isLoad()) {
        DPRINTF(RxuDispipe3, "WTB insert: Memory instruction "
                "encountered, adding to LSQ.\n");
        add_to_wtb = true;
    } else if (inst->isStore()) {
        DPRINTF(RxuDispipe3, " WTB insert: Memory instruction "
            "encountered, adding to LSQ.\n");
            
        add_to_wtb = true;

        if (inst->isStoreConditional()) {
            // Store conditionals need to be set as "canCommit()"
            // so that commit can process them when they reach the
            // head of commit.
            // @todo: This is somewhat specific to Alpha.
            inst->setCanCommit();
            wtb.insertNonSpec(inst);
            add_to_wtb = false;

            ++stats.dispNonSpecInsts;
        } else {
            add_to_wtb = true;
        }
    } else if (inst->isReadBarrier() || inst->isWriteBarrier()) {
            // Same as non-speculative stores.
        add_to_wtb = false;
        if(!inst->isSquashed()){
            inst->setCanCommit();
            wtb.insertBarrier(inst);
        }
        DPRINTF(RxuLDST, "atomic  ibufferid:%i  insert in wtb this group.,seqnum[sn:%i]\n",inst->ibuffer_id,inst->seqNum);

    } else {
        assert(!inst->isExecuted());
        add_to_wtb = true;
    }

    if (add_to_wtb && inst->isNonSpeculative()) {
        DPRINTF(RxuDispipe3, "WTB insert: Nonspeculative instruction "
                "encountered, skipping.\n");

        // Same as non-speculative stores.
        inst->setCanCommit();

        // Specifically insert it as nonspeculative.
        wtb.insertNonSpec(inst);

        ++stats.dispNonSpecInsts;

        add_to_wtb = false;
    }

    if (add_to_wtb) {
        wtb.insert(inst);
        switch (inst->ibuffer_id)
        {
            case 22:
                stats.ibuffer22Insts++;
                break;
            case 23:
                stats.ibuffer23Insts++;
                break;
            default:
                break;
        }
    }

    ppDispatch->notify(inst);

    return true;

    // if (wtb.isFull(0)) {
    //     DPRINTF(RxuDispipe3,"[tid:%i] some ibuffers are Full.\n", tid);
    //     block(tid);
    //     toDispipe2->dispipe3Unblock[tid] = false;
    //     ++stats.wtbFullEvents;
    // }
}




void
Dispipe3::insertInsts(ThreadID tid)
{
    handle_inst_noRport(tid);
    std::deque<DynInstPtr> &insts_from_dispipe2 = insts[tid];
    std::deque<DynInstPtr> &insts_from_dispipe2_in = insts_final[tid];

    ldst_wtb_full = false;
    intnomal_wtb_full =false;
    intspecial_wtb_full =false;
    fp_wtb_full =false;
    vector_wtb_full =false;

    ldst_vector_ready = true;

    if(next_group){
        for (int i = 0; i < 6; ++i){
            ldstdisq_in[i] = false;
        }
        ldstdisq_in[6] = true;
        ldstdisq_in[7] = true;
        for (int i = 14; i < 20; ++i){
            ldstdisq_in[i] = true;
        }
        next_group = false;
    }
    DPRINTF(RxuLDST, "ldstdisq_in[6]:%i,ldstdisq_in[7]]:%i,ldstdisq_in[14]:%i,ldstdisq_in[15]:%i,"
    "ldstdisq_in[16]:%i,ldstdisq_in[17]:%i,ldstdisq_in[18]:%i,ldstdisq_in[19]:%i\n",
            ldstdisq_in[6],ldstdisq_in[7],ldstdisq_in[14],ldstdisq_in[15],ldstdisq_in[16],
            ldstdisq_in[17],ldstdisq_in[18],ldstdisq_in[19]);
    DynInstPtr inst;
    DynInstPtr inst_uop;
    int insts_available = insts_from_dispipe2.size();
    for (int i = 0; i < insts_available; i++){
        inst = insts_from_dispipe2.front();
        insts_from_dispipe2.pop_front();

        if((inst->isMemRef() || inst->isReadBarrier() || inst->isWriteBarrier())&& ldstdisq_in[inst->ibuffer_id]){
            ldstdisq_in[inst->ibuffer_id] = false;
            insts_from_dispipe2_in.push_back(inst);
        }
        else if((inst->isMemRef() || inst->isReadBarrier() || inst->isWriteBarrier()) && !ldstdisq_in[inst->ibuffer_id]){
            insts_from_dispipe2.push_back(inst);
        }
        else {
            insts_from_dispipe2_in.push_back(inst);
        }

    }

    int insts_available_in = insts_from_dispipe2_in.size();
    int processed_insts = 0;
    bool unable_odd = false ;
    bool unable_even = false ;
    int num_ldst_towtb = 0 ;
    bool ldst_fifo_full = false;
    odd_cnt = 0;
    even_cnt = 0;
    all_cnt = 0;
    int deep_eo = 8;
    // Loop through the instructions, putting them in the wtb.
    for (int i = 0; i < insts_available_in; i++)
    {
        bool add_to_wtb = false;
        inst = insts_from_dispipe2_in.front();
        insts_from_dispipe2_in.pop_front();
        if(inst->isVector() && inst->isMacroVectorMemRef()){
            if(inst->isSquashed()){
                if(ldstdisq[inst->ibuffer_id]){
                    fifo_cnt++;
                    ldstdisq[inst->ibuffer_id] = false;
                }
                DPRINTF(RxuLDST, "vector memref inst ibuffer_id :%i, [sn:%llu] isquashed \n",inst->ibuffer_id,inst->seqNum);
                if(fifo_cnt == 8){
                    DPRINTF(RxuLDST, "ldst fifo=8 can't insert ldst inst in this circle.,fifo_cnt:%i\n",fifo_cnt);
                    fifo_cnt = 0;
                    unable_odd = false;
                    unable_even = false;
                    num_even_str = 0;
                    num_odd_str = 0;
                    ldst_fifo_full = true;
                    next_group = true;
                    for(int i = 0 ;i <20 ;i++){
                        ldstdisq[i] = true;
                    }
                }
                continue;
            }
            else if(!ldst_vector_ready){
                insts_from_dispipe2_in.push_back(inst);
                continue;
            }
            else if (inst->vnopReady) {
                fifo_cnt++;
                ldstdisq[inst->ibuffer_id] = false;
                DPRINTF(RxuLDST, "vector memref inst[sn:%llu] ibuffer_id:%i all uop into wtb\n",inst->seqNum,inst->ibuffer_id);
                if(fifo_cnt == 8){
                    DPRINTF(RxuLDST, "ldst fifo=8 can't insert ldst inst in this circle.,fifo_cnt:%i\n",fifo_cnt);
                    fifo_cnt = 0;
                    unable_odd = false;
                    unable_even = false;
                    num_even_str = 0;
                    num_odd_str = 0;
                    ldst_fifo_full = true;
                    next_group = true;
                    for(int i = 0 ;i <20 ;i++){
                        ldstdisq[i] = true;
                    }
                }
                if (!inst->isInRemove()) {
                    cpu->removeInstsThisCycle = true;
                    cpu->removeList.push(inst->getInstListIt());
                    inst->setInRemove();
                    inst->setSquashed();
                    inst->macroMemRefIsErased = false;
                }
                continue;
            }
            else if(inst->allUopInVector || inst->allEopInVector){
                int num_usable = deep_eo - all_cnt;
                if(num_usable == 0){
                    insts_from_dispipe2_in.push_back(inst);
                    continue;
                }
                int residue_uop = (inst->uopNumber_notWtb >= num_usable) ? (inst->uopNumber_notWtb-num_usable) : 0;
                for (int uop_number = 0; uop_number < num_usable; uop_number++)
                {
                    if(inst->isMemRef() && (ldst_wtb_full || !ldst_vector_ready)){
                        insts_from_dispipe2_in.push_back(inst);
                        break;
                    }
                     for (auto mapIt = vector_ldstQueue.begin(); mapIt != vector_ldstQueue.end(); ++mapIt) {
                        int key = mapIt->first;
                        
                    }
                    // if(inst->pcState().as<gem5::RiscvISA::PCState>().vl() == 0){}
                    if(vector_ldstQueue[inst->seqNum].empty()){
                        inst->setSquashed();
                        insts_from_dispipe2_in.push_back(inst);
                        break;
                    }
                    inst_uop = vector_ldstQueue[inst->seqNum].front();
                    bool addSucc = insertLdStUop(inst_uop);
                    if(addSucc){
                        all_cnt++;
                        inst->uopNumber_notWtb--;
                        DPRINTF(RxuDispipe3,
                        "[sn:%i] had been insert to ldstdisQ,uopNumber_notWtb = %i.\n", 
                        inst_uop->seqNum,inst->uopNumber_notWtb);
                        vector_ldstQueue[inst->seqNum].erase(vector_ldstQueue[inst->seqNum].begin());
                    }
                    else {
                        insts_from_dispipe2_in.push_back(inst);
                        break;
                    }

                    if(inst->uopNumber_notWtb == 0){
                        vector_ldstQueue.erase(inst->seqNum);
                        if (!inst->isInRemove()) {
                            cpu->removeInstsThisCycle = true;
                            cpu->removeList.push(inst->getInstListIt());
                            inst->setInRemove();
                            inst->setSquashed();
                            inst->macroMemRefIsErased = false;
                        }
                        fifo_cnt++;
                        ldstdisq[inst->ibuffer_id] = false;
                        DPRINTF(RxuLDST, "vector memref inst[sn:%llu] ibuffer_id:%i all uop into wtb\n",inst->seqNum,inst->ibuffer_id);
                        if(fifo_cnt == 8){
                            DPRINTF(RxuLDST, "ldst fifo=8 can't insert ldst inst in this circle.,fifo_cnt:%i\n",fifo_cnt);
                            fifo_cnt = 0;
                            unable_odd = false;
                            unable_even = false;
                            num_even_str = 0;
                            num_odd_str = 0;
                            ldst_fifo_full = true;
                            next_group = true;
                            for(int i = 0 ;i <20 ;i++){
                                ldstdisq[i] = true;
                            }
                        }
                        break;
                    }

                    if((uop_number == num_usable-1) && inst->uopNumber_notWtb != 0){
                        insts_from_dispipe2_in.push_back(inst);
                        DPRINTF(RxuDispipe3, "Have uop not in wtb [sn:%llu]\n", inst->seqNum);
                        // block(tid);
                        // toDispipe2->dispipe3Unblock[tid] = false;
                        ldst_vector_ready = false;
                        break;
                    }
                } 
                continue;  
            }
            else {
                insts_from_dispipe2_in.push_back(inst);
                DPRINTF(RxuDispipe3, "mac vector inst is not ready [sn:%llu]\n", inst->seqNum);
                // block(tid);
                // toDispipe2->dispipe3Unblock[tid] = false;
                ldst_vector_ready = false;
                continue;
            }
        }

        DPRINTF(RxuDispipe3, "[tid:%i] Try to add PC %s [sn:%lli] [tid:%i] to "
                "WTB, ibuffer_id = %i.\n",
                tid, inst->pcState(), inst->seqNum, inst->threadNumber, inst->ibuffer_id);
        inst->tryInWtb = true;

        if (inst->isSquashed() && !(inst->isMemRef() || inst->isReadBarrier() || inst->isWriteBarrier())) {
            DPRINTF(RxuDispipe3, "[tid:%i] Squashed instruction encountered, "
                    "not adding to WTB.\n", tid);

            ++stats.SquashedInsts;

            continue;
        }
        if(inst->isMemRef() && (ldst_wtb_full || (!ldst_vector_ready))){
            insts_from_dispipe2_in.push_back(inst);
            continue;
        }
        if((inst->ibuffer_id == 0 || inst->ibuffer_id == 1 ||
            inst->ibuffer_id == 4 || inst->ibuffer_id == 5) && intnomal_wtb_full){
            insts_from_dispipe2_in.push_back(inst);
            continue;
        }
        if((inst->ibuffer_id == 2 || inst->ibuffer_id == 3 ||
            inst->ibuffer_id == 20 || inst->ibuffer_id == 21) && intspecial_wtb_full){
            insts_from_dispipe2_in.push_back(inst);
            continue;
        }
        if((inst->ibuffer_id == 8 || inst->ibuffer_id == 9 || inst->ibuffer_id == 10 || inst->ibuffer_id == 11 
        || inst->ibuffer_id == 12 || inst->ibuffer_id == 13) && fp_wtb_full){
            insts_from_dispipe2_in.push_back(inst);
            continue;
        }

        if(!inst->isSquashed() && !inst->inst_had_insertq && (deep_eo - all_cnt) > 0){
            bool stall = false;
            stall = ((128-ldstQueue.getCountstore(tid)) <= 8) || ((204-ldstQueue.getCountload(tid)) <= 8);
            if((inst->isAtomic() || inst->isStore()) && !stall){
                DPRINTF(RxuLDSTQ, "insert store/atomic inst into storequeue seqnum[sn:%i]\n",inst->seqNum);
                ldstQueue.insertStore(inst);
            } else if ((inst->isAtomic() || inst->isStore()) && stall) {
                DPRINTF(RxuLDSTQ, "fail to insert store/atomic inst into storequeue seqnum[sn:%i]\n",inst->seqNum);
                insts_from_dispipe2_in.push_back(inst);
                continue;
            }
            if(inst->isLoad() && !stall){
                DPRINTF(RxuLDSTQ, "insert load inst into loadqueue seqnum[sn:%i]\n",inst->seqNum);
                ldstQueue.insertLoad(inst);
            } else if(inst->isLoad() && stall){
                DPRINTF(RxuLDSTQ, "fail to insert load inst into loadqueue seqnum[sn:%i]\n",inst->seqNum);
                insts_from_dispipe2_in.push_back(inst);
                continue;
            }
            inst->inst_had_insertq = true;
        }
        bool isStore = inst->isStore() || inst->isAtomic();
        bool memRef = inst->isMemRef();

        // Check for full conditions.
        if (wtb.isFull(tid, inst->ibuffer_id,isStore,memRef)) {
            DPRINTF(RxuDispipe3, "[tid:%i] ibuffer[%i] is full.\n", tid, inst->ibuffer_id);
            if(inst->ibuffer_id == 6 || inst->ibuffer_id == 7 ||
            inst->ibuffer_id == 14 || inst->ibuffer_id == 15 || 
            inst->ibuffer_id == 16 || inst->ibuffer_id == 17 || 
            inst->ibuffer_id == 18 || inst->ibuffer_id == 19) {
                ldst_wtb_full = true;
            }
            if(inst->ibuffer_id == 0 || inst->ibuffer_id == 1 ||
            inst->ibuffer_id == 4 || inst->ibuffer_id == 5) {
                intnomal_wtb_full = true;
            }
            if(inst->ibuffer_id == 2 || inst->ibuffer_id == 3 ||
            inst->ibuffer_id == 20 || inst->ibuffer_id == 21) {
                intspecial_wtb_full = true;
            }
            if(inst->ibuffer_id == 8 || inst->ibuffer_id == 9 ||
            inst->ibuffer_id == 10 || inst->ibuffer_id == 11 ||
            inst->ibuffer_id == 12 || inst->ibuffer_id == 13) {
                fp_wtb_full = true;
            }

            if(inst->isSquashed()){
                if((inst->ibuffer_id == 6 || inst->ibuffer_id == 7 || inst->ibuffer_id == 18 || inst->ibuffer_id == 19 ||
                inst->ibuffer_id == 14 || inst->ibuffer_id == 15 || inst->ibuffer_id == 16 || inst->ibuffer_id == 17 ) && ldstdisq[inst->ibuffer_id]){
                    fifo_cnt++;
                    ldstdisq[inst->ibuffer_id] = false;
                    DPRINTF(RxuLDST, "inst  ibufferid:%i squash in wtb this group.,seqnum[sn:%i]\n",inst->ibuffer_id,inst->seqNum);
                }
                if(fifo_cnt == 8){
                        DPRINTF(RxuLDST, "ldst fifo=8 can't insert ldst inst in this circle.,fifo_cnt:%i\n",fifo_cnt);
                        fifo_cnt = 0;
                        unable_odd = false;
                        unable_even = false;
                        num_even_str = 0;
                        num_odd_str = 0;
                        ldst_fifo_full = true;
                        next_group = true;
                        for(int i = 0 ;i <20 ;i++){
                            ldstdisq[i] = true;
                        }
                }
            }
            else {
                insts_from_dispipe2_in.push_back(inst);
            }


            // // Call function to start blocking.
            // block(tid);

            // toDispipe2->dispipe3Unblock[tid] = false;

            continue;
        }

        // Check LSQ if inst is LD/ST
        // if ((inst->isAtomic() && ldstQueue.sqFull(tid)) ||
        //     (inst->isLoad() && ldstQueue.lqFull(tid)) ||
        //     (inst->isStore() && ldstQueue.sqFull(tid))) {
        //     DPRINTF(RxuDispipe3, "[tid:%i] %s has become full.\n",tid,
        //             inst->isLoad() ? "LQ" : "SQ");

        //     insts_from_dispipe2_in.push_back(inst);

        //     // Call function to start blocking.
        //     block(tid);
        //     toDispipe2->dispipe3Unblock[tid] = false;

        //     ++stats.lsqFullEvents;

        //     continue;
        // }

        // hardware transactional memory
        // CPU needs to track transactional state in program order.
        const int numHtmStarts = ldstQueue.numHtmStarts(tid);
        const int numHtmStops = ldstQueue.numHtmStops(tid);
        const int htmDepth = numHtmStarts - numHtmStops;

        if (htmDepth > 0) {
            inst->setHtmTransactionalState(ldstQueue.getLatestHtmUid(tid),
                                            htmDepth);
        } else {
            inst->clearHtmTransactionalState();
        }


        // Otherwise insert the instruction just fine.
        if (inst->isAtomic()) {
            DPRINTF(RxuDispipe3, "[tid:%i] WTB insert: Memory instruction "
                    "encountered, adding to LSQ.\n", tid);

            // AMOs need to be set as "canCommit()"
            // so that commit can process them when they reach the
            // head of commit.
            add_to_wtb = false;
            if((inst->ibuffer_id == 6 || inst->ibuffer_id == 7 || inst->ibuffer_id == 18 || inst->ibuffer_id == 19 ||
            inst->ibuffer_id == 14 || inst->ibuffer_id == 15 || inst->ibuffer_id == 16 || inst->ibuffer_id == 17 )
            && (all_cnt < deep_eo) && ldstdisq[inst->ibuffer_id]){
                if(!inst->isSquashed()){
                    inst->setCanCommit();
                    wtb.insertNonSpec(inst);
                }
                fifo_cnt++;
                // if(!inst->isSquashed()){
                //     ldstQueue.insertStore(inst);
                // }
                // if(inst->ibuffer_id == 6 || inst->ibuffer_id == 14 || inst->ibuffer_id == 18 || inst->ibuffer_id == 16 ){
                //     even_cnt++;
                // }
                // else if(inst->ibuffer_id == 7 || inst->ibuffer_id == 15 || inst->ibuffer_id == 19 || inst->ibuffer_id == 17){
                //     odd_cnt++;
                // }
                all_cnt++;

                ldstdisq[inst->ibuffer_id] = false;
                DPRINTF(RxuLDST, "atomic  ibufferid:%i  insert in wtb this group.,seqnum[sn:%i]\n",inst->ibuffer_id,inst->seqNum);
            }
            else if(all_cnt >= deep_eo){
                insts_from_dispipe2_in.push_back(inst);
                DPRINTF(RxuDispipe3, "WTB ldst odd+even=4 can't insert ldst odd inst in this circle.\n");
                continue;
            }
            if(fifo_cnt == 8){
                    DPRINTF(RxuLDST, "ldst fifo=8 can't insert ldst inst in this circle.,fifo_cnt:%i\n",fifo_cnt);
                    fifo_cnt = 0;
                    unable_odd = false;
                    unable_even = false;
                    num_even_str = 0;
                    num_odd_str = 0;
                    ldst_fifo_full = true;
                    next_group = true;
                    for(int i = 0 ;i <20 ;i++){
                        ldstdisq[i] = true;
                    }
            }

            ++stats.dispNonSpecInsts;
        } else if (inst->isLoad()) {
            DPRINTF(RxuDispipe3, "[tid:%i] WTB insert: Memory instruction "
                    "encountered, adding to LSQ.\n", tid);

            bool wheOE = inst->numDestRegs() > 0 ? inst->renamedDestIdx(0)->index() & 1 : false;
            // const RegId &src_reg = inst->srcRegIdx(0);
            // bool wheOE = src_reg & 1 ? true : false;


            if(wheOE && all_cnt < deep_eo && ldstdisq[inst->ibuffer_id]){
                DPRINTF(RxuLDST, "ldst odd ibufferid:%i  insert in wtb this group.,all_cnt%i,seqnum[sn:%i]\n",inst->ibuffer_id,all_cnt,inst->seqNum);
                ++stats.dispLoadInsts;
                add_to_wtb = true;
                fifo_cnt++;
                num_ldst_towtb++;
                all_cnt++;
                num_odd_str = inst->ibuffer_id;
                ldstdisq[inst->ibuffer_id] = false;
                // if(!inst->isSquashed()){
                //     ldstQueue.insertLoad(inst);
                // }
                if(fifo_cnt == 8){
                    DPRINTF(RxuLDST, "ldst fifo=8 can't insert ldst inst in this circle.,fifo_cnt:%i\n",fifo_cnt);
                    fifo_cnt = 0;
                    unable_odd = false;
                    unable_even = false;
                    num_even_str = 0;
                    num_odd_str = 0;
                    ldst_fifo_full = true;
                    next_group = true;
                    for(int i = 0 ;i <20 ;i++){
                        ldstdisq[i] = true;
                    }
                }
            }
            else if(wheOE && all_cnt >= deep_eo){
                insts_from_dispipe2_in.push_back(inst);
                DPRINTF(RxuDispipe3, "WTB ldst odd=2 can't insert ldst odd inst in this circle.\n");
                continue;
            }


            if(!wheOE && all_cnt < deep_eo && ldstdisq[inst->ibuffer_id]){
                DPRINTF(RxuLDST, "ldst even ibufferid:%i  insert in wtb this group.,all_cnt%i,seqnum[sn:%i]\n",inst->ibuffer_id,all_cnt,inst->seqNum);
                ++stats.dispLoadInsts;
                add_to_wtb = true;
                fifo_cnt++;
                num_ldst_towtb++;
                all_cnt++;
                num_even_str = inst->ibuffer_id;
                ldstdisq[inst->ibuffer_id] = false;
                // if(!inst->isSquashed()){
                //     ldstQueue.insertLoad(inst);
                // }

                if(fifo_cnt == 8){
                    DPRINTF(RxuLDST, "ldst fifo=8 can't insert ldst inst in this circle.,fifo_cnt:%i\n",fifo_cnt);
                    fifo_cnt = 0;
                    unable_odd = false;
                    unable_even = false;
                    num_even_str = 0;
                    num_odd_str = 0;
                    ldst_fifo_full = true;
                    next_group = true;
                    for(int i = 0 ;i <20 ;i++){
                        ldstdisq[i] = true;
                    }
                }
            }
            else if(!wheOE && all_cnt >= deep_eo){
                insts_from_dispipe2_in.push_back(inst);
                DPRINTF(RxuDispipe3, "WTB ldst even=2 can't insert ldst even inst in this circle.\n");
                continue;
            }


        } else if (inst->isStore()) {
            DPRINTF(RxuDispipe3, "[tid:%i] WTB insert: Memory instruction "
                    "encountered, adding to LSQ.\n", tid);

            bool wheOE = inst->numDestRegs() > 0 ? inst->renamedDestIdx(0)->index() & 1 : false;
            // const RegId &src_reg = inst->srcRegIdx(0);
            // bool wheOE = src_reg & 1 ? true : false;


            if(wheOE && all_cnt < deep_eo && ldstdisq[inst->ibuffer_id]){
                DPRINTF(RxuLDST, "ldst odd ibufferid:%i  insert in wtb this group.,all_cnt%i,seqnum[sn:%i]\n",inst->ibuffer_id,all_cnt,inst->seqNum);
                ++stats.dispLoadInsts;
                add_to_wtb = true;
                fifo_cnt++;
                num_ldst_towtb++;
                all_cnt++;
                num_odd_str = inst->ibuffer_id;
                ldstdisq[inst->ibuffer_id] = false;
                // if(!inst->isSquashed()){
                //     ldstQueue.insertStore(inst);
                // }

                if(fifo_cnt == 8){
                    DPRINTF(RxuLDST, "ldst fifo=8 can't insert ldst inst in this circle.,fifo_cnt:%i\n",fifo_cnt);
                    fifo_cnt = 0;
                    unable_odd = false;
                    unable_even = false;
                    num_even_str = 0;
                    num_odd_str = 0;
                    ldst_fifo_full = true;
                    next_group = true;
                    for(int i = 0 ;i <20 ;i++){
                        ldstdisq[i] = true;
                    }
                }
            }
            else if(wheOE && all_cnt >= deep_eo){
                insts_from_dispipe2_in.push_back(inst);
                DPRINTF(RxuDispipe3, "WTB ldst odd=2 can't insert ldst odd inst in this circle.\n");
                continue;
            }


            if(!wheOE && all_cnt < deep_eo && ldstdisq[inst->ibuffer_id]){
                DPRINTF(RxuLDST, "ldst even ibufferid:%i  insert in wtb this group.,all_cnt%i,seqnum[sn:%i]\n",inst->ibuffer_id,all_cnt,inst->seqNum);
                ++stats.dispLoadInsts;
                add_to_wtb = true;
                fifo_cnt++;
                num_ldst_towtb++;
                all_cnt++;
                num_even_str = inst->ibuffer_id;
                ldstdisq[inst->ibuffer_id] = false;
                // if(!inst->isSquashed()){
                //     ldstQueue.insertStore(inst);
                // }

                if(fifo_cnt == 8){
                    DPRINTF(RxuLDST, "ldst fifo=8 can't insert ldst inst in this circle.,fifo_cnt:%i\n",fifo_cnt);
                    fifo_cnt = 0;
                    unable_odd = false;
                    unable_even = false;
                    num_even_str = 0;
                    num_odd_str = 0;
                    ldst_fifo_full = true;
                    next_group = true;
                    for(int i = 0 ;i <20 ;i++){
                        ldstdisq[i] = true;
                    }
                }
            }
            else if(!wheOE && all_cnt >= deep_eo){
                insts_from_dispipe2_in.push_back(inst);
                DPRINTF(RxuDispipe3, "WTB ldst even=2 can't insert ldst even inst in this circle.\n");
                continue;
            }


            if (inst->isStoreConditional()) {
                // Store conditionals need to be set as "canCommit()"
                // so that commit can process them when they reach the
                // head of commit.
                // @todo: This is somewhat specific to Alpha.
                if(inst->isSquashed()){
                    continue;
                }
                inst->setCanCommit();
                wtb.insertNonSpec(inst);
                add_to_wtb = false;

                ++stats.dispNonSpecInsts;
            } else {
                add_to_wtb = true;
            }
        } else if (inst->isReadBarrier() || inst->isWriteBarrier()) {
            // Same as non-speculative stores.
            add_to_wtb = false;
            if((inst->ibuffer_id == 6 || inst->ibuffer_id == 7 || inst->ibuffer_id == 18 || inst->ibuffer_id == 19 ||
            inst->ibuffer_id == 14 || inst->ibuffer_id == 15 || inst->ibuffer_id == 16 || inst->ibuffer_id == 17 )
            && (all_cnt < deep_eo) && ldstdisq[inst->ibuffer_id]){
                if(!inst->isSquashed()){
                    inst->setCanCommit();
                    wtb.insertBarrier(inst);
                }
                fifo_cnt++;
                // if(inst->ibuffer_id == 6 || inst->ibuffer_id == 14 || inst->ibuffer_id == 18 || inst->ibuffer_id == 16 ){
                //     even_cnt++;
                // }
                // else if(inst->ibuffer_id == 7 || inst->ibuffer_id == 15 || inst->ibuffer_id == 19 || inst->ibuffer_id == 17){
                //     odd_cnt++;
                // }
                all_cnt++;

                ldstdisq[inst->ibuffer_id] = false;
                DPRINTF(RxuLDST, "atomic  ibufferid:%i  insert in wtb this group.,seqnum[sn:%i]\n",inst->ibuffer_id,inst->seqNum);
            }
            else if(all_cnt >= deep_eo){
                insts_from_dispipe2_in.push_back(inst);
                DPRINTF(RxuDispipe3, "WTB ldst odd+even=4 can't insert ldst odd inst in this circle.\n");
                continue;
            }
            if(fifo_cnt == 8){
                    DPRINTF(RxuLDST, "ldst fifo=8 can't insert ldst inst in this circle.,fifo_cnt:%i\n",fifo_cnt);
                    fifo_cnt = 0;
                    unable_odd = false;
                    unable_even = false;
                    num_even_str = 0;
                    num_odd_str = 0;
                    ldst_fifo_full = true;
                    next_group = true;
                    for(int i = 0 ;i <20 ;i++){
                        ldstdisq[i] = true;
                    }
            }

        } else if (inst->isNop()) {
            DPRINTF(RxuDispipe3, "[tid:%i] WTB insert: Nop instruction encountered, "
                    "skipping.\n", tid);

            inst->setIssued();
            inst->setExecuted();
            inst->setCanCommit();
            add_to_wtb = false;
            if((inst->ibuffer_id == 6 || inst->ibuffer_id == 7 || inst->ibuffer_id == 18 || inst->ibuffer_id == 19 ||
            inst->ibuffer_id == 14 || inst->ibuffer_id == 15 || inst->ibuffer_id == 16 || inst->ibuffer_id == 17 )
            && (all_cnt < deep_eo) && ldstdisq[inst->ibuffer_id]){
                if(!inst->isSquashed()){
                    inst->setCanCommit();
                    wtb.insertNonSpec(inst);
                }
                fifo_cnt++;
                // if(inst->ibuffer_id == 6 || inst->ibuffer_id == 14 || inst->ibuffer_id == 18 || inst->ibuffer_id == 16 ){
                //     even_cnt++;
                // }
                // else if(inst->ibuffer_id == 7 || inst->ibuffer_id == 15 || inst->ibuffer_id == 19 || inst->ibuffer_id == 17){
                //     odd_cnt++;
                // }
                all_cnt++;

                ldstdisq[inst->ibuffer_id] = false;
                DPRINTF(RxuLDST, "atomic  ibufferid:%i  insert in wtb this group.,seqnum[sn:%i]\n",inst->ibuffer_id,inst->seqNum);
            }
            else if(all_cnt >= deep_eo){
                insts_from_dispipe2_in.push_back(inst);
                DPRINTF(RxuDispipe3, "WTB ldst odd+even=4 can't insert ldst odd inst in this circle.\n");
                continue;
            }
            if(fifo_cnt == 8){
                    DPRINTF(RxuLDST, "ldst fifo=8 can't insert ldst inst in this circle.,fifo_cnt:%i\n",fifo_cnt);
                    fifo_cnt = 0;
                    unable_odd = false;
                    unable_even = false;
                    num_even_str = 0;
                    num_odd_str = 0;
                    ldst_fifo_full = true;
                    next_group = true;
                    for(int i = 0 ;i <20 ;i++){
                        ldstdisq[i] = true;
                    }
            }
        } else {
            assert(!inst->isExecuted());
            add_to_wtb = true;
        }

        if (add_to_wtb && inst->isNonSpeculative()) {
            DPRINTF(RxuDispipe3, "[tid:%i] WTB insert: Nonspeculative instruction "
                    "encountered, skipping.\n", tid);
            if(inst->isSquashed()){
                continue;
            }

            // Same as non-speculative stores.
            inst->setCanCommit();

            // Specifically insert it as nonspeculative.
            wtb.insertNonSpec(inst);

            ++stats.dispNonSpecInsts;

            add_to_wtb = false;
        }

        if (add_to_wtb) {
            if(inst->isSquashed()){
                continue;
            }
            wtb.insert(inst);
            switch (inst->ibuffer_id)
            {
            case 0:
                stats.ibuffer0Insts++;
                break;
            case 1:
                stats.ibuffer1Insts++;
                break;
            case 2:
                stats.ibuffer2Insts++;
                break;
            case 3:
                stats.ibuffer3Insts++;
                break;
            case 4:
                stats.ibuffer4Insts++;
                break;
            case 5:
                stats.ibuffer5Insts++;
                break;
            case 6:
                stats.ibuffer6Insts++;
                break;
            case 7:
                stats.ibuffer7Insts++;
                break;
            case 8:
                stats.ibuffer8Insts++;
                break;
            case 9:
                stats.ibuffer9Insts++;
                break;
            case 10:
                stats.ibuffer10Insts++;
                break;
            case 11:
                stats.ibuffer11Insts++;
                break;
            case 12:
                stats.ibuffer12Insts++;
                break;
            case 13:
                stats.ibuffer13Insts++;
                break;
            case 14:
                stats.ibuffer14Insts++;
                break;
            case 15:
                stats.ibuffer15Insts++;
                break;
            case 16:
                stats.ibuffer16Insts++;
                break;
            case 17:
                stats.ibuffer17Insts++;
                break;
            case 18:
                stats.ibuffer18Insts++;
                break;
            case 19:
                stats.ibuffer19Insts++;
                break;
            case 20:
                stats.ibuffer20Insts++;
                break;
            case 21:
                stats.ibuffer21Insts++;
                break;
            case 22:
                stats.ibuffer22Insts++;
                break;
            case 23:
                stats.ibuffer23Insts++;
                break;
            default:
                break;
            }
        }

        processed_insts++;

#if TRACING_ON
        inst->dispipe3Tick = curTick() - inst->fetchTick;
#endif
        ppDispatch->notify(inst);
    }
    DPRINTF(RxuDispipe3,"num_ldst_towtb:%i\n",num_ldst_towtb);

    stats.dispipe3Insts += processed_insts;

    DPRINTF(RxuDispipe3,"[tid:%i] %i instructions insert into wtb this cycle.\n",
            tid, processed_insts);

    for (int i = 0; i < 24; i++) {
        if (wtb.isFull(tid, i)) {
            DPRINTF(RxuDispipe3,"[tid:%i] ibuffer[%i] is Full.\n", tid, i);
            switch (i)
            {
            case 0:
                ++stats.ibuffer0FullEvents;
                break;
            case 1:
                ++stats.ibuffer1FullEvents;
                break;
            case 2:
                ++stats.ibuffer2FullEvents;
                break;
            case 3:
                ++stats.ibuffer3FullEvents;
                break;
            case 4:
                ++stats.ibuffer4FullEvents;
                break;
            case 5:
                ++stats.ibuffer5FullEvents;
                break;
            case 6:
                ++stats.ibuffer6FullEvents;
                break;
            case 7:
                ++stats.ibuffer7FullEvents;
                break;
            case 8:
                ++stats.ibuffer8FullEvents;
                break;
            case 9:
                ++stats.ibuffer9FullEvents;
                break;
            case 10:
                ++stats.ibuffer10FullEvents;
                break;
            case 11:
                ++stats.ibuffer11FullEvents;
                break;
            case 12:
                ++stats.ibuffer12FullEvents;
                break;
            case 13:
                ++stats.ibuffer13FullEvents;
                break;
            case 14:
                ++stats.ibuffer14FullEvents;
                break;
            case 15:
                ++stats.ibuffer15FullEvents;
                break;
            case 16:
                ++stats.ibuffer16FullEvents;
                break;
            case 17:
                ++stats.ibuffer17FullEvents;
                break;
            case 18:
                ++stats.ibuffer18FullEvents;
                break;
            case 19:
                ++stats.ibuffer19FullEvents;
                break;
            case 20:
                ++stats.ibuffer20FullEvents;
                break;
            case 21:
                ++stats.ibuffer21FullEvents;
                break;
            case 22:
                ++stats.ibuffer22FullEvents;
                break;
            case 23:
                ++stats.ibuffer23FullEvents;
                break;
            default:
                break;
            }
        }
    }

    if (wtb.isFull(tid)) {
        DPRINTF(RxuDispipe3,"[tid:%i] some ibuffers are Full.\n", tid);
        // block(tid);
        // toDispipe2->dispipe3Unblock[tid] = false;
        ++stats.wtbFullEvents;
    }
}

void
Dispipe3::tick()
{
    wroteToTimeBuffer = false;

    ldstQueue.tick();

    DPRINTF(RxuDispipe3,"cache blocked: %d\n", cpu->dispipe3.ldstQueue.cacheBlocked());

    sortInsts();
    checkAndSplitEop();
    std::list<ThreadID>::iterator threads = activeThreads->begin();
    std::list<ThreadID>::iterator end = activeThreads->end();

    // Check stall and squash signals, process any instructions.
    while (threads != end) {
        ThreadID tid = *threads++;

        DPRINTF(RxuDispipe3,"WTB send and insert: Processing [tid:%i]\n",tid);

        wtb.stats.ibuffer0Utilize += wtb.ibuffer[tid][0].size();
        wtb.stats.ibuffer1Utilize += wtb.ibuffer[tid][1].size();
        wtb.stats.ibuffer2Utilize += wtb.ibuffer[tid][2].size();
        wtb.stats.ibuffer3Utilize += wtb.ibuffer[tid][3].size();
        wtb.stats.ibuffer4Utilize += wtb.ibuffer[tid][4].size();
        wtb.stats.ibuffer5Utilize += wtb.ibuffer[tid][5].size();
        wtb.stats.ibuffer6Utilize += wtb.ibuffer[tid][6].size();
        wtb.stats.ibuffer7Utilize += wtb.ibuffer[tid][7].size();
        wtb.stats.ibuffer8Utilize += wtb.ibuffer[tid][8].size();
        wtb.stats.ibuffer9Utilize += wtb.ibuffer[tid][9].size();
        wtb.stats.ibuffer10Utilize += wtb.ibuffer[tid][10].size();
        wtb.stats.ibuffer11Utilize += wtb.ibuffer[tid][11].size();
        wtb.stats.ibuffer12Utilize += wtb.ibuffer[tid][12].size();
        wtb.stats.ibuffer13Utilize += wtb.ibuffer[tid][13].size();
        wtb.stats.ibuffer14Utilize += wtb.ibuffer[tid][14].size();
        wtb.stats.ibuffer15Utilize += wtb.ibuffer[tid][15].size();
        wtb.stats.ibuffer16Utilize += wtb.ibuffer[tid][16].size();
        wtb.stats.ibuffer17Utilize += wtb.ibuffer[tid][17].size();
        wtb.stats.ibuffer18Utilize += wtb.ibuffer[tid][18].size();
        wtb.stats.ibuffer19Utilize += wtb.ibuffer[tid][19].size();
        wtb.stats.ibuffer20Utilize += wtb.ibuffer[tid][20].size();
        wtb.stats.ibuffer21Utilize += wtb.ibuffer[tid][21].size();
        wtb.stats.ibuffer22Utilize += wtb.ibuffer[tid][22].size();
        wtb.stats.ibuffer23Utilize += wtb.ibuffer[tid][23].size();

        checkSignalsAndUpdate(tid);
        dispipe3(tid);
    }

    updateStatus();

    if (wroteToTimeBuffer) {
        DPRINTF(RxuActivity, "Activity this cycle.\n");
        cpu->activityThisCycle();
    }

    // for (int i = 0; i < 22; i++) {
    //     if (i == 2 || i == 3) {
    //         continue;
    //     } else if (i == 20) {
    //         if ((wtb.ibuffer[0][20].size() + wtb.ibuffer[0][2].size()) >= wtb.numIntSpecialEntriesMax) {
    //             toDispipe2->Dispipe3Info->wtbFreeEntries[20]
    //                 = wtb.numWTBEntries > (wtb.ibuffer[0][20].size() + wtb.ibuffer[0][2].size()) ?
    //                     wtb.numWTBEntries - wtb.ibuffer[0][20].size() - wtb.ibuffer[0][2].size() : 0;

    //             toDispipe2->Dispipe3Info->wtbFreeEntries[2] = 0;
    //         } else {
    //             toDispipe2->Dispipe3Info->wtbFreeEntries[20] = (wtb.numWTBEntries - (wtb.ibuffer[0][20].size() + wtb.ibuffer[0][2].size())) >> 1;
    //             toDispipe2->Dispipe3Info->wtbFreeEntries[2] = (wtb.numWTBEntries - (wtb.ibuffer[0][20].size() + wtb.ibuffer[0][2].size())) >> 1;
    //         }
    //     } else if (i == 21) {
    //         if ((wtb.ibuffer[0][21].size() + wtb.ibuffer[0][3].size()) >= wtb.numIntSpecialEntriesMax) {
    //             toDispipe2->Dispipe3Info->wtbFreeEntries[21]
    //                 = wtb.numWTBEntries > (wtb.ibuffer[0][21].size() - wtb.ibuffer[0][3].size()) ?
    //                     wtb.numWTBEntries - wtb.ibuffer[0][21].size() - wtb.ibuffer[0][3].size() : 0;

    //             toDispipe2->Dispipe3Info->wtbFreeEntries[3] = 0;
    //         } else {
    //             toDispipe2->Dispipe3Info->wtbFreeEntries[21] = (wtb.numWTBEntries - (wtb.ibuffer[0][21].size() + wtb.ibuffer[0][3].size())) >> 1;
    //             toDispipe2->Dispipe3Info->wtbFreeEntries[3] = (wtb.numWTBEntries - (wtb.ibuffer[0][21].size() + wtb.ibuffer[0][3].size())) >> 1;
    //         }
    //     } else if (i != 6 && i != 7 && i != 14 && i != 15 && i != 16 && i != 17 && i != 18 && i != 19) {
    //         toDispipe2->Dispipe3Info->wtbFreeEntries[i]
    //             = wtb.numWTBEntries - wtb.ibuffer[0][i].size();
    //     } else {
    //         toDispipe2->Dispipe3Info->wtbFreeEntries[i]
    //             = wtb.LdStEntries - wtb.ibuffer[0][i].size();
    //     }
    // }

    int notInWtb[24] = {0};
    int size = insts_final[0].size();
    for (int i = 0; i < size; i++) {
        notInWtb[insts_final[0].at(i)->ibuffer_id]++;
    }


    for (int i = 0; i < 24; i++) {
        if (i == 2 || i == 3) {
            toDispipe2->Dispipe3Info->wtbFreeEntries[i]
                =  wtb.numIntSpecialEntries - wtb.ibuffer[0][i].size() - notInWtb[i];
        } else if (i != 6 && i != 7 && i != 14 && i != 15 && i != 16 && i != 17 && i != 18 && i != 19) {
            toDispipe2->Dispipe3Info->wtbFreeEntries[i]
                = wtb.numWTBEntries - wtb.ibuffer[0][i].size() - notInWtb[i];
        } else {
            toDispipe2->Dispipe3Info->wtbFreeEntries[i]
                = wtb.LdStEntries - wtb.ibuffer[0][i].size() - notInWtb[i];
        }

    }

    int m = wtb.ibuffer[0][2].size();
    int brInSpecial_2 = 0;
    for (int i = 0; i < m; i++) {
        DynInstPtr inst = wtb.ibuffer[0][2].front();
        wtb.ibuffer[0][2].pop_front();
        wtb.ibuffer[0][2].push_back(inst);
        if (inst->isCondCtrl()) {
            brInSpecial_2++;
        }
    }

    int n = wtb.ibuffer[0][3].size();
    int brInSpecial_3 = 0;
    for (int i = 0; i < n; i++) {
        DynInstPtr inst = wtb.ibuffer[0][3].front();
        wtb.ibuffer[0][3].pop_front();
        wtb.ibuffer[0][3].push_back(inst);
        if (inst->isCondCtrl()) {
            brInSpecial_3++;
        }
    }

    toDispipe2->Dispipe3Info->brInSpecialFull = (brInSpecial_2 >= 8) || (brInSpecial_3 >= 8);

    toDispipe2->Dispipe3Info->fpNormalBusy = wtb.ibuffer[0][8].size() > 8
            || wtb.ibuffer[0][9].size() > 8 || wtb.ibuffer[0][10].size() > 8 || wtb.ibuffer[0][13].size() > 8;

    // toDispipe2->Dispipe3Info->brInSpecialFull
    //      = (wtb.ibuffer[0][20].size() >= 8) || (wtb.ibuffer[0][21].size() >= 8);
}

bool
Dispipe3::hasStoresToWB()
{
    return ldstQueue.hasStoresToWB();
}

bool
Dispipe3::hasStoresToWB(ThreadID tid)
{
    return ldstQueue.hasStoresToWB(tid);
}

void
Dispipe3::setLastRetiredHtmUid(ThreadID tid, uint64_t htmUid)
{
    ldstQueue.setLastRetiredHtmUid(tid, htmUid);
}

} // namespace rxuo3
} // namespace gem5
// ----------------------------------------------------------------------------
