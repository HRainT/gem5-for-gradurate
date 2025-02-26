// -- add by hongfei.liu ---------------------------------------------
#include "cpu/rxuo3/dispipe1.hh"

#include <list>

#include "cpu/rxuo3/cpu.hh"
#include "cpu/rxuo3/dyn_inst.hh"
#include "cpu/rxuo3/limits.hh"
#include "cpu/reg_class.hh"
#include "debug/RxuActivity.hh"
#include "debug/RxuO3PipeView.hh"
#include "debug/RxuDispipe1.hh"
#include "params/BaseRxuO3CPU.hh"

namespace gem5
{

namespace rxuo3
{

Dispipe1::Dispipe1(CPU *_cpu, const BaseRxuO3CPUParams &params)
    : cpu(_cpu),
      dispipe2ToDispipe1Delay(params.dispipe2ToDispipe1Delay),
      dispipe3ToDispipe1Delay(params.dispipe3ToDispipe1Delay),
      dispipe0ToDispipe1Delay(params.dispipe0ToDispipe1Delay),
      commitToDispipe1Delay(params.commitToDispipe1Delay),
      dispipe1Width(params.dispipe1Width),
      disqueueSize(params.disqueueSize),
    //   disqueueSizeBranch(params.disqueueSizeBranch),
    //   disqueueSizeIntSpecial(disqueueSize - disqueueSizeBranch),
      disqueueSizeBranch(params.disqueueSizeBranch),
      disqueueSizeIntSpecial(params.disqueueSize),
      numThreads(params.numThreads),
      stats(_cpu)
{
    if (dispipe1Width > MaxWidth)
        fatal("dispipe1Width (%d) is larger than compiled limit (%d),\n"
             "\tincrease MaxWidth in src/cpu/rxuo3/limits.hh\n",
             dispipe1Width, static_cast<int>(MaxWidth));

    for (uint32_t tid = 0; tid < MaxThreads; tid++) {
        dispipe1Status[tid] = Idle;
        stalls[tid] = {false};
    }
}

std::string
Dispipe1::name() const
{
    return cpu->name() + ".dispipe1";
}

Dispipe1::Dispipe1Stats::Dispipe1Stats(statistics::Group *parent)
    : statistics::Group(parent, "dispipe1"),
      ADD_STAT(squashCycles, statistics::units::Cycle::get(),
               "Number of cycles dispipe1 is squashing"),
      ADD_STAT(idleCycles, statistics::units::Cycle::get(),
               "Number of cycles dispipe1 is idle"),
      ADD_STAT(blockCycles, statistics::units::Cycle::get(),
               "Number of cycles dispipe1 is blocking"),
      ADD_STAT(runCycles, statistics::units::Cycle::get(),
               "Number of cycles dispipe1 is running"),
      ADD_STAT(unblockCycles, statistics::units::Cycle::get(),
               "Number of cycles dispipe1 is unblocking"),
      ADD_STAT(dispipe1Insts, statistics::units::Count::get(),
               "Number of instructions processed by dispipe1"),
      ADD_STAT(squashedInsts, statistics::units::Count::get(),
               "Number of squashed instructions processed by dispipe1"),
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
               "Number of times dispipe1 has blocked due to disqueue23 full")
{
    squashCycles.prereq(squashCycles);
    idleCycles.prereq(idleCycles);
    blockCycles.prereq(blockCycles);
    runCycles.prereq(runCycles);
    unblockCycles.prereq(unblockCycles);
    dispipe1Insts.prereq(dispipe1Insts);
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
}

void
Dispipe1::setTimeBuffer(TimeBuffer<TimeStruct> *tb_ptr)
{
    timeBuffer = tb_ptr;

    // Setup wire to read information from time buffer, from dispipe2 stage.
    fromDispipe2 = timeBuffer->getWire(-dispipe2ToDispipe1Delay);

    // Setup wire to read infromation from time buffer, from dispipe3 stage.
    fromDispipe3 = timeBuffer->getWire(-dispipe3ToDispipe1Delay);

    // Setup wire to read infromation from time buffer, from commit stage.
    fromCommit = timeBuffer->getWire(-commitToDispipe1Delay);

    // Setup wire to write information to previous stages.
    toDispipe0 = timeBuffer->getWire(0);
}

void
Dispipe1::setDispipe1Queue(TimeBuffer<Dispipe1Struct> *p1q_ptr)
{
    dispipe1Queue = p1q_ptr;

    // Setup wire to write information to future stages.
    toDispipe2 = dispipe1Queue->getWire(0);
}

void
Dispipe1::setDispipe0Queue(TimeBuffer<Dispipe0Struct> *p0q_ptr)
{
    dispipe0Queue = p0q_ptr;

    // Setup wire to get information from predisq.
    fromDispipe0 = dispipe0Queue->getWire(-dispipe0ToDispipe1Delay);
}

void
Dispipe1::setDispipe2Queue(TimeBuffer<Dispipe2Struct> *p2q_ptr)
{
    dispipe2Queue = p2q_ptr;
}

void
Dispipe1::startupStage()
{
    resetStage();
}

void
Dispipe1::clearStates(ThreadID tid)
{
    dispipe1Status[tid] = Idle;

    stalls[tid] = {false};

    for (int i = 0; i < 24; i++) {
        disqs[i].clear();
        lastSendWtbEntries[i] = 0;
    }
}

void
Dispipe1::setDisqsLimits()
{
    // Index corresponding to ibuffer
    // i0  i1  i2  i3  i4  i6  ls7  ls8  f0  f1  f2  f3  f4  f5  ls3  ls4
    // 0   1   2   3   4   5   6    7    8   9   10  11  12  13  14   15
    for (int i = 0; i < 24; i++) {
        disqsLimits.out[i] = 1;
        if (i == 6 || i == 7 || i == 14 || i == 15 || i == 16 || i == 17 || i == 18 || i == 19) {
            disqsLimits.in[i] = 8;
            disqsLimits.out[i] = 1;
        } else if (i == 11 || i == 12) {
            disqsLimits.in[i] = 8;
            disqsLimits.out[i] = 1;
        } else if (i == 2 || i == 3 || i == 20 || i == 21) {
            disqsLimits.in[i] = 8;
            disqsLimits.out[i] = 1;
        } else if(i == 22 || i == 23){
            disqsLimits.in[i] = 8;
            disqsLimits.out[i] = 4;
        }
        else {
            disqsLimits.in[i] = 2;
        }
    }
}

void
Dispipe1::resetStage()
{
    _status = Inactive;

    for (ThreadID tid = 0; tid < numThreads; tid++) {
        dispipe1Status[tid] = Idle;

        stalls[tid] = {false};
    }

    for (int i = 0; i < 24; i++) {
        disqs[i].clear();
        lastSendWtbEntries[i] = 0;
    }
}

void
Dispipe1::setActiveThreads(std::list<ThreadID> *at_ptr)
{
    activeThreads = at_ptr;
}

bool
Dispipe1::isDrained() const
{
    for (int i = 0; i < 24; i++) {
        if (!disqs[i].empty()) {
            return false;
        }
    }
    for (ThreadID tid = 0; tid < numThreads; tid++) {
        if (!insts[tid].empty() ||
            (dispipe1Status[tid] != Idle && dispipe1Status[tid] != Running))
            return false;
    }
    return true;
}

void
Dispipe1::takeOverFrom()
{
    resetStage();
}

void
Dispipe1::drainSanityCheck() const
{
    for (int i = 0; i < 24; i++) {
        assert(disqs[i].empty());
    }
    for (ThreadID tid = 0; tid < numThreads; tid++) {
        assert(insts[tid].empty());
    }
}

void
Dispipe1::squash(const InstSeqNum &squash_seq_num, ThreadID tid)
{
    DPRINTF(RxuDispipe1, "[tid:%i] [squash sn:%llu] Squashing instructions.\n",
        tid,squash_seq_num);

    if (dispipe1Status[tid] == Blocked ||
        dispipe1Status[tid] == Unblocking) {
        toDispipe0->dispipe1Unblock[tid] = true;
        toDispipe0->dispipe1VecUnblock[tid] = true;
    }

    // Set the status to Squashing.
    dispipe1Status[tid] = Squashing;

    // Squash any instructions from dispipe0.
    for (int i = 0; i < fromDispipe0->size; i++) {
        if (fromDispipe0->insts[i]->threadNumber == tid &&
            fromDispipe0->insts[i]->seqNum > squash_seq_num) {
            // if(fromDispipe0->insts[i]->isMemRef()){
            //     if(cpu->dispipe3.ldstdisq_in[fromDispipe0->insts[i]->ibuffer_id]){
            //         cpu->dispipe3.ldstdisq_in[fromDispipe0->insts[i]->ibuffer_id] = false;
            //         cpu->dispipe3.fifo_cnt++;
            //     }   
            // }
            if (!fromDispipe0->insts[i]->isSquashed()) {
                // if(fromDispipe0->insts[i]->isMemRef()){
                //     if(cpu->dispipe3.ldstdisq_in[fromDispipe0->insts[i]->ibuffer_id]){
                //         cpu->dispipe3.ldstdisq_in[fromDispipe0->insts[i]->ibuffer_id] = false;
                //         cpu->dispipe3.fifo_cnt++;
                //     }   
                // }
                fromDispipe0->insts[i]->setSquashed();
                rmu->dependGraph.clrrem_base(fromDispipe0->insts[i]);
                //rmu->dependGraph.clear_base(fromDispipe0->insts[i]);
                wroteToTimeBuffer = true;
                DPRINTF(RxuDispipe1,
                        "[tid:%i] "
                        "instruction %i with PC %s is squashed.\n",
                        tid, fromDispipe0->insts[i]->seqNum,
                        fromDispipe0->insts[i]->pcState());
            } else {
                DPRINTF(RxuDispipe1,
                        "[tid:%i] "
                        "instruction %i with PC %s has already been squashed before.\n",
                        tid, fromDispipe0->insts[i]->seqNum,
                        fromDispipe0->insts[i]->pcState());
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
                DPRINTF(RxuDispipe1,
                        "[tid:%i] "
                        "instruction %i with PC %s has already been squashed before.\n",
                        tid, inst->seqNum, inst->pcState());
            } else {
                // if(fromDispipe0->insts[i]->isMemRef()){
                //     if(cpu->dispipe3.ldstdisq_in[fromDispipe0->insts[i]->ibuffer_id]){
                //         cpu->dispipe3.ldstdisq_in[fromDispipe0->insts[i]->ibuffer_id] = false;
                //         cpu->dispipe3.fifo_cnt++;
                //     }   
                // }
                inst->setSquashed();
                rmu->dependGraph.clrrem_base(inst);
                // rmu->dependGraph.clear_base(inst);
                ++stats.squashedInsts;
                DPRINTF(RxuDispipe1,
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

    for (int i = 0; i < 24; i++) {
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
                    DPRINTF(RxuDispipe1,
                            "[tid:%i] "
                            "instruction %i with PC %s is squashed.\n",
                            tid, inst->seqNum, inst->pcState());
                } else {
                    DPRINTF(RxuDispipe1,
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
Dispipe1::tick()
{
    wroteToTimeBuffer = false;

    blockThisCycle = false;

    bool status_change = false;

    toDispipe2Index = 0;

    setDisqsLimits();
    readFreeEntries();

    DPRINTF(RxuDispipe1,
            "%d valid instructions from Dispipe0 this cycle.\n", validInsts());

    for (int i = 0; i < 24; ++i) {
        DPRINTF(RxuDispipe1,
                "%i instructions in disq[%i] before processing.\n",
                 disqs[i].size(), i);
    }

    sortInsts();

    std::list<ThreadID>::iterator threads = activeThreads->begin();
    std::list<ThreadID>::iterator end = activeThreads->end();

    // Check stall and squash signals.
    while (threads != end) {
        ThreadID tid = *threads++;

        DPRINTF(RxuDispipe1, "Processing thread : %i\n", tid);

        status_change = checkSignalsAndUpdate(tid) || status_change;

        dispipe1(status_change, tid);
    }

    if (status_change) {
        updateStatus();
    }

    for (int i = 0; i < 24; ++i) {
        DPRINTF(RxuDispipe1,
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

    DPRINTF(RxuDispipe1, "Send %i instructions to dispipe2 this cycle.\n",
                                                            toDispipe2Index);
}

void
Dispipe1::dispipe1(bool &status_change, ThreadID tid)
{
    if (dispipe1Status[tid] == Blocked) {
        ++stats.blockCycles;
        blockedInsert(tid);
    } else if (dispipe1Status[tid] == Squashing) {
        ++stats.squashCycles;
        blockedInsert(tid);
    }

    if (dispipe1Status[tid] == Running ||
        dispipe1Status[tid] == Idle) {

        DPRINTF(RxuDispipe1, "[tid:%i] Not blocked, so attempting to run dispipe1 "
                "stage.\n",tid);
        insertAndSendInsts(tid);

    } else if (dispipe1Status[tid] == Unblocking) {

        DPRINTF(RxuDispipe1, "[tid:%i] Unblocking, send instructions to"
                " dispipe2 but dispipe0 still is stall.\n",tid);
        insertAndSendInsts(tid);

        // If we switched over to blocking, then there's a potential for
        // an overall status change.
        status_change = unblock(tid) || status_change || blockThisCycle;
    }
}

void
Dispipe1::insertAndSendInsts(ThreadID tid)
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
            DPRINTF(RxuDispipe1, "[tid:%i] Instruction %i with PC %s is "
                    "squashed, skipping.\n",
                    tid, inst->seqNum, inst->pcState());

            --scalar_insts_available;
            scalarInsts.pop_front();
            ++stats.squashedInsts;
            continue;
        }

        DPRINTF(RxuDispipe1,
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
            DPRINTF(RxuDispipe1, "[tid:%i] Instruction %i with PC %s is "
                    "squashed, skipping.\n",
                    tid, inst->seqNum, inst->pcState());

            --vector_insts_available;
            vectorInsts.pop_front();
            ++stats.squashedInsts;
            continue;
        }

        DPRINTF(RxuDispipe1,
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
        DPRINTF(RxuDispipe1, "[tid:%i] scalar disqueue full, send stall signal to"
                " dispipe0.\n",tid);

        block(tid);
        ++stats.DisqFullEvents;

        toDispipe0->dispipe1Unblock[tid] = false;
        toDispipe0->dispipe1Block[tid] = true;
        wroteToTimeBuffer = true;
    } else {
        toDispipe0->dispipe1Unblock[tid] = true;
        toDispipe0->dispipe1Block[tid] = false;       
    }

    if (disqfull[1]) {
        DPRINTF(RxuDispipe1, "[tid:%i] vector disqueue full, send stall signal to"
                " dispipe0.\n",tid);

        block(tid);
        ++stats.DisqFullEvents;

        toDispipe0->dispipe1VecUnblock[tid] = false;
        toDispipe0->dispipe1VecBlock[tid] = true;
        wroteToTimeBuffer = true;        
    } else {
        toDispipe0->dispipe1VecUnblock[tid] = true;
        toDispipe0->dispipe1VecBlock[tid] = false;
    }

    DPRINTF(RxuDispipe1, "[tid:%i] Start Sending instruction to dispipe2.\n",tid);

    for (int i = 0; i < 24; i++) {
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

    for (int i = 0; i < 24; i++) {
        lastSendWtbEntries[i] = 0;
    }
    while (!outSortQueue.empty()) {
        DynInstPtr inst = outSortQueue.top();
        outSortQueue.pop();
        DPRINTF(RxuDispipe1, "[tid:%i] Sending instruction [sn:%lli] with "
        "PC %s, ibuffer_id = %i\n", tid, inst->seqNum, inst->pcState(), inst->ibuffer_id);
        toDispipe2->insts[toDispipe2Index] = inst;
        (toDispipe2->size)++;

        // if (inst->ibuffer_id == 20) {
        //     inst->ibuffer_id = 2;
        // } else if (inst->ibuffer_id == 21) {
        //     inst->ibuffer_id = 3;
        // }

        lastSendWtbEntries[inst->ibuffer_id]++;
        toDispipe2Index++;

#if TRACING_ON
        if (debug::RxuO3PipeView) {
            inst->dispipe1Tick = curTick() - inst->fetchTick;
        }
#endif
    }

    stats.dispipe1Insts += toDispipe2Index;

    if (toDispipe2Index == 0) {
        DPRINTF(RxuDispipe1, "[tid:%i] No inst to send.\n",tid);
        ++stats.idleCycles;
        return;
    }

    if (dispipe1Status[tid] == Running) {
        ++stats.runCycles;
    } else {
        ++stats.unblockCycles;
    }
}

void
Dispipe1::blockedInsert(ThreadID tid)
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
            DPRINTF(RxuDispipe1, "[tid:%i] Instruction %i with PC %s is "
                    "squashed, skipping.\n",
                    tid, inst->seqNum, inst->pcState());

            --scalar_insts_available;
            scalarInsts.pop_front();
            ++stats.squashedInsts;
            continue;
        }

        DPRINTF(RxuDispipe1,
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
            DPRINTF(RxuDispipe1, "[tid:%i] Instruction %i with PC %s is "
                    "squashed, skipping.\n",
                    tid, inst->seqNum, inst->pcState());

            --vector_insts_available;
            vectorInsts.pop_front();
            ++stats.squashedInsts;
            continue;
        }

        DPRINTF(RxuDispipe1,
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
        DPRINTF(RxuDispipe1, "[tid:%i] Scalar disqueue full, send stall signal to"
                " dispipe0.\n",tid);

        ++stats.DisqFullEvents;
        block(tid);

        toDispipe0->dispipe1Unblock[tid] = false;
        toDispipe0->dispipe1Block[tid] = true;
        wroteToTimeBuffer = true;
    } else {
        toDispipe0->dispipe1Unblock[tid] = true;
        toDispipe0->dispipe1Block[tid] = false;       
    }

    if (disqfull[1]) {
        DPRINTF(RxuDispipe1, "[tid:%i] Vector disqueue full, send stall signal to"
                " dispipe0.\n",tid);

        ++stats.DisqFullEvents;
        block(tid);

        toDispipe0->dispipe1VecUnblock[tid] = false;
        toDispipe0->dispipe1VecBlock[tid] = true;
        wroteToTimeBuffer = true;
    } else {
        toDispipe0->dispipe1VecUnblock[tid] = true;
        toDispipe0->dispipe1VecBlock[tid] = false;
    }
}

std::vector<bool>
Dispipe1::checkDisqsFull()
{
    std::vector<bool> disqfull(2,false);
    for (int i = 0; i < 24; i++) {
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
                } else {
                    disqfull[0] = true;
                }
            }
        }
    }

    return disqfull;
}

std::vector<bool>
Dispipe1::checkDisqsMoreOut()
{
    std::vector<bool> disqfull(2,false);
    for (int i = 0; i < 24; i++) {
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
                } else {
                    disqfull[0] = true;                    
                }
            }
        }
    }

    return disqfull;
}

void
Dispipe1::sortInsts()
{
    int insts_from_dispipe0 = fromDispipe0->size;
    for (int i = 0; i < insts_from_dispipe0; ++i) {
        const DynInstPtr &inst = fromDispipe0->insts[i];
        insts[inst->threadNumber].push_back(inst);
    }
}

void
Dispipe1::updateStatus()
{
    bool any_unblocking = false;

    std::list<ThreadID>::iterator threads = activeThreads->begin();
    std::list<ThreadID>::iterator end = activeThreads->end();

    while (threads != end) {
        ThreadID tid = *threads++;

        if (dispipe1Status[tid] == Unblocking) {
            any_unblocking = true;
            break;
        }
    }

    // Dispipe1 will have activity if it's unblocking.
    if (any_unblocking) {
        if (_status == Inactive) {
            _status = Active;

            DPRINTF(RxuActivity, "Activating stage.\n");

            cpu->activateStage(CPU::Dispipe1Idx);
        }
    } else {
        // If it's not unblocking, then dispipe1 will not have any internal
        // activity.  Switch it to inactive.
        if (_status == Active) {
            _status = Inactive;
            DPRINTF(RxuActivity, "Deactivating stage.\n");

            cpu->deactivateStage(CPU::Dispipe1Idx);
        }
    }
}

bool
Dispipe1::block(ThreadID tid)
{
    DPRINTF(RxuDispipe1, "[tid:%i] Dispipe1 switchs to blocked status.\n", tid);

    if (dispipe1Status[tid] != Blocked) {
        // Set the status to Blocked.
        dispipe1Status[tid] = Blocked;

        return true;
    }

    return false;
}

bool
Dispipe1::unblock(ThreadID tid)
{
    DPRINTF(RxuDispipe1, "[tid:%i] Trying to unblock.\n", tid);

    std::vector<bool> disqfull = checkDisqsFull();
    if (disqfull[0] || disqfull[1]) {
        return false;
    }

    if (insts[tid].size() < dispipe1Width) {

        DPRINTF(RxuDispipe1, "[tid:%i] Done unblocking.\n", tid);

        toDispipe0->dispipe1Unblock[tid] = true;
        toDispipe0->dispipe1Block[tid] = false;
        toDispipe0->dispipe1VecUnblock[tid] = true;
        toDispipe0->dispipe1VecBlock[tid] = false;
        wroteToTimeBuffer = true;

        dispipe1Status[tid] = Running;
        return true;
    }

    return false;
}

unsigned
Dispipe1::validInsts()
{
    unsigned inst_count = 0;

    for (int i = 0; i < fromDispipe0->size; i++) {
        if (!fromDispipe0->insts[i]->isSquashed() || fromDispipe0->insts[i]->isMemRef()
        || fromDispipe0->insts[i]->isReadBarrier() || fromDispipe0->insts[i]->isReadBarrier())
            inst_count++;
    }

    return inst_count;
}

void
Dispipe1::readStallSignals(ThreadID tid)
{
    // if (wtbFreeEntries[2] >= 2) {
    //     disqsLimits.out[2] = 1;
    //     disqsLimits.out[20] = 1;
    // } else if (wtbFreeEntries[2] == 1) {
    //     if ((!disqs[2].empty() && !disqs[20].empty() && disqs[2].front()->seqNum < disqs[20].front()->seqNum)
    //              || (!disqs[2].empty() && disqs[20].empty())) {
    //         disqsLimits.out[2] = 1;
    //         disqsLimits.out[20] = 0;
    //     } else {
    //         disqsLimits.out[2] = 0;
    //         disqsLimits.out[20] = 1;
    //     }
    // } else {
    //     disqsLimits.out[2] = 0;
    //     disqsLimits.out[20] = 0;
    // }

    // if (wtbFreeEntries[3] >= 2) {
    //     disqsLimits.out[3] = 1;
    //     disqsLimits.out[21] = 1;
    // } else if (wtbFreeEntries[3] == 1) {
    //     if ((!disqs[3].empty() && !disqs[21].empty() && disqs[3].front()->seqNum < disqs[21].front()->seqNum)
    //              || (!disqs[3].empty() && disqs[21].empty())) {
    //         disqsLimits.out[3] = 1;
    //         disqsLimits.out[21] = 0;
    //     } else {
    //         disqsLimits.out[3] = 0;
    //         disqsLimits.out[21] = 1;
    //     }
    // }  else {
    //     disqsLimits.out[3] = 0;
    //     disqsLimits.out[21] = 0;
    // }

    // for (int i = 0; i < 22; i++) {
    //     if (i != 2 && i != 3 && i != 20 && i != 21) {
    //         if (wtbFreeEntries[i] < disqsLimits.out[i]) {
    //             // stalls[tid].dispipe2 = true;
    //             // DPRINTF(RxuDispipe1,
    //             //         "[tid:%i] Stall due to ibuffer[%i] full.\n", tid, i);
    //             // return;
    //             disqsLimits.out[i] = wtbFreeEntries[i];
    //         }
    //     }
    // }

    // for (int i = 0; i < 22; i++) {
    //     if (disqsLimits.out[i]) {
    //         break;
    //     }
    //     if (i == 22 && disqsLimits.out[i] == 0) {
    //         stalls[tid].dispipe2 = true;
    //         DPRINTF(RxuDispipe1,
    //                 "[tid:%i] Stall due to all ibuffers are full.\n", tid);
    //         return;
    //     }
    // }

    for (int i = 0; i < 24; i++) {
        
        if(i == 6 || i == 7 || i == 14 || i == 15 || i == 16 || i == 17 || i == 18 || i == 19){
            if(disqs[i].size() >= 17){
                disqsLimits.out[i] = 0;
            }
        } 
        else if (wtbFreeEntries[i] < disqsLimits.out[i]) {
            // stalls[tid].dispipe2 = true;
            // DPRINTF(RxuDispipe1,
            //         "[tid:%i] Stall due to ibuffer[%i] full.\n", tid, i);
            // return;
            disqsLimits.out[i] = wtbFreeEntries[i];
        }
    }

    for (int i = 0; i < 24; i++) {
        if (disqsLimits.out[i]) {
            break;
        }
        if (i == 24 && disqsLimits.out[i] == 0) {
            stalls[tid].dispipe2 = true;
            DPRINTF(RxuDispipe1,
                    "[tid:%i] Stall due to all ibuffers are full.\n", tid);
            return;
        }
    }

    stalls[tid].dispipe2 = false;
}

bool
Dispipe1::checkStall(ThreadID tid)
{
    bool ret_val = false;

    if (stalls[tid].dispipe2) {
        ret_val = true;
    }

    return ret_val;
}

void
Dispipe1::readFreeEntries()
{
    for (int i = 0; i < 24; i++) {
        // DPRINTF(RxuDispipe1, "fromDispipe3->Dispipe3Info->wtbFreeEntries[%i]:%i,lastSendWtbEntries[%i],fromDispipe2->Dispipe2Info->wtbInFlight[%i]\n",i,wtbFreeEntries[i],lastSendWtbEntries[i],fromDispipe2->Dispipe2Info->wtbInFlight[i]);
        if(i != 6 && i != 7 && i != 14 && i != 15 && i != 16 && i != 17 && i != 19 && i != 18){
            wtbFreeEntries[i] = fromDispipe3->Dispipe3Info->wtbFreeEntries[i]
                            - lastSendWtbEntries[i]
                            - fromDispipe2->Dispipe2Info->wtbInFlight[i] > 0 ?
                            fromDispipe3->Dispipe3Info->wtbFreeEntries[i]
                            - lastSendWtbEntries[i]
                            - fromDispipe2->Dispipe2Info->wtbInFlight[i] : 0;
        }
        else {
            wtbFreeEntries[i] = 17-disqs[i].size()- lastSendWtbEntries[i]
                            - fromDispipe2->Dispipe2Info->wtbInFlight[i] > 0 ?
                            17-disqs[i].size()- lastSendWtbEntries[i]
                            - fromDispipe2->Dispipe2Info->wtbInFlight[i] : 0;
        }
        // wtbFreeEntries[i] = fromDispipe3->Dispipe3Info->wtbFreeEntries[i]
        //                     - lastSendWtbEntries[i]
        //                     - fromDispipe2->Dispipe2Info->wtbInFlight[i];
        // DPRINTF(RxuDispipe1, "fromDispipe3->Dispipe3Info->wtbFreeEntries[%i]:%i,lastSendWtbEntries[%i],fromDispipe2->Dispipe2Info->wtbInFlight[%i]\n",i,wtbFreeEntries[i],lastSendWtbEntries[i],fromDispipe2->Dispipe2Info->wtbInFlight[i]);
    }
}

bool
Dispipe1::checkSignalsAndUpdate(ThreadID tid)
{
    readStallSignals(tid);

    if (fromCommit->commitInfo[tid].squash) {
        DPRINTF(RxuDispipe1, "[tid:%i] Squashing instructions due to squash from "
                "commit.\n", tid);

        squash(fromCommit->commitInfo[tid].doneSeqNum, tid);

        return true;
    }

    if (checkStall(tid)) {
        return block(tid);
    }

    if (dispipe1Status[tid] == Blocked) {
        DPRINTF(RxuDispipe1, "[tid:%i] Done blocking, switching to unblocking.\n",
                tid);

        dispipe1Status[tid] = Unblocking;

        unblock(tid);

        return true;
    }

    if (dispipe1Status[tid] == Squashing) {
        DPRINTF(RxuDispipe1, "[tid:%i] Done squashing, switching to running.\n",
                tid);
        dispipe1Status[tid] = Running;

        return false;
    }

    // If we've reached this point, we have not gotten any signals that
    // cause dispipe1 to change its status.  Dispipe1 remains the same as before.
    return false;
}

void
Dispipe1::statDisqueueFull(int ibuffer_id)
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
    
    default:
        break;
    }
}

bool
Dispipe1::PqCompare::operator()(
        const DynInstPtr &lhs, const DynInstPtr &rhs) const
{
    return lhs->iid > rhs->iid;
}

void
Dispipe1::disqsInsert(DynInstPtr inst)
{
    // Index corresponding to ibuffer
    // i0  i1  i2  i3  i4  i6  ls7  ls8  f0  f1  f2  f3  f4  f5
    // 0   1   2   3   4   5   6    7    8   9   10  11  12  13

    disqs[inst->ibuffer_id].push_back(inst);
}

} // namespace rxuo3
} // namespace gem5
// ----------------------------------------------------------------------------
