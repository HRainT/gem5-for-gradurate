/*
 * Copyright (c) 2011-2012,2016-2017, 2019-2020 Arm Limited
 * All rights reserved
 *
 * The license below extends only to copyright in the software and shall
 * not be construed as granting a license to any other intellectual
 * property including but not limited to intellectual property relating
 * to a hardware implementation of the functionality of the software
 * licensed hereunder.  You may use the software subject to the license
 * terms below provided that you ensure that this notice is replicated
 * unmodified and in its entirety in all distributions of the software,
 * modified or unmodified, in source code or in binary form.
 *
 * Copyright (c) 2002-2005 The Regents of The University of Michigan
 * Copyright (c) 2011 Regents of the University of California
 * Copyright (c) 2013 Advanced Micro Devices, Inc.
 * Copyright (c) 2013 Mark D. Hill and David A. Wood
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "cpu/base.hh"

#include <iostream>
#include <sstream>
#include <string>

#include "arch/generic/decoder.hh"
#include "arch/generic/isa.hh"
#include "arch/generic/tlb.hh"
// #include "arch/riscv/regs/misc.hh"
// #include "arch/riscv/insts/static_inst.hh"
#include "base/cprintf.hh"
#include "base/loader/symtab.hh"
#include "base/logging.hh"
#include "base/output.hh"
#include "base/trace.hh"
#include "cpu/checker/cpu.hh"
#include "cpu/thread_context.hh"
#include "debug/Mwait.hh"
#include "debug/SyscallVerbose.hh"
#include "debug/Thread.hh"
// #include "debug/DumpCommit.hh"
// #include "debug/Diff.hh"
// #include "debug/Diff2.hh"
#include "mem/page_table.hh"
#include "params/BaseCPU.hh"
#include "sim/clocked_object.hh"
#include "sim/full_system.hh"
#include "sim/process.hh"
#include "sim/root.hh"
#include "sim/sim_events.hh"
#include "sim/sim_exit.hh"
#include "sim/system.hh"

// Hack
#include "sim/stat_control.hh"

namespace gem5
{

std::unique_ptr<BaseCPU::GlobalStats> BaseCPU::globalStats;

std::vector<BaseCPU *> BaseCPU::cpuList;

// This variable reflects the max number of threads in any CPU.  Be
// careful to only use it once all the CPUs that you care about have
// been initialized
int maxThreadsPerCPU = 1;

CPUProgressEvent::CPUProgressEvent(BaseCPU *_cpu, Tick ival)
    : Event(Event::Progress_Event_Pri), _interval(ival), lastNumInst(0),
      cpu(_cpu), _repeatEvent(true)
{
    if (_interval)
        cpu->schedule(this, curTick() + _interval);
}

void
CPUProgressEvent::process()
{
    Counter temp = cpu->totalOps();

    if (_repeatEvent)
        cpu->schedule(this, curTick() + _interval);

    if (cpu->switchedOut()) {
        return;
    }

#ifndef NDEBUG
    double ipc = double(temp - lastNumInst) / (_interval / cpu->clockPeriod());

    DPRINTFN("%s progress event, total committed:%i, progress insts committed: "
             "%lli, IPC: %0.8d\n", cpu->name(), temp, temp - lastNumInst,
             ipc);
    ipc = 0.0;
#else
    cprintf("%lli: %s progress event, total committed:%i, progress insts "
            "committed: %lli\n", curTick(), cpu->name(), temp,
            temp - lastNumInst);
#endif
    lastNumInst = temp;
}

const char *
CPUProgressEvent::description() const
{
    return "CPU Progress";
}

BaseCPU::BaseCPU(const Params &p, bool is_checker)
    : ClockedObject(p), instCnt(0), _cpuId(p.cpu_id), _socketId(p.socket_id),
      _instRequestorId(p.system->getRequestorId(this, "inst")),
      _dataRequestorId(p.system->getRequestorId(this, "data")),
      _taskId(context_switch_task_id::Unknown), _pid(invldPid),
      _switchedOut(p.switched_out), _cacheLineSize(p.system->cacheLineSize()),
      modelResetPort(p.name + ".model_reset"),
      interrupts(p.interrupts), numThreads(p.numThreads), system(p.system),
      previousCycle(0), previousState(CPU_STATE_SLEEP),
      functionTraceStream(nullptr), currentFunctionStart(0),
      currentFunctionEnd(0), functionEntryTick(0),
      baseStats(this),
      addressMonitor(p.numThreads),
      syscallRetryLatency(p.syscallRetryLatency),
      pwrGatingLatency(p.pwr_gating_latency),
      powerGatingOnIdle(p.power_gating_on_idle),
      enterPwrGatingEvent([this]{ enterPwrGating(); }, name()),
      warmupInstCount(p.warmupInstCount),
      enableDifftest(p.enable_difftest),
      dumpCommitFlag(p.dump_commit),
      dumpStartNum(p.dump_start)
{
    // if Python did not provide a valid ID, do it here
    if (_cpuId == -1 ) {
        _cpuId = cpuList.size();
    }

    // add self to global list of CPUs
    cpuList.push_back(this);

    DPRINTF(SyscallVerbose, "Constructing CPU with id %d, socket id %d\n",
            _cpuId, _socketId);

    if (numThreads > maxThreadsPerCPU)
        maxThreadsPerCPU = numThreads;

    functionTracingEnabled = false;
    if (p.function_trace) {
        const std::string fname = csprintf("ftrace.%s", name());
        functionTraceStream = simout.findOrCreate(fname)->stream();

        currentFunctionStart = currentFunctionEnd = 0;
        functionEntryTick = p.function_trace_start;

        if (p.function_trace_start == 0) {
            functionTracingEnabled = true;
        } else {
            Event *event = new EventFunctionWrapper(
                [this]{ enableFunctionTrace(); }, name(), true);
            schedule(event, p.function_trace_start);
        }
    }

    tracer = params().tracer;

    // diffAllStates = std::make_shared<DiffAllStates>();
    // if (enableDifftest) {
    //     assert(params().difftest_ref_so.length() > 2);
    //     diffAllStates->diff.nemu_reg = diffAllStates->referenceRegFile;
    //     diffAllStates->diff.nemu_this_pc = 0x80000000u;
    //     diffAllStates->diff.cpu_id = params().cpu_id;
    //     warn("cpu_id set to %d\n", params().cpu_id);
    //     diffAllStates->proxy = new NemuProxy(
    //         params().cpu_id, params().difftest_ref_so.c_str(),
    //         params().nemuSDimg.size() && params().nemuSDCptBin.size());
    //     warn("Difftest is enabled with ref so: %s.\n",
    //          params().difftest_ref_so.c_str());
    //     diffAllStates->proxy->regcpy(diffAllStates->gem5RegFile, REF_TO_DUT);
    //     diffAllStates->diff.dynamic_config.ignore_illegal_mem_access = false;
    //     diffAllStates->diff.dynamic_config.debug_difftest = false;
    //     diffAllStates->proxy->update_config(&diffAllStates->diff.dynamic_config);
    //     if (params().nemuSDimg.size() && params().nemuSDCptBin.size()) {
    //         diffAllStates->proxy->sdcard_init(params().nemuSDimg.c_str(),
    //                            params().nemuSDCptBin.c_str());
    //     }
    //     diffAllStates->diff.will_handle_intr = false;
    // } else {
    //     warn("Difftest is disabled\n");
    //     diffAllStates->hasCommit = true;
    // }

    // if (dumpCommitFlag) {
    //     registerExitCallback([this]() {
    //         auto out_handle = simout.create("dumpCommit.txt", false, true);
    //         for (auto iter : committedInsts) {
    //             *out_handle->stream() << std::hex << iter.first << " " << iter.second << std::endl;
    //         }
    //         simout.close(out_handle);
    //     });
    // }

    if (params().isa.size() != numThreads) {
        fatal("Number of ISAs (%i) assigned to the CPU does not equal number "
              "of threads (%i).\n", params().isa.size(), numThreads);
    }

    if (!FullSystem && params().workload.size() != numThreads) {
        fatal("Number of processes (cpu.workload) (%i) assigned to the CPU "
              "does not equal number of threads (%i).\n",
              params().workload.size(), numThreads);
    }

    modelResetPort.onChange([this](const bool &new_val) {
        setReset(new_val);
    });
    // create a stat group object for each thread on this core
    fetchStats.reserve(numThreads);
    executeStats.reserve(numThreads);
    commitStats.reserve(numThreads);
    for (int i = 0; i < numThreads; i++) {
        // create fetchStat object for thread i and set rate formulas
        FetchCPUStats* fetchStatptr = new FetchCPUStats(this, i);
        fetchStatptr->fetchRate = fetchStatptr->numInsts / baseStats.numCycles;
        fetchStatptr->branchRate = fetchStatptr->numBranches /
            baseStats.numCycles;
        fetchStats.emplace_back(fetchStatptr);

        // create executeStat object for thread i and set rate formulas
        ExecuteCPUStats* executeStatptr = new ExecuteCPUStats(this, i);
        executeStatptr->instRate = executeStatptr->numInsts /
            baseStats.numCycles;
        executeStats.emplace_back(executeStatptr);

        // create commitStat object for thread i and set ipc, cpi formulas
        CommitCPUStats* commitStatptr = new CommitCPUStats(this, i);
        commitStatptr->ipc = commitStatptr->numInsts / baseStats.numCycles;
        commitStatptr->cpi = baseStats.numCycles / commitStatptr->numInsts;
        commitStats.emplace_back(commitStatptr);
    }

    for (int i = 0; i < 5002; i++) {
        LoopCPUStats* loopStatptr = new LoopCPUStats(this, i);
        loopStatptr->recoverRate = loopStatptr->commitSquashedInsts / (loopStatptr->commitSquashedInsts + loopStatptr->commitRetiredInsts);
        loopStatptr->cpi = loopStatptr->numCycles / loopStatptr->commitRetiredInsts;
        loopStatptr->ipc = loopStatptr->commitRetiredInsts / loopStatptr->numCycles;
        loopStatptr->bpu0MissRate = (loopStatptr->bpu0MissCommitCount + loopStatptr->bpu0MissDecodeCount) / loopStatptr->retiredBranchInsts;
        loopStatptr->bpu1MissRate = (loopStatptr->bpu1MissCommitCount + loopStatptr->bpu1MissDecodeCount) / loopStatptr->retiredBranchInsts;
        loopStats.emplace_back(loopStatptr);
        loopStatptr->xhrecoverRate = loopStatptr->xhcommitSquashedInsts / (loopStatptr->xhcommitSquashedInsts + loopStatptr->xhcommitRetiredInsts);
        loopStatptr->xhcpi = loopStatptr->xhnumCycles / loopStatptr->xhcommitRetiredInsts;
        loopStatptr->xhipc = loopStatptr->xhcommitRetiredInsts / loopStatptr->xhnumCycles;
        loopStatptr->xhbpuMissRate = (loopStatptr->xhbpuMissCommitCount + loopStatptr->xhbpuMissDecodeCount) / loopStatptr->xhretiredBranchInsts;
    }
}

void
BaseCPU::enableFunctionTrace()
{
    functionTracingEnabled = true;
}

BaseCPU::~BaseCPU()
{
}

void
BaseCPU::postInterrupt(ThreadID tid, int int_num, int index)
{
    interrupts[tid]->post(int_num, index);
    // Only wake up syscall emulation if it is not waiting on a futex.
    // This is to model the fact that instructions such as ARM SEV
    // should wake up a WFE sleep, but not a futex syscall WAIT.
    if (FullSystem || !system->futexMap.is_waiting(threadContexts[tid]))
        wakeup(tid);
}

void
BaseCPU::armMonitor(ThreadID tid, Addr address)
{
    assert(tid < numThreads);
    AddressMonitor &monitor = addressMonitor[tid];

    monitor.armed = true;
    monitor.vAddr = address;
    monitor.pAddr = 0x0;
    DPRINTF(Mwait, "[tid:%d] Armed monitor (vAddr=0x%lx)\n", tid, address);
}

bool
BaseCPU::mwait(ThreadID tid, PacketPtr pkt)
{
    assert(tid < numThreads);
    AddressMonitor &monitor = addressMonitor[tid];

    if (!monitor.gotWakeup) {
        Addr block_size = cacheLineSize();
        Addr mask = ~(block_size - 1);

        assert(pkt->req->hasPaddr());
        monitor.pAddr = pkt->getAddr() & mask;
        monitor.waiting = true;

        DPRINTF(Mwait, "[tid:%d] mwait called (vAddr=0x%lx, "
                "line's paddr=0x%lx)\n", tid, monitor.vAddr, monitor.pAddr);
        return true;
    } else {
        monitor.gotWakeup = false;
        return false;
    }
}

void
BaseCPU::mwaitAtomic(ThreadID tid, ThreadContext *tc, BaseMMU *mmu)
{
    assert(tid < numThreads);
    AddressMonitor &monitor = addressMonitor[tid];

    RequestPtr req = std::make_shared<Request>();

    Addr addr = monitor.vAddr;
    Addr block_size = cacheLineSize();
    Addr mask = ~(block_size - 1);
    int size = block_size;

    //The address of the next line if it crosses a cache line boundary.
    Addr secondAddr = roundDown(addr + size - 1, block_size);

    if (secondAddr > addr)
        size = secondAddr - addr;

    req->setVirt(addr, size, 0x0, dataRequestorId(),
            tc->pcState().instAddr());

    // translate to physical address
    Fault fault = mmu->translateAtomic(req, tc, BaseMMU::Read);
    assert(fault == NoFault);

    monitor.pAddr = req->getPaddr() & mask;
    monitor.waiting = true;

    DPRINTF(Mwait, "[tid:%d] mwait called (vAddr=0x%lx, line's paddr=0x%lx)\n",
            tid, monitor.vAddr, monitor.pAddr);
}

void
BaseCPU::init()
{
    // Set up instruction-count-based termination events, if any. This needs
    // to happen after threadContexts has been constructed.
    if (params().max_insts_any_thread != 0) {
        scheduleInstStopAnyThread(params().max_insts_any_thread);
    }

    // Set up instruction-count-based termination events for SimPoints
    // Typically, there are more than one action points.
    // Simulation.py is responsible to take the necessary actions upon
    // exitting the simulation loop.
    if (!params().simpoint_start_insts.empty()) {
        scheduleSimpointsInstStop(params().simpoint_start_insts);
    }

    if (params().max_insts_all_threads != 0) {
        std::string cause = "all threads reached the max instruction count";

        // allocate & initialize shared downcounter: each event will
        // decrement this when triggered; simulation will terminate
        // when counter reaches 0
        int *counter = new int;
        *counter = numThreads;
        for (ThreadID tid = 0; tid < numThreads; ++tid) {
            Event *event = new CountedExitEvent(cause, *counter);
            threadContexts[tid]->scheduleInstCountEvent(
                    event, params().max_insts_all_threads);
        }
    }

    if (!params().switched_out) {
        registerThreadContexts();

        verifyMemoryMode();
    }
}

void
BaseCPU::startup()
{
    if (params().progress_interval) {
        new CPUProgressEvent(this, params().progress_interval);
    }

    if (_switchedOut)
        powerState->set(enums::PwrState::OFF);

    // Assumption CPU start to operate instantaneously without any latency
    if (powerState->get() == enums::PwrState::UNDEFINED)
        powerState->set(enums::PwrState::ON);

}

probing::PMUUPtr
BaseCPU::pmuProbePoint(const char *name)
{
    probing::PMUUPtr ptr;
    ptr.reset(new probing::PMU(getProbeManager(), name));

    return ptr;
}

void
BaseCPU::regProbePoints()
{
    ppAllCycles = pmuProbePoint("Cycles");
    ppActiveCycles = pmuProbePoint("ActiveCycles");

    ppRetiredInsts = pmuProbePoint("RetiredInsts");
    ppRetiredInstsPC = pmuProbePoint("RetiredInstsPC");
    ppRetiredLoads = pmuProbePoint("RetiredLoads");
    ppRetiredStores = pmuProbePoint("RetiredStores");
    ppRetiredBranches = pmuProbePoint("RetiredBranches");

    ppSleeping = new ProbePointArg<bool>(this->getProbeManager(),
                                         "Sleeping");
}

void
BaseCPU::probeInstCommit(const StaticInstPtr &inst, Addr pc)
{
    if (!inst->isMicroop() || inst->isLastMicroop()) {
        ppRetiredInsts->notify(1);
        ppRetiredInstsPC->notify(pc);
    }

    if (inst->isLoad())
        ppRetiredLoads->notify(1);

    if (inst->isStore() || inst->isAtomic())
        ppRetiredStores->notify(1);

    if (inst->isControl())
        ppRetiredBranches->notify(1);
}

BaseCPU::
BaseCPUStats::BaseCPUStats(statistics::Group *parent)
    : statistics::Group(parent),
      ADD_STAT(numCycles, statistics::units::Cycle::get(),
               "Number of cpu cycles simulated"),
      ADD_STAT(cpi, statistics::units::Rate<
                statistics::units::Cycle, statistics::units::Count>::get(),
               "CPI: cycles per instruction (core level)"),
      ADD_STAT(ipc, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "IPC: instructions per cycle (core level)"),
// --------------- add by hongfei.liu -------------------------------------------
      ADD_STAT(numInsts1_100loop, statistics::units::Count::get(),
               "Number of insts retired in 1 - 100 loop"),
      ADD_STAT(numInsts101_200loop, statistics::units::Count::get(),
               "Number of insts retired in 101 - 200 loop"),
      ADD_STAT(numInsts201_300loop, statistics::units::Count::get(),
               "Number of insts retired in 201 - 300 loop"),
      ADD_STAT(numInsts301_400loop, statistics::units::Count::get(),
               "Number of insts retired in 301 - 400 loop"),
      ADD_STAT(numInsts401_500loop, statistics::units::Count::get(),
               "Number of insts retired in 401 - 500 loop"),
      ADD_STAT(numInsts501_600loop, statistics::units::Count::get(),
               "Number of insts retired in 501 - 600 loop"),
      ADD_STAT(numInsts601_700loop, statistics::units::Count::get(),
               "Number of insts retired in 601 - 700 loop"),
      ADD_STAT(numInsts701_800loop, statistics::units::Count::get(),
               "Number of insts retired in 701 - 800 loop"),
      ADD_STAT(numInsts801_900loop, statistics::units::Count::get(),
               "Number of insts retired in 801 - 900 loop"),
      ADD_STAT(numInsts901_1000loop, statistics::units::Count::get(),
               "Number of insts retired in 901 - 1000 loop"),
      ADD_STAT(numInsts1001_1100loop, statistics::units::Count::get(),
               "Number of insts retired in 1001 - 1100 loop"),
      ADD_STAT(numInsts1101_1200loop, statistics::units::Count::get(),
               "Number of insts retired in 1101 - 1200 loop"),
      ADD_STAT(numInsts1201_1300loop, statistics::units::Count::get(),
               "Number of insts retired in 1201 - 1300 loop"),
      ADD_STAT(numInsts1301_1400loop, statistics::units::Count::get(),
               "Number of insts retired in 1301 - 1400 loop"),
      ADD_STAT(numInsts1401_1500loop, statistics::units::Count::get(),
               "Number of insts retired in 1401 - 1500 loop"),
      ADD_STAT(numInsts1501_1600loop, statistics::units::Count::get(),
               "Number of insts retired in 1501 - 1600 loop"),
      ADD_STAT(numInsts1601_1700loop, statistics::units::Count::get(),
               "Number of insts retired in 1601 - 1700 loop"),
      ADD_STAT(numInsts1701_1800loop, statistics::units::Count::get(),
               "Number of insts retired in 1701 - 1800 loop"),
      ADD_STAT(numInsts1801_1900loop, statistics::units::Count::get(),
               "Number of insts retired in 1801 - 1900 loop"),
      ADD_STAT(numInsts1901_2000loop, statistics::units::Count::get(),
               "Number of insts retired in 1901 - 2000 loop"),
      ADD_STAT(numInsts1_500loop, statistics::units::Count::get(),
               "Number of insts retired in 1 - 500 loop"),
      ADD_STAT(numInsts501_1000loop, statistics::units::Count::get(),
               "Number of insts retired in 501 - 1000 loop"),
      ADD_STAT(numInsts1001_1500loop, statistics::units::Count::get(),
               "Number of insts retired in 1001 - 1500 loop"),
      ADD_STAT(numInsts1501_2000loop, statistics::units::Count::get(),
               "Number of insts retired in 1501 - 2000 loop"),

      ADD_STAT(numCycles1_100loop, statistics::units::Cycle::get(),
               "Number of cpu cycles simulated in 1 - 100 loop"),
      ADD_STAT(numCycles101_200loop, statistics::units::Cycle::get(),
               "Number of cpu cycles simulated in 101 - 200 loop"),
      ADD_STAT(numCycles201_300loop, statistics::units::Cycle::get(),
               "Number of cpu cycles simulated in 201 - 300 loop"),
      ADD_STAT(numCycles301_400loop, statistics::units::Cycle::get(),
               "Number of cpu cycles simulated in 301 - 400 loop"),
      ADD_STAT(numCycles401_500loop, statistics::units::Cycle::get(),
               "Number of cpu cycles simulated in 401 - 500 loop"),
      ADD_STAT(numCycles501_600loop, statistics::units::Cycle::get(),
               "Number of cpu cycles simulated in 501 - 600 loop"),
      ADD_STAT(numCycles601_700loop, statistics::units::Cycle::get(),
               "Number of cpu cycles simulated in 601 - 700 loop"),
      ADD_STAT(numCycles701_800loop, statistics::units::Cycle::get(),
               "Number of cpu cycles simulated in 701 - 800 loop"),
      ADD_STAT(numCycles801_900loop, statistics::units::Cycle::get(),
               "Number of cpu cycles simulated in 801 - 900 loop"),
      ADD_STAT(numCycles901_1000loop, statistics::units::Cycle::get(),
               "Number of cpu cycles simulated in 901 - 1000 loop"),
      ADD_STAT(numCycles1001_1100loop, statistics::units::Cycle::get(),
               "Number of cpu cycles simulated in 1001 - 1100 loop"),
      ADD_STAT(numCycles1101_1200loop, statistics::units::Cycle::get(),
               "Number of cpu cycles simulated in 1101 - 1200 loop"),
      ADD_STAT(numCycles1201_1300loop, statistics::units::Cycle::get(),
               "Number of cpu cycles simulated in 1201 - 1300 loop"),
      ADD_STAT(numCycles1301_1400loop, statistics::units::Cycle::get(),
               "Number of cpu cycles simulated in 1301 - 1400 loop"),
      ADD_STAT(numCycles1401_1500loop, statistics::units::Cycle::get(),
               "Number of cpu cycles simulated in 1401 - 1500 loop"),
      ADD_STAT(numCycles1501_1600loop, statistics::units::Cycle::get(),
               "Number of cpu cycles simulated in 1501 - 1600 loop"),
      ADD_STAT(numCycles1601_1700loop, statistics::units::Cycle::get(),
               "Number of cpu cycles simulated in 1601 - 1700 loop"),
      ADD_STAT(numCycles1701_1800loop, statistics::units::Cycle::get(),
               "Number of cpu cycles simulated in 1701 - 1800 loop"),
      ADD_STAT(numCycles1801_1900loop, statistics::units::Cycle::get(),
               "Number of cpu cycles simulated in 1801 - 1900 loop"),
      ADD_STAT(numCycles1901_2000loop, statistics::units::Cycle::get(),
               "Number of cpu cycles simulated in 1901 - 2000 loop"),
      ADD_STAT(numCycles1_500loop, statistics::units::Cycle::get(),
               "Number of cpu cycles simulated in 1 - 500 loop"),
      ADD_STAT(numCycles501_1000loop, statistics::units::Cycle::get(),
               "Number of cpu cycles simulated in 501 - 1000 loop"),
      ADD_STAT(numCycles1001_1500loop, statistics::units::Cycle::get(),
               "Number of cpu cycles simulated in 1001 - 1500 loop"),
      ADD_STAT(numCycles1501_2000loop, statistics::units::Cycle::get(),
               "Number of cpu cycles simulated in 1501 - 2000 loop"),

      ADD_STAT(ipc1_100loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "IPC: instructions per cycle (1 - 100 loop average)"),
      ADD_STAT(ipc101_200loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "IPC: instructions per cycle (101 - 200 loop average)"),
      ADD_STAT(ipc201_300loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "IPC: instructions per cycle (201 - 300 loop average)"),
      ADD_STAT(ipc301_400loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "IPC: instructions per cycle (301 - 400 loop average)"),
      ADD_STAT(ipc401_500loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "IPC: instructions per cycle (401 - 500 loop average)"),
      ADD_STAT(ipc501_600loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "IPC: instructions per cycle (501 - 600 loop average)"),
      ADD_STAT(ipc601_700loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "IPC: instructions per cycle (601 - 600 loop average)"),
      ADD_STAT(ipc701_800loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "IPC: instructions per cycle (701 - 800 loop average)"),
      ADD_STAT(ipc801_900loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "IPC: instructions per cycle (801 - 900 loop average)"),
      ADD_STAT(ipc901_1000loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "IPC: instructions per cycle (901 - 1000 loop average)"),
      ADD_STAT(ipc1001_1100loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "IPC: instructions per cycle (1001 - 1100 loop average)"),
      ADD_STAT(ipc1101_1200loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "IPC: instructions per cycle (1101 - 1200 loop average)"),
      ADD_STAT(ipc1201_1300loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "IPC: instructions per cycle (1201 - 1300 loop average)"),
      ADD_STAT(ipc1301_1400loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "IPC: instructions per cycle (1301 - 1400 loop average)"),
      ADD_STAT(ipc1401_1500loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "IPC: instructions per cycle (1401 - 1500 loop average)"),
      ADD_STAT(ipc1501_1600loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "IPC: instructions per cycle (1501 - 1600 loop average)"),
      ADD_STAT(ipc1601_1700loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "IPC: instructions per cycle (1601 - 1700 loop average)"),
      ADD_STAT(ipc1701_1800loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "IPC: instructions per cycle (1701 - 1800 loop average)"),
      ADD_STAT(ipc1801_1900loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "IPC: instructions per cycle (1801 - 1900 loop average)"),
      ADD_STAT(ipc1901_2000loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "IPC: instructions per cycle (1901 - 2000 loop average)"),
      ADD_STAT(ipc1_500loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "IPC: instructions per cycle (1 - 500 loop average)"),
      ADD_STAT(ipc501_1000loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "IPC: instructions per cycle (501 - 1000 loop average)"),
      ADD_STAT(ipc1001_1500loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "IPC: instructions per cycle (1001 - 1500 loop average)"),
      ADD_STAT(ipc1501_2000loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "IPC: instructions per cycle (1501 - 2000 loop average)"),

      ADD_STAT(commitSquashedInsts1_100loop, statistics::units::Count::get(),
               "The number of squashed insts skipped by commit in 1 - 100 loop"),
      ADD_STAT(commitSquashedInsts101_200loop, statistics::units::Count::get(),
               "The number of squashed insts skipped by commit in 101 - 200 loop"),
      ADD_STAT(commitSquashedInsts201_300loop, statistics::units::Count::get(),
               "The number of squashed insts skipped by commit in 201 - 300 loop"),
      ADD_STAT(commitSquashedInsts301_400loop, statistics::units::Count::get(),
               "The number of squashed insts skipped by commit in 301 - 400 loop"),
      ADD_STAT(commitSquashedInsts401_500loop, statistics::units::Count::get(),
               "The number of squashed insts skipped by commit in 401 - 500 loop"),

      ADD_STAT(commitRetiredInsts1_100loop, statistics::units::Count::get(),
               "The number of retired insts processed by commit in 1 - 100 loop"),
      ADD_STAT(commitRetiredInsts101_200loop, statistics::units::Count::get(),
               "The number of retired insts processed by commit in 101 - 200 loop"),
      ADD_STAT(commitRetiredInsts201_300loop, statistics::units::Count::get(),
               "The number of retired insts processed by commit in 201 - 300 loop"),
      ADD_STAT(commitRetiredInsts301_400loop, statistics::units::Count::get(),
               "The number of retired insts processed by commit in 301 - 400 loop"),
      ADD_STAT(commitRetiredInsts401_500loop, statistics::units::Count::get(),
               "The number of retired insts processed by commit in 401 - 500 loop"),

      ADD_STAT(recoverRate1_100loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Count>::get(),
               "RecoverRate (1 - 100 loop average)"),
      ADD_STAT(recoverRate101_200loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Count>::get(),
               "RecoverRate (101 - 200 loop average)"),
      ADD_STAT(recoverRate201_300loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Count>::get(),
               "RecoverRate (201 - 300 loop average)"),
      ADD_STAT(recoverRate301_400loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Count>::get(),
               "RecoverRate (301 - 400 loop average)"),
      ADD_STAT(recoverRate401_500loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Count>::get(),
               "RecoverRate (401 - 500 loop average)"),

      ADD_STAT(bpu0MissCommitCount1_100loop, statistics::units::Count::get(),
               "Number of squash times due to BPU0_miss in commit in 1 - 100 loop"),
      ADD_STAT(bpu0MissCommitCount101_200loop, statistics::units::Count::get(),
               "Number of squash times due to BPU0_miss in commit in 101 - 200 loop"),
      ADD_STAT(bpu0MissCommitCount201_300loop, statistics::units::Count::get(),
               "Number of squash times due to BPU0_miss in commit in 201 - 300 loop"),
      ADD_STAT(bpu0MissCommitCount301_400loop, statistics::units::Count::get(),
               "Number of squash times due to BPU0_miss in commit in 301 - 400 loop"),
      ADD_STAT(bpu0MissCommitCount401_500loop, statistics::units::Count::get(),
               "Number of squash times due to BPU0_miss in commit in 401 - 500 loop"),
      ADD_STAT(bpu1MissCommitCount1_100loop, statistics::units::Count::get(),
               "Number of squash times due to BPU1_miss in commit in 1 - 100 loop"),
      ADD_STAT(bpu1MissCommitCount101_200loop, statistics::units::Count::get(),
               "Number of squash times due to BPU1_miss in commit in 101 - 200 loop"),
      ADD_STAT(bpu1MissCommitCount201_300loop, statistics::units::Count::get(),
               "Number of squash times due to BPU1_miss in commit in 201 - 300 loop"),
      ADD_STAT(bpu1MissCommitCount301_400loop, statistics::units::Count::get(),
               "Number of squash times due to BPU1_miss in commit in 301 - 400 loop"),
      ADD_STAT(bpu1MissCommitCount401_500loop, statistics::units::Count::get(),
               "Number of squash times due to BPU1_miss in commit in 401 - 500 loop"),

      ADD_STAT(bpu0MissCommitCount, statistics::units::Count::get(),
               "Number of mispredicted times due to BPU0_miss in commit"),
      ADD_STAT(bpu1MissCommitCount, statistics::units::Count::get(),
               "Number of mispredicted times due to BPU1_miss in commit"),

      ADD_STAT(bpu1MissCommitCount_predWeak, statistics::units::Count::get(),
               "Number of mispredicted times due to BPU1_miss in commit when pred was weak"),

      ADD_STAT(bpu0MissDecodeCount1_100loop, statistics::units::Count::get(),
               "Number of squash times due to BPU0_miss in decode in 1 - 100 loop"),
      ADD_STAT(bpu0MissDecodeCount101_200loop, statistics::units::Count::get(),
               "Number of squash times due to BPU0_miss in decode in 101 - 200 loop"),
      ADD_STAT(bpu0MissDecodeCount201_300loop, statistics::units::Count::get(),
               "Number of squash times due to BPU0_miss in decode in 201 - 300 loop"),
      ADD_STAT(bpu0MissDecodeCount301_400loop, statistics::units::Count::get(),
               "Number of squash times due to BPU0_miss in decode in 301 - 400 loop"),
      ADD_STAT(bpu0MissDecodeCount401_500loop, statistics::units::Count::get(),
               "Number of squash times due to BPU0_miss in decode in 401 - 500 loop"),
      ADD_STAT(bpu1MissDecodeCount1_100loop, statistics::units::Count::get(),
               "Number of squash times due to BPU1_miss in decode in 1 - 100 loop"),
      ADD_STAT(bpu1MissDecodeCount101_200loop, statistics::units::Count::get(),
               "Number of squash times due to BPU1_miss in decode in 101 - 200 loop"),
      ADD_STAT(bpu1MissDecodeCount201_300loop, statistics::units::Count::get(),
               "Number of squash times due to BPU1_miss in decode in 201 - 300 loop"),
      ADD_STAT(bpu1MissDecodeCount301_400loop, statistics::units::Count::get(),
               "Number of squash times due to BPU1_miss in decode in 301 - 400 loop"),
      ADD_STAT(bpu1MissDecodeCount401_500loop, statistics::units::Count::get(),
               "Number of squash times due to BPU1_miss in decode in 401 - 500 loop"),

      ADD_STAT(bpu0MissDecodeCount, statistics::units::Count::get(),
               "Number of mispredicted times due to BPU0_miss in decode"),
      ADD_STAT(bpu1MissDecodeCount, statistics::units::Count::get(),
               "Number of mispredicted times due to BPU1_miss in decode"),
      
      ADD_STAT(bpuMissDecodeCount, statistics::units::Count::get(),
               "Number of mispredicted times due to BPU_miss in decode"),
      ADD_STAT(bpuMissCommitCount, statistics::units::Count::get(),
               "Number of mispredicted times due to BPU_miss in commit"),
      ADD_STAT(retiredBranchInsts1_100loop, statistics::units::Count::get(),
               "Number of retired branch insts processed by commit in 1 - 100 loop"),
      ADD_STAT(retiredBranchInsts101_200loop, statistics::units::Count::get(),
               "Number of retired branch insts processed by commit in 101 - 200 loop"),
      ADD_STAT(retiredBranchInsts201_300loop, statistics::units::Count::get(),
               "Number of retired branch insts processed by commit in 201 - 300 loop"),
      ADD_STAT(retiredBranchInsts301_400loop, statistics::units::Count::get(),
               "Number of retired branch insts processed by commit in 301 - 400 loop"),
      ADD_STAT(retiredBranchInsts401_500loop, statistics::units::Count::get(),
               "Number of retired branch insts processed by commit in 401 - 500 loop"),

      ADD_STAT(retiredBranchInsts, statistics::units::Count::get(),
               "Number of retired branch insts processed by commit"),

      ADD_STAT(bpu0MissRate1_100loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Count>::get(),
               "bpu0MissRate (1 - 100 loop average)"),
      ADD_STAT(bpu0MissRate101_200loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Count>::get(),
               "bpu0MissRate (101 - 200 loop average)"),
      ADD_STAT(bpu0MissRate201_300loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Count>::get(),
               "bpu0MissRate (201 - 300 loop average)"),
      ADD_STAT(bpu0MissRate301_400loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Count>::get(),
               "bpu0MissRate (301 - 400 loop average)"),
      ADD_STAT(bpu0MissRate401_500loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Count>::get(),
               "bpu0MissRate (401 - 500 loop average)"),
      ADD_STAT(bpu1MissRate1_100loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Count>::get(),
               "bpu1MissRate (1 - 100 loop average)"),
      ADD_STAT(bpu1MissRate101_200loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Count>::get(),
               "bpu1MissRate (101 - 200 loop average)"),
      ADD_STAT(bpu1MissRate201_300loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Count>::get(),
               "bpu1MissRate (201 - 300 loop average)"),
      ADD_STAT(bpu1MissRate301_400loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Count>::get(),
               "bpu1MissRate (301 - 400 loop average)"),
      ADD_STAT(bpu1MissRate401_500loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Count>::get(),
               "bpu1MissRate (401 - 500 loop average)"),

    ADD_STAT(bpu0MissRate, statistics::units::Rate<
                statistics::units::Count, statistics::units::Count>::get(),
               "bpu0MissRate"),
    ADD_STAT(bpu1MissRate, statistics::units::Rate<
                statistics::units::Count, statistics::units::Count>::get(),
               "bpu1MissRate"),
    
    ADD_STAT(bpuMissRate, statistics::units::Rate<
                statistics::units::Count, statistics::units::Count>::get(),
               "bpuMissRate"),
    //   ADD_STAT(ipc1, statistics::units::Rate<
    //             statistics::units::Count, statistics::units::Cycle>::get(),
    //            "IPC1: instructions per cycle (core level) 0 - 6000000"),
    //   ADD_STAT(ipc2, statistics::units::Rate<
    //             statistics::units::Count, statistics::units::Cycle>::get(),
    //            "IPC2: instructions per cycle (core level) 6000000 - 12000000"),
// ------------------------------------------------------------------------------
      ADD_STAT(numWorkItemsStarted, statistics::units::Count::get(),
               "Number of work items this cpu started"),
      ADD_STAT(numWorkItemsCompleted, statistics::units::Count::get(),
               "Number of work items this cpu completed"),
      ADD_STAT(xhnumCycles, statistics::units::Cycle::get(),
               "Number of cpu xh cycles simulated"),
      ADD_STAT(xhcpi, statistics::units::Rate<
                statistics::units::Cycle, statistics::units::Count>::get(),
               "xhCPI: cycles per instruction (core level)"),
      ADD_STAT(xhipc, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "IPC: instructions per cycle (core level)"),
// --------------- add by zhuohao.zhang -------------------------------------------
      ADD_STAT(xhnumInsts1_110loop, statistics::units::Count::get(),
               "Number of insts retired in 1 - 110 loop"),
      ADD_STAT(xhnumInsts1_30loop, statistics::units::Count::get(),
               "Number of insts retired in 1 - 30 loop"),
      ADD_STAT(xhnumInsts1_64loop, statistics::units::Count::get(),
               "Number of insts retired in 1 - 64 loop"),
      ADD_STAT(xhnumInsts1_134loop, statistics::units::Count::get(),
               "Number of insts retired in 1 - 134 loop"),
      ADD_STAT(xhnumInsts1_1024loop, statistics::units::Count::get(),
               "Number of insts retired in 1 - 1024 loop"),
      ADD_STAT(xhnumInsts50_110loop, statistics::units::Count::get(),
               "Number of insts retired in 50 - 110 loop"),
      ADD_STAT(xhnumInsts20_30loop, statistics::units::Count::get(),
               "Number of insts retired in 20 - 30 loop"),
      ADD_STAT(xhnumInsts30_64loop, statistics::units::Count::get(),
               "Number of insts retired in 30 - 64 loop"),
      ADD_STAT(xhnumInsts60_134loop, statistics::units::Count::get(),
               "Number of insts retired in 60 - 134 loop"),
      ADD_STAT(xhnumInsts500_1024loop, statistics::units::Count::get(),
               "Number of insts retired in 500 - 1024 loop"),
      ADD_STAT(xhnumInsts1000_1024loop, statistics::units::Count::get(),
               "Number of insts retired in 1000 - 1024 loop"),
    
      ADD_STAT(xhnumCycles1_110loop, statistics::units::Count::get(),
               "Number of cpu cycles simulated in 1 - 110 loop"),
      ADD_STAT(xhnumCycles1_30loop, statistics::units::Count::get(),
               "Number of cpu cycles simulated in 1 - 30 loop"),
      ADD_STAT(xhnumCycles1_64loop, statistics::units::Count::get(),
               "Number of cpu cycles simulated in 1 - 64 loop"),
      ADD_STAT(xhnumCycles1_134loop, statistics::units::Count::get(),
               "Number of cpu cycles simulated in 1 - 134 loop"),
      ADD_STAT(xhnumCycles1_1024loop, statistics::units::Count::get(),
               "Number of cpu cycles simulated in 1 - 1024 loop"),
      ADD_STAT(xhnumCycles50_110loop, statistics::units::Count::get(),
               "Number of cpu cycles simulated in 50 - 110 loop"),
      ADD_STAT(xhnumCycles20_30loop, statistics::units::Count::get(),
               "Number of cpu cycles simulated in 20 - 30 loop"),
      ADD_STAT(xhnumCycles30_64loop, statistics::units::Count::get(),
               "Number of cpu cycles simulated in 30 - 64 loop"),
      ADD_STAT(xhnumCycles60_134loop, statistics::units::Count::get(),
               "Number of cpu cycles simulated in 60 - 134 loop"),
      ADD_STAT(xhnumCycles500_1024loop, statistics::units::Count::get(),
               "Number of cpu cycles simulated in 500 - 1024 loop"),
      ADD_STAT(xhnumCycles1000_1024loop, statistics::units::Count::get(),
               "Number of cpu cycles simulated in 1000 - 1024 loop"),

      ADD_STAT(xhipc1_110loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "IPC: instructions per cycle in 1 - 110 loop"),
      ADD_STAT(xhipc1_30loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "IPC: instructions per cycle in 1 - 30 loop"),
      ADD_STAT(xhipc1_64loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "IPC: instructions per cycle in 1 - 64 loop"),
      ADD_STAT(xhipc1_134loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "IPC: instructions per cycle in 1 - 134 loop"),
      ADD_STAT(xhipc1_1024loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "IPC: instructions per cycle in 1 - 1024 loop"),
      ADD_STAT(xhipc50_110loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "IPC: instructions per cycle in 50 - 110 loop"),
      ADD_STAT(xhipc20_30loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "IPC: instructions per cycle in 20 - 30 loop"),
      ADD_STAT(xhipc30_64loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "IPC: instructions per cycle in 30 - 64 loop"),
      ADD_STAT(xhipc60_134loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "IPC: instructions per cycle in 60 - 134 loop"),
      ADD_STAT(xhipc500_1024loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "IPC: instructions per cycle in 500 - 1024 loop"),
      ADD_STAT(xhipc1000_1024loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "IPC: instructions per cycle in 1000 - 1024 loop"),

      ADD_STAT(xhcommitSquashedInsts1_110loop, statistics::units::Count::get(),
               "Number of squashed insts skipped by commit in 1 - 110 loop"),
      ADD_STAT(xhcommitSquashedInsts1_30loop, statistics::units::Count::get(),
               "Number of squashed insts skipped by commit in 1 - 30 loop"),
      ADD_STAT(xhcommitSquashedInsts1_64loop, statistics::units::Count::get(),
               "Number of squashed insts skipped by commit in 1 - 64 loop"),
      ADD_STAT(xhcommitSquashedInsts1_134loop, statistics::units::Count::get(),
               "Number of squashed insts skipped by commit in 1 - 134 loop"),
      ADD_STAT(xhcommitSquashedInsts1_1024loop, statistics::units::Count::get(),
               "Number of squashed insts skipped by commit in 1 - 1024 loop"),
      ADD_STAT(xhcommitSquashedInsts50_110loop, statistics::units::Count::get(),
               "Number of squashed insts skipped by commit in 50 - 110 loop"),
      ADD_STAT(xhcommitSquashedInsts20_30loop, statistics::units::Count::get(),
               "Number of squashed insts skipped by commit in 20 - 30 loop"),
      ADD_STAT(xhcommitSquashedInsts30_64loop, statistics::units::Count::get(),
               "Number of squashed insts skipped by commit in 30 - 64 loop"),
      ADD_STAT(xhcommitSquashedInsts60_134loop, statistics::units::Count::get(),
               "Number of squashed insts skipped by commit in 60 - 134 loop"),
      ADD_STAT(xhcommitSquashedInsts500_1024loop, statistics::units::Count::get(),
               "Number of squashed insts skipped by commit in 500 - 1024 loop"),
      ADD_STAT(xhcommitSquashedInsts1000_1024loop, statistics::units::Count::get(),
               "Number of squashed insts skipped by commit in 1000 - 1024 loop"),

      ADD_STAT(xhcommitRetiredInsts1_110loop, statistics::units::Count::get(),
               "Number of retired insts processed by commit in 1 - 110 loop"),
      ADD_STAT(xhcommitRetiredInsts1_30loop, statistics::units::Count::get(),
               "Number of retired insts processed by commit in 1 - 30 loop"),
      ADD_STAT(xhcommitRetiredInsts1_64loop, statistics::units::Count::get(),
               "Number of retired insts processed by commit in 1 - 64 loop"),
      ADD_STAT(xhcommitRetiredInsts1_134loop, statistics::units::Count::get(),
               "Number of retired insts processed by commit in 1 - 134 loop"),
      ADD_STAT(xhcommitRetiredInsts1_1024loop, statistics::units::Count::get(),
               "Number of retired insts processed by commit in 1 - 1024 loop"),
      ADD_STAT(xhcommitRetiredInsts50_110loop, statistics::units::Count::get(),
               "Number of retired insts processed by commit in 50 - 110 loop"),
      ADD_STAT(xhcommitRetiredInsts20_30loop, statistics::units::Count::get(),
               "Number of retired insts processed by commit in 20 - 30 loop"),
      ADD_STAT(xhcommitRetiredInsts30_64loop, statistics::units::Count::get(),
               "Number of retired insts processed by commit in 30 - 64 loop"),
      ADD_STAT(xhcommitRetiredInsts60_134loop, statistics::units::Count::get(),
               "Number of retired insts processed by commit in 60 - 134 loop"),
      ADD_STAT(xhcommitRetiredInsts500_1024loop, statistics::units::Count::get(),
               "Number of retired insts processed by commit in 500 - 1024 loop"),
      ADD_STAT(xhcommitRetiredInsts1000_1024loop, statistics::units::Count::get(),
               "Number of retired insts processed by commit in 1000 - 1024 loop"),

      ADD_STAT(xhrecoverRate1_110loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "recoverRate:   in 1 - 110 loop"),
      ADD_STAT(xhrecoverRate1_30loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "recoverRate:   in 1 - 30 loop"),
      ADD_STAT(xhrecoverRate1_64loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "recoverRate:   in 1 - 64 loop"),
      ADD_STAT(xhrecoverRate1_134loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "recoverRate:   in 1 - 134 loop"),
      ADD_STAT(xhrecoverRate1_1024loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "recoverRate:   in 1 - 1024 loop"),
      ADD_STAT(xhrecoverRate50_110loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "recoverRate:   in 50 - 110 loop"),
      ADD_STAT(xhrecoverRate20_30loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "recoverRate:   in 20 - 30 loop"),
      ADD_STAT(xhrecoverRate30_64loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "recoverRate:   in 30 - 64 loop"),
      ADD_STAT(xhrecoverRate60_134loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "recoverRate:   in 60 - 134 loop"),
      ADD_STAT(xhrecoverRate500_1024loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "recoverRate:   in 500 - 1024 loop"),
      ADD_STAT(xhrecoverRate1000_1024loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "recoverRate:   in 1000 - 1024 loop"),

      ADD_STAT(xhbpuMissCommitCount1_110loop, statistics::units::Count::get(),
               "Number of squash times due to BPU_miss by commit in 1 - 110 loop"),
      ADD_STAT(xhbpuMissCommitCount1_30loop, statistics::units::Count::get(),
               "Number of squash times due to BPU_miss by commit in 1 - 30 loop"),
      ADD_STAT(xhbpuMissCommitCount1_64loop, statistics::units::Count::get(),
               "Number of squash times due to BPU_miss by commit in 1 - 64 loop"),
      ADD_STAT(xhbpuMissCommitCount1_134loop, statistics::units::Count::get(),
               "Number of squash times due to BPU_miss by commit in 1 - 134 loop"),
      ADD_STAT(xhbpuMissCommitCount1_1024loop, statistics::units::Count::get(),
               "Number of squash times due to BPU_miss by commit in 1 - 1024 loop"),
      ADD_STAT(xhbpuMissCommitCount50_110loop, statistics::units::Count::get(),
               "Number of squash times due to BPU_miss by commit in 50 - 110 loop"),
      ADD_STAT(xhbpuMissCommitCount20_30loop, statistics::units::Count::get(),
               "Number of squash times due to BPU_miss by commit in 20 - 30 loop"),
      ADD_STAT(xhbpuMissCommitCount30_64loop, statistics::units::Count::get(),
               "Number of squash times due to BPU_miss by commit in 30 - 64 loop"),
      ADD_STAT(xhbpuMissCommitCount60_134loop, statistics::units::Count::get(),
               "Number of squash times due to BPU_miss by commit in 60 - 134 loop"),
      ADD_STAT(xhbpuMissCommitCount500_1024loop, statistics::units::Count::get(),
               "Number of squash times due to BPU_miss by commit in 500 - 1024 loop"),
      ADD_STAT(xhbpuMissCommitCount1000_1024loop, statistics::units::Count::get(),
               "Number of squash times due to BPU_miss by commit in 1000 - 1024 loop"),

      ADD_STAT(xhbpuMissDecodeCount1_110loop, statistics::units::Count::get(),
               "Number of squash times due to BPU_miss in decode in 1 - 110 loop"),
      ADD_STAT(xhbpuMissDecodeCount1_30loop, statistics::units::Count::get(),
               "Number of squash times due to BPU_miss in decode in 1 - 30 loop"),
      ADD_STAT(xhbpuMissDecodeCount1_64loop, statistics::units::Count::get(),
               "Number of squash times due to BPU_miss in decode in 1 - 64 loop"),
      ADD_STAT(xhbpuMissDecodeCount1_134loop, statistics::units::Count::get(),
               "Number of squash times due to BPU_miss in decode in 1 - 134 loop"),
      ADD_STAT(xhbpuMissDecodeCount1_1024loop, statistics::units::Count::get(),
               "Number of squash times due to BPU_miss in decode in 1 - 1024 loop"),
      ADD_STAT(xhbpuMissDecodeCount50_110loop, statistics::units::Count::get(),
               "Number of squash times due to BPU_miss in decode in 50 - 110 loop"),
      ADD_STAT(xhbpuMissDecodeCount20_30loop, statistics::units::Count::get(),
               "Number of squash times due to BPU_miss in decode in 20 - 30 loop"),
      ADD_STAT(xhbpuMissDecodeCount30_64loop, statistics::units::Count::get(),
               "Number of squash times due to BPU_miss in decode in 30 - 64 loop"),
      ADD_STAT(xhbpuMissDecodeCount60_134loop, statistics::units::Count::get(),
               "Number of squash times due to BPU_miss in decode in 60 - 134 loop"),
      ADD_STAT(xhbpuMissDecodeCount500_1024loop, statistics::units::Count::get(),
               "Number of squash times due to BPU_miss in decode in 500 - 1024 loop"),
      ADD_STAT(xhbpuMissDecodeCount1000_1024loop, statistics::units::Count::get(),
               "Number of squash times due to BPU_miss in decode in 1000 - 1024 loop"),


      ADD_STAT(xhretiredBranchInsts1_110loop, statistics::units::Count::get(),
               "Number of retired branch insts processed by commit in 1 - 110 loop"),
      ADD_STAT(xhretiredBranchInsts1_30loop, statistics::units::Count::get(),
               "Number of retired branch insts processed by commit in 1 - 30 loop"),
      ADD_STAT(xhretiredBranchInsts1_64loop, statistics::units::Count::get(),
               "Number of retired branch insts processed by commit in 1 - 64 loop"),
      ADD_STAT(xhretiredBranchInsts1_134loop, statistics::units::Count::get(),
               "Number of retired branch insts processed by commit in 1 - 134 loop"),
      ADD_STAT(xhretiredBranchInsts1_1024loop, statistics::units::Count::get(),
               "Number of retired branch insts processed by commit in 1 - 1024 loop"),
      ADD_STAT(xhretiredBranchInsts50_110loop, statistics::units::Count::get(),
               "Number of retired branch insts processed by commit in 50 - 110 loop"),
      ADD_STAT(xhretiredBranchInsts20_30loop, statistics::units::Count::get(),
               "Number of retired branch insts processed by commit in 20 - 30 loop"),
      ADD_STAT(xhretiredBranchInsts30_64loop, statistics::units::Count::get(),
               "Number of retired branch insts processed by commit in 30 - 64 loop"),
      ADD_STAT(xhretiredBranchInsts60_134loop, statistics::units::Count::get(),
               "Number of retired branch insts processed by commit in 60 - 134 loop"),
      ADD_STAT(xhretiredBranchInsts500_1024loop, statistics::units::Count::get(),
               "Number of retired branch insts processed by commit in 500 - 1024 loop"),
      ADD_STAT(xhretiredBranchInsts1000_1024loop, statistics::units::Count::get(),
               "Number of retired branch insts processed by commit in 1000 - 1024 loop"),

      ADD_STAT(xhbpuMissRate1_110loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "bpuMissRate:   in 1 - 110 loop"),
      ADD_STAT(xhbpuMissRate1_30loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "bpuMissRate:   in 1 - 30 loop"),
      ADD_STAT(xhbpuMissRate1_64loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "bpuMissRate:   in 1 - 64 loop"),
      ADD_STAT(xhbpuMissRate1_134loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "bpuMissRate:   in 1 - 134 loop"),
      ADD_STAT(xhbpuMissRate1_1024loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "bpuMissRate:   in 1 - 1024 loop"),
      ADD_STAT(xhbpuMissRate50_110loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "bpuMissRate:   in 50 - 110 loop"),
      ADD_STAT(xhbpuMissRate20_30loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "bpuMissRate:   in 20 - 30 loop"),
      ADD_STAT(xhbpuMissRate30_64loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "bpuMissRate:   in 30 - 64 loop"),
      ADD_STAT(xhbpuMissRate60_134loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "bpuMissRate:   in 60 - 134 loop"),
      ADD_STAT(xhbpuMissRate500_1024loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "bpuMissRate:   in 500 - 1024 loop"),
      ADD_STAT(xhbpuMissRate1000_1024loop, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "bpuMissRate:   in 1000 - 1024 loop"),

      ADD_STAT(xhipc1, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "IPC1: instructions per cycle (core level) 0 - 6000000"),
      ADD_STAT(xhipc2, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
               "IPC2: instructions per cycle (core level) 6000000 - 12000000"),
// ------------------------------------------------------------------------------
      ADD_STAT(xhnumWorkItemsStarted, statistics::units::Count::get(),
               "Number of work items this cpu started"),
      ADD_STAT(xhnumWorkItemsCompleted, statistics::units::Count::get(),
               "Number of work items this cpu completed")
{
    cpi.precision(6);
    cpi = numCycles / numInsts;



    ipc.precision(6);
    ipc = numInsts / numCycles;

// --------------- add by hongfei.liu -------------------
    numInsts1_100loop.prereq(numInsts1_100loop);
    numInsts101_200loop.prereq(numInsts101_200loop);
    numInsts201_300loop.prereq(numInsts201_300loop);
    numInsts301_400loop.prereq(numInsts301_400loop);
    numInsts401_500loop.prereq(numInsts401_500loop);
    numInsts501_600loop.prereq(numInsts501_600loop);
    numInsts601_700loop.prereq(numInsts601_700loop);
    numInsts701_800loop.prereq(numInsts701_800loop);
    numInsts801_900loop.prereq(numInsts801_900loop);
    numInsts901_1000loop.prereq(numInsts901_1000loop);
    numInsts1001_1100loop.prereq(numInsts1001_1100loop);
    numInsts1101_1200loop.prereq(numInsts1101_1200loop);
    numInsts1201_1300loop.prereq(numInsts1201_1300loop);
    numInsts1301_1400loop.prereq(numInsts1301_1400loop);
    numInsts1401_1500loop.prereq(numInsts1401_1500loop);
    numInsts1501_1600loop.prereq(numInsts1501_1600loop);
    numInsts1601_1700loop.prereq(numInsts1601_1700loop);
    numInsts1701_1800loop.prereq(numInsts1701_1800loop);
    numInsts1801_1900loop.prereq(numInsts1801_1900loop);
    numInsts1901_2000loop.prereq(numInsts1901_2000loop);
    numInsts1_500loop.prereq(numInsts1_500loop);
    numInsts501_1000loop.prereq(numInsts501_1000loop);
    numInsts1001_1500loop.prereq(numInsts1001_1500loop);
    numInsts1501_2000loop.prereq(numInsts1501_2000loop);

    numCycles1_100loop.prereq(numCycles1_100loop);
    numCycles101_200loop.prereq(numCycles101_200loop);
    numCycles201_300loop.prereq(numCycles201_300loop);
    numCycles301_400loop.prereq(numCycles301_400loop);
    numCycles401_500loop.prereq(numCycles401_500loop);
    numCycles501_600loop.prereq(numCycles501_600loop);
    numCycles601_700loop.prereq(numCycles601_700loop);
    numCycles701_800loop.prereq(numCycles701_800loop);
    numCycles801_900loop.prereq(numCycles801_900loop);
    numCycles901_1000loop.prereq(numCycles901_1000loop);
    numCycles1001_1100loop.prereq(numCycles1001_1100loop);
    numCycles1101_1200loop.prereq(numCycles1101_1200loop);
    numCycles1201_1300loop.prereq(numCycles1201_1300loop);
    numCycles1301_1400loop.prereq(numCycles1301_1400loop);
    numCycles1401_1500loop.prereq(numCycles1401_1500loop);
    numCycles1501_1600loop.prereq(numCycles1501_1600loop);
    numCycles1601_1700loop.prereq(numCycles1601_1700loop);
    numCycles1701_1800loop.prereq(numCycles1701_1800loop);
    numCycles1801_1900loop.prereq(numCycles1801_1900loop);
    numCycles1901_2000loop.prereq(numCycles1901_2000loop);
    numCycles1_500loop.prereq(numCycles1_500loop);
    numCycles501_1000loop.prereq(numCycles501_1000loop);
    numCycles1001_1500loop.prereq(numCycles1001_1500loop);
    numCycles1501_2000loop.prereq(numCycles1501_2000loop);

    ipc1_100loop.precision(6);
    ipc1_100loop = numInsts1_100loop / numCycles1_100loop;
    ipc101_200loop.precision(6);
    ipc101_200loop = numInsts101_200loop / numCycles101_200loop;
    ipc201_300loop.precision(6);
    ipc201_300loop = numInsts201_300loop / numCycles201_300loop;
    ipc301_400loop.precision(6);
    ipc301_400loop = numInsts301_400loop / numCycles301_400loop;
    ipc401_500loop.precision(6);
    ipc401_500loop = numInsts401_500loop / numCycles401_500loop;
    ipc501_600loop.precision(6);
    ipc501_600loop = numInsts501_600loop / numCycles501_600loop;
    ipc601_700loop.precision(6);
    ipc601_700loop = numInsts601_700loop / numCycles601_700loop;
    ipc701_800loop.precision(6);
    ipc701_800loop = numInsts701_800loop / numCycles701_800loop;
    ipc801_900loop.precision(6);
    ipc801_900loop = numInsts801_900loop / numCycles801_900loop;
    ipc901_1000loop.precision(6);
    ipc901_1000loop = numInsts901_1000loop / numCycles901_1000loop;
    ipc1001_1100loop.precision(6);
    ipc1001_1100loop = numInsts1001_1100loop / numCycles1001_1100loop;
    ipc1101_1200loop.precision(6);
    ipc1101_1200loop = numInsts1101_1200loop / numCycles1101_1200loop;
    ipc1201_1300loop.precision(6);
    ipc1201_1300loop = numInsts1201_1300loop / numCycles1201_1300loop;
    ipc1301_1400loop.precision(6);
    ipc1301_1400loop = numInsts1301_1400loop / numCycles1301_1400loop;
    ipc1401_1500loop.precision(6);
    ipc1401_1500loop = numInsts1401_1500loop / numCycles1401_1500loop;
    ipc1501_1600loop.precision(6);
    ipc1501_1600loop = numInsts1501_1600loop / numCycles1501_1600loop;
    ipc1601_1700loop.precision(6);
    ipc1601_1700loop = numInsts1601_1700loop / numCycles1601_1700loop;
    ipc1701_1800loop.precision(6);
    ipc1701_1800loop = numInsts1701_1800loop / numCycles1701_1800loop;
    ipc1801_1900loop.precision(6);
    ipc1801_1900loop = numInsts1801_1900loop / numCycles1801_1900loop;
    ipc1901_2000loop.precision(6);
    ipc1901_2000loop = numInsts1901_2000loop / numCycles1901_2000loop;
    ipc1_500loop.precision(6);
    ipc1_500loop = numInsts1_500loop / numCycles1_500loop;
    ipc501_1000loop.precision(6);
    ipc501_1000loop = numInsts501_1000loop / numCycles501_1000loop;
    ipc1001_1500loop.precision(6);
    ipc1001_1500loop = numInsts1001_1500loop / numCycles1001_1500loop;
    ipc1501_2000loop.precision(6);
    ipc1501_2000loop = numInsts1501_2000loop / numCycles1501_2000loop;

    commitSquashedInsts1_100loop.prereq(commitSquashedInsts1_100loop);
    commitSquashedInsts101_200loop.prereq(commitSquashedInsts101_200loop);
    commitSquashedInsts201_300loop.prereq(commitSquashedInsts201_300loop);
    commitSquashedInsts301_400loop.prereq(commitSquashedInsts301_400loop);
    commitSquashedInsts401_500loop.prereq(commitSquashedInsts401_500loop);

    commitRetiredInsts1_100loop.prereq(commitRetiredInsts1_100loop);
    commitRetiredInsts101_200loop.prereq(commitRetiredInsts101_200loop);
    commitRetiredInsts201_300loop.prereq(commitRetiredInsts201_300loop);
    commitRetiredInsts301_400loop.prereq(commitRetiredInsts301_400loop);
    commitRetiredInsts401_500loop.prereq(commitRetiredInsts401_500loop);

    recoverRate1_100loop.precision(6);
    recoverRate1_100loop = commitSquashedInsts1_100loop / (commitSquashedInsts1_100loop + commitRetiredInsts1_100loop);
    recoverRate101_200loop.precision(6);
    recoverRate101_200loop = commitSquashedInsts101_200loop / (commitSquashedInsts101_200loop + commitRetiredInsts101_200loop);
    recoverRate201_300loop.precision(6);
    recoverRate201_300loop = commitSquashedInsts201_300loop / (commitSquashedInsts201_300loop + commitRetiredInsts201_300loop);
    recoverRate301_400loop.precision(6);
    recoverRate301_400loop = commitSquashedInsts301_400loop / (commitSquashedInsts301_400loop + commitRetiredInsts301_400loop);
    recoverRate401_500loop.precision(6);
    recoverRate401_500loop = commitSquashedInsts401_500loop / (commitSquashedInsts401_500loop + commitRetiredInsts401_500loop);

    bpu0MissCommitCount1_100loop.prereq(bpu0MissCommitCount1_100loop);
    bpu0MissCommitCount101_200loop.prereq(bpu0MissCommitCount101_200loop);
    bpu0MissCommitCount201_300loop.prereq(bpu0MissCommitCount201_300loop);
    bpu0MissCommitCount301_400loop.prereq(bpu0MissCommitCount301_400loop);
    bpu0MissCommitCount401_500loop.prereq(bpu0MissCommitCount401_500loop);
    bpu1MissCommitCount1_100loop.prereq(bpu1MissCommitCount1_100loop);
    bpu1MissCommitCount101_200loop.prereq(bpu1MissCommitCount101_200loop);
    bpu1MissCommitCount201_300loop.prereq(bpu1MissCommitCount201_300loop);
    bpu1MissCommitCount301_400loop.prereq(bpu1MissCommitCount301_400loop);
    bpu1MissCommitCount401_500loop.prereq(bpu1MissCommitCount401_500loop);

    bpu0MissCommitCount.prereq(bpu0MissCommitCount);
    bpu1MissCommitCount.prereq(bpu1MissCommitCount);

    bpu1MissCommitCount_predWeak.prereq(bpu1MissCommitCount_predWeak);

    bpu0MissDecodeCount1_100loop.prereq(bpu0MissDecodeCount1_100loop);
    bpu0MissDecodeCount101_200loop.prereq(bpu0MissDecodeCount101_200loop);
    bpu0MissDecodeCount201_300loop.prereq(bpu0MissDecodeCount201_300loop);
    bpu0MissDecodeCount301_400loop.prereq(bpu0MissDecodeCount301_400loop);
    bpu0MissDecodeCount401_500loop.prereq(bpu0MissDecodeCount401_500loop);
    bpu1MissDecodeCount1_100loop.prereq(bpu1MissDecodeCount1_100loop);
    bpu1MissDecodeCount101_200loop.prereq(bpu1MissDecodeCount101_200loop);
    bpu1MissDecodeCount201_300loop.prereq(bpu1MissDecodeCount201_300loop);
    bpu1MissDecodeCount301_400loop.prereq(bpu1MissDecodeCount301_400loop);
    bpu1MissDecodeCount401_500loop.prereq(bpu1MissDecodeCount401_500loop);

    bpu0MissDecodeCount.prereq(bpu0MissDecodeCount);
    bpu1MissDecodeCount.prereq(bpu1MissDecodeCount);
    bpuMissDecodeCount.prereq(bpuMissDecodeCount);
    bpuMissCommitCount.prereq(bpuMissCommitCount);

    retiredBranchInsts1_100loop.prereq(retiredBranchInsts1_100loop);
    retiredBranchInsts101_200loop.prereq(retiredBranchInsts101_200loop);
    retiredBranchInsts201_300loop.prereq(retiredBranchInsts201_300loop);
    retiredBranchInsts301_400loop.prereq(retiredBranchInsts301_400loop);
    retiredBranchInsts401_500loop.prereq(retiredBranchInsts401_500loop);

    retiredBranchInsts.prereq(retiredBranchInsts);

    bpu0MissRate1_100loop.precision(6);
    bpu0MissRate1_100loop = (bpu0MissCommitCount1_100loop + bpu0MissDecodeCount1_100loop) / retiredBranchInsts1_100loop;
    bpu0MissRate101_200loop.precision(6);
    bpu0MissRate101_200loop = (bpu0MissCommitCount101_200loop + bpu0MissDecodeCount101_200loop) / retiredBranchInsts101_200loop;
    bpu0MissRate201_300loop.precision(6);
    bpu0MissRate201_300loop = (bpu0MissCommitCount201_300loop + bpu0MissDecodeCount201_300loop) / retiredBranchInsts201_300loop;
    bpu0MissRate301_400loop.precision(6);
    bpu0MissRate301_400loop = (bpu0MissCommitCount301_400loop + bpu0MissDecodeCount301_400loop) / retiredBranchInsts301_400loop;
    bpu0MissRate401_500loop.precision(6);
    bpu0MissRate401_500loop = (bpu0MissCommitCount401_500loop + bpu0MissDecodeCount401_500loop) / retiredBranchInsts401_500loop;
    bpu1MissRate1_100loop.precision(6);
    bpu1MissRate1_100loop = (bpu1MissCommitCount1_100loop + bpu1MissDecodeCount1_100loop) / retiredBranchInsts1_100loop;
    bpu1MissRate101_200loop.precision(6);
    bpu1MissRate101_200loop = (bpu1MissCommitCount101_200loop + bpu1MissDecodeCount101_200loop) / retiredBranchInsts101_200loop;
    bpu1MissRate201_300loop.precision(6);
    bpu1MissRate201_300loop = (bpu1MissCommitCount201_300loop + bpu1MissDecodeCount201_300loop) / retiredBranchInsts201_300loop;
    bpu1MissRate301_400loop.precision(6);
    bpu1MissRate301_400loop = (bpu1MissCommitCount301_400loop + bpu1MissDecodeCount301_400loop) / retiredBranchInsts301_400loop;
    bpu1MissRate401_500loop.precision(6);
    bpu1MissRate401_500loop = (bpu1MissCommitCount401_500loop + bpu1MissDecodeCount401_500loop) / retiredBranchInsts401_500loop;

    bpu0MissRate.precision(6);
    bpu0MissRate = (bpu0MissCommitCount + bpu0MissDecodeCount) / retiredBranchInsts;
    bpuMissRate.precision(6);
    bpuMissRate = (bpuMissDecodeCount + bpuMissCommitCount) / retiredBranchInsts;
    bpu1MissRate.precision(6);
    bpu1MissRate = (bpu1MissCommitCount + bpu1MissDecodeCount) / retiredBranchInsts;
    
    // ipc1.precision(6);
    // ipc1 = numInsts1 / numCycles1;

    // ipc2.precision(6);
    // ipc2 = numInsts2 / numCycles2;
// ------------------------------------------------------
     xhcpi.precision(6);
    xhcpi = numCycles / numInsts;



    xhipc.precision(6);
    xhipc = xhnumInsts / xhnumCycles;

// --------------- add by zhuohao.zhang -------------------


    xhnumInsts1_110loop.prereq(xhnumInsts1_110loop);
    xhnumInsts1_30loop.prereq(xhnumInsts1_30loop);
    xhnumInsts1_64loop.prereq(xhnumInsts1_64loop);
    xhnumInsts1_134loop.prereq(xhnumInsts1_134loop);
    xhnumInsts1_1024loop.prereq(xhnumInsts1_1024loop);
    xhnumInsts50_110loop.prereq(xhnumInsts50_110loop);
    xhnumInsts20_30loop.prereq(xhnumInsts20_30loop);
    xhnumInsts30_64loop.prereq(xhnumInsts30_64loop);
    xhnumInsts60_134loop.prereq(xhnumInsts60_134loop);
    xhnumInsts500_1024loop.prereq(xhnumInsts500_1024loop);
    xhnumInsts1000_1024loop.prereq(xhnumInsts1000_1024loop);

    xhnumCycles1_110loop.prereq(xhnumCycles1_110loop);
    xhnumCycles1_30loop.prereq(xhnumCycles1_30loop);
    xhnumCycles1_64loop.prereq(xhnumCycles1_64loop);
    xhnumCycles1_134loop.prereq(xhnumCycles1_134loop);
    xhnumCycles1_1024loop.prereq(xhnumCycles1_1024loop);
    xhnumCycles50_110loop.prereq(xhnumCycles50_110loop);
    xhnumCycles20_30loop.prereq(xhnumCycles20_30loop);
    xhnumCycles30_64loop.prereq(xhnumCycles30_64loop);
    xhnumCycles60_134loop.prereq(xhnumCycles60_134loop);
    xhnumCycles500_1024loop.prereq(xhnumCycles500_1024loop);
    xhnumCycles1000_1024loop.prereq(xhnumCycles1000_1024loop);

    xhipc1_110loop.precision(6);
    xhipc1_110loop = xhnumInsts1_110loop / xhnumCycles1_110loop;
    xhipc1_30loop.precision(6);
    xhipc1_30loop = xhnumInsts1_30loop / xhnumCycles1_30loop;
    xhipc1_64loop.precision(6);
    xhipc1_64loop = xhnumInsts1_64loop / xhnumCycles1_64loop;
    xhipc1_134loop.precision(6);
    xhipc1_134loop = xhnumInsts1_134loop / xhnumCycles1_134loop;
    xhipc1_1024loop.precision(6);
    xhipc1_1024loop = xhnumInsts1_1024loop / xhnumCycles1_1024loop;
    xhipc50_110loop.precision(6);
    xhipc50_110loop = xhnumInsts50_110loop / xhnumCycles50_110loop;
    xhipc20_30loop.precision(6);
    xhipc20_30loop = xhnumInsts20_30loop / xhnumCycles20_30loop;
    xhipc30_64loop.precision(6);
    xhipc30_64loop = xhnumInsts30_64loop / xhnumCycles30_64loop;
    xhipc60_134loop.precision(6);
    xhipc60_134loop = xhnumInsts60_134loop / xhnumCycles60_134loop;
    xhipc500_1024loop.precision(6);
    xhipc500_1024loop = xhnumInsts500_1024loop / xhnumCycles500_1024loop;
    xhipc1000_1024loop.precision(6);
    xhipc1000_1024loop = xhnumInsts1000_1024loop / xhnumCycles1000_1024loop;

    xhcommitSquashedInsts1_110loop.prereq(xhcommitSquashedInsts1_110loop);
    xhcommitSquashedInsts1_30loop.prereq(xhcommitSquashedInsts1_30loop);
    xhcommitSquashedInsts1_64loop.prereq(xhcommitSquashedInsts1_64loop);
    xhcommitSquashedInsts1_134loop.prereq(xhcommitSquashedInsts1_134loop);
    xhcommitSquashedInsts1_1024loop.prereq(xhcommitSquashedInsts1_1024loop);
    xhcommitSquashedInsts50_110loop.prereq(xhcommitSquashedInsts50_110loop);
    xhcommitSquashedInsts20_30loop.prereq(xhcommitSquashedInsts20_30loop);
    xhcommitSquashedInsts30_64loop.prereq(xhcommitSquashedInsts30_64loop);
    xhcommitSquashedInsts60_134loop.prereq(xhcommitSquashedInsts60_134loop);
    xhcommitSquashedInsts500_1024loop.prereq(xhcommitSquashedInsts500_1024loop);
    xhcommitSquashedInsts1000_1024loop.prereq(xhcommitSquashedInsts1000_1024loop);

    xhcommitRetiredInsts1_110loop.prereq(xhcommitRetiredInsts1_110loop);
    xhcommitRetiredInsts1_30loop.prereq(xhcommitRetiredInsts1_30loop);
    xhcommitRetiredInsts1_64loop.prereq(xhcommitRetiredInsts1_64loop);
    xhcommitRetiredInsts1_134loop.prereq(xhcommitRetiredInsts1_134loop);
    xhcommitRetiredInsts1_1024loop.prereq(xhcommitRetiredInsts1_1024loop);
    xhcommitRetiredInsts50_110loop.prereq(xhcommitRetiredInsts50_110loop);
    xhcommitRetiredInsts20_30loop.prereq(xhcommitRetiredInsts20_30loop);
    xhcommitRetiredInsts30_64loop.prereq(xhcommitRetiredInsts30_64loop);
    xhcommitRetiredInsts60_134loop.prereq(xhcommitRetiredInsts60_134loop);
    xhcommitRetiredInsts500_1024loop.prereq(xhcommitRetiredInsts500_1024loop);
    xhcommitRetiredInsts1000_1024loop.prereq(xhcommitRetiredInsts1000_1024loop);

    xhrecoverRate1_110loop.precision(6);
    xhrecoverRate1_110loop = xhcommitSquashedInsts1_110loop / (xhcommitSquashedInsts1_110loop + xhcommitRetiredInsts1_110loop);
    xhrecoverRate1_30loop.precision(6);
    xhrecoverRate1_30loop = xhcommitSquashedInsts1_30loop / (xhcommitSquashedInsts1_30loop + xhcommitRetiredInsts1_30loop);
    xhrecoverRate1_64loop.precision(6);
    xhrecoverRate1_64loop = xhcommitSquashedInsts1_64loop / (xhcommitSquashedInsts1_64loop + xhcommitRetiredInsts1_64loop);
    xhrecoverRate1_134loop.precision(6);
    xhrecoverRate1_134loop = xhcommitSquashedInsts1_134loop / (xhcommitSquashedInsts1_134loop + xhcommitRetiredInsts1_134loop);
    xhrecoverRate1_1024loop.precision(6);
    xhrecoverRate1_1024loop = xhcommitSquashedInsts1_1024loop / (xhcommitSquashedInsts1_1024loop + xhcommitRetiredInsts1_1024loop);
    xhrecoverRate50_110loop.precision(6);
    xhrecoverRate50_110loop = xhcommitSquashedInsts50_110loop / (xhcommitSquashedInsts50_110loop + xhcommitRetiredInsts50_110loop);
    xhrecoverRate20_30loop.precision(6);
    xhrecoverRate20_30loop = xhcommitSquashedInsts20_30loop / (xhcommitSquashedInsts20_30loop + xhcommitRetiredInsts20_30loop);
    xhrecoverRate30_64loop.precision(6);
    xhrecoverRate30_64loop = xhcommitSquashedInsts30_64loop / (xhcommitSquashedInsts30_64loop + xhcommitRetiredInsts30_64loop);
    xhrecoverRate60_134loop.precision(6);
    xhrecoverRate60_134loop = xhcommitSquashedInsts60_134loop / (xhcommitSquashedInsts60_134loop + xhcommitRetiredInsts60_134loop);
    xhrecoverRate500_1024loop.precision(6);
    xhrecoverRate500_1024loop = xhcommitSquashedInsts500_1024loop / (xhcommitSquashedInsts500_1024loop + xhcommitRetiredInsts500_1024loop);
    xhrecoverRate1000_1024loop.precision(6);
    xhrecoverRate1000_1024loop = xhcommitSquashedInsts1000_1024loop / (xhcommitSquashedInsts1000_1024loop + xhcommitRetiredInsts1000_1024loop);

    xhbpuMissCommitCount1_110loop.prereq(xhbpuMissCommitCount1_110loop);
    xhbpuMissCommitCount1_30loop.prereq(xhbpuMissCommitCount1_30loop);
    xhbpuMissCommitCount1_64loop.prereq(xhbpuMissCommitCount1_64loop);
    xhbpuMissCommitCount1_134loop.prereq(xhbpuMissCommitCount1_134loop);
    xhbpuMissCommitCount1_1024loop.prereq(xhbpuMissCommitCount1_1024loop);
    xhbpuMissCommitCount50_110loop.prereq(xhbpuMissCommitCount50_110loop);
    xhbpuMissCommitCount20_30loop.prereq(xhbpuMissCommitCount20_30loop);
    xhbpuMissCommitCount30_64loop.prereq(xhbpuMissCommitCount30_64loop);
    xhbpuMissCommitCount60_134loop.prereq(xhbpuMissCommitCount60_134loop);
    xhbpuMissCommitCount500_1024loop.prereq(xhbpuMissCommitCount500_1024loop);
    xhbpuMissCommitCount1000_1024loop.prereq(xhbpuMissCommitCount1000_1024loop);

    xhbpuMissDecodeCount1_110loop.prereq(xhbpuMissDecodeCount1_110loop);
    xhbpuMissDecodeCount1_30loop.prereq(xhbpuMissDecodeCount1_30loop);
    xhbpuMissDecodeCount1_64loop.prereq(xhbpuMissDecodeCount1_64loop);
    xhbpuMissDecodeCount1_134loop.prereq(xhbpuMissDecodeCount1_134loop);
    xhbpuMissDecodeCount1_1024loop.prereq(xhbpuMissDecodeCount1_1024loop);
    xhbpuMissDecodeCount50_110loop.prereq(xhbpuMissDecodeCount50_110loop);
    xhbpuMissDecodeCount20_30loop.prereq(xhbpuMissDecodeCount20_30loop);
    xhbpuMissDecodeCount30_64loop.prereq(xhbpuMissDecodeCount30_64loop);
    xhbpuMissDecodeCount60_134loop.prereq(xhbpuMissDecodeCount60_134loop);
    xhbpuMissDecodeCount500_1024loop.prereq(xhbpuMissDecodeCount500_1024loop);
    xhbpuMissDecodeCount1000_1024loop.prereq(xhbpuMissDecodeCount1000_1024loop);

    xhretiredBranchInsts1_110loop.prereq(xhretiredBranchInsts1_110loop);
    xhretiredBranchInsts1_30loop.prereq(xhretiredBranchInsts1_30loop);
    xhretiredBranchInsts1_64loop.prereq(xhretiredBranchInsts1_64loop);
    xhretiredBranchInsts1_134loop.prereq(xhretiredBranchInsts1_134loop);
    xhretiredBranchInsts1_1024loop.prereq(xhretiredBranchInsts1_1024loop);
    xhretiredBranchInsts50_110loop.prereq(xhretiredBranchInsts50_110loop);
    xhretiredBranchInsts20_30loop.prereq(xhretiredBranchInsts20_30loop);
    xhretiredBranchInsts30_64loop.prereq(xhretiredBranchInsts30_64loop);
    xhretiredBranchInsts60_134loop.prereq(xhretiredBranchInsts60_134loop);
    xhretiredBranchInsts500_1024loop.prereq(xhretiredBranchInsts500_1024loop);
    xhretiredBranchInsts1000_1024loop.prereq(xhretiredBranchInsts1000_1024loop);

    xhbpuMissRate1_110loop.precision(6);
    xhbpuMissRate1_110loop = xhbpuMissCommitCount1_110loop / (xhbpuMissDecodeCount1_110loop + xhretiredBranchInsts1_110loop);
    xhbpuMissRate1_30loop.precision(6);
    xhbpuMissRate1_30loop = xhbpuMissCommitCount1_30loop / (xhbpuMissDecodeCount1_30loop + xhretiredBranchInsts1_30loop);
    xhbpuMissRate1_64loop.precision(6);
    xhbpuMissRate1_64loop = xhbpuMissCommitCount1_64loop / (xhbpuMissDecodeCount1_64loop + xhretiredBranchInsts1_64loop);
    xhbpuMissRate1_134loop.precision(6);
    xhbpuMissRate1_134loop = xhbpuMissCommitCount1_134loop / (xhbpuMissDecodeCount1_134loop + xhretiredBranchInsts1_134loop);
    xhbpuMissRate1_1024loop.precision(6);
    xhbpuMissRate1_1024loop = xhbpuMissCommitCount1_1024loop / (xhbpuMissDecodeCount1_1024loop + xhretiredBranchInsts1_1024loop);
    xhbpuMissRate50_110loop.precision(6);
    xhbpuMissRate50_110loop = xhbpuMissCommitCount50_110loop / (xhbpuMissDecodeCount50_110loop + xhretiredBranchInsts50_110loop);
    xhbpuMissRate20_30loop.precision(6);
    xhbpuMissRate20_30loop = xhbpuMissCommitCount20_30loop / (xhbpuMissDecodeCount20_30loop + xhretiredBranchInsts20_30loop);
    xhbpuMissRate30_64loop.precision(6);
    xhbpuMissRate30_64loop = xhbpuMissCommitCount30_64loop / (xhbpuMissDecodeCount30_64loop + xhretiredBranchInsts30_64loop);
    xhbpuMissRate60_134loop.precision(6);
    xhbpuMissRate60_134loop = xhbpuMissCommitCount60_134loop / (xhbpuMissDecodeCount60_134loop + xhretiredBranchInsts60_134loop);
    xhbpuMissRate500_1024loop.precision(6);
    xhbpuMissRate500_1024loop = xhbpuMissCommitCount500_1024loop / (xhbpuMissDecodeCount500_1024loop + xhretiredBranchInsts500_1024loop);
    xhbpuMissRate1000_1024loop.precision(6);
    xhbpuMissRate1000_1024loop = xhbpuMissCommitCount1000_1024loop / (xhbpuMissDecodeCount1000_1024loop + xhretiredBranchInsts1000_1024loop);

    
    xhipc1.precision(6);
    xhipc1 = xhnumInsts1 / xhnumCycles1;

    xhipc2.precision(6);
    xhipc2 = xhnumInsts2 / xhnumCycles2;
}

void
BaseCPU::regStats()
{
    ClockedObject::regStats();

    if (!globalStats) {
        /* We need to construct the global CPU stat structure here
         * since it needs a pointer to the Root object. */
        globalStats.reset(new GlobalStats(Root::root()));
    }

    using namespace statistics;

    int size = threadContexts.size();
    if (size > 1) {
        for (int i = 0; i < size; ++i) {
            std::stringstream namestr;
            ccprintf(namestr, "%s.ctx%d", name(), i);
            threadContexts[i]->regStats(namestr.str());
        }
    } else if (size == 1)
        threadContexts[0]->regStats(name());
}

