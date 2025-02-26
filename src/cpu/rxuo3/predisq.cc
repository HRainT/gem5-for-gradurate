// -- add by hongfei.liu ---------------------------------------
#include "cpu/rxuo3/predisq.hh"

#include "arch/generic/pcstate.hh"
#include "base/trace.hh"
#include "cpu/inst_seq.hh"
#include "cpu/rxuo3/dyn_inst.hh"
#include "cpu/rxuo3/limits.hh"
#include "debug/RxuActivity.hh"
#include "debug/RxuPredisq.hh"
#include "debug/RxuO3PipeView.hh"
#include "params/BaseRxuO3CPU.hh"

// clang complains about std::set being overloaded with Packet::set if
// we open up the entire namespace std
using std::list;

namespace gem5
{

namespace rxuo3
{

Predisq::Predisq(CPU *_cpu, const BaseRxuO3CPUParams &params)
    : cpu(_cpu),
      predisqWidth(params.predisqWidth),
      predisqueueSize(params.predisqueueSize),
      predisqGroupNums(params.predisqGroupNums),
      compressFlag(params.compressFlag),
      dispipe0ToPredisqDelay(params.dispipe0ToPredisqDelay),
      commitToPredisqDelay(params.commitToPredisqDelay),
      decodeToPredisqDelay(params.decodeToPredisqDelay),
      numThreads(params.numThreads),
      stats(_cpu)
{
    if (predisqWidth > MaxWidth)
        fatal("predisqWidth (%d) is larger than compiled limit (%d),\n"
             "\tincrease MaxWidth in src/cpu/rxuo3/limits.hh\n",
             predisqWidth, static_cast<int>(MaxWidth));

    for (int tid = 0; tid < MaxThreads; tid++) {
        stalls[tid] = {false};
        predisqStatus[tid] = Idle;
        lastStatus[tid] = Idle;
        freeEntries = predisqueueSize;
        freeGroups = predisqGroupNums;
        csrFenceInfo[tid].sent = false;
        csrFenceInfo[tid].inst = NULL;
    }
    
}

void
Predisq::startupStage()
{
    resetStage();
}

void
Predisq::clearStates(ThreadID tid)
{
    predisqStatus[tid] = Idle;
    lastStatus[tid] = Idle;
    stalls[tid].dispipe0 = false;

    if (compressFlag) {
        predisqueue.clear();
    } else {
        predisqGroups.clear();
    }

    freeEntries = predisqueueSize;
    freeGroups = predisqGroupNums;
    csrFenceInfo[tid].sent = false;
    csrFenceInfo[tid].inst = NULL;
}

void
Predisq::resetStage()
{
    _status = Inactive;

    if (compressFlag) {
        predisqueue.clear();
    } else {
        predisqGroups.clear();
    }

    freeEntries = predisqueueSize;
    freeGroups = predisqGroupNums;

    // Setup status, make sure stall signals are clear.
    for (ThreadID tid = 0; tid < numThreads; ++tid) {

        predisqStatus[tid] = Idle;
        lastStatus[tid] = Idle;

        stalls[tid].dispipe0 = false;
        csrFenceInfo[tid].sent = false;
        csrFenceInfo[tid].inst = NULL;
    }
}

std::string
Predisq::name() const
{
    return cpu->name() + ".predisq";
}

Predisq::PredisqStats::PredisqStats(CPU *cpu)
    : statistics::Group(cpu, "predisq"),
      ADD_STAT(idleCycles, statistics::units::Cycle::get(),
               "Number of cycles predisq is idle"),
      ADD_STAT(blockedCycles, statistics::units::Cycle::get(),
               "Number of cycles predisq is blocked"),
      ADD_STAT(runCycles, statistics::units::Cycle::get(),
               "Number of cycles predisq is running"),
      ADD_STAT(squashCycles, statistics::units::Cycle::get(),
               "Number of cycles predisq is squashing"),
      ADD_STAT(csrFenceCycles, statistics::units::Cycle::get(),
               "Number of cycles predisq is waiting csr fence instruction"),
      ADD_STAT(predisqFullEvents, statistics::units::Count::get(),
               "Number of times predisq has blocked due to predisqueue full"),              
      ADD_STAT(predisqedInsts, statistics::units::Count::get(),
               "Number of instructions handled by predisq"),
      ADD_STAT(squashedInsts, statistics::units::Count::get(),
               "Number of squashed instructions handled by predisq"),
      ADD_STAT(Out8, statistics::units::Count::get(),
               "Times of instructions == 8"),
      ADD_STAT(Out8Stall, statistics::units::Count::get(),
               "Times of instructions == 8 in stall status"),
      ADD_STAT(Out7, statistics::units::Count::get(),
               "Times of instructions == 7"),
      ADD_STAT(Out7Stall, statistics::units::Count::get(),
               "Times of instructions == 7 in stall status"),
      ADD_STAT(Out6, statistics::units::Count::get(),
               "Times of instructions == 6"),
      ADD_STAT(Out6Stall, statistics::units::Count::get(),
               "Times of instructions == 6 in stall status"),
      ADD_STAT(Out5, statistics::units::Count::get(),
               "Times of instructions == 5"),
      ADD_STAT(Out5Stall, statistics::units::Count::get(),
               "Times of instructions == 5 in stall status"),
      ADD_STAT(Out4, statistics::units::Count::get(),
               "Times of instructions == 4"),
      ADD_STAT(Out4Stall, statistics::units::Count::get(),
               "Times of instructions == 4 in stall status"),
      ADD_STAT(Out3, statistics::units::Count::get(),
               "Times of instructions == 3"),
      ADD_STAT(Out3Stall, statistics::units::Count::get(),
               "Times of instructions == 3 in stall status"),
      ADD_STAT(Out2, statistics::units::Count::get(),
               "Times of instructions == 2"),
      ADD_STAT(Out2Stall, statistics::units::Count::get(),
               "Times of instructions == 2 in stall status"), 
      ADD_STAT(Out1, statistics::units::Count::get(),
               "Times of instructions == 1"),
      ADD_STAT(Out1Stall, statistics::units::Count::get(),
               "Times of instructions == 1 in stall status"),
      //ADD_STAT(Out0Stall, statistics::units::Count::get(),
      //         "Times of instructions == 0 in stall status"),
      ADD_STAT(Out0NoStall, statistics::units::Count::get(),
               "Times of instructions == 0 not in stall status"),
      ADD_STAT(StallButZero, statistics::units::Count::get(),
                "Times of instructions == 0 in stall status"),
      ADD_STAT(Out8Rate, statistics::units::Ratio::get(),
               "Rate of instructions == 8"),
      ADD_STAT(Out8StallRate, statistics::units::Ratio::get(),
               "Rate of instructions == 8 in stall status"),
      ADD_STAT(Out7Rate, statistics::units::Ratio::get(),
               "Rate of instructions == 7"),
      ADD_STAT(Out7StallRate, statistics::units::Ratio::get(),
               "Rate of instructions == 7 in stall status"),  
      ADD_STAT(Out6Rate, statistics::units::Ratio::get(),
               "Rate of instructions == 6"),
      ADD_STAT(Out6StallRate, statistics::units::Ratio::get(),
               "Rate of instructions == 6 in stall status"),
      ADD_STAT(Out5Rate, statistics::units::Ratio::get(),
               "Rate of instructions == 5"),
      ADD_STAT(Out5StallRate, statistics::units::Ratio::get(),
               "Rate of instructions == 5 in stall status"),
      ADD_STAT(Out4Rate, statistics::units::Ratio::get(),
               "Rate of instructions == 4"),
      ADD_STAT(Out4StallRate, statistics::units::Ratio::get(),
               "Rate of instructions == 4 in stall status"),
      ADD_STAT(Out3Rate, statistics::units::Ratio::get(),
               "Rate of instructions == 3"),
      ADD_STAT(Out3StallRate, statistics::units::Ratio::get(),
               "Rate of instructions == 3 in stall status"),
      ADD_STAT(Out2Rate, statistics::units::Ratio::get(),
               "Rate of instructions == 2"),
      ADD_STAT(Out2StallRate, statistics::units::Ratio::get(),
               "Rate of instructions == 2 in stall status"),
      ADD_STAT(Out1Rate, statistics::units::Ratio::get(),
               "Rate of instructions == 1"),
      ADD_STAT(Out1StallRate, statistics::units::Ratio::get(),
               "Rate of instructions == 1 in stall status"),
      //ADD_STAT(Out0StallRate, statistics::units::Ratio::get(),
      //         "Rate of instructions == 0 in stall status"),
      ADD_STAT(Out0NoStallRate, statistics::units::Ratio::get(),
               "Rate of instructions == 0 not in stall status")
{
    idleCycles.prereq(idleCycles);
    blockedCycles.prereq(blockedCycles);
    runCycles.prereq(runCycles);
    squashCycles.prereq(squashCycles);
    csrFenceCycles.prereq(csrFenceCycles);
    predisqFullEvents.prereq(predisqFullEvents);
    predisqedInsts.prereq(predisqedInsts);
    squashedInsts.prereq(squashedInsts);
    Out8.prereq(Out8);
    Out8Stall.prereq(Out8Stall);
    Out7.prereq(Out7);
    Out7Stall.prereq(Out7Stall);
    Out6.prereq(Out6);
    Out6Stall.prereq(Out6Stall);
    Out5.prereq(Out5);
    Out5Stall.prereq(Out5Stall);
    Out4.prereq(Out4);
    Out4Stall.prereq(Out4Stall);
    Out3.prereq(Out3);
    Out3Stall.prereq(Out3Stall);
    Out2.prereq(Out2);
    Out2Stall.prereq(Out2Stall);
    Out1.prereq(Out1);
    Out1Stall.prereq(Out1Stall);
    //Out0Stall.prereq(Out0Stall);
    Out0NoStall.prereq(Out0NoStall);


    

    Out8Rate.precision(9);
    Out8Rate = Out8 / (Out8 + Out8Stall + Out7 + Out7Stall + Out6 + Out6Stall + Out5 + Out5Stall + Out4 + Out4Stall + Out3 + Out3Stall + Out2 + Out2Stall + Out1 + Out1Stall + Out0NoStall);
    Out8StallRate.precision(9);
    Out8StallRate = Out8Stall / (Out8 + Out8Stall + Out7 + Out7Stall + Out6 + Out6Stall + Out5 + Out5Stall + Out4 + Out4Stall + Out3 + Out3Stall + Out2 + Out2Stall + Out1 + Out1Stall + Out0NoStall);
    Out7Rate.precision(9);
    Out7Rate = Out7 / (Out8 + Out8Stall + Out7 + Out7Stall + Out6 + Out6Stall + Out5 + Out5Stall + Out4 + Out4Stall + Out3 + Out3Stall + Out2 + Out2Stall + Out1 + Out1Stall + Out0NoStall);
    Out7StallRate.precision(9);
    Out7StallRate = Out7Stall / (Out8 + Out8Stall + Out7 + Out7Stall + Out6 + Out6Stall + Out5 + Out5Stall + Out4 + Out4Stall + Out3 + Out3Stall + Out2 + Out2Stall + Out1 + Out1Stall + Out0NoStall);
    Out6Rate.precision(9);
    Out6Rate = Out6 / (Out8 + Out8Stall + Out7 + Out7Stall + Out6 + Out6Stall + Out5 + Out5Stall + Out4 + Out4Stall + Out3 + Out3Stall + Out2 + Out2Stall + Out1 + Out1Stall + Out0NoStall);
    Out6StallRate.precision(9);
    Out6StallRate = Out6Stall / (Out8 + Out8Stall + Out7 + Out7Stall + Out6 + Out6Stall + Out5 + Out5Stall + Out4 + Out4Stall + Out3 + Out3Stall + Out2 + Out2Stall + Out1 + Out1Stall + Out0NoStall);
    Out5Rate.precision(9);
    Out5Rate = Out5 / (Out8 + Out8Stall + Out7 + Out7Stall + Out6 + Out6Stall + Out5 + Out5Stall + Out4 + Out4Stall + Out3 + Out3Stall + Out2 + Out2Stall + Out1 + Out1Stall + Out0NoStall);
    Out5StallRate.precision(9);
    Out5StallRate = Out5Stall / (Out8 + Out8Stall + Out7 + Out7Stall + Out6 + Out6Stall + Out5 + Out5Stall + Out4 + Out4Stall + Out3 + Out3Stall + Out2 + Out2Stall + Out1 + Out1Stall + Out0NoStall);
    Out4Rate.precision(9);
    Out4Rate = Out4 / (Out8 + Out8Stall + Out7 + Out7Stall + Out6 + Out6Stall + Out5 + Out5Stall + Out4 + Out4Stall + Out3 + Out3Stall + Out2 + Out2Stall + Out1 + Out1Stall + Out0NoStall);
    Out4StallRate.precision(9);
    Out4StallRate = Out4Stall / (Out8 + Out8Stall + Out7 + Out7Stall + Out6 + Out6Stall + Out5 + Out5Stall + Out4 + Out4Stall + Out3 + Out3Stall + Out2 + Out2Stall + Out1 + Out1Stall + Out0NoStall);
    Out3Rate.precision(9);
    Out3Rate = Out3 / (Out8 + Out8Stall + Out7 + Out7Stall + Out6 + Out6Stall + Out5 + Out5Stall + Out4 + Out4Stall + Out3 + Out3Stall + Out2 + Out2Stall + Out1 + Out1Stall + Out0NoStall);
    Out3StallRate.precision(9);
    Out3StallRate = Out3Stall / (Out8 + Out8Stall + Out7 + Out7Stall + Out6 + Out6Stall + Out5 + Out5Stall + Out4 + Out4Stall + Out3 + Out3Stall + Out2 + Out2Stall + Out1 + Out1Stall + Out0NoStall);
    Out2Rate.precision(9);
    Out2Rate = Out2 / (Out8 + Out8Stall + Out7 + Out7Stall + Out6 + Out6Stall + Out5 + Out5Stall + Out4 + Out4Stall + Out3 + Out3Stall + Out2 + Out2Stall + Out1 + Out1Stall + Out0NoStall);
    Out2StallRate.precision(9);
    Out2StallRate = Out2Stall / (Out8 + Out8Stall + Out7 + Out7Stall + Out6 + Out6Stall + Out5 + Out5Stall + Out4 + Out4Stall + Out3 + Out3Stall + Out2 + Out2Stall + Out1 + Out1Stall + Out0NoStall);
    Out1Rate.precision(9);
    Out1Rate = Out1 / (Out8 + Out8Stall + Out7 + Out7Stall + Out6 + Out6Stall + Out5 + Out5Stall + Out4 + Out4Stall + Out3 + Out3Stall + Out2 + Out2Stall + Out1 + Out1Stall + Out0NoStall);
    Out1StallRate.precision(9);
    Out1StallRate = Out1Stall / (Out8 + Out8Stall + Out7 + Out7Stall + Out6 + Out6Stall + Out5 + Out5Stall + Out4 + Out4Stall + Out3 + Out3Stall + Out2 + Out2Stall + Out1 + Out1Stall + Out0NoStall);

    //Out0StallRate.precision(9);
    //Out0StallRate = Out0Stall / (Out8 + Out8Stall + Out7 + Out7Stall + Out6 + Out6Stall + Out5 + Out5Stall + Out4 + Out4Stall + Out3 + Out3Stall + Out2 + Out2Stall + Out1 + Out1Stall + Out0NoStall);
    Out0NoStallRate.precision(9);
    Out0NoStallRate = Out0NoStall / (Out8 + Out8Stall + Out7 + Out7Stall + Out6 + Out6Stall + Out5 + Out5Stall + Out4 + Out4Stall + Out3 + Out3Stall + Out2 + Out2Stall + Out1 + Out1Stall + Out0NoStall);

}

void
Predisq::setTimeBuffer(TimeBuffer<TimeStruct> *tb_ptr)
{
    timeBuffer = tb_ptr;

    // Setup wire to write information back to decode.
    toDecode = timeBuffer->getWire(0);

    // Create wires to get information from proper places in time buffer.
    fromDispipe0 = timeBuffer->getWire(-dispipe0ToPredisqDelay);
    fromCommit = timeBuffer->getWire(-commitToPredisqDelay);
}

void
Predisq::setPredisqQueue(TimeBuffer<PredisqStruct> *pq_ptr)
{
    predisqQueue = pq_ptr;

    // Setup wire to write information to proper place in predisq queue.
    toDispipe0 = predisqQueue->getWire(0);
}

void
Predisq::setDecodeQueue(TimeBuffer<DecodeStruct> *dq_ptr)
{
    decodeQueue = dq_ptr;

    // Setup wire to read information from decode queue.
    fromDecode = decodeQueue->getWire(-decodeToPredisqDelay);
}

void
Predisq::setActiveThreads(std::list<ThreadID> *at_ptr)
{
    activeThreads = at_ptr;
}

void
Predisq::drainSanityCheck() const
{
    assert(predisqueue.empty());
    assert(predisqGroups.empty());
    for (ThreadID tid = 0; tid < numThreads; ++tid) {
        assert(insts[tid].empty());
    }
}

bool
Predisq::isDrained() const
{
    if (!predisqueue.empty()) {
        return false;
    }
    if (!predisqGroups.empty()) {
        return false;
    }

    for (ThreadID tid = 0; tid < numThreads; ++tid) {
        if (!insts[tid].empty() || 
            (predisqStatus[tid] != Running && predisqStatus[tid] != Idle))
            return false;
    }
    return true;
}

void
Predisq::tick()
{
    wroteToTimeBuffer = false;

    bool status_change = false;

    toDispipe0Index = 0;
    predisqueueInsertNums = 0;

    list<ThreadID>::iterator threads = activeThreads->begin();
    list<ThreadID>::iterator end = activeThreads->end();

    DPRINTF(RxuPredisq, "%i instructions from decode this cycle.\n",
            fromDecode->size);

    if (compressFlag) {
        DPRINTF(RxuPredisq, "free entries = %d before processing.\n",
            freeEntries);
    } else {
        DPRINTF(RxuPredisq, "free groups = %d before processing.\n",
            freeGroups);
    }

    if (decodeInstsValid()) {
        sortInsts();
    }

    //Check stall and squash signals.
    while (threads != end) {
        ThreadID tid = *threads++;

        if (csrFenceInfo[tid].inst && csrFenceInfo[tid].inst->readyToCommit()) {
            DPRINTF(RxuPredisq, "[tid:%i] CSR fence instruction %i with PC %s is "
                    "committed, release stall.\n",
                    tid, csrFenceInfo[tid].inst->seqNum, csrFenceInfo[tid].inst->pcState());
            csrFenceInfo[tid] = {false, NULL};
        }

        lastStatus[tid] = predisqStatus[tid];

        DPRINTF(RxuPredisq,"Processing thread : %i\n",tid);
        status_change =  checkSignalsAndUpdate(tid) || status_change;

        predisq(status_change, tid);
    }

    if (compressFlag) {
        DPRINTF(RxuPredisq, "free entries = %d after processing.\n",
            freeEntries);
    } else {
        DPRINTF(RxuPredisq, "free groups = %d after processing.\n",
            freeGroups);
    }

    if (status_change) {
        updateStatus();
    }

    if (wroteToTimeBuffer) {
        DPRINTF(RxuActivity, "Activity this cycle.\n");

        cpu->activityThisCycle();
    }

    DPRINTF(RxuPredisq, "push %i instructions into predisqueue "
            "and send %i instructions to dispipe0 this cycle.\n",
                        predisqueueInsertNums, toDispipe0Index);
    
    if (toDispipe0Index == 8) {
        stats.Out8++;
    } else if (toDispipe0Index == 7) {
        stats.Out7++;
    } else if (toDispipe0Index == 6) {
        stats.Out6++;
    } else if (toDispipe0Index == 5) {
        stats.Out5++;
    } else if (toDispipe0Index == 4) {
        stats.Out4++;
    } else if (toDispipe0Index == 3) {
        stats.Out3++;
    } else if (toDispipe0Index == 2) {
        stats.Out2++;
    } else if (toDispipe0Index == 1) {
        stats.Out1++;
    } else if (toDispipe0Index == 0){
        if (predisqStatus[0] == Running || predisqStatus[0] == Idle) {
            stats.Out0NoStall++;
        } else if(predisqueue.size() >= 8){
            stats.Out8Stall++;
        } else if(predisqueue.size() == 7){
            stats.Out7Stall++;
        } else if(predisqueue.size() == 6){
            stats.Out6Stall++;
        } else if(predisqueue.size() == 5){
            stats.Out5Stall++;
        } else if(predisqueue.size() == 4){
            stats.Out4Stall++;
        } else if(predisqueue.size() == 3){
            stats.Out3Stall++;
        } else if(predisqueue.size() == 2){
            stats.Out2Stall++;
        } else if(predisqueue.size() == 1){
            stats.Out1Stall++;
        } else {
            stats.StallButZero;
        }
    }

}

void
Predisq::predisq(bool &status_change, ThreadID tid)
{
    if (predisqStatus[tid] == Blocked) {
        InsertInsts(tid);
        ++stats.blockedCycles;
    } else if (predisqStatus[tid] == Squashing) {
        InsertInsts(tid);
        ++stats.squashCycles;
    } else if (predisqStatus[tid] == Running ||
        predisqStatus[tid] == Idle) {
        DPRINTF(RxuPredisq, "[tid:%i] Not blocked, so attempting to run "
                "predisq stage.\n",tid);
        InsertInsts(tid);
        SendInsts(tid);
    }

    updateFreeEntries();
}

void
Predisq::InsertInsts(ThreadID tid)
{
    int insts_available = insts[tid].size();
    DynInstPtr inst;

    if (compressFlag) {
        for (int i = 0; i < insts_available; i++) {
            inst = insts[tid].front();
            insts[tid].pop_front();

            if (inst->isSquashed()) {
                DPRINTF(RxuPredisq, "[tid:%i] Instruction %i with PC %s is "
                        "squashed, skipping.\n",
                        tid, inst->seqNum, inst->pcState());

                ++stats.squashedInsts;
                continue;
            }

            if (inst->staticInst->isFence_i()) {
                cpu->uc.flushUopCache();
                cpu->bpu0.l0btb.clear();
                cpu->bpu0.return_Cam.clear();
            }

            DPRINTF(RxuPredisq, "[tid:%i] Inserting instruction into predisqueue [sn:%lli] with "
            "PC %s\n", tid, inst->seqNum, inst->pcState());
            inst->arrive_predisq = curTick();
            predisqueue.push_back(inst);
            ++predisqueueInsertNums;
        }
    } else {
        GroupStruct group;
        group.size = 0;

        for (int i = 0; i < insts_available; i++) {
            inst = insts[tid].front();
            insts[tid].pop_front();

            if (inst->isSquashed()) {
                DPRINTF(RxuPredisq, "[tid:%i] Instruction %i with PC %s is "
                        "squashed, skipping.\n",
                        tid, inst->seqNum, inst->pcState());

                ++stats.squashedInsts;
                continue;
            }

            if (inst->staticInst->isFence_i()) {
                cpu->uc.flushUopCache();
                cpu->bpu0.l0btb.clear();
                cpu->bpu0.return_Cam.clear();
            }

            DPRINTF(RxuPredisq, "[tid:%i] Inserting instruction into predisqueue [sn:%lli] with "
            "PC %s\n", tid, inst->seqNum, inst->pcState());
            group.insts[group.size] = inst;
            inst->arrive_predisq = curTick();
            group.size++;
            ++predisqueueInsertNums;
        }
        
        if (group.size > 0) {
            predisqGroups.push_back(group);
        }
    }
}

void
Predisq::SendInsts(ThreadID tid) 
{

    DPRINTF(RxuPredisq, "[tid:%i] Start Sending instruction to dispipe0.\n",tid);

    if (compressFlag) {

        if (predisqueue.empty()) {
            DPRINTF(RxuPredisq, "[tid:%i] No inst to send, breaking out"
                    " early.\n",tid);
            predisqStatus[tid] = Idle;
            ++stats.idleCycles;
            return;
        }

        predisqStatus[tid] = Running;

        DynInstPtr inst;

        unsigned sendWidth = in_order ? 1 : predisqWidth;

        while ((toDispipe0Index < sendWidth) && (!predisqueue.empty())) {
            if (csrFenceInfo[tid].inst && csrFenceInfo[tid].sent) {
                DPRINTF(RxuPredisq, "Waiting a CSR fence instruction to commit.\n");
                DPRINTF(RxuPredisq, "Instruction [sn:%i] is: %s\n", csrFenceInfo[tid].inst->seqNum,
                        csrFenceInfo[tid].inst->staticInst->disassemble(csrFenceInfo[tid].inst->pc->instAddr()));
                break;
            }
            inst = predisqueue.front();
            // if (inst->arrive_predisq == curTick()) {
            //     break;
            // }
            predisqueue.pop_front();

            if (inst->isSquashed()) {
                DPRINTF(RxuPredisq, "[tid:%i] Instruction %i with PC %s is "
                        "squashed, skipping.\n",
                        tid, inst->seqNum, inst->pcState());

                ++stats.squashedInsts;
                continue;
            }

            DPRINTF(RxuPredisq, "[tid:%i] Sending instruction [sn:%lli] with "
                    "PC %s\n", tid, inst->seqNum, inst->pcState());
            
            if (inst->isSerializeAfter()) {
                if (csrFenceInfo[tid].inst) {
                    DPRINTF(RxuPredisq, "[tid:%i] Trying to send a CSR fence instruction [sn:%lli] with PC %s.\n",
                            tid, inst->seqNum, inst->pcState()); 
                } else {
                    DPRINTF(RxuPredisq, "[tid:%i] Encountered a CSR fence instruction [sn:%lli] with PC %s.\n",
                            tid, inst->seqNum, inst->pcState());
                    csrFenceInfo[tid].inst = inst;
                }
                if (cpu->isOldestInstInPipe(inst)) {
                    csrFenceInfo[tid].sent = true;
                    toDispipe0->insts[toDispipe0Index] = inst;

                    ++(toDispipe0->size);
                    ++toDispipe0Index;
                    ++stats.predisqedInsts;

#if TRACING_ON
                    if (debug::RxuO3PipeView) {
                        inst->predisqTick = curTick() - inst->fetchTick;
                    }
#endif
                } else {
                    predisqueue.push_front(inst);
                }
                break;
            }

            if (inst->staticInst->isCompressed()) inst->pc->as<RiscvISA::PCState>().compressed(true);

            toDispipe0->insts[toDispipe0Index] = inst;

            ++(toDispipe0->size);
            ++toDispipe0Index;
            ++stats.predisqedInsts;

            if (inst->pred_ctr == 3 || inst->pred_ctr == 4) {
                pred_weak_youngest = inst->seqNum;
                // sendWidth = 1;
                // in_order = true;
            }

#if TRACING_ON
            if (debug::RxuO3PipeView) {
                inst->predisqTick = curTick() - inst->fetchTick;
            }
#endif

        }
    } else {
        if (predisqGroups.empty()) {
            DPRINTF(RxuPredisq, "[tid:%i] No inst to send, breaking out"
                    " early.\n",tid);
            predisqStatus[tid] = Idle;
            ++stats.idleCycles;
            return;
        }

        predisqStatus[tid] = Running;

        GroupStruct group;
        DynInstPtr inst;
        bool flag = false;

        while ((toDispipe0Index == 0) && (!predisqGroups.empty()) && !flag) {

            if (csrFenceInfo[tid].inst && csrFenceInfo[tid].sent) {
                DPRINTF(RxuPredisq, "Waiting a CSR fence instruction to commit.\n");
                DPRINTF(RxuPredisq, "Instruction [sn:%i] is: %s\n", csrFenceInfo[tid].inst->seqNum,
                        csrFenceInfo[tid].inst->staticInst->disassemble(csrFenceInfo[tid].inst->pc->instAddr()));
                break;
            }

            group = predisqGroups.front();
            // if (group.insts[0]->arrive_predisq == curTick()) {
            //     break;
            // }
            predisqGroups.pop_front();

            for (int i = 0; i < group.size; i++) {
                inst = group.insts[i];

                if (inst->isSquashed()) {
                    DPRINTF(RxuPredisq, "[tid:%i] Instruction %i with PC %s is "
                            "squashed, skipping.\n",
                            tid, inst->seqNum, inst->pcState());

                    ++stats.squashedInsts;
                    continue;
                }

                DPRINTF(RxuPredisq, "[tid:%i] Sending instruction [sn:%lli] with "
                        "PC %s\n", tid, inst->seqNum, inst->pcState());

                if (inst->isSerializeAfter()) {
                    if (csrFenceInfo[tid].inst) {
                        DPRINTF(RxuPredisq, "[tid:%i] Trying to send a CSR fence instruction [sn:%lli] with PC %s.\n",
                                tid, inst->seqNum, inst->pcState()); 
                    } else {
                        DPRINTF(RxuPredisq, "[tid:%i] Encountered a CSR fence instruction [sn:%lli] with PC %s.\n",
                                tid, inst->seqNum, inst->pcState());
                        csrFenceInfo[tid].inst = inst;
                    }
                    int index;
                    if (cpu->isOldestInstInPipe(inst)) {
                        csrFenceInfo[tid].sent = true;
                        toDispipe0->insts[toDispipe0Index] = inst;

                        ++(toDispipe0->size);
                        ++toDispipe0Index;
                        ++stats.predisqedInsts;
                        index = i + 1;

#if TRACING_ON
                        if (debug::RxuO3PipeView) {
                            inst->predisqTick = curTick() - inst->fetchTick;
                        }
#endif
                    } else {
                        index = i;
                    }
                    GroupStruct group1;
                    group1.size = group.size - index;
                    if (group1.size > 0) {
                        for (int j = 0; j < group1.size; j++) {
                            group1.insts[j] = group.insts[index++];
                        }
                        predisqGroups.push_front(group1);
                    }

                    flag = true;
                    break;
                }

                toDispipe0->insts[toDispipe0Index] = inst;

                ++(toDispipe0->size);
                ++toDispipe0Index;
                ++stats.predisqedInsts;

#if TRACING_ON
                if (debug::RxuO3PipeView) {
                    inst->predisqTick = curTick() - inst->fetchTick;
                }
#endif
            }
        }

    }

    // for (int i = 0; i < toDispipe0Index; i++) {
    //     if (toDispipe0->insts[i]->isCsrFRM()) {
    //         CPU::frm_Ready frm;
    //         frm.NewSeqNum = toDispipe0->insts[i]->seqNum;
    //         frm.ready = false;
    //         cpu->frm_Insts.push_back(frm);
    //     }
    //     if (toDispipe0->insts[i]->needFRM()) {
    //         cpu->frm_Insts.back().FloatInsts.push_back(toDispipe0->insts[i]);
    //     }
    // }
    
    // Record that predisq has written to the time buffer for activity
    // tracking.
    if (toDispipe0Index) {
        ++stats.runCycles;
        wroteToTimeBuffer = true;
    } else {
        ++stats.csrFenceCycles;
    }

}

void 
Predisq::updateFreeEntries()
{
    if (compressFlag) {
        freeEntries = predisqueueSize - predisqueue.size();
        if (freeEntries == 0) {
            ++stats.predisqFullEvents;
            DPRINTF(RxuPredisq, "predisqueue is full.\n");
        }
    } else {
        freeGroups = predisqGroupNums - predisqGroups.size();
        if (freeGroups == 0) {
            ++stats.predisqFullEvents;
            DPRINTF(RxuPredisq, "predisqGroups is full.\n");
        }
    }
    toDecode->PredisqInfo->freeEntries = freeEntries;
    toDecode->PredisqInfo->freeGroups = freeGroups;
}

void
Predisq::updateStatus()
{
    bool any_unblocking = false;

    list<ThreadID>::iterator threads = activeThreads->begin();
    list<ThreadID>::iterator end = activeThreads->end();

    while (threads != end) {
        ThreadID tid = *threads++;

        if ((lastStatus[tid] == Blocked) && (!stalls[tid].dispipe0)) {
            any_unblocking = true;
            break;
        }
    }

    // Predisq will have activity.
    if (any_unblocking) {
        if (_status == Inactive) {
            _status = Active;

            DPRINTF(RxuActivity, "Activating stage.\n");

            cpu->activateStage(CPU::PredisqIdx);
        }
    } else {
        // If it's not unblocking, then predisq will not have any internal
        // activity.  Switch it to inactive.
        if (_status == Active) {
            _status = Inactive;
            DPRINTF(RxuActivity, "Deactivating stage.\n");

            cpu->deactivateStage(CPU::PredisqIdx);
        }
    }
}

void
Predisq::sortInsts()
{
    int insts_from_decode = fromDecode->size;
    for (int i = 0; i < insts_from_decode; ++i) {

        if (fromDecode->insts[i]->staticInst->isCsr() && fromDecode->insts[i]->updateCsrFence()) {
            DPRINTF(RxuPredisq, "Clear the fence attribute of the CSR instruction.\n");
            DPRINTF(RxuPredisq, "Instruction [sn:%i] is: %s\n", fromDecode->insts[i]->seqNum,
                    fromDecode->insts[i]->staticInst->disassemble(fromDecode->insts[i]->pc->instAddr()));
        }

        insts[fromDecode->insts[i]->threadNumber].push_back(fromDecode->insts[i]);
    }
}

void
Predisq::readStallSignals(ThreadID tid)
{
    if (fromDispipe0->dispipe0Block[tid]) {
        stalls[tid].dispipe0 = true;
    }

    if (fromDispipe0->dispipe0Unblock[tid]) {
        assert(stalls[tid].dispipe0);
        stalls[tid].dispipe0 = false;
    }
}

bool
Predisq::checkSignalsAndUpdate(ThreadID tid)
{
    readStallSignals(tid);

    if (fromCommit->commitInfo[tid].squash) {

        DPRINTF(RxuPredisq, "[tid:%i] Squashing instructions due to squash "
                "from commit.\n", tid);

        squash(tid);

        return true;
    }

    if (fromCommit->commitInfo[tid].doneSeqNum >= pred_weak_youngest) {
        in_order = false;
    }

    // if (predisqStatus[tid] == Squashing && fromCommit->commitInfo[tid].robSquashing) {

    //     DPRINTF(RxuPredisq, "[tid:%i] Rob is squashing.\n", tid);
        
    //     return true;
    // }

    if (checkStall(tid)) {   
        return block(tid);
    }

    if (predisqStatus[tid] == Blocked) {
        DPRINTF(RxuPredisq, "[tid:%i] Done blocking, switching to running.\n",
                tid);

        unblock(tid);

        return true;
    }

    if (predisqStatus[tid] == Squashing) {
        // Switch status to running if predisq isn't being told to block or
        // squash this cycle.
        DPRINTF(RxuPredisq, "[tid:%i] Done squashing, switching to running.\n",
                tid);

        predisqStatus[tid] = Running;

        return true;
    }

    // If we've reached this point, we have not gotten any signals that
    // cause predisq to change its status.  Predisq remains the same as before.
    return false;
}

bool
Predisq::checkStall(ThreadID tid) const
{
    bool ret_val = false;

    if (fromDispipe0->Dispipe0Info->vecQueueFull || stalls[tid].dispipe0 ||
       fromCommit->commitInfo[tid].robSquashing || fromDispipe0->Dispipe0Info->vecRenameStall){
        DPRINTF(RxuPredisq,"[tid:%i] Stall from Dispipe0 stage detected.\n", tid);
        ret_val = true;
        }

    return ret_val;
}

bool
Predisq::decodeInstsValid()
{
    return (fromDecode->size > 0);
}

bool
Predisq:: block(ThreadID tid)
{
    DPRINTF(RxuPredisq, 
            "[tid:%i] Predisq can't send instruction to dispipe0 this cycle.\n", tid);

    if (predisqStatus[tid] != Blocked) {
        // Set the status to Blocked.
        predisqStatus[tid] = Blocked;

        return true;
    }

    return false;
}

void
Predisq::unblock(ThreadID tid)
{
    predisqStatus[tid] = Running;
    // toDecode->predisqUnblock[tid] = true;
    wroteToTimeBuffer = true;
}

void
Predisq::squash(ThreadID tid)
{
    DPRINTF(RxuPredisq, "[tid:%i] Squashing.\n",tid);

    // Set status to squashing.
    predisqStatus[tid] = Squashing;

    if (csrFenceInfo[tid].inst && fromCommit->commitInfo[tid].doneSeqNum <= csrFenceInfo[tid].inst->seqNum) {
        DPRINTF(RxuPredisq, "[tid:%i] CSR fence instruction %i with PC %s is "
                "squashed, release stall.\n",
                tid, csrFenceInfo[tid].inst->seqNum, csrFenceInfo[tid].inst->pcState());
        csrFenceInfo[tid] = {false, NULL};
    }

    // Go through incoming instructions from decode and squash them.
    for (int i = 0; i < fromDecode->size; i++) {
        if ((fromDecode->insts[i]->threadNumber == tid) &&
            (fromDecode->insts[i]->seqNum > fromCommit->commitInfo[tid].doneSeqNum)) {

            fromDecode->insts[i]->setSquashed();

            wroteToTimeBuffer = true;
        }
    }

    // int index = cpu->frm_Insts.size() - 1;
    // for (; index >= 0; index--) {
    //     if (cpu->frm_Insts.at(index).NewSeqNum > fromCommit->commitInfo[tid].doneSeqNum) {
    //         cpu->frm_Insts.pop_back();
    //     } else {
    //         break;
    //     }
    // }

    // int i = cpu->frm_Insts.at(index).FloatInsts.size() - 1;
    // for (; i >= 0; i--) {
    //     if (cpu->frm_Insts.at(index).FloatInsts.at(i) > fromCommit->commitInfo[tid].doneSeqNum) {
    //         cpu->frm_Insts.at(index).FloatInsts.pop_back();
    //     } else {
    //         break;
    //     }
    // }

    DynInstPtr inst;
    int instNums;
    // instNums = insts[tid].size();

    // for (int i = 0; i < instNums; i++) {

    //     inst = insts[tid].at(i);
    //     if (inst->seqNum > fromCommit->commitInfo[tid].doneSeqNum) {
    //         inst->setSquashed();
    //     }
    // }

    if (compressFlag) {
        instNums = predisqueue.size();

        for (int i = 0; i < instNums; i++) {

            inst = predisqueue.front();
            predisqueue.pop_front();
            if (inst->seqNum > fromCommit->commitInfo[tid].doneSeqNum) {
                inst->setSquashed();
                ++stats.squashedInsts;
            } else {
                predisqueue.push_back(inst);
            }
        }
    } else {
        int groupNums = predisqGroups.size();
        GroupStruct group;

        for (int i = 0; i < groupNums; i++) {
            group = predisqGroups.front();
            predisqGroups.pop_front();

            while((group.size > 0) && 
                    (group.insts[group.size - 1]->seqNum >
                    fromCommit->commitInfo[tid].doneSeqNum)) {
                group.insts[group.size - 1]->setSquashed();
                group.size--;
                ++stats.squashedInsts;
            }

            if (group.size > 0) {
                predisqGroups.push_back(group);
            }
        }
    }
}

} // namespace rxuo3
} // namespace gem5
// -----------------------------------------------------------------------------------