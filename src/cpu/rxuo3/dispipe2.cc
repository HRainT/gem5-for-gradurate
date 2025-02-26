// -- add by hongfei.liu ---------------------------------------------
#include "cpu/rxuo3/dispipe2.hh"

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
      dispipe2Width(params.dispipe2Width),
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
               "Number of squashed instructions processed by dispipe2")
{
    squashCycles.prereq(squashCycles);
    idleCycles.prereq(idleCycles);
    blockCycles.prereq(blockCycles);
    runCycles.prereq(runCycles);
    unblockCycles.prereq(unblockCycles);
    dispipe2Insts.prereq(dispipe2Insts);
    squashedInsts.prereq(squashedInsts);
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
}

void
Dispipe2::resetStage()
{
    _status = Inactive;

    for (ThreadID tid = 0; tid < numThreads; tid++) {
        dispipe2Status[tid] = Idle;

        stalls[tid].dispipe3 = false;
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
    } else if (dispipe2Status[tid] == Squashing) {
        ++stats.squashCycles;
    }

    if (dispipe2Status[tid] == Running ||
        dispipe2Status[tid] == Idle) {
        DPRINTF(RxuDispipe2,
                "[tid:%i] "
                "Not blocked, so attempting to run dispipe2 stage.\n",
                tid);

        processInsts(tid);
    } else if (dispipe2Status[tid] == Unblocking) {
        DPRINTF(RxuDispipe2, "[tid:%i] Unblocking status.\n",tid);
        processInsts(tid);

        // If we switched over to blocking, then there's a potential for
        // an overall status change.
        status_change = unblock(tid) || status_change || blockThisCycle;
    }
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

    if (insts[tid].size() < dispipe2Width) {

        DPRINTF(RxuDispipe2, "[tid:%i] Done unblocking.\n", tid);

        toDispipe1->dispipe2Unblock[tid] = true;
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
    if (fromDispipe3->dispipe3Block[tid]) {
        stalls[tid].dispipe3 = true;
    }

    if (fromDispipe3->dispipe3Unblock[tid]) {
        assert(stalls[tid].dispipe3);
        stalls[tid].dispipe3 = false;
    }
}

bool
Dispipe2::checkStall(ThreadID tid)
{
    bool ret_val = false;

    // if (stalls[tid].dispipe3) {
    //     DPRINTF(RxuDispipe2,
    //             "[tid:%i] Stall from Dispipe3 stage due to any ibuffer full.\n", tid);
    //     ret_val = true;
    // }

    return ret_val;
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

} // namespace rxuo3
} // namespace gem5