Port &
BaseCPU::getPort(const std::string &if_name, PortID idx)
{
    // Get the right port based on name. This applies to all the
    // subclasses of the base CPU and relies on their implementation
    // of getDataPort and getInstPort.
    if (if_name == "dcache_port")
        return getDataPort();
    else if (if_name == "icache_port")
        return getInstPort();
    else if (if_name == "model_reset")
        return modelResetPort;
    else
        return ClockedObject::getPort(if_name, idx);
}

void
BaseCPU::registerThreadContexts()
{
    assert(system->multiThread || numThreads == 1);

    fatal_if(interrupts.size() != numThreads,
             "CPU %s has %i interrupt controllers, but is expecting one "
             "per thread (%i)\n",
             name(), interrupts.size(), numThreads);

    for (ThreadID tid = 0; tid < threadContexts.size(); ++tid) {
        ThreadContext *tc = threadContexts[tid];

        system->registerThreadContext(tc);

        if (!FullSystem)
            tc->getProcessPtr()->assignThreadContext(tc->contextId());

        interrupts[tid]->setThreadContext(tc);
        tc->getIsaPtr()->setThreadContext(tc);
    }
}

void
BaseCPU::deschedulePowerGatingEvent()
{
    if (enterPwrGatingEvent.scheduled()){
        deschedule(enterPwrGatingEvent);
    }
}

