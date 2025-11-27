#include "cpu/rxuo3/commit.hh"

#include <algorithm>
#include <set>
#include <string>
#include <bitset>

#include "base/compiler.hh"
#include "base/loader/symtab.hh"
#include "base/logging.hh"
#include "cpu/base.hh"
#include "cpu/checker/cpu.hh"
#include "cpu/exetrace.hh"
#include "cpu/rxuo3/cpu.hh"
#include "cpu/rxuo3/dyn_inst.hh"
#include "cpu/rxuo3/limits.hh"
#include "cpu/rxuo3/thread_state.hh"
#include "cpu/timebuf.hh"
#include "debug/RxuActivity.hh"
#include "debug/RxuCommit.hh"
#include "debug/RxuCommitRate.hh"
#include "debug/CommitInsts.hh"
#include "debug/CommitInst.hh"
#include "debug/Cmodel_inst.hh"
#include "debug/Br_code.hh"
#include "debug/Drain.hh"
#include "debug/ExecFaulting.hh"
#include "debug/HtmCpu.hh"
#include "debug/RxuO3PipeView.hh"
#include "debug/RxuMisPredPC.hh"
#include "debug/Spike.hh"
#include "debug/InstStream.hh"
#include "debug/RxuBPU.hh"
#include "params/BaseRxuO3CPU.hh"
#include "sim/faults.hh"
#include "sim/full_system.hh"
#include "sim/sim_exit.hh"
#include "arch/riscv/pcstate.hh"
#include "arch/riscv/regs/vector.hh"

