// -- add by hongfei.liu ---------------------------------------------
#include "cpu/rxuo3/dispipe0.hh"

#include <ctime>
#include <random>

#include "cpu/rxuo3/cpu.hh"
#include "cpu/rxuo3/dyn_inst.hh"
#include "cpu/rxuo3/limits.hh"
#include "debug/RxuActivity.hh"
#include "debug/RxuDispipe0.hh"
#include "debug/RxuO3PipeView.hh"
#include "params/BaseRxuO3CPU.hh"
#include "arch/riscv/pcstate.hh"
#include "debug/CommitInsts.hh"

namespace gem5
{

namespace rxuo3
{

Dispipe0::Dispipe0(CPU *_cpu, const BaseRxuO3CPUParams &params)
    : cpu(_cpu),
      dispipe1ToDispipe0Delay(params.dispipe1ToDispipe0Delay),
      predisqToDispipe0Delay(params.predisqToDispipe0Delay),
      commitToDispipe0Delay(params.commitToDispipe0Delay),
      dispipe0Width(params.dispipe0Width),
      numThreads(params.numThreads),
      stats(_cpu),
      stats_vector(_cpu)
{
    if (dispipe0Width > MaxWidth)
        fatal("dispipe0Width (%d) is larger than compiled limit (%d),\n"
             "\tincrease MaxWidth in src/cpu/rxuo3/limits.hh\n",
             dispipe0Width, static_cast<int>(MaxWidth));

    for (uint32_t tid = 0; tid < MaxThreads; tid++) {
        dispipe0Status[tid] = Idle;
        decoder[tid] = nullptr;
        stalls[tid] = {false};
        instsInProgress[tid] = 0;
        lastSendCount[tid] = 0;
        lastLastSendCount[tid] = 0;
        lastSendVecCount[tid] = 0;
        lastLastSendVecCount[tid] = 0;
        emptyROB[tid] = true;
        freeEntries[tid] = {0};
        rotate_flags[tid] = {false, false, false, false, false, false};
    }

    for (ThreadID tid = 0; tid < numThreads; tid++) {
        decoder[tid] = (RiscvISA::Decoder*)params.decoder[tid];
        decoder[tid]->use_rxuo3_cpu = true;
    }

    vecQueue = new VecQueue(this, 16, 8);

    rename_idx = 0;
}

std::string
Dispipe0::name() const
{
    return cpu->name() + ".dispipe0";
}

Dispipe0::Dispipe0Stats::Dispipe0Stats(statistics::Group *parent)
    : statistics::Group(parent, "dispipe0"),
      ADD_STAT(squashCycles, statistics::units::Cycle::get(),
               "Number of cycles dispipe0 is squashing"),
      ADD_STAT(idleCycles, statistics::units::Cycle::get(),
               "Number of cycles dispipe0 is idle"),
      ADD_STAT(blockCycles, statistics::units::Cycle::get(),
               "Number of cycles dispipe0 is blocking"),
      ADD_STAT(runCycles, statistics::units::Cycle::get(),
               "Number of cycles dispipe0 is running"),
      ADD_STAT(unblockCycles, statistics::units::Cycle::get(),
               "Number of cycles dispipe0 is unblocking"),
      ADD_STAT(dispipe0Insts, statistics::units::Count::get(),
               "Number of instructions processed by dispipe0"),
      ADD_STAT(squashedInsts, statistics::units::Count::get(),
               "Number of squashed instructions processed by dispipe0"),
      ADD_STAT(ROBFullEvents, statistics::units::Count::get(),
               "Number of times dispipe0 has blocked due to ROB full"),
      ADD_STAT(vecROBFullEvents, statistics::units::Count::get(),
               "Number of times dispipe0 has blocked due to vector ROB full"),
      ADD_STAT(renameVecStallLackRegs, statistics::units::Count::get(),
               "Number of times dispipe0 has blocked due to rename(lack of free vector physical registers to rename)"),
      ADD_STAT(renameScalarStallLackRegs, statistics::units::Count::get(),
               "Number of times dispipe0 has blocked due to rename(lack of free scalar physical registers to rename)"),
      ADD_STAT(renameVecStallRecover, statistics::units::Count::get(),
               "Number of times dispipe0 has blocked due to rename(vector recover is doing)"),
      ADD_STAT(renameScalarStallRecover, statistics::units::Count::get(),
               "Number of times dispipe0 has blocked due to rename(scalar recover is doing)"),
      ADD_STAT(renameVecStallSizeOver56, statistics::units::Count::get(),
               "Number of times dispipe0 has blocked due to rename(vector mapping table size > 56)"),
      ADD_STAT(renameScalarStallSizeOver56, statistics::units::Count::get(),
               "Number of times dispipe0 has blocked due to rename(scalar mapping table size > 56)"),
      ADD_STAT(vecCsrRenameLackStall, statistics::units::Count::get(),
               "Number of times dispipe0 has blocked due to vector csr rename lack stall"),
      ADD_STAT(vecCsrRenameRecStall, statistics::units::Count::get(),
               "Number of times dispipe0 has blocked due to vector csr rename recover stall"),
      ADD_STAT(DestOdd, statistics::units::Count::get(),
               "Number of odd dest reg instructions"),
      ADD_STAT(DestEven, statistics::units::Count::get(),
               "Number of even dest reg instructions"),
      ADD_STAT(DestOddIntNormal, statistics::units::Count::get(),
               "Number of odd dest reg int normal instructions"),
      ADD_STAT(DestEvenIntNormal, statistics::units::Count::get(),
               "Number of even dest reg int normal instructions"),
      ADD_STAT(DestOddIntSpecial, statistics::units::Count::get(),
               "Number of odd dest reg int special instructions"),
      ADD_STAT(DestEvenIntSpecial, statistics::units::Count::get(),
               "Number of even dest reg int special instructions"),
      ADD_STAT(DestOddFpNormal, statistics::units::Count::get(),
               "Number of odd dest reg fp normal instructions"),
      ADD_STAT(DestEvenFpNormal, statistics::units::Count::get(),
               "Number of even dest reg fp normal instructions"),
      ADD_STAT(Src0OddLdst, statistics::units::Count:: get(),
               "Number of odd src0 reg load/store instructions"),
      ADD_STAT(Src0EvenLdst, statistics::units::Count::get(),
               "Number of even src0 reg load/store instructions"),
      ADD_STAT(Src1OddLdst, statistics::units::Count::get(),
               "Number of odd src1 reg load/store instructions"),
      ADD_STAT(Src1EvenLdst, statistics::units::Count::get(),
               "Number of even src1 reg load/store instructions"),
      ADD_STAT(ibuffer6Insts, statistics::units::Count::get(),
               "Number of load/store instructions ibuffer_id == 6"),
      ADD_STAT(ibuffer7Insts, statistics::units::Count::get(),
               "Number of load/store instructions ibuffer_id == 7"),
      ADD_STAT(ibuffer14Insts, statistics::units::Count::get(),
               "Number of load/store instructions ibuffer_id == 14"),
      ADD_STAT(ibuffer15Insts, statistics::units::Count::get(),
               "Number of load/store instructions ibuffer_id == 15"),
      ADD_STAT(ibuffer16Insts, statistics::units::Count::get(),
               "Number of load/store instructions ibuffer_id == 16"),
      ADD_STAT(ibuffer17Insts, statistics::units::Count::get(),
               "Number of load/store instructions ibuffer_id == 17"),
      ADD_STAT(ibuffer18Insts, statistics::units::Count::get(),
               "Number of load/store instructions ibuffer_id == 18"),
      ADD_STAT(ibuffer19Insts, statistics::units::Count::get(),
               "Number of load/store instructions ibuffer_id == 19"),
      ADD_STAT(ibuffer20Insts, statistics::units::Count::get(),
               "Number of load/store instructions ibuffer_id == 20"),
      ADD_STAT(ibuffer21Insts, statistics::units::Count::get(),
               "Number of load/store instructions ibuffer_id == 21"),
      ADD_STAT(ibuffer22Insts, statistics::units::Count::get(),
               "Number of load/store instructions ibuffer_id == 22"),
      ADD_STAT(ibuffer23Insts, statistics::units::Count::get(),
               "Number of load/store instructions ibuffer_id == 23"),
      ADD_STAT(ibuffer24Insts, statistics::units::Count::get(),
               "Number of load/store instructions ibuffer_id == 24"),
      ADD_STAT(ibuffer25Insts, statistics::units::Count::get(),
               "Number of load/store instructions ibuffer_id == 25"),
      ADD_STAT(ibuffer26Insts, statistics::units::Count::get(),
               "Number of load/store instructions ibuffer_id == 26"),
      ADD_STAT(ibuffer27Insts, statistics::units::Count::get(),
               "Number of load/store instructions ibuffer_id == 27"),
      ADD_STAT(noDestControlNum, statistics::units::Count::get(),
               "Number of no Dest Control"),
      ADD_STAT(ControlNum, statistics::units::Count::get(),
               "Number of Control")
{
    squashCycles.prereq(squashCycles);
    idleCycles.prereq(idleCycles);
    blockCycles.prereq(blockCycles);
    runCycles.prereq(runCycles);
    unblockCycles.prereq(unblockCycles);
    dispipe0Insts.prereq(dispipe0Insts);
    squashedInsts.prereq(squashedInsts);
    ROBFullEvents.prereq(ROBFullEvents);
    vecROBFullEvents.prereq(vecROBFullEvents);
    renameVecStallLackRegs.prereq(renameVecStallLackRegs);
    renameVecStallRecover.prereq(renameVecStallRecover);
    renameVecStallSizeOver56.prereq(renameVecStallSizeOver56);
    renameScalarStallLackRegs.prereq(renameScalarStallLackRegs);
    renameScalarStallRecover.prereq(renameScalarStallRecover);
    renameScalarStallSizeOver56.prereq(renameScalarStallSizeOver56);
    vecCsrRenameLackStall.prereq(vecCsrRenameLackStall);
    vecCsrRenameRecStall.prereq(vecCsrRenameRecStall);

    DestOdd.prereq(DestOdd);
    DestEven.prereq(DestEven);
    DestOddIntNormal.prereq(DestOddIntNormal);
    DestEvenIntNormal.prereq(DestEvenIntNormal);
    DestOddIntSpecial.prereq(DestOddIntSpecial);
    DestEvenIntSpecial.prereq(DestEvenIntSpecial);
    DestOddFpNormal.prereq(DestOddFpNormal);
    DestEvenFpNormal.prereq(DestEvenFpNormal);
    Src0OddLdst.prereq(Src0OddLdst);
    Src0EvenLdst.prereq(Src0EvenLdst);
    Src1OddLdst.prereq(Src1OddLdst);
    Src1EvenLdst.prereq(Src1EvenLdst);

    ibuffer6Insts.prereq(ibuffer6Insts);
    ibuffer7Insts.prereq(ibuffer7Insts);
    ibuffer14Insts.prereq(ibuffer14Insts);
    ibuffer15Insts.prereq(ibuffer15Insts);
    ibuffer16Insts.prereq(ibuffer16Insts);
    ibuffer17Insts.prereq(ibuffer17Insts);
    ibuffer18Insts.prereq(ibuffer18Insts);
    ibuffer19Insts.prereq(ibuffer19Insts);
    ibuffer20Insts.prereq(ibuffer20Insts);
    ibuffer21Insts.prereq(ibuffer21Insts);
    ibuffer22Insts.prereq(ibuffer22Insts);
    ibuffer23Insts.prereq(ibuffer23Insts);
    ibuffer24Insts.prereq(ibuffer24Insts);
    ibuffer25Insts.prereq(ibuffer25Insts);
    ibuffer26Insts.prereq(ibuffer26Insts);
    ibuffer27Insts.prereq(ibuffer27Insts);
    noDestControlNum.prereq(noDestControlNum);
    ControlNum.prereq(ControlNum);
}

Dispipe0::VectorQueueStats::VectorQueueStats(CPU *cpu)
    : statistics::Group(cpu),
      ADD_STAT(VQStallCycles, statistics::units::Cycle::get(),
               "Number of cycles VQ is stalling"),
      ADD_STAT(VQStallInsts, statistics::units::Count::get(),
               "Number of instructions is stall in VQ"),
      ADD_STAT(VQAverageStall, statistics::units::Rate<
                statistics::units::Cycle, statistics::units::Count>::get(),
            "Average cycles of Vector Queue stall as vset"),
      ADD_STAT(circle_invectorQ, "Distribution of cycle latency between the "
                "first time a vector is at queue front and its split ok"),
      ADD_STAT(number_instIssue, statistics::units::Count::get(),
               "Number of instructions issue from vectorqueue"),
      ADD_STAT(number_instIssueOneCircle, statistics::units::Ratio::get(),
             "the number one circle of insts issue from vectorQueue",
             number_instIssue / (cpu->baseStats.numCycles)),
      ADD_STAT(number_instMacroIn, statistics::units::Count::get(),
               "Number of macrovector to vectorqueue"),
      ADD_STAT(number_instMacroInOneCircle, statistics::units::Ratio::get(),
             "the number one circle of macrovector to vectorqueue",
             number_instMacroIn / (cpu->baseStats.numCycles)),

