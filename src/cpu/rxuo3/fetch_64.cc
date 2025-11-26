#include "cpu/rxuo3/fetch.hh"

#include "arch/generic/tlb.hh"
#include "base/random.hh"
#include "base/types.hh"
#include "cpu/base.hh"
#include "cpu/exetrace.hh"
#include "cpu/nop_static_inst.hh"
#include "cpu/rxuo3/cpu.hh"
#include "cpu/rxuo3/dyn_inst.hh"
#include "cpu/rxuo3/limits.hh"
#include "debug/RxuActivity.hh"
#include "debug/Drain.hh"
#include "debug/RxuFetch.hh"
#include "debug/RxuUC.hh"
#include "debug/RxuDecode.hh"
#include "debug/RxuO3CPU.hh"
#include "debug/RxuO3PipeView.hh"
#include "mem/packet.hh"
#include "params/BaseRxuO3CPU.hh"
#include "sim/byteswap.hh"
#include "sim/core.hh"
#include "sim/eventq.hh"
#include "sim/full_system.hh"
#include "sim/system.hh"

namespace gem5
{

namespace rxuo3
{

Fetch::IcachePort::IcachePort(Fetch *_fetch, CPU *_cpu) :
        RequestPort(_cpu->name() + ".icache_port"), fetch(_fetch)
{}

// Fetch::UopCacheCompletion::UopCacheCompletion(Fetch *_fetch)
//     : fetch(_fetch)
// {
//     fetch->ucHit.clear();
//     fetch->ucAccessDone = false;
//     fetch->ucBlockResponseAddr = 0;
//     cachelines = 4;
// }

// void
// Fetch::UopCacheCompletion::process()
// {
//     // if (curTick() >= 126891500) {
//     //     assert(fetch->cpu->iBandLB.instbuffer.at(2));
//     //     DPRINTF(RxuFetch, "test57: [sn:%llu].\n",
//     //                 fetch->cpu->iBandLB.instbuffer.at(2)->seqNum);
//     // }
//     auto x = fetch->uc->access(*pc, fetch->ucBlockResponseAddr, cachelines);

//     // if (curTick() >= 126891500) {
//     //     assert(fetch->cpu->iBandLB.instbuffer.at(2));
//     //     DPRINTF(RxuFetch, "test64: [sn:%llu].\n",
//     //                 fetch->cpu->iBandLB.instbuffer.at(2)->seqNum);
//     // }

//     fetch->ucHit.more(x);

//     // if (curTick() >= 126891500) {
//     //     assert(fetch->cpu->iBandLB.instbuffer.at(2));
//     //     DPRINTF(RxuFetch, "test72: [sn:%llu].\n",
//     //                 fetch->cpu->iBandLB.instbuffer.at(2)->seqNum);
//     // }

//     DPRINTF(RxuFetch, "Hit info: %#x, queue size: %i.\n", fetch->ucHit.hit, fetch->instsFromUC.size());
//     fetch->ucAccessDone = true;
//     fetch->ucBlockOffset = 0;
// }

// const char *
// Fetch::UopCacheCompletion::description() const
// {
//     return "Uop cache Finish ACCESS.";
// }

Fetch::UopCacheStoreCompletion::UopCacheStoreCompletion(Fetch *_fetch) :
    fetch(_fetch)
{
}

void 
Fetch::UopCacheStoreCompletion::setQueue(std::list<StaticInstPtr> _instList) {
    while (!_instList.empty()) {
        instList.push_back(_instList.front());
        _instList.pop_front();
    }
}

void 
Fetch::UopCacheStoreCompletion::process()
{ 
    fetch->uc->update(pc, instList, pc_list);
}

const char *
Fetch::UopCacheStoreCompletion::description() const
{
    return "Uop cache Finish store.";
}

Fetch::Fetch(CPU *_cpu, const BaseRxuO3CPUParams &params)
    : fetchPolicy(params.smtFetchPolicy),
      cpu(_cpu),
    //   branchPred(nullptr),
      decodeToFetchDelay(params.decodeToFetchDelay),
      BPUToFetchDelay(params.Bpu0ToFetchDelay),
      // -- add by hongfei.liu ----------------------
    //   predisqToFetchDelay(params.predisqToFetchDelay),
    //   ewToFetchDelay(params.ewToFetchDelay),
      commitToFetchDelay(params.commitToFetchDelay),
      fetchWidth(params.fetchWidth),
      bpuWidth(params.bpu0Width),
      retryPkt(NULL),
      retryTid(InvalidThreadID),
      cacheBlkSize(cpu->cacheLineSize()),
      fetchBufferSize(params.fetchBufferSize),
      fetchBufferMask(fetchBufferSize - 1),
      fetchQueueSize(params.fetchQueueSize),
      numThreads(params.numThreads),
      numFetchingThreads(params.smtNumFetchingThreads),
      icachePort(this, _cpu),
      finishTranslationEvent(this), 
      initBPUDelay(params.system->bpuDelay()),
      fetchStats(_cpu, this)
{
    if (numThreads > MaxThreads)
        fatal("numThreads (%d) is larger than compiled limit (%d),\n"
              "\tincrease MaxThreads in src/cpu/rxuo3/limits.hh\n",
              numThreads, static_cast<int>(MaxThreads));
    if (fetchWidth > MaxWidth)
        fatal("fetchWidth (%d) is larger than compiled limit (%d),\n"
             "\tincrease MaxWidth in src/cpu/rxuo3/limits.hh\n",
             fetchWidth, static_cast<int>(MaxWidth));
    if (fetchBufferSize > cacheBlkSize)
        fatal("fetch buffer size (%u bytes) is greater than the cache "
              "block size (%u bytes)\n", fetchBufferSize, cacheBlkSize);
    if (cacheBlkSize % fetchBufferSize)
        fatal("cache block (%u bytes) is not a multiple of the "
              "fetch buffer (%u bytes)\n", cacheBlkSize, fetchBufferSize);

    for (int i = 0; i < MaxThreads; i++) {
        fetchStatus[i] = Idle;
        decoder[i] = nullptr;
        pc[i].reset(params.isa[0]->newPCState());
        fetchOffset[i] = 0;
        macroop[i] = nullptr;
        delayedCommit[i] = false;
        memReq[i] = nullptr;
        stalls[i] = {false, false};
        // anotherMemReq[i] = nullptr;
        fetchBuffer[i] = NULL;
        fetchBufferPC[i] = 0;
        fetchBufferValid[i] = false;
        lastIcacheStall[i] = 0;
        issuePipelinedIfetch[i] = false;
        set(lpc[i], pc[i]);
    }

    // branchPred = params.branchPred;
    // if (params.system->useLtage()) {
    //     // branchPred = params.branchPredLTAGE;
    // } else if (params.system->useTournamentBP()){
    //     branchPred = params.branchPredTournamentBP;
    // } else if (params.system->useLocalBP()) {
    //     branchPred = params.branchPredLocalBP;
    // } else if (params.system->useBiModeBP()) {
    //     branchPred = params.branchPredBiModeBP;
    // } else {
    //     branchPred = params.branchPredTAGE;
    // }

    for (ThreadID tid = 0; tid < numThreads; tid++) {
        decoder[tid] = params.decoder[tid];
        // Create space to buffer the cache line data,
        // which may not hold the entire cache line.
        fetchBuffer[tid] = new uint8_t[fetchBufferSize];
    }

    // Get the size of an instruction.
    instSize = decoder[0]->moreBytesSize();
    // loopBufferActive = false;
    ucBlockResponseAddr = 0;

    _tid = 0;
    BPUdelay = 0;

    // firstDataBuf = new uint8_t[fetchBufferSize];
    // secondDataBuf = new uint8_t[fetchBufferSize];
}

std::string Fetch::name() const { return cpu->name() + ".fetch"; }

void
Fetch::regProbePoints()
{
    ppFetch = new ProbePointArg<DynInstPtr>(cpu->getProbeManager(), "Fetch");
    ppFetchRequestSent = new ProbePointArg<RequestPtr>(cpu->getProbeManager(),
                                                       "FetchRequest");

}

Fetch::FetchStatGroup::FetchStatGroup(CPU *cpu, Fetch *fetch)
    : statistics::Group(cpu, "fetch"),
    ADD_STAT(predictedBranches, statistics::units::Count::get(),
             "Number of branches that fetch has predicted taken"),
    ADD_STAT(cycles, statistics::units::Cycle::get(),
             "Number of cycles fetch has run and was not squashing or "
             "blocked"),
    ADD_STAT(squashCycles, statistics::units::Cycle::get(),
             "Number of cycles fetch has spent squashing"),
    ADD_STAT(tlbCycles, statistics::units::Cycle::get(),
             "Number of cycles fetch has spent waiting for tlb"),
    ADD_STAT(idleCycles, statistics::units::Cycle::get(),
             "Number of cycles fetch was idle"),
    ADD_STAT(blockedCycles, statistics::units::Cycle::get(),
             "Number of cycles fetch has spent blocked"),
    ADD_STAT(miscStallCycles, statistics::units::Cycle::get(),
             "Number of cycles fetch has spent waiting on interrupts, or bad "
             "addresses, or out of MSHRs"),
    ADD_STAT(pendingDrainCycles, statistics::units::Cycle::get(),
             "Number of cycles fetch has spent waiting on pipes to drain"),
    ADD_STAT(noActiveThreadStallCycles, statistics::units::Cycle::get(),
             "Number of stall cycles due to no active thread to fetch from"),
    ADD_STAT(pendingTrapStallCycles, statistics::units::Cycle::get(),
             "Number of stall cycles due to pending traps"),
    ADD_STAT(pendingQuiesceStallCycles, statistics::units::Cycle::get(),
             "Number of stall cycles due to pending quiesce instructions"),
    ADD_STAT(icacheWaitRetryStallCycles, statistics::units::Cycle::get(),
             "Number of stall cycles due to full MSHR"),
    ADD_STAT(cacheLines, statistics::units::Count::get(),
             "Number of cache lines fetched"),
    ADD_STAT(icacheSquashes, statistics::units::Count::get(),
             "Number of outstanding Icache misses that were squashed"),
    ADD_STAT(tlbSquashes, statistics::units::Count::get(),
             "Number of outstanding ITLB misses that were squashed"),
    ADD_STAT(nisnDist, statistics::units::Count::get(),
             "Number of instructions fetched each cycle (Total)"),
    ADD_STAT(idleRate, statistics::units::Ratio::get(),
             "Ratio of cycles fetch was idle",
             idleCycles / cpu->baseStats.numCycles),
    ADD_STAT(sendCachelineCycles, statistics::units::Cycle::get(),
            "Number of cycles fetch can send a cacheline"),
    ADD_STAT(noCachelineCycles, statistics::units::Cycle::get(),
            "Number of cycles fetch can't send a cacheline due to bubble"),
    ADD_STAT(stallCycles, statistics::units::Cycle::get(),
            "Number of cycles fetch can't send a cacheline due to stall/squash"),
    ADD_STAT(sendCachelineRate, statistics::units::Rate<
                statistics::units::Count, statistics::units::Count>::get(),
            "Rate of fetch can send a cacheline"),
    ADD_STAT(noCachelineRate, statistics::units::Rate<
                statistics::units::Count, statistics::units::Count>::get(),
            "Rate of fetch can't send a cacheline due to bubble"),
    ADD_STAT(stallRate, statistics::units::Rate<
                statistics::units::Count, statistics::units::Count>::get(),
            "Rate of fetch can't send a cacheline due to stall/squash"),
    ADD_STAT(bubbleCount, "Distribution of number of bubble detected by fetch"),
    ADD_STAT(fetchOutInsts, "Distribution of number of instructions sent by fetch")
{
        predictedBranches
            .prereq(predictedBranches); 
        cycles
            .prereq(cycles);
        squashCycles
            .prereq(squashCycles);
        tlbCycles
            .prereq(tlbCycles);
        idleCycles
            .prereq(idleCycles);
        blockedCycles
            .prereq(blockedCycles);
        cacheLines
            .prereq(cacheLines);
        miscStallCycles
            .prereq(miscStallCycles);
        pendingDrainCycles
            .prereq(pendingDrainCycles);
        noActiveThreadStallCycles
            .prereq(noActiveThreadStallCycles);
        pendingTrapStallCycles
            .prereq(pendingTrapStallCycles);
        pendingQuiesceStallCycles
            .prereq(pendingQuiesceStallCycles);
        icacheWaitRetryStallCycles
            .prereq(icacheWaitRetryStallCycles);
        icacheSquashes
            .prereq(icacheSquashes);
        tlbSquashes
            .prereq(tlbSquashes);
        nisnDist
            .init(/* base value */ 0,
              /* last value */ fetch->fetchWidth,
              /* bucket size */ 1)
            .flags(statistics::pdf);
        idleRate
            .prereq(idleRate);

        sendCachelineCycles.prereq(sendCachelineCycles);
        noCachelineCycles.prereq(noCachelineCycles);
        stallCycles.prereq(stallCycles);

        sendCachelineRate
            .precision(6);
        sendCachelineRate = sendCachelineCycles / (sendCachelineCycles + noCachelineCycles + stallCycles);

        noCachelineRate
            .precision(6);
        noCachelineRate = noCachelineCycles / (sendCachelineCycles + noCachelineCycles + stallCycles);

        stallRate
            .precision(6);
        stallRate = stallCycles / (sendCachelineCycles + noCachelineCycles + stallCycles);

        bubbleCount
            .init(0, 1000, 1)
            .flags(statistics::nozero);

        fetchOutInsts
            .init(1, 16, 1)
            .flags(statistics::nozero);
}
void
Fetch::setTimeBuffer(TimeBuffer<TimeStruct> *time_buffer)
{
    timeBuffer = time_buffer;

    // Create wires to get information from proper places in time buffer.
    fromDecode = timeBuffer->getWire(-decodeToFetchDelay);
    fromBP0 = timeBuffer->getWire(-BPUToFetchDelay);
    fromBP1 = timeBuffer->getWire(-BPUToFetchDelay);
    // -- add by hongfei.liu ------------------------------
    // fromPredisq = timeBuffer->getWire(-predisqToFetchDelay);
    // ----------------------------------------------------
    // fromRename = timeBuffer->getWire(-renameToFetchDelay);
    // fromEW = timeBuffer->getWire(-ewToFetchDelay);
    fromCommit = timeBuffer->getWire(-commitToFetchDelay);
}

void
Fetch::setActiveThreads(std::list<ThreadID> *at_ptr)
{
    activeThreads = at_ptr;
}

void 
Fetch::setFetchQueue(TimeBuffer<FetchStruct> *fq_ptr) 
{
    toBP0 = fq_ptr->getWire(0);
}

void
Fetch::startupStage()
{
    assert(priorityList.empty());
    resetStage();

    // Fetch needs to start fetching instructions at the very beginning,
    // so it must start up in active state.
    switchToActive();
}

void
Fetch::clearStates(ThreadID tid)
{
    fetchStatus[tid] = Running;
    set(pc[tid], cpu->pcState(tid));
    fetchOffset[tid] = 0;
    macroop[tid] = NULL;
    delayedCommit[tid] = false;
    memReq[tid] = NULL;
    stalls[tid].bp = false;
    // anotherMemReq[tid] = NULL;
    stalls[tid].drain = false;
    fetchBufferPC[tid] = 0;
    fetchBufferValid[tid] = false;
    fetchQueue[tid].clear();
    // loopBufferActive = false;
    set(lpc[tid], pc[tid]);
    ucHit.clear();

    // TODO not sure what to do with priorityList for now
    // priorityList.push_back(tid);
}

void
Fetch::resetStage()
{
    numInst = 0;
    interruptPending = false;
    cacheBlocked = false;

    priorityList.clear();

    // Setup PC and nextPC with initial state.
    for (ThreadID tid = 0; tid < numThreads; ++tid) {
        fetchStatus[tid] = Running;
        set(pc[tid], cpu->pcState(tid));
        fetchOffset[tid] = 0;
        macroop[tid] = NULL;

        delayedCommit[tid] = false;
        memReq[tid] = NULL;
        // anotherMemReq[tid] = NULL;

        stalls[tid].bp = false;
        stalls[tid].drain = false;

        fetchBufferPC[tid] = 0;
        fetchBufferValid[tid] = false;

        fetchQueue[tid].clear();

        priorityList.push_back(tid);

        set(lpc[tid], pc[tid]);
    }

    wroteToTimeBuffer = false;
    _status = Inactive;
    // loopBufferActive = false;
    ucHit.clear();
}

void
Fetch::processCacheCompletion(PacketPtr pkt)
{
    ThreadID tid = cpu->contextToThread(pkt->req->contextId());

    // if (pkt->req->isMisalignedFetch() && (pkt->req == memReq[tid] || pkt->req == anotherMemReq[tid])) {
    //     DPRINTF(RxuFetch, "[tid:%i] Misaligned pkt receive.\n", tid);
    //     Addr anotherPC = 0;
    //     unsigned anotherSize = 0;
    //     if (pkt->req->getReqNum() == 1) {
    //         firstPkt[tid] = pkt;
    //         anotherPC = pkt->req->getVaddr() + 64 - pkt->req->getVaddr() % 64;
    //         anotherSize = fetchBufferSize - pkt->getSize();
    //     } else if (pkt->req->getReqNum() == 2) {
    //         secondPkt[tid] = pkt;
    //         anotherPC = pkt->req->getVaddr() - 64 + pkt->getSize();
    //         anotherSize = fetchBufferSize - pkt->getSize();
    //     }

    //     if (firstPkt[tid] == nullptr || secondPkt[tid] == nullptr) {
    //         DPRINTF(RxuFetch, "[tid:%i] Waiting for %s pkt.\n", tid,
    //                 firstPkt[tid] == nullptr ? "first" : "second");
    //         if (pkt->isRetriedPkt()) {
    //             DPRINTF(RxuFetch, "[tid:%i] Retried pkt.\n", tid);
    //             DPRINTF(RxuFetch, "[tid:%i] send next pkt, addr: %#x, size: %d\n",
    //                     tid, pkt->req->getVaddr() + 64 - pkt->req->getVaddr() % 64, 
    //                     fetchBufferSize - pkt->getSize());

    //             RequestPtr mem_req = std::make_shared<Request>(
    //                                 anotherPC, 
    //                                 anotherSize,
    //                                 Request::INST_FETCH, cpu->instRequestorId(), pkt->req->getPC(),
    //                                 cpu->thread[tid]->contextId());

    //             mem_req->taskId(cpu->taskId());

    //             mem_req->setMisalignedFetch();

    //             if (pkt->req->getReqNum() == 1) {
    //                 mem_req->setReqNum(2);
    //             } else if (pkt->req->getReqNum() == 2) {
    //                 mem_req->setReqNum(1);
    //             }

    //             anotherMemReq[tid] = memReq[tid];

    //             memReq[tid] = mem_req;

    //             fetchStatus[tid] = ItlbWait;
    //             FetchTranslation *trans = new FetchTranslation(this);
    //             cpu->mmu->translateTiming(mem_req, cpu->thread[tid]->getTC(),
    //                                       trans, BaseMMU::Execute);
    //         }
    //         return;
    //     } else {
    //         DPRINTF(RxuFetch, "[tid:%i] Received another pkt addr=%#lx, mem_req addr=%#lx.\n", tid,
    //                 pkt->getAddr(), pkt->req->getVaddr());

    //         // Copy two packets data into second packet
    //         firstPkt[tid]->getData(firstDataBuf);
    //         secondPkt[tid]->getData(secondDataBuf);
    //         if (memReq[tid]->getReqNum() == 2) {
    //             pkt = secondPkt[tid];
    //         } else {
    //             pkt = firstPkt[tid];
    //         }
    //         pkt->setData(firstDataBuf, 0, 0, firstPkt[tid]->getSize());
    //         pkt->setData(secondDataBuf, 0, firstPkt[tid]->getSize(), secondPkt[tid]->getSize());
    //     }
    // }

    DPRINTF(RxuFetch, "[tid:%i] Waking up from cache miss.\n", tid);
    assert(!cpu->switchedOut());

    // Only change the status if it's still waiting on the icache access
    // to return.
    if (fetchStatus[tid] != IcacheWaitResponse ||
        pkt->req != memReq[tid]) {
        // DPRINTF(RxuFetch, "delete pkt %#lx\n", pkt->getAddr());
        ++fetchStats.icacheSquashes;
        delete pkt;
        return;
    }

    memcpy(fetchBuffer[tid], pkt->getConstPtr<uint8_t>(), fetchBufferSize);
    fetchBufferValid[tid] = true;

    // Wake up the CPU (if it went to sleep and was waiting on
    // this completion event).
    cpu->wakeCPU();

    DPRINTF(RxuActivity, "[tid:%i] Activating fetch due to cache completion\n",
            tid);

    switchToActive();

    // Only switch to IcacheAccessComplete if we're not stalled as well.
    if (checkStall(tid)) {
        fetchStatus[tid] = Blocked;
    } else {
        fetchStatus[tid] = IcacheAccessComplete;
    }

    pkt->req->setAccessLatency();
    cpu->ppInstAccessComplete->notify(pkt);
    // Reset the mem req to NULL.
    // if (!pkt->req->isMisalignedFetch()) {
        delete pkt;
    // } else {
    //     delete firstPkt[tid];
    //     delete secondPkt[tid];
    //     firstPkt[tid] = nullptr;
    //     secondPkt[tid] = nullptr;
    // }
    memReq[tid] = NULL;
    // anotherMemReq[tid] = NULL;
}

void
Fetch::drainResume()
{
    for (ThreadID i = 0; i < numThreads; ++i) {
        stalls[i].bp = false;
        stalls[i].drain = false;
    }
}

void
Fetch::drainSanityCheck() const
{
    assert(isDrained());
    assert(retryPkt == NULL);
    assert(retryTid == InvalidThreadID);
    assert(!cacheBlocked);
    assert(!interruptPending);

    for (ThreadID i = 0; i < numThreads; ++i) {
        assert(!memReq[i]);
        // assert(!anotherMemReq[i]);
        assert(fetchStatus[i] == Idle || stalls[i].drain);
    }

    // branchPred->drainSanityCheck();
}

bool
Fetch::isDrained() const
{
    /* Make sure that threads are either idle of that the commit stage
     * has signaled that draining has completed by setting the drain
     * stall flag. This effectively forces the pipeline to be disabled
     * until the whole system is drained (simulation may continue to
     * drain other components).
     */
    for (ThreadID i = 0; i < numThreads; ++i) {
        // Verify fetch queues are drained
        if (!fetchQueue[i].empty())
            return false;

        // Return false if not idle or drain stalled
        if (fetchStatus[i] != Idle) {
            if (fetchStatus[i] == Blocked && stalls[i].drain)
                continue;
            else
                return false;
        }
    }

    /* The pipeline might start up again in the middle of the drain
     * cycle if the finish translation event is scheduled, so make
     * sure that's not the case.
     */
    return !finishTranslationEvent.scheduled();
}

void
Fetch::takeOverFrom()
{
    assert(cpu->getInstPort().isConnected());
    resetStage();

}

void
Fetch::drainStall(ThreadID tid)
{
    assert(cpu->isDraining());
    assert(!stalls[tid].drain);
    DPRINTF(Drain, "%i: Thread drained.\n", tid);
    stalls[tid].drain = true;
}

void
Fetch::wakeFromQuiesce()
{
    DPRINTF(RxuFetch, "Waking up from quiesce\n");
    // Hopefully this is safe
    // @todo: Allow other threads to wake from quiesce.
    fetchStatus[0] = Running;
}

void
Fetch::switchToActive()
{
    if (_status == Inactive) {
        DPRINTF(RxuActivity, "Activating stage.\n");

        cpu->activateStage(CPU::FetchIdx);

        _status = Active;
    }
}

void
Fetch::switchToInactive()
{
    if (_status == Active) {
        DPRINTF(RxuActivity, "Deactivating stage.\n");

        cpu->deactivateStage(CPU::FetchIdx);

        _status = Inactive;
    }
}

void
Fetch::deactivateThread(ThreadID tid)
{
    // Update priority list
    auto thread_it = std::find(priorityList.begin(), priorityList.end(), tid);
    if (thread_it != priorityList.end()) {
        priorityList.erase(thread_it);
    }
}

bool
Fetch::lookupAndUpdateNextPC(const DynInstPtr &inst, PCStateBase &next_pc)
{
    // Do branch prediction check here.
    // A bit of a misnomer...next_PC is actually the current PC until
    // this function updates it.
    // bool predict_taken;

    // if (!inst->isControl()) {
    inst->staticInst->advancePC(next_pc);
    inst->setPredTarg(next_pc);
    inst->setPredTaken(false);
    return false;
    // }

    // ThreadID tid = inst->threadNumber;
    // predict_taken = branchPred->predict(inst->staticInst, inst->seqNum,
    //                                     next_pc, tid);

    // if (predict_taken) {
    //     DPRINTF(RxuFetch, "[tid:%i] [sn:%llu] Branch at PC %#x "
    //             "predicted to be taken to %s\n",
    //             tid, inst->seqNum, inst->pcState().instAddr(), next_pc);
    // } else {
    //     DPRINTF(RxuFetch, "[tid:%i] [sn:%llu] Branch at PC %#x "
    //             "predicted to be not taken\n",
    //             tid, inst->seqNum, inst->pcState().instAddr());
    // }

    // DPRINTF(RxuFetch, "[tid:%i] [sn:%llu] Branch at PC %#x "
    //         "predicted to go to %s\n",
    //         tid, inst->seqNum, inst->pcState().instAddr(), next_pc);
    // inst->setPredTarg(next_pc);
    // inst->setPredTaken(predict_taken);

    // cpu->fetchStats[tid]->numBranches++;

    // if (predict_taken) {
    //     ++fetchStats.predictedBranches;
    // }

    // return predict_taken;
}

bool
Fetch::fetchCacheLine(Addr vaddr, ThreadID tid, Addr pc)
{
    Fault fault = NoFault;

    assert(!cpu->switchedOut());

    // @todo: not sure if these should block translation.
    //AlphaDep
    if (cacheBlocked) {
        DPRINTF(RxuFetch, "[tid:%i] Can't fetch cache line, cache blocked\n",
                tid);
        return false;
    } else if (checkInterrupt(pc) && !delayedCommit[tid]) {
        // Hold off fetch from getting new instructions when:
        // Cache is blocked, or
        // while an interrupt is pending and we're not in PAL mode, or
        // fetch is switched out.
        DPRINTF(RxuFetch, "[tid:%i] Can't fetch cache line, interrupt pending\n",
                tid);
        return false;
    }

    // Align the fetch address to the start of a fetch buffer segment.
    Addr fetchBufferBlockPC = fetchBufferAlignPC(vaddr);

    DPRINTF(RxuFetch, "[tid:%i] Fetching cache line %#x for addr %#x\n",
            tid, fetchBufferBlockPC, vaddr);

    // Setup the memReq to do a read of the first instruction's address.
    // Set the appropriate read size and flags as well.
    // Build request here.
    RequestPtr mem_req = std::make_shared<Request>(
        fetchBufferBlockPC, fetchBufferSize,
        Request::INST_FETCH, cpu->instRequestorId(), pc,
        cpu->thread[tid]->contextId());

    mem_req->taskId(cpu->taskId());

    memReq[tid] = mem_req;

    // Initiate translation of the icache block
    fetchStatus[tid] = ItlbWait;
    FetchTranslation *trans = new FetchTranslation(this);
    cpu->mmu->translateTiming(mem_req, cpu->thread[tid]->getTC(),
                              trans, BaseMMU::Execute);
    return true;
}

void
Fetch::finishTranslation(const Fault &fault, const RequestPtr &mem_req)
{
    ThreadID tid = cpu->contextToThread(mem_req->contextId());
    Addr fetchBufferBlockPC = mem_req->getVaddr();

    assert(!cpu->switchedOut());

    // Wake up CPU if it was idle
    cpu->wakeCPU();

    if (fetchStatus[tid] != ItlbWait || mem_req != memReq[tid] ||
        mem_req->getVaddr() != memReq[tid]->getVaddr()) {
        DPRINTF(RxuFetch, "[tid:%i] Ignoring itlb completed after squash\n",
                tid);
        ++fetchStats.tlbSquashes;
        return;
    }

    // If translation was successful, attempt to read the icache block.
    if (fault == NoFault) {
        // Check that we're not going off into random memory
        // If we have, just wait around for commit to squash something and put
        // us on the right track
        if (!cpu->system->isMemAddr(mem_req->getPaddr())) {
            warn("Address %#x is outside of physical memory, stopping fetch\n",
                    mem_req->getPaddr());
            fetchStatus[tid] = NoGoodAddr;
            memReq[tid] = NULL;
            return;
        }

        // Build packet here.
        PacketPtr data_pkt = new Packet(mem_req, MemCmd::ReadReq);
        data_pkt->dataDynamic(new uint8_t[fetchBufferSize]);

        fetchBufferPC[tid] = fetchBufferBlockPC;
        fetchBufferValid[tid] = false;
        DPRINTF(RxuFetch, "Fetch: Doing instruction read.\n");

        fetchStats.cacheLines++;

        // Access the cache.
        if (!icachePort.sendTimingReq(data_pkt)) {
            assert(retryPkt == NULL);
            assert(retryTid == InvalidThreadID);
            DPRINTF(RxuFetch, "[tid:%i] Out of MSHRs!\n", tid);

            fetchStatus[tid] = IcacheWaitRetry;
            retryPkt = data_pkt;
            retryTid = tid;
            cacheBlocked = true;
        } else {
            DPRINTF(RxuFetch, "[tid:%i] Doing Icache access.\n", tid);
            DPRINTF(RxuActivity, "[tid:%i] Activity: Waiting on I-cache "
                    "response.\n", tid);
            lastIcacheStall[tid] = curTick();
            fetchStatus[tid] = IcacheWaitResponse;
            // Notify Fetch Request probe when a packet containing a fetch
            // request is successfully sent
            ppFetchRequestSent->notify(mem_req);
        }
    } else {
        // Don't send an instruction to decode if we can't handle it.
        if (!(numInst < fetchWidth) ||
                !(fetchQueue[tid].size() < fetchQueueSize)) {
            assert(!finishTranslationEvent.scheduled());
            finishTranslationEvent.setFault(fault);
            finishTranslationEvent.setReq(mem_req);
            cpu->schedule(finishTranslationEvent,
                          cpu->clockEdge(Cycles(1)));
            return;
        }
        DPRINTF(RxuFetch,
                "[tid:%i] Got back req with addr %#x but expected %#x\n",
                tid, mem_req->getVaddr(), memReq[tid]->getVaddr());
        // Translation faulted, icache request won't be sent.
        memReq[tid] = NULL;

        // Send the fault to commit.  This thread will not do anything
        // until commit handles the fault.  The only other way it can
        // wake up is if a squash comes along and changes the PC.
        const PCStateBase &fetch_pc = *pc[tid];

        DPRINTF(RxuFetch, "[tid:%i] Translation faulted, building noop.\n", tid);
        // We will use a nop in ordier to carry the fault.
        DynInstPtr instruction = buildInst(tid, nopStaticInstPtr, nullptr,
                fetch_pc, fetch_pc, false);
        instruction->setNotAnInst();

        instruction->setPredTarg(fetch_pc);
        instruction->fault = fault;
        wroteToTimeBuffer = true;

        DPRINTF(RxuActivity, "Activity this cycle.\n");
        cpu->activityThisCycle();

        fetchStatus[tid] = TrapPending;

        DPRINTF(RxuFetch, "[tid:%i] Blocked, need to handle the trap.\n", tid);
        DPRINTF(RxuFetch, "[tid:%i] fault (%s) detected @ PC %s.\n",
                tid, fault->name(), *pc[tid]);
    }
    _status = updateFetchStatus();
}

void
Fetch::doSquash(const PCStateBase &new_pc, const DynInstPtr squashInst,
        ThreadID tid)
{
    DPRINTF(RxuFetch, "[tid:%i] Squashing, setting PC to: %s.\n",
            tid, new_pc);

    set(pc[tid], new_pc);
    fetchOffset[tid] = 0;
    if (squashInst && squashInst->pcState().instAddr() == new_pc.instAddr())
        macroop[tid] = squashInst->macroop;
    else
        macroop[tid] = NULL;
    decoder[tid]->reset();

    // Clear the icache miss if it's outstanding.
    if (fetchStatus[tid] == IcacheWaitResponse) {
        DPRINTF(RxuFetch, "[tid:%i] Squashing outstanding Icache miss.\n",
                tid);
        memReq[tid] = NULL;
        // anotherMemReq[tid] = NULL;
    } else if (fetchStatus[tid] == ItlbWait) {
        DPRINTF(RxuFetch, "[tid:%i] Squashing outstanding ITLB miss.\n",
                tid);
        memReq[tid] = NULL;
        // anotherMemReq[tid] = NULL;
    }

    // Get rid of the retrying packet if it was from this thread.
    if (retryTid == tid) {
        assert(cacheBlocked);
        if (retryPkt) {
            delete retryPkt;
        }
        retryPkt = NULL;
        retryTid = InvalidThreadID;
    }

    fetchStatus[tid] = Squashing;

    // Empty fetch queue
    fetchQueue[tid].clear();
    instsFromUC.clear();
    // restoreQueue.clear();
    ucHit.clear();

    fetchBufferValid[tid] = false;

    // microops are being squashed, it is not known wheather the
    // youngest non-squashed microop was  marked delayed commit
    // or not. Setting the flag to true ensures that the
    // interrupts are not handled when they cannot be, though
    // some opportunities to handle interrupts may be missed.
    delayedCommit[tid] = true;
}

void
Fetch::squashFromDecode(const PCStateBase &new_pc, const DynInstPtr squashInst,
        const InstSeqNum seq_num, ThreadID tid)
{
    DPRINTF(RxuFetch, "[tid:%i] Squashing from decode.\n", tid);

    doSquash(new_pc, squashInst, tid);
    ++fetchStats.squashCycles;

    // Tell the CPU to remove any instructions that are in flight between
    // fetch and decode.
    cpu->removeInstsUntil(seq_num, tid);
}

bool
Fetch::checkStall(ThreadID tid) const
{
    bool ret_val = false;

    if (stalls[tid].drain) {
        assert(cpu->isDraining());
        DPRINTF(RxuFetch,"[tid:%i] Drain stall detected.\n",tid);
        ret_val = true;
    }

    if (stalls[tid].bp) {
        DPRINTF(RxuFetch,"[tid:%i] IBANDLB stall detected.\n",tid);
        ret_val = true;
    }

    return ret_val;
}

Fetch::FetchStatus
Fetch::updateFetchStatus()
{
    //Check Running
    std::list<ThreadID>::iterator threads = activeThreads->begin();
    std::list<ThreadID>::iterator end = activeThreads->end();

    while (threads != end) {
        ThreadID tid = *threads++;

        if (fetchStatus[tid] == Running ||
            fetchStatus[tid] == Squashing ||
            fetchStatus[tid] == IcacheAccessComplete) {

            if (_status == Inactive) {
                DPRINTF(RxuActivity, "[tid:%i] Activating stage.\n",tid);

                if (fetchStatus[tid] == IcacheAccessComplete) {
                    DPRINTF(RxuActivity, "[tid:%i] Activating fetch due to cache"
                            "completion\n",tid);
                }

                cpu->activateStage(CPU::FetchIdx);
            }

            return Active;
        }
    }

    // Stage is switching from active to inactive, notify CPU of it.
    if (_status == Active) {
        DPRINTF(RxuActivity, "Deactivating stage.\n");

        cpu->deactivateStage(CPU::FetchIdx);
    }

    return Inactive;
}

void
Fetch::squash(const PCStateBase &new_pc, const InstSeqNum seq_num,
        DynInstPtr squashInst, ThreadID tid)
{
    DPRINTF(RxuFetch, "[tid:%i] Squash from commit.\n", tid);

    doSquash(new_pc, squashInst, tid);
    ++fetchStats.squashCycles;

    // Tell the CPU to remove any instructions that are not in the ROB.
    // cpu->removeInstsNotInROB(tid);
}

void
Fetch::tick()
{
    std::list<ThreadID>::iterator threads = activeThreads->begin();
    std::list<ThreadID>::iterator end = activeThreads->end();
    bool status_change = false;

    wroteToTimeBuffer = false;

    for (ThreadID i = 0; i < numThreads; ++i) {
        issuePipelinedIfetch[i] = false;
    }

    while (threads != end) {
        ThreadID tid = *threads++;

        // Check the signals for each thread to determine the proper status
        // for each thread.
        bool updated_status = checkSignalsAndUpdate(tid);
        status_change =  status_change || updated_status;
    }

    DPRINTF(RxuFetch, "Running stage.\n");

    if (FullSystem) {
        if (fromCommit->commitInfo[0].interruptPending) {
            interruptPending = true;
        }

        if (fromCommit->commitInfo[0].clearInterrupt) {
            interruptPending = false;
        }
    }

    for (threadFetched = 0; threadFetched < numFetchingThreads;
         threadFetched++) {
        fetch(status_change);
    }

    // Send instructions enqueued into the fetch queue to BP0.
    // Limit rate by fetchWidth.  Stall if decode is stalled.
    unsigned insts_to_bp0 = 0;
    unsigned available_insts = 0;

    for (auto tid : *activeThreads) {
        if (!stalls[tid].bp) {
            available_insts += fetchQueue[tid].size();
        }
    }

    // Pick a random thread to start trying to grab instructions from
    auto tid_itr = activeThreads->begin();
    std::advance(tid_itr,
            random_mt.random<uint8_t>(0, activeThreads->size() - 1));
    
    bool fetchSendSth = false;
    bool ucSendSth = false;
    DPRINTF(RxuFetch, "uc hit info: %#x, win size:%i.\n", ucHit.hit, ucHit.win_size);
    if (ucHit.hit & 1) {
        if (!instsFromUC.empty()) {
            Addr ucHeadPC = instsFromUC.front()->pcState().instAddr();
            Addr prefetch_pc = pc[_tid]->instAddr();
            DPRINTF(RxuFetch, "PC %#x hitting in uop cache.\n", ucHeadPC);

            if (ucHeadPC <= prefetch_pc) {
                int insts_to_bp = 0;
                toBP0->size = 0;
                while (!stalls[_tid].bp && 
                    !ucHit.empty() && 
                    !instsFromUC.empty() && 
                    insts_to_bp < bpuWidth) {
                    // fetchQueue[tid].push_back(instsFromUC.front());
                    DynInstPtr inst = instsFromUC.front();
                    toBP0->insts[toBP0->size++] = inst;
                    DPRINTF(RxuFetch, "[%#x] Sending instruction to BP0 from uop cache. "
                                                "Size: %i.\n", inst->pcState().instAddr(), instsFromUC.size());
                    instsFromUC.pop_front();
                    insts_to_bp++;
                    ucHit.decrease();
                    ucSendSth = true;
                    if (inst->staticInst->isRxuJump()) {
                        while (!ucHit.empty()) {
                            instsFromUC.pop_front();
                            ucHit.decrease();
                        }
                        break;
                    }
                }

                if (insts_to_bp)
                    fetchStats.fetchOutInsts.sample(insts_to_bp);

            } else {
                DPRINTF(RxuFetch, "Uop cache wait for fetch.\n");
            }
        } else {
            panic("Uopcache hit, but no insts.");
        }
    } else {
        DPRINTF(RxuFetch, "PC %#x miss in uop cache.\n",
                        pc[_tid]->instAddr());

        while (available_insts != 0 && insts_to_bp0 < bpuWidth) {
            ThreadID tid = *tid_itr;
            if (!stalls[tid].bp && !fetchQueue[tid].empty()) {
                DynInstPtr inst = fetchQueue[tid].front();
                toBP0->insts[toBP0->size++] = inst;
                DPRINTF(RxuFetch, "[tid:%i] [sn:%#x] Sending instruction to BP0 "
                        "from fetch queue. Fetch queue size: %i.\n",
                        tid, inst->seqNum, fetchQueue[tid].size());

                fetchSendSth = true;
                fetchQueue[tid].pop_front();
                insts_to_bp0++;
                available_insts--;
            }

            tid_itr++;
            // Wrap around if at end of active threads list
            if (tid_itr == activeThreads->end())
                tid_itr = activeThreads->begin();
        }
    }

    if (insts_to_bp0)
        fetchStats.fetchOutInsts.sample(insts_to_bp0);

    if (ucSendSth && ucHit.empty()) {
        ucHit.advance();
    }
    if (fetchSendSth) {
        ucHit.advanceDuetoFetch();
    }
    DPRINTF(RxuFetch, "uc hit info: %#x, win size: %i.\n", ucHit.hit, ucHit.win_size);

    // Record number of instructions fetched this cycle for distribution.
    fetchStats.nisnDist.sample(numInst);

    if (status_change) {
        // Change the fetch stage status if there was a status change.
        _status = updateFetchStatus();
    }

    // Issue the next I-cache request if possible.

    // for (ThreadID i = 0; i < numThreads; ++i) {
    //     if (issuePipelinedIfetch[i]) {
    //         pipelineIcacheAccesses(i);
    //     }
    // }

    // If there was activity this cycle, inform the CPU of it.
    if (wroteToTimeBuffer) {
        DPRINTF(RxuActivity, "Activity this cycle.\n");
        cpu->activityThisCycle();
    }

    // Reset the number of the instruction we've fetched.
    numInst = 0;

    // DPRINTF(RxuFetch, "cache line size: %i.\n", ucacheLineData.size());
    // if (ucBlockStoreAddr > 0 && !ucacheLineData.empty()) {
    //     DPRINTF(RxuFetch, "PC starts with %#x can be saved to uop cache.\n",
    //             ucBlockStoreAddr);
    //     UopCacheStoreCompletion *ucStoreCompletion = new UopCacheStoreCompletion(this);
    //     ucStoreCompletion->setPC(ucBlockStoreAddr);
    //     ucStoreCompletion->setQueue(ucacheLineData);
    //     cpu->schedule(ucStoreCompletion, cpu->clockEdge(Cycles(1)));
    //     ucBlockStoreAddr = 0;
    //     ucacheLineData.clear();
    // }
}

bool
Fetch::checkSignalsAndUpdate(ThreadID tid)
{
    // Update the per thread stall statuses.
    if (fromBP0->IBandLBBlock[tid]) {
        stalls[tid].bp = true;
    }

    if (fromBP0->IBandLBUnblock[tid]) {
        // assert(stalls[tid].bp);
        // assert(!fromBP0->BP0Block[tid]);
        stalls[tid].bp = false; 
    }

    // Check squash signals from commit.
    if (fromCommit->commitInfo[tid].squash) {

        DPRINTF(RxuFetch, "[tid:%i] Squashing instructions due to squash "
                "from commit.\n",tid);
        // In any case, squash.
        squash(*fromCommit->commitInfo[tid].pc,
               fromCommit->commitInfo[tid].doneSeqNum,
               fromCommit->commitInfo[tid].squashInst, tid);

        // cpu->removeInstsSquash(tid);
        // cpu->removeInstsUntil(fromCommit->commitInfo[tid].doneSeqNum, tid);

        // If it was a branch mispredict on a control instruction, update the
        // branch predictor with that instruction, otherwise just kill the
        // invalid state we generated in after sequence number
//        if (fromCommit->commitInfo[tid].mispredictInst &&
//            fromCommit->commitInfo[tid].mispredictInst->isControl()) {
//            branchPred->squash(fromCommit->commitInfo[tid].doneSeqNum,
//                    *fromCommit->commitInfo[tid].pc,
//                    fromCommit->commitInfo[tid].branchTaken, tid);
//        } else {
//            branchPred->squash(fromCommit->commitInfo[tid].doneSeqNum,
//                              tid);
//        }

        return true;
    } else if (fromCommit->commitInfo[tid].doneSeqNum) {
        // Update the branch predictor if it wasn't a squashed instruction
        // that was broadcasted.
//        branchPred->update(fromCommit->commitInfo[tid].doneSeqNum, tid);
    }


    // Check squash signals from decode.
    if (fromDecode->decodeInfo[tid].squash) {
        DPRINTF(RxuFetch, "[tid:%i] Squashing instructions due to squash "
                "from decode.\n",tid);

        // Update the branch predictor.
        if (fromDecode->decodeInfo[tid].branchMispredict) {
//            branchPred->squash(fromDecode->decodeInfo[tid].doneSeqNum,
//                    *fromDecode->decodeInfo[tid].nextPC,
//                    fromDecode->decodeInfo[tid].branchTaken, tid);
        } else {
//            branchPred->squash(fromDecode->decodeInfo[tid].doneSeqNum, tid);
        }

        if (fetchStatus[tid] != Squashing) {

            DPRINTF(RxuFetch, "Squashing from decode with PC = %s\n",
                *fromDecode->decodeInfo[tid].nextPC);
            // Squash unless we're already squashing
            squashFromDecode(*fromDecode->decodeInfo[tid].nextPC,
                             fromDecode->decodeInfo[tid].squashInst,
                             fromDecode->decodeInfo[tid].doneSeqNum,
                             tid);

            return true;
        }
    }

    if (fromBP1->bpu1Info[tid].squash) {
        DPRINTF(RxuFetch, "[tid:%i] Squashing instructions due to squash "
                "from BPU1.\n",tid);
        doSquash(*fromBP1->bpu1Info[tid].pc, 
                  fromBP1->bpu1Info[tid].branchInst, 
                  tid);
        ++fetchStats.squashCycles;
        // Tell the CPU to remove any instructions that are not in the ROB.
        // cpu->removeInstsNotInROB(tid);

        fetchStatus[tid] = Running;

        return true;
    }

    if (checkStall(tid) &&
        fetchStatus[tid] != IcacheWaitResponse &&
        fetchStatus[tid] != IcacheWaitRetry &&
        fetchStatus[tid] != ItlbWait &&
        fetchStatus[tid] != QuiescePending) {
        DPRINTF(RxuFetch, "[tid:%i] Setting to blocked.\n",tid);

        fetchStatus[tid] = Blocked;

        return true;
    }

    if (fetchStatus[tid] == Blocked ||
        fetchStatus[tid] == Squashing) {
        // Switch status to running if fetch isn't being told to block or
        // squash this cycle.
        DPRINTF(RxuFetch, "[tid:%i] Done squashing and blocking, switching to running.\n",
                tid);

        fetchStatus[tid] = Running;

        return true;
    }

    // If we've reached this point, we have not gotten any signals that
    // cause fetch to change its status.  Fetch remains the same as before.
    return false;
}

DynInstPtr
Fetch::buildInst(ThreadID tid, StaticInstPtr staticInst,
        StaticInstPtr curMacroop, const PCStateBase &this_pc,
        const PCStateBase &next_pc, bool trace)
{
    // Get a sequence number.
    // InstSeqNum seq = cpu->getAndIncrementInstSeq();
    InstSeqNum seq = this_pc.instAddr();

    DynInst::Arrays arrays;
    arrays.numSrcs = staticInst->numSrcRegs();
    arrays.numDests = staticInst->numDestRegs();

    // Create a new DynInst from the instruction fetched.
    DynInstPtr instruction = new (arrays) DynInst(
            arrays, staticInst, curMacroop, this_pc, next_pc, seq, cpu);
    instruction->setTid(tid);

    instruction->setThreadState(cpu->thread[tid]);

    DPRINTF(RxuFetch, "[tid:%i] Instruction PC %s created.\n",
            tid, this_pc);

    DPRINTF(RxuFetch, "[tid:%i] Instruction is: %s\n", tid,
            instruction->staticInst->disassemble(this_pc.instAddr()));

    DPRINTF(RxuFetch, "[tid:%i] Instruction' opclass is: %s\n", tid,
        enums::OpClassStrings[instruction->opClass()]);

#if TRACING_ON
    if (trace) {
        instruction->traceData =
            cpu->getTracer()->getInstRecord(curTick(), cpu->tcBase(tid),
                    instruction->staticInst, this_pc, curMacroop);
    }
#else
    instruction->traceData = NULL;
#endif

    // Add instruction to the CPU's list of instructions.
    // instruction->setInstListIt(cpu->addInst(instruction));

    // Write the instruction to the first slot in the queue
    // that heads to decode.
    // assert(numInst < fetchWidth);
    // fetchQueue[tid].push_back(instruction);
    // assert(fetchQueue[tid].size() <= fetchQueueSize);
    // DPRINTF(RxuFetch, "[tid:%i] Fetch queue entry created (%i/%i).\n",
    //         tid, fetchQueue[tid].size(), fetchQueueSize);
    //toDecode->insts[toDecode->size++] = instruction;

    // Keep track of if we can take an interrupt at this boundary
    delayedCommit[tid] = instruction->isDelayedCommit();

    return instruction;
}

void
Fetch::fetch(bool &status_change)
{
    //////////////////////////////////////////
    // Start actual fetch
    //////////////////////////////////////////

    ThreadID tid = getFetchingThread();

    assert(!cpu->switchedOut());

    if (tid == InvalidThreadID) {
        // Breaks looping condition in tick()
        threadFetched = numFetchingThreads;

        if (numThreads == 1) {  // @todo Per-thread stats
            profileStall(0);
        }

        return;
    }

    DPRINTF(RxuFetch, "Attempting to fetch from [tid:%i]\n", tid);

    // The current PC.
    PCStateBase &this_pc = *pc[tid];
    // std::unique_ptr<PCStateBase> _pc(pc[tid]->clone());
    // PCStateBase &this_pc = *_pc;
    set(lpc[tid], pc[tid]);

    Addr pcOffset = fetchOffset[tid];
    Addr fetchAddr = (this_pc.instAddr() + pcOffset) & decoder[tid]->pcMask();

    bool inRom = isRomMicroPC(this_pc.microPC());

    // If returning from the delay of a cache miss, then update the status
    // to running, otherwise do the cache access.  Possibly move this up
    // to tick() function.
    if (fetchStatus[tid] == IcacheAccessComplete) {
        DPRINTF(RxuFetch, "[tid:%i] Icache miss is complete.\n", tid);

        fetchStatus[tid] = Running;
        status_change = true;
    } else if (fetchStatus[tid] == Running) {
        // Align the fetch PC so its at the start of a fetch buffer segment.
        Addr fetchBufferBlockPC = fetchBufferAlignPC(fetchAddr);

        // If buffer is no longer valid or fetchAddr has moved to point
        // to the next cache block, AND we have no remaining ucode
        // from a macro-op, then start fetch from icache.
        if (!(fetchBufferValid[tid] &&
                    fetchBufferBlockPC == fetchBufferPC[tid]) && !inRom &&
                !macroop[tid]) {
            DPRINTF(RxuFetch, "[tid:%i] Attempting to translate and read "
                    "instruction, starting at PC %s.\n", tid, this_pc);

            fetchCacheLine(fetchAddr, tid, this_pc.instAddr());

            if (fetchStatus[tid] == IcacheWaitResponse) {
                cpu->fetchStats[tid]->icacheStallCycles++;
            }
            else if (fetchStatus[tid] == ItlbWait)
                ++fetchStats.tlbCycles;
            else
                ++fetchStats.miscStallCycles;
            return;
        } else if (checkInterrupt(this_pc.instAddr()) &&
                !delayedCommit[tid]) {
            // Stall CPU if an interrupt is posted and we're not issuing
            // an delayed commit micro-op currently (delayed commit
            // instructions are not interruptable by interrupts, only faults)
            ++fetchStats.miscStallCycles;
            DPRINTF(RxuFetch, "[tid:%i] Fetch is stalled!\n", tid);
            return;
        }
    } else {
        if (fetchStatus[tid] == Idle) {
            ++fetchStats.idleCycles;
            DPRINTF(RxuFetch, "[tid:%i] Fetch is idle!\n", tid);
        }

        // Status is Idle, so fetch should do nothing.
        return;
    }

    ++fetchStats.cycles;

    std::unique_ptr<PCStateBase> next_pc(this_pc.clone());

    StaticInstPtr staticInst = NULL;
    StaticInstPtr curMacroop = macroop[tid];

    // If the read of the first instruction was successful, then grab the
    // instructions from the rest of the cache line and put them into the
    // queue heading to BP0.

    DPRINTF(RxuFetch, "[tid:%i] Adding instructions to queue to "
            "BPU.\n", tid);

    // Need to keep track of whether or not a predicted branch
    // ended this fetch block.
    bool predictedBranch = false;

    // Need to halt fetch if quiesce instruction detected
    bool quiesce = false;

    const unsigned numInsts = fetchBufferSize / instSize;
    unsigned blkOffset = (fetchAddr - fetchBufferPC[tid]) / instSize;
    // unsigned _blkOffset = (fetchAddr - fetchBufferPC[tid]) / instSize;
    // unsigned blkOffset = 0;

    std::unique_ptr<PCStateBase> base_pc(this_pc.clone());
    // this_pc.advance(-(this_pc.instAddr() - alignHalfPC(this_pc.instAddr())));

    auto *dec_ptr = decoder[tid];
    const Addr pc_mask = dec_ptr->pcMask();

    toBP0->static_size = 0;
    toBP0->pc = this_pc.instAddr() & ((~63));

    bool rightHead = false;

    // Loop through instruction memory from the cache.
    // Keep issuing while fetchWidth is available and branch is not
    // predicted taken
    while (numInst < fetchWidth && fetchQueue[tid].size() < fetchQueueSize
        && !quiesce) {
        // We need to process more memory if we aren't going to get a
        // StaticInst from the rom, the current macroop, or what's already
        // in the decoder.
        bool needMem = !inRom && !curMacroop && !dec_ptr->instReady();
        fetchAddr = (this_pc.instAddr() + pcOffset) & pc_mask;
        Addr fetchBufferBlockPC = fetchBufferAlignPC(fetchAddr);

        if (needMem) {
            // If buffer is no longer valid or fetchAddr has moved to point
            // to the next cache block then start fetch from icache.
            if (!fetchBufferValid[tid] ||
                fetchBufferBlockPC != fetchBufferPC[tid])
                break;

            if (blkOffset >= numInsts) {
                // We need to process more memory, but we've run out of the
                // current block.
                break;
            }

            memcpy(dec_ptr->moreBytesPtr(),
                    fetchBuffer[tid] + blkOffset * instSize, instSize);
            decoder[tid]->moreBytes(this_pc, fetchAddr);

            if (dec_ptr->needMoreBytes()) {
                blkOffset++;
                fetchAddr += instSize;
                pcOffset += instSize;
            }
        }

        // Extract as many instructions and/or microops as we can from
        // the memory we've processed so far.
        do {
            if (!(curMacroop || inRom)) {
                if (dec_ptr->instReady()) {
                    staticInst = dec_ptr->decode(this_pc);

                    // Increment stat of fetched instructions.
                    cpu->fetchStats[tid]->numInsts++;

                    if (staticInst->isMacroop()) {
                        curMacroop = staticInst;
                    } else {
                        pcOffset = 0;
                    }
                } else {
                    // We need more bytes for this instruction so blkOffset and
                    // pcOffset will be updated
                    break;
                }
            }
            // Whether we're moving to a new macroop because we're at the
            // end of the current one, or the branch predictor incorrectly
            // thinks we are...
            bool newMacro = false;
            if (curMacroop || inRom) {
                if (inRom) {
                    staticInst = dec_ptr->fetchRomMicroop(
                            this_pc.microPC(), curMacroop);
                } else {
                    staticInst = curMacroop->fetchMicroop(this_pc.microPC());
                }
                newMacro |= staticInst->isLastMicroop();
            }

            DynInstPtr instruction = buildInst(
                    tid, staticInst, curMacroop, this_pc, *next_pc, true);

            // assert(numInst < fetchWidth);
            // if (this_pc.instAddr() == base_pc->instAddr() || rightHead) {
            //     fetchQueue[tid].push_back(instruction);
            //     assert(fetchQueue[tid].size() <= fetchQueueSize);
            //     DPRINTF(RxuFetch, "[tid:%i] Fetch queue entry created (%i/%i).\n",
            //     tid, fetchQueue[tid].size(), fetchQueueSize);

            //     ppFetch->notify(instruction);

            //     rightHead = true;

            // } else if (this_pc.instAddr() > base_pc->instAddr() && !rightHead) {
            //     this_pc.advance(base_pc->instAddr() - this_pc.instAddr());
            //     DPRINTF(RxuFetch, "redirect pc to right head %#x.\n", *base_pc);
            //     decoder[tid]->reset();
            //     fetchAddr = (this_pc.instAddr()) & decoder[tid]->pcMask();
            //     blkOffset = (fetchAddr - fetchBufferPC[tid]) / instSize;

            //     toBP0->static_size = 0;
            //     toBP0->pc = this_pc.instAddr();

            //     break;
            // } else {
            //     DPRINTF(RxuFetch, "inst %#x not needed.\n", this_pc);
            // }

            fetchQueue[tid].push_back(instruction);
            assert(fetchQueue[tid].size() <= fetchQueueSize);
            DPRINTF(RxuFetch, "[tid:%i] Fetch queue entry created (%i/%i).\n",
            tid, fetchQueue[tid].size(), fetchQueueSize);

            ppFetch->notify(instruction);

            toBP0->pc_insts[toBP0->static_size] = this_pc.instAddr();
            toBP0->static_insts[toBP0->static_size] = instruction->staticInst;
            toBP0->static_size++;
            
            numInst++;

#if TRACING_ON
            if (debug::RxuO3PipeView) {
                instruction->fetchTick = curTick();
            }
#endif

            set(next_pc, this_pc);

            // If we're branching after this instruction, quit fetching
            // from the same block.
            predictedBranch |= this_pc.branching();
            predictedBranch |= lookupAndUpdateNextPC(instruction, *next_pc);
            if (predictedBranch) {
                DPRINTF(RxuFetch, "Branch detected with PC = %s\n", this_pc);
            }

            newMacro |= this_pc.instAddr() != next_pc->instAddr();

            // Move to the next instruction, unless we have a branch.
            set(this_pc, *next_pc);
            inRom = isRomMicroPC(this_pc.microPC());

            if (newMacro) {
                fetchAddr = this_pc.instAddr() & pc_mask;
                blkOffset = (fetchAddr - fetchBufferPC[tid]) / instSize;
                pcOffset = 0;
                curMacroop = NULL;
            }

            if (instruction->isQuiesce()) {
                DPRINTF(RxuFetch,
                        "Quiesce instruction encountered, halting fetch!\n");
                fetchStatus[tid] = QuiescePending;
                status_change = true;
                quiesce = true;
                break;
            }
        } while ((curMacroop || dec_ptr->instReady()) &&
                 numInst < fetchWidth &&
                 fetchQueue[tid].size() < fetchQueueSize);

        // Re-evaluate whether the next instruction to fetch is in micro-op ROM
        // or not.
        inRom = isRomMicroPC(this_pc.microPC());
    }

    if (predictedBranch) {
        DPRINTF(RxuFetch, "[tid:%i] Done fetching, predicted branch "
                "instruction encountered.\n", tid);
    } else if (numInst >= fetchWidth) {
        DPRINTF(RxuFetch, "[tid:%i] Done fetching, reached fetch bandwidth "
                "for this cycle.\n", tid);
    } else if (blkOffset >= fetchBufferSize) {
        DPRINTF(RxuFetch, "[tid:%i] Done fetching, reached the end of the"
                "fetch buffer.\n", tid);
    }

    macroop[tid] = curMacroop;
    fetchOffset[tid] = pcOffset;

    if (numInst > 0) {
        wroteToTimeBuffer = true;
    }

    // pipeline a fetch if we're crossing a fetch buffer boundary and not in
    // a state that would preclude fetching
    fetchAddr = (this_pc.instAddr() + pcOffset) & pc_mask;
    Addr fetchBufferBlockPC = fetchBufferAlignPC(fetchAddr);
    issuePipelinedIfetch[tid] = fetchBufferBlockPC != fetchBufferPC[tid] &&
        fetchStatus[tid] != IcacheWaitResponse &&
        fetchStatus[tid] != ItlbWait &&
        fetchStatus[tid] != IcacheWaitRetry &&
        fetchStatus[tid] != QuiescePending &&
        !curMacroop;
}

void
Fetch::recvReqRetry()
{
    if (retryPkt != NULL) {
        assert(cacheBlocked);
        assert(retryTid != InvalidThreadID);
        assert(fetchStatus[retryTid] == IcacheWaitRetry);

        if (icachePort.sendTimingReq(retryPkt)) {
            fetchStatus[retryTid] = IcacheWaitResponse;
            // Notify Fetch Request probe when a retryPkt is successfully sent.
            // Note that notify must be called before retryPkt is set to NULL.
            ppFetchRequestSent->notify(retryPkt->req);
            retryPkt = NULL;
            retryTid = InvalidThreadID;
            cacheBlocked = false;
        }
    } else {
        assert(retryTid == InvalidThreadID);
        // Access has been squashed since it was sent out.  Just clear
        // the cache being blocked.
        cacheBlocked = false;
    }
}

///////////////////////////////////////
//                                   //
//  SMT FETCH POLICY MAINTAINED HERE //
//                                   //
///////////////////////////////////////
ThreadID
Fetch::getFetchingThread()
{
    if (numThreads > 1) {
        switch (fetchPolicy) {
          case RxuSMTFetchPolicy::RoundRobin:
            return roundRobin();
        //   case RxuSMTFetchPolicy::IQCount:
        //     return iqCount();
        //   case RxuSMTFetchPolicy::LSQCount:
        //     return lsqCount();
          case RxuSMTFetchPolicy::Branch:
            return branchCount();
          default:
            return InvalidThreadID;
        }
    } else {
        std::list<ThreadID>::iterator thread = activeThreads->begin();
        if (thread == activeThreads->end()) {
            return InvalidThreadID;
        }

        ThreadID tid = *thread;

        if (fetchStatus[tid] == Running ||
            fetchStatus[tid] == IcacheAccessComplete ||
            fetchStatus[tid] == Idle) {
            return tid;
        } else {
            return InvalidThreadID;
        }
    }
}


ThreadID
Fetch::roundRobin()
{
    std::list<ThreadID>::iterator pri_iter = priorityList.begin();
    std::list<ThreadID>::iterator end      = priorityList.end();

    ThreadID high_pri;

    while (pri_iter != end) {
        high_pri = *pri_iter;

        assert(high_pri <= numThreads);

        if (fetchStatus[high_pri] == Running ||
            fetchStatus[high_pri] == IcacheAccessComplete ||
            fetchStatus[high_pri] == Idle) {

            priorityList.erase(pri_iter);
            priorityList.push_back(high_pri);

            return high_pri;
        }

        pri_iter++;
    }

    return InvalidThreadID;
}

// ThreadID
// Fetch::iqCount()
// {
//     //sorted from lowest->highest
//     std::priority_queue<unsigned, std::vector<unsigned>,
//                         std::greater<unsigned> > PQ;
//     std::map<unsigned, ThreadID> threadMap;

//     std::list<ThreadID>::iterator threads = activeThreads->begin();
//     std::list<ThreadID>::iterator end = activeThreads->end();

//     while (threads != end) {
//         ThreadID tid = *threads++;
//         unsigned iqCount = fromIEW->iewInfo[tid].iqCount;

//         //we can potentially get tid collisions if two threads
//         //have the same iqCount, but this should be rare.
//         PQ.push(iqCount);
//         threadMap[iqCount] = tid;
//     }

//     while (!PQ.empty()) {
//         ThreadID high_pri = threadMap[PQ.top()];

//         if (fetchStatus[high_pri] == Running ||
//             fetchStatus[high_pri] == IcacheAccessComplete ||
//             fetchStatus[high_pri] == Idle)
//             return high_pri;
//         else
//             PQ.pop();

//     }

//     return InvalidThreadID;
// }

// ThreadID
// Fetch::lsqCount()
// {
//     //sorted from lowest->highest
//     std::priority_queue<unsigned, std::vector<unsigned>,
//                         std::greater<unsigned> > PQ;
//     std::map<unsigned, ThreadID> threadMap;

//     std::list<ThreadID>::iterator threads = activeThreads->begin();
//     std::list<ThreadID>::iterator end = activeThreads->end();

//     while (threads != end) {
//         ThreadID tid = *threads++;
//         unsigned ldstqCount = fromIEW->iewInfo[tid].ldstqCount;

//         //we can potentially get tid collisions if two threads
//         //have the same iqCount, but this should be rare.
//         PQ.push(ldstqCount);
//         threadMap[ldstqCount] = tid;
//     }

//     while (!PQ.empty()) {
//         ThreadID high_pri = threadMap[PQ.top()];

//         if (fetchStatus[high_pri] == Running ||
//             fetchStatus[high_pri] == IcacheAccessComplete ||
//             fetchStatus[high_pri] == Idle)
//             return high_pri;
//         else
//             PQ.pop();
//     }

//     return InvalidThreadID;
// }

ThreadID
Fetch::branchCount()
{
    panic("Branch Count Fetch policy unimplemented\n");
    return InvalidThreadID;
}

void
Fetch::pipelineIcacheAccesses(ThreadID tid)
{
    if (!issuePipelinedIfetch[tid]) {
        return;
    }

    // The next PC to access.
    const PCStateBase &this_pc = *pc[tid];

    if (isRomMicroPC(this_pc.microPC())) {
        return;
    }

    Addr pcOffset = fetchOffset[tid];
    Addr fetchAddr = (this_pc.instAddr() + pcOffset) & decoder[tid]->pcMask();

    // Align the fetch PC so its at the start of a fetch buffer segment.
    Addr fetchBufferBlockPC = fetchBufferAlignPC(fetchAddr);

    // Unless buffer already got the block, fetch it from icache.
    if (!(fetchBufferValid[tid] && fetchBufferBlockPC == fetchBufferPC[tid])) {
        DPRINTF(RxuFetch, "[tid:%i] Issuing a pipelined I-cache access, "
                "starting at PC %s.\n", tid, this_pc);

        fetchCacheLine(fetchAddr, tid, this_pc.instAddr());
    }
}

void
Fetch::profileStall(ThreadID tid)
{
    DPRINTF(RxuFetch,"There are no more threads available to fetch from.\n");

    // @todo Per-thread stats

    if (stalls[tid].drain) {
        ++fetchStats.pendingDrainCycles;
        DPRINTF(RxuFetch, "Fetch is waiting for a drain!\n");
    } else if (activeThreads->empty()) {
        ++fetchStats.noActiveThreadStallCycles;
        DPRINTF(RxuFetch, "Fetch has no active thread!\n");
    } else if (fetchStatus[tid] == Blocked) {
        ++fetchStats.blockedCycles;
        DPRINTF(RxuFetch, "[tid:%i] Fetch is blocked!\n", tid);
    } else if (fetchStatus[tid] == Squashing) {
        ++fetchStats.squashCycles;
        DPRINTF(RxuFetch, "[tid:%i] Fetch is squashing!\n", tid);
    } else if (fetchStatus[tid] == IcacheWaitResponse) {
        cpu->fetchStats[tid]->icacheStallCycles++;
        DPRINTF(RxuFetch, "[tid:%i] Fetch is waiting cache response!\n",
                tid);
    } else if (fetchStatus[tid] == ItlbWait) {
        ++fetchStats.tlbCycles;
        DPRINTF(RxuFetch, "[tid:%i] Fetch is waiting ITLB walk to "
                "finish!\n", tid);
    } else if (fetchStatus[tid] == TrapPending) {
        ++fetchStats.pendingTrapStallCycles;
        DPRINTF(RxuFetch, "[tid:%i] Fetch is waiting for a pending trap!\n",
                tid);
    } else if (fetchStatus[tid] == QuiescePending) {
        ++fetchStats.pendingQuiesceStallCycles;
        DPRINTF(RxuFetch, "[tid:%i] Fetch is waiting for a pending quiesce "
                "instruction!\n", tid);
    } else if (fetchStatus[tid] == IcacheWaitRetry) {
        ++fetchStats.icacheWaitRetryStallCycles;
        DPRINTF(RxuFetch, "[tid:%i] Fetch is waiting for an I-cache retry!\n",
                tid);
    } else if (fetchStatus[tid] == NoGoodAddr) {
            DPRINTF(RxuFetch, "[tid:%i] Fetch predicted non-executable address\n",
                    tid);
    } else {
        DPRINTF(RxuFetch, "[tid:%i] Unexpected fetch stall reason "
            "(Status: %i)\n",
            tid, fetchStatus[tid]);
    }
}

bool
Fetch::IcachePort::recvTimingResp(PacketPtr pkt)
{
    DPRINTF(RxuO3CPU, "Fetch unit received timing\n");
    // We shouldn't ever get a cacheable block in Modified state
    assert(pkt->req->isUncacheable() ||
           !(pkt->cacheResponding() && !pkt->hasSharers()));
    fetch->processCacheCompletion(pkt);

    return true;
}

void
Fetch::IcachePort::recvReqRetry()
{
    fetch->recvReqRetry();
}

void 
Fetch::lookupUopCache(const PCStateBase &_pc, bool taken)
{
    // UopCacheCompletion *ucCompletion = new UopCacheCompletion(this);
    int cachelines = 4;

    if (taken) {
        removeAlltoRestore();

        DPRINTF(RxuFetch, "Redirecting into PC: %s.\n", _pc);
        set(pc[_tid], _pc);

        UopCache::HitInfo hit = uc->lookupFour(pc[_tid]->instAddr());
        DPRINTF(RxuFetch, "Hit info: %#x.\n", hit.hit);
        while (hit.hitHead()) {
            pc[_tid]->advance(hit.pcShift(pc[_tid]->instAddr()));
            hit.advance();
        }

        ucBlockLookupAddr = _pc.instAddr();
        std::unique_ptr<PCStateBase> uc_pc(_pc.clone());
        // DPRINTF(RxuFetch, "uc hit info: %#x, win size:%i.\n", ucHit.hit, ucHit.win_size);
        ucHit = uc->access(*uc_pc, cachelines);
        // ucHit.more(uc->access(*uc_pc, cachelines));
        // ucHit.more(hitinfo);
        DPRINTF(RxuFetch, "uc hit info: %#x, win size:%i.\n", ucHit.hit, ucHit.win_size);

    }
    else {
        set(pc[_tid], _pc);
        
        std::unique_ptr<PCStateBase> uc_pc(pc[_tid]->clone());
        UopCache::HitInfo hit = uc->lookupFour(pc[_tid]->instAddr());
        DPRINTF(RxuFetch, "Hit info: %#x.\n", hit.hit);
        while (hit.hit & 1) {
            pc[_tid]->advance(hit.pcShift(pc[_tid]->instAddr()));
            hit.advance();
        }

        bool hasSend = false;
        if (toBP0->size > 0) {
            for (int i = 0; i < toBP0->size; ++i) {
                auto inst = toBP0->insts[i];
                if (inst->pcState().instAddr() == uc_pc->instAddr()) {
                    DPRINTF(RxuFetch, "PC %s has been sent to BP0. Passing.\n", *uc_pc);
                    // uc_pc->advance();
                    if (inst->staticInst->isCompressed()) {
                        uc_pc->set(inst->pcState().instAddr() + 2);
                    } else {
                        uc_pc->set(inst->pcState().instAddr() + 4);
                    }
                    hasSend = true;
                } else {
                    break;
                }
            }
        }
        if (hasSend) {
            set(pc[_tid], uc_pc);
            UopCache::HitInfo hit = uc->lookupFour(uc_pc->instAddr());
            DPRINTF(RxuFetch, "Redo looking up uopcache. Hit info: %#x.\n", hit.hit);
            while (hit.hit & 1) {
                pc[_tid]->advance(hit.pcShift(pc[_tid]->instAddr()));
                hit.advance();
            }
        }

        int size = instsFromUC.size();
        if (size > 0 && instsFromUC.back()->pcState().instAddr() > uc_pc->instAddr()) {
            while (size--) {
                DynInstPtr inst = instsFromUC.front();
                instsFromUC.pop_front();
                if (inst->pcState().instAddr() < uc_pc->instAddr()) {
                    instsFromUC.push_back(inst);
                } else {
                    DPRINTF(RxuFetch, "Removing inst %s to avoid repeated looking up uc.\n", inst->pcState());
                }
            }
        }
        
        DPRINTF(RxuFetch, "Looking up uop cache from %s.\n", *uc_pc);
        ucBlockLookupAddr = uc_pc->instAddr();
        ucHit = uc->access(*uc_pc, cachelines);
        // if (ucHit.hit == 0) ucHit.clear();
        // ucHit.more(uc->access(*uc_pc, cachelines - ucHit.win_size));
        DPRINTF(RxuFetch, "uc hit info: %#x, win size:%i.\n", ucHit.hit, ucHit.win_size);
    }

    // if (fetchQueue[_tid].size() > 0 && 
    //     fetchQueue[_tid].front()->pcState().instAddr() == pc[_tid]->instAddr()) {
    //     std::list<DynInstPtr>::iterator it = fetchQueue[_tid].begin();
    //     for (; it != fetchQueue[_tid].end(); ++it) {
    //         if ((*it)->pcState().instAddr() == pc[_tid]->instAddr()) {
    //             DPRINTF(RxuFetch, "PC %s has been fetched. Passing.\n", *pc[_tid]);
    //             pc[_tid]->advance();
    //         }
    //     }
    // } else {
    Addr fetchAddr = pc[_tid]->instAddr() & decoder[_tid]->pcMask();
    fetchCacheLine(fetchAddr, _tid, pc[_tid]->instAddr());
    // cpu->schedule(ucCompletion, cpu->clockEdge(Cycles(0)));
    fetchQueue[_tid].clear();
    decoder[_tid]->reset();
    // }
}

void 
Fetch::turnOffUC() {
    ucBlockLookupAddr = 0;
    ucBlockOffset = 0;
    ucHit.clear();
    instsFromUC.clear();
    uc->turnOff();
}

void 
Fetch::cancelSquash(PCStateBase &right_pc, bool l0_hit)
{
    DPRINTF(RxuFetch, "Clear wrong access icache in (%#x).\n", pc[_tid]->instAddr());
    turnOffIC(_tid);
    fetchQueue[_tid].clear();
    decoder[_tid]->reset();
    toBP0->size = 0;
    DPRINTF(RxuFetch, "Clear wrong access uop cache in (%#x).\n", ucBlockLookupAddr);
    turnOffUC();

    set(pc[_tid], right_pc);

    std::unique_ptr<PCStateBase> uc_pc(pc[_tid]->clone());
    UopCache::HitInfo hit = uc->lookupFour(pc[_tid]->instAddr());
    DPRINTF(RxuFetch, "Hit info: %#x.\n", hit.hit);
    while (hit.hitHead()) {
        pc[_tid]->advance(hit.pcShift(pc[_tid]->instAddr()));
        hit.advance();
    }

    ucBlockLookupAddr = uc_pc->instAddr();
    ucHit = uc->access(*uc_pc, 4);

    if (!l0_hit) {
        if (ucHit.hitHead()) {
            DPRINTF(RxuFetch, "L0 not hit. Accelerating uop cache sending.\n");
            if (!instsFromUC.empty()) {
                int insts_to_bp = 0;
                toBP0->size = 0;
                while (!stalls[_tid].bp && 
                    !ucHit.empty() && 
                    !instsFromUC.empty() && 
                    insts_to_bp < bpuWidth) {
                    DynInstPtr inst = instsFromUC.front();
                    toBP0->insts[toBP0->size++] = inst;
                    DPRINTF(RxuFetch, "[%#x] Sending instruction to BP0 from uop cache. "
                                                "Size: %i.\n", inst->pcState().instAddr(), instsFromUC.size());
                    wroteToTimeBuffer = true;
                    insts_to_bp++;
                    instsFromUC.pop_front();
                    ucHit.decrease();
                    if (inst->staticInst->isRxuJump()) {
                        while (!ucHit.empty()) {
                            instsFromUC.pop_front();
                            ucHit.decrease();
                        }
                        break;
                    }
                }
            } else {
                panic("Uopcache hit, but no insts.");
            }
            if (ucHit.empty()) ucHit.advance();
        }
    }

    Addr fetchAddr = pc[_tid]->instAddr() & decoder[_tid]->pcMask();
    fetchCacheLine(fetchAddr, _tid, pc[_tid]->instAddr());
}

void 
Fetch::removeAlltoRestore() 
{
    DPRINTF(RxuFetch, "Receive a taken branch PC in BP0. Squashing instructions.\n");

    toBP0->size = 0;

    if (ucAccessDone) reHit = ucHit;
    DPRINTF(RxuFetch, "Storing hit info: %#x\n", ucHit.hit);
    
    ucHit.clear();
    fetchQueue[_tid].clear();
    instsFromUC.clear();
}

// void 
// Fetch::cancelSquash(PCStateBase &right_pc, PCStateBase &false_pc)
// {
//     DPRINTF(RxuFetch, "Clear wrong access icache in (%s).\n", *pc[_tid]);
//     turnOffIC(_tid);
//     DPRINTF(RxuFetch, "Clear wrong access uop cache in (%s).\n", false_pc);
//     turnOffUC();
    
//     while (!restoreQueue.empty()) {
//         DPRINTF(RxuFetch, "Restoring inst [%s] from restore queue (size:%i).\n",
//                                 restoreQueue.front()->pcState(),
//                                 restoreQueue.size());
//         fetchQueue[_tid].push_back(restoreQueue.front());
//         restoreQueue.pop_front();
//     }
//     set(pc[_tid], right_pc);
// }

// void 
// Fetch::removeAlltoRestore() 
// {
//     DPRINTF(RxuFetch, "Receive a taken branch PC in BP0. Squashing instructions.\n");
//     restoreQueue.clear();
//     for (int i = 0; i < toBP0->size; i++) {
//         DPRINTF(RxuFetch, "Removing inst %s into restore queue from timebuff (size:%i).\n",
//                             toBP0->insts[i]->pcState(),
//                             restoreQueue.size() + 1);
//         restoreQueue.push_back(toBP0->insts[i]);
//     }
//     toBP0->size = 0;
//     UopCache::HitInfo pre_hit;
//     if (ucHit.hit <= 0xf) pre_hit = ucHit;
//     DPRINTF(RxuFetch, "preHit info: %#x\n", pre_hit.hit);
//     while (pre_hit.hit && !pre_hit.instCnts.empty()) {
//         if (pre_hit.hit & 1) {
//             for (int i = 0; i < pre_hit.instCnts.front() && !instsFromUC.empty(); ++i) {
//                 DPRINTF(RxuFetch, "Removing inst %s into restore queue from UC (size:%i).\n",
//                             instsFromUC.front()->pcState(),
//                             restoreQueue.size() + 1);
//                 restoreQueue.push_back(instsFromUC.front());
//                 instsFromUC.pop_front();
//             }
//         } else {
//             for (int i = 0; i < 16 && !fetchQueue[_tid].empty(); ++i) {
//                 DPRINTF(RxuFetch, "Removing inst %s into restore queue from fetch (size:%i).\n",
//                             fetchQueue[_tid].front()->pcState(),
//                             restoreQueue.size() + 1);
//                 restoreQueue.push_back(fetchQueue[_tid].front());
//                 fetchQueue[_tid].pop_front();
//             }
//         }
//         pre_hit.advance();
//     }

//     for (int i = 0; i < 16 && !fetchQueue[_tid].empty(); ++i) {
//         DPRINTF(RxuFetch, "Removing inst %s into restore queue from fetch (size:%i).\n",
//                     fetchQueue[_tid].front()->pcState(),
//                     restoreQueue.size() + 1);
//         restoreQueue.push_back(fetchQueue[_tid].front());
//         fetchQueue[_tid].pop_front();
//     }
// }

} // namespace rxuo3
} // namespace gem5