void
BaseCPU::schedulePowerGatingEvent()
{
    for (auto tc : threadContexts) {
        if (tc->status() == ThreadContext::Active)
            return;
    }

    if (powerState->get() == enums::PwrState::CLK_GATED &&
        powerGatingOnIdle) {
        assert(!enterPwrGatingEvent.scheduled());
        // Schedule a power gating event when clock gated for the specified
        // amount of time
        schedule(enterPwrGatingEvent, clockEdge(pwrGatingLatency));
    }
}

int
BaseCPU::findContext(ThreadContext *tc)
{
    ThreadID size = threadContexts.size();
    for (ThreadID tid = 0; tid < size; ++tid) {
        if (tc == threadContexts[tid])
            return tid;
    }
    return 0;
}

void
BaseCPU::activateContext(ThreadID thread_num)
{
    if (modelResetPort.state()) {
        DPRINTF(Thread, "CPU in reset, not activating context %d\n",
                threadContexts[thread_num]->contextId());
        return;
    }

    DPRINTF(Thread, "activate contextId %d\n",
            threadContexts[thread_num]->contextId());
    // Squash enter power gating event while cpu gets activated
    if (enterPwrGatingEvent.scheduled())
        deschedule(enterPwrGatingEvent);
    // For any active thread running, update CPU power state to active (ON)
    powerState->set(enums::PwrState::ON);

    updateCycleCounters(CPU_STATE_WAKEUP);
}

