// -- add by hongfei.liu ---------------------------------------------
#include "cpu/rxuo3/dispipe2.hh"

#include <list>

#include "cpu/rxuo3/cpu.hh"
#include "cpu/rxuo3/dyn_inst.hh"
#include "cpu/rxuo3/limits.hh"
#include "cpu/reg_class.hh"
#include "debug/RxuActivity.hh"
#include "debug/RxuO3PipeView.hh"
#include "debug/RxuDispipe2.hh"
#include "params/BaseRxuO3CPU.hh"

namespace gem5
{

namespace rxuo3
{

Dispipe2::Dispipe2(CPU *_cpu, const BaseRxuO3CPUParams &params)
    : cpu(_cpu),
      dispipe3ToDispipe2Delay(params.dispipe3ToDispipe2Delay),
      dispipe1ToDispipe2Delay(params.dispipe1ToDispipe2Delay),
      commitToDispipe2Delay(params.commitToDispipe2Delay),
      dispipe2Width(params.dispipe1Width),
      disqueueSize(params.disqueueSize),
    //   disqueueSizeBranch(params.disqueueSizeBranch),
    //   disqueueSizeIntSpecial(disqueueSize - disqueueSizeBranch),
      disqueueSizeBranch(params.disqueueSizeBranch),
      disqueueSizeIntSpecial(params.disqueueSize),
      numThreads(params.numThreads),
      stats(_cpu)
{
    if (dispipe2Width > MaxWidth)
        fatal("dispipe2Width (%d) is larger than compiled limit (%d),\n"
             "\tincrease MaxWidth in src/cpu/rxuo3/limits.hh\n",
             dispipe2Width, static_cast<int>(MaxWidth));

    for (uint32_t tid = 0; tid < MaxThreads; tid++) {
        dispipe2Status[tid] = Idle;
        stalls[tid] = {false};
    }
}

std::string
Dispipe2::name() const
{
    return cpu->name() + ".dispipe2";
}

Dispipe2::Dispipe2Stats::Dispipe2Stats(statistics::Group *parent)
    : statistics::Group(parent, "dispipe2"),
      ADD_STAT(squashCycles, statistics::units::Cycle::get(),
               "Number of cycles dispipe2 is squashing"),
      ADD_STAT(idleCycles, statistics::units::Cycle::get(),
               "Number of cycles dispipe2 is idle"),
      ADD_STAT(blockCycles, statistics::units::Cycle::get(),
               "Number of cycles dispipe2 is blocking"),
      ADD_STAT(runCycles, statistics::units::Cycle::get(),
               "Number of cycles dispipe2 is running"),
      ADD_STAT(unblockCycles, statistics::units::Cycle::get(),
               "Number of cycles dispipe2 is unblocking"),
      ADD_STAT(dispipe2Insts, statistics::units::Count::get(),
               "Number of instructions processed by dispipe2"),
      ADD_STAT(squashedInsts, statistics::units::Count::get(),
               "Number of squashed instructions processed by dispipe2"),
      ADD_STAT(DisqFullEvents, statistics::units::Count::get(),
               "Number of times dispipe1 has blocked due to Disqs full"),
      ADD_STAT(disqueue0FullEvents, statistics::units::Count::get(),
               "Number of times dispipe1 has blocked due to disqueue0 full"),
      ADD_STAT(disqueue1FullEvents, statistics::units::Count::get(),
               "Number of times dispipe1 has blocked due to disqueue1 full"),
      ADD_STAT(disqueue4FullEvents, statistics::units::Count::get(),
               "Number of times dispipe1 has blocked due to disqueue4 full"),
      ADD_STAT(disqueue5FullEvents, statistics::units::Count::get(),
               "Number of times dispipe1 has blocked due to disqueue5 full"),
      ADD_STAT(disqueue2FullEvents, statistics::units::Count::get(),
               "Number of times dispipe1 has blocked due to disqueue2 full"),
      ADD_STAT(disqueue3FullEvents, statistics::units::Count::get(),
               "Number of times dispipe1 has blocked due to disqueue3 full"),
      ADD_STAT(disqueue6FullEvents, statistics::units::Count::get(),
               "Number of times dispipe1 has blocked due to disqueue6 full"),
      ADD_STAT(disqueue7FullEvents, statistics::units::Count::get(),
               "Number of times dispipe1 has blocked due to disqueue7 full"),
      ADD_STAT(disqueue14FullEvents, statistics::units::Count::get(),
               "Number of times dispipe1 has blocked due to disqueue14 full"),
      ADD_STAT(disqueue15FullEvents, statistics::units::Count::get(),
               "Number of times dispipe1 has blocked due to disqueue15 full"),
      ADD_STAT(disqueue16FullEvents, statistics::units::Count::get(),
               "Number of times dispipe1 has blocked due to disqueue16 full"),
      ADD_STAT(disqueue17FullEvents, statistics::units::Count::get(),
               "Number of times dispipe1 has blocked due to disqueue17 full"),
      ADD_STAT(disqueue18FullEvents, statistics::units::Count::get(),
               "Number of times dispipe1 has blocked due to disqueue18 full"),
      ADD_STAT(disqueue19FullEvents, statistics::units::Count::get(),
               "Number of times dispipe1 has blocked due to disqueue19 full"),
      ADD_STAT(disqueue8FullEvents, statistics::units::Count::get(),
               "Number of times dispipe1 has blocked due to disqueue8 full"),
      ADD_STAT(disqueue9FullEvents, statistics::units::Count::get(),
               "Number of times dispipe1 has blocked due to disqueue9 full"),
      ADD_STAT(disqueue10FullEvents, statistics::units::Count::get(),
               "Number of times dispipe1 has blocked due to disqueue10 full"),
      ADD_STAT(disqueue13FullEvents, statistics::units::Count::get(),
               "Number of times dispipe1 has blocked due to disqueue13 full"),
      ADD_STAT(disqueue11FullEvents, statistics::units::Count::get(),
               "Number of times dispipe1 has blocked due to disqueue11 full"),
      ADD_STAT(disqueue12FullEvents, statistics::units::Count::get(),
               "Number of times dispipe1 has blocked due to disqueue12 full"),
      ADD_STAT(disqueue20FullEvents, statistics::units::Count::get(),
               "Number of times dispipe1 has blocked due to disqueue20 full"),
      ADD_STAT(disqueue21FullEvents, statistics::units::Count::get(),
               "Number of times dispipe1 has blocked due to disqueue21 full"),
      ADD_STAT(disqueue22FullEvents, statistics::units::Count::get(),
               "Number of times dispipe1 has blocked due to disqueue22 full"),
      ADD_STAT(disqueue23FullEvents, statistics::units::Count::get(),
               "Number of times dispipe1 has blocked due to disqueue23 full"),
      ADD_STAT(disqueue24FullEvents, statistics::units::Count::get(),
               "Number of times dispipe1 has blocked due to disqueue24 full"),
      ADD_STAT(disqueue25FullEvents, statistics::units::Count::get(),
               "Number of times dispipe1 has blocked due to disqueue25 full"),
      ADD_STAT(disqueue26FullEvents, statistics::units::Count::get(),
               "Number of times dispipe1 has blocked due to disqueue26 full"),
      ADD_STAT(disqueue27FullEvents, statistics::units::Count::get(),
               "Number of times dispipe1 has blocked due to disqueue27 full")
{
    squashCycles.prereq(squashCycles);
    idleCycles.prereq(idleCycles);
    blockCycles.prereq(blockCycles);
    runCycles.prereq(runCycles);
    unblockCycles.prereq(unblockCycles);
    dispipe2Insts.prereq(dispipe2Insts);
    squashedInsts.prereq(squashedInsts);
    DisqFullEvents.prereq(DisqFullEvents);
    disqueue0FullEvents.prereq(disqueue0FullEvents);
    disqueue1FullEvents.prereq(disqueue1FullEvents);
    disqueue4FullEvents.prereq(disqueue4FullEvents);
    disqueue5FullEvents.prereq(disqueue5FullEvents);
    disqueue2FullEvents.prereq(disqueue2FullEvents);
    disqueue3FullEvents.prereq(disqueue3FullEvents);
    disqueue6FullEvents.prereq(disqueue6FullEvents);
    disqueue7FullEvents.prereq(disqueue7FullEvents);
    disqueue14FullEvents.prereq(disqueue14FullEvents);
    disqueue15FullEvents.prereq(disqueue15FullEvents);
    disqueue16FullEvents.prereq(disqueue16FullEvents);
    disqueue17FullEvents.prereq(disqueue17FullEvents);
    disqueue18FullEvents.prereq(disqueue18FullEvents);
    disqueue19FullEvents.prereq(disqueue19FullEvents);
    disqueue8FullEvents.prereq(disqueue8FullEvents);
    disqueue9FullEvents.prereq(disqueue9FullEvents);
    disqueue10FullEvents.prereq(disqueue10FullEvents);
    disqueue13FullEvents.prereq(disqueue13FullEvents);
    disqueue11FullEvents.prereq(disqueue11FullEvents);
    disqueue12FullEvents.prereq(disqueue12FullEvents);
    disqueue20FullEvents.prereq(disqueue20FullEvents);
    disqueue21FullEvents.prereq(disqueue21FullEvents);
    disqueue22FullEvents.prereq(disqueue22FullEvents);
    disqueue23FullEvents.prereq(disqueue23FullEvents);
    disqueue24FullEvents.prereq(disqueue24FullEvents);
    disqueue25FullEvents.prereq(disqueue25FullEvents);
    disqueue26FullEvents.prereq(disqueue26FullEvents);
    disqueue27FullEvents.prereq(disqueue27FullEvents);
}

void
Dispipe2::setTimeBuffer(TimeBuffer<TimeStruct> *tb_ptr)
{
    timeBuffer = tb_ptr;

    // Setup wire to read information from time buffer, from Dispipe3 stage.
    fromDispipe3 = timeBuffer->getWire(-dispipe3ToDispipe2Delay);

    // Setup wire to read infromation from time buffer, from commit stage.
    fromCommit = timeBuffer->getWire(-commitToDispipe2Delay);

    // Setup wire to write information to previous stages.
    toDispipe1 = timeBuffer->getWire(0);
}

void
Dispipe2::setDispipe2Queue(TimeBuffer<Dispipe2Struct> *p2q_ptr)
{
    dispipe2Queue = p2q_ptr;

    // Setup wire to write information to future stages.
    toDispipe3 = dispipe2Queue->getWire(0);
}

void
Dispipe2::setDispipe1Queue(TimeBuffer<Dispipe1Struct> *p1q_ptr)
{
    dispipe1Queue = p1q_ptr;

    // Setup wire to get information from dispipe1.
    fromDispipe1 = dispipe1Queue->getWire(-dispipe1ToDispipe2Delay);
}

void
Dispipe2::startupStage()
{
    resetStage();
}

void
Dispipe2::clearStates(ThreadID tid)
{
    dispipe2Status[tid] = Idle;

    stalls[tid].dispipe3 = false;

    for (int i = 0; i < 28; i++) {
        disqs[i].clear();
        lastSendWtbEntries[i] = 0;
    }
}

void
Dispipe2::setDisqsLimits()
{
    // Index corresponding to ibuffer
    // i0  i1  i2  i3  i4  i6  ls7  ls8  f0  f1  f2  f3  f4  f5  ls3  ls4
    // 0   1   2   3   4   5   6    7    8   9   10  11  12  13  14   15
    for (int i = 0; i < 28; i++) {
        disqsLimits.out[i] = 1;
        if (i == 6 || i == 7 || i == 14 || i == 15 || i == 16 || i == 17 || i == 18 || i == 19) {
            disqsLimits.in[i] = 1;
            disqsLimits.out[i] = 1;
        } else if (i == 11 || i == 12) {
            disqsLimits.in[i] = 4;
            disqsLimits.out[i] = 1;
        } else if (i == 2 || i == 3 || i == 20 || i == 21) {
            disqsLimits.in[i] = 4;
            disqsLimits.out[i] = 2;
        } else if(i == 22 || i == 23){
            disqsLimits.in[i] = 2;
            disqsLimits.out[i] = 1;
        } else if(i == 24 || i == 25 || i == 26 || i ==27){
            disqsLimits.in[i] = 1;
            disqsLimits.out[i] = 1;
        }
        else {
            disqsLimits.in[i] = 2;
        }
    }
}

void
Dispipe2::resetStage()
{
    _status = Inactive;

    for (ThreadID tid = 0; tid < numThreads; tid++) {
        dispipe2Status[tid] = Idle;

        stalls[tid].dispipe3 = false;
    }

    for (int i = 0; i < 28; i++) {
        disqs[i].clear();
        lastSendWtbEntries[i] = 0;
    }
}

void
Dispipe2::setActiveThreads(std::list<ThreadID> *at_ptr)
{
    activeThreads = at_ptr;
}

bool
Dispipe2::isDrained() const
{   
    for (int i = 0; i < 28; i++) {
        if (!disqs[i].empty()) {
            return false;
        }
    }

    for (ThreadID tid = 0; tid < numThreads; tid++) {
        if (!insts[tid].empty() ||
            (dispipe2Status[tid] != Idle && dispipe2Status[tid] != Running))
            return false;
    }

    return true;
}

void
Dispipe2::takeOverFrom()
{
    resetStage();
}

void
Dispipe2::drainSanityCheck() const
{
    for (int i = 0; i < 28; i++) {
        assert(disqs[i].empty());
    }
    for (ThreadID tid = 0; tid < numThreads; tid++) {
        assert(insts[tid].empty());
    }
}

void
Dispipe2::squash(const InstSeqNum &squash_seq_num, ThreadID tid)
{
    DPRINTF(RxuDispipe2, "[tid:%i] [squash sn:%llu] Squashing instructions.\n",
        tid,squash_seq_num);

    if (dispipe2Status[tid] == Blocked ||
        dispipe2Status[tid] == Unblocking) {
        toDispipe1->dispipe2Unblock[tid] = true;
    }

    // Set the status to Squashing.
    dispipe2Status[tid] = Squashing;

    // Squash any instructions from dispipe1.
    for (int i = 0; i < fromDispipe1->size; i++) {
        if (fromDispipe1->insts[i]->threadNumber == tid &&
            fromDispipe1->insts[i]->seqNum > squash_seq_num) {
            if (!fromDispipe1->insts[i]->isSquashed()) {
            // if(fromDispipe1->insts[i]->isSquashed()){
            //     if(cpu->dispipe3.ldstdisq_in[fromDispipe1->insts[i]->ibuffer_id]){
            //         cpu->dispipe3.ldstdisq_in[fromDispipe1->insts[i]->ibuffer_id] = false;
            //         cpu->dispipe3.fifo_cnt++;
            //     }   
            // }
                fromDispipe1->insts[i]->setSquashed();
                rmu->dependGraph.clrrem_base(fromDispipe1->insts[i]);
                //rmu->dependGraph.clear_base(fromDispipe1->insts[i]);
                wroteToTimeBuffer = true;
                DPRINTF(RxuDispipe2,
                        "[tid:%i] "
                        "instruction %i with PC %s is squashed.\n",
                        tid, fromDispipe1->insts[i]->seqNum,
                        fromDispipe1->insts[i]->pcState());
            } else {
                DPRINTF(RxuDispipe2,
                        "[tid:%i] "
                        "instruction %i with PC %s has already been squashed before.\n",
                        tid, fromDispipe1->insts[i]->seqNum,
                        fromDispipe1->insts[i]->pcState());
            }
        }
    }

    DynInstPtr inst;
    int instNums;
    instNums = insts[tid].size();

    for (int i = 0; i < instNums; i++) {

        inst = insts[tid].front();
        insts[tid].pop_front();
        if (inst->seqNum > squash_seq_num) {
            // if(inst->isMemRef()){
            //     if(cpu->dispipe3.ldstdisq_in[inst->ibuffer_id]){
            //         cpu->dispipe3.ldstdisq_in[inst->ibuffer_id] = false;
            //         cpu->dispipe3.fifo_cnt++;
            //     }   
            // }
            if (inst->isSquashed()) {
                ++stats.squashedInsts;
                DPRINTF(RxuDispipe2,
                        "[tid:%i] "
                        "instruction %i with PC %s has already been squashed before.\n",
                        tid, inst->seqNum, inst->pcState());
            } else {
            //     if(inst->isMemRef()){
            //     if(cpu->dispipe3.ldstdisq_in[inst->ibuffer_id]){
            //         cpu->dispipe3.ldstdisq_in[inst->ibuffer_id] = false;
            //         cpu->dispipe3.fifo_cnt++;
            //     }   
            // }
                inst->setSquashed();
                rmu->dependGraph.clrrem_base(inst);
                // rmu->dependGraph.clear_base(inst);
                ++stats.squashedInsts;
                DPRINTF(RxuDispipe2,
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

    for (int i = 0; i < 28; i++) {
        int instNums_disq = disqs[i].size();
        for (int j = 0; j < instNums_disq; j++){
        //while (disqs[i].size() > 0) {
            inst = disqs[i].front();
            disqs[i].pop_front();
            if (inst->seqNum > squash_seq_num) {
                if (!inst->isSquashed()) {
                    inst->setSquashed();
                    // DPRINTF(RxuDispipe1,"%i289289289\n",tid);
                    rmu->dependGraph.clrrem_base(inst);
                    // rmu->dependGraph.clear_base(disqs[i].back());
                    DPRINTF(RxuDispipe2,
                            "[tid:%i] "
                            "instruction %i with PC %s is squashed.\n",
                            tid, inst->seqNum, inst->pcState());
                } else {
                    DPRINTF(RxuDispipe2,
                            "[tid:%i] "
                            "instruction %i with PC %s has already been squashed before.\n",
                            tid, inst->seqNum, inst->pcState());
                }

                ++stats.squashedInsts;
                if((inst->isMemRef() || inst->isReadBarrier() || inst->isWriteBarrier())){
                    disqs[i].push_back(inst);
                }
            } else {
                disqs[i].push_back(inst);
            }
        }
    }
}

void
Dispipe2::tick()
{
    wroteToTimeBuffer = false;

    blockThisCycle = false;

    bool status_change = false;

    toDispipe3Index = 0;

    DPRINTF(RxuDispipe2,
            "%d valid instructions from Dispipe1(disq) this cycle\n",
            validInsts());
    
    setDisqsLimits();
    readFreeEntries();

    // for (int i = 0; i < 28; ++i) {
    //     DPRINTF(RxuDispipe2,
    //             "%i instructions in disq[%i] before processing.\n",
    //              disqs[i].size(), i);
    // }

    sortInsts();

    std::list<ThreadID>::iterator threads = activeThreads->begin();
    std::list<ThreadID>::iterator end = activeThreads->end();

    // Check stall and squash signals.
    while (threads != end) {
        ThreadID tid = *threads++;

        DPRINTF(RxuDispipe2, "Processing thread : %i\n", tid);

        status_change = checkSignalsAndUpdate(tid) || status_change;

        dispipe2(status_change, tid);
    }

    if (status_change) {
        updateStatus();
    }

    for (int i = 0; i < 28; ++i) {
        DPRINTF(RxuDispipe2,
                "%i instructions in disq[%i] after processing.\n",
                 disqs[i].size(), i);
        
        if (i == 2 || i == 3) {
            if (disqs[i].size() >= disqueueSizeIntSpecial) {
                statDisqueueFull(i);
            }
        } else if (i == 20 || i == 21) {
            if (disqs[i].size() >= disqueueSizeBranch) {
                statDisqueueFull(i);
            }
        } else if(i == 6 || i == 7 || i == 14 || i == 15 || i == 16 || i == 17 || i == 18 || i == 19){
            if (disqs[i].size() >= 17) {
                statDisqueueFull(i);
            }
        }
        else {
            if (disqs[i].size() >= disqueueSize) {
                statDisqueueFull(i);
            }
        }
    }

    if (wroteToTimeBuffer) {
        DPRINTF(RxuActivity, "Activity this cycle.\n");
        cpu->activityThisCycle();
    }

    DPRINTF(RxuDispipe2, "Send %i instructions to dispipe3 this cycle.\n",
                                                            toDispipe3Index);
}

void
Dispipe2::dispipe2(bool &status_change, ThreadID tid)
{
    if (dispipe2Status[tid] == Blocked) {
        ++stats.blockCycles;
        blockedInsert(tid);
    } else if (dispipe2Status[tid] == Squashing) {
        ++stats.squashCycles;
        blockedInsert(tid);
    }

    if (dispipe2Status[tid] == Running ||
        dispipe2Status[tid] == Idle) {
        DPRINTF(RxuDispipe2,
                "[tid:%i] "
                "Not blocked, so attempting to run dispipe2 stage.\n",
                tid);

        insertAndSendInsts(tid);
    } else if (dispipe2Status[tid] == Unblocking) {
        DPRINTF(RxuDispipe2, "[tid:%i] Unblocking status.\n",tid);
        insertAndSendInsts(tid);

        // If we switched over to blocking, then there's a potential for
        // an overall status change.
        status_change = unblock(tid) || status_change || blockThisCycle;
    }
}

void
Dispipe2::insertAndSendInsts(ThreadID tid)
{   
    int insts_available = insts[tid].size();
    InstQueue vectorInsts;
    InstQueue scalarInsts;
    int scalar_insts_available = 0;
    int vector_insts_available = 0;
    for (int i = 0; i < insts_available; i++) {
        DynInstPtr inst = insts[tid].front();
        if (inst->isMicroVector()) {
            vectorInsts.push_back(inst);
            vector_insts_available++;
            insts[tid].pop_front();
        } else {
            scalarInsts.push_back(inst);
            scalar_insts_available++;
            insts[tid].pop_front();
        }
    }

    while (scalar_insts_available > 0) {
        std::vector<bool> disqfull = checkDisqsMoreOut();
        if (disqfull[0]) break;
        DynInstPtr inst = scalarInsts.front();
        if (inst->isSquashed() && !(inst->isMemRef() || inst->isReadBarrier() || inst->isWriteBarrier())) {
            DPRINTF(RxuDispipe2, "[tid:%i] Instruction %i with PC %s is "
                    "squashed, skipping.\n",
                    tid, inst->seqNum, inst->pcState());

            --scalar_insts_available;
            scalarInsts.pop_front();
            ++stats.squashedInsts;
            continue;
        }

        DPRINTF(RxuDispipe2,
        "[tid:%i] "
        "Inserting instruction [sn:%llu] with PC %s, ibuffer_id = %i.\n",
        tid, inst->seqNum, inst->pcState(), inst->ibuffer_id);

        --scalar_insts_available;
        scalarInsts.pop_front();

        // disqs[inst->ibuffer_id].push_back(inst);
        disqsInsert(inst);
        // --disqsLimits.in[inst->ibuffer_id];
    }

    while (vector_insts_available > 0) {
        std::vector<bool> disqfull = checkDisqsMoreOut();
        if (disqfull[1] || disqfull[2]) break;
        DynInstPtr inst = vectorInsts.front();
        if (inst->isSquashed() && !(inst->isMemRef() || inst->isReadBarrier() || inst->isWriteBarrier())) {
            DPRINTF(RxuDispipe2, "[tid:%i] Instruction %i with PC %s is "
                    "squashed, skipping.\n",
                    tid, inst->seqNum, inst->pcState());

            --vector_insts_available;
            vectorInsts.pop_front();
            ++stats.squashedInsts;
            continue;
        }

        DPRINTF(RxuDispipe2,
        "[tid:%i] "
        "Inserting instruction [sn:%llu] with PC %s, ibuffer_id = %i.\n",
        tid, inst->seqNum, inst->pcState(), inst->ibuffer_id);

        --vector_insts_available;
        vectorInsts.pop_front();

        // disqs[inst->ibuffer_id].push_back(inst);
        disqsInsert(inst);
        // --disqsLimits.in[inst->ibuffer_id];
    }


    while (!vectorInsts.empty()) {
        DynInstPtr inst = vectorInsts.front();
        insts[tid].push_back(inst);
        vectorInsts.pop_front();
    }

    while (!scalarInsts.empty()) {
        DynInstPtr inst = scalarInsts.front();
        insts[tid].push_back(inst);
        scalarInsts.pop_front();
    }

    std::vector<bool> disqfull = checkDisqsMoreOut();
    if (disqfull[0]) {
        DPRINTF(RxuDispipe2, "[tid:%i] scalar disqueue full, send stall signal to"
                " dispipe0.\n",tid);

        block(tid);
        ++stats.DisqFullEvents;

        toDispipe1->dispipe1Unblock[tid] = false;
        toDispipe1->dispipe1Block[tid] = true;
        wroteToTimeBuffer = true;
    } else {
        toDispipe1->dispipe1Unblock[tid] = true;
        toDispipe1->dispipe1Block[tid] = false;       
    }

    if (disqfull[1]) {
        DPRINTF(RxuDispipe2, "[tid:%i] vector disqueue full, send stall signal to"
                " dispipe0.\n",tid);

        block(tid);
        ++stats.DisqFullEvents;

        toDispipe1->dispipe1VecUnblock[tid] = false;
        toDispipe1->dispipe1VecBlock[tid] = true;
        wroteToTimeBuffer = true;        
    } else {
        toDispipe1->dispipe1VecUnblock[tid] = true;
        toDispipe1->dispipe1VecBlock[tid] = false;
    }

    DPRINTF(RxuDispipe2, "[tid:%i] Start Sending instruction to dispipe3.\n",tid);

    for (int i = 0; i < 28; i++) {
        if (i == 2 || i == 3) {
            int m = disqsLimits.out[i];
            for (int j = 0; j < m; j++) {
                if (disqs[i].empty()) {
                    break;
                } else {
                    DynInstPtr inst = disqs[i].front();
                    disqs[i].pop_front();
                    outSortQueue.push(inst);
                    disqsLimits.out[i]--;
                }
            }
        } else if (i == 20 || i == 21) {
            for (int j = 0; j < disqsLimits.out[i]; j++) {
                if (disqs[i].empty()) {
                    break;
                } else {
                    DynInstPtr inst = disqs[i].front();
                    disqs[i].pop_front();
                    outSortQueue.push(inst);
                }
            }
        } else {
            for (int j = 0; j < disqsLimits.out[i]; j++) {
                if (disqs[i].empty()) {
                    break;
                } else {
                    DynInstPtr inst = disqs[i].front();
                    disqs[i].pop_front();
                    outSortQueue.push(inst);
                }
            }
        }
    }

    for (int i = 0; i < 28; i++) {
        lastSendWtbEntries[i] = 0;
    }
    while (!outSortQueue.empty()) {
        DynInstPtr inst = outSortQueue.top();
        outSortQueue.pop();
        DPRINTF(RxuDispipe2, "[tid:%i] Sending instruction [sn:%lli] with "
        "PC %s, ibuffer_id = %i\n", tid, inst->seqNum, inst->pcState(), inst->ibuffer_id);
        toDispipe3->insts[toDispipe3Index] = inst;
        (toDispipe3->size)++;

        // if (inst->ibuffer_id == 20) {
        //     inst->ibuffer_id = 2;
        // } else if (inst->ibuffer_id == 21) {
        //     inst->ibuffer_id = 3;
        // }

        lastSendWtbEntries[inst->ibuffer_id]++;
        toDispipe3Index++;

#if TRACING_ON
        if (debug::RxuO3PipeView) {
            inst->dispipe1Tick = curTick() - inst->fetchTick;
        }
#endif
    }

    stats.dispipe2Insts += toDispipe3Index;

    if (toDispipe3Index == 0) {
        DPRINTF(RxuDispipe2, "[tid:%i] No inst to send.\n",tid);
        ++stats.idleCycles;
        return;
    }

    if (dispipe2Status[tid] == Running) {
        ++stats.runCycles;
    } else {
        ++stats.unblockCycles;
    }
}

void
Dispipe2::blockedInsert(ThreadID tid)
{
    int insts_available = insts[tid].size();
    InstQueue vectorInsts;
    InstQueue scalarInsts;
    int scalar_insts_available = 0;
    int vector_insts_available = 0;
    for (int i = 0; i < insts_available; i++) {
        DynInstPtr inst = insts[tid].front();
        if (inst->isMicroVector()) {
            vectorInsts.push_back(inst);
            vector_insts_available++;
            insts[tid].pop_front();
        } else {
            scalarInsts.push_back(inst);
            scalar_insts_available++;
            insts[tid].pop_front();
        }
    }

    while (scalar_insts_available > 0) {
        std::vector<bool> disqfull = checkDisqsMoreOut();
        if (disqfull[0]) break;
        DynInstPtr inst = scalarInsts.front();
        if (inst->isSquashed() && !(inst->isMemRef() || inst->isReadBarrier() || inst->isWriteBarrier())) {
            DPRINTF(RxuDispipe2, "[tid:%i] Instruction %i with PC %s is "
                    "squashed, skipping.\n",
                    tid, inst->seqNum, inst->pcState());

            --scalar_insts_available;
            scalarInsts.pop_front();
            ++stats.squashedInsts;
            continue;
        }

        DPRINTF(RxuDispipe2,
        "[tid:%i] "
        "Inserting instruction [sn:%llu] with PC %s, ibuffer_id = %i.\n",
        tid, inst->seqNum, inst->pcState(), inst->ibuffer_id);

        --scalar_insts_available;
        scalarInsts.pop_front();

        // disqs[inst->ibuffer_id].push_back(inst);
        disqsInsert(inst);
        // --disqsLimits.in[inst->ibuffer_id];
    }

    while (vector_insts_available > 0) {
        std::vector<bool> disqfull = checkDisqsMoreOut();
        if (disqfull[1]) break;
        DynInstPtr inst = vectorInsts.front();
        if (inst->isSquashed() && !(inst->isMemRef() || inst->isReadBarrier() || inst->isWriteBarrier())) {
            DPRINTF(RxuDispipe2, "[tid:%i] Instruction %i with PC %s is "
                    "squashed, skipping.\n",
                    tid, inst->seqNum, inst->pcState());

            --vector_insts_available;
            vectorInsts.pop_front();
            ++stats.squashedInsts;
            continue;
        }

        DPRINTF(RxuDispipe2,
        "[tid:%i] "
        "Inserting instruction [sn:%llu] with PC %s, ibuffer_id = %i.\n",
        tid, inst->seqNum, inst->pcState(), inst->ibuffer_id);

        --vector_insts_available;
        vectorInsts.pop_front();

        // disqs[inst->ibuffer_id].push_back(inst);
        disqsInsert(inst);
        // --disqsLimits.in[inst->ibuffer_id];
    }

    while (!vectorInsts.empty()) {
        DynInstPtr inst = vectorInsts.front();
        insts[tid].push_back(inst);
        vectorInsts.pop_front();
    }

    while (!scalarInsts.empty()) {
        DynInstPtr inst = scalarInsts.front();
        insts[tid].push_back(inst);
        scalarInsts.pop_front();
    }

    std::vector<bool> disqfull = checkDisqsFull();
    if (disqfull[0]) {
        DPRINTF(RxuDispipe2, "[tid:%i] Scalar disqueue full, send stall signal to"
                " dispipe0.\n",tid);

        ++stats.DisqFullEvents;
        block(tid);

        toDispipe1->dispipe1Unblock[tid] = false;
        toDispipe1->dispipe1Block[tid] = true;
        wroteToTimeBuffer = true;
    } else {
        toDispipe1->dispipe1Unblock[tid] = true;
        toDispipe1->dispipe1Block[tid] = false;       
    }

    if (disqfull[1]) {
        DPRINTF(RxuDispipe2, "[tid:%i] Vector disqueue full, send stall signal to"
                " dispipe0.\n",tid);

        ++stats.DisqFullEvents;
        block(tid);

        toDispipe1->dispipe1VecUnblock[tid] = false;
        toDispipe1->dispipe1VecBlock[tid] = true;
        wroteToTimeBuffer = true;
    } else {
        toDispipe1->dispipe1VecUnblock[tid] = true;
        toDispipe1->dispipe1VecBlock[tid] = false;
    }
}

std::vector<bool>
Dispipe2::checkDisqsFull()
{
    std::vector<bool> disqfull(2,false);
    for (int i = 0; i < 28; i++) {
        if (i == 2 || i == 3) {
            if (disqs[i].size() >= disqueueSizeIntSpecial) {
                disqfull[0] = true;
            }
        } else if (i == 20 || i == 21) {
            if (disqs[i].size() >= disqueueSizeBranch) {
                disqfull[0] = true;
            }
        } else if(i == 6 || i == 7 || i == 14 || i == 15 || i == 16 || i == 17 || i == 18 || i == 19){
            if (disqs[i].size() >= 17) {
                disqfull[0] = true;
            }
        } else {
            if (disqs[i].size() >= disqueueSize) {
                if (i == 22 || i == 23) {
                    disqfull[1] = true;
                } else if(i == 24 || i == 25 || i == 26 || i == 27){
                    disqfull[2] = true;
                } else {
                    disqfull[0] = true;
                }
            }
        }
    }

    return disqfull;
}

std::vector<bool>
Dispipe2::checkDisqsMoreOut()
{
    std::vector<bool> disqfull(3,false);
    for (int i = 0; i < 28; i++) {
        if (i == 2 || i == 3) {
            if (disqs[i].size() >= disqueueSizeIntSpecial + disqsLimits.out[i]) {
                disqfull[0] = true;
            }
        } else if (i == 20 || i == 21) {
            if (disqs[i].size() >= disqueueSizeBranch + disqsLimits.out[i]) {
                disqfull[0] = true;
            }
        } else if(i == 6 || i == 7 || i == 14 || i == 15 || i == 16 || i == 17 || i == 18 || i == 19){
            if (disqs[i].size() >= 17+disqsLimits.out[i]) {
                disqfull[0] = true;
            }
        } else {
            if (disqs[i].size() >= disqueueSize + disqsLimits.out[i]) {
                if (i == 22 || i == 23) {
                    disqfull[1] = true;
                } 
                else if(i == 24 || i == 25 || i == 26 || i == 27){
                    disqfull[2] = true;
                }else {
                    disqfull[0] = true;                    
                }
            }
        }
    }
    return disqfull;
}

void
Dispipe2::processInsts(ThreadID tid)
{
    int insts_available = insts[tid].size();

    if (insts_available == 0) {
        DPRINTF(RxuDispipe2, "[tid:%i] No inst need to process.\n",
                tid);
        ++stats.idleCycles;
        return;
    } else if (dispipe2Status[tid] == Unblocking) {
        ++stats.unblockCycles;
    } else {
        ++stats.runCycles;
    }

    InstQueue &insts_to_dispipe3 = insts[tid];

    DPRINTF(RxuDispipe2,
            "[tid:%i] "
            "%i available instructions to send dispipe3.\n",
            tid, insts_available);

    int processed_insts = 0;

    while (insts_available > 0  &&  toDispipe3Index < dispipe2Width) {

        assert(!insts_to_dispipe3.empty());

        DynInstPtr inst = insts_to_dispipe3.front();

        insts_to_dispipe3.pop_front();

        if (inst->isSquashed() && !(inst->isMemRef() || inst->isReadBarrier() || inst->isWriteBarrier())) {
            DPRINTF(RxuDispipe2,
                    "[tid:%i] "
                    "instruction %i with PC %s is squashed, skipping.\n",
                    tid, inst->seqNum, inst->pcState());

            --insts_available;
            ++stats.squashedInsts;

            continue;
        }

        DPRINTF(RxuDispipe2, "[tid:%i] Sending instruction [sn:%lli] with "
                "PC %s, ibuffer_id = %i\n",
                tid, inst->seqNum, inst->pcState(), inst->ibuffer_id);

        ++processed_insts;

        // Put instruction in dispipe2 queue.
        toDispipe3->insts[toDispipe3Index] = inst;
        toDispipe1->Dispipe2Info->wtbInFlight[inst->ibuffer_id]++;
        ++(toDispipe3->size);

        // Increment which instruction we're on.
        ++toDispipe3Index;

        // Decrement how many instructions are available.
        --insts_available;

#if TRACING_ON
        if (debug::RxuO3PipeView) {
            inst->dispipe2Tick = curTick() - inst->fetchTick;
        }
#endif
    }

    // If we wrote to the time buffer, record this.
    if (toDispipe3Index) {
        wroteToTimeBuffer = true;
    }

    stats.dispipe2Insts += processed_insts;

    // Check if there's any instructions left that haven't yet been sent.
    // If so then block.
    if (insts_available) {
        blockThisCycle = true;
    }

    if (blockThisCycle) {
        block(tid);
        toDispipe1->dispipe2Unblock[tid] = false;
    }
}

void
Dispipe2::sortInsts()
{
    int insts_from_dispipe1 = fromDispipe1->size;
    for (int i = 0; i < insts_from_dispipe1; ++i) {
        const DynInstPtr &inst = fromDispipe1->insts[i];
        // disqs[inst->threadNumber][inst->ibuffer_id].push_back(inst);
        insts[inst->threadNumber].push_back(inst);
    }
}

void
Dispipe2::updateStatus()
{
    bool any_unblocking = false;

    std::list<ThreadID>::iterator threads = activeThreads->begin();
    std::list<ThreadID>::iterator end = activeThreads->end();

    while (threads != end) {
        ThreadID tid = *threads++;

        if (dispipe2Status[tid] == Unblocking) {
            any_unblocking = true;
            break;
        }
    }

    // Dispipe2 will have activity if it's unblocking.
    if (any_unblocking) {
        if (_status == Inactive) {
            _status = Active;

            DPRINTF(RxuActivity, "Activating stage.\n");

            cpu->activateStage(CPU::Dispipe2Idx);
        }
    } else {
        // If it's not unblocking, then dispipe2 will not have any internal
        // activity.  Switch it to inactive.
        if (_status == Active) {
            _status = Inactive;
            DPRINTF(RxuActivity, "Deactivating stage.\n");

            cpu->deactivateStage(CPU::Dispipe2Idx);
        }
    }
}

bool
Dispipe2::block(ThreadID tid)
{
    DPRINTF(RxuDispipe2,
            "[tid:%i] Dispipe2 switches to Blocked.\n", tid);

    if (dispipe2Status[tid] != Blocked) {
        // Set the status to Blocked.
        dispipe2Status[tid] = Blocked;

        toDispipe1->dispipe2Unblock[tid] = false;
        toDispipe1->dispipe2Block[tid] = true;
        wroteToTimeBuffer = true;

        return true;
    }

    return false;
}

bool
Dispipe2::unblock(ThreadID tid)
{
    DPRINTF(RxuDispipe2, "[tid:%i] Trying to unblock.\n", tid);

    std::vector<bool> disqfull = checkDisqsFull();
    if (disqfull[0] || disqfull[1]) {
        return false;
    }

    if (insts[tid].size() < dispipe2Width) {

        DPRINTF(RxuDispipe2, "[tid:%i] Done unblocking.\n", tid);

        toDispipe1->dispipe1Unblock[tid] = true;
        toDispipe1->dispipe1Block[tid] = false;
        toDispipe1->dispipe1VecUnblock[tid] = true;
        toDispipe1->dispipe1VecBlock[tid] = false;
        wroteToTimeBuffer = true;

        dispipe2Status[tid] = Running;
        return true;
    }

    return false;
}

unsigned
Dispipe2::validInsts()
{
    unsigned inst_count = 0;

    for (int i = 0; i < fromDispipe1->size; i++) {
        if (!fromDispipe1->insts[i]->isSquashed() || fromDispipe1->insts[i]->isMemRef() 
        || fromDispipe1->insts[i]->isReadBarrier() || fromDispipe1->insts[i]->isReadBarrier())
            inst_count++;
    }

    return inst_count;
}

void
Dispipe2::readStallSignals(ThreadID tid)
{
    for (int i = 0; i < 28; i++) {
        
        if(i == 6 || i == 7 || i == 14 || i == 15 || i == 16 || i == 17 || i == 18 || i == 19){
            if(disqs[i].size() >= 17){
                disqsLimits.out[i] = 0;
            }
        } 
        else if (wtbFreeEntries[i] < disqsLimits.out[i]) {
           
            disqsLimits.out[i] = wtbFreeEntries[i];
        }
    }

    for (int i = 0; i < 28; i++) {
        if (disqsLimits.out[i]) {
            break;
        }
        if (i == 28 && disqsLimits.out[i] == 0) {
            stalls[tid].dispipe3 = true;
            DPRINTF(RxuDispipe2,
                    "[tid:%i] Stall due to all ibuffers are full.\n", tid);
            return;
        }
    }

    stalls[tid].dispipe3 = false;
}

bool
Dispipe2::checkStall(ThreadID tid)
{
    bool ret_val = false;

    if (stalls[tid].dispipe3) {
        ret_val = true;
    }

    return ret_val;
}

void
Dispipe2::readFreeEntries()
{
    for (int i = 0; i < 28; i++) {
        // DPRINTF(RxuDispipe1, "fromDispipe3->Dispipe3Info->wtbFreeEntries[%i]:%i,lastSendWtbEntries[%i],fromDispipe2->Dispipe2Info->wtbInFlight[%i]\n",i,wtbFreeEntries[i],lastSendWtbEntries[i],fromDispipe2->Dispipe2Info->wtbInFlight[i]);
        if(i != 6 && i != 7 && i != 14 && i != 15 && i != 16 && i != 17 && i != 19 && i != 18){
            wtbFreeEntries[i] = fromDispipe3->Dispipe3Info->wtbFreeEntries[i]
                            - lastSendWtbEntries[i] ?
                            fromDispipe3->Dispipe3Info->wtbFreeEntries[i]
                            - lastSendWtbEntries[i]: 0;
        }
        else {
            wtbFreeEntries[i] = 17-disqs[i].size()- lastSendWtbEntries[i]
                            - fromDispipe3->Dispipe3Info->wtbInFlight[i] > 0 ?
                            17-disqs[i].size()- lastSendWtbEntries[i]
                            - fromDispipe3->Dispipe3Info->wtbInFlight[i] : 0;
        }
        // wtbFreeEntries[i] = fromDispipe3->Dispipe3Info->wtbFreeEntries[i]
        //                     - lastSendWtbEntries[i]
        //                     - fromDispipe2->Dispipe2Info->wtbInFlight[i];
        // DPRINTF(RxuDispipe1, "fromDispipe3->Dispipe3Info->wtbFreeEntries[%i]:%i,lastSendWtbEntries[%i],fromDispipe2->Dispipe2Info->wtbInFlight[%i]\n",i,wtbFreeEntries[i],lastSendWtbEntries[i],fromDispipe2->Dispipe2Info->wtbInFlight[i]);
    }
}

bool
Dispipe2::checkSignalsAndUpdate(ThreadID tid)
{
    readStallSignals(tid);

    if (fromCommit->commitInfo[tid].squash) {
        DPRINTF(RxuDispipe2, "[tid:%i] Squashing instructions due to squash from "
                "commit.\n", tid);

        squash(fromCommit->commitInfo[tid].doneSeqNum, tid);

        return true;
    }

    if (checkStall(tid)) {
        return block(tid);
    }

    if (dispipe2Status[tid] == Blocked) {
        DPRINTF(RxuDispipe2, "[tid:%i] Done blocking, switching to unblocking.\n",
                tid);

        dispipe2Status[tid] = Unblocking;

        unblock(tid);

        return true;
    }

    if (dispipe2Status[tid] == Squashing) {
        DPRINTF(RxuDispipe2, "[tid:%i] Done squashing, switching to running.\n",
                tid);
        dispipe2Status[tid] = Running;
        return false;
    }

    // If we've reached this point, we have not gotten any signals that
    // cause dispipe2 to change its status.  Dispipe2 remains the same as before.
    return false;
}

void
Dispipe2::statDisqueueFull(int ibuffer_id)
{
    switch (ibuffer_id)
    {
    case 0:
        ++stats.disqueue0FullEvents;
        break;
    case 1:
        ++stats.disqueue1FullEvents;
        break;
    case 2:
        ++stats.disqueue2FullEvents;
        break;
    case 3:
        ++stats.disqueue3FullEvents;
        break;
    case 4:
        ++stats.disqueue4FullEvents;
        break;
    case 5:
        ++stats.disqueue5FullEvents;
        break;
    case 6:
        ++stats.disqueue6FullEvents;
        break;
    case 7:
        ++stats.disqueue7FullEvents;
        break;
    case 8:
        ++stats.disqueue8FullEvents;
        break;
    case 9:
        ++stats.disqueue9FullEvents;
        break;
    case 10:
        ++stats.disqueue10FullEvents;
        break;
    case 11:
        ++stats.disqueue11FullEvents;
        break;
    case 12:
        ++stats.disqueue12FullEvents;
        break;
    case 13:
        ++stats.disqueue13FullEvents;
        break;
    case 14:
        ++stats.disqueue14FullEvents;
        break;
    case 15:
        ++stats.disqueue15FullEvents;
        break;
    case 16:
        ++stats.disqueue16FullEvents;
        break;
    case 17:
        ++stats.disqueue17FullEvents;
        break;
    case 18:
        ++stats.disqueue18FullEvents;
        break;
    case 19:
        ++stats.disqueue19FullEvents;
        break;
    case 20:
        ++stats.disqueue20FullEvents;
        break;
    case 21:
        ++stats.disqueue21FullEvents;
        break;
    case 22:
        ++stats.disqueue22FullEvents;
        break;
    case 23:
        ++stats.disqueue23FullEvents;
        break;
    case 24:
        ++stats.disqueue24FullEvents;
        break;
    case 25:
        ++stats.disqueue25FullEvents;
        break;
    case 26:
        ++stats.disqueue26FullEvents;
        break;
    case 27:
        ++stats.disqueue27FullEvents;
        break;
    
    default:
        break;
    }
}

bool
Dispipe2::PqCompare::operator()(
        const DynInstPtr &lhs, const DynInstPtr &rhs) const
{
    return lhs->seqNum > rhs->seqNum;
}

void
Dispipe2::disqsInsert(DynInstPtr inst)
{
    // Index corresponding to ibuffer
    // i0  i1  i2  i3  i4  i6  ls7  ls8  f0  f1  f2  f3  f4  f5
    // 0   1   2   3   4   5   6    7    8   9   10  11  12  13

    disqs[inst->ibuffer_id].push_back(inst);
    // if(inst->isStore()){
    //     if(disqsLimits.out[store_inNormal_cnt] > 0){
    //         disqsLimits.out[store_inNormal_cnt]--;
    //     }else if(disqsLimits.out[0] > 0){
    //         disqsLimits.out[0]--;
    //     }else if(disqsLimits.out[1] > 0){
    //         disqsLimits.out[1]--;
    //     }else if(disqsLimits.out[2] > 0){
    //         disqsLimits.out[4]--;
    //     }else if(disqsLimits.out[3] > 0){
    //         disqsLimits.out[5]--;
    //     }
    //     if(store_inNormal_cnt == 0){
    //         store_inNormal_cnt == 1;
    //     }
    //     else if(store_inNormal_cnt == 1){
    //         store_inNormal_cnt == 2;
    //     }
    //     else if(store_inNormal_cnt == 2){
    //         store_inNormal_cnt == 3;
    //     }
    //     else if(store_inNormal_cnt == 3){
    //         store_inNormal_cnt == 0;
    //     }
    // }
}


} // namespace rxuo3
} // namespace gem5
