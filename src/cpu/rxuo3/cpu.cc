#include "cpu/rxuo3/cpu.hh"

#include "cpu/activity.hh"
#include "cpu/checker/cpu.hh"
#include "cpu/checker/thread_context.hh"
#include "cpu/rxuo3/dyn_inst.hh"
#include "cpu/rxuo3/limits.hh"
#include "cpu/rxuo3/thread_context.hh"
#include "cpu/simple_thread.hh"
#include "cpu/thread_context.hh"
#include "debug/RxuActivity.hh"
#include "debug/Drain.hh"
#include "debug/RxuO3CPU.hh"
#include "debug/Quiesce.hh"
#include "enums/MemoryMode.hh"
#include "sim/cur_tick.hh"
#include "sim/full_system.hh"
#include "sim/process.hh"
#include "sim/stat_control.hh"
#include "sim/system.hh"

namespace gem5
{

struct BaseCPUParams;

namespace rxuo3
{

CPU::CPU(const BaseRxuO3CPUParams &params)
    : BaseCPU(params),
      mmu(params.mmu),
      tickEvent([this]{ tick(); }, "RxuO3CPU tick",
                false, Event::CPU_Tick_Pri),
      threadExitEvent([this]{ exitThreads(); }, "RxuO3CPU exit threads",
                false, Event::CPU_Exit_Pri),
#ifndef NDEBUG
      instcount(0),
#endif
      removeInstsThisCycle(false),
      iprefetch(params.iprefetch),
      fetch(this, params),
      bpu0(this, params),
      bpu1(this, params),
      iBandLB(this, params),
      uc(this, params),
      decode(this, params),
      // -- add by hongfei.liu ---------------
      predisq(this, params),
      rename(this, params),
      o3rename(this, params),
      rmu(this, params),
      dispipe0(this,params),
      dispipe1(this,params),
      dispipe2(this,params),
      dispipe3(this,params),
      // -------------------------------------
      ew(this, params),
      commit(this, params),

      regFile(params.system->numPhyIntRegs(),
              params.system->numPhyFpRegs(),
              params.numPhysVecRegs,
              params.numPhysVecPredRegs,
              params.numPhysMatRegs,
              params.numPhysCCRegs,
              params.isa[0]->regClasses()),

      freeList(name() + ".freelist", &regFile),

      o3_freeList(name() + ".o3freelist", &regFile),

      frm(),

      vecCsr(),
      mergeBuffer(16),

      rob(this, params),

      scoreboard(name() + ".scoreboard", regFile.totalNumPhysRegs()),

      isa(numThreads, NULL),

      rxu_rename(params.system->rxuRename()),

      timeBuffer(params.backComSize, params.forwardComSize),
      fetchQueue(params.backComSize, params.forwardComSize),
      bpu0Queue(params.backComSize, params.forwardComSize),
      bpu1Queue(params.backComSize, params.forwardComSize),
      IBandLBQueue(params.backComSize, params.forwardComSize),
      decodeQueue(params.backComSize, params.forwardComSize),
      // -- add by hongfei.liu --------------------------------
      predisqQueue(params.backComSize, params.forwardComSize),
      dispipe0ToRobQueue(params.backComSize, params.forwardComSize),
      dispipe0Queue(params.backComSize, params.forwardComSize),
      dispipe1Queue(params.backComSize, params.forwardComSize),
      dispipe2Queue(params.backComSize, params.forwardComSize),
      wakeQueue(params.backComSize, params.forwardComSize),
      // ------------------------------------------------------
    //   renameQueue(params.backComSize, params.forwardComSize),
      ewQueue(params.backComSize, params.forwardComSize),
      activityRec(name(), NumStages,
                  params.backComSize + params.forwardComSize,
                  params.activity),