void
BaseCPU::suspendContext(ThreadID thread_num)
{
    DPRINTF(Thread, "suspend contextId %d\n",
            threadContexts[thread_num]->contextId());
    // Check if all threads are suspended
    for (auto t : threadContexts) {
        if (t->status() != ThreadContext::Suspended) {
            return;
        }
    }

    // All CPU thread are suspended, update cycle count
    updateCycleCounters(CPU_STATE_SLEEP);

    // All CPU threads suspended, enter lower power state for the CPU
    powerState->set(enums::PwrState::CLK_GATED);

    // If pwrGatingLatency is set to 0 then this mechanism is disabled
    if (powerGatingOnIdle) {
        // Schedule power gating event when clock gated for pwrGatingLatency
        // cycles
        schedule(enterPwrGatingEvent, clockEdge(pwrGatingLatency));
    }
}

void
BaseCPU::haltContext(ThreadID thread_num)
{
    updateCycleCounters(BaseCPU::CPU_STATE_SLEEP);
}

void
BaseCPU::enterPwrGating(void)
{
    powerState->set(enums::PwrState::OFF);
}

void
BaseCPU::switchOut()
{
    assert(!_switchedOut);
    _switchedOut = true;

    // Flush all TLBs in the CPU to avoid having stale translations if
    // it gets switched in later.
    flushTLBs();

    // Go to the power gating state
    powerState->set(enums::PwrState::OFF);
}