      ADD_STAT(VsetToDispipe1, "Number of vset instructions to dispipe1")
{
    VQStallCycles.prereq(VQStallCycles);

    VQStallInsts.prereq(VQStallInsts);

    VQAverageStall
        .precision(6);
    VQAverageStall = VQStallCycles / VQStallInsts;

    circle_invectorQ
        .init(0, 50, 1);

    number_instIssue.prereq(number_instIssue);

    number_instIssueOneCircle
        .precision(6);

    number_instMacroIn.prereq(number_instMacroIn);

    number_instMacroInOneCircle
        .precision(6);

    VsetToDispipe1
        .init(0, 8, 1);
}


Dispipe0::vectorQueueEvent::vectorQueueEvent(Dispipe0 *dispipe0_Ptr, InstQueue insts)
        : Event(Serialize_Pri, AutoDelete),
          dispipe0Ptr(dispipe0_Ptr),
          buffer(insts)
{
}

void
Dispipe0::vectorQueueEvent::process()
{
    assert(buffer.size() > 0);
    DPRINTF(RxuDispipe0, "begin process event.\n");
    for (auto inst: buffer) {
        inst->printDisassembly();
    }

    for (auto inst : buffer) {
        inst->alreadyDelay = true;
    }

    buffer.clear();
}

Dispipe0::VecQueue::VecQueue(Dispipe0 *dispipe0_Ptr, int maxDepth, int maxFiFoNum)
        : dispipe0Ptr(dispipe0_Ptr),
          maxDepth(maxDepth),
          maxFiFoNum(maxFiFoNum)
{
}

bool
Dispipe0::VecQueue::isFull()
{
    return (queue.size() == maxDepth);
}

bool
Dispipe0::VecQueue::isEmpty()
{
    return (queue.size() == 0 && uopBuffer.size() == 0);
}

bool
Dispipe0::VecQueue::macroopCanSplit(DynInstPtr inst)
{
    bool delayed = inst->alreadyDelay;
    if (!dispipe0Ptr->cpu->vsetBranch) {
        bool vl_vtype_ready = dispipe0Ptr->rename->checkVlVtypeReady(inst);
        DPRINTF(RxuDispipe0, "[sn:%llu] delayed: %d, ready: %d.\n", inst->seqNum, delayed, vl_vtype_ready);
        return delayed && vl_vtype_ready;
    } else {
        return delayed;
    }
}

void
Dispipe0::VecQueue::addMacroop(std::deque<DynInstPtr> entry)
{
    queue.push_back(entry);
}

DynInstPtr
Dispipe0::VecQueue::fetchUop()
{
    DynInstPtr uop;
    if (uopBuffer.size() > 0) {
        uop = uopBuffer.front();
        uopBuffer.pop_front();
    } else {
        if (queue.size() == 0) {
            uop = nullptr;
        } else if (queue.front().size() != 0) {
            checkAndSplit();
        }
        if (uopBuffer.size() > 0) {
            uop = uopBuffer.front();
            uopBuffer.pop_front();
        } else {
            uop = nullptr;
        }
    }

    return uop;
}

void
Dispipe0::VecQueue::checkAndSplit()
{
    while(1) {
        DynInstPtr inst = queue.front().front();
        if(inst->arrive_vectorQFront == -1){
            inst->arrive_vectorQFront = curTick();
        }
        if (macroopCanSplit(inst)) {
            if(inst->readyToIssueQ == -1){
                inst->readyToIssueQ = curTick();
            }   
            if (inst->isNonSplitVector()) {
                inst->setOriInst(inst);
                inst->increaseCnt();

                if (!dispipe0Ptr->cpu->vsetBranch) {
                    dispipe0Ptr->rename->getVlVtype(inst);
                    inst->staticInst->machInst.vl = inst->new_vl;
                    inst->staticInst->machInst.vtype8 = inst->new_vtype.vtype8;
                    inst->staticInst->machInst.vill = inst->new_vtype.vill;
                }

                auto *dec_ptr = dispipe0Ptr->decoder[0];
                StaticInstPtr op = dec_ptr->decodeVector(inst->staticInst->machInst, inst->pc->instAddr());
                inst->staticInst = op;
                int vill = inst->new_vtype.vill;

                std::list<DynInstPtr> uop_list;
                uop_list.push_back(inst);
                dispipe0Ptr->cpu->commit.rob->insertUopInst(uop_list);
                uop_list.clear();
                uopBuffer.push_back(inst);
            } else {
                dispipe0Ptr->decodeVecInst(inst, uopBuffer);
            }
            queue.front().pop_front();
        } else {
            break;
        }

        if (queue.front().size() == 0) break;
    }
}

void
Dispipe0::VecQueue::tryPopFront()
{
    if (queue.size() != 0 && queue.front().size() == 0 && uopBuffer.size() == 0) {
        queue.pop_front();
    }
}

void
Dispipe0::VecQueue::squash(const InstSeqNum &doneSeqNum, ThreadID tid)
{
    if (queue.empty()) return;

    DynInstPtr inst;
    InstQueue entry;

    /** Squash uopBuffer */
    int uop_buffer_size = uopBuffer.size();
    for (int i = 0; i < uop_buffer_size; i++) {
        inst = uopBuffer.front();
        uopBuffer.pop_front();
        if (inst->seqNum > doneSeqNum) {
            if (!inst->isSquashed()) {
                inst->setSquashed();
                DPRINTF(RxuDispipe0,
                        "[tid:%i] "
                        "Micro instruction [sn:%i] with PC %s is squashed in uopBuffer.\n",
                        tid, inst->seqNum, inst->pcState());
            } else {
                DPRINTF(RxuDispipe0,
                        "[tid:%i] "
                        "Micro instruction [sn:%i] with PC %s has already been squashed before.\n",
                        tid, inst->seqNum, inst->pcState());
            }
        } else {
            uopBuffer.push_back(inst);
        }
    }
    
    /** Squash VecQueue */
    int queue_size = queue.size();
    for (int i = 0; i < queue_size; i++) {
        entry = queue.front();
        queue.pop_front();
        int entry_size = entry.size();
        for (int j = 0; j < entry_size; j++) {
            inst = entry.front();
            entry.pop_front();
            if (inst->seqNum > doneSeqNum) {
                if (!inst->isSquashed()) {
                    inst->setSquashed();
                    DPRINTF(RxuDispipe0,
                            "[tid:%i] "
                            "Macro instruction [sn:%i] with PC %s is squashed in uopBuffer.\n",
                            tid, inst->seqNum, inst->pcState());
                } else {
                    DPRINTF(RxuDispipe0,
                            "[tid:%i] "
                            "Macro instruction [sn:%i] with PC %s has already been squashed before.\n",
                            tid, inst->seqNum, inst->pcState());
                }
            } else {
                entry.push_back(inst);
            }
        }
        if (i == 0) {
            if (entry.empty() && uopBuffer.empty()) {
            } else {
                queue.push_back(entry);
            }
            continue;
        }
        if (!entry.empty()) queue.push_back(entry);
    }
}

void
Dispipe0::setTimeBuffer(TimeBuffer<TimeStruct> *tb_ptr)
{
    timeBuffer = tb_ptr;

    // Setup wire to read information from time buffer, from Dispipe1 stage.
    fromDispipe1 = timeBuffer->getWire(-dispipe1ToDispipe0Delay);

    // Setup wire to read infromation from time buffer, from commit stage.
    fromCommit = timeBuffer->getWire(-commitToDispipe0Delay);

    // Setup wire to write information to previous stages.
    toPredisq = timeBuffer->getWire(0);
}

void
Dispipe0::setDispipe0ToRobQueue(TimeBuffer<Dispipe0ToRobStruct> *p0q_ptr)
{
    dispipe0ToRobQueue = p0q_ptr;

    // Setup wire to write information to future stages.
    toRob = dispipe0ToRobQueue->getWire(0);
}

void
Dispipe0::setDispipe0Queue(TimeBuffer<Dispipe0Struct> *p0q_ptr)
{
    dispipe0Queue = p0q_ptr;

    // Setup wire to write information to future stages.
    toDispipe1 = dispipe0Queue->getWire(0);
}

void
Dispipe0::setPredisqQueue(TimeBuffer<PredisqStruct> *pq_ptr)
{
    predisqQueue = pq_ptr;

    // Setup wire to get information from predisq.
    fromPredisq = predisqQueue->getWire(-predisqToDispipe0Delay);
}

void
Dispipe0::startupStage()
{
    resetStage();
}

void
Dispipe0::clearStates(ThreadID tid)
{
    dispipe0Status[tid] = Idle;

    stalls[tid].dispipe1Vec = false;
    stalls[tid].dispipe1Scalar = false;

    freeEntries[tid].robEntries = commit_ptr->numROBFreeEntries(tid);
    freeEntries[tid].vecRobEntries = commit_ptr->numROBFreeVecEntries(tid);

    instsInProgress[tid] = 0;
    lastSendCount[tid] = 0;
    lastLastSendCount[tid] = 0;
    lastSendVecCount[tid] = 0;
    lastLastSendVecCount[tid] = 0;
    emptyROB[tid] = true;

    rotate_flags[tid] = {false, false, false, false, false, false};
}

void
Dispipe0::resetStage()
{
    _status = Inactive;

    resumeUnblocking = false;

    for (ThreadID tid = 0; tid < numThreads; tid++) {
        dispipe0Status[tid] = Idle;

        stalls[tid].dispipe1Vec = false;
        stalls[tid].dispipe1Scalar = false;

        instsInProgress[tid] = 0;
        lastSendCount[tid] = 0;
        lastLastSendCount[tid] = 0;
        lastSendVecCount[tid] = 0;
        lastLastSendVecCount[tid] = 0;
        emptyROB[tid] = true;

        freeEntries[tid].robEntries = commit_ptr->numROBFreeEntries(tid);
        freeEntries[tid].vecRobEntries = commit_ptr->numROBFreeVecEntries(tid);

        rotate_flags[tid] = {false, false, false, false, false, false};
    }

    rename_idx = 0;
}

void
Dispipe0::setActiveThreads(std::list<ThreadID> *at_ptr)
{
    activeThreads = at_ptr;
}

bool
Dispipe0::isDrained() const
{
    if (cpu->rxu_rename) 
    for (ThreadID tid = 0; tid < numThreads; tid++) {
        if (!insts[tid].empty() ||
            !(rename->historyBuffer[tid].empty()) ||
            instsInProgress[tid] != 0 ||
            (dispipe0Status[tid] != Idle && dispipe0Status[tid] != Running))
            return false;
    }
    else
    for (ThreadID tid = 0; tid < numThreads; tid++) {
        if (!insts[tid].empty() ||
            !(o3rename->historyBuffer[tid].empty()) ||
            instsInProgress[tid] != 0 ||
            (dispipe0Status[tid] != Idle && dispipe0Status[tid] != Running))
            return false;
    }
    return true;
}

void
Dispipe0::takeOverFrom()
{
    resetStage();
}

void
Dispipe0::drainSanityCheck() const
{
    for (ThreadID tid = 0; tid < numThreads; tid++) {
        // assert(rename->historyBuffer[tid].empty());
        assert(insts[tid].empty());
        assert(instsInProgress[tid] == 0);
    }
}

void
Dispipe0::squash(const InstSeqNum &squash_seq_num, ThreadID tid)
{
    DPRINTF(RxuDispipe0, "[tid:%i] [squash sn:%llu] Squashing instructions.\n",
        tid,squash_seq_num);

    if (dispipe0Status[tid] == Blocked ||
        dispipe0Status[tid] == Unblocking) {
        toPredisq->dispipe0Unblock[tid] = true;

    }
    //ldst_cnt == 6;
    // Set the status to Squashing.
    dispipe0Status[tid] = Squashing;

    // Squash any instructions from predisq.
    for (int i = 0; i < fromPredisq->size; i++) {
        if (fromPredisq->insts[i]->threadNumber == tid &&
            fromPredisq->insts[i]->seqNum > squash_seq_num) {
            // if(fromPredisq->insts[i]->isMemRef()){
            //     if(cpu->dispipe3.ldstdisq_in[fromPredisq->insts[i]->ibuffer_id]){
            //         cpu->dispipe3.ldstdisq_in[fromPredisq->insts[i]->ibuffer_id] = false;
            //         cpu->dispipe3.fifo_cnt++;
            //     }   
            // }
            if (!fromPredisq->insts[i]->isSquashed()) {
                // if(fromPredisq->insts[i]->isMemRef()){
                //     if(cpu->dispipe3.ldstdisq_in[fromPredisq->insts[i]->ibuffer_id]){
                //         cpu->dispipe3.ldstdisq_in[fromPredisq->insts[i]->ibuffer_id] = false;
                //         cpu->dispipe3.fifo_cnt++;
                //     }   
                // }
                fromPredisq->insts[i]->setSquashed();
                wroteToTimeBuffer = true;
                DPRINTF(RxuDispipe0,
                        "[tid:%i] "
                        "instruction %i with PC %s is squashed.\n",
                        tid, fromPredisq->insts[i]->seqNum,
                        fromPredisq->insts[i]->pcState());
            } else {
                DPRINTF(RxuDispipe0,
                        "[tid:%i] "
                        "instruction %i with PC %s has already been squashed before.\n",
                        tid, fromPredisq->insts[i]->seqNum,
                        fromPredisq->insts[i]->pcState());
            }
        }
    }

    DynInstPtr inst;
    int instNums;
    instNums = insts[tid].size();

    for (int i = 0; i < instNums; i++) {

        inst = insts[tid].front();
        insts[tid].pop_front();
        if (inst->seqNum > fromCommit->commitInfo[tid].doneSeqNum) {
            // if(inst->isMemRef()){
            //     if(cpu->dispipe3.ldstdisq_in[inst->ibuffer_id]){
            //         cpu->dispipe3.ldstdisq_in[inst->ibuffer_id] = false;
            //         cpu->dispipe3.fifo_cnt++;
            //     }   
            // }
            if (!inst->isSquashed()) {
                // if(inst->isMemRef()){
                //     if(cpu->dispipe3.ldstdisq_in[inst->ibuffer_id]){
                //         cpu->dispipe3.ldstdisq_in[inst->ibuffer_id] = false;
                //         cpu->dispipe3.fifo_cnt++;
                //     }   
                // }
                inst->setSquashed();
                DPRINTF(RxuDispipe0,
                        "[tid:%i] "
                        "instruction %i with PC %s is squashed.\n",
                        tid, inst->seqNum, inst->pcState());
            } else {
                DPRINTF(RxuDispipe0,
                        "[tid:%i] "
                        "instruction %i with PC %s has already been squashed before.\n",
                        tid, inst->seqNum, inst->pcState());
            }
            ++stats.squashedInsts;
            // if(inst->isMemRef()){
            //     insts[tid].push_back(inst);
            // }
        } else {
            insts[tid].push_back(inst);
        }
    }

    instNums = vecRenameStallBuffer.size();

    for (int i = 0; i < instNums; i++) {

        inst = vecRenameStallBuffer.front();
        vecRenameStallBuffer.pop_front();
        if (inst->seqNum > fromCommit->commitInfo[tid].doneSeqNum) {

            if (!inst->isSquashed()) {

                inst->setSquashed();
                DPRINTF(RxuDispipe0,
                        "[tid:%i] "
                        "instruction %i with PC %s is squashed.\n",
                        tid, inst->seqNum, inst->pcState());
            } else {
                DPRINTF(RxuDispipe0,
                        "[tid:%i] "
                        "instruction %i with PC %s has already been squashed before.\n",
                        tid, inst->seqNum, inst->pcState());
            }
            ++stats.squashedInsts;

        } else {
            vecRenameStallBuffer.push_back(inst);
        }
    }

    instNums = uops[tid].size();

    for (int i = 0; i < instNums; i++) {

        inst = uops[tid].front();
        uops[tid].pop_front();
        if (inst->seqNum > fromCommit->commitInfo[tid].doneSeqNum) {

            if (!inst->isSquashed()) {

                inst->setSquashed();
                DPRINTF(RxuDispipe0,
                        "[tid:%i] "
                        "instruction %i with PC %s is squashed.\n",
                        tid, inst->seqNum, inst->pcState());
            } else {
                DPRINTF(RxuDispipe0,
                        "[tid:%i] "
                        "instruction %i with PC %s has already been squashed before.\n",
                        tid, inst->seqNum, inst->pcState());
            }
            ++stats.squashedInsts;

        } else {
            uops[tid].push_back(inst);
        }
    }

    vecQueue->squash(squash_seq_num, tid);

    doSquash(squash_seq_num, tid);

    
}

void
Dispipe0::tick()
{
    wroteToTimeBuffer = false;

    blockThisCycle = false;

    bool status_change = false;

    toDispipe1Index = 0;
    num_int_normal = 0;
    toRobIndex = 0;
    toDispipe1->size = 0;
    toRob->size = 0;
    toRob->sizeVec = 0;

    // DPRINTF(RxuDispipe0, "%d instructions from Predisq this cycle.\n", validInsts());

    sortInsts();

    std::list<ThreadID>::iterator threads = activeThreads->begin();
    std::list<ThreadID>::iterator end = activeThreads->end();

    // Check stall and squash signals.
    while (threads != end) {
        ThreadID tid = *threads++;

        DPRINTF(RxuDispipe0, "Processing thread : %i\n",tid);

        status_change = checkSignalsAndUpdate(tid) || status_change;

        dispipe0(status_change, tid);

    }

    if (status_change) {
        updateStatus();
    }

    if (wroteToTimeBuffer) {
        DPRINTF(RxuActivity, "Activity this cycle.\n");
        cpu->activityThisCycle();
    }

    threads = activeThreads->begin();

    while (threads != end) {
        ThreadID tid = *threads++;

        // If we committed this cycle then doneSeqNum will be > 0
        if (fromCommit->commitInfo[tid].doneSeqNum != 0 &&
            !fromCommit->commitInfo[tid].squash &&
            dispipe0Status[tid] != Squashing) {
            
            if (cpu->rxu_rename)
            rename->removeFromHistory(fromCommit->commitInfo[tid].doneSeqNum,
                                  tid);
            else 
            o3rename->removeFromHistory(fromCommit->commitInfo[tid].doneSeqNum,
                                  tid);
        }
    }

    if (cpu->rxu_rename)
        rename->recoverAndRelease();
    else
        o3rename->recoverAndRelease();

    DPRINTF(RxuDispipe0, "Send %i instructions to dispipe1 this cycle.\n",
                                                        toDispipe1Index);
}

void
Dispipe0::dispipe0(bool &status_change, ThreadID tid)
{
    if (dispipe0Status[tid] == Blocked) {
        ++stats.blockCycles;
    } else if (dispipe0Status[tid] == Squashing) {
        ++stats.squashCycles;
    } else if (dispipe0Status[tid] == Unblocking) {
        if (resumeUnblocking) {
            block(tid);
            resumeUnblocking = false;
            toPredisq->dispipe0Unblock[tid] = false;
        }
    }

    if (dispipe0Status[tid] == Running ||
        dispipe0Status[tid] == Idle) {
        DPRINTF(RxuDispipe0,
                "[tid:%i] "
                "Not blocked, so attempting to run dispipe0 stage.\n",
                tid);

        processInsts(tid);
        processUops(tid);
    } else if (dispipe0Status[tid] == Unblocking) {
        DPRINTF(RxuDispipe0, "[tid:%i] Unblocking, send insts to dispipe1 but predisq still is"
        " blocked.\n",tid);
        processInsts(tid);
        processUops(tid);
        // If we switched over to blocking, then there's a potential for
        // an overall status change.
        status_change = unblock(tid) || status_change || blockThisCycle;
    }

    if (vecQueue->isFull()) {
        DPRINTF(RxuDispipe0,
                "[tid:%i] Block predisq due to no free VectorQueue entries.\n",
                tid);

        toPredisq->Dispipe0Info->vecQueueFull = true;
    } else {
        toPredisq->Dispipe0Info->vecQueueFull = false;
    }

}

void
Dispipe0::processUops(ThreadID tid)
{   
    /** Check if special issueQ entry enough. */
    if (!dispipe3->wtb.vSpecialBuffer->entryEnough()) {
        DPRINTF(RxuDispipe0,
                "Special issueQ entry not enough, entry num: %d.\n",
                dispipe3->wtb.vSpecialBuffer->size());
        return;
    }

    if (stalls->dispipe1Vec) {
        DPRINTF(RxuDispipe0, "Vector stall due to dispipe1.\n");
        return;
    }
    DPRINTF(RxuDispipe0, "Begin process uop from Vector Queue.\n");
    int uops_available = 0;
    bool wroteVecQueue = false;
    bool remain_uop = false;
    int toDispipe1IndexVec = 0;

    if (!uops[0].empty()) {
        remain_uop = true;
        for (auto it = uops[0].begin(); it != uops[0].end(); ++it) {
            if ((*it)->isMicroop()) uops_available++;
        }
    }
    /** Fetch uop from VecQueue */
    for (int i = 0; i < maxUopNum; i++) {
        if (remain_uop) break;
        const DynInstPtr &inst = vecQueue->fetchUop();
        if(inst){
            if (inst->ori_inst->arrive_vectorQFront != -1 && inst->ori_inst->readyToIssueQ != -1 && 
                !inst->ori_inst->macro_issue) {
                if(inst->ori_inst->readyToIssueQ >= inst->ori_inst->arrive_vectorQFront){
                    stats_vector.circle_invectorQ.sample(cpu->ticksToCycles(
                            inst->ori_inst->readyToIssueQ - inst->ori_inst->arrive_vectorQFront));
                            inst->ori_inst->macro_issue =  true;
                }
                else {
                    stats_vector.circle_invectorQ.sample(cpu->ticksToCycles(
                            inst->ori_inst->readyToIssueQ - inst->ori_inst->readyToIssueQ));
                            inst->ori_inst->macro_issue =  true;
                }
            }
        }
        if (inst) {
            stallUopCount = false;
            ++stats_vector.number_instIssue;
            if (inst->isNonSplitVector()) {
                inst->setMacroop(false);
                inst->setMicroop(true);
                inst->setFirstMicroop(true);
                inst->setLastMicroop(true);
            }
            /** Special vector macro inst should rename dest and src 
             * before insert to special issueQ. */
            if (inst->isSpecialVector() && inst->isFirstMicroop()) {
                uops[inst->threadNumber].push_back(inst->ori_inst);
                DPRINTF(RxuDispipe0, "Fetch special marcoop [sn:%i] with pc: %s.\n", inst->ori_inst->seqNum, inst->pcState());
            }
            uops_available++;
            uops[inst->threadNumber].push_back(inst);
            DPRINTF(RxuDispipe0, "Fetch uop [sn:%i] with pc: %s.\n", inst->seqNum, inst->pcState());
        } else {
            if (!vecQueue->isEmpty()) {
                if (!stallUopCount) {
                    stallUopCount = true;
                    ++stats_vector.VQStallInsts;
                }
                ++stats_vector.VQStallCycles;
            }
            break;
        }
    }

    /** if VectorQueue's front is empty, pop it. */
    vecQueue->tryPopFront();

    DPRINTF(RxuDispipe0, "Totel fetch %d uops from VectorQueue.\n", uops[0].size());

    if (uops_available == 0 && uops[tid].empty()) {
        DPRINTF(RxuDispipe0, "[tid:%i] No uop to send, breaking out early.\n",tid);
        ++stats.idleCycles;
        return;
    } else if (dispipe0Status[tid] == Unblocking) {
        ++stats.unblockCycles;
    } else if (dispipe0Status[tid] == Running) {
        ++stats.runCycles;
    }

    InstQueue &uops_to_dispipe1 = uops[tid];
    int processed_uops = 0;
    DynInstPtr inst;

    while (uops_available > 0) {
        DPRINTF(RxuDispipe0, "[tid:%i] Sending uops to Dispipe1.\n", tid);

        assert(!uops_to_dispipe1.empty());

        inst = uops_to_dispipe1.front();

        if (inst->isSquashed() && !(inst->isMemRef() || inst->isReadBarrier() || inst->isWriteBarrier())) {
            DPRINTF(RxuDispipe0,
                    "[tid:%i] "
                    "uop %i with PC %s is squashed, skipping.\n",
                    tid, inst->seqNum, inst->pcState());

            ++stats.squashedInsts;

            // Decrement how many instructions are available.
            --uops_available;
            uops_to_dispipe1.pop_front();

            continue;
        }

        DPRINTF(RxuDispipe0,
        "[tid:%i] "
        "Processing uop [sn:%llu] with PC %s.\n",
        tid, inst->seqNum, inst->pcState());

        std::vector<bool> renameStallStatus = rename->checkVecRenameStall();
        if (renameStallStatus[0]) {
            DPRINTF(RxuDispipe0,
                    "Blocking because vector mapping table size > 56.\n");
            ++stats.renameVecStallSizeOver56;
            break;
        }
        if (renameStallStatus[1]) {
            DPRINTF(RxuDispipe0,
                    "Blocking because vector recover is doing.\n");
            ++stats.renameVecStallRecover;
            break;
        }
        if (renameStallStatus[2]) {
            DPRINTF(RxuDispipe0,
                    "Blocking due to lack of free vector physical registers to rename.\n");
            ++stats.renameVecStallLackRegs;
            break;
        }

        rename->renameSrcRegs(inst, tid);
        /** Because Special Macro will reach here and can't rename dest. */
        if (inst->isMicroVector()) {
            rename->renameDestRegs(inst, tid, toDispipe1IndexVec);
            /** Help Special Macro excute. */
            if (inst->isSpecialVector())
                inst->staticInst->setRenamedDestIdx(inst->renamedDestIdx(0));
            if (inst->vs1FromMicro()) {
                inst->staticInst->setRenamedVs1Idx(inst->renamedSrcIdx(0));
            }
        }

        uops_to_dispipe1.pop_front();

        if (inst->isSpecialVector() && inst->isMacroVector()) {
            dispipe3->wtb.vSpecialBuffer->insertInst(inst);
            DPRINTF(RxuDispipe0, "Insert special vector macro inst [sn:%i] to special issueQ.\n", inst->seqNum);
            continue;
        }

        unsigned num_dest_regs = inst->numDestRegs();
        if (num_dest_regs) {
            const RegId &dest_reg = inst->destRegIdx(0);
            if (inst->isNonSplitVector() && dest_reg.regClass().type() == VecRegClass) {
                rmu->addToProducers(inst);
            } else if(!inst->isNonSplitVector()) {
                rmu->addToProducers(inst);
            }
        }

        setIidAndIbufferid(inst);

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
        
        ++processed_uops;

        toDispipe1->insts[toDispipe1Index] = inst;
        ++(toDispipe1->size);
        ++toDispipe1Index;
        if(inst->ibuffer_id == 0 || inst->ibuffer_id == 1 || inst->ibuffer_id == 4 ||
        inst->ibuffer_id == 5){
            num_int_normal++;
        }
        ++toDispipe1IndexVec;

        --uops_available;

        if(inst->isReadBarrier() || inst->isWriteBarrier()){
            DPRINTF(RxuDispipe0, "Inserting barrier PC %s [sn:%lli].\n",
                inst->pcState(), inst->seqNum);
            dispipe3->wtb.memDepUnit->memhashadd(inst);
        }

        if (inst->isAtomic()){
            //lsq->insertStore(inst);
            //dispipe3->ldstQueue.insertStore(inst);
            DPRINTF(RxuDispipe0, "Inserting atomic PC %s [sn:%lli].\n",
                inst->pcState(), inst->seqNum);
            //dispipe3->wtb.memDepUnit->stq.emplace(inst->seqNum,inst);  
            //dispipe3->wtb.memDepUnit->validlfst(inst);
            dispipe3->wtb.memDepUnit->memhashadd(inst);
        }
        if (inst->isStore() && !inst->needEop()){
            //lsq->insertStore(inst);
            //dispipe3->ldstQueue.insertStore(inst);
            DPRINTF(RxuDispipe0, "Inserting store PC %s [sn:%lli].\n",
                inst->pcState(), inst->seqNum);
            dispipe3->wtb.memDepUnit->stq.emplace(inst->seqNum,inst);  
           // dispipe3->wtb.memDepUnit->validlfst(inst);
            dispipe3->wtb.memDepUnit->memhashadd(inst);
        }

        if (inst->isLoad() && !inst->needEop()){
            //lsq->insertLoad(inst);
            //dispipe3->ldstQueue.insertLoad(inst);
            DPRINTF(RxuDispipe0, "Inserting load hash PC %s [sn:%lli].\n",
                inst->pcState(), inst->seqNum);
            dispipe3->wtb.memDepUnit->memhashadd(inst);
        }

#if TRACING_ON
        if (debug::RxuO3PipeView) {
            inst->dispipe0Tick = curTick() - inst->fetchTick;
        }
#endif

    }

    stats.dispipe0Insts += processed_uops;

    // If we wrote to the time buffer, record this.
    if (toDispipe1Index) {
        wroteToTimeBuffer = true;
    }

}

void
Dispipe0::processInsts(ThreadID tid)
{
    if (stalls->dispipe1Scalar) {
        block(tid);
        blockThisCycle = true;
        DPRINTF(RxuDispipe0, "Scalar stall due to dispipe1.\n");
        return;
    }
    int insts_available = insts[tid].size();
    int macro_insts_available = 0;
    bool wroteVecQueue = false;
    int send_insts = 0;
    int vsetToDispipe1 = 0;
    for (auto it : insts[tid]) {
        if (it->isMacroVector()) {
            macro_insts_available++;
        } else if(it->isNonSplitVector() && !it->isMicroop()) {
            it->setMacroop(true);
            it->setMicroop(false);
            macro_insts_available++;            
        }
    }

    if(!macro_insts_available) macro_insts_available = 1;

    if (insts_available == 0) {
        DPRINTF(RxuDispipe0, "[tid:%i] No inst to send, breaking out"
                " early.\n",tid);
        ++stats.idleCycles;
        return;
    } else if (dispipe0Status[tid] == Unblocking) {
        ++stats.unblockCycles;
    } else if (dispipe0Status[tid] == Running) {
        ++stats.runCycles;
    }

    int free_rob_entries = calcFreeROBEntries(tid);

    int free_vec_rob_entries = calcFreeVecROBEntries(tid);    

    if (free_rob_entries <= 0) {
        DPRINTF(RxuDispipe0,
                "[tid:%i] Blocking due to no free ROB entries.\n"
                "ROB has %i free entries.\n",
                tid, free_rob_entries);
        ++stats.ROBFullEvents;
        block(tid);
        blockThisCycle = true;
        return;
    } else if (free_rob_entries < insts_available) {
        DPRINTF(RxuDispipe0,
                "[tid:%i] "
                "Will have to block this cycle. "
                "%i insts available, "
                "but only %i insts can be processed due to ROB limits.\n",
                tid, insts_available, free_rob_entries);

        insts_available = free_rob_entries;

        blockThisCycle = true;

    }

    if (free_vec_rob_entries <= 0) {
        DPRINTF(RxuDispipe0,
                "[tid:%i] Blocking due to no free vector ROB entries.\n"
                "ROB has %i free entries.\n",
                tid, free_vec_rob_entries);
        blockThisCycle = true;
        block(tid);
        return;

    } else if (free_vec_rob_entries < macro_insts_available) {
        DPRINTF(RxuDispipe0,
                "[tid:%i] "
                "Will have to block this cycle. "
                "%i vector insts available, "
                "but only %i macrovector insts can be processed due to ROB limits.\n",
                tid, macro_insts_available, free_vec_rob_entries);

        macro_insts_available = free_vec_rob_entries;

        ++stats.vecROBFullEvents;
        blockThisCycle = true;

    }    

    InstQueue &insts_to_dispipe1 = insts[tid];

    DPRINTF(RxuDispipe0,
            "[tid:%i] "
            "%i available instructions need to process.\n",
            tid, insts_available);

    DPRINTF(RxuDispipe0,
            "[tid:%i] "
            "%i insts pipelining from Dispipe0 | "
            "%i insts sent from dispipe0 last cycle | "
            "%i insts sent from dispipe0 last last cycle.\n",
            tid, instsInProgress[tid], lastSendCount[tid], lastLastSendCount[tid]);

    int processed_insts = 0;
    DynInstPtr inst;

    std::default_random_engine e;
    std::uniform_int_distribution<int> u(0, 1);
    e.seed(time(0));

    brIn8 = 0;
    for (int i = 0; i < std::min(insts_available, 8); i++) {
        if (insts_to_dispipe1.at(i)->isCondCtrl())
            brIn8++;
    }

    InstQueue vq_entry;

    while (insts_available > 0 && macro_insts_available > 0 &&  send_insts < dispipe0Width) {

        assert(!insts_to_dispipe1.empty());

        inst = insts_to_dispipe1.front();

        if (inst->isSquashed() && !(inst->isMemRef() || inst->isReadBarrier() || inst->isWriteBarrier())) {
            DPRINTF(RxuDispipe0,
                    "[tid:%i] "
                    "instruction %i with PC %s is squashed, skipping.\n",
                    tid, inst->seqNum, inst->pcState());

            ++stats.squashedInsts;

            // Decrement how many instructions are available.
            --insts_available;

            if (inst->isMacroVector()) {
                --macro_insts_available;
            }

            insts_to_dispipe1.pop_front();

            continue;
        }

        DPRINTF(RxuDispipe0,
        "[tid:%i] "
        "Processing instruction [sn:%llu] with PC %s.\n",
        tid, inst->seqNum, inst->pcState());

        if (cpu->rxu_rename) {
            std::vector<bool> renameStallStatus = rename->checkScalarRenameStall();
            if (renameStallStatus[0]) {
                DPRINTF(RxuDispipe0,
                        "Blocking because scalar mapping table size > 56.\n");
                ++stats.renameScalarStallSizeOver56;
                blockThisCycle = true;
                break;
            }
            if (renameStallStatus[1]) {
                DPRINTF(RxuDispipe0,
                        "Blocking because scalar recover is doing.\n");
                ++stats.renameScalarStallRecover;
                blockThisCycle = true;
                break;
            }
            if (renameStallStatus[2]) {
                DPRINTF(RxuDispipe0,
                        "Blocking due to lack of free scalar physical registers to rename.\n");
                ++stats.renameScalarStallLackRegs;
                blockThisCycle = true;
                break;
            }

            if (rename->checkFRMRenameStall()) {
                DPRINTF(RxuDispipe0,
                        "Blocking due to stall of rename FRM.\n");
                ++stats.renameScalarStallLackRegs;
                blockThisCycle = true;
                break;
            }

            std::vector<bool> renameStall = rename->checkVecCsrRenameStall();
            if (renameStall[0] || renameStall[1]) {
                if(renameStall[0]) {
                    DPRINTF(RxuDispipe0,
                        "Blocking due to lack of rename vector csr.\n");
                    stats.vecCsrRenameLackStall++;                    
                }

                if(renameStall[1]) {
                    DPRINTF(RxuDispipe0,
                        "Blocking due to recover of rename vector csr.\n");  
                    stats.vecCsrRenameRecStall++;                   
                }
                
                toPredisq->Dispipe0Info->vecRenameStall = true;
                break;
            } else {

                toPredisq->Dispipe0Info->vecRenameStall = false;

            }

            if (inst->isVset()) {
                vsetToDispipe1++;
            }

            rename->renameSrcRegs(inst, tid);

            // rename->renameDestRegs(inst, tid, rename_idx);
            rename->renameDestRegs(inst, tid, toDispipe1Index);

        } else {
            if (!o3rename->renameMap[tid]->canRename(inst)) {
                DPRINTF(RxuRename,
                        "Blocking due to "
                        " lack of free physical registers to rename to.\n");
                blockThisCycle = true;
                ++stats.renameScalarStallLackRegs;

                break;
            }

            if (o3rename->checkFRMRenameStall()) {
                DPRINTF(RxuRename,
                        "Blocking due to stall of rename FRM.\n");
                ++stats.renameScalarStallLackRegs;
                blockThisCycle = true;

                break;
            }

            o3rename->renameSrcRegs(inst, tid);
            o3rename->renameDestRegs(inst, tid);
        }

        rename_idx++;

        if (rename_idx >= 8) {
            rename_idx = 0;
        }

        insts_to_dispipe1.pop_front();

        if(inst->ifrenamedest) {
            rmu->addToProducers(inst);
        }

        if (inst->numDestRegs() == 0 || inst->destRegIdx(0).isZeroReg()) {
            inst->odd_inst = rename_idx & 1;
        } else if (inst->ifrenamedest){
            PhysRegIdPtr dest_reg = inst->renamedDestIdx(0);
            inst->odd_inst = dest_reg->flatIndex() & 1 ? true : false;
        }

        setIidAndIbufferid(inst);

        switch (inst->ibuffer_id)
        {
        case 6:
            stats.ibuffer6Insts++;
            break;
        case 7:
            stats.ibuffer7Insts++;
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
        case 24:
            stats.ibuffer24Insts++;
            break;
        case 25:
            stats.ibuffer25Insts++;
            break;
        case 26:
            stats.ibuffer26Insts++;
            break;
        case 27:
            stats.ibuffer27Insts++;
            break;
        default:
            break;
        }

        ++processed_insts;

        if (!(inst->isMacroVector() && !inst->isMemRef())) {
            toDispipe1->insts[toDispipe1Index] = inst;
            ++(toDispipe1->size);
            // Increment which instruction we're on.
            ++toDispipe1Index;
        }

        send_insts++;
        toRob->insts[toRobIndex] = inst;
        ++(toRob->size);
        // Increment which instruction we're on.
        ++toRobIndex;        

        // Decrement how many instructions are available.
        --insts_available;
 
        if (inst->isMacroVector()) {
            --macro_insts_available;
            ++(toRob->sizeVec);
            // Push Macroop to VectorQueue
            vq_entry.push_back(inst);
            stats_vector.number_instMacroIn++;
            wroteVecQueue = true;
            DPRINTF(RxuDispipe0, "Insert Vector Inst [sn:%lli] to vecQueue\n", inst->seqNum);
            continue;
        }

        if(inst->isReadBarrier() || inst->isWriteBarrier()){
            DPRINTF(RxuDispipe0, "Inserting barrier PC %s [sn:%lli].\n",
                inst->pcState(), inst->seqNum);
            dispipe3->wtb.memDepUnit->memhashadd(inst);
        }

        if (inst->isAtomic()){
            //lsq->insertStore(inst);
            //dispipe3->ldstQueue.insertStore(inst);
            DPRINTF(RxuDispipe0, "Inserting atomic PC %s [sn:%lli].\n",
                inst->pcState(), inst->seqNum);
            //dispipe3->wtb.memDepUnit->stq.emplace(inst->seqNum,inst);  
            //dispipe3->wtb.memDepUnit->validlfst(inst);
            dispipe3->wtb.memDepUnit->memhashadd(inst);
        }
        if (inst->isStore()){
            //lsq->insertStore(inst);
            //dispipe3->ldstQueue.insertStore(inst);
            DPRINTF(RxuDispipe0, "Inserting store PC %s [sn:%lli].\n",
                inst->pcState(), inst->seqNum);
            dispipe3->wtb.memDepUnit->stq.emplace(inst->seqNum,inst);  
           // dispipe3->wtb.memDepUnit->validlfst(inst);
            dispipe3->wtb.memDepUnit->memhashadd(inst);
        }

        if (inst->isLoad()){
            //lsq->insertLoad(inst);
            //dispipe3->ldstQueue.insertLoad(inst);
            dispipe3->wtb.memDepUnit->memhashadd(inst);
        }



#if TRACING_ON
        if (debug::RxuO3PipeView) {
            inst->dispipe0Tick = curTick() - inst->fetchTick;
        }
#endif

    }

    DPRINTF(RxuDispipe0, "To Rob insts is %d\n", toRob->size);

    stats.dispipe0Insts += processed_insts;

    // If we wrote to the time buffer, record this.
    if (toDispipe1Index) {
        wroteToTimeBuffer = true;
    }

    if (vsetToDispipe1) {
        stats_vector.VsetToDispipe1.sample(vsetToDispipe1);
    }

    int intNormal = 0;
    int intSpecial = 0;
    int fpNormal = 0;
    int fpSpecial = 0;
    int LoadStoreOdd = 0;
    int LoadStoreEven = 0;
    for (int i = 0; i < toDispipe1->size; i++) {
        if (toDispipe1->insts[i]->odd_inst) {
            stats.DestOdd++;
        } else {
            stats.DestEven++;
        }
        if (inst->ibuffer_id == 0 || inst->ibuffer_id == 4 || inst->ibuffer_id == 1 || inst->ibuffer_id == 5) {
            intNormal++;
            if (toDispipe1->insts[i]->odd_inst) {
                stats.DestOddIntNormal++;
            } else {
                stats.DestEvenIntNormal++;
            }
        } else if (inst->ibuffer_id == 2 || inst->ibuffer_id == 3) {
            intSpecial++;
            if (toDispipe1->insts[i]->odd_inst) {
                stats.DestOddIntSpecial++;
            } else {
                stats.DestEvenIntSpecial++;
            }
        } else if (inst->ibuffer_id == 8 || inst->ibuffer_id == 13 || inst->ibuffer_id == 9 || inst->ibuffer_id == 10) {
            fpNormal++;
            if (toDispipe1->insts[i]->odd_inst) {
                stats.DestOddFpNormal++;
            } else {
                stats.DestEvenFpNormal++;
            }
        } else if (inst->ibuffer_id == 11 || inst->ibuffer_id == 12) {
            fpSpecial++;
        } else if (inst->ibuffer_id == 6 || inst->ibuffer_id == 7 || inst->ibuffer_id == 14 || inst->ibuffer_id == 15) {
            if (inst->ibuffer_id == 6 || inst->ibuffer_id == 14) {
                LoadStoreEven++;
            } else {
                LoadStoreOdd++;
            }
            if (toDispipe1->insts[i]->numSrcRegs() >= 1) {
                if (toDispipe1->insts[i]->renamedSrcIdx(0)->index() & 1) {
                    stats.Src0OddLdst++;
                } else {
                    stats.Src0EvenLdst++;
                }
            }
            if (toDispipe1->insts[i]->numSrcRegs() >= 2) {
                if (toDispipe1->insts[i]->renamedSrcIdx(1)->index() & 1) {
                    stats.Src1OddLdst++;
                } else {
                    stats.Src1EvenLdst++;
                }
            }
        }
    }
    rotate_flags[tid] = {
        intNormal ? !(rotate_flags[tid].int_normal) : rotate_flags[tid].int_normal,
        intSpecial ? !(rotate_flags[tid].int_special) : rotate_flags[tid].int_special,
        fpNormal ? !(rotate_flags[tid].fp_normal) : rotate_flags[tid].fp_normal,
        fpSpecial ? !(rotate_flags[tid].fp_special) : rotate_flags[tid].fp_special,
        LoadStoreOdd ? !(rotate_flags[tid].load_store_odd) : rotate_flags[tid].load_store_odd,
        LoadStoreEven ? !(rotate_flags[tid].load_store_even) : rotate_flags[tid].load_store_even };

    // Schedule vectorQueueEvent
    if (wroteVecQueue) {
        vectorQueueEvent *VQEvent = new vectorQueueEvent(this, vq_entry);
        cpu->schedule(VQEvent, cpu->clockEdge(Cycles(3)));
        vecQueue->addMacroop(vq_entry);
    }

    // Check if there's any instructions left that haven't yet been sent.
    // If so then block.
    if (insts_available >= dispipe0Width) {
        blockThisCycle = true;
    }

    if (blockThisCycle) {
        block(tid);
        toPredisq->dispipe0Unblock[tid] = false;
    }
}

DynInstPtr
Dispipe0::buildInst(ThreadID tid, StaticInstPtr staticInst,
        StaticInstPtr curMacroop, const PCStateBase &this_pc,
        const PCStateBase &next_pc, bool trace, InstSeqNum sn)
{
    // Get a sequence number.
    // InstSeqNum seq = cpu->getAndIncrementInstSeq();
    InstSeqNum seq = sn;

    DynInst::Arrays arrays;
    arrays.numSrcs = staticInst->numSrcRegs();
    arrays.numDests = staticInst->numDestRegs();

    // Create a new DynInst from the instruction fetched.
    DynInstPtr instruction = new (arrays) DynInst(
            arrays, staticInst, curMacroop, this_pc, next_pc, seq, cpu);
    instruction->setTid(tid);

    instruction->setThreadState(cpu->thread[tid]);

    DPRINTF(RxuDispipe0, "[tid:%i] [sn:%i] macro/uop/eop created, inst is: \"%s\", opclass is: %s.\n",
            tid, sn,
            instruction->staticInst->disassemble(this_pc.instAddr()),
            enums::OpClassStrings[instruction->opClass()]);

    return instruction;
}

void
Dispipe0::decodeVecInst(DynInstPtr &inst, InstQueue &uopBuffer)
{
    auto *dec_ptr = decoder[0];
    inst->pc->uReset();
    unsigned uop_num = 1;
    if(inst->isSplitMacro()) uop_num = 17;
    uint32_t vl;
    uint32_t vsew;
    RiscvISA::VTYPE vtype;

    if (cpu->vsetBranch) {
        inst->staticInst->machInst.vl = inst->mutablePCState().getVl();
        inst->staticInst->machInst.vtype8 = inst->mutablePCState().getVtype().vtype8;
        inst->staticInst->machInst.vill = inst->mutablePCState().getVtype().vill;
    } else {
        rename->getVlVtype(inst);
        inst->staticInst->machInst.vl = inst->new_vl;
        inst->staticInst->machInst.vtype8 = inst->new_vtype.vtype8;
        inst->staticInst->machInst.vill = inst->new_vtype.vill;
    }

    if(!inst->staticInst->machInst.vtype8.vma || !inst->staticInst->machInst.vtype8.vta){
        inst->if_ta_ma = false;
    }

    inst->staticInst->machInst.uop = 0;
    vl = inst->staticInst->machInst.vl;
    vsew = 8 * (1<<(inst->staticInst->machInst.vtype8.vsew));

    vecMacroop = dec_ptr->decodeVector(inst->staticInst->machInst, inst->pc->instAddr());
    inst->staticInst = vecMacroop;
    inst->redecoded = true;

    if (inst->isSpecialVector()) {
        inst->setNumSrcs(vecMacroop->numSrcRegs());
    }

    DPRINTF(RxuDispipe0,
            "[tid:%i] [sn:%i] vl: %d, vsew: %d, lmul: %d, Begin split uop from \"%s\".\n",
            0,
            inst->seqNum,
            vl,
            vsew,
            1 << inst->staticInst->machInst.vtype8.vlmul,
            inst->staticInst->disassemble((*inst->pc).instAddr())
            );

    // due to VlFFTrimVlMicroOp is control inst and will push to VQ
    std::unique_ptr<PCStateBase> next_PC;
    set(next_PC, *inst->pc);
    next_PC->set(inst->pcState().instAddr() + 4);

    std::list<DynInstPtr> uop_list;

    while (1) {
        vecMicroop = vecMacroop->fetchMicroop(inst->pc->microPC());
        DynInstPtr instruction = buildInst(
            0, vecMicroop, vecMacroop, *inst->pc, *inst->pc, true, inst->seqNum + uop_num);

        instruction->setPredTarg(*next_PC);
        instruction->setPredTaken(false);
        instruction->setPredTarg0(*next_PC);
        instruction->setPredTaken0(false);
        if(inst->isSplitMacro())
            uop_num += 17;
        else 
            uop_num++;

        instruction->setOriInst(inst);
        instruction->increaseCnt();
        inst->actualUopNumber++;

        if (vl == 0 && instruction->ori_inst->staticInst->opClass() != SimdWholeRegisterLoadOp && instruction->ori_inst->staticInst->opClass() != SimdWholeRegisterStoreOp
            && instruction->ori_inst->staticInst->opClass() != SimdVLoadOnceOp) {
            DPRINTF(RxuDispipe0,
                "vl: %d, sn:%i set allUopInVector true\n",
                vl,
                instruction->ori_inst->seqNum,
                vecMicroop->isLastMicroop());
            instruction->ori_inst->vnopReady = true;
        }

        uopBuffer.push_back(instruction);

        uop_list.push_back(instruction);
        if (inst->isSpecialMacro()) inst->uopQueue.push_back(instruction);

        instruction->setInstListIt(cpu->addMicroInst(instruction));

        if (vecMicroop->isLastMicroop()) {
            (*inst->pc).as<RiscvISA::PCState>().uEnd();
            
            if (vecMicroop->isVxsat()) {
                auto* riscvInst = dynamic_cast<gem5::RiscvISA::RiscvStaticInst*>(vecMicroop.get());
                riscvInst->machInst.rxuvxsat = 1; 
            }

            break;
        } else {
            (*inst->pc).as<RiscvISA::PCState>().uAdvance();
        }
    }

    cpu->commit.rob->insertUopInst(uop_list);

    uop_list.clear();

    DPRINTF(RxuDispipe0,
            "[tid:%i] [sn:%i] Finish split uop from \"%s\".\n",
            0,
            inst->seqNum,
            inst->staticInst->disassemble((*inst->pc).instAddr()));
}

void
Dispipe0::sortInsts()
{
    int insts_from_predisq = fromPredisq->size;
    for (int i = 0; i < insts_from_predisq; ++i) {
        const DynInstPtr &inst = fromPredisq->insts[i];
        insts[inst->threadNumber].push_back(inst);
    }
}

void
Dispipe0::updateStatus()
{
    bool any_unblocking = false;

    std::list<ThreadID>::iterator threads = activeThreads->begin();
    std::list<ThreadID>::iterator end = activeThreads->end();

    while (threads != end) {
        ThreadID tid = *threads++;

        if (dispipe0Status[tid] == Unblocking) {
            any_unblocking = true;
            break;
        }
    }

    // Dispipe0 will have activity if it's unblocking.
    if (any_unblocking) {
        if (_status == Inactive) {
            _status = Active;

            DPRINTF(RxuActivity, "Activating stage.\n");

            cpu->activateStage(CPU::Dispipe0Idx);
        }
    } else {
        // If it's not unblocking, then dispipe0 will not have any internal
        // activity.  Switch it to inactive.
        if (_status == Active) {
            _status = Inactive;
            DPRINTF(RxuActivity, "Deactivating stage.\n");

            cpu->deactivateStage(CPU::Dispipe0Idx);
        }
    }
}

bool
Dispipe0::block(ThreadID tid)
{
    DPRINTF(RxuDispipe0, "[tid:%i] Blocking.\n", tid);

    if (dispipe0Status[tid] != Blocked) {

        if (resumeUnblocking || dispipe0Status[tid] != Unblocking) {
            toPredisq->dispipe0Block[tid] = true;
            toPredisq->dispipe0Unblock[tid] = false;
            wroteToTimeBuffer = true;
        }

        // Set status to Blocked.
        dispipe0Status[tid] = Blocked;
        return true;
    }

    return false;
}

bool
Dispipe0::unblock(ThreadID tid)
{
    DPRINTF(RxuDispipe0, "[tid:%i] Trying to unblock.\n", tid);

    if (insts[tid].size() < dispipe0Width) {

        DPRINTF(RxuDispipe0, "[tid:%i] Done unblocking.\n", tid);

        toPredisq->dispipe0Unblock[tid] = true;
        wroteToTimeBuffer = true;

        dispipe0Status[tid] = Running;
        return true;
    }

    return false;
}

void
Dispipe0::doSquash(const InstSeqNum &squashed_seq_num, ThreadID tid)
{
    if (cpu->rxu_rename)
        rename->doSquash(squashed_seq_num, tid);
    else
        o3rename->doSquash(squashed_seq_num, tid);
    
    decoder[tid]->reset();
}

int
Dispipe0::calcFreeROBEntries(ThreadID tid)
{
    lastSendCount[tid] = (*dispipe0ToRobQueue)[-1].size;
    lastLastSendCount[tid] = (*dispipe0ToRobQueue)[-2].size;
    instsInProgress[tid] = lastSendCount[tid] + lastLastSendCount[tid];

    int num_free = freeEntries[tid].robEntries - instsInProgress[tid];

    return num_free;
}

int
Dispipe0::calcFreeVecROBEntries(ThreadID tid)
{
    lastSendVecCount[tid] = (*dispipe0ToRobQueue)[-1].sizeVec;
    lastLastSendVecCount[tid] = (*dispipe0ToRobQueue)[-2].sizeVec;
    vecInstsInProgress[tid] = lastSendVecCount[tid] + lastLastSendVecCount[tid];

    int num_free = freeEntries[tid].vecRobEntries - vecInstsInProgress[tid];

    return num_free;
}

unsigned
Dispipe0::validInsts()
{
    unsigned inst_count = 0;

    for (int i = 0; i < fromPredisq->size; i++) {
        if (!fromPredisq->insts[i]->isSquashed() || fromPredisq->insts[i]->isMemRef()
        || fromPredisq->insts[i]->isReadBarrier() || fromPredisq->insts[i]->isWriteBarrier())
            inst_count++;
    }

    return inst_count;
}

void
Dispipe0::readStallSignals(ThreadID tid)
{
    if (fromDispipe1->dispipe1Block[tid]) {
        stalls[tid].dispipe1Scalar = true;
    }

    if (fromDispipe1->dispipe1Unblock[tid]) {
        stalls[tid].dispipe1Scalar = false;
    }

    if (fromDispipe1->dispipe1VecBlock[tid]) {
        stalls[tid].dispipe1Vec = true;
    }

    if (fromDispipe1->dispipe1VecUnblock[tid]) {
        stalls[tid].dispipe1Vec = false;
    }
}

std::vector<bool>
Dispipe0::checkStall(ThreadID tid)
{
    std::vector<bool> ret_val(2,false);

    if (stalls[tid].dispipe1Vec) {
        DPRINTF(RxuDispipe0,"[tid:%i] Vector stall from Dispipe1 stage detected.\n", tid);
        ret_val[1] = true;
    }

    if (stalls[tid].dispipe1Scalar) {
        DPRINTF(RxuDispipe0,"[tid:%i] Scalar stall from Dispipe1 stage detected.\n", tid);
        ret_val[0] = true;
    }    

    return ret_val;
}

void
Dispipe0::readFreeEntries(ThreadID tid)
{
    if (fromCommit->commitInfo[tid].usedROB) {
        freeEntries[tid].robEntries =
            fromCommit->commitInfo[tid].freeROBEntries;
        freeEntries[tid].vecRobEntries =
            fromCommit->commitInfo[tid].freeVecROBEntries;
        emptyROB[tid] = fromCommit->commitInfo[tid].emptyROB;
    }

    DPRINTF(RxuDispipe0, "[tid:%i] Free ROB: %i\n",
            tid,
            freeEntries[tid].robEntries);

    DPRINTF(RxuDispipe0, "[tid:%i] Free Vector ROB: %i\n",
            tid,
            freeEntries[tid].vecRobEntries);

    DPRINTF(RxuDispipe0, "[tid:%i] %i instructions not yet in ROB\n",
            tid, instsInProgress[tid]);
}

bool
Dispipe0::checkSignalsAndUpdate(ThreadID tid)
{
    readFreeEntries(tid);

    readStallSignals(tid);

    if (fromCommit->commitInfo[tid].squash) {
        DPRINTF(RxuDispipe0, "[tid:%i] Squashing instructions due to squash from "
                "commit.\n", tid);

        squash(fromCommit->commitInfo[tid].doneSeqNum, tid);

        return true;
    }

    std::vector<bool> disqfull = checkStall(tid);
    if (disqfull[0] && disqfull[1]) {
        DPRINTF(RxuDispipe0, "[tid:%i] Dispipe0 can't send instructions to dispipe1"
                " this cycle and tell Predisq switching to Blocked.\n", tid);
        return block(tid);
    }

    if (dispipe0Status[tid] == Blocked) {
        DPRINTF(RxuDispipe0, "[tid:%i] Done blocking, switching to unblocking.\n",
                tid);

        dispipe0Status[tid] = Unblocking;

        unblock(tid);

        return true;
    }

    if (dispipe0Status[tid] == Squashing) {
        if (resumeUnblocking) {
            DPRINTF(RxuDispipe0,
                    "[tid:%i] Done squashing, switching to unblocking.\n",
                    tid);
            dispipe0Status[tid] = Unblocking;
            return true;
        } else {
            DPRINTF(RxuDispipe0, "[tid:%i] Done squashing, switching to running.\n",
                    tid);
            dispipe0Status[tid] = Running;
            return false;
        }
    }

    // If we've reached this point, we have not gotten any signals that
    // cause dispipe0 to change its status.  Dispipe0 remains the same as before.
    return false;
}

void
Dispipe0::setIidAndIbufferid(const DynInstPtr &inst)
{
    // int index = 0;
    inst->iid = inst->seqNum;

    // Index corresponding to ibuffer
    // i0  i1  i2  i3  i4  i6  ls7  ls8  f0  f1  f2  f3  f4  f5  ls3 ls4 
    // 0   1   2   3   4   5   6    7    8   9   10  11  12  13  14  15
    if(inst->isMicroVector() && inst->isMemRef()){
        if(vector_ldst_cnt == 24){
            inst->ibuffer_id = 24;
            DPRINTF(RxuDispipe0,"inst seqnum:%i,ibuffer_id:%i\n",inst->seqNum,inst->ibuffer_id);
            vector_ldst_cnt = 25;
        }else if(vector_ldst_cnt == 25){
            inst->ibuffer_id = 25;
            DPRINTF(RxuDispipe0,"inst seqnum:%i,ibuffer_id:%i\n",inst->seqNum,inst->ibuffer_id);
            vector_ldst_cnt = 26;
        }else if(vector_ldst_cnt == 26){
            inst->ibuffer_id = 26;
            DPRINTF(RxuDispipe0,"inst seqnum:%i,ibuffer_id:%i\n",inst->seqNum,inst->ibuffer_id);
            vector_ldst_cnt = 27;
        }else if(vector_ldst_cnt == 27){
            inst->ibuffer_id = 27;
            DPRINTF(RxuDispipe0,"inst seqnum:%i,ibuffer_id:%i\n",inst->seqNum,inst->ibuffer_id);
            vector_ldst_cnt = 24;
        }
    }
    else if (!inst->isMicroVector() && (inst->isMemRef() || inst->isReadBarrier() || inst->isWriteBarrier())) {
        if(ldst_cnt == 6) {
            inst->ibuffer_id = 6;
            DPRINTF(RxuDispipe0,"inst seqnum:%i,ibuffer_id:%i\n",inst->seqNum,inst->ibuffer_id);
            ldst_cnt = 7;
        }
        else if(ldst_cnt == 7) {
            inst->ibuffer_id = 7;
            DPRINTF(RxuDispipe0,"inst seqnum:%i,ibuffer_id:%i\n",inst->seqNum,inst->ibuffer_id);
            ldst_cnt = 14;
        }
        else if(ldst_cnt == 14) {
            inst->ibuffer_id = 14;
            DPRINTF(RxuDispipe0,"inst seqnum:%i,ibuffer_id:%i\n",inst->seqNum,inst->ibuffer_id);
            ldst_cnt = 15;
        }
        else if(ldst_cnt == 15) {
            inst->ibuffer_id = 15;
            DPRINTF(RxuDispipe0,"inst seqnum:%i,ibuffer_id:%i\n",inst->seqNum,inst->ibuffer_id);
            ldst_cnt = 16;
        }
        else if(ldst_cnt == 16) {
            inst->ibuffer_id = 16;
            DPRINTF(RxuDispipe0,"inst seqnum:%i,ibuffer_id:%i\n",inst->seqNum,inst->ibuffer_id);
            ldst_cnt =17;
        }
        else if(ldst_cnt == 17) {
            inst->ibuffer_id = 17;
            DPRINTF(RxuDispipe0,"inst seqnum:%i,ibuffer_id:%i\n",inst->seqNum,inst->ibuffer_id);
            ldst_cnt =18;
        }
        else if(ldst_cnt == 18) {
            inst->ibuffer_id = 18;
            DPRINTF(RxuDispipe0,"inst seqnum:%i,ibuffer_id:%i\n",inst->seqNum,inst->ibuffer_id);
            ldst_cnt = 19;
        }
        else if(ldst_cnt == 19) {
            inst->ibuffer_id = 19;
            DPRINTF(RxuDispipe0,"inst seqnum:%i,ibuffer_id:%i\n",inst->seqNum,inst->ibuffer_id);
            ldst_cnt = 6;
        }
        inst->ibufferid = true;
    } else if (inst->isFloatNormal() 
            ) {
        if (inst->odd_inst) {
            inst->ibuffer_id = selectIbuffer8 ? 8 : 9;
            selectIbuffer8 = !selectIbuffer8;
        } else {
            inst->ibuffer_id = selectIbuffer10 ? 10 : 13;
            selectIbuffer10 = !selectIbuffer10;
        }
    } else if (inst->isFloatSpec()) {
        inst->ibuffer_id = selectIbuffer11 ? 11 : 12;
        selectIbuffer11 = !selectIbuffer11;
    } else if (inst->isMicroVector() || inst->isVnop()) {
       if(vector_cnt == 22){
            inst->ibuffer_id = 22;
            vector_cnt = 23;
        }
        else if(vector_cnt == 23){
            inst->ibuffer_id = 23;
            vector_cnt = 22;
        } 
    } else if ((inst->isIntNormal() || (inst->isCompressed() && !inst->isControl()))
            && !((toDispipe1Index == 6 || toDispipe1Index == 7)
            && (inst->opClass() == enums::IntAlu) 
            && num_int_normal >= 4
            && !fromDispipe1->Dispipe3Info[0].brInSpecialFull)) {
        if (inst->odd_inst) {
            inst->ibuffer_id = selectIbuffer4 ? 4 : 5;
            selectIbuffer4 = !selectIbuffer4;
        } else {
            inst->ibuffer_id = selectIbuffer0 ? 0 : 1;
            selectIbuffer0 = !selectIbuffer0;
        }
    } else {
        inst->ibuffer_id = inst->odd_inst ? 2 : 3;
        if (inst->isControl()) {
            stats.ControlNum++;
            if (inst->numDestRegs() == 0 || inst->destRegIdx(0).isZeroReg()) {
                stats.noDestControlNum++;
                inst->odd_inst = noDestOdd;
                noDestOdd = !noDestOdd;
                inst->ibuffer_id = inst->odd_inst ? 20 : 21;
            }
        }
    }

}

} // namespace rxuo3
} // namespace gem5
// ----------------------------------------------------------------------------
