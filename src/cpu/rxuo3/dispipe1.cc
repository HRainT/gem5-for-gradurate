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
      dispipe0ToDispipe1Delay(params.dispipe0ToDispipe1Delay),
      commitToDispipe1Delay(params.commitToDispipe1Delay),
      dispipe1Width(params.dispipe1Width),
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
               "Number of squashed instructions processed by dispipe1")
{
    squashCycles.prereq(squashCycles);
    idleCycles.prereq(idleCycles);
    blockCycles.prereq(blockCycles);
    runCycles.prereq(runCycles);
    unblockCycles.prereq(unblockCycles);
    dispipe1Insts.prereq(dispipe1Insts);
    squashedInsts.prereq(squashedInsts);
}

void
Dispipe1::setTimeBuffer(TimeBuffer<TimeStruct> *tb_ptr)
{
    timeBuffer = tb_ptr;

    // Setup wire to read information from time buffer, from dispipe2 stage.
    fromDispipe2 = timeBuffer->getWire(-dispipe2ToDispipe1Delay);

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

}

void
Dispipe1::resetStage()
{
    _status = Inactive;

    for (ThreadID tid = 0; tid < numThreads; tid++) {
        dispipe1Status[tid] = Idle;

        stalls[tid] = {false};
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
}

void
Dispipe1::tick()
{
    wroteToTimeBuffer = false;

    blockThisCycle = false;

    bool status_change = false;

    toDispipe2Index = 0;

    DPRINTF(RxuDispipe1,
            "%d valid instructions from Dispipe0 this cycle.\n", validInsts());

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
    } else if (dispipe1Status[tid] == Squashing) {
        ++stats.squashCycles;
    }

    if (dispipe1Status[tid] == Running ||
        dispipe1Status[tid] == Idle) {

        DPRINTF(RxuDispipe1, "[tid:%i] Not blocked, so attempting to run dispipe1 "
                "stage.\n",tid);
        processInsts(tid);

    } else if (dispipe1Status[tid] == Unblocking) {

        DPRINTF(RxuDispipe1, "[tid:%i] Unblocking, send instructions to"
                " dispipe2 but dispipe0 still is stall.\n",tid);

        // If we switched over to blocking, then there's a potential for
        // an overall status change.
        status_change = unblock(tid) || status_change || blockThisCycle;
        processInsts(tid);
    }
}


void
Dispipe1::processInsts(ThreadID tid)
{
    int insts_available = insts[tid].size();

    if (insts_available == 0) {
        DPRINTF(RxuDispipe1, "[tid:%i] No inst need to process.\n",
                tid);
        ++stats.idleCycles;
        return;
    } else if (dispipe1Status[tid] == Unblocking) {
        ++stats.unblockCycles;
    } else {
        ++stats.runCycles;
    }

    InstQueue &insts_to_dispipe2 = insts[tid];

    DPRINTF(RxuDispipe1,
            "[tid:%i] "
            "%i available instructions to send dispipe2.\n",
            tid, insts_available);

    int processed_insts = 0;

    while (insts_available > 0  &&  toDispipe2Index < dispipe1Width) {

        assert(!insts_to_dispipe2.empty());

        DynInstPtr inst = insts_to_dispipe2.front();

        insts_to_dispipe2.pop_front();

        if (inst->isSquashed() && !(inst->isMemRef() || inst->isReadBarrier() || inst->isWriteBarrier())) {
            DPRINTF(RxuDispipe1,
                    "[tid:%i] "
                    "instruction %i with PC %s is squashed, skipping.\n",
                    tid, inst->seqNum, inst->pcState());

            --insts_available;
            ++stats.squashedInsts;

            continue;
        }

        DPRINTF(RxuDispipe1, "[tid:%i] Sending instruction [sn:%lli] with "
                "PC %s, ibuffer_id = %i\n",
                tid, inst->seqNum, inst->pcState(), inst->ibuffer_id);

        ++processed_insts;

        // Put instruction in dispipe2 queue.
        toDispipe2->insts[toDispipe2Index] = inst;
        toDispipe0->Dispipe1Info->wtbInFlight[inst->ibuffer_id]++;
        ++(toDispipe2->size);

        // Increment which instruction we're on.
        ++toDispipe2Index;

        // Decrement how many instructions are available.
        --insts_available;

#if TRACING_ON
        if (debug::RxuO3PipeView) {
            inst->dispipe2Tick = curTick() - inst->fetchTick;
        }
#endif
    }

    // If we wrote to the time buffer, record this.
    if (toDispipe2Index) {
        wroteToTimeBuffer = true;
    }

    stats.dispipe1Insts += processed_insts;

    // Check if there's any instructions left that haven't yet been sent.
    // If so then block.
    if (insts_available) {
        blockThisCycle = true;
    }

    if (blockThisCycle) {
        block(tid);
        toDispipe0->dispipe2Unblock[tid] = false;
    }
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

        toDispipe0->dispipe1Unblock[tid] = false;
        toDispipe0->dispipe1Block[tid] = true;
        wroteToTimeBuffer = true;

        return true;
    }

    return false;
}

bool
Dispipe1::unblock(ThreadID tid)
{
    DPRINTF(RxuDispipe1, "[tid:%i] Trying to unblock.\n", tid);

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

    if (fromDispipe2->dispipe2Block[tid]) {
        stalls[tid].dispipe2 = true;
    }

    if (fromDispipe2->dispipe2Unblock[tid]) {
        assert(stalls[tid].dispipe2);
        stalls[tid].dispipe2 = false;
    }
}

bool
Dispipe1::checkStall(ThreadID tid)
{
    bool ret_val = false;

    return ret_val;
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

} // namespace rxuo3
} // namespace gem5
// ----------------------------------------------------------------------------