void
BaseCPU::takeOverFrom(BaseCPU *oldCPU)
{
    assert(threadContexts.size() == oldCPU->threadContexts.size());
    assert(_cpuId == oldCPU->cpuId());
    assert(_switchedOut);
    assert(oldCPU != this);
    _pid = oldCPU->getPid();
    _taskId = oldCPU->taskId();
    // Take over the power state of the switchedOut CPU
    powerState->set(oldCPU->powerState->get());

    previousState = oldCPU->previousState;
    previousCycle = oldCPU->previousCycle;

    _switchedOut = false;

    ThreadID size = threadContexts.size();
    for (ThreadID i = 0; i < size; ++i) {
        ThreadContext *newTC = threadContexts[i];
        ThreadContext *oldTC = oldCPU->threadContexts[i];

        newTC->getIsaPtr()->setThreadContext(newTC);

        newTC->takeOverFrom(oldTC);

        assert(newTC->contextId() == oldTC->contextId());
        assert(newTC->threadId() == oldTC->threadId());
        system->replaceThreadContext(newTC, newTC->contextId());

        /* This code no longer works since the zero register (e.g.,
         * r31 on Alpha) doesn't necessarily contain zero at this
         * point.
           if (debug::Context)
            ThreadContext::compare(oldTC, newTC);
        */

        newTC->getMMUPtr()->takeOverFrom(oldTC->getMMUPtr());

        // Checker whether or not we have to transfer CheckerCPU
        // objects over in the switch
        CheckerCPU *old_checker = oldTC->getCheckerCpuPtr();
        CheckerCPU *new_checker = newTC->getCheckerCpuPtr();
        if (old_checker && new_checker) {
            new_checker->getMMUPtr()->takeOverFrom(old_checker->getMMUPtr());
        }
    }

    interrupts = oldCPU->interrupts;
    for (ThreadID tid = 0; tid < numThreads; tid++) {
        interrupts[tid]->setThreadContext(threadContexts[tid]);
    }
    oldCPU->interrupts.clear();

    // All CPUs have an instruction and a data port, and the new CPU's
    // ports are dangling while the old CPU has its ports connected
    // already. Unbind the old CPU and then bind the ports of the one
    // we are switching to.
    getInstPort().takeOverFrom(&oldCPU->getInstPort());
    getDataPort().takeOverFrom(&oldCPU->getDataPort());

    // Switch over the reset line as well, if necessary.
    if (oldCPU->modelResetPort.isConnected())
        modelResetPort.takeOverFrom(&oldCPU->modelResetPort);

    // If old CPU enabled difftest, move it to this CPU as well
    // auto [enable_diff, diff_all] = oldCPU->getDiffAllStates();
    // if (enable_diff) {
    //     warn("Take over difftest state to new CPU\n");
    //     enableDifftest = enable_diff;
    //     takeOverDiffAllStates(diff_all);
    // }
}