      globalSeqNum(1),
      system(params.system),
      //lsq(this, params),
      lastRunningCycle(curCycle()),
      loopPC(params.system->loopPC()),
      loopstartPC(params.system->loopstartPC()),
      loopendPC(params.system->loopendPC()),
      vsetBranch(params.system->vsetBranch()),
      cpuStats(this)
{
    fatal_if(FullSystem && params.numThreads > 1,
            "SMT is not supported in RxuO3 in full system mode currently.");

    fatal_if(!FullSystem && params.numThreads < params.workload.size(),
            "More workload items (%d) than threads (%d) on CPU %s.",
            params.workload.size(), params.numThreads, name());

    if (!params.switched_out) {
        _status = Running;
    } else {
        _status = SwitchedOut;
    }

    if (params.checker) {
        BaseCPU *temp_checker = params.checker;
        checker = dynamic_cast<Checker<DynInstPtr> *>(temp_checker);
        checker->setIcachePort(&fetch.getInstPort());
        checker->setSystem(params.system);
    } else {
        checker = NULL;
    }

    if (!FullSystem) {
        thread.resize(numThreads);
        tids.resize(numThreads);
    }

    // The stages also need their CPU pointer setup.  However this
    // must be done at the upper level CPU because they have pointers
    // to the upper level CPU, and not this CPU.

    // Set up Pointers to the activeThreads list for each stage
    fetch.setActiveThreads(&activeThreads);
    bpu0.setActiveThreads(&activeThreads);
    bpu1.setActiveThreads(&activeThreads);
    iBandLB.setActiveThreads(&activeThreads);
    decode.setActiveThreads(&activeThreads);
    // -- add by hongfei.liu -----------------------
    predisq.setActiveThreads(&activeThreads);
    dispipe0.setActiveThreads(&activeThreads);
    dispipe1.setActiveThreads(&activeThreads);
    dispipe2.setActiveThreads(&activeThreads);
    dispipe3.setActiveThreads(&activeThreads);
    // ---------------------------------------------
    // rename.setActiveThreads(&activeThreads);
    ew.setActiveThreads(&activeThreads);
    commit.setActiveThreads(&activeThreads);

    // Give each of the stages the time buffer they will use.
    fetch.setTimeBuffer(&timeBuffer);
    bpu0.setTimeBuffer(&timeBuffer);
    bpu1.setTimeBuffer(&timeBuffer);
    iBandLB.setTimeBuffer(&timeBuffer);
    decode.setTimeBuffer(&timeBuffer);
    // -- add by hongfei.liu -----------------------
    predisq.setTimeBuffer(&timeBuffer);
    dispipe0.setTimeBuffer(&timeBuffer);
    dispipe1.setTimeBuffer(&timeBuffer);
    dispipe2.setTimeBuffer(&timeBuffer);
    dispipe3.setTimeBuffer(&timeBuffer);
    // ---------------------------------------------
    // rename.setTimeBuffer(&timeBuffer);
    ew.setTimeBuffer(&timeBuffer);
    commit.setTimeBuffer(&timeBuffer);

    // Also setup each of the stages' queues.
    fetch.setFetchQueue(&fetchQueue);
    bpu0.setFetchQueue(&fetchQueue);
    bpu0.setBpu0Queue(&bpu0Queue);
    bpu1.setBpu0Queue(&bpu0Queue);
    bpu1.setBpu1Queue(&bpu1Queue);
    iBandLB.setBpu1Queue(&bpu1Queue);
    iBandLB.setIBandLBQueue(&IBandLBQueue);
    decode.setIBandLBQueue(&IBandLBQueue);
    commit.setFetchQueue(&fetchQueue);
    decode.setDecodeQueue(&decodeQueue);
    // -- add by hongfei.liu -----------------
    predisq.setDecodeQueue(&decodeQueue);
    predisq.setPredisqQueue(&predisqQueue);
    dispipe0.setPredisqQueue(&predisqQueue);
    dispipe0.setDispipe0ToRobQueue(&dispipe0ToRobQueue);
    dispipe0.setDispipe0Queue(&dispipe0Queue);
    dispipe1.setDispipe0Queue(&dispipe0Queue);
    dispipe1.setDispipe1Queue(&dispipe1Queue);
    dispipe1.setDispipe2Queue(&dispipe2Queue);
    dispipe2.setDispipe1Queue(&dispipe1Queue);
    dispipe2.setDispipe2Queue(&dispipe2Queue);
    dispipe3.setDispipe2Queue(&dispipe2Queue);
    dispipe3.setWakeQueue(&wakeQueue);
    // rename.setDecodeQueue(&decodeQueue);
    // rename.setPredisqQueue(&predisqQueue);
    // ---------------------------------------
    // rename.setRenameQueue(&renameQueue);
    // iew.setRenameQueue(&renameQueue);
    ew.setWakeQueue(&wakeQueue);
    ew.setEWQueue(&ewQueue);
    commit.setEWQueue(&ewQueue);
    // commit.setRenameQueue(&renameQueue);
    commit.setDispipe0ToRobQueue(&dispipe0ToRobQueue);

    dispipe0.setCommitStage(&commit);
    dispipe0.setRMU(&rmu);

if (rxu_rename) {
    dispipe0.setRename(&rename);
} else {
    dispipe0.setO3Rename(&o3rename);
}
    dispipe0.setDispipe3(&dispipe3);

    dispipe1.setRMU(&rmu);

    dispipe2.setRMU(&rmu);

    dispipe3.setRMU(&rmu);

    ew.setDispipe3Satge(&dispipe3);

    // dispipe3.ldstQueue.setEWStage(&ew);
    commit.setEWStage(&ew);
    commit.setDispipe3Stage(&dispipe3);
    // commit.setEWStage(&ew);
    // rename.setIEWStage(&iew);
    // rename.setCommitStage(&commit);
    // dispipe0.setIEWStage(&iew);
    // dispipe0.setCommitStage(&commit);
    // dispipe2.setIEWStage(&iew);
    // dispipe2.setCommitStage(&commit);

    fetch.setUC(&uc);

    ThreadID active_threads;
    if (FullSystem) {
        active_threads = 1;
    } else {
        active_threads = params.workload.size();

        if (active_threads > MaxThreads) {
            panic("Workload Size too large. Increase the 'MaxThreads' "
                  "constant in cpu/rxuo3/limits.hh or edit your workload size.");
        }
    }

    // Make Sure That this a Valid Architeture
    assert(numThreads);
    const auto &regClasses = params.isa[0]->regClasses();

    assert(params.numPhysIntRegs >=
            numThreads * regClasses.at(IntRegClass)->numRegs());
    assert(params.numPhysFloatRegs >=
            numThreads * regClasses.at(FloatRegClass)->numRegs());
    assert(params.numPhysVecRegs >=
            numThreads * regClasses.at(VecRegClass)->numRegs());
    assert(params.numPhysVecPredRegs >=
            numThreads * regClasses.at(VecPredRegClass)->numRegs());
    assert(params.numPhysMatRegs >=
            numThreads * regClasses.at(MatRegClass)->numRegs());
    assert(params.numPhysCCRegs >=
            numThreads * regClasses.at(CCRegClass)->numRegs());

    // Just make this a warning and go ahead anyway, to keep from having to
    // add checks everywhere.
    warn_if(regClasses.at(CCRegClass)->numRegs() == 0 &&
            params.numPhysCCRegs != 0,
            "Non-zero number of physical CC regs specified, even though\n"
            "    ISA does not use them.");
if (rxu_rename){
    rename.setScoreboard(&scoreboard);
    }
else{
    o3rename.setScoreboard(&scoreboard);
    }

    ew.setScoreboard(&scoreboard);

    // Setup the rename map for whichever stages need it.
    for (ThreadID tid = 0; tid < numThreads; tid++) {
        isa[tid] = params.isa[tid];
        if (rxu_rename) {
            commitRenameMap[tid].init(regClasses, &regFile, &freeList);
            renameMap[tid].init(regClasses, &regFile, &freeList);
        } else {
            commitRenameMap[tid].init(regClasses, &regFile, &o3_freeList);
            renameMap[tid].init(regClasses, &regFile, &o3_freeList);
        }
    }

    // Initialize rename map to assign physical registers to the
    // architectural registers for active threads only.
    for (ThreadID tid = 0; tid < active_threads; tid++) {
        for (auto type = (RegClassType)0; type <= CCRegClass;
                type = (RegClassType)(type + 1)) {
            for (auto &id: *regClasses.at(type)) {
                // Note that we can't use the rename() method because we don't
                // want special treatment for the zero register at this point
                if (rxu_rename) {
                    PhysRegIdPtr phys_reg = freeList.getReg(type);
                    renameMap[tid].setEntry(id, phys_reg);
                    commitRenameMap[tid].setEntry(id, phys_reg);
                } else {
                    PhysRegIdPtr phys_reg = o3_freeList.getReg(type);
                    renameMap[tid].setEntry(id, phys_reg);
                    commitRenameMap[tid].setEntry(id, phys_reg);
                }
            }
        }
    }

if (params.system->useRotating() && rxu_rename) 
{
    freeList.setRotating(false);
}

if (params.system->idealRotating() && rxu_rename) {
    freeList.setRotating(true);
}

if (rxu_rename){
    rename.setRenameMap(renameMap);
} 
else{
    o3rename.setRenameMap(renameMap);
}
    
    commit.setRenameMap(commitRenameMap);
if (rxu_rename) {
    rename.setFreeList(&freeList);
    rename.setFrm(&frm);
    rename.setVecCsr(&vecCsr);
} else {
    o3rename.setFreeList(&o3_freeList);
    o3rename.setFrm(&frm);
}
    frm.setWTB(&(dispipe3.wtb));

    vecCsr.setWTB(Vxrm, &(dispipe3.wtb));
    vecCsr.setWTB(Vl, &(dispipe3.wtb));
    vecCsr.setWTB(Vtype, &(dispipe3.wtb));

    // dispipe0.setRename(&rename);
    // dispipe1.setRename(&rename);

    // Setup the ROB for whichever stages need it.
    commit.setROB(&rob);

    lastActivatedCycle = 0;

    DPRINTF(RxuO3CPU, "Creating RxuO3CPU object.\n");

    // Setup any thread state.
    thread.resize(numThreads);

    for (ThreadID tid = 0; tid < numThreads; ++tid) {
        if (FullSystem) {
            // SMT is not supported in FS mode yet.
            assert(numThreads == 1);
            thread[tid] = new ThreadState(this, 0, NULL);
        } else {
            if (tid < params.workload.size()) {
                DPRINTF(RxuO3CPU, "Workload[%i] process is %#x", tid,
                        thread[tid]);
                thread[tid] = new ThreadState(this, tid, params.workload[tid]);
            } else {
                //Allocate Empty thread so M5 can use later
                //when scheduling threads to CPU
                Process* dummy_proc = NULL;

                thread[tid] = new ThreadState(this, tid, dummy_proc);
            }
        }

        gem5::ThreadContext *tc;

        // Setup the TC that will serve as the interface to the threads/CPU.
        auto *rxuo3_tc = new ThreadContext;

        tc = rxuo3_tc;

        // If we're using a checker, then the TC should be the
        // CheckerThreadContext.
        if (params.checker) {
            tc = new CheckerThreadContext<ThreadContext>(rxuo3_tc, checker);
        }

        rxuo3_tc->cpu = this;
        rxuo3_tc->thread = thread[tid];

        // Give the thread the TC.
        thread[tid]->tc = tc;

        // Add the TC to the CPU's list of TC's.
        threadContexts.push_back(tc);
    }

    // RxuO3CPU always requires an interrupt controller.
    if (!params.switched_out && interrupts.empty()) {
        fatal("RxuO3CPU %s has no interrupt controller.\n"
              "Ensure createInterruptController() is called.\n", name());
    }

    loopIndex = 0;
    loopxhIndex = 0;
    // frm_Ready frm;
    // frm.NewSeqNum = 0;
    // frm.ready = true;
    // frm_Insts.push_back(frm);
}

void
CPU::regProbePoints()
{
    BaseCPU::regProbePoints();

    ppInstAccessComplete = new ProbePointArg<PacketPtr>(
            getProbeManager(), "InstAccessComplete");
    ppDataAccessComplete = new ProbePointArg<
        std::pair<DynInstPtr, PacketPtr>>(
                getProbeManager(), "DataAccessComplete");

    fetch.regProbePoints();
    rename.regProbePoints();
    o3rename.regProbePoints();
    dispipe3.regProbePoints();
    ew.regProbePoints();
    commit.regProbePoints();
}

CPU::CPUStats::CPUStats(CPU *cpu)
    : statistics::Group(cpu),
      ADD_STAT(timesIdled, statistics::units::Count::get(),
               "Number of times that the entire CPU went into an idle state "
               "and unscheduled itself"),
      ADD_STAT(idleCycles, statistics::units::Cycle::get(),
               "Total number of cycles that the CPU has spent unscheduled due "
               "to idling"),
      ADD_STAT(quiesceCycles, statistics::units::Cycle::get(),
               "Total number of cycles that CPU has spent quiesced or waiting "
               "for an interrupt"),
      // -- add by hongfei.liu -------------------------------------------------
      ADD_STAT(committedInsts, statistics::units::Count::get(),
               "Number of Instructions Simulated"),
      ADD_STAT(committedMacroVectorInsts, statistics::units::Count::get(),
               "Number of Macro vector Instructions Simulated"),
      ADD_STAT(committedOps, statistics::units::Count::get(),
               "Number of Ops (including micro ops) Simulated"),
      ADD_STAT(cpi, statistics::units::Rate<
                    statistics::units::Cycle, statistics::units::Count>::get(),
               "CPI: Cycles Per Instruction"),
      ADD_STAT(totalCpi, statistics::units::Rate<
                    statistics::units::Cycle, statistics::units::Count>::get(),
               "CPI: Total CPI of All Threads"),
    //   ADD_STAT(ipc, statistics::units::Rate<
    //                 statistics::units::Count, statistics::units::Cycle>::get(),
    //            "IPC: Instructions Per Cycle"),
      ADD_STAT(macroVectorIpc, statistics::units::Rate<
                    statistics::units::Count, statistics::units::Cycle>::get(),
               "Macro vector IPC: Instructions Per Cycle"),
      ADD_STAT(totalIpc, statistics::units::Rate<
                    statistics::units::Count, statistics::units::Cycle>::get(),
               "IPC: Total IPC of All Threads"),
      ADD_STAT(intRegfileReads, statistics::units::Count::get(),
               "Number of integer regfile reads"),
      ADD_STAT(intRegfileWrites, statistics::units::Count::get(),
               "Number of integer regfile writes"),
      ADD_STAT(fpRegfileReads, statistics::units::Count::get(),
               "Number of floating regfile reads"),
      ADD_STAT(fpRegfileWrites, statistics::units::Count::get(),
               "Number of floating regfile writes"),
      ADD_STAT(vecRegfileReads, statistics::units::Count::get(),
               "number of vector regfile reads"),
      ADD_STAT(vecRegfileWrites, statistics::units::Count::get(),
               "number of vector regfile writes"),
      ADD_STAT(vecPredRegfileReads, statistics::units::Count::get(),
               "number of predicate regfile reads"),
      ADD_STAT(vecPredRegfileWrites, statistics::units::Count::get(),
               "number of predicate regfile writes"),
      ADD_STAT(ccRegfileReads, statistics::units::Count::get(),
               "number of cc regfile reads"),
      ADD_STAT(ccRegfileWrites, statistics::units::Count::get(),
               "number of cc regfile writes"),
      ADD_STAT(miscRegfileReads, statistics::units::Count::get(),
               "number of misc regfile reads"),
      ADD_STAT(miscRegfileWrites, statistics::units::Count::get(),
               "number of misc regfile writes")
      // -----------------------------------------------------------------------
{
    // Register any of the RxuO3CPU's stats here.
    timesIdled
        .prereq(timesIdled);

    idleCycles
        .prereq(idleCycles);

    quiesceCycles
        .prereq(quiesceCycles);
    // -- add by hongfei.liu ------------------------------------
    committedInsts
        .init(cpu->numThreads)
        .flags(statistics::total);

    committedMacroVectorInsts
        .prereq(committedMacroVectorInsts);

    committedOps
        .init(cpu->numThreads)
        .flags(statistics::total);

    cpi
        .precision(6);
    cpi = cpu->baseStats.numCycles / committedInsts;

    totalCpi
        .precision(6);
    totalCpi = cpu->baseStats.numCycles / sum(committedInsts);

    ipc
        .precision(6);
    ipc = committedInsts / cpu->baseStats.numCycles;

    macroVectorIpc
        .precision(6);
    macroVectorIpc = committedMacroVectorInsts / cpu->baseStats.numCycles;

    totalIpc
        .precision(6);
    totalIpc = sum(committedInsts) / cpu->baseStats.numCycles;

    intRegfileReads
        .prereq(intRegfileReads);

    intRegfileWrites
        .prereq(intRegfileWrites);

    fpRegfileReads
        .prereq(fpRegfileReads);

    fpRegfileWrites
        .prereq(fpRegfileWrites);

    vecRegfileReads
        .prereq(vecRegfileReads);

    vecRegfileWrites
        .prereq(vecRegfileWrites);

    vecPredRegfileReads
        .prereq(vecPredRegfileReads);

    vecPredRegfileWrites
        .prereq(vecPredRegfileWrites);

    ccRegfileReads
        .prereq(ccRegfileReads);

    ccRegfileWrites
        .prereq(ccRegfileWrites);

    miscRegfileReads
        .prereq(miscRegfileReads);

    miscRegfileWrites
        .prereq(miscRegfileWrites);
    // ----------------------------------------------------------
}

void
CPU::tick()
{
    DPRINTF(RxuO3CPU, "\n\nRxuO3CPU: Ticking main, RxuO3CPU.\n");
    assert(!switchedOut());
    assert(drainState() != DrainState::Drained);

    ++baseStats.numCycles;
    loopStats[loopIndex]->numCycles++;

    if (loopIndex >= 1 && loopIndex <= 500) {
        baseStats.numCycles1_500loop++;
    } else if (loopIndex >= 501 && loopIndex <= 1000) {
        baseStats.numCycles501_1000loop++;
    } else if (loopIndex >= 1001 && loopIndex <= 1500) {
        baseStats.numCycles1001_1500loop++;
    } else if (loopIndex >= 1501 && loopIndex <= 2000) {
        baseStats.numCycles1501_2000loop++;
    }

     ++baseStats.xhnumCycles;
    loopStats[loopxhIndex]->xhnumCycles++;

    if (loopxhIndex >= 1 && loopxhIndex <= 110) {
        ++baseStats.xhnumCycles1_110loop;
    }
    if (loopxhIndex >= 1 && loopxhIndex <= 30) {
        ++baseStats.xhnumCycles1_30loop;
    }
    if (loopxhIndex >= 1 && loopxhIndex <= 64) {
        ++baseStats.xhnumCycles1_64loop;
    }
    if (loopxhIndex >= 1 && loopxhIndex <= 134) {
        ++baseStats.xhnumCycles1_134loop;
    }
    if (loopxhIndex >= 1 && loopxhIndex <= 1024) {
        ++baseStats.xhnumCycles1_1024loop;
    }
    if (loopxhIndex >= 50 && loopxhIndex <= 110) {
        ++baseStats.xhnumCycles50_110loop;
    }
    if (loopxhIndex >= 20 && loopxhIndex <= 30) {
        ++baseStats.xhnumCycles20_30loop;
    }
    if (loopxhIndex >= 30 && loopxhIndex <= 64) {
        ++baseStats.xhnumCycles30_64loop;
    }
    if (loopxhIndex >= 60 && loopxhIndex <= 134) {
        ++baseStats.xhnumCycles60_134loop;
    }
    if (loopxhIndex >= 500 && loopxhIndex <= 1024) {
        ++baseStats.xhnumCycles500_1024loop;
    }
    if (loopxhIndex >= 1000 && loopxhIndex <= 1024) {
        ++baseStats.xhnumCycles1000_1024loop;
    }

    if (loopIndex >= 1 && loopIndex <= 100) {
        baseStats.numCycles1_100loop++;
    } else if (loopIndex >= 101 && loopIndex <= 200) {
        baseStats.numCycles101_200loop++;
    } else if (loopIndex >= 201 && loopIndex <= 300) {
        baseStats.numCycles201_300loop++;
    } else if (loopIndex >= 301 && loopIndex <= 400) {
        baseStats.numCycles301_400loop++;
    } else if (loopIndex >= 401 && loopIndex <= 500) {
        baseStats.numCycles401_500loop++;
    } else if (loopIndex >= 501 && loopIndex <= 600) {
        baseStats.numCycles501_600loop++;
    } else if (loopIndex >= 601 && loopIndex <= 700) {
        baseStats.numCycles601_700loop++;
    } else if (loopIndex >= 701 && loopIndex <= 800) {
        baseStats.numCycles701_800loop++;
    } else if (loopIndex >= 801 && loopIndex <= 900) {
        baseStats.numCycles801_900loop++;
    } else if (loopIndex >= 901 && loopIndex <= 1000) {
        baseStats.numCycles901_1000loop++;
    } else if (loopIndex >= 1001 && loopIndex <= 1100) {
        baseStats.numCycles1001_1100loop++;
    } else if (loopIndex >= 1101 && loopIndex <= 1200) {
        baseStats.numCycles1101_1200loop++;
    } else if (loopIndex >= 1201 && loopIndex <= 1300) {
        baseStats.numCycles1201_1300loop++;
    } else if (loopIndex >= 1301 && loopIndex <= 1400) {
        baseStats.numCycles1301_1400loop++;
    } else if (loopIndex >= 1401 && loopIndex <= 1500) {
        baseStats.numCycles1401_1500loop++;
    } else if (loopIndex >= 1501 && loopIndex <= 1600) {
        baseStats.numCycles1501_1600loop++;
    } else if (loopIndex >= 1601 && loopIndex <= 1700) {
        baseStats.numCycles1601_1700loop++;
    } else if (loopIndex >= 1701 && loopIndex <= 1800) {
        baseStats.numCycles1701_1800loop++;
    } else if (loopIndex >= 1801 && loopIndex <= 1900) {
        baseStats.numCycles1801_1900loop++;
    } else if (loopIndex >= 1901 && loopIndex <= 2000) {
        baseStats.numCycles1901_2000loop++;
    }


    if (curTick() <= 6000000) {
        ++baseStats.xhnumCycles1;
    }
    if (curTick() > 6000000 && curTick() <= 12000000) {
        ++baseStats.xhnumCycles2;
    }

    if (curTick() <= 6000000) {
        ++baseStats.xhnumCycles1;
    }
    if (curTick() > 6000000 && curTick() <= 12000000) {
        ++baseStats.xhnumCycles2;
    }

    updateCycleCounters(BaseCPU::CPU_STATE_ON);
    // iprefetch
    if(iprefetch)iprefetch->tick();
//    activity = false;

    //Tick each of the stages
    auto start = std::chrono::high_resolution_clock::now();

    fetch.tick();
    auto fetch_done = std::chrono::high_resolution_clock::now();

    bpu0.tick();
    auto bpu0_done = std::chrono::high_resolution_clock::now();

    bpu1.tick();
    auto bpu1_done = std::chrono::high_resolution_clock::now();

    iBandLB.tick();
    auto iBandLB_done = std::chrono::high_resolution_clock::now();

    decode.tick();
    auto decode_done = std::chrono::high_resolution_clock::now();

    // -- add by hongfei.liu ------------
    predisq.tick();
    auto predisq_done = std::chrono::high_resolution_clock::now();

    dispipe0.tick();
    auto dispipe0_done = std::chrono::high_resolution_clock::now();

    dispipe1.tick();
    auto dispipe1_done = std::chrono::high_resolution_clock::now();

    dispipe2.tick();
    auto dispipe2_done = std::chrono::high_resolution_clock::now();

    dispipe3.tick();
    auto dispipe3_done = std::chrono::high_resolution_clock::now();
    // ----------------------------------

    // rename.tick();

    ew.tick();
    auto ew_done = std::chrono::high_resolution_clock::now();

    commit.tick();
    auto commit_done = std::chrono::high_resolution_clock::now();

    // Now advance the time buffers
    timeBuffer.advance();

    fetchQueue.advance();
    bpu0Queue.advance();
    bpu1Queue.advance();
    IBandLBQueue.advance();
    decodeQueue.advance();
    // -- add by hongfei.liu ------------
    predisqQueue.advance();
    dispipe0ToRobQueue.advance();
    dispipe0Queue.advance();
    dispipe1Queue.advance();
    dispipe2Queue.advance();
    wakeQueue.advance();
    // ----------------------------------
    // renameQueue.advance();
    ewQueue.advance();

    activityRec.advance();

    if (removeInstsThisCycle) {
        cleanUpRemovedInsts();
    }

    if (!tickEvent.scheduled()) {
        if (_status == SwitchedOut) {
            DPRINTF(RxuO3CPU, "Switched out!\n");
            // increment stat
            lastRunningCycle = curCycle();
        } 
        // else if (!activityRec.active() || _status == Idle) {
        //     DPRINTF(RxuO3CPU, "Idle!\n");
        //     lastRunningCycle = curCycle();
        //     cpuStats.timesIdled++;
        // } 
        else {
            schedule(tickEvent, clockEdge(Cycles(1)));
            DPRINTF(RxuO3CPU, "Scheduling next tick!\n");
        }
    }

    dispipe3.wtb.setFreeReadRegPorts();
    dispipe3.wtb.updateWbNums();

    auto end = std::chrono::high_resolution_clock::now();
    if (curCycle() % 10000 == 0 && curCycle() > 0) {
        cprintf("curCycle(): %llu\n", curCycle());
        
        cprintf("Number of current committed Macro instructions: %llu\n", curMacroCommitInsts);
        cprintf("Number of current committed Micro instructions: %llu\n", curMicroCommitInsts);
        double cur_macro_ipc = (curMacroCommitInsts - lastMacroCommitInsts) * 1.0 / 10000;
        double cur_micro_ipc = (curMicroCommitInsts - lastMicroCommitInsts) * 1.0 / 10000;
        if (cur_micro_ipc < 0.0001) {
            lowIPCCount++;
        } else {
            lowIPCCount = 0;
        }
        if (lowIPCCount >= 10) {
            panic("IPC < 0.0001 sustain. An error existing in your CPU. Please check it.\n");
        }
        if (curCycle()) {
            cprintf("Current Macro IPC: %f\n", cur_macro_ipc);
            cprintf("Current Micro IPC: %f\n", cur_micro_ipc);
            cprintf("Total Macro IPC: %f\n", (double)curMacroCommitInsts / (double)curCycle());
            cprintf("Total Micro IPC: %f\n", (double)curMicroCommitInsts / (double)curCycle());
        } else {
            cprintf("Current Macro IPC: 0.0\n");
            cprintf("Current Micro IPC: 0.0\n");
            cprintf("Total Macro IPC: 0.0\n");
            cprintf("Total Micro IPC: 0.0\n");
        }
        macroNumber = curMacroCommitInsts;
        microNumber = curMicroCommitInsts;
        lastMacroCommitInsts = curMacroCommitInsts;
        lastMicroCommitInsts = curMicroCommitInsts;
        cprintf("\n");

        // cprintf("Time taken by uc: %llu\n", 
        //         std::chrono::duration_cast<std::chrono::microseconds>(uc_done - start).count());
        // cprintf("Time taken by fetch: %llu\n", 
        //         std::chrono::duration_cast<std::chrono::microseconds>(fetch_done - start).count());
        // cprintf("Time taken by bpu0: %llu\n", 
        //         std::chrono::duration_cast<std::chrono::microseconds>(bpu0_done - fetch_done).count());
        // cprintf("Time taken by bpu1: %llu\n", 
        //         std::chrono::duration_cast<std::chrono::microseconds>(bpu1_done - bpu0_done).count());
        // cprintf("Time taken by IBandLB: %llu\n", 
        //         std::chrono::duration_cast<std::chrono::microseconds>(iBandLB_done - bpu1_done).count());
        // cprintf("Time taken by decode: %llu\n", 
        //         std::chrono::duration_cast<std::chrono::microseconds>(decode_done - iBandLB_done).count());
        // cprintf("Time taken by predisq: %llu\n", 
        //         std::chrono::duration_cast<std::chrono::microseconds>(predisq_done - decode_done).count());
        // cprintf("Time taken by dispipe0: %llu\n", 
        //         std::chrono::duration_cast<std::chrono::microseconds>(dispipe0_done - predisq_done).count());
        // cprintf("Time taken by dispipe1: %llu\n", 
        //         std::chrono::duration_cast<std::chrono::microseconds>(dispipe1_done - dispipe0_done).count());
        // cprintf("Time taken by dispipe2: %llu\n", 
        //         std::chrono::duration_cast<std::chrono::microseconds>(dispipe2_done - dispipe1_done).count());
        // cprintf("Time taken by dispipe3: %llu\n", 
        //         std::chrono::duration_cast<std::chrono::microseconds>(dispipe3_done - dispipe2_done).count());
        // cprintf("Time taken by ew: %llu\n", 
        //         std::chrono::duration_cast<std::chrono::microseconds>(ew_done - dispipe3_done).count());
        // cprintf("Time taken by commit: %llu\n", 
        //         std::chrono::duration_cast<std::chrono::microseconds>(commit_done - ew_done).count());
        // cprintf("Time taken by other: %llu\n", 
        //         std::chrono::duration_cast<std::chrono::microseconds>(end - commit_done).count());

        cprintf("\n");
        cprintf("\n");
    }

    if (!FullSystem)
        updateThreadPriority();

    tryDrain();
}

void
CPU::init()
{
    BaseCPU::init();

    for (ThreadID tid = 0; tid < numThreads; ++tid) {
        // Set noSquashFromTC so that the CPU doesn't squash when initially
        // setting up registers.
        thread[tid]->noSquashFromTC = true;
    }

    // Clear noSquashFromTC.
    for (int tid = 0; tid < numThreads; ++tid)
        thread[tid]->noSquashFromTC = false;

    commit.setThreads(thread);
}

void
CPU::startup()
{
    BaseCPU::startup();

    fetch.startupStage();
    bpu0.startupStage();
    bpu1.startupStage();
    iBandLB.startupStage();
    decode.startupStage();
    // -- add by hongfei.liu ------------
    predisq.startupStage();
    dispipe0.startupStage();
    dispipe1.startupStage();
    dispipe2.startupStage();
    dispipe3.startupStage();
    // ----------------------------------
    ew.startupStage();
    // rename.startupStage();
    commit.startupStage();
}

void
CPU::activateThread(ThreadID tid)
{
    std::list<ThreadID>::iterator isActive =
        std::find(activeThreads.begin(), activeThreads.end(), tid);

    DPRINTF(RxuO3CPU, "[tid:%i] Calling activate thread.\n", tid);
    assert(!switchedOut());

    if (isActive == activeThreads.end()) {
        DPRINTF(RxuO3CPU, "[tid:%i] Adding to active threads list\n", tid);

        activeThreads.push_back(tid);
    }
}

void
CPU::deactivateThread(ThreadID tid)
{
    // hardware transactional memory
    // shouldn't deactivate thread in the middle of a transaction
    assert(!commit.executingHtmTransaction(tid));

    //Remove From Active List, if Active
    std::list<ThreadID>::iterator thread_it =
        std::find(activeThreads.begin(), activeThreads.end(), tid);

    DPRINTF(RxuO3CPU, "[tid:%i] Calling deactivate thread.\n", tid);
    assert(!switchedOut());

    if (thread_it != activeThreads.end()) {
        DPRINTF(RxuO3CPU,"[tid:%i] Removing from active threads list\n",
                tid);
        activeThreads.erase(thread_it);
    }

    fetch.deactivateThread(tid);
    commit.deactivateThread(tid);
}

Counter
CPU::totalInsts() const
{
    Counter total(0);

    ThreadID size = thread.size();
    for (ThreadID i = 0; i < size; i++)
        total += thread[i]->numInst;

    return total;
}

Counter
CPU::totalOps() const
{
    Counter total(0);

    ThreadID size = thread.size();
    for (ThreadID i = 0; i < size; i++)
        total += thread[i]->numOp;

    return total;
}

void
CPU::activateContext(ThreadID tid)
{
    assert(!switchedOut());

    // Needs to set each stage to running as well.
    activateThread(tid);

    // We don't want to wake the CPU if it is drained. In that case,
    // we just want to flag the thread as active and schedule the tick
    // event from drainResume() instead.
    if (drainState() == DrainState::Drained)
        return;

    // If we are time 0 or if the last activation time is in the past,
    // schedule the next tick and wake up the fetch unit
    if (lastActivatedCycle == 0 || lastActivatedCycle < curTick()) {
        scheduleTickEvent(Cycles(0));

        // Be sure to signal that there's some activity so the CPU doesn't
        // deschedule itself.
        activityRec.activity();
        fetch.wakeFromQuiesce();

        Cycles cycles(curCycle() - lastRunningCycle);
        // @todo: This is an oddity that is only here to match the stats
        if (cycles != 0)
            --cycles;
        cpuStats.quiesceCycles += cycles;

        lastActivatedCycle = curTick();

        _status = Running;

        BaseCPU::activateContext(tid);
    }
}

void
CPU::suspendContext(ThreadID tid)
{
    DPRINTF(RxuO3CPU,"[tid:%i] Suspending Thread Context.\n", tid);
    assert(!switchedOut());

    deactivateThread(tid);

    // If this was the last thread then unschedule the tick event.
    if (activeThreads.size() == 0) {
        unscheduleTickEvent();
        lastRunningCycle = curCycle();
        _status = Idle;
    }

    DPRINTF(Quiesce, "Suspending Context\n");

    BaseCPU::suspendContext(tid);
}

void
CPU::haltContext(ThreadID tid)
{
    //For now, this is the same as deallocate
    DPRINTF(RxuO3CPU,"[tid:%i] Halt Context called. Deallocating\n", tid);
    assert(!switchedOut());

    deactivateThread(tid);
    removeThread(tid);

    // If this was the last thread then unschedule the tick event.
    if (activeThreads.size() == 0) {
        if (tickEvent.scheduled())
        {
            unscheduleTickEvent();
        }
        lastRunningCycle = curCycle();
        _status = Idle;
    }
    updateCycleCounters(BaseCPU::CPU_STATE_SLEEP);
}

void
CPU::insertThread(ThreadID tid)
{
    DPRINTF(RxuO3CPU,"[tid:%i] Initializing thread into CPU");
    // Will change now that the PC and thread state is internal to the CPU
    // and not in the ThreadContext.
    gem5::ThreadContext *src_tc;
    if (FullSystem)
        src_tc = system->threads[tid];
    else
        src_tc = tcBase(tid);

    //Bind Int Regs to Rename Map
    const auto &regClasses = isa[tid]->regClasses();

    for (auto type = (RegClassType)0; type <= CCRegClass;
            type = (RegClassType)(type + 1)) {
        for (auto &id: *regClasses.at(type)) {
            PhysRegIdPtr phys_reg = freeList.getReg(type);
            renameMap[tid].setEntry(id, phys_reg);
            scoreboard.setReg(phys_reg);
        }
    }

    //Copy Thread Data Into RegFile
    //copyFromTC(tid);

    //Set PC/NPC/NNPC
    pcState(src_tc->pcState(), tid);

    src_tc->setStatus(gem5::ThreadContext::Active);

    activateContext(tid);

    //Reset ROB/IQ/LSQ Entries
    commit.rob->resetEntries();
}

void
CPU::removeThread(ThreadID tid)
{
    DPRINTF(RxuO3CPU,"[tid:%i] Removing thread context from CPU.\n", tid);

    // Copy Thread Data From RegFile
    // If thread is suspended, it might be re-allocated
    // copyToTC(tid);


    // @todo: 2-27-2008: Fix how we free up rename mappings
    // here to alleviate the case for double-freeing registers
    // in SMT workloads.

    // clear all thread-specific states in each stage of the pipeline
    // since this thread is going to be completely removed from the CPU
    commit.clearStates(tid);
    fetch.clearStates(tid);
    bpu0.clearStates(tid);
    bpu1.clearStates(tid);
    iBandLB.clearStates(tid);
    decode.clearStates(tid);
    // -- add by hongfei.liu ------------
    predisq.clearStates(tid);
    dispipe0.clearStates(tid);
    dispipe1.clearStates(tid);
    dispipe2.clearStates(tid);
    dispipe3.clearStates(tid);
    // ----------------------------------
    // rename.clearStates(tid);
    ew.clearStates(tid);

    // Flush out any old data from the time buffers.
    for (int i = 0; i < timeBuffer.getSize(); ++i) {
        timeBuffer.advance();
        fetchQueue.advance();
        bpu0Queue.advance();
        bpu1Queue.advance();
        IBandLBQueue.advance();
        decodeQueue.advance();
        // -- add by hongfei.liu ------------
        predisqQueue.advance();
        dispipe0ToRobQueue.advance();
        dispipe0Queue.advance();
        dispipe1Queue.advance();
        dispipe2Queue.advance();
        wakeQueue.advance();
        // ----------------------------------
        // renameQueue.advance();
        ewQueue.advance();
    }

    // at this step, all instructions in the pipeline should be already
    // either committed successfully or squashed. All thread-specific
    // queues in the pipeline must be empty.
    // assert(dispipe3.instQueue.getCount(tid) == 0);
    assert(dispipe3.ldstQueue.getCount(tid) == 0);
    assert(commit.rob->isEmpty(tid));

    // Reset ROB/IQ/LSQ Entries

    // Commented out for now.  This should be possible to do by
    // telling all the pipeline stages to drain first, and then
    // checking until the drain completes.  Once the pipeline is
    // drained, call resetEntries(). - 10-09-06 ktlim
/*
    if (activeThreads.size() >= 1) {
        commit.rob->resetEntries();
        iew.resetEntries();
    }
*/
}

Fault
CPU::getInterrupts()
{
    // Check if there are any outstanding interrupts
    return interrupts[0]->getInterrupt();
}

void
CPU::processInterrupts(const Fault &interrupt)
{
    // Check for interrupts here.  For now can copy the code that
    // exists within isa_fullsys_traits.hh.  Also assume that thread 0
    // is the one that handles the interrupts.
    // @todo: Possibly consolidate the interrupt checking code.
    // @todo: Allow other threads to handle interrupts.

    assert(interrupt != NoFault);
    interrupts[0]->updateIntrInfo();

    DPRINTF(RxuO3CPU, "Interrupt %s being handled\n", interrupt->name());
    trap(interrupt, 0, nullptr);
}

void
CPU::trap(const Fault &fault, ThreadID tid, const StaticInstPtr &inst)
{
    // Pass the thread's TC into the invoke method.
    fault->invoke(threadContexts[tid], inst);
}

void
CPU::serializeThread(CheckpointOut &cp, ThreadID tid) const
{
    thread[tid]->serialize(cp);
}

void
CPU::unserializeThread(CheckpointIn &cp, ThreadID tid)
{
    thread[tid]->unserialize(cp);
}

DrainState
CPU::drain()
{
    // Deschedule any power gating event (if any)
    deschedulePowerGatingEvent();

    // If the CPU isn't doing anything, then return immediately.
    if (switchedOut())
        return DrainState::Drained;

    DPRINTF(Drain, "Draining...\n");

    // We only need to signal a drain to the commit stage as this
    // initiates squashing controls the draining. Once the commit
    // stage commits an instruction where it is safe to stop, it'll
    // squash the rest of the instructions in the pipeline and force
    // the fetch stage to stall. The pipeline will be drained once all
    // in-flight instructions have retired.
    commit.drain();

    // Wake the CPU and record activity so everything can drain out if
    // the CPU was not able to immediately drain.
    if (!isCpuDrained())  {
        // If a thread is suspended, wake it up so it can be drained
        for (auto t : threadContexts) {
            if (t->status() == gem5::ThreadContext::Suspended){
                DPRINTF(Drain, "Currently suspended so activate %i \n",
                        t->threadId());
                t->activate();
                // As the thread is now active, change the power state as well
                activateContext(t->threadId());
            }
        }

        wakeCPU();
        activityRec.activity();

        DPRINTF(Drain, "CPU not drained\n");

        return DrainState::Draining;
    } else {
        DPRINTF(Drain, "CPU is already drained\n");
        if (tickEvent.scheduled())
            deschedule(tickEvent);

        // Flush out any old data from the time buffers.  In
        // particular, there might be some data in flight from the
        // fetch stage that isn't visible in any of the CPU buffers we
        // test in isCpuDrained().
        for (int i = 0; i < timeBuffer.getSize(); ++i) {
            timeBuffer.advance();
            fetchQueue.advance();
            bpu0Queue.advance();
            bpu1Queue.advance();
            IBandLBQueue.advance();
            decodeQueue.advance();
            // -- add by hongfei.liu ------------
            predisqQueue.advance();
            dispipe0ToRobQueue.advance();
            dispipe0Queue.advance();
            dispipe1Queue.advance();
            dispipe2Queue.advance();
            wakeQueue.advance();
            // ----------------------------------
            // renameQueue.advance();
            ewQueue.advance();
        }

        drainSanityCheck();
        return DrainState::Drained;
    }
}

bool
CPU::tryDrain()
{
    if (drainState() != DrainState::Draining || !isCpuDrained())
        return false;

    if (tickEvent.scheduled())
        deschedule(tickEvent);

    DPRINTF(Drain, "CPU done draining, processing drain event\n");
    signalDrainDone();

    return true;
}

void
CPU::drainSanityCheck() const
{
    assert(isCpuDrained());
    fetch.drainSanityCheck();
    bpu0.drainSanityCheck();
    bpu1.drainSanityCheck();
    iBandLB.drainSanityCheck();
    decode.drainSanityCheck();
    // -- add by hongfei.liu ---------------------
    predisq.drainSanityCheck();
    dispipe0.drainSanityCheck();
    dispipe1.drainSanityCheck();
    dispipe2.drainSanityCheck();
    dispipe3.drainSanityCheck();
    // -------------------------------------------
    // rename.drainSanityCheck();
    ew.drainSanityCheck();
    commit.drainSanityCheck();
}

bool
CPU::isCpuDrained() const
{
    bool drained(true);

    if (!instList.empty() || !removeList.empty()) {
        DPRINTF(Drain, "Main CPU structures not drained.\n");
        drained = false;
    }

    if (!fetch.isDrained()) {
        DPRINTF(Drain, "Fetch not drained.\n");
        drained = false;
    }

    if (!bpu0.isDrained()) {
        DPRINTF(Drain, "Bpu0 not drained.\n");
        drained = false;
    }

    if (!bpu1.isDrained()) {
        DPRINTF(Drain, "Bpu1 not drained.\n");
        drained = false;
    }

    if (!iBandLB.isDrained()) {
        DPRINTF(Drain, "IBandLB not drained.\n");
        drained = false;
    }

    if (!decode.isDrained()) {
        DPRINTF(Drain, "Decode not drained.\n");
        drained = false;
    }

    // -- add by hongfei.liu ------------------
    if (!predisq.isDrained()) {
        DPRINTF(Drain, "Predisq not drained.\n");
        drained = false;
    }

    if (!dispipe0.isDrained()) {
        DPRINTF(Drain, "Dispipe0 not drained.\n");
        drained = false;
    }

    if (!dispipe1.isDrained()) {
        DPRINTF(Drain, "Dispipe1 not drained.\n");
        drained = false;
    }

    if (!dispipe2.isDrained()) {
        DPRINTF(Drain, "Dispipe2 not drained.\n");
        drained = false;
    }

    if (!dispipe3.isDrained()) {
        DPRINTF(Drain, "Dispipe3 not drained.\n");
        drained = false;
    }
    // ----------------------------------------

    // if (!rename.isDrained()) {
    //     DPRINTF(Drain, "Rename not drained.\n");
    //     drained = false;
    // }

    if (!ew.isDrained()) {
        DPRINTF(Drain, "EW not drained.\n");
        drained = false;
    }

    if (!commit.isDrained()) {
        DPRINTF(Drain, "Commit not drained.\n");
        drained = false;
    }

    return drained;
}

void CPU::commitDrained(ThreadID tid) { fetch.drainStall(tid); }

void
CPU::drainResume()
{
    if (switchedOut())
        return;

    DPRINTF(Drain, "Resuming...\n");
    verifyMemoryMode();

    fetch.drainResume();
    commit.drainResume();

    _status = Idle;
    for (ThreadID i = 0; i < thread.size(); i++) {
        if (thread[i]->status() == gem5::ThreadContext::Active) {
            DPRINTF(Drain, "Activating thread: %i\n", i);
            activateThread(i);
            _status = Running;
        }
    }

    assert(!tickEvent.scheduled());
    if (_status == Running)
        schedule(tickEvent, nextCycle());

    // Reschedule any power gating event (if any)
    schedulePowerGatingEvent();
}

void
CPU::switchOut()
{
    DPRINTF(RxuO3CPU, "Switching out\n");
    BaseCPU::switchOut();

    activityRec.reset();

    _status = SwitchedOut;

    if (checker)
        checker->switchOut();
}

void
CPU::takeOverFrom(BaseCPU *oldCPU)
{
    BaseCPU::takeOverFrom(oldCPU);

    fetch.takeOverFrom();
    bpu0.takeOverFrom();
    bpu1.takeOverFrom();
    iBandLB.takeOverFrom();
    decode.takeOverFrom();
    // -- add by hongfei.liu ---------------
    predisq.takeOverFrom();
    dispipe0.takeOverFrom();
    dispipe1.takeOverFrom();
    dispipe2.takeOverFrom();
    dispipe3.takeOverFrom();
    // -------------------------------------
    // rename.takeOverFrom();
    ew.takeOverFrom();
    commit.takeOverFrom();

    assert(!tickEvent.scheduled());

    auto *oldRxuO3CPU = dynamic_cast<CPU *>(oldCPU);
    if (oldRxuO3CPU)
        globalSeqNum = oldRxuO3CPU->globalSeqNum;

    lastRunningCycle = curCycle();
    _status = Idle;
}

void
CPU::verifyMemoryMode() const
{
    if (!system->isTimingMode()) {
        fatal("The RxuO3 CPU requires the memory system to be in "
              "'timing' mode.\n");
    }
}

RegVal
CPU::readMiscRegNoEffect(int misc_reg, ThreadID tid) const
{
    return isa[tid]->readMiscRegNoEffect(misc_reg);
}

RegVal
CPU::readMiscReg(int misc_reg, ThreadID tid)
{
    executeStats[tid]->numMiscRegReads++;
    return isa[tid]->readMiscReg(misc_reg);
}

void
CPU::setMiscRegNoEffect(int misc_reg, RegVal val, ThreadID tid)
{
    isa[tid]->setMiscRegNoEffect(misc_reg, val);
}

void
CPU::setMiscReg(int misc_reg, RegVal val, ThreadID tid)
{
    executeStats[tid]->numMiscRegWrites++;
    isa[tid]->setMiscReg(misc_reg, val);
}

__uint128_t
CPU::getVectorReg(PhysRegIdPtr phys_reg)
{
    return  regFile.getVectorReg(phys_reg);
}

RegVal
CPU::getReg(PhysRegIdPtr phys_reg, ThreadID tid)
{
    switch (phys_reg->classValue()) {
      case IntRegClass:
        executeStats[tid]->numIntRegReads++;
        break;
      case FloatRegClass:
        executeStats[tid]->numFpRegReads++;
        break;
      case CCRegClass:
        executeStats[tid]->numCCRegReads++;
        break;
      case VecRegClass:
      case VecElemClass:
        executeStats[tid]->numVecRegReads++;
        break;
      case VecPredRegClass:
        executeStats[tid]->numVecPredRegReads++;
        break;
      default:
        break;
    }
    return regFile.getReg(phys_reg);
}

void
CPU::getReg(PhysRegIdPtr phys_reg, void *val, ThreadID tid)
{
    switch (phys_reg->classValue()) {
      case IntRegClass:
        executeStats[tid]->numIntRegReads++;
        break;
      case FloatRegClass:
        executeStats[tid]->numFpRegReads++;
        break;
      case CCRegClass:
        executeStats[tid]->numCCRegReads++;
        break;
      case VecRegClass:
      case VecElemClass:
        executeStats[tid]->numVecRegReads++;
        break;
      case VecPredRegClass:
        executeStats[tid]->numVecPredRegReads++;
        break;
      default:
        break;
    }
    regFile.getReg(phys_reg, val);
}

void *
CPU::getWritableReg(PhysRegIdPtr phys_reg, ThreadID tid)
{
    switch (phys_reg->classValue()) {
      case VecRegClass:
        executeStats[tid]->numVecRegReads++;
        break;
      case VecPredRegClass:
        executeStats[tid]->numVecPredRegReads++;
        break;
      default:
        break;
    }
    return regFile.getWritableReg(phys_reg);
}

void
CPU::setReg(PhysRegIdPtr phys_reg, RegVal val, ThreadID tid)
{
    switch (phys_reg->classValue()) {
      case IntRegClass:
        executeStats[tid]->numIntRegWrites++;
        break;
      case FloatRegClass:
        executeStats[tid]->numFpRegWrites++;
        break;
      case CCRegClass:
        executeStats[tid]->numCCRegWrites++;
        break;
      case VecRegClass:
      case VecElemClass:
        executeStats[tid]->numVecRegWrites++;
        break;
      case VecPredRegClass:
        executeStats[tid]->numVecPredRegWrites++;
        break;
      default:
        break;
    }
    regFile.setReg(phys_reg, val);
}

void
CPU::setReg(PhysRegIdPtr phys_reg, const void *val, ThreadID tid)
{
    switch (phys_reg->classValue()) {
      case IntRegClass:
        executeStats[tid]->numIntRegWrites++;
        break;
      case FloatRegClass:
        executeStats[tid]->numFpRegWrites++;
        break;
      case CCRegClass:
        executeStats[tid]->numCCRegWrites++;
        break;
      case VecRegClass:
      case VecElemClass:
        executeStats[tid]->numVecRegWrites++;
        break;
      case VecPredRegClass:
        executeStats[tid]->numVecPredRegWrites++;
        break;
      default:
        break;
    }
    regFile.setReg(phys_reg, val);
}

RegVal
CPU::getArchReg(const RegId &reg, ThreadID tid)
{
    const RegId flat = reg.flatten(*isa[tid]);
    PhysRegIdPtr phys_reg = commitRenameMap[tid].lookup(flat);
    return regFile.getReg(phys_reg);
}

void
CPU::getArchReg(const RegId &reg, void *val, ThreadID tid)
{
    const RegId flat = reg.flatten(*isa[tid]);
    PhysRegIdPtr phys_reg = commitRenameMap[tid].lookup(flat);
    regFile.getReg(phys_reg, val);
}

void *
CPU::getWritableArchReg(const RegId &reg, ThreadID tid)
{
    const RegId flat = reg.flatten(*isa[tid]);
    PhysRegIdPtr phys_reg = commitRenameMap[tid].lookup(flat);
    return regFile.getWritableReg(phys_reg);
}

void
CPU::setArchReg(const RegId &reg, RegVal val, ThreadID tid)
{
    const RegId flat = reg.flatten(*isa[tid]);
    PhysRegIdPtr phys_reg = commitRenameMap[tid].lookup(flat);
    regFile.setReg(phys_reg, val);
}

void
CPU::setArchReg(const RegId &reg, const void *val, ThreadID tid)
{
    const RegId flat = reg.flatten(*isa[tid]);
    PhysRegIdPtr phys_reg = commitRenameMap[tid].lookup(flat);
    regFile.setReg(phys_reg, val);
}

const PCStateBase &
CPU::pcState(ThreadID tid)
{
    return commit.pcState(tid);
}

void
CPU::pcState(const PCStateBase &val, ThreadID tid)
{
    commit.pcState(val, tid);
}

void
CPU::squashFromTC(ThreadID tid)
{
    thread[tid]->noSquashFromTC = true;
    commit.generateTCEvent(tid);
}

CPU::ListIt
CPU::addInst(const DynInstPtr &inst)
{
    DPRINTF(RxuO3CPU, "Adding created instruction into instLIst [tid:%i] PC %s "
            "[sn:%lli]\n",
            inst->threadNumber, inst->pcState(), inst->seqNum);
    instList.push_back(inst);

    return --(instList.end());
}

CPU::ListIt
CPU::addMicroInst(const DynInstPtr &inst)
{
    DPRINTF(RxuO3CPU, "[tid:%i] [sn:%lli] PC %s Adding Vector Micro instruction into instLIst.\n",
            inst->threadNumber, inst->seqNum, inst->pcState());

    ListIt ret_it;

    for(auto rit = instList.rbegin(); rit != instList.rend(); ++rit) {
        if ((*rit)->seqNum <= inst->seqNum) {
            ListIt it = rit.base();
            ret_it = instList.insert(it, inst);
            return ret_it;
        }
    }

    ret_it = instList.insert(instList.begin(), inst);

    return ret_it;
}

void
CPU::instDone(ThreadID tid, const DynInstPtr &inst)
{
    // Keep an instruction count.
    if (!inst->isMicroop() || inst->isLastMicroop()) {
        thread[tid]->numInst++;
        thread[tid]->threadStats.numInsts++;
        commitStats[tid]->numInstsNotNOP++;

        if (this->nextDumpInstCount
                && totalInsts() == this->nextDumpInstCount) {
            fprintf(stderr, "Will trigger stat dump and reset\n");
            statistics::schedStatEvent(true, true, curTick(), 0);
            scheduleInstStop(tid,0,"Will trigger stat dump and reset");

            /*if (this->repeatDumpInstCount) {
                this->nextDumpInstCount += this->repeatDumpInstCount;
            };*/
        }

        // Check for instruction-count-based events.
        thread[tid]->comInstEventQueue.serviceEvents(thread[tid]->numInst);

        if (this->warmupInstCount && totalInsts() == this->warmupInstCount) {
            fprintf(stderr, "Will trigger stat dump and reset\n");
            statistics::schedStatEvent(true, true, curTick(), 0);
            scheduleInstStop(tid,0,"Will trigger stat dump and reset");
        }
    }

     
    thread[tid]->numOp++;
    thread[tid]->threadStats.numOps++;
    commitStats[tid]->numOpsNotNOP++;

    probeInstCommit(inst->staticInst, inst->pcState().instAddr());
}

void
CPU::removeFrontInst(const DynInstPtr &inst)
{
    DPRINTF(RxuO3CPU, "Removing committed/squashed instruction [tid:%i] PC %s "
            "[sn:%lli]\n",
            inst->threadNumber, inst->pcState(), inst->seqNum);

    removeInstsThisCycle = true;

    // Remove the front instruction.

    ListIt inst_iter = instList.begin();

    while (inst_iter != instList.end()) {
        // DPRINTF(RxuO3CPU, "Current iter instruction [tid:%i] PC %s "
        //         "[sn:%lli]\n",
        //         (*inst_iter)->threadNumber, (*inst_iter)->pcState(), (*inst_iter)->seqNum);
        if ((*inst_iter)->seqNum == inst->seqNum) {
            break;
        }
        // if (!(*inst_iter)->isSquashed() || (*inst_iter)->isInRemove()) {
        //     DPRINTF(RxuO3CPU, "Current iter instruction [tid:%i] PC %s "
        //             "[sn:%lli]\n",
        //             (*inst_iter)->threadNumber, (*inst_iter)->pcState(), (*inst_iter)->seqNum);
            // assert((*inst_iter)->isSquashed() || (*inst_iter)->isInRemove());
        // }
        // assert((*inst_iter)->isSquashed() || (*inst_iter)->isInRemove());

        if (!(*inst_iter)->isInRemove() &&!(*inst_iter)->macroMemRefIsErased)
            squashInstIt(inst_iter, (*inst_iter)->threadNumber);

        inst_iter++;
    }

    if (!inst->isInRemove()) {
        removeList.push(inst->getInstListIt());
        inst->setInRemove();
    }
}

void
CPU::removeInstsNotInROB(ThreadID tid)
{
    DPRINTF(RxuO3CPU, "Thread %i: Deleting instructions from instruction"
            " list.\n", tid);

    ListIt end_it;

    bool rob_empty = false;

    if (instList.empty()) {
        return;
    } else if (rob.isEmpty(tid)) {
        DPRINTF(RxuO3CPU, "ROB is empty, squashing all insts.\n");
        end_it = instList.begin();
        rob_empty = true;
    } else {
        end_it = (rob.readTailInst(tid))->getInstListIt();
        DPRINTF(RxuO3CPU, "ROB is not empty, squashing insts not in ROB.\n");
    }

    removeInstsThisCycle = true;

    ListIt inst_it = instList.end();

    inst_it--;

    // Walk through the instruction list, removing any instructions
    // that were inserted after the given instruction iterator, end_it.
    while (inst_it != end_it) {
        assert(!instList.empty());

        squashInstIt(inst_it, tid);

        inst_it--;
    }
    
    // If the ROB was empty, then we actually need to remove the first
    // instruction as well.
    if (rob_empty) {
        squashInstIt(inst_it, tid);
    }
}

void
CPU::removeInstsUntil(const InstSeqNum &seq_num, ThreadID tid)
{
    // if (instList.empty()) {
    //     return;
    // }
    assert(!instList.empty());

    removeInstsThisCycle = true;

    ListIt inst_iter = instList.end();

    inst_iter--;

    DPRINTF(RxuO3CPU, "Deleting instructions from instruction "
            "list that are from [tid:%i] and above [sn:%lli] (end=%lli).\n",
            tid, seq_num, (*inst_iter)->seqNum);

    while ((*inst_iter)->seqNum > seq_num) {

        bool break_loop = (inst_iter == instList.begin());

        squashInstIt(inst_iter, tid);

        inst_iter--;

        if (break_loop)
            break;
    }
}

void
CPU::removeInst(const InstSeqNum &seq_num, ThreadID tid)
{
    assert(!instList.empty());

    removeInstsThisCycle = true;

    ListIt inst_iter = instList.end();

    inst_iter--;

    DPRINTF(RxuO3CPU, "Deleting instruction [sn:%lli] from instruction "
            "list.\n", seq_num);

    while (true) {
        if ((*inst_iter)->seqNum == seq_num) {
            squashInstIt(inst_iter, tid);
            break;
        }

        if (inst_iter == instList.begin()) {
            break;
        }

        inst_iter--;
    }
}

void
CPU::removeInstsSquash(ThreadID tid)
{
    if (instList.empty()) {
        return;
    }

    removeInstsThisCycle = true;

    ListIt inst_iter = instList.end();

    inst_iter--;

    DPRINTF(RxuO3CPU, "Clear squashed insts.\n");

    while (true) {
        if ((*inst_iter)->isSquashed()) {
            squashInstIt(inst_iter, tid);
        }

        if (inst_iter == instList.begin()) {
            break;
        }

        inst_iter--;
    }
}

void
CPU::squashInstIt(const ListIt &instIt, ThreadID tid)
{
    if ((*instIt)->threadNumber == tid) {
        DPRINTF(RxuO3CPU, "Squashing instruction, "
                "[tid:%i] [sn:%lli] PC %s\n",
                (*instIt)->threadNumber,
                (*instIt)->seqNum,
                (*instIt)->pcState());
       
        // Mark it as squashed.
        (*instIt)->setSquashed();

        // @todo: Formulate a consistent method for deleting
        // instructions from the instruction list
        // Remove the instruction from the list.
        if (!(*instIt)->isInRemove()) {
            removeList.push(instIt);
        }

        (*instIt)->setInRemove();
    }
}

void
CPU::cleanUpRemovedInsts()
{
    while (!removeList.empty()) {
        DPRINTF(RxuO3CPU, "Removing instruction, "
                "[tid:%i] [sn:%lli] PC %s\n",
                (*removeList.front())->threadNumber,
                (*removeList.front())->seqNum,
                (*removeList.front())->pcState());
            
        if((*removeList.front())->isMemRef()){
          
            if((*removeList.front())->isSquashed()){
                if(((*removeList.front())->seqNum >= dispipe3.wtb.squashedSeqNum[0])){
                    for (int src_reg_idx = 0;
                                src_reg_idx < (*removeList.front())->numSrcRegs();
                                src_reg_idx++)
                        {
                            if((*removeList.front())->ifrenamesrc){
                                PhysRegIdPtr src_reg =
                                (*removeList.front())->renamedSrcIdx(src_reg_idx);
                                if (!(*removeList.front())->readySrcIdx(src_reg_idx)) {
                                    rmu.dependGraph.remove(src_reg->flatIndex(),
                                        (*removeList.front()));
                                }
                            }
                        }

                    for (int dest_reg_idx = 0;
                            dest_reg_idx < (*removeList.front())->numDestRegs();
                            dest_reg_idx++)
                        {   
                            if((*removeList.front())->ifrenamedest){
                                PhysRegIdPtr dest_reg =
                                    (*removeList.front())->renamedDestIdx(dest_reg_idx);
                                if (dest_reg->isFixedMapping()){
                                    continue;
                                }
                                if(rmu.dependGraph.dependGraph[dest_reg->flatIndex()].inst
                                    && (*removeList.front())->seqNum != rmu.dependGraph.dependGraph[dest_reg->flatIndex()].inst->seqNum){
                                    continue;
                                }
                                while(!rmu.dependGraph.empty(dest_reg->flatIndex())) {
                                    rmu.dependGraph.remove(dest_reg->flatIndex(),rmu.dependGraph.dependGraph[dest_reg->flatIndex()].next->inst);
                                // dispipe3Stage->rmu->dependGraph.dependGraph[dest_reg->flatIndex()].next = NULL;
                                }
                                assert(rmu.dependGraph.empty(dest_reg->flatIndex()));
                                rmu.dependGraph.clearInst(dest_reg->flatIndex());
                            }
                        }
                }
            }
        }
        instList.erase(removeList.front());
        removeList.pop();
    }

    removeInstsThisCycle = false;
}
/*
void
CPU::removeAllInsts()
{
    instList.clear();
}
*/
void
CPU::dumpInsts()
{
    int num = 0;

    ListIt inst_list_it = instList.begin();

    cprintf("Dumping Instruction List\n");

    while (inst_list_it != instList.end()) {
        cprintf("Instruction:%i\nPC:%#x\n[tid:%i]\n[sn:%lli]\nIssued:%i\n"
                "Squashed:%i\n\n",
                num, (*inst_list_it)->pcState().instAddr(),
                (*inst_list_it)->threadNumber,
                (*inst_list_it)->seqNum, (*inst_list_it)->isIssued(),
                (*inst_list_it)->isSquashed());
        inst_list_it++;
        ++num;
    }
}
/*
void
CPU::wakeDependents(const DynInstPtr &inst)
{
    iew.wakeDependents(inst);
}
*/
void
CPU::wakeCPU()
{
    if (activityRec.active() || tickEvent.scheduled()) {
        DPRINTF(RxuActivity, "CPU already running.\n");
        return;
    }

    DPRINTF(RxuActivity, "Waking up CPU\n");

    Cycles cycles(curCycle() - lastRunningCycle);
    // @todo: This is an oddity that is only here to match the stats
    if (cycles > 1) {
        --cycles;
        cpuStats.idleCycles += cycles;
        baseStats.numCycles += cycles;
        loopStats[loopIndex]->numCycles += cycles;
        if (loopIndex >= 1 && loopIndex <= 100) {
            baseStats.numCycles1_100loop += cycles;
        } else if (loopIndex >= 101 && loopIndex <= 200) {
            baseStats.numCycles101_200loop += cycles;
        } else if (loopIndex >= 201 && loopIndex <= 300) {
            baseStats.numCycles201_300loop += cycles;
        } else if (loopIndex >= 301 && loopIndex <= 400) {
            baseStats.numCycles301_400loop += cycles;
        } else if (loopIndex >= 401 && loopIndex <= 500) {
            baseStats.numCycles401_500loop += cycles;
        }
        // if (curTick() <= 6000000) {
        //     baseStats.numCycles1 += cycles;
        // }
        // if (curTick() > 6000000 && curTick() <= 12000000) {
        //     baseStats.numCycles2 += cycles;
        // }
    }

    if (cycles > 1) {
        --cycles;
        cpuStats.idleCycles += cycles;
        if (loopxhIndex >= 1 && loopxhIndex <= 110) {
            baseStats.xhnumCycles += cycles;
            loopStats[loopxhIndex]->xhnumCycles += cycles;
        }
        
        if (loopxhIndex >= 1 && loopxhIndex <= 110) {
            baseStats.xhnumCycles1_110loop += cycles;
        }
        if (loopxhIndex >= 1 && loopxhIndex <= 30) {
            baseStats.xhnumCycles1_30loop += cycles;
        }
        if (loopxhIndex >= 1 && loopxhIndex <= 64) {
            baseStats.xhnumCycles1_64loop += cycles;
        }
        if (loopxhIndex >= 1 && loopxhIndex <= 134) {
            baseStats.xhnumCycles1_134loop += cycles;
        }
        if (loopxhIndex >= 1 && loopxhIndex <= 1024) {
            baseStats.xhnumCycles1_1024loop += cycles;
        }
        if (loopxhIndex >= 50 && loopxhIndex <= 110) {
            baseStats.xhnumCycles50_110loop += cycles;
        }
        if (loopxhIndex >= 20 && loopxhIndex <= 30) {
            baseStats.xhnumCycles20_30loop += cycles;
        }
        if (loopxhIndex >= 30 && loopxhIndex <= 64) {
            baseStats.xhnumCycles30_64loop += cycles;
        }
        if (loopxhIndex >= 60 && loopxhIndex <= 134) {
            baseStats.xhnumCycles60_134loop += cycles;
        }
        if (loopxhIndex >= 500 && loopxhIndex <= 1024) {
            baseStats.xhnumCycles500_1024loop += cycles;
        }
        if (loopxhIndex >= 1000 && loopxhIndex <= 1024) {
            baseStats.xhnumCycles1000_1024loop += cycles;
        }
        if (curTick() <= 6000000) {
            baseStats.xhnumCycles1 += cycles;
        }
        if (curTick() > 6000000 && curTick() <= 12000000) {
            baseStats.xhnumCycles2 += cycles;
        }
    }

    if (cycles > 1) {
        --cycles;
        cpuStats.idleCycles += cycles;
        if (loopxhIndex >= 1 && loopxhIndex <= 110) {
            baseStats.xhnumCycles += cycles;
            loopStats[loopxhIndex]->xhnumCycles += cycles;
        }
        
        if (loopxhIndex >= 1 && loopxhIndex <= 110) {
            baseStats.xhnumCycles1_110loop += cycles;
        }
        if (loopxhIndex >= 1 && loopxhIndex <= 30) {
            baseStats.xhnumCycles1_30loop += cycles;
        }
        if (loopxhIndex >= 1 && loopxhIndex <= 64) {
            baseStats.xhnumCycles1_64loop += cycles;
        }
        if (loopxhIndex >= 1 && loopxhIndex <= 134) {
            baseStats.xhnumCycles1_134loop += cycles;
        }
        if (loopxhIndex >= 1 && loopxhIndex <= 1024) {
            baseStats.xhnumCycles1_1024loop += cycles;
        }
        if (loopxhIndex >= 50 && loopxhIndex <= 110) {
            baseStats.xhnumCycles50_110loop += cycles;
        }
        if (loopxhIndex >= 20 && loopxhIndex <= 30) {
            baseStats.xhnumCycles20_30loop += cycles;
        }
        if (loopxhIndex >= 30 && loopxhIndex <= 64) {
            baseStats.xhnumCycles30_64loop += cycles;
        }
        if (loopxhIndex >= 60 && loopxhIndex <= 134) {
            baseStats.xhnumCycles60_134loop += cycles;
        }
        if (loopxhIndex >= 500 && loopxhIndex <= 1024) {
            baseStats.xhnumCycles500_1024loop += cycles;
        }
        if (loopxhIndex >= 1000 && loopxhIndex <= 1024) {
            baseStats.xhnumCycles1000_1024loop += cycles;
        }
        if (curTick() <= 6000000) {
            baseStats.xhnumCycles1 += cycles;
        }
        if (curTick() > 6000000 && curTick() <= 12000000) {
            baseStats.xhnumCycles2 += cycles;
        }
    }

    schedule(tickEvent, clockEdge());
}

void
CPU::wakeup(ThreadID tid)
{
    if (thread[tid]->status() != gem5::ThreadContext::Suspended)
        return;

    wakeCPU();

    DPRINTF(Quiesce, "Suspended Processor woken\n");
    threadContexts[tid]->activate();
}

ThreadID
CPU::getFreeTid()
{
    for (ThreadID tid = 0; tid < numThreads; tid++) {
        if (!tids[tid]) {
            tids[tid] = true;
            return tid;
        }
    }

    return InvalidThreadID;
}

bool
CPU::isOldestInstInPipe(const DynInstPtr &inst)
{
    ListIt it = std::find(instList.begin(), instList.end(), inst);

    assert(it != instList.end());

    for (ListIt prev_it = instList.begin(); prev_it != it; ++prev_it) {
        if (!(*prev_it)->readyToCommit() && !(*prev_it)->isSquashed()) {
            return false;
        }
    }

    return true;
}

void
CPU::updateThreadPriority()
{
    if (activeThreads.size() > 1) {
        //DEFAULT TO ROUND ROBIN SCHEME
        //e.g. Move highest priority to end of thread list
        std::list<ThreadID>::iterator list_begin = activeThreads.begin();

        unsigned high_thread = *list_begin;

        activeThreads.erase(list_begin);

        activeThreads.push_back(high_thread);
    }
}

void
CPU::addThreadToExitingList(ThreadID tid)
{
    DPRINTF(RxuO3CPU, "Thread %d is inserted to exitingThreads list\n", tid);

    // the thread trying to exit can't be already halted
    assert(tcBase(tid)->status() != gem5::ThreadContext::Halted);

    // make sure the thread has not been added to the list yet
    assert(exitingThreads.count(tid) == 0);

    // add the thread to exitingThreads list to mark that this thread is
    // trying to exit. The boolean value in the pair denotes if a thread is
    // ready to exit. The thread is not ready to exit until the corresponding
    // exit trap event is processed in the future. Until then, it'll be still
    // an active thread that is trying to exit.
    exitingThreads.emplace(std::make_pair(tid, false));
}

bool
CPU::isThreadExiting(ThreadID tid) const
{
    return exitingThreads.count(tid) == 1;
}

void
CPU::scheduleThreadExitEvent(ThreadID tid)
{
    assert(exitingThreads.count(tid) == 1);

    // exit trap event has been processed. Now, the thread is ready to exit
    // and be removed from the CPU.
    exitingThreads[tid] = true;

    // we schedule a threadExitEvent in the next cycle to properly clean
    // up the thread's states in the pipeline. threadExitEvent has lower
    // priority than tickEvent, so the cleanup will happen at the very end
    // of the next cycle after all pipeline stages complete their operations.
    // We want all stages to complete squashing instructions before doing
    // the cleanup.
    if (!threadExitEvent.scheduled()) {
        schedule(threadExitEvent, nextCycle());
    }
}

void
CPU::exitThreads()
{
    // there must be at least one thread trying to exit
    assert(exitingThreads.size() > 0);

    // terminate all threads that are ready to exit
    auto it = exitingThreads.begin();
    while (it != exitingThreads.end()) {
        ThreadID thread_id = it->first;
        bool readyToExit = it->second;

        if (readyToExit) {
            DPRINTF(RxuO3CPU, "Exiting thread %d\n", thread_id);
            haltContext(thread_id);
            tcBase(thread_id)->setStatus(gem5::ThreadContext::Halted);
            it = exitingThreads.erase(it);
        } else {
            it++;
        }
    }
}

void
CPU::htmSendAbortSignal(ThreadID tid, uint64_t htm_uid,
        HtmFailureFaultCause cause)
{
    const Addr addr = 0x0ul;
    const int size = 8;
    const Request::Flags flags =
      Request::PHYSICAL|Request::STRICT_ORDER|Request::HTM_ABORT;

    // RxuO3-specific actions
    dispipe3.ldstQueue.resetHtmStartsStops(tid);
    commit.resetHtmStartsStops(tid);

    // notify l1 d-cache (ruby) that core has aborted transaction
    RequestPtr req =
        std::make_shared<Request>(addr, size, flags, _dataRequestorId);

    req->taskId(taskId());
    req->setContext(thread[tid]->contextId());
    req->setHtmAbortCause(cause);

    assert(req->isHTMAbort());

    PacketPtr abort_pkt = Packet::createRead(req);
    uint8_t *memData = new uint8_t[8];
    assert(memData);
    abort_pkt->dataStatic(memData);
    abort_pkt->setHtmTransactional(htm_uid);

    // TODO include correct error handling here
    if (!dispipe3.ldstQueue.getDataPort().sendTimingReq(abort_pkt)) {
        panic("HTM abort signal was not sent to the memory subsystem.");
    }
}

uint64_t
CPU::extractBits(uint64_t value, int start, int numBits)
{
    
    if (start < 0 || numBits < 0 || start + numBits > 64) {
        std::cerr << "Invalid parameters for bit extraction." << std::endl;
        return 0;
    }

    uint64_t mask = (1ULL << numBits) - 1;

    mask <<= start;

    uint64_t result = (value & mask) >> start;

    return result;    
}

} // namespace rxuo3
} // namespace gem5