namespace gem5
{

namespace rxuo3
{

void
Commit::processTrapEvent(ThreadID tid)
{
    // This will get reset by commit if it was switched out at the
    // time of this event processing.
    trapSquash[tid] = true;
}

Commit::Commit(CPU *_cpu, const BaseRxuO3CPUParams &params)
    : commitPolicy(params.smtCommitPolicy),
      cpu(_cpu),
      SpecificPC(params.system->SpecificPC()),
      ewToCommitDelay(params.ewToCommitDelay),
      commitToEWDelay(params.commitToEWDelay),
      dispipe0ToROBDelay(params.dispipe0ToROBDelay),
      fetchToCommitDelay(params.commitToFetchDelay),
      dispipe0Width(params.dispipe0Width),
      commitWidth(params.commitWidth),
      commitPtr(0),
      numThreads(params.numThreads),
      drainPending(false),
      drainImminent(false),
      trapLatency(params.trapLatency),
      canHandleInterrupts(true),
      avoidQuiesceLiveLock(false),
      stats(_cpu, this)
{
    if (commitWidth > MaxWidth)
        fatal("commitWidth (%d) is larger than compiled limit (%d),\n"
             "\tincrease MaxWidth in src/cpu/rxuo3/limits.hh\n",
             commitWidth, static_cast<int>(MaxWidth));

    _status = Active;
    _nextStatus = Inactive;

    if (commitPolicy == RxuCommitPolicy::RoundRobin) {
        //Set-Up Priority List
        for (ThreadID tid = 0; tid < numThreads; tid++) {
            priority_list.push_back(tid);
        }
    }

    for (ThreadID tid = 0; tid < MaxThreads; tid++) {
        commitStatus[tid] = Idle;
        changedROBNumEntries[tid] = false;
        trapSquash[tid] = false;
        tcSquash[tid] = false;
        squashAfterInst[tid] = nullptr;
        pc[tid].reset(params.isa[0]->newPCState());
        youngestSeqNum[tid] = 0;
        lastCommitedSeqNum[tid] = 0;
        trapInFlight[tid] = false;
        committedStores[tid] = false;
        checkEmptyROB[tid] = false;
        renameMap[tid] = nullptr;
        htmStarts[tid] = 0;
        htmStops[tid] = 0;
    }
    interrupt = NoFault;

    cannotCommitInst = 0;
    cannotCommitCount = 0;
}

std::string Commit::name() const { return cpu->name() + ".commit"; }

void
Commit::regProbePoints()
{
    ppCommit = new ProbePointArg<DynInstPtr>(
            cpu->getProbeManager(), "Commit");
    ppCommitStall = new ProbePointArg<DynInstPtr>(
            cpu->getProbeManager(), "CommitStall");
    ppSquash = new ProbePointArg<DynInstPtr>(
            cpu->getProbeManager(), "Squash");
}

Commit::CommitStats::CommitStats(CPU *cpu, Commit *commit)
    : statistics::Group(cpu, "commit"),
    ADD_STAT(commitSquashedInsts, statistics::units::Count::get(),
             "The number of squashed insts skipped by commit"),
    ADD_STAT(commitNonSpecStalls, statistics::units::Count::get(),
             "The number of times commit has been forced to stall to "
             "communicate backwards"),
    ADD_STAT(branchMispredicts, statistics::units::Count::get(),
             "The number of times a branch was mispredicted"),
    ADD_STAT(branchMispredicts_predWeak, statistics::units::Count::get(),
             "The number of times a branch was mispredicted and pred was weak"),
    ADD_STAT(numCommittedDist, statistics::units::Count::get(),
             "Number of insts commited each cycle"),
    ADD_STAT(amos, statistics::units::Count::get(),
             "Number of atomic instructions committed"),
    ADD_STAT(membars, statistics::units::Count::get(),
             "Number of memory barriers committed"),
    ADD_STAT(scs, statistics::units::Count::get(),
             "Number of SC insts committed"),
    ADD_STAT(lrs, statistics::units::Count::get(),
             "Number of Lr insts committed"),
    ADD_STAT(functionCalls, statistics::units::Count::get(),
             "Number of function calls committed."),
    ADD_STAT(committedInstType, statistics::units::Count::get(),
             "Class of committed instruction"),
    ADD_STAT(commitEligibleSamples, statistics::units::Cycle::get(),
             "number cycles where commit BW limit reached"),
    ADD_STAT(commitRetiredInsts, statistics::units::Count::get(),
             "The number of retired insts processed by commit"),
    ADD_STAT(totalRecoverRate, statistics::units::Rate<
                  statistics::units::Count, statistics::units::Count>::get(),
             "totalRecoverRate: SquashedInsts / (SquashedInsts + RetiredInsts) in ROB"),
    ADD_STAT(rbkCount, statistics::units::Count::get(),
             "Number of squash times due to rollback"),
    ADD_STAT(retiredBranchInsts, statistics::units::Count::get(),
             "Number of retired branch insts processed by commit"),
    // ADD_STAT(totalBpuMissRate, statistics::units::Rate<
    // /               statistics::units::Count, statistics::units::Count>::get(),
    // /              "totalBpuMissRate: branchMispredicts / retiredBranchInsts in ROB"),
    ADD_STAT(cannotCommit, 
             "Distribution of cycle latency between the "
              "an instruction is reached head of ROB and retired"),
    ADD_STAT(bpuCtr, 
             "Distribution of bpu ctr"),
    ADD_STAT(bpuCtr_true, 
             "Distribution of bpu ctr when pred is true"),
    ADD_STAT(bpuCtr_false, 
             "Distribution of bpu ctr when pred is false"),
    ADD_STAT(fetch_latency1, statistics::units::Count::get(),
             "fetch_latency == 1"),
    ADD_STAT(fetch_latency2, statistics::units::Count::get(),
             "fetch_latency == 2"),
    ADD_STAT(fetch_latency3andover, statistics::units::Count::get(),
             "fetch_latency >= 3"),
    ADD_STAT(fetch_latencyfault, statistics::units::Count::get(),
             "fetch_latencyfault"),
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
    ADD_STAT(DestEvenLoad, statistics::units::Count::get(),
             "Number of even dest reg Load instructions"),
    ADD_STAT(DestOddLoad, statistics::units::Count::get(),
             "Number of odd dest reg Load instructions"),
    ADD_STAT(DestEvenIntSpecial, statistics::units::Count::get(),
             "Number of even dest reg int special instructions"),
    ADD_STAT(DestOddFpNormal, statistics::units::Count::get(),
             "Number of odd dest reg fp normal instructions"),
    ADD_STAT(DestEvenFpNormal, statistics::units::Count::get(),
               "Number of even dest reg fp normal instructions"),
    ADD_STAT(DestOddVector, statistics::units::Count::get(),
             "Number of odd dest reg vector normal instructions"),
    ADD_STAT(DestEvenVector, statistics::units::Count::get(),
               "Number of even dest reg vector normal instructions"),
    ADD_STAT(bubblesInstscommit,"Distribution of bubbles in commit"),
    ADD_STAT(vsetivliInstNum, statistics::units::Count::get(),
    "vsetivliInstNum"),
    ADD_STAT(vsetvliInstNum, statistics::units::Count::get(),
        "vsetvliInstNum"),
    ADD_STAT(vsetvlInstNum, statistics::units::Count::get(),
        "vsetvlInstNum"),
    ADD_STAT(vsetivliVlChangedNum, statistics::units::Cycle::get(),
        "vlChangedNum"),
    ADD_STAT(vsetvliVlChangedNum, statistics::units::Cycle::get(),
        "vlChangedNum"),
    ADD_STAT(vsetvlVlChangedNum, statistics::units::Cycle::get(),
        "vlChangedNum"),
    ADD_STAT(vsetivliVtypeChangedNum, statistics::units::Cycle::get(),
        "vtypeChangedNum"),
    ADD_STAT(vsetvliVtypeChangedNum, statistics::units::Cycle::get(),
        "vtypeChangedNum"),
    ADD_STAT(vsetvlVtypeChangedNum, statistics::units::Cycle::get(),
        "vsetvlVtypeChangedNum")

{
    using namespace statistics;

    vsetivliInstNum.prereq(vsetivliInstNum);
    vsetvliInstNum.prereq(vsetvliInstNum);
    vsetvlInstNum.prereq(vsetvlInstNum);
    vsetivliVlChangedNum.prereq(vsetivliVlChangedNum);
    vsetvliVlChangedNum.prereq(vsetvliVlChangedNum);
    vsetvlVlChangedNum.prereq(vsetvlVlChangedNum);
    vsetivliVtypeChangedNum.prereq(vsetivliVtypeChangedNum);
    vsetvliVtypeChangedNum.prereq(vsetvliVtypeChangedNum);
    vsetvlVtypeChangedNum.prereq(vsetvlVtypeChangedNum);

    commitSquashedInsts.prereq(commitSquashedInsts);
    commitNonSpecStalls.prereq(commitNonSpecStalls);
    branchMispredicts.prereq(branchMispredicts);
    branchMispredicts_predWeak.prereq(branchMispredicts_predWeak);

    numCommittedDist
        .init(0,commit->commitWidth,1)
        .flags(statistics::pdf);

    amos
        .init(cpu->numThreads)
        .flags(total);

    membars
        .init(cpu->numThreads)
        .flags(total);

    scs
        .init(cpu->numThreads)
        .flags(total);

    lrs
        .init(cpu->numThreads)
        .flags(total);

    functionCalls
        .init(commit->numThreads)
        .flags(total);

    committedInstType
        .init(commit->numThreads,enums::Num_OpClass)
        .flags(total | pdf | dist);

    committedInstType.ysubnames(enums::OpClassStrings);

    commitRetiredInsts.prereq(commitRetiredInsts);

    totalRecoverRate
        .precision(6);
    totalRecoverRate = commitSquashedInsts / (commitSquashedInsts + commitRetiredInsts);

    rbkCount.prereq(rbkCount);
    retiredBranchInsts.prereq(retiredBranchInsts);

    // totalBpuMissRate
    //     .precision(6);
    // totalBpuMissRate = (branchMispredicts + cpu->decode.stats.bpuMissDecodeCount) / retiredBranchInsts;

    cannotCommit
        .init(0, 299, 1)
        .flags(statistics::nozero);

    bpuCtr
        .init(0, 10, 1)
        .flags(statistics::nozero);

    bpuCtr_true
        .init(0, 10, 1)
        .flags(statistics::nozero);

    bpuCtr_false
        .init(0, 10, 1)
        .flags(statistics::nozero);
    
    fetch_latency1.prereq(fetch_latency1);
    fetch_latency2.prereq(fetch_latency2);
    fetch_latency3andover.prereq(fetch_latency3andover);
    fetch_latencyfault.prereq(fetch_latencyfault);

    bubblesInstscommit
        .init(0, 1000, 1)
        .flags(statistics::nozero);

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

    DestOdd.prereq(DestOdd);
    DestEven.prereq(DestEven);
    DestOddIntNormal.prereq(DestOddIntNormal);
    DestEvenIntNormal.prereq(DestEvenIntNormal);
    DestOddIntSpecial.prereq(DestOddIntSpecial);
    DestEvenIntSpecial.prereq(DestEvenIntSpecial);
    DestEvenLoad.prereq(DestEvenLoad);
    DestOddLoad.prereq(DestOddLoad);
    DestOddFpNormal.prereq(DestOddFpNormal);
    DestEvenFpNormal.prereq(DestEvenFpNormal);
}

void
Commit::setThreads(std::vector<ThreadState *> &threads)
{
    thread = threads;
}

void
Commit::setTimeBuffer(TimeBuffer<TimeStruct> *tb_ptr)
{
    timeBuffer = tb_ptr;

    // Setup wire to send information back to EW.
    toEW = timeBuffer->getWire(0);

    // Setup wire to read data from EW (for the ROB).
    robInfoFromEW = timeBuffer->getWire(-ewToCommitDelay);
}

void
Commit::setFetchQueue(TimeBuffer<FetchStruct> *fq_ptr)
{
    fetchQueue = fq_ptr;

    fromFetch = fetchQueue->getWire(-fetchToCommitDelay);
}

void
Commit::setDispipe0ToRobQueue(TimeBuffer<Dispipe0ToRobStruct> *p0q_ptr)
{
    dispipe0Queue = p0q_ptr;

    // Setup wire to get instructions from dispipe0 (for the ROB).
    fromDispipe0 = dispipe0Queue->getWire(-dispipe0ToROBDelay);
}

void
Commit::setEWQueue(TimeBuffer<EWStruct> *eq_ptr)
{
    ewQueue = eq_ptr;

    // Setup wire to get instructions from EW.
    fromEW = ewQueue->getWire(-ewToCommitDelay);
}

void
Commit::setEWStage(EW *ew_stage)
{
    ewStage = ew_stage;
}

void
Commit::setDispipe3Stage(Dispipe3 *p3_ptr)
{
    dispipe3Stage = p3_ptr;
}

void
Commit::setActiveThreads(std::list<ThreadID> *at_ptr)
{
    activeThreads = at_ptr;
}

void
Commit::setRenameMap(UnifiedRenameMap rm_ptr[])
{
    for (ThreadID tid = 0; tid < numThreads; tid++)
        renameMap[tid] = &rm_ptr[tid];
}

void Commit::setROB(ROB *rob_ptr) { rob = rob_ptr; }

void
Commit::startupStage()
{
    rob->setActiveThreads(activeThreads);
    rob->resetEntries();

    // Broadcast the number of free entries.
    for (ThreadID tid = 0; tid < numThreads; tid++) {
        toEW->commitInfo[tid].usedROB = true;
        toEW->commitInfo[tid].freeROBEntries = rob->numFreeEntries(tid);
        toEW->commitInfo[tid].freeVecROBEntries = rob->numFreeVecEntries(tid);
        toEW->commitInfo[tid].emptyROB = true;
    }

    // Commit must broadcast the number of free entries it has at the
    // start of the simulation, so it starts as active.
    cpu->activateStage(CPU::CommitIdx);

    cpu->activityThisCycle();

    cannotCommitInst = 0;
    cannotCommitCount = 0;
}

void
Commit::clearStates(ThreadID tid)
{
    commitStatus[tid] = Idle;
    changedROBNumEntries[tid] = false;
    checkEmptyROB[tid] = false;
    trapInFlight[tid] = false;
    committedStores[tid] = false;
    trapSquash[tid] = false;
    tcSquash[tid] = false;
    pc[tid].reset(cpu->tcBase(tid)->getIsaPtr()->newPCState());
    lastCommitedSeqNum[tid] = 0;
    squashAfterInst[tid] = NULL;

    cannotCommitInst = 0;
    cannotCommitCount = 0;
}

void Commit::drain() { drainPending = true; }

void
Commit::drainResume()
{
    drainPending = false;
    drainImminent = false;
}

void
Commit::drainSanityCheck() const
{
    assert(isDrained());
    rob->drainSanityCheck();

    // hardware transactional memory
    // cannot drain partially through a transaction
    for (ThreadID tid = 0; tid < numThreads; tid++) {
        if (executingHtmTransaction(tid)) {
            panic("cannot drain partially through a HTM transaction");
        }
    }
}

bool
Commit::isDrained() const
{
    /* Make sure no one is executing microcode. There are two reasons
     * for this:
     * - Hardware virtualized CPUs can't switch into the middle of a
     *   microcode sequence.
     * - The current fetch implementation will most likely get very
     *   confused if it tries to start fetching an instruction that
     *   is executing in the middle of a ucode sequence that changes
     *   address mappings. This can happen on for example x86.
     */
    for (ThreadID tid = 0; tid < numThreads; tid++) {
        if (pc[tid]->microPC() != 0)
            return false;
    }

    /* Make sure that all instructions have finished committing before
     * declaring the system as drained. We want the pipeline to be
     * completely empty when we declare the CPU to be drained. This
     * makes debugging easier since CPU handover and restoring from a
     * checkpoint with a different CPU should have the same timing.
     */
    return rob->isEmpty() &&
        interrupt == NoFault;
}

void
Commit::takeOverFrom()
{
    _status = Active;
    _nextStatus = Inactive;
    for (ThreadID tid = 0; tid < numThreads; tid++) {
        commitStatus[tid] = Idle;
        changedROBNumEntries[tid] = false;
        trapSquash[tid] = false;
        tcSquash[tid] = false;
        squashAfterInst[tid] = NULL;
    }
    rob->takeOverFrom();
}

void
Commit::deactivateThread(ThreadID tid)
{
    std::list<ThreadID>::iterator thread_it = std::find(priority_list.begin(),
            priority_list.end(), tid);

    if (thread_it != priority_list.end()) {
        priority_list.erase(thread_it);
    }
}

bool
Commit::executingHtmTransaction(ThreadID tid) const
{
    if (tid == InvalidThreadID)
        return false;
    else
        return (htmStarts[tid] > htmStops[tid]);
}

void
Commit::resetHtmStartsStops(ThreadID tid)
{
    if (tid != InvalidThreadID)
    {
        htmStarts[tid] = 0;
        htmStops[tid] = 0;
    }
}


void
Commit::updateStatus()
{
    // reset ROB changed variable
    std::list<ThreadID>::iterator threads = activeThreads->begin();
    std::list<ThreadID>::iterator end = activeThreads->end();

    while (threads != end) {
        ThreadID tid = *threads++;

        changedROBNumEntries[tid] = false;

        // Also check if any of the threads has a trap pending
        if (commitStatus[tid] == TrapPending ||
            commitStatus[tid] == FetchTrapPending) {
            _nextStatus = Active;
        }
    }

    if (_nextStatus == Inactive && _status == Active) {
        DPRINTF(RxuActivity, "Deactivating stage.\n");
        cpu->deactivateStage(CPU::CommitIdx);
    } else if (_nextStatus == Active && _status == Inactive) {
        DPRINTF(RxuActivity, "Activating stage.\n");
        cpu->activateStage(CPU::CommitIdx);
    }

    _status = _nextStatus;
}

bool
Commit::changedROBEntries()
{
    std::list<ThreadID>::iterator threads = activeThreads->begin();
    std::list<ThreadID>::iterator end = activeThreads->end();

    while (threads != end) {
        ThreadID tid = *threads++;

        if (changedROBNumEntries[tid]) {
            return true;
        }
    }

    return false;
}

size_t
Commit::numROBFreeEntries(ThreadID tid)
{
    return rob->numFreeEntries(tid);
}

size_t
Commit::numROBFreeVecEntries(ThreadID tid)
{
    return rob->numFreeVecEntries(tid);
}

void
Commit::generateTrapEvent(ThreadID tid, Fault inst_fault)
{
    DPRINTF(RxuCommit, "Generating trap event for [tid:%i]\n", tid);

    EventFunctionWrapper *trap = new EventFunctionWrapper(
        [this, tid]{ processTrapEvent(tid); },
        "Trap", true, Event::CPU_Tick_Pri);

    Cycles latency = std::dynamic_pointer_cast<SyscallRetryFault>(inst_fault) ?
                     cpu->syscallRetryLatency : trapLatency;

    // hardware transactional memory
    if (inst_fault != nullptr &&
        std::dynamic_pointer_cast<GenericHtmFailureFault>(inst_fault)) {
        // TODO
        // latency = default abort/restore latency
        // could also do some kind of exponential back off if desired
    }

    cpu->schedule(trap, cpu->clockEdge(latency));
    trapInFlight[tid] = true;
    thread[tid]->trapPending = true;
}

void
Commit::generateTCEvent(ThreadID tid)
{
    assert(!trapInFlight[tid]);
    DPRINTF(RxuCommit, "Generating TC squash event for [tid:%i]\n", tid);

    tcSquash[tid] = true;
}

void
Commit::squashAll(ThreadID tid)
{
    // If we want to include the squashing instruction in the squash,
    // then use one older sequence number.
    // Hopefully this doesn't mess things up.  Basically I want to squash
    // all instructions of this thread.
    InstSeqNum squashed_inst = rob->isEmpty(tid) ?
        lastCommitedSeqNum[tid] : rob->readHeadInst(tid)->seqNum - 1;

    // All younger instructions will be squashed. Set the sequence
    // number as the youngest instruction in the ROB (0 in this case.
    // Hopefully nothing breaks.)
    youngestSeqNum[tid] = lastCommitedSeqNum[tid];

    rob->squash(squashed_inst, tid);
    changedROBNumEntries[tid] = true;

    // Send back the sequence number of the squashed instruction.
    toEW->commitInfo[tid].doneSeqNum = squashed_inst;

    // Send back the squash signal to tell stages that they should
    // squash.
    toEW->commitInfo[tid].squash = true;

    // Send back the rob squashing signal so other stages know that
    // the ROB is in the process of squashing.
    toEW->commitInfo[tid].robSquashing = true;

    toEW->commitInfo[tid].mispredictInst = NULL;
    toEW->commitInfo[tid].squashInst = NULL;

    set(toEW->commitInfo[tid].pc, pc[tid]);
}

void
Commit::squashFromTrap(ThreadID tid)
{
    squashAll(tid);

    DPRINTF(RxuCommit, "Squashing from trap, restarting at PC %s\n", *pc[tid]);

    thread[tid]->trapPending = false;
    thread[tid]->noSquashFromTC = false;
    trapInFlight[tid] = false;

    trapSquash[tid] = false;

    commitStatus[tid] = ROBSquashing;
    cpu->activityThisCycle();
}

void
Commit::squashFromTC(ThreadID tid)
{
    squashAll(tid);

    DPRINTF(RxuCommit, "Squashing from TC, restarting at PC %s\n", *pc[tid]);

    thread[tid]->noSquashFromTC = false;
    assert(!thread[tid]->trapPending);

    commitStatus[tid] = ROBSquashing;
    cpu->activityThisCycle();

    tcSquash[tid] = false;
}

void
Commit::squashFromSquashAfter(ThreadID tid)
{
    DPRINTF(RxuCommit, "Squashing after squash after request, "
            "restarting at PC %s\n", *pc[tid]);

    squashAll(tid);
    // Make sure to inform the fetch stage of which instruction caused
    // the squash. It'll try to re-fetch an instruction executing in
    // microcode unless this is set.
    toEW->commitInfo[tid].squashInst = squashAfterInst[tid];
    squashAfterInst[tid] = NULL;

    commitStatus[tid] = ROBSquashing;
    cpu->activityThisCycle();
}

void
Commit::squashAfter(ThreadID tid, const DynInstPtr &head_inst)
{
    DPRINTF(RxuCommit, "Executing squash after for [tid:%i] inst [sn:%llu]\n",
            tid, head_inst->seqNum);

    assert(!squashAfterInst[tid] || squashAfterInst[tid] == head_inst);
    commitStatus[tid] = SquashAfterPending;
    squashAfterInst[tid] = head_inst;
}

void
Commit::insert_whensq()
{
    DPRINTF(RxuCommit, "Getting instructions from Dispipe0 stage when squashing.\n");
    // Read any renamed instructions and place them into the ROB.
    int insts_to_process = std::min((int)dispipe0Width, fromDispipe0->size);

    for (int inst_num = 0; inst_num < insts_to_process; ++inst_num) {
        const DynInstPtr &inst = fromDispipe0->insts[inst_num];
        ThreadID tid = inst->threadNumber;

        if ((!inst->isSquashed() &&
            (commitStatus[tid] == ROBSquashing ||
            commitStatus[tid] == TrapPending)) &&
            (inst->seqNum < fromEW->squashedSeqNum[tid])) {
            changedROBNumEntries[tid] = false;

            DPRINTF(RxuCommit, "[tid:%i] [sn:%llu] Inserting PC %s into TSlist.\n",
                    tid, inst->seqNum, inst->pcState());

            //rob->insertInst(inst);
            TS_instList[tid].push_back(inst);

           // assert(rob->getThreadEntries(tid) <= rob->getMaxEntries(tid));

            youngestSeqNum[tid] = inst->seqNum;
        } else {
            DPRINTF(RxuCommit, "[tid:%i] [sn:%llu] "
                    "Instruction PC %s was  squashed, should not add to rob.\n",
                    tid, inst->seqNum, inst->pcState());
        }
    }
}


void
Commit::tick()
{
    wroteToTimeBuffer = false;
    _nextStatus = Inactive;

    if (activeThreads->empty())
        return;

    std::list<ThreadID>::iterator threads = activeThreads->begin();
    std::list<ThreadID>::iterator end = activeThreads->end();

    // Check if any of the threads are done squashing.  Change the
    // status if they are done.
    while (threads != end) {
        ThreadID tid = *threads++;

        // Clear the bit saying if the thread has committed stores
        // this cycle.
        committedStores[tid] = false;

        if (commitStatus[tid] == ROBSquashing) {

            if (rob->isDoneSquashing(tid)) {
                commitStatus[tid] = Running;
                addtoROB(tid);
            } else {
                DPRINTF(RxuCommit,"[tid:%i] Still Squashing, cannot commit any"
                        " insts this cycle.\n", tid);
                rob->doSquash(tid);
                toEW->commitInfo[tid].robSquashing = true;
                wroteToTimeBuffer = true;
            }
        }
    }

    commit();

    markCompletedInsts();

    threads = activeThreads->begin();

    while (threads != end) {
        ThreadID tid = *threads++;

        if (!rob->isEmpty(tid) && rob->readHeadInst(tid)->readyToCommit()) {
            // The ROB has more instructions it can commit. Its next status
            // will be active.
            _nextStatus = Active;

            [[maybe_unused]] const DynInstPtr &inst = rob->readHeadInst(tid);

            DPRINTF(RxuCommit,"[tid:%i] Instruction [sn:%llu] PC %s is head of"
                    " ROB and ready to commit\n",
                    tid, inst->seqNum, inst->pcState());

        } else if (!rob->isEmpty(tid)) {
            const DynInstPtr &inst = rob->readHeadInst(tid);

            if (inst->robHeadTick == -1) {
                inst->robHeadTick = curTick();
            }

            ppCommitStall->notify(inst);

            DPRINTF(RxuCommit,"[tid:%i] Can't commit, Instruction [sn:%llu] PC "
                    "%s is head of ROB and not ready\n",
                    tid, inst->seqNum, inst->pcState());

            if (inst->seqNum != cannotCommitInst) {
                cannotCommitInst = inst->seqNum;
                cannotCommitCount = 1;
            } else {
                cannotCommitCount++;
            }

            if (cannotCommitCount >= 200000) {
                std::string message = "Can't commit over 10000 cycles, Instruction [sn:" 
                    + std::to_string(inst->seqNum) + "] is " 
                    + inst->staticInst->disassemble(inst->pc->instAddr());
                // exitSimLoop(message);
                panic(message);
            }
        }

        DPRINTF(RxuCommit, "[tid:%i] ROB has %d insts &%d free entries &%d vector free entries.\n",
                tid, rob->countInsts(tid), rob->numFreeEntries(tid), rob->numFreeVecEntries(tid));
    }


    if (wroteToTimeBuffer) {
        DPRINTF(RxuActivity, "RxuActivity This Cycle.\n");
        cpu->activityThisCycle();
    }

    updateStatus();
}

void
Commit::handleInterrupt()
{
    // Verify that we still have an interrupt to handle
    if (!cpu->checkInterrupts(0)) {
        DPRINTF(RxuCommit, "Pending interrupt is cleared by requestor before "
                "it got handled. Restart fetching from the orig path.\n");
        toEW->commitInfo[0].clearInterrupt = true;
        interrupt = NoFault;
        avoidQuiesceLiveLock = true;
        return;
    }

    // Wait until all in flight instructions are finished before enterring
    // the interrupt.
    if (canHandleInterrupts && cpu->instList.empty()) {
        // Squash or record that I need to squash this cycle if
        // an interrupt needed to be handled.
        DPRINTF(RxuCommit, "Interrupt detected.\n");

        // Clear the interrupt now that it's going to be handled
        toEW->commitInfo[0].clearInterrupt = true;

        assert(!thread[0]->noSquashFromTC);
        thread[0]->noSquashFromTC = true;

        if (cpu->checker) {
            cpu->checker->handlePendingInt();
        }

        // CPU will handle interrupt. Note that we ignore the local copy of
        // interrupt. This is because the local copy may no longer be the
        // interrupt that the interrupt controller thinks is being handled.
        cpu->processInterrupts(cpu->getInterrupts());

        thread[0]->noSquashFromTC = false;

        commitStatus[0] = TrapPending;

        interrupt = NoFault;

        // Generate trap squash event.
        generateTrapEvent(0, interrupt);

        avoidQuiesceLiveLock = false;
    } else {
        DPRINTF(RxuCommit, "Interrupt pending: instruction is %sin "
                "flight, ROB is %sempty\n",
                canHandleInterrupts ? "not " : "",
                cpu->instList.empty() ? "" : "not " );
    }
}

void
Commit::propagateInterrupt()
{
    // Don't propagate intterupts if we are currently handling a trap or
    // in draining and the last observable instruction has been committed.
    if (commitStatus[0] == TrapPending || interrupt || trapSquash[0] ||
            tcSquash[0] || drainImminent)
        return;

    // Process interrupts if interrupts are enabled, not in PAL
    // mode, and no other traps or external squashes are currently
    // pending.
    // @todo: Allow other threads to handle interrupts.

    // Get any interrupt that happened
    interrupt = cpu->getInterrupts();

    // Tell fetch that there is an interrupt pending.  This
    // will make fetch wait until it sees a non PAL-mode PC,
    // at which point it stops fetching instructions.
    if (interrupt != NoFault)
        toEW->commitInfo[0].interruptPending = true;
}

void
Commit::commit()
{
    if (FullSystem) {
        // Check if we have a interrupt and get read to handle it
        if (cpu->checkInterrupts(0))
            propagateInterrupt();
    }

    ////////////////////////////////////
    // Check for any possible squashes, handle them first
    ////////////////////////////////////
    std::list<ThreadID>::iterator threads = activeThreads->begin();
    std::list<ThreadID>::iterator end = activeThreads->end();

    int num_squashing_threads = 0;

    while (threads != end) {
        ThreadID tid = *threads++;

        // Not sure which one takes priority.  I think if we have
        // both, that's a bad sign.
        if (trapSquash[tid]) {
            assert(!tcSquash[tid]);
            squashFromTrap(tid);

            // If the thread is trying to exit (i.e., an exit syscall was
            // executed), this trapSquash was originated by the exit
            // syscall earlier. In this case, schedule an exit event in
            // the next cycle to fully terminate this thread
            if (cpu->isThreadExiting(tid))
                cpu->scheduleThreadExitEvent(tid);
        } else if (tcSquash[tid]) {
            assert(commitStatus[tid] != TrapPending);
            squashFromTC(tid);
        } else if (commitStatus[tid] == SquashAfterPending) {
            // A squash from the previous cycle of the commit stage (i.e.,
            // commitInsts() called squashAfter) is pending. Squash the
            // thread now.
            squashFromSquashAfter(tid);
        }

        // Squashed sequence number must be older than youngest valid
        // instruction in the ROB. This prevents squashes from younger
        // instructions overriding squashes from older instructions.
        if (fromEW->squash[tid] &&
            commitStatus[tid] != TrapPending &&
            fromEW->squashedSeqNum[tid] <= youngestSeqNum[tid]) {

            if (fromEW->mispredictInst[tid]) {
                DPRINTF(RxuCommit,
                    "[tid:%i] Squashing due to branch mispred "
                    "PC:%#x [sn:%llu]\n",
                    tid,
                    fromEW->mispredictInst[tid]->pcState().instAddr(),
                    fromEW->squashedSeqNum[tid]);
                    // cpu->loopStats[cpu->loopIndex]->bpuMissCommitCount++;
                    ++stats.branchMispredicts;
                    if (fromEW->mispredictInst[tid]->pred_ctr == 3 || fromEW->mispredictInst[tid]->pred_ctr == 4) {
                        cpu->baseStats.bpu1MissCommitCount_predWeak++;
                    }

                    cpu->loopStats[cpu->loopxhIndex]->xhbpuMissCommitCount++;

                    // cpu->baseStats.bpu1MissCommitCount++;


                    if (cpu->loopIndex >= 1 && cpu->loopIndex <= 100) {
                        ++cpu->baseStats.bpu1MissCommitCount1_100loop;
                    } else if (cpu->loopIndex >= 101 && cpu->loopIndex <= 200) {
                        ++cpu->baseStats.bpu1MissCommitCount101_200loop;
                    } else if (cpu->loopIndex >= 201 && cpu->loopIndex <= 300) {
                        ++cpu->baseStats.bpu1MissCommitCount201_300loop;
                    } else if (cpu->loopIndex >= 301 && cpu->loopIndex <= 400) {
                        ++cpu->baseStats.bpu1MissCommitCount301_400loop;
                    } else if (cpu->loopIndex >= 401 && cpu->loopIndex <= 500) {
                        ++cpu->baseStats.bpu1MissCommitCount401_500loop;
                    }

                    if (cpu->loopxhIndex >= 1 && cpu->loopxhIndex <= 110) {
                        ++cpu->baseStats.xhbpuMissCommitCount1_110loop;
                    }
                    if (cpu->loopxhIndex >= 1 && cpu->loopxhIndex <= 30) {
                        ++cpu->baseStats.xhbpuMissCommitCount1_30loop;
                    }
                    if (cpu->loopxhIndex >= 1 && cpu->loopxhIndex <= 64) {
                        ++cpu->baseStats.xhbpuMissCommitCount1_64loop;
                    }
                    if (cpu->loopxhIndex >= 1 && cpu->loopxhIndex <= 134) {
                        ++cpu->baseStats.xhbpuMissCommitCount1_134loop;
                    }
                    if (cpu->loopxhIndex >= 1 && cpu->loopxhIndex <= 1024) {
                        ++cpu->baseStats.xhbpuMissCommitCount1_1024loop;
                    }
                    if (cpu->loopxhIndex >= 50 && cpu->loopxhIndex <= 110) {
                        ++cpu->baseStats.xhbpuMissCommitCount50_110loop;
                    }
                    if (cpu->loopxhIndex >= 20 && cpu->loopxhIndex <= 30) {
                        ++cpu->baseStats.xhbpuMissCommitCount20_30loop;
                    }
                    if (cpu->loopxhIndex >= 30 && cpu->loopxhIndex <= 64) {
                        ++cpu->baseStats.xhbpuMissCommitCount30_64loop;
                    }
                    if (cpu->loopxhIndex >= 60 && cpu->loopxhIndex <= 134) {
                        ++cpu->baseStats.xhbpuMissCommitCount60_134loop;
                    }
                    if (cpu->loopxhIndex >= 500 && cpu->loopxhIndex <= 1024) {
                        ++cpu->baseStats.xhbpuMissCommitCount500_1024loop;
                    }
                    if (cpu->loopxhIndex >= 1000 && cpu->loopxhIndex <= 1024) {
                        ++cpu->baseStats.xhbpuMissCommitCount1000_1024loop;
                    }

                    if (cpu->loopxhIndex >= 1 && cpu->loopxhIndex <= 110) {
                        ++cpu->baseStats.xhbpuMissCommitCount1_110loop;
                    }
                    if (cpu->loopxhIndex >= 1 && cpu->loopxhIndex <= 30) {
                        ++cpu->baseStats.xhbpuMissCommitCount1_30loop;
                    }
                    if (cpu->loopxhIndex >= 1 && cpu->loopxhIndex <= 64) {
                        ++cpu->baseStats.xhbpuMissCommitCount1_64loop;
                    }
                    if (cpu->loopxhIndex >= 1 && cpu->loopxhIndex <= 134) {
                        ++cpu->baseStats.xhbpuMissCommitCount1_134loop;
                    }
                    if (cpu->loopxhIndex >= 1 && cpu->loopxhIndex <= 1024) {
                        ++cpu->baseStats.xhbpuMissCommitCount1_1024loop;
                    }
                    if (cpu->loopxhIndex >= 50 && cpu->loopxhIndex <= 110) {
                        ++cpu->baseStats.xhbpuMissCommitCount50_110loop;
                    }
                    if (cpu->loopxhIndex >= 20 && cpu->loopxhIndex <= 30) {
                        ++cpu->baseStats.xhbpuMissCommitCount20_30loop;
                    }
                    if (cpu->loopxhIndex >= 30 && cpu->loopxhIndex <= 64) {
                        ++cpu->baseStats.xhbpuMissCommitCount30_64loop;
                    }
                    if (cpu->loopxhIndex >= 60 && cpu->loopxhIndex <= 134) {
                        ++cpu->baseStats.xhbpuMissCommitCount60_134loop;
                    }
                    if (cpu->loopxhIndex >= 500 && cpu->loopxhIndex <= 1024) {
                        ++cpu->baseStats.xhbpuMissCommitCount500_1024loop;
                    }
                    if (cpu->loopxhIndex >= 1000 && cpu->loopxhIndex <= 1024) {
                        ++cpu->baseStats.xhbpuMissCommitCount1000_1024loop;
                    }
                    
                    DPRINTF(RxuMisPredPC,
                        "BranchMisPred PC:%#x\n", fromEW->mispredictInst[tid]->pcState().instAddr());
            } else {
                DPRINTF(RxuCommit,
                    "[tid:%i] Squashing due to order violation [sn:%llu]\n",
                    tid, fromEW->squashedSeqNum[tid]);
                    // cpu->loopStats[cpu->loopIndex]->rbkCount++;
                    // stats.rbkCount++;
            }

            DPRINTF(RxuCommit, "[tid:%i] Redirecting PC %#x, vl = %d\n",
                    tid, *fromEW->pc[tid], (*fromEW->pc[tid]).as<gem5::RiscvISA::PCState>()._vl);


            commitStatus[tid] = ROBSquashing;

            // If we want to include the squashing instruction in the squash,
            // then use one older sequence number.
            InstSeqNum squashed_inst = fromEW->squashedSeqNum[tid];
            
            if (rob->findInst(tid, squashed_inst) && rob->findInst(tid, squashed_inst)->has_ori_inst) {
                    squashed_inst = rob->findInst(tid, squashed_inst)->ori_inst->seqNum;
            }

            if (fromEW->includeSquashInst[tid]) {
                squashed_inst--;
            }

            // All younger instructions will be squashed. Set the sequence
            // number as the youngest instruction in the ROB.
            youngestSeqNum[tid] = squashed_inst;

            rob->squash(squashed_inst, tid);
            changedROBNumEntries[tid] = true;

            toEW->commitInfo[tid].doneSeqNum = squashed_inst;

            toEW->commitInfo[tid].squash = true;

            // Send back the rob squashing signal so other stages know that
            // the ROB is in the process of squashing.
            toEW->commitInfo[tid].robSquashing = true;

            toEW->commitInfo[tid].mispredictInst =
                fromEW->mispredictInst[tid];
            toEW->commitInfo[tid].branchTaken =
                fromEW->branchTaken[tid];
            toEW->commitInfo[tid].squashInst =
                                    rob->findInst(tid, squashed_inst);
            if (toEW->commitInfo[tid].mispredictInst) {
                if (toEW->commitInfo[tid].mispredictInst->isUncondCtrl()) {
                     toEW->commitInfo[tid].branchTaken = true;
                }
            }

            set(toEW->commitInfo[tid].pc, fromEW->pc[tid]);
        }

        if (commitStatus[tid] == ROBSquashing) {
            num_squashing_threads++;
        }
    }

    // If commit is currently squashing, then it will have activity for the
    // next cycle. Set its next status as active.
    if (num_squashing_threads) {
        _nextStatus = Active;
    }

    if (num_squashing_threads != numThreads) {
        // If we're not currently squashing, then get instructions.
        getInsts();

    }
    else if(num_squashing_threads == numThreads){
        insert_whensq();
    }
    
        // Try to commit any instructions.
        commitInsts();


    //Check for any activity
    threads = activeThreads->begin();

    while (threads != end) {
        ThreadID tid = *threads++;

        if (changedROBNumEntries[tid]) {
            toEW->commitInfo[tid].usedROB = true;
            toEW->commitInfo[tid].freeROBEntries = rob->numFreeEntries(tid);
            toEW->commitInfo[tid].freeVecROBEntries = rob->numFreeVecEntries(tid);

            wroteToTimeBuffer = true;
            changedROBNumEntries[tid] = false;
            if (rob->isEmpty(tid))
                checkEmptyROB[tid] = true;
        }

        // ROB is only considered "empty" for previous stages if: a)
        // ROB is empty, b) there are no outstanding stores, c) EW
        // stage has received any information regarding stores that
        // committed.
        // c) is checked by making sure to not consider the ROB empty
        // on the same cycle as when stores have been committed.
        // @todo: Make this handle multi-cycle communication between
        // commit and EW.
        if (checkEmptyROB[tid] && rob->isEmpty(tid) &&
            !dispipe3Stage->hasStoresToWB(tid) && !committedStores[tid]) {
            checkEmptyROB[tid] = false;
            toEW->commitInfo[tid].usedROB = true;
            toEW->commitInfo[tid].emptyROB = true;
            toEW->commitInfo[tid].freeROBEntries = rob->numFreeEntries(tid);
            toEW->commitInfo[tid].freeVecROBEntries = rob->numFreeVecEntries(tid);
            wroteToTimeBuffer = true;
        }

    }
}

void
Commit::insertInstFromTS(const DynInstPtr &inst,ThreadID tid)
{
    if (!inst->isSquashed() &&
            commitStatus[tid] != ROBSquashing &&
            commitStatus[tid] != TrapPending) {

    changedROBNumEntries[tid] = true;

    rob->insertInst(inst);

    assert(rob->getThreadEntries(tid) <= rob->getMaxEntries(tid));

    youngestSeqNum[tid] = inst->seqNum;
            }
}

bool
Commit::ifTsEmpty(ThreadID tid)
{
    if(TS_instList[tid].empty()){
        return true;
    }
    else {return false;}
}

void
Commit::addtoROB(ThreadID tid)
{
     DPRINTF(RxuCommit, "add inst to rob .\n");

   for (auto it = TS_instList[tid].begin(); it != TS_instList[tid].end(); ++it) {
            
            insertInstFromTS(*it,tid);

    }
    TS_instList[tid].clear();
}


void
Commit::commitInsts()
{
    ////////////////////////////////////
    // Handle commit
    // Note that commit will be handled prior to putting new
    // instructions in the ROB so that the ROB only tries to commit
    // instructions it has in this current cycle, and not instructions
    // it is writing in during this cycle.  Can't commit and squash
    // things at the same time...
    ////////////////////////////////////

    DPRINTF(RxuCommit, "Trying to commit instructions in the ROB.\n");

    unsigned num_committed = 0;

    DynInstPtr head_inst;

    // Commit as many instructions as possible until the commit bandwidth
    // limit is reached, or it becomes impossible to commit any more.
    while (commitPtr < commitWidth) {
        // hardware transactionally memory
        // If executing within a transaction,
        // need to handle interrupts specially

        ThreadID commit_thread = getCommittingThread();

        // Check for any interrupt that we've already squashed for
        // and start processing it.
        if (interrupt != NoFault) {
            // If inside a transaction, postpone interrupts
            if (executingHtmTransaction(commit_thread)) {
                cpu->clearInterrupts(0);
                toEW->commitInfo[0].clearInterrupt = true;
                interrupt = NoFault;
                avoidQuiesceLiveLock = true;
            } else {
                handleInterrupt();
            }
        }

        // ThreadID commit_thread = getCommittingThread();

        if (commit_thread == -1 || !rob->isHeadReady(commit_thread))
            break;

        head_inst = rob->readHeadInst(commit_thread);

        ThreadID tid = head_inst->threadNumber;

        assert(tid == commit_thread);

        DPRINTF(RxuCommit,
                "Trying to commit head instruction, [tid:%i] [sn:%llu]\n",
                tid, head_inst->seqNum);
        
        // If the head instruction is squashed, it is ready to retire
        // (be removed from the ROB) at any time.
        if (head_inst->isSquashed()) {

            // bool is_acq_rel = head_inst->isFullMemBarrier() &&
            //             (head_inst->isLoad() ||
            //             (head_inst->isStore() &&
            //             !head_inst->isStoreConditional()));

            // // Remove the instruction from the dependency list.
            // if (is_acq_rel ||
            //     (!head_inst->isNonSpeculative() &&
            //     !head_inst->isStoreConditional() &&
            //     !head_inst->isAtomic() &&
            //     !head_inst->isReadBarrier() &&
            //     !head_inst->isWriteBarrier())) {

            // for (int src_reg_idx = 0;
            //     src_reg_idx < head_inst->numSrcRegs();
            //     src_reg_idx++){
            //         PhysRegIdPtr src_reg =
            //             head_inst->renamedSrcIdx(src_reg_idx);

            //         if (!head_inst->readySrcIdx(src_reg_idx) &&
            //             !src_reg->isFixedMapping()) {
            //             cpu->dispipe3.rmu->dependGraph.remove(src_reg->flatIndex(),
            //                 head_inst);
            //         }
            //     }
            // }

            // for (int dest_reg_idx = 0;
            //     dest_reg_idx < head_inst->numDestRegs();
            //     dest_reg_idx++)
            // {
            //     PhysRegIdPtr dest_reg =
            //         head_inst->renamedDestIdx(dest_reg_idx);
            //     if (dest_reg->isFixedMapping()){
            //         continue;
            //     }
            //     if (!cpu->dispipe3.rmu->dependGraph.empty(dest_reg->flatIndex())) {
            //         cpu->dispipe3.rmu->dependGraph.dependGraph[dest_reg->flatIndex()].next = NULL;
            //     }
            //     assert(cpu->dispipe3.rmu->dependGraph.empty(dest_reg->flatIndex()));
            //     cpu->dispipe3.rmu->dependGraph.clearInst(dest_reg->flatIndex());
            // }

            DPRINTF(RxuCommit, "Retiring squashed instruction from "
                    "ROB.\n");

            rob->retireHead(commit_thread);

            ++stats.commitSquashedInsts;
            cpu->loopStats[cpu->loopIndex]->commitSquashedInsts++;
            cpu->loopStats[cpu->loopxhIndex]->xhcommitSquashedInsts++;

            if (cpu->loopIndex >= 1 && cpu->loopIndex <= 100) {
                ++cpu->baseStats.commitSquashedInsts1_100loop;
            } else if (cpu->loopIndex >= 101 && cpu->loopIndex <= 200) {
                ++cpu->baseStats.commitSquashedInsts101_200loop;
            } else if (cpu->loopIndex >= 201 && cpu->loopIndex <= 300) {
                ++cpu->baseStats.commitSquashedInsts201_300loop;
            } else if (cpu->loopIndex >= 301 && cpu->loopIndex <= 400) {
                ++cpu->baseStats.commitSquashedInsts301_400loop;
            } else if (cpu->loopIndex >= 401 && cpu->loopIndex <= 500) {
                ++cpu->baseStats.commitSquashedInsts401_500loop;
            }

            if (cpu->loopxhIndex >= 1 && cpu->loopxhIndex <= 110) {
                ++cpu->baseStats.xhcommitSquashedInsts1_110loop;
            }
            if (cpu->loopxhIndex >= 1 && cpu->loopxhIndex <= 30) {
                ++cpu->baseStats.xhcommitSquashedInsts1_30loop;
            }
            if (cpu->loopxhIndex >= 1 && cpu->loopxhIndex <= 64) {
                ++cpu->baseStats.xhcommitSquashedInsts1_64loop;
            }
            if (cpu->loopxhIndex >= 1 && cpu->loopxhIndex <= 134) {
                ++cpu->baseStats.xhcommitSquashedInsts1_134loop;
            }
            if (cpu->loopxhIndex >= 1 && cpu->loopxhIndex <= 1024) {
                ++cpu->baseStats.xhcommitSquashedInsts1_1024loop;
            }
            if (cpu->loopxhIndex >= 50 && cpu->loopxhIndex <= 110) {
                ++cpu->baseStats.xhcommitSquashedInsts50_110loop;
            }
            if (cpu->loopxhIndex >= 20 && cpu->loopxhIndex <= 30) {
                ++cpu->baseStats.xhcommitSquashedInsts20_30loop;
            }
            if (cpu->loopxhIndex >= 30 && cpu->loopxhIndex <= 64) {
                ++cpu->baseStats.xhcommitSquashedInsts30_64loop;
            }
            if (cpu->loopxhIndex >= 60 && cpu->loopxhIndex <= 134) {
                ++cpu->baseStats.xhcommitSquashedInsts60_134loop;
            }
            if (cpu->loopxhIndex >= 500 && cpu->loopxhIndex <= 1024) {
                ++cpu->baseStats.xhcommitSquashedInsts500_1024loop;
            }
            if (cpu->loopxhIndex >= 1000 && cpu->loopxhIndex <= 1024) {
                ++cpu->baseStats.xhcommitSquashedInsts1000_1024loop;
            }

            // Notify potential listeners that this instruction is squashed
            ppSquash->notify(head_inst);

            // Record that the number of ROB entries has changed.
            changedROBNumEntries[tid] = true;
        } else {

            set(pc[tid], head_inst->pcState());

            // Try to commit the head instruction.
            bool commit_success = commitHead(head_inst, num_committed);

            if (commit_success) {
                head_inst->printDisassemblyAndResult(cpu->name());
                // print dest reg
                volatile __uint128_t dest_val = 0;
                if (head_inst->numDestRegs() > 0 && !head_inst->destRegIdx(0).isZeroReg()) {
                    PhysRegIdPtr phys_reg = head_inst->renamedDestIdx(0);
                    RegClassType type = phys_reg->classValue();
                    RegIndex idx = phys_reg->index();
                    if (type == VecRegClass || type == VecElemClass) {
                        dest_val = cpu->getVectorReg(phys_reg);
                    } else {
                        dest_val = cpu->getReg(phys_reg, tid);
                    }
                    if(cpu->regtable[head_inst->destRegIdx(0)] == false && cpu->RegSnMap[head_inst->destRegIdx(0)] == head_inst->seqNum) {
                        cpu->regtable[head_inst->destRegIdx(0)] = true;
                        cpu->digestMap[head_inst->destRegIdx(0)] = make_int_digest((uint64_t)dest_val,head_inst->destRegIdx(0));
                    }
                }
                volatile uint64_t high = (uint64_t)(dest_val >> 64);
                volatile uint64_t low = (uint64_t)dest_val;
                // print mem addr and data
                uint64_t mem_addr = 0;
                if (head_inst->isMemRef()) {
                    mem_addr = head_inst->effAddr;
                }

                if (head_inst->staticInst->canPrintLog) {
                    DPRINTF(CommitInsts,
                        "[sn:%d], 0x%.8x, 0x%.16lx_%.16lx, 0x%.16x,  %s   (oldvl: 0x%x, vl: 0x%x)\n",
                        head_inst->seqNum,
                        head_inst->pcState().instAddr(),
                        high,
                        low,
                        mem_addr,
                        head_inst->staticInst->disassemble(head_inst->pcState().instAddr()),
                        old_vl,
                        head_inst->mutablePCState().getVl()
                    );
                }


                if (cpu->loopPC == head_inst->pcState().instAddr()) {
                    cpu->loopIndex++;
                }
                
                if (cpu->loopstartPC == head_inst->pcState().instAddr()) {
                    if(cpu->loopstartIndex == 1){
                        cpu->loopstartIndex = 1;
                        cpu->loopendIndex = 0;
                        cpu->loopxhIndex = 4000;
                    }
                    else{
                        cpu->loopstartIndex = 1;
                        cpu->loopendIndex = 0;
                        cpu->loopxhIndex_m++;
                        cpu->loopxhIndex = cpu->loopxhIndex_m;
                    }
                    
                    
                }
                
                if (cpu->loopendPC == head_inst->pcState().instAddr()) {
                    cpu->loopendIndex = 1;
                    if(cpu->loopendIndex ==cpu->loopstartIndex &&cpu->loopendIndex ==1){
                        cpu->loopstartIndex = 0;
                        cpu->loopendIndex = 0;
                        cpu->loopendIndex = -1;
                    }
                }

                // statistics for vector
                if (head_inst->staticInst->isVset()) {
                    if (head_inst->staticInst->isVsetivli()) {
                        stats.vsetivliInstNum++;
                        if (head_inst->mutablePCState().getVl() != old_vl) stats.vsetivliVlChangedNum++;
                        if (head_inst->mutablePCState().getVtype() != old_vtype) stats.vsetivliVtypeChangedNum++;
                    } else if (head_inst->staticInst->isVsetvli()) {
                        stats.vsetvliInstNum++;
                        if (head_inst->mutablePCState().getVl() != old_vl) stats.vsetvliVlChangedNum++;
                        if (head_inst->mutablePCState().getVtype() != old_vtype) stats.vsetvliVtypeChangedNum++;
                    } else if (head_inst->staticInst->isVsetvl()) {
                        stats.vsetvlInstNum++;
                        if (head_inst->mutablePCState().getVl() != old_vl) stats.vsetvlVlChangedNum++;
                        if (head_inst->mutablePCState().getVtype() != old_vtype) stats.vsetvlVtypeChangedNum++;
                    }
                    old_vl = head_inst->mutablePCState().getVl();
                    old_vtype = head_inst->mutablePCState().getVtype();
                }
                
                ++num_committed;

                if (!head_inst->isMicroVector() || head_inst->isLastMicroop()) {
                    ++commitPtr;
                }

                cpu->commitStats[tid]
                    ->committedInstType[head_inst->opClass()]++;
                stats.committedInstType[tid][head_inst->opClass()]++;
                ppCommit->notify(head_inst);

                // hardware transactional memory

                // update nesting depth
                if (head_inst->isHtmStart())
                    htmStarts[tid]++;

                // sanity check
                if (head_inst->inHtmTransactionalState()) {
                    assert(executingHtmTransaction(tid));
                } else {
                    assert(!executingHtmTransaction(tid));
                }

                // update nesting depth
                if (head_inst->isHtmStop())
                    htmStops[tid]++;

                changedROBNumEntries[tid] = true;

                // Set the doneSeqNum to the youngest committed instruction.
                toEW->commitInfo[tid].doneSeqNum = head_inst->seqNum;

                if (tid == 0)
                    canHandleInterrupts = !head_inst->isDelayedCommit();

                // at this point store conditionals should either have
                // been completed or predicated false
                assert(!head_inst->isStoreConditional() ||
                       head_inst->isCompleted() ||
                       !head_inst->readPredicate());

                // Updates misc. registers.
                head_inst->updateMiscRegs();

                // if (head_inst->staticInst->isFence_i()) {
                //     cpu->uc.flushUopCache();
                //     cpu->bpu0.l0btb.clear();
                //     cpu->bpu0.return_Cam.clear();
                // }

                // Check instruction execution if it successfully commits and
                // is not carrying a fault.
                if (cpu->checker) {
                    cpu->checker->verify(head_inst);
                }

                cpu->traceFunctions(pc[tid]->instAddr());

                head_inst->staticInst->advancePC(*pc[tid]);

                // Keep track of the last sequence number commited
                lastCommitedSeqNum[tid] = head_inst->seqNum;

                // If this is an instruction that doesn't play nicely with
                // others squash everything and restart fetch
                if (head_inst->isSquashAfter())
                    squashAfter(tid, head_inst);

                if (drainPending) {
                    if (pc[tid]->microPC() == 0 && interrupt == NoFault &&
                        !thread[tid]->trapPending) {
                        // Last architectually committed instruction.
                        // Squash the pipeline, stall fetch, and use
                        // drainImminent to disable interrupts
                        DPRINTF(Drain, "Draining: %i:%s\n", tid, *pc[tid]);
                        squashAfter(tid, head_inst);
                        cpu->commitDrained(tid);
                        drainImminent = true;
                    }
                }

                bool onInstBoundary = !head_inst->isMicroop() ||
                                      head_inst->isLastMicroop() ||
                                      !head_inst->isDelayedCommit();

                if (onInstBoundary) {
                    int count = 0;
                    Addr oldpc;
                    // Make sure we're not currently updating state while
                    // handling PC events.
                    assert(!thread[tid]->noSquashFromTC &&
                           !thread[tid]->trapPending);
                    do {
                        oldpc = pc[tid]->instAddr();
                        thread[tid]->pcEventQueue.service(
                                oldpc, thread[tid]->getTC());
                        count++;
                    } while (oldpc != pc[tid]->instAddr());
                    if (count > 1) {
                        DPRINTF(RxuCommit,
                                "PC skip function event, stopping commit\n");
                        break;
                    }
                }

                // Check if an instruction just enabled interrupts and we've
                // previously had an interrupt pending that was not handled
                // because interrupts were subsequently disabled before the
                // pipeline reached a place to handle the interrupt. In that
                // case squash now to make sure the interrupt is handled.
                //
                // If we don't do this, we might end up in a live lock
                // situation.
                if (!interrupt && avoidQuiesceLiveLock &&
                    onInstBoundary && cpu->checkInterrupts(0))
                    squashAfter(tid, head_inst);

            } else {
                DPRINTF(RxuCommit, "Unable to commit head instruction PC:%s "
                        "[tid:%i] [sn:%llu].\n",
                        head_inst->pcState(), tid ,head_inst->seqNum);
                break;
            }
        }
    }


    DPRINTF(RxuCommitRate, "%i\n", num_committed);
    stats.numCommittedDist.sample(num_committed);

    if (num_committed == commitWidth) {
        stats.commitEligibleSamples++;
    }

    // commitPtr = commitPtr == commitWidth ? 0 : commitPtr;
    commitPtr = 0;
}

bool
Commit::commitHead(const DynInstPtr &head_inst, unsigned inst_num)
{
    assert(head_inst);

    ThreadID tid = head_inst->threadNumber;

    if (head_inst->isMacroVector()) {
        return false;
    }

    // If the instruction is not executed yet, then it will need extra
    // handling.  Signal backwards that it should be executed.
    if (!head_inst->isExecuted()) {
        // Make sure we are only trying to commit un-executed instructions we
        // think are possible.
        assert(head_inst->isNonSpeculative() || head_inst->isStoreConditional()
               || head_inst->isReadBarrier() || head_inst->isWriteBarrier()
               || head_inst->isAtomic()
               || (head_inst->isLoad() && head_inst->strictlyOrdered()));

        DPRINTF(RxuCommit,
                "Encountered a barrier or non-speculative "
                "instruction [tid:%i] [sn:%llu] "
                "at the head of the ROB, PC %s.\n",
                tid, head_inst->seqNum, head_inst->pcState());

        if (inst_num > 0 || dispipe3Stage->hasStoresToWB(tid)) {
            DPRINTF(RxuCommit,
                    "[tid:%i] [sn:%llu] "
                    "Waiting for all stores to writeback.\n",
                    tid, head_inst->seqNum);
            return false;
        }

        toEW->commitInfo[tid].nonSpecSeqNum = head_inst->seqNum;

        // Change the instruction so it won't try to commit again until
        // it is executed.
        head_inst->clearCanCommit();

        if (head_inst->isLoad() && head_inst->strictlyOrdered()) {
            DPRINTF(RxuCommit, "[tid:%i] [sn:%llu] "
                    "Strictly ordered load, PC %s.\n",
                    tid, head_inst->seqNum, head_inst->pcState());
            toEW->commitInfo[tid].strictlyOrdered = true;
            toEW->commitInfo[tid].strictlyOrderedLoad = head_inst;
        } else {
            ++stats.commitNonSpecStalls;
        }

        return false;
    }

    // Check if the instruction caused a fault.  If so, trap.
    Fault inst_fault = head_inst->getFault();

    // hardware transactional memory
    // if a fault occurred within a HTM transaction
    // ensure that the transaction aborts
    if (inst_fault != NoFault && head_inst->inHtmTransactionalState()) {
        // There exists a generic HTM fault common to all ISAs
        if (!std::dynamic_pointer_cast<GenericHtmFailureFault>(inst_fault)) {
            DPRINTF(HtmCpu, "%s - fault (%s) encountered within transaction"
                            " - converting to GenericHtmFailureFault\n",
            head_inst->staticInst->getName(), inst_fault->name());
            inst_fault = std::make_shared<GenericHtmFailureFault>(
                head_inst->getHtmTransactionUid(),
                HtmFailureFaultCause::EXCEPTION);
        }
        // If this point is reached and the fault inherits from the HTM fault,
        // then there is no need to raise a new fault
    }

    // Stores mark themselves as completed.
    if (!head_inst->isStore() && inst_fault == NoFault) {
        head_inst->setCompleted();
    }

    if (inst_fault != NoFault) {
        DPRINTF(RxuCommit, "Inst [tid:%i] [sn:%llu] PC %s has a fault, fault name: %s\n",
                tid, head_inst->seqNum, head_inst->pcState(), inst_fault->name());

        if (dispipe3Stage->hasStoresToWB(tid) || inst_num > 0) {
            DPRINTF(RxuCommit,
                    "[tid:%i] [sn:%llu] "
                    "Stores outstanding, fault must wait.\n",
                    tid, head_inst->seqNum);
            return false;
        }

        head_inst->setCompleted();

        // If instruction has faulted, let the checker execute it and
        // check if it sees the same fault and control flow.
        if (cpu->checker) {
            // Need to check the instruction before its fault is processed
            cpu->checker->verify(head_inst);
        }

        assert(!thread[tid]->noSquashFromTC);

        // Mark that we're in state update mode so that the trap's
        // execution doesn't generate extra squashes.
        thread[tid]->noSquashFromTC = true;

        // Execute the trap.  Although it's slightly unrealistic in
        // terms of timing (as it doesn't wait for the full timing of
        // the trap event to complete before updating state), it's
        // needed to update the state as soon as possible.  This
        // prevents external agents from changing any specific state
        // that the trap need.
        cpu->trap(inst_fault, tid,
                  head_inst->notAnInst() ? nullStaticInstPtr :
                      head_inst->staticInst);

        // Exit state update mode to avoid accidental updating.
        thread[tid]->noSquashFromTC = false;

        commitStatus[tid] = TrapPending;

        DPRINTF(RxuCommit,
            "[tid:%i] [sn:%llu] Committing instruction with fault\n",
            tid, head_inst->seqNum);

        if (head_inst->traceData) {
            // We ignore ReExecution "faults" here as they are not real
            // (architectural) faults but signal flush/replays.
            if (debug::ExecFaulting
                && dynamic_cast<ReExec*>(inst_fault.get()) == nullptr) {

                head_inst->traceData->setFaulting(true);
                head_inst->traceData->setFetchSeq(head_inst->seqNum);
                head_inst->traceData->setCPSeq(thread[tid]->numOp);
                head_inst->traceData->dump();
            }
            delete head_inst->traceData;
            head_inst->traceData = NULL;
        }

        if (head_inst->pcState().instAddr() >= 0x80000000 && head_inst->pcState().instAddr() < 0x90000000)
            // printf("[sn:%lu] \"%s\" has a fault, pc: %.8lx, fault name: %s, trap_value: %.16lx\n", 
            //     head_inst->seqNum,
            //     head_inst->staticInst->disassemble(head_inst->pcState().instAddr()).c_str(),
            //     head_inst->pcState().instAddr(),
            //     inst_fault->name(), 
            //     std::dynamic_pointer_cast<RiscvISA::RiscvFault>(inst_fault)->trap_value());
            exitSimLoop("reached instruction: unknown");

        // Generate trap squash event.
        generateTrapEvent(tid, inst_fault);
        return false;
    }

    if (head_inst->isMicroVector() && !(head_inst->ori_inst->readyToCommit())) {
        DPRINTF(RxuCommit,
                "can't Commit microvector instruction [%s] with PC [%#x] and seqNum [%llu] commit_cnt[%i] exe_cnt[%i] specialUopRdyNum[%i].\n",
                head_inst->staticInst->disassemble(head_inst->pc->instAddr()),
                head_inst->pc->instAddr(), head_inst->seqNum, head_inst->ori_inst->vector_CanCommit_cnt,
                head_inst->ori_inst->vector_exe_cnt,
                head_inst->ori_inst->specialUopRdyNum);
        return false;
    }

    updateComInstStats(head_inst);

    if (head_inst->staticInst->disassemble(head_inst->pc->instAddr()) 
            == "jal zero, 0") {
        exitSimLoop("reached instruction: jal zero, 0");
    }

    switch (head_inst->ibuffer_id)
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

    if(head_inst->numDestRegs() == 0 || head_inst->destRegIdx(0).isZeroReg()) {
        head_inst->odd_inst = rotating;
        rotating = !rotating;
    } else if (head_inst->ifrenamedest){
        PhysRegIdPtr dest_reg = head_inst->renamedDestIdx(0);
        head_inst->odd_inst = dest_reg->flatIndex() & 1 ? true : false;
    }
    if (head_inst->odd_inst) {
            stats.DestOdd++;
        } else {
            stats.DestEven++;
        }
        if (head_inst->ibuffer_id == 0 || head_inst->ibuffer_id == 4 || head_inst->ibuffer_id == 1 || head_inst->ibuffer_id == 5) {
            if (head_inst->odd_inst) {
                stats.DestOddIntNormal++;
            } else {
                stats.DestEvenIntNormal++;
            }
        } else if (head_inst->ibuffer_id == 2 || head_inst->ibuffer_id == 3) {
            if (head_inst->odd_inst) {
                stats.DestOddIntSpecial++;
            } else {
                stats.DestEvenIntSpecial++;
            }
        } else if (head_inst->ibuffer_id == 6 || head_inst->ibuffer_id == 7) {
            if (head_inst->odd_inst) {
                stats.DestOddLoad++;
            } else {
                stats.DestEvenLoad++;
            }
        } else if (head_inst->ibuffer_id == 8 || head_inst->ibuffer_id == 13 || head_inst->ibuffer_id == 9 || head_inst->ibuffer_id == 10) {
            if (head_inst->odd_inst) {
                stats.DestOddFpNormal++;
            } else {
                stats.DestEvenFpNormal++;
            }
        } else if (head_inst->ibuffer_id == 22 || head_inst->ibuffer_id == 23) {
            if (head_inst->odd_inst) {
                stats.DestOddVector++;
            } else {
                stats.DestEvenVector++;
            }
        }

    DPRINTF(Spike,
            "Committing instruction [%s] with PC [%#x] and seqNum [%llu].\n",
            head_inst->staticInst->disassemble(head_inst->pc->instAddr()),
            head_inst->pc->instAddr(), head_inst->seqNum);
    if(head_inst->fromWhichContainer[0]){
        stats.fetch_latency1++;
    }
    else if(head_inst->fromWhichContainer[1]){
        stats.fetch_latency2++;
    }
    else if(head_inst->fromWhichContainer[2]){
        stats.fetch_latency3andover++;
    }

    DPRINTF(InstStream, "pc=%#x inst=\"%s\" pa=%#x\n",
            head_inst->pc->instAddr(),
            head_inst->staticInst->disassemble(head_inst->pc->instAddr()),
            head_inst->physEffAddr);

    DPRINTF(RxuCommit,
            "[tid:%i] [sn:%llu] Committing instruction with PC %s\n",
            tid, head_inst->seqNum, head_inst->pcState());
    
    DPRINTF(Cmodel_inst,
    "PC: %s inst code:%x  \n",head_inst->pcState(),head_inst->staticInst->machInst.instcode);
    
    DPRINTF(CommitInst,
    "[tid:%i] [sn:%llu] opcode:%x Committing instruction with PC %s\n",
    tid, head_inst->seqNum,head_inst->staticInst->machInst.opcode, head_inst->pcState());

    DPRINTF(CommitInst, "[tid:%i] Instruction is: %s\n", tid,
            head_inst->staticInst->disassemble(head_inst->pc->instAddr()));
    
    if (head_inst->isControl()) {
        DPRINTF(RxuBPU,
                "[sn:%llu], BranchIndex: [%i], PC: [%#x], conditional: [%d], Bank: [%i], Miss: [%d]\n",
                head_inst->seqNum,commitBranchIndex, head_inst->pcState().instAddr(), head_inst->isCondCtrl(), (head_inst->pcState().instAddr()>>2)&0b1111, head_inst->mispredicted());
        commitBranchIndex++;
    }

    if (head_inst->isControl() && head_inst->readPredTaken0() &&
        (head_inst->readPredTarg().instAddr() != head_inst->readPredTarg0().instAddr())) {
        
        if (cpu->bpu0.l0btb.getEntryCtr(head_inst->pcState().instAddr()) == 0) {
            cpu->bpu0.l0btb.invalidateEntry(head_inst->pcState().instAddr());

            DPRINTF(RxuCommit, "br_PC: [%#x], TPC: [%#x] invalidated from L0BTB\n"
                    , head_inst->pcState().instAddr(), head_inst->readPredTarg0().instAddr());
        
            cpu->bpu0.mapper_l0btb.removeMapping(head_inst->readPredTarg0().instAddr(), head_inst->pcState().instAddr());
        } else {
            cpu->bpu0.l0btb.entryCtrDown(head_inst->pcState().instAddr());
            DPRINTF(RxuCommit, "br_PC: [%#x], TPC: [%#x] cnt = %i.\n"
                    , head_inst->pcState().instAddr(), head_inst->readPredTarg0().instAddr(), cpu->bpu0.l0btb.getEntryCtr(head_inst->pcState().instAddr()));
        }
    }

    if (head_inst->isControl() && (head_inst->readPredTarg().instAddr() == head_inst->readPredTarg0().instAddr())) {
        bool reset = cpu->bpu0.l0btb.entryCtrReset(head_inst->pcState().instAddr());
        if (reset)
            DPRINTF(RxuCommit, "br_PC: [%#x], TPC: [%#x] cnt reset, cnt = %i.\n"
                    , head_inst->pcState().instAddr(), head_inst->readPredTarg0().instAddr(),
                    cpu->bpu0.l0btb.getEntryCtr(head_inst->pcState().instAddr()));
    }

    if (head_inst->isControl() && !head_inst->isReturn() && 
            ((head_inst->pred_ctr == 7 && head_inst->readPredTaken()) || head_inst->staticInst->isRxuJump())) {
        std::unique_ptr<PCStateBase> tpc(head_inst->predPC->clone());
        auto previousEntry = cpu->bpu0.l0btb.updateEntry(head_inst->pcState().instAddr(), head_inst->predPC->instAddr(), std::move(tpc));
        if (!cpu->bpu0.L0BTB_Used[std::get<3>(previousEntry)]) {
            cpu->bpu0.L0BTB_Used[std::get<3>(previousEntry)] = true;
            cpu->bpu0.stats.L0BTB_Used++;
        }

        if (!std::get<0>(previousEntry)) {
            DPRINTF(RxuCommit, "br_PC: [%#x], TPC: [%#x] saved into L0BTB before.\n"
                    , head_inst->pcState().instAddr(), head_inst->predPC->instAddr());
        } else {
            if (std::get<1>(previousEntry) == 0) {
                DPRINTF(RxuCommit, "br_PC: [%#x], TPC: [%#x] saved into L0BTB.\n"
                        , head_inst->pcState().instAddr(), head_inst->predPC->instAddr());
            } else {
                ++cpu->bpu0.stats.L0BTB_conflict;
                DPRINTF(RxuCommit, "br_PC: [%#x], TPC: [%#x] saved into L0BTB and br_PC: [%#x], TPC: [%#x] removed from L0BTB.\n"
                        , head_inst->pcState().instAddr(), head_inst->predPC->instAddr(), std::get<1>(previousEntry), std::get<2>(previousEntry));

                cpu->bpu0.mapper_l0btb.removeMapping(std::get<2>(previousEntry), std::get<1>(previousEntry));
            }

            cpu->bpu0.mapper_l0btb.addMapping(head_inst->predPC->instAddr(), head_inst->pcState().instAddr());
        }
    }

    if (head_inst->bubbles != -1){
        stats.bubblesInstscommit.sample(head_inst->bubbles);
    }

    if (head_inst->isControl()) {
        if (head_inst->pred_ctr != -1)
            stats.bpuCtr.sample(head_inst->pred_ctr);
    }

    if (head_inst->isControl() && head_inst->mispredicted()) {
        cpu->baseStats.bpu1MissCommitCount++;
        if(head_inst->pcState().instAddr() == SpecificPC)
            cpu->baseStats.PCMissCommitCount++;
        if (head_inst->pred_ctr != -1)
            stats.bpuCtr_false.sample(head_inst->pred_ctr);
    } else if (head_inst->isControl() && !head_inst->mispredicted()) { 
        if (head_inst->pred_ctr != -1)
            stats.bpuCtr_true.sample(head_inst->pred_ctr);
    }

    if (head_inst->mispredicted0()) {
        cpu->loopStats[cpu->loopIndex]->bpu0MissCommitCount++;
        if (cpu->loopIndex >= 1 && cpu->loopIndex <= 100) {
            ++cpu->baseStats.bpu0MissCommitCount1_100loop;
        } else if (cpu->loopIndex >= 101 && cpu->loopIndex <= 200) {
            ++cpu->baseStats.bpu0MissCommitCount101_200loop;
        } else if (cpu->loopIndex >= 201 && cpu->loopIndex <= 300) {
            ++cpu->baseStats.bpu0MissCommitCount201_300loop;
        } else if (cpu->loopIndex >= 301 && cpu->loopIndex <= 400) {
            ++cpu->baseStats.bpu0MissCommitCount301_400loop;
        } else if (cpu->loopIndex >= 401 && cpu->loopIndex <= 500) {
            ++cpu->baseStats.bpu0MissCommitCount401_500loop;
        }
    }

    DPRINTF(RxuCommit, "[tid:%i] Instruction is: %s\n", tid,
            head_inst->staticInst->disassemble(head_inst->pc->instAddr()));
            
    if (head_inst->traceData) {
        head_inst->traceData->setFetchSeq(head_inst->seqNum);
        head_inst->traceData->setCPSeq(thread[tid]->numOp);
        head_inst->traceData->dump();
        delete head_inst->traceData;
        head_inst->traceData = NULL;
    }
    if (head_inst->isReturn()) {
        DPRINTF(RxuCommit,
                "[tid:%i] [sn:%llu] Return Instruction Committed PC %s \n",
                tid, head_inst->seqNum, head_inst->pcState());
    }

    // Update the commit rename map
    for (int i = 0; i < head_inst->numDestRegs(); i++) {
        renameMap[tid]->setEntry(head_inst->flattenedDestIdx(i),
                                 head_inst->renamedDestIdx(i));
    }

    // hardware transactional memory
    // the HTM UID is purely for correctness and debugging purposes
    if (head_inst->isHtmStart())
        dispipe3Stage->setLastRetiredHtmUid(tid, head_inst->getHtmTransactionUid());

    // Finally clear the head ROB entry.
    rob->retireHead(tid);

    #if TRACING_ON
        if (debug::RxuO3PipeView) {
            head_inst->commitTick = curTick() - head_inst->fetchTick;
        }
    #endif
    head_inst->commitTick = curTick() - head_inst->fetchTick;
    DPRINTF(RxuCommit, "[sn:%llu] instruction PC %s time:%i.\n",
                    head_inst->seqNum, head_inst->pcState(),head_inst->commitTick);
    // If this was a store, record it for this cycle.
    if (head_inst->isStore() || head_inst->isAtomic())
        committedStores[tid] = true;

    // Return true to indicate that we have committed an instruction.
    return true;
}

void
Commit::getInsts()
{

    // Read any renamed instructions and place them into the ROB.
    int insts_to_process = std::min((int)dispipe0Width, fromDispipe0->size);

    DPRINTF(RxuCommit, "Getting %d instructions from Dispipe0 stage.\n", insts_to_process);

    for (int inst_num = 0; inst_num < insts_to_process; ++inst_num) {
        const DynInstPtr &inst = fromDispipe0->insts[inst_num];
        ThreadID tid = inst->threadNumber;

        if (!inst->isSquashed() &&
            commitStatus[tid] != ROBSquashing &&
            commitStatus[tid] != TrapPending) {
            changedROBNumEntries[tid] = true;

            DPRINTF(RxuCommit, "[tid:%i] [sn:%llu] Inserting PC %s into ROB.\n",
                    tid, inst->seqNum, inst->pcState());

            rob->insertInst(inst);

            assert(rob->getThreadEntries(tid) <= rob->getMaxEntries(tid));

            youngestSeqNum[tid] = inst->seqNum;
        } else {
            DPRINTF(RxuCommit, "[tid:%i] [sn:%llu] "
                    "Instruction PC %s was squashed, skipping.\n",
                    tid, inst->seqNum, inst->pcState());
        }
    }
}

void
Commit::markCompletedInsts()
{
    // Grab completed insts out of the EW instruction queue, and mark
    // instructions completed within the ROB.
    for (int inst_num = 0; inst_num < fromEW->size; ++inst_num) {
        assert(fromEW->insts[inst_num]);
        if (!fromEW->insts[inst_num]->isSquashed()) {
            DPRINTF(RxuCommit, "[tid:%i] Marking PC %s, [sn:%llu] ready "
                    "within ROB.\n",
                    fromEW->insts[inst_num]->threadNumber,
                    fromEW->insts[inst_num]->pcState(),
                    fromEW->insts[inst_num]->seqNum);
            if(fromEW->insts[inst_num]->isEop() && fromEW->insts[inst_num]->isVIndexORStrideStore()){
                fromEW->insts[inst_num]->ori_inst->EopNotExe_Cnt--;
                DPRINTF(RxuCommit, "[sn:%llu] ST Eop had Executed,[sn:%llu] Uop Cnt = %d.\n"
                ,fromEW->insts[inst_num]->seqNum, fromEW->insts[inst_num]->ori_inst->seqNum, fromEW->insts[inst_num]->ori_inst->EopNotExe_Cnt);
                if(fromEW->insts[inst_num]->ori_inst->EopNotExe_Cnt == 0){
                    fromEW->insts[inst_num]->ori_inst->setExecuted();
                    fromEW->insts[inst_num]->ori_inst->setCanCommit();
                }
            }
            // Mark the instruction as ready to commit.
            fromEW->insts[inst_num]->setCanCommit();
        }
    }
}

void
Commit::updateComInstStats(const DynInstPtr &inst)
{
    ThreadID tid = inst->threadNumber;

    if (inst->isVector() && inst->isLastMicroop()) cpu->cpuStats.committedMacroVectorInsts++;

    if (!inst->isMicroop() || inst->isLastMicroop()) {
        cpu->commitStats[tid]->numInsts++;
        cpu->baseStats.numInsts++;
        stats.commitRetiredInsts++;

        cpu->curMacroCommitInsts++;

        if (cpu->loopIndex >= 1 && cpu->loopIndex <= 500) {
            cpu->baseStats.numInsts1_500loop++;
        } else if (cpu->loopIndex >= 501 && cpu->loopIndex <= 1000) {
            cpu->baseStats.numInsts501_1000loop++;
        } else if (cpu->loopIndex >= 1001 && cpu->loopIndex <= 1500) {
            cpu->baseStats.numInsts1001_1500loop++;
        } else if (cpu->loopIndex >= 1501 && cpu->loopIndex <= 2000) {
            cpu->baseStats.numInsts1501_2000loop++;
        }

        if (cpu->loopIndex >= 1 && cpu->loopIndex <= 100) {
            cpu->baseStats.numInsts1_100loop++;
        } else if (cpu->loopIndex >= 101 && cpu->loopIndex <= 200) {
            cpu->baseStats.numInsts101_200loop++;
        } else if (cpu->loopIndex >= 201 && cpu->loopIndex <= 300) {
            cpu->baseStats.numInsts201_300loop++;
        } else if (cpu->loopIndex >= 301 && cpu->loopIndex <= 400) {
            cpu->baseStats.numInsts301_400loop++;
        } else if (cpu->loopIndex >= 401 && cpu->loopIndex <= 500) {
            cpu->baseStats.numInsts401_500loop++;
        } else if (cpu->loopIndex >= 501 && cpu->loopIndex <= 600) {
            cpu->baseStats.numInsts501_600loop++;
        } else if (cpu->loopIndex >= 601 && cpu->loopIndex <= 700) {
            cpu->baseStats.numInsts601_700loop++;
        } else if (cpu->loopIndex >= 701 && cpu->loopIndex <= 800) {
            cpu->baseStats.numInsts701_800loop++;
        } else if (cpu->loopIndex >= 801 && cpu->loopIndex <= 900) {
            cpu->baseStats.numInsts801_900loop++;
        } else if (cpu->loopIndex >= 901 && cpu->loopIndex <= 1000) {
            cpu->baseStats.numInsts901_1000loop++;
        } else if (cpu->loopIndex >= 1001 && cpu->loopIndex <= 1100) {
            cpu->baseStats.numInsts1001_1100loop++;
        } else if (cpu->loopIndex >= 1101 && cpu->loopIndex <= 1200) {
            cpu->baseStats.numInsts1101_1200loop++;
        } else if (cpu->loopIndex >= 1201 && cpu->loopIndex <= 1300) {
            cpu->baseStats.numInsts1201_1300loop++;
        } else if (cpu->loopIndex >= 1301 && cpu->loopIndex <= 1400) {
            cpu->baseStats.numInsts1301_1400loop++;
        } else if (cpu->loopIndex >= 1401 && cpu->loopIndex <= 1500) {
            cpu->baseStats.numInsts1401_1500loop++;
        } else if (cpu->loopIndex >= 1501 && cpu->loopIndex <= 1600) {
            cpu->baseStats.numInsts1501_1600loop++;
        } else if (cpu->loopIndex >= 1601 && cpu->loopIndex <= 1700) {
            cpu->baseStats.numInsts1601_1700loop++;
        } else if (cpu->loopIndex >= 1701 && cpu->loopIndex <= 1800) {
            cpu->baseStats.numInsts1701_1800loop++;
        } else if (cpu->loopIndex >= 1801 && cpu->loopIndex <= 1900) {
            cpu->baseStats.numInsts1801_1900loop++;
        } else if (cpu->loopIndex >= 1901 && cpu->loopIndex <= 2000) {
            cpu->baseStats.numInsts1901_2000loop++;
        }
        ++cpu->baseStats.xhnumInsts;
        if (cpu->loopxhIndex >= 1 && cpu->loopxhIndex <= 110) {
            ++cpu->baseStats.xhnumInsts1_110loop;
        } 
        if (cpu->loopxhIndex >= 1 && cpu->loopxhIndex <= 30) {
            ++cpu->baseStats.xhnumInsts1_30loop;
        }
        if (cpu->loopxhIndex >= 1 && cpu->loopxhIndex <= 64) {
            ++cpu->baseStats.xhnumInsts1_64loop;
        }
        if (cpu->loopxhIndex >= 1 && cpu->loopxhIndex <= 134) {
            ++cpu->baseStats.xhnumInsts1_134loop;
        }
        if (cpu->loopxhIndex >= 1 && cpu->loopxhIndex <= 1024) {
            ++cpu->baseStats.xhnumInsts1_1024loop;
        }
        if (cpu->loopxhIndex >= 50 && cpu->loopxhIndex <= 110) {
            ++cpu->baseStats.xhnumInsts50_110loop;
        }
        if (cpu->loopxhIndex >= 20 && cpu->loopxhIndex <= 30) {
            ++cpu->baseStats.xhnumInsts20_30loop;
        }
        if (cpu->loopxhIndex >= 30 && cpu->loopxhIndex <= 64) {
            ++cpu->baseStats.xhnumInsts30_64loop;
        }
        if (cpu->loopxhIndex >= 60 && cpu->loopxhIndex <= 134) {
            ++cpu->baseStats.xhnumInsts60_134loop;
        }
        if (cpu->loopxhIndex >= 500 && cpu->loopxhIndex <= 1024) {
            ++cpu->baseStats.xhnumInsts500_1024loop;
        }
        if (cpu->loopxhIndex >= 1000 && cpu->loopxhIndex <= 1024) {
            ++cpu->baseStats.xhnumInsts1000_1024loop;
        }

        if (inst->isControl() && !inst->isReturn() && !inst->isCall()) {
            cpu->loopStats[cpu->loopIndex]->retiredBranchInsts++;
            cpu->loopStats[cpu->loopxhIndex]->xhretiredBranchInsts++;
            stats.retiredBranchInsts++;
            DPRINTF(Br_code, "inst code:%x.\n",inst->staticInst->machInst.instcode);
            cpu->baseStats.retiredBranchInsts++;
            if(inst->pcState().instAddr() == SpecificPC)
                cpu->baseStats.PCretiredBranchInsts++;
            if (cpu->loopIndex >= 1 && cpu->loopIndex <= 100) {
                ++cpu->baseStats.retiredBranchInsts1_100loop;
            } else if (cpu->loopIndex >= 101 && cpu->loopIndex <= 200) {
                ++cpu->baseStats.retiredBranchInsts101_200loop;
            } else if (cpu->loopIndex >= 201 && cpu->loopIndex <= 300) {
                ++cpu->baseStats.retiredBranchInsts201_300loop;
            } else if (cpu->loopIndex >= 301 && cpu->loopIndex <= 400) {
                ++cpu->baseStats.retiredBranchInsts301_400loop;
            } else if (cpu->loopIndex >= 401 && cpu->loopIndex <= 500) {
                ++cpu->baseStats.retiredBranchInsts401_500loop;
            }


            if (cpu->loopxhIndex >= 1 && cpu->loopxhIndex <= 110) {
            ++cpu->baseStats.xhretiredBranchInsts1_110loop;
            }
            if (cpu->loopxhIndex >= 1 && cpu->loopxhIndex <= 30) {
                ++cpu->baseStats.xhretiredBranchInsts1_30loop;
            }
            if (cpu->loopxhIndex >= 1 && cpu->loopxhIndex <= 64) {
                ++cpu->baseStats.xhretiredBranchInsts1_64loop;
            }
            if (cpu->loopxhIndex >= 1 && cpu->loopxhIndex <= 134) {
                ++cpu->baseStats.xhretiredBranchInsts1_134loop;
            }
            if (cpu->loopxhIndex >= 1 && cpu->loopxhIndex <= 1024) {
                ++cpu->baseStats.xhretiredBranchInsts1_1024loop;
            }
            if (cpu->loopxhIndex >= 50 && cpu->loopxhIndex <= 110) {
                ++cpu->baseStats.xhretiredBranchInsts50_110loop;
            }
            if (cpu->loopxhIndex >= 20 && cpu->loopxhIndex <= 30) {
                ++cpu->baseStats.xhretiredBranchInsts20_30loop;
            }
            if (cpu->loopxhIndex >= 30 && cpu->loopxhIndex <= 64) {
                ++cpu->baseStats.xhretiredBranchInsts30_64loop;
            }
            if (cpu->loopxhIndex >= 60 && cpu->loopxhIndex <= 134) {
                ++cpu->baseStats.xhretiredBranchInsts60_134loop;
            }
            if (cpu->loopxhIndex >= 500 && cpu->loopxhIndex <= 1024) {
                ++cpu->baseStats.xhretiredBranchInsts500_1024loop;
            }
            if (cpu->loopxhIndex >= 1000 && cpu->loopxhIndex <= 1024) {
                ++cpu->baseStats.xhretiredBranchInsts1000_1024loop;
            }
        }

        cpu->loopStats[cpu->loopIndex]->commitRetiredInsts++;
        cpu->loopStats[cpu->loopxhIndex]->xhcommitRetiredInsts++;

        if (cpu->loopIndex >= 1 && cpu->loopIndex <= 100) {
            ++cpu->baseStats.commitRetiredInsts1_100loop;
        } else if (cpu->loopIndex >= 101 && cpu->loopIndex <= 200) {
            ++cpu->baseStats.commitRetiredInsts101_200loop;
        } else if (cpu->loopIndex >= 201 && cpu->loopIndex <= 300) {
            ++cpu->baseStats.commitRetiredInsts201_300loop;
        } else if (cpu->loopIndex >= 301 && cpu->loopIndex <= 400) {
            ++cpu->baseStats.commitRetiredInsts301_400loop;
        } else if (cpu->loopIndex >= 401 && cpu->loopIndex <= 500) {
            ++cpu->baseStats.commitRetiredInsts401_500loop;
        }

        if (cpu->loopxhIndex >= 1 && cpu->loopxhIndex <= 110) {
            ++cpu->baseStats.xhcommitRetiredInsts1_110loop;
        } else if (cpu->loopxhIndex >= 1 && cpu->loopxhIndex <= 30) {
            ++cpu->baseStats.xhcommitRetiredInsts1_30loop;
        } else if (cpu->loopxhIndex >= 1 && cpu->loopxhIndex <= 64) {
            ++cpu->baseStats.xhcommitRetiredInsts1_64loop;
        } else if (cpu->loopxhIndex >= 1 && cpu->loopxhIndex <= 134) {
            ++cpu->baseStats.xhcommitRetiredInsts1_134loop;
        } else if (cpu->loopxhIndex >= 1 && cpu->loopxhIndex <= 1024) {
            ++cpu->baseStats.xhcommitRetiredInsts1_1024loop;
        } else if (cpu->loopxhIndex >= 50 && cpu->loopxhIndex <= 110) {
            ++cpu->baseStats.xhcommitRetiredInsts50_110loop;
        } else if (cpu->loopxhIndex >= 20 && cpu->loopxhIndex <= 30) {
            ++cpu->baseStats.xhcommitRetiredInsts20_30loop;
        } else if (cpu->loopxhIndex >= 30 && cpu->loopxhIndex <= 64) {
            ++cpu->baseStats.xhcommitRetiredInsts30_64loop;
        } else if (cpu->loopxhIndex >= 60 && cpu->loopxhIndex <= 134) {
            ++cpu->baseStats.xhcommitRetiredInsts60_134loop;
        } else if (cpu->loopxhIndex >= 500 && cpu->loopxhIndex <= 1024) {
            ++cpu->baseStats.xhcommitRetiredInsts500_1024loop;
        } else if (cpu->loopxhIndex >= 1000 && cpu->loopxhIndex <= 1024) {
            ++cpu->baseStats.xhcommitRetiredInsts1000_1024loop;
        }

        if (curTick() <= 6000000) {
            cpu->baseStats.xhnumInsts1++;
        }
        if (curTick() > 6000000 && curTick() <= 12000000) {
            cpu->baseStats.xhnumInsts2++;
        }
    }
    cpu->commitStats[tid]->numOps++;

    cpu->curMicroCommitInsts++;

    // To match the old model, don't count nops and instruction
    // prefetches towards the total commit count.
    if (!inst->isNop() && !inst->isInstPrefetch()) {
        cpu->instDone(tid, inst);
    }

    //
    //  Control Instructions
    //
    cpu->commitStats[tid]->updateComCtrlStats(inst->staticInst);

    //
    //  Memory references
    //
    if (inst->isMemRef()) {
        cpu->commitStats[tid]->numMemRefs++;

        if (inst->isLoad()) {
            cpu->commitStats[tid]->numLoadInsts++;
        }

        if (inst->isStore()) {
            cpu->commitStats[tid]->numStoreInsts++;
        }
    }

    if (inst->isFullMemBarrier()) {
        stats.membars[tid]++;
    }

    if (inst->isAtomic()) {
        stats.amos[tid]++;
    }

    // if (inst->staticInst->isSc()) {
    //     stats.scs[tid]++;
    // }

    // if (inst->staticInst->isLr()) {
    //     stats.lrs[tid]++;
    // }

    // Integer Instruction
    if (inst->isInteger()) {
        cpu->commitStats[tid]->numIntInsts++;
    }

    // Floating Point Instruction
    if (inst->isFloating()) {
        cpu->commitStats[tid]->numFpInsts++;
    }
    // Vector Instruction
    if (inst->isVector()) {
        cpu->commitStats[tid]->numVecInsts++;
    }

    // Function Calls
    if (inst->isCall())
        stats.functionCalls[tid]++;

}

////////////////////////////////////////
//                                    //
//  SMT COMMIT POLICY MAINTAINED HERE //
//                                    //
////////////////////////////////////////
ThreadID
Commit::getCommittingThread()
{
    if (numThreads > 1) {
        switch (commitPolicy) {
          case RxuCommitPolicy::RoundRobin:
            return roundRobin();

          case RxuCommitPolicy::OldestReady:
            return oldestReady();

          default:
            return InvalidThreadID;
        }
    } else {
        assert(!activeThreads->empty());
        ThreadID tid = activeThreads->front();

        if (commitStatus[tid] == Running ||
            commitStatus[tid] == Idle ||
            commitStatus[tid] == FetchTrapPending) {
            return tid;
        } else {
            return InvalidThreadID;
        }
    }
}

ThreadID
Commit::roundRobin()
{
    std::list<ThreadID>::iterator pri_iter = priority_list.begin();
    std::list<ThreadID>::iterator end      = priority_list.end();

    while (pri_iter != end) {
        ThreadID tid = *pri_iter;

        if (commitStatus[tid] == Running ||
            commitStatus[tid] == Idle ||
            commitStatus[tid] == FetchTrapPending) {

            if (rob->isHeadReady(tid)) {
                priority_list.erase(pri_iter);
                priority_list.push_back(tid);

                return tid;
            }
        }

        pri_iter++;
    }

    return InvalidThreadID;
}

ThreadID
Commit::oldestReady()
{
    unsigned oldest = 0;
    unsigned oldest_seq_num = 0;
    bool first = true;

    std::list<ThreadID>::iterator threads = activeThreads->begin();
    std::list<ThreadID>::iterator end = activeThreads->end();

    while (threads != end) {
        ThreadID tid = *threads++;

        if (!rob->isEmpty(tid) &&
            (commitStatus[tid] == Running ||
             commitStatus[tid] == Idle ||
             commitStatus[tid] == FetchTrapPending)) {

            if (rob->isHeadReady(tid)) {

                const DynInstPtr &head_inst = rob->readHeadInst(tid);

                if (first) {
                    oldest = tid;
                    oldest_seq_num = head_inst->seqNum;
                    first = false;
                } else if (head_inst->seqNum < oldest_seq_num) {
                    oldest = tid;
                    oldest_seq_num = head_inst->seqNum;
                }
            }
        }
    }

    if (!first) {
        return oldest;
    } else {
        return InvalidThreadID;
    }
}

} // namespace rxuo3
} // namespace gem5