void
BaseCPU::setReset(bool state)
{
    for (auto tc: threadContexts) {
        if (state) {
            // As we enter reset, stop execution.
            tc->quiesce();
        } else {
            // As we leave reset, first reset thread state,
            tc->getIsaPtr()->resetThread();
            // reset the decoder in case it had partially decoded something,
            tc->getDecoderPtr()->reset();
            // flush the TLBs,
            tc->getMMUPtr()->reset();
            // Clear any interrupts,
            interrupts[tc->threadId()]->clearAll();
            // and finally reenable execution.
            tc->activate();
        }
    }
}

void
BaseCPU::flushTLBs()
{
    for (ThreadID i = 0; i < threadContexts.size(); ++i) {
        ThreadContext &tc(*threadContexts[i]);
        CheckerCPU *checker(tc.getCheckerCpuPtr());

        tc.getMMUPtr()->flushAll();
        if (checker) {
            checker->getMMUPtr()->flushAll();
        }
    }
}

void
BaseCPU::serialize(CheckpointOut &cp) const
{
    SERIALIZE_SCALAR(instCnt);

    if (!_switchedOut) {
        /* Unlike _pid, _taskId is not serialized, as they are dynamically
         * assigned unique ids that are only meaningful for the duration of
         * a specific run. We will need to serialize the entire taskMap in
         * system. */
        SERIALIZE_SCALAR(_pid);

        // Serialize the threads, this is done by the CPU implementation.
        for (ThreadID i = 0; i < numThreads; ++i) {
            ScopedCheckpointSection sec(cp, csprintf("xc.%i", i));
            interrupts[i]->serialize(cp);
            serializeThread(cp, i);
        }
    }
}

void
BaseCPU::unserialize(CheckpointIn &cp)
{
    UNSERIALIZE_SCALAR(instCnt);

    if (!_switchedOut) {
        UNSERIALIZE_SCALAR(_pid);

        // Unserialize the threads, this is done by the CPU implementation.
        for (ThreadID i = 0; i < numThreads; ++i) {
            ScopedCheckpointSection sec(cp, csprintf("xc.%i", i));
            interrupts[i]->unserialize(cp);
            unserializeThread(cp, i);
        }
    }
}

void
BaseCPU::scheduleInstStop(ThreadID tid, Counter insts, std::string cause)
{
    const Tick now(getCurrentInstCount(tid));
    Event *event(new LocalSimLoopExitEvent(cause, 0));

    threadContexts[tid]->scheduleInstCountEvent(event, now + insts);
}

Tick
BaseCPU::getCurrentInstCount(ThreadID tid)
{
    return threadContexts[tid]->getCurrentInstCount();
}

AddressMonitor::AddressMonitor()
{
    armed = false;
    waiting = false;
    gotWakeup = false;
}

bool
AddressMonitor::doMonitor(PacketPtr pkt)
{
    assert(pkt->req->hasPaddr());
    if (armed && waiting) {
        if (pAddr == pkt->getAddr()) {
            DPRINTF(Mwait, "pAddr=0x%lx invalidated: waking up core\n",
                    pkt->getAddr());
            waiting = false;
            return true;
        }
    }
    return false;
}


void
BaseCPU::traceFunctionsInternal(Addr pc)
{
    if (loader::debugSymbolTable.empty())
        return;

    // if pc enters different function, print new function symbol and
    // update saved range.  Otherwise do nothing.
    if (pc < currentFunctionStart || pc >= currentFunctionEnd) {
        auto it = loader::debugSymbolTable.findNearest(
                pc, currentFunctionEnd);

        std::string sym_str;
        if (it == loader::debugSymbolTable.end()) {
            // no symbol found: use addr as label
            sym_str = csprintf("%#x", pc);
            currentFunctionStart = pc;
            currentFunctionEnd = pc + 1;
        } else {
            sym_str = it->name();
            currentFunctionStart = it->address();
        }

        ccprintf(*functionTraceStream, " (%d)\n%d: %s",
                 curTick() - functionEntryTick, curTick(), sym_str);
        functionEntryTick = curTick();
    }
}

void
BaseCPU::scheduleSimpointsInstStop(std::vector<Counter> inst_starts)
{
    std::string cause = "simpoint starting point found";
    for (size_t i = 0; i < inst_starts.size(); ++i) {
        scheduleInstStop(0, inst_starts[i], cause);
    }
}

void
BaseCPU::scheduleInstStopAnyThread(Counter max_insts)
{
    std::string cause = "a thread reached the max instruction count";
    for (ThreadID tid = 0; tid < numThreads; ++tid) {
        scheduleInstStop(tid, max_insts, cause);
    }
}

BaseCPU::GlobalStats::GlobalStats(statistics::Group *parent)
    : statistics::Group(parent),
    ADD_STAT(simInsts, statistics::units::Count::get(),
             "Number of instructions simulated"),
    ADD_STAT(simOps, statistics::units::Count::get(),
             "Number of ops (including micro ops) simulated"),
    ADD_STAT(hostInstRate, statistics::units::Rate<
                statistics::units::Count, statistics::units::Second>::get(),
             "Simulator instruction rate (inst/s)"),
    ADD_STAT(hostOpRate, statistics::units::Rate<
                statistics::units::Count, statistics::units::Second>::get(),
             "Simulator op (including micro ops) rate (op/s)")
{
    simInsts
        .functor(BaseCPU::numSimulatedInsts)
        .precision(0)
        .prereq(simInsts)
        ;

    simOps
        .functor(BaseCPU::numSimulatedOps)
        .precision(0)
        .prereq(simOps)
        ;

    hostInstRate
        .precision(0)
        .prereq(simInsts)
        ;

    hostOpRate
        .precision(0)
        .prereq(simOps)
        ;

    hostInstRate = simInsts / hostSeconds;
    hostOpRate = simOps / hostSeconds;
}

BaseCPU::
FetchCPUStats::FetchCPUStats(statistics::Group *parent, int thread_id)
    : statistics::Group(parent, csprintf("fetchStats%i", thread_id).c_str()),
    ADD_STAT(numInsts, statistics::units::Count::get(),
             "Number of instructions fetched (thread level)"),
    ADD_STAT(numOps, statistics::units::Count::get(),
             "Number of ops (including micro ops) fetched (thread level)"),
    ADD_STAT(fetchRate, statistics::units::Rate<
             statistics::units::Count, statistics::units::Cycle>::get(),
             "Number of inst fetches per cycle"),
    ADD_STAT(numBranches, statistics::units::Count::get(),
             "Number of branches fetched"),
    ADD_STAT(branchRate, statistics::units::Ratio::get(),
             "Number of branch fetches per cycle"),
    ADD_STAT(icacheStallCycles, statistics::units::Cycle::get(),
             "ICache total stall cycles"),
    ADD_STAT(numFetchSuspends, statistics::units::Count::get(),
             "Number of times Execute suspended instruction fetching")

{
    fetchRate
        .flags(statistics::total);

    numBranches
        .prereq(numBranches);

    branchRate
        .flags(statistics::total);

    icacheStallCycles
        .prereq(icacheStallCycles);

}

// means it is incremented in a vector indexing and not directly
BaseCPU::
ExecuteCPUStats::ExecuteCPUStats(statistics::Group *parent, int thread_id)
    : statistics::Group(parent, csprintf("executeStats%i", thread_id).c_str()),
    ADD_STAT(numInsts, statistics::units::Count::get(),
             "Number of executed instructions"),
    ADD_STAT(numNop, statistics::units::Count::get(),
             "Number of nop insts executed"),
    ADD_STAT(numBranches, statistics::units::Count::get(),
             "Number of branches executed"),
    ADD_STAT(numLoadInsts, statistics::units::Count::get(),
             "Number of load instructions executed"),
    ADD_STAT(numStoreInsts, statistics::units::Count::get(),
             "Number of stores executed"),
    ADD_STAT(instRate, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
             "Inst execution rate"),
    ADD_STAT(dcacheStallCycles, statistics::units::Cycle::get(),
             "DCache total stall cycles"),
    ADD_STAT(numCCRegReads, statistics::units::Count::get(),
             "Number of times the CC registers were read"),
    ADD_STAT(numCCRegWrites, statistics::units::Count::get(),
             "Number of times the CC registers were written"),
    ADD_STAT(numFpAluAccesses, statistics::units::Count::get(),
             "Number of float alu accesses"),
    ADD_STAT(numFpRegReads, statistics::units::Count::get(),
             "Number of times the floating registers were read"),
    ADD_STAT(numFpRegWrites, statistics::units::Count::get(),
             "Number of times the floating registers were written"),
    ADD_STAT(numIntAluAccesses, statistics::units::Count::get(),
             "Number of integer alu accesses"),
    ADD_STAT(numIntRegReads, statistics::units::Count::get(),
             "Number of times the integer registers were read"),
    ADD_STAT(numIntRegWrites, statistics::units::Count::get(),
             "Number of times the integer registers were written"),
    ADD_STAT(numMemRefs, statistics::units::Count::get(),
             "Number of memory refs"),
    ADD_STAT(numMiscRegReads, statistics::units::Count::get(),
             "Number of times the Misc registers were read"),
    ADD_STAT(numMiscRegWrites, statistics::units::Count::get(),
             "Number of times the Misc registers were written"),
    ADD_STAT(numVecAluAccesses, statistics::units::Count::get(),
             "Number of vector alu accesses"),
    ADD_STAT(numVecPredRegReads, statistics::units::Count::get(),
             "Number of times the predicate registers were read"),
    ADD_STAT(numVecPredRegWrites, statistics::units::Count::get(),
             "Number of times the predicate registers were written"),
    ADD_STAT(numVecRegReads, statistics::units::Count::get(),
             "Number of times the vector registers were read"),
    ADD_STAT(numVecRegWrites, statistics::units::Count::get(),
             "Number of times the vector registers were written"),
    ADD_STAT(numDiscardedOps, statistics::units::Count::get(),
             "Number of ops (including micro ops) which were discarded before "
             "commit")
{
    numStoreInsts = numMemRefs - numLoadInsts;

    dcacheStallCycles
        .prereq(dcacheStallCycles);
    numCCRegReads
        .prereq(numCCRegReads)
        .flags(statistics::nozero);
    numCCRegWrites
        .prereq(numCCRegWrites)
        .flags(statistics::nozero);
    numFpAluAccesses
        .prereq(numFpAluAccesses);
    numFpRegReads
        .prereq(numFpRegReads);
    numIntAluAccesses
        .prereq(numIntAluAccesses);
    numIntRegReads
        .prereq(numIntRegReads);
    numIntRegWrites
        .prereq(numIntRegWrites);
    numMiscRegReads
        .prereq(numMiscRegReads);
    numMiscRegWrites
        .prereq(numMiscRegWrites);
    numVecPredRegReads
        .prereq(numVecPredRegReads);
    numVecPredRegWrites
        .prereq(numVecPredRegWrites);
    numVecRegReads
        .prereq(numVecRegReads);
    numVecRegWrites
        .prereq(numVecRegWrites);
}

BaseCPU::
CommitCPUStats::CommitCPUStats(statistics::Group *parent, int thread_id)
    : statistics::Group(parent, csprintf("commitStats%i", thread_id).c_str()),
    ADD_STAT(numInsts, statistics::units::Count::get(),
             "Number of instructions committed (thread level)"),
    ADD_STAT(numOps, statistics::units::Count::get(),
             "Number of ops (including micro ops) committed (thread level)"),
    ADD_STAT(numInstsNotNOP, statistics::units::Count::get(),
             "Number of instructions committed excluding NOPs or prefetches"),
    ADD_STAT(numOpsNotNOP, statistics::units::Count::get(),
             "Number of Ops (including micro ops) Simulated"),
    ADD_STAT(cpi, statistics::units::Rate<
                statistics::units::Cycle, statistics::units::Count>::get(),
             "CPI: cycles per instruction (thread level)"),
    ADD_STAT(ipc, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
             "IPC: instructions per cycle (thread level)"),
    ADD_STAT(numMemRefs, statistics::units::Count::get(),
            "Number of memory references committed"),
    ADD_STAT(numFpInsts, statistics::units::Count::get(),
            "Number of float instructions"),
    ADD_STAT(numIntInsts, statistics::units::Count::get(),
            "Number of integer instructions"),
    ADD_STAT(numLoadInsts, statistics::units::Count::get(),
            "Number of load instructions"),
    ADD_STAT(numStoreInsts, statistics::units::Count::get(),
            "Number of store instructions"),
    ADD_STAT(numVecInsts, statistics::units::Count::get(),
            "Number of vector instructions"),
    ADD_STAT(committedInstType, statistics::units::Count::get(),
            "Class of committed instruction."),
    ADD_STAT(committedControl, statistics::units::Count::get(),
             "Class of control type instructions committed")
{
    numInsts
        .prereq(numInsts);

    cpi.precision(6);
    ipc.precision(6);

    committedInstType
        .init(enums::Num_OpClass)
        .flags(statistics::total | statistics::pdf | statistics::dist);

    for (unsigned i = 0; i < Num_OpClasses; ++i) {
        committedInstType.subname(i, enums::OpClassStrings[i]);
    }

    committedControl
        .init(StaticInstFlags::Flags::Num_Flags)
        .flags(statistics::nozero);

    for (unsigned i = 0; i < StaticInstFlags::Flags::Num_Flags; i++) {
        committedControl.subname(i, StaticInstFlags::FlagsStrings[i]);
    }
}


void
BaseCPU::
CommitCPUStats::updateComCtrlStats(const StaticInstPtr staticInst)
{
    /* Add a count for every control instruction type */
    if (staticInst->isControl()) {
        if (staticInst->isReturn()) {
            committedControl[gem5::StaticInstFlags::Flags::IsReturn]++;
        }
        if (staticInst->isCall()) {
            committedControl[gem5::StaticInstFlags::Flags::IsCall]++;
        }
        if (staticInst->isDirectCtrl()) {
            committedControl[gem5::StaticInstFlags::Flags::IsDirectControl]++;
        }
        if (staticInst->isIndirectCtrl()) {
            committedControl
                [gem5::StaticInstFlags::Flags::IsIndirectControl]++;
        }
        if (staticInst->isCondCtrl()) {
            committedControl[gem5::StaticInstFlags::Flags::IsCondControl]++;
        }
        if (staticInst->isUncondCtrl()) {
            committedControl[gem5::StaticInstFlags::Flags::IsUncondControl]++;
        }
        committedControl[gem5::StaticInstFlags::Flags::IsControl]++;
    }
}

BaseCPU::
LoopCPUStats::LoopCPUStats(statistics::Group *parent, int loop_id)
    : statistics::Group(parent, csprintf("loopStats%i", loop_id).c_str()),
    ADD_STAT(commitSquashedInsts, statistics::units::Count::get(),
             "Number of squashed insts skipped by commit per loop"),
    ADD_STAT(commitRetiredInsts, statistics::units::Count::get(),
             "Number of retired insts processed by commit per loop"),
    ADD_STAT(recoverRate, statistics::units::Rate<
                statistics::units::Count, statistics::units::Count>::get(),
             "RecoverRate: SquashedInsts / (SquashedInsts + RetiredInsts) in ROB per loop"),
    ADD_STAT(numCycles, statistics::units::Cycle::get(),
             "Number of cpu cycles simulated per loop"),
    ADD_STAT(cpi, statistics::units::Rate<
                statistics::units::Cycle, statistics::units::Count>::get(),
             "CPI: cycles per instruction per loop"),
    ADD_STAT(ipc, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
             "IPC: instructions per cycle per loop"),
    ADD_STAT(bpu0MissCommitCount, statistics::units::Count::get(),
             "Number of squash times due to BPU0_miss in commit per loop"),
    ADD_STAT(bpu0MissDecodeCount, statistics::units::Count::get(),
             "Number of squash times due to BPU0_miss in decode per loop"),
    ADD_STAT(bpu1MissCommitCount, statistics::units::Count::get(),
             "Number of squash times due to BPU1_miss in commit per loop"),
    ADD_STAT(bpu1MissDecodeCount, statistics::units::Count::get(),
             "Number of squash times due to BPU1_miss in decode per loop"),
    ADD_STAT(rbkCount, statistics::units::Count::get(),
             "Number of squash times due to rollback per loop"),
    ADD_STAT(retiredBranchInsts, statistics::units::Count::get(),
             "Number of retired branch insts processed by commit per loop"),
    ADD_STAT(bpu0MissRate, statistics::units::Rate<
                statistics::units::Count, statistics::units::Count>::get(),
             "BpuMissRate: bpu0MissCommitCount / retiredBranchInsts in ROB per loop"),
    ADD_STAT(bpu1MissRate, statistics::units::Rate<
                statistics::units::Count, statistics::units::Count>::get(),
             "BpuMissRate: bpu1MissCommitCount / retiredBranchInsts in ROB per loop"),
    ADD_STAT(xhcommitSquashedInsts, statistics::units::Count::get(),
             "Number of squashed insts skipped by commit per loop"),
    ADD_STAT(xhcommitRetiredInsts, statistics::units::Count::get(),
             "Number of retired insts processed by commit per loop"),
    ADD_STAT(xhrecoverRate, statistics::units::Rate<
                statistics::units::Count, statistics::units::Count>::get(),
             "RecoverRate: SquashedInsts / (SquashedInsts + RetiredInsts) in ROB per loop"),
    ADD_STAT(xhnumCycles, statistics::units::Cycle::get(),
             "Number of cpu cycles simulated per loop"),
    ADD_STAT(xhcpi, statistics::units::Rate<
                statistics::units::Cycle, statistics::units::Count>::get(),
             "CPI: cycles per instruction per loop"),
    ADD_STAT(xhipc, statistics::units::Rate<
                statistics::units::Count, statistics::units::Cycle>::get(),
             "IPC: instructions per cycle per loop"),
    ADD_STAT(xhbpuMissCommitCount, statistics::units::Count::get(),
             "Number of squash times due to BPU_miss in commit per loop"),
    ADD_STAT(xhbpuMissDecodeCount, statistics::units::Count::get(),
             "Number of squash times due to BPU_miss in decode per loop"),
    ADD_STAT(xhrbkCount, statistics::units::Count::get(),
             "Number of squash times due to rollback per loop"),
    ADD_STAT(xhretiredBranchInsts, statistics::units::Count::get(),
             "Number of retired branch insts processed by commit per loop"),
    ADD_STAT(xhbpuMissRate, statistics::units::Rate<
                statistics::units::Count, statistics::units::Count>::get(),
             "BpuMissRate: bpuMissCommitCount / retiredBranchInsts in ROB per loop")
{
    commitSquashedInsts
        .prereq(commitSquashedInsts);
    commitRetiredInsts
        .prereq(commitRetiredInsts);

    recoverRate.precision(6);

    numCycles
        .prereq(numCycles);

    cpi.precision(6);
    ipc.precision(6);

    bpu0MissCommitCount
        .prereq(bpu0MissCommitCount);
    bpu0MissDecodeCount
        .prereq(bpu0MissDecodeCount);
    bpu1MissCommitCount
        .prereq(bpu1MissCommitCount);
    bpu1MissDecodeCount
        .prereq(bpu1MissDecodeCount);

    rbkCount
        .prereq(rbkCount);

    retiredBranchInsts
        .prereq(retiredBranchInsts);

    bpu0MissRate.precision(6);
    bpu1MissRate.precision(6);

    xhcommitSquashedInsts
        .prereq(xhcommitSquashedInsts);
    xhcommitRetiredInsts
        .prereq(xhcommitRetiredInsts);

    xhrecoverRate.precision(6);

    xhnumCycles
        .prereq(xhnumCycles);

    xhcpi.precision(6);
    xhipc.precision(6);

    xhbpuMissCommitCount
        .prereq(xhbpuMissCommitCount);
    xhbpuMissDecodeCount
        .prereq(xhbpuMissDecodeCount);
    xhrbkCount
        .prereq(xhrbkCount);

    xhretiredBranchInsts
        .prereq(xhretiredBranchInsts);

    xhbpuMissRate.precision(6);
}

// std::pair<int, bool>
// BaseCPU::diffWithNEMU(ThreadID tid, InstSeqNum seq)
// {
//     int diff_at = DiffAt::NoneDiff;
//     bool npc_match = false;
//     bool is_mmio = diffInfo.curInstStrictOrdered;

//     if (diffInfo.inst->isStoreConditional()) {
//         diffAllStates->proxy->uarchstatus_cpy(&diffAllStates->diff.sync, DIFFTEST_TO_REF);
//     }
//     if (is_mmio) {
//         // ismmio
//         diffAllStates->diff.dynamic_config.ignore_illegal_mem_access = true;
//         diffAllStates->proxy->update_config(&diffAllStates->diff.dynamic_config);
//     }

//     if (diffAllStates->diff.will_handle_intr) {
//         diffAllStates->proxy->regcpy(diffAllStates->diff.nemu_reg, REF_TO_DIFFTEST);
//         diffAllStates->diff.nemu_this_pc = diffAllStates->diff.nemu_reg[DIFFTEST_THIS_PC];
//         diffAllStates->diff.will_handle_intr = false;
//     }
//     // difftest step start
//     DPRINTF(Diff, "Step NEMU\n");
//     diffAllStates->proxy->exec(1);
//     diffAllStates->proxy->regcpy(diffAllStates->diff.nemu_reg, REF_TO_DIFFTEST);

//     uint64_t next_pc = diffAllStates->diff.nemu_reg[DIFFTEST_THIS_PC];

//     // replace with "this pc" for checking
//     diffAllStates->diff.nemu_commit_inst_pc = diffAllStates->diff.nemu_this_pc;
//     diffAllStates->diff.nemu_this_pc = next_pc;
//     diffAllStates->diff.npc = next_pc;
//     // difftest step end

//     if (is_mmio) {
//         // ismmio
//         diffAllStates->diff.dynamic_config.ignore_illegal_mem_access = false;
//         diffAllStates->proxy->update_config(&diffAllStates->diff.dynamic_config);
//     }
//     auto gem5_pc = diffInfo.pc->instAddr();
//     auto nemu_pc = diffAllStates->diff.nemu_commit_inst_pc;
//     DPRINTF(Diff, "NEMU PC: %#10lx, GEM5 PC: %#10lx, inst: %s\n", nemu_pc,
//             gem5_pc,
//             diffInfo.inst->disassemble(diffInfo.pc->instAddr()).c_str());

//     // auto nemu_store_addr = referenceRegFile[DIFFTEST_STORE_ADDR];
//     // if (nemu_store_addr) {
//     //     DPRINTF(ValueCommit, "NEMU store addr: %#lx\n", nemu_store_addr);
//     // }

//     // uint8_t gem5_inst[5];
//     // uint8_t nemu_inst[9];

//     // int nemu_inst_len = referenceRegFile[DIFFTEST_RVC] ? 2 : 4;
//     // int gem5_inst_len = inst->pcState().compressed() ? 2 : 4;

//     // assert(inst->staticInst->asBytes(gem5_inst, 8));
//     // *reinterpret_cast<uint32_t *>(nemu_inst) =
//     //     htole<uint32_t>(referenceRegFile[DIFFTEST_INST_PAYLOAD]);

//     if (nemu_pc != gem5_pc) {
//         // warn("NEMU store addr: %#lx\n", nemu_store_addr);
//         DPRINTF(Diff, "Inst [sn:%lli]\n", seq);
//         DPRINTF(Diff, "Diff at %s, NEMU: %#lx, GEM5: %#lx\n", "PC", nemu_pc,
//                 gem5_pc);
//         if (!diff_at) {
//             diff_at = PCDiff;
//             DPRINTF(Diff, "GEM5 pc: %#lx, NEMU npc: %#lx\n", gem5_pc,
//                     diffAllStates->diff.npc);
//             if (diffAllStates->diff.npc == gem5_pc) {
//                 npc_match = true;
//             }
//         }
//     }
//     DPRINTF(Diff2, "pc %#x inst %#x @ %s\n", gem5_pc, diffInfo.pc->instAddr(),
//             diffInfo.inst->disassemble(diffInfo.pc->instAddr()));
//     DPRINTF(Diff, "Inst [sn:%lli] PC, NEMU: %#lx, GEM5: %#lx\n", seq, nemu_pc,
//             gem5_pc);

//     DPRINTF(Diff, "Inst [sn:%llu] @ %#lx in GEM5 is %s\n", seq,
//             diffInfo.pc->instAddr(),
//             diffInfo.inst->disassemble(diffInfo.pc->instAddr()));
//     auto machInst = dynamic_cast<RiscvISA::RiscvStaticInst &>(*diffInfo.inst).machInst;
//     DPRINTF(Diff, "MachInst: %#lx\n", machInst);
//     if (diffInfo.inst->numDestRegs() > 0) {
//         const auto &dest = diffInfo.inst->destRegIdx(0);
//         auto dest_tag = dest.index() + dest.isFloatReg() * 32;

//         if ((dest.isFloatReg() || dest.isIntReg()) && !dest.isZeroReg()) {
//             auto gem5_val = diffInfo.result;
//             // threadContexts[curThread]->getReg(dest);
//             auto nemu_val = diffAllStates->referenceRegFile[dest_tag];

//             DPRINTF(Diff, "At %s Ref value: %#lx, GEM5 value: %#lx\n",
//                     reg_name[dest_tag], nemu_val, gem5_val);

//             if (diffInfo.inst->isLoad()) {
//                 DPRINTF(Diff, "Load addr: %#lx\n", diffInfo.physEffAddr);
//             }
//             if (diffInfo.inst->isStore()) {
//                 DPRINTF(Diff, "Store addr: %#lx\n", diffInfo.physEffAddr);
//             }
//             if (gem5_val != nemu_val) {
//                 if (dest.isFloatReg() &&
//                     (gem5_val ^ nemu_val) == ((0xffffffffULL) << 32)) {
//                     DPRINTF(Diff,
//                             "Difference might be caused by box,"
//                             " ignore it\n");

//                 } else if (is_mmio) {
//                     DPRINTF(Diff,
//                             "Difference might be caused by read %s at %#lx,"
//                             " ignore it\n",
//                             "mmio", diffInfo.physEffAddr);
//                     diffAllStates->referenceRegFile[dest_tag] = gem5_val;
//                     diffAllStates->proxy->regcpy(diffAllStates->referenceRegFile, DUT_TO_REF);
//                 } else {
//                     for (int i = 0; i < diffInfo.inst->numSrcRegs(); i++) {
//                         const auto &src = diffInfo.inst->srcRegIdx(i);
//                         DPRINTF(Diff, "Src%d %s = %lx\n", i,
//                                 reg_name[src.index()],
//                                 diffInfo.getSrcReg(src));
//                         // threadContexts[curThread]->getReg(src));
//                     }
//                     bool skipCSR = false;
//                     for (auto iter : skipCSRs) {
//                         if ((machInst & 0xfff00073) == iter) {
//                             skipCSR = true;
//                             DPRINTF(Diff, "This is an csr instruction, skip!\n");
//                             diffAllStates->referenceRegFile[dest_tag] = gem5_val;
//                             diffAllStates->proxy->regcpy(diffAllStates->referenceRegFile, DUT_TO_REF);
//                             break;
//                         }
//                     }
//                     DPRINTF(Diff, "Inst src count: %u, dest count: %u\n",
//                             diffInfo.inst->numSrcRegs(),
//                             diffInfo.inst->numDestRegs());
//                     warn("Inst [sn:%lli] pc: %#lx\n", seq, diffInfo.pc->instAddr());
//                     warn("Diff at %s Ref value: %#lx, GEM5 value: %#lx\n",
//                          reg_name[dest_tag], nemu_val, gem5_val);
//                     if (!diff_at && !skipCSR)
//                         diff_at = ValueDiff;
//                 }
//             }
//         }

//         // always check some CSR regs
//         {
//             // mstatus
//             auto gem5_val = readMiscRegNoEffect(
//                 RiscvISA::MiscRegIndex::MISCREG_STATUS, tid);
//             // readMiscRegNoEffect(RiscvISA::MiscRegIndex::MISCREG_STATUS, 0);
//             auto ref_val = diffAllStates->referenceRegFile[DIFFTEST_MSTATUS];
//             if (gem5_val != ref_val) {
//                 warn("Inst [sn:%lli] pc:%s\n", seq, diffInfo.pc);
//                 warn("Diff at %s Ref value: %#lx, GEM5 value: %#lx\n",
//                      "mstatus", ref_val, gem5_val);
//                 if (!diff_at)
//                     diff_at = ValueDiff;
//             }
//             //stval
//             gem5_val = readMiscRegNoEffect(
//                 RiscvISA::MiscRegIndex::MISCREG_STVAL, tid);
//             ref_val = diffAllStates->referenceRegFile[DIFFTEST_STVAL];
//             if (gem5_val != ref_val) {
//                 warn("Inst [sn:%lli] pc:%s\n", seq, diffInfo.pc);
//                 warn("Diff at %s Ref value: %#lx, GEM5 value: %#lx\n", "stval",
//                      ref_val, gem5_val);
//                 if (!diff_at)
//                     diff_at = ValueDiff;
//             }
//              //DIFFTEST_STVDIFFTEST_STVAL
//             // mcause
//             gem5_val = readMiscRegNoEffect(
//                 RiscvISA::MiscRegIndex::MISCREG_MCAUSE, tid);
//             // readMiscRegNoEffect(RiscvISA::MiscRegIndex::MISCREG_MCAUSE, 0);
//             ref_val = diffAllStates->referenceRegFile[DIFFTEST_MCAUSE];
//             if (gem5_val != ref_val) {
//                 warn("Inst [sn:%lli] pc:%s\n", seq, diffInfo.pc);
//                 warn("Diff at %s Ref value: %#lx, GEM5 value: %#lx\n",
//                      "mcause", ref_val, gem5_val);
//                 if (!diff_at)
//                     diff_at = ValueDiff;
//             }
//             // satp
//             gem5_val =
//                 readMiscRegNoEffect(RiscvISA::MiscRegIndex::MISCREG_SATP, tid);
//             // readMiscRegNoEffect(RiscvISA::MiscRegIndex::MISCREG_SATP, 0);
//             ref_val = diffAllStates->referenceRegFile[DIFFTEST_SATP];
//             if (gem5_val != ref_val) {
//                 warn("Inst [sn:%lli] pc:%s\n", seq, diffInfo.pc);
//                 warn("Diff at %s Ref value: %#lx, GEM5 value: %#lx\n", "satp",
//                      ref_val, gem5_val);
//                 if (!diff_at)
//                     diff_at = ValueDiff;
//             }

//             // mie
//             gem5_val = readMiscReg(RiscvISA::MiscRegIndex::MISCREG_IE, tid);
//             // readMiscReg(RiscvISA::MiscRegIndex::MISCREG_IE, 0);
//             ref_val = diffAllStates->referenceRegFile[DIFFTEST_MIE];
//             if (gem5_val != ref_val) {
//                 warn("Inst [sn:%lli] pc:%s\n", seq, diffInfo.pc);
//                 warn("Diff at %s Ref value: %#lx, GEM5 value: %#lx\n", "mie",
//                      ref_val, gem5_val);
//                 if (!diff_at)
//                     diff_at = ValueDiff;
//             }
//             // mip
//             gem5_val = readMiscReg(RiscvISA::MiscRegIndex::MISCREG_IP, tid);
//             // readMiscReg(RiscvISA::MiscRegIndex::MISCREG_IP, 0);
//             ref_val = diffAllStates->referenceRegFile[DIFFTEST_MIP];
//             if (gem5_val != ref_val) {
//                 DPRINTF(Diff, "Inst [sn:%lli] pc:%s\n", seq, diffInfo.pc);
//                 DPRINTF(Diff, "Diff at %s Ref value: %#lx, GEM5 value: %#lx\n",
//                         "mip", ref_val, gem5_val);
//             }

//             if (diff_at != NoneDiff) {
//                 warn("Inst [sn:%llu] @ %#lx in GEM5 is %s\n", seq,
//                      diffInfo.pc->instAddr(),
//                      diffInfo.inst->disassemble(diffInfo.pc->instAddr()));
//                 if (diffInfo.inst->isLoad()) {
//                     warn("Load addr: %#lx\n", diffInfo.physEffAddr);
//                 }
//             }
//         }
//     }
//     return std::make_pair(diff_at, npc_match);
// }

// void
// BaseCPU::difftestStep(ThreadID tid, InstSeqNum seq)
// {
//     bool should_diff = false;
//     DPRINTF(DumpCommit, "[sn:%llu] %#lx, %s\n",
//             seq, diffInfo.pc->instAddr(), diffInfo.inst->disassemble(diffInfo.pc->instAddr()));
//     DPRINTF(Diff, "DiffTest step on inst pc: %#lx: %s\n",
//             diffInfo.pc->instAddr(),
//             diffInfo.inst->disassemble(diffInfo.pc->instAddr()));
//     // Keep an instruction count.
//     if (!diffInfo.inst->isMicroop() || diffInfo.inst->isLastMicroop()) {
//         should_diff = true;
//         if (!diffAllStates->hasCommit && diffInfo.pc->instAddr() == 0x80000000u) {
//             diffAllStates->hasCommit = true;
//             readGem5Regs();
//             diffAllStates->gem5RegFile[DIFFTEST_THIS_PC] = diffInfo.pc->instAddr();
//             fprintf(stderr, "Will start memcpy to NEMU from %#lx, size=%lu\n",
//                     (uint64_t)pmemStart, pmemSize);
//             diffAllStates->proxy->memcpy(
//                 0x80000000u, pmemStart + pmemSize * diffAllStates->diff.cpu_id,
//                 pmemSize, DUT_TO_REF);
//             fprintf(stderr, "Will start regcpy to NEMU\n");
//             diffAllStates->proxy->regcpy(diffAllStates->gem5RegFile, DUT_TO_REF);
//         }

//         if (diffAllStates->scFenceInFlight) {
//             assert(diffInfo.inst->isWriteBarrier() &&
//                    diffInfo.inst->isReadBarrier());
//             should_diff = false;
//         }
//     }

//     diffAllStates->scFenceInFlight = false;

//     if (!diffInfo.inst->isLastMicroop() &&
//         diffInfo.inst->isStoreConditional() &&
//         diffInfo.inst->isDelayedCommit()) {
//         diffAllStates->scFenceInFlight = true;
//         should_diff = true;
//     }

//     if (enableDifftest && should_diff) {
//         auto [diff_at, npc_match] = diffWithNEMU(tid, seq);
//         if (diff_at != NoneDiff) {
//             if (npc_match && diff_at == PCDiff) {
//                 // warn("Found PC mismatch, Let NEMU run one more
//                 // instruction\n");
//                 std::tie(diff_at, npc_match) = diffWithNEMU(tid, 0);
//                 if (diff_at != NoneDiff) {
//                     diffAllStates->proxy->isa_reg_display();
//                     panic("Difftest failed again!\n");
//                 } else {
//                     warn(
//                         "Difftest matched again, "
//                         "NEMU seems to commit the failed mem instruction\n");
//                 }
//             } else {
//                 diffAllStates->proxy->isa_reg_display();
//                 panic("Difftest failed!\n");
//             }
//         }
//     }
//     committedInstNum++;
//     if (dumpCommitFlag && committedInstNum >= dumpStartNum) {
//         committedInsts.push_back(std::make_pair(
//             diffInfo.pc->instAddr(),
//             diffInfo.inst->disassemble(diffInfo.pc->instAddr()).c_str()));
//     }
//     DPRINTF(Diff, "commit_pc: %s, committedInstNum: %d\n", diffInfo.pc, committedInstNum);
// }

// void
// BaseCPU::difftestRaiseIntr(uint64_t no)
// {
//     diffAllStates->diff.will_handle_intr = true;
//     diffAllStates->proxy->raise_intr(no);
// }

// void
// BaseCPU::clearGuideExecInfo()
// {
//     diffAllStates->diff.guide.force_raise_exception = false;
//     diffAllStates->diff.guide.force_set_jump_target = false;
// }

// void
// BaseCPU::enableDiffPrint()
// {
//     diffAllStates->diff.dynamic_config.debug_difftest = true;
//     diffAllStates->proxy->update_config(&diffAllStates->diff.dynamic_config);
// }

// void BaseCPU::setSCSuccess(bool success)
// {
//     diffAllStates->diff.sync.lrscValid = success;
// }

// void
// BaseCPU::setExceptionGuideExecInfo(uint64_t exception_num, uint64_t mtval,
//                           uint64_t stval, bool force_set_jump_target,
//                           uint64_t jump_target)
// {
//     // auto &gd = diffAllStates->diff.guide;
//     // gd.force_raise_exception = true;
//     // gd.exception_num = exception_num;
//     // gd.mtval = mtval;
//     // gd.stval = stval;
//     // gd.force_set_jump_target = force_set_jump_target;
//     // gd.jump_target = jump_target;

//     // // diffAllStates->diff.dynamic_config.debug_difftest = true;
//     // // diffAllStates->proxy->update_config(&diffAllStates->diff.dynamic_config);

//     // diffAllStates->proxy->guided_exec(&(diffAllStates->diff.guide));

//     // diffAllStates->proxy->regcpy(diffAllStates->diff.nemu_reg, REF_TO_DIFFTEST);
//     // diffAllStates->diff.nemu_this_pc = diffAllStates->diff.nemu_reg[DIFFTEST_THIS_PC];
//     // DPRINTF(Diff, "After guided exec on NEMU, new PC: %#lx\n", diffAllStates->diff.nemu_this_pc);

//     // diffAllStates->diff.dynamic_config.debug_difftest = false;
//     // diffAllStates->proxy->update_config(&diffAllStates->diff.dynamic_config);
// }

} // namespace gem5
