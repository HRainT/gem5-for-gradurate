// -- add by hongfei.liu ---------------------------------------
#include "cpu/rxuo3/IBandLB.hh"

#include "arch/generic/pcstate.hh"
#include "base/trace.hh"
#include "cpu/inst_seq.hh"
#include "cpu/rxuo3/dyn_inst.hh"
#include "cpu/rxuo3/limits.hh"
#include "debug/RxuActivity.hh"
#include "debug/RxuIBandLB.hh"
#include "debug/RxuO3PipeView.hh"
#include "params/BaseRxuO3CPU.hh"

// clang complains about std::set being overloaded with Packet::set if
// we open up the entire namespace std
using std::list;

namespace gem5
{

namespace rxuo3
{

IBandLB::IBandLB(CPU *_cpu, const BaseRxuO3CPUParams &params)
    : cpu(_cpu),
    //   branchPred(nullptr),
      IBWidth(params.IBWidth),
    //   LBWidth(params.LBWidth),
      IBSize(params.IBSize),
    //   LBSize(params.LBSize),
    //   MaxLoopNums(params.MaxLoopNums),
    //   LoopBufferUse(params.system->useLoopBuffer()),
      decodeToIBandLBDelay(params.decodeToIBandLBDelay),
      commitToIBandLBDelay(params.commitToIBandLBDelay),
      bpu1ToIBandLBDelay(params.bpu1ToIBandLBDelay),
      pcToIBandLBDelay(params.pcToIBandLBDelay),
      numThreads(params.numThreads),
      stats(_cpu)
{
    if (IBWidth > MaxWidth)
        fatal("IBWidth (%d) is larger than compiled limit (%d),\n"
             "\tincrease MaxWidth in src/cpu/rxuo3/limits.hh\n",
             IBWidth, static_cast<int>(MaxWidth));

    // branchPred = params.branchPred;
    // loopBufferStore = false;
    // loopBufferActive = false;
    // loopLayer = -1;
    // loopBufferSize = 0;
    // LoopBuffer.clear();
    // loop_PC.clear();
    // loopbody_stored.clear();

    for (int tid = 0; tid < MaxThreads; tid++) {
        stalls[tid] = {false};
        IBandLBStatus[tid] = Idle;
        lastStatus[tid] = Idle;
        freeEntries = IBSize;
    }
}

void
IBandLB::startupStage()
{
    resetStage();
}

void
IBandLB::clearStates(ThreadID tid)
{
    IBandLBStatus[tid] = Idle;
    lastStatus[tid] = Idle;
    stalls[tid].decode = false;

    instbuffer.clear();

    freeEntries = IBSize;
    // loopBufferStore = false;
    // loopBufferActive = false;
    // loopLayer = -1;
    // loopBufferSize = 0;
    // LoopBuffer.clear();
    // loop_PC.clear();
    // loopbody_stored.clear();
}

void
IBandLB::resetStage()
{
    _status = Inactive;

    instbuffer.clear();

    freeEntries = IBSize;
    // loopBufferStore = false;
    // loopBufferActive = false;
    // loopLayer = -1;
    // loopBufferSize = 0;
    // LoopBuffer.clear();
    // loop_PC.clear();
    // loopbody_stored.clear();

    // Setup status, make sure stall signals are clear.
    for (ThreadID tid = 0; tid < numThreads; ++tid) {

        IBandLBStatus[tid] = Idle;
        lastStatus[tid] = Idle;

        stalls[tid].decode = false;
    }
}

std::string
IBandLB::name() const
{
    return cpu->name() + ".IBandLB";
}

IBandLB::IBandLBStats::IBandLBStats(CPU *cpu)
    : statistics::Group(cpu, "IBandLB"),
      ADD_STAT(idleCycles, statistics::units::Cycle::get(),
               "Number of cycles IBandLB is idle"),
      ADD_STAT(blockedCycles, statistics::units::Cycle::get(),
               "Number of cycles IBandLB is blocked"),
      ADD_STAT(runCycles, statistics::units::Cycle::get(),
               "Number of cycles IBandLB is running"),
      ADD_STAT(squashCycles, statistics::units::Cycle::get(),
               "Number of cycles IBandLB is squashing"),
      ADD_STAT(instBufferFullEvents, statistics::units::Count::get(),
               "Number of times IBandLB has blocked due to inst buffer full"),              
      ADD_STAT(IBandLBedInsts, statistics::units::Count::get(),
               "Number of instructions handled by IBandLB"),
      ADD_STAT(squashedInsts, statistics::units::Count::get(),
               "Number of squashed instructions handled by IBandLB"),
    //   ADD_STAT(loopBufferFullEvents, statistics::units::Count::get(),
    //            "Number of times that the loopBuffer is full"),
      ADD_STAT(fromBpu1Insts, statistics::units::Count::get(),
               "Number of instructions from bpu1")
    //   ADD_STAT(fromLBInsts, statistics::units::Count::get(),
    //            "Number of instructions from LoopBuffer"),
    //   ADD_STAT(loopBufferActive, statistics::units::Count::get(),
    //            "Active times of LoopBuffer")
{
    idleCycles.prereq(idleCycles);
    blockedCycles.prereq(blockedCycles);
    runCycles.prereq(runCycles);
    squashCycles.prereq(squashCycles);
    instBufferFullEvents.prereq(instBufferFullEvents);
    IBandLBedInsts.prereq(IBandLBedInsts);
    squashedInsts.prereq(squashedInsts);
    // loopBufferFullEvents.prereq(loopBufferFullEvents);
    fromBpu1Insts.prereq(fromBpu1Insts);
    // fromLBInsts.prereq(fromLBInsts);
    // loopBufferActive.prereq(loopBufferActive);
}

void
IBandLB::setTimeBuffer(TimeBuffer<TimeStruct> *tb_ptr)
{
    timeBuffer = tb_ptr;

    // Setup wire to write information back to bpu1.
    toBpu1 = timeBuffer->getWire(0);

    // Create wires to get information from proper places in time buffer.
    fromDecode = timeBuffer->getWire(-decodeToIBandLBDelay);
    fromCommit = timeBuffer->getWire(-commitToIBandLBDelay);
}

void
IBandLB::setIBandLBQueue(TimeBuffer<IBandLBStruct> *iq_ptr)
{
    IBandLBQueue = iq_ptr;

    // Setup wire to write information to proper place in IBandLB queue.
    toDecode = IBandLBQueue->getWire(0);
}

void
IBandLB::setBpu1Queue(TimeBuffer<Bpu1Struct> *b1q_ptr)
{
    bpu1Queue = b1q_ptr;

    // Setup wire to read information from bpu1 queue.
    fromBpu1 = bpu1Queue->getWire(-bpu1ToIBandLBDelay);
}

// void 
// IBandLB::setPcQueue(TimeBuffer<PcStruct> *pq_ptr)
// {
//     pcQueue = pq_ptr;

//     // Setup wire to read information from fetch' PC.
//     fromFetch_PC = pcQueue->getWire(-pcToIBandLBDelay);
// }

void
IBandLB::setActiveThreads(std::list<ThreadID> *at_ptr)
{
    activeThreads = at_ptr;
}

void
IBandLB::drainSanityCheck() const
{
    assert(instbuffer.empty());
    for (ThreadID tid = 0; tid < numThreads; ++tid) {
        assert(insts[tid].empty());
    }

    // branchPred->drainSanityCheck();
}

bool
IBandLB::isDrained() const
{
    if (!instbuffer.empty()) {
        return false;
    }

    for (ThreadID tid = 0; tid < numThreads; ++tid) {
        if (!insts[tid].empty() || 
            (IBandLBStatus[tid] != Running && IBandLBStatus[tid] != Idle))
            return false;
    }
    return true;
}

void
IBandLB::tick()
{
    wroteToTimeBuffer = false;

    bool status_change = false;

    toDecodeIndex = 0;
    instBufferInsertNums = 0;

    list<ThreadID>::iterator threads = activeThreads->begin();
    list<ThreadID>::iterator end = activeThreads->end();

    DPRINTF(RxuIBandLB, "free entries = %d before processing.\n",
        freeEntries);

    // if (loopBufferActive) {
    //     DPRINTF(RxuIBandLB, "Generate instructions through loopbuffer this cycle.\n");
    //     generateInstsLB();
    // } else {
        DPRINTF(RxuIBandLB, "%i instructions from bpu1 this cycle.\n",
                fromBpu1->size);
        sortInsts();
    // }

    //Check stall and squash signals.
    while (threads != end) {
        ThreadID tid = *threads++;

        lastStatus[tid] = IBandLBStatus[tid];

        DPRINTF(RxuIBandLB,"Processing thread : %i\n",tid);
        status_change =  checkSignalsAndUpdate(tid) || status_change;

        iBandLB(status_change, tid);
    }

    DPRINTF(RxuIBandLB, "free entries = %d after processing.\n",
        freeEntries);

    if (status_change) {
        updateStatus();
    }

    if (wroteToTimeBuffer) {
        DPRINTF(RxuActivity, "Activity this cycle.\n");

        cpu->activityThisCycle();
    }

    DPRINTF(RxuIBandLB, "push %i instructions into inst buffer "
            "and send %i instructions to decode this cycle.\n",
                        instBufferInsertNums, toDecodeIndex);
}

void
IBandLB::iBandLB(bool &status_change, ThreadID tid)
{
    if (IBandLBStatus[tid] == Blocked) {
        InsertInsts(tid);
        ++stats.blockedCycles;
    } else if (IBandLBStatus[tid] == Squashing) {
        InsertInsts(tid);
        ++stats.squashCycles;
    } else if (IBandLBStatus[tid] == Running ||
        IBandLBStatus[tid] == Idle) {
        DPRINTF(RxuIBandLB, "[tid:%i] Not blocked, so attempting to run "
                "IBandLB stage.\n",tid);
        InsertInsts(tid);
        SendInsts(tid);
    }

    updateFreeEntries();
}

void
IBandLB::InsertInsts(ThreadID tid)
{
    int insts_available = insts[tid].size();
    DynInstPtr inst;

    for (int i = 0; i < insts_available; i++) {
        inst = insts[tid].front();
        insts[tid].pop_front();

        if (inst->isSquashed()) {
            DPRINTF(RxuIBandLB, "[tid:%i] Instruction %i with PC %s is "
                    "squashed, skipping.\n",
                    tid, inst->seqNum, inst->pcState());

            ++stats.squashedInsts;
            continue;
        }

        assert(inst);
        // DPRINTF(RxuIBandLB, "instx\n");
        // DynInstPtr & instx = inst;
        // DPRINTF(RxuIBandLB, "instx\n");

        // DPRINTF(RxuIBandLB, "insty\n");
        // DynInstPtr insty = inst;
        // DPRINTF(RxuIBandLB, "insty\n");

        DPRINTF(RxuIBandLB, "[tid:%i] Inserting instruction into inst buffer [sn:%lli] with "
        "PC %s, address: %s\n", tid, inst->seqNum, inst->pcState(), inst.get());

        DPRINTF(RxuIBandLB, "[tid:%i] Inserting instruction into inst buffer [sn:%lli] with "
        "PC %s\n", tid, inst->seqNum, inst->pcState());
        instbuffer.push_back(inst);
        ++instBufferInsertNums;
    }
}

void
IBandLB::SendInsts(ThreadID tid) 
{

    DPRINTF(RxuIBandLB, "[tid:%i] Start Sending instruction to decode.\n",tid);

    if (instbuffer.empty()) {
        DPRINTF(RxuIBandLB, "[tid:%i] No inst to send, breaking out"
                " early.\n",tid);
        IBandLBStatus[tid] = Idle;
        ++stats.idleCycles;
        return;
    }

    IBandLBStatus[tid] = Running;
    ++stats.runCycles;

    DynInstPtr inst;
    while ((toDecodeIndex < IBWidth) && (!instbuffer.empty())) {
        DPRINTF(RxuIBandLB, "instBuffer.size(): %i in pop before.\n",
                instbuffer.size());
        inst = instbuffer.front();
        instbuffer.pop_front();
        assert(inst);
        DPRINTF(RxuIBandLB, "instBuffer.size(): %i in pop after.\n",
                instbuffer.size());
        if (inst->isSquashed()) {
            DPRINTF(RxuIBandLB, "[tid:%i] Instruction %i with PC %s is "
                    "squashed, skipping.\n",
                    tid, inst->seqNum, inst->pcState());

            ++stats.squashedInsts;
            continue;
        }

        DPRINTF(RxuIBandLB, "[tid:%i] Sending instruction [sn:%lli] with "
                "PC %s, compressed: %d\n", tid, inst->seqNum, inst->pcState(), inst->pcState().as<RiscvISA::PCState>().compressed());

        toDecode->insts[toDecodeIndex] = inst;

        ++(toDecode->size);
        ++toDecodeIndex;
        ++stats.IBandLBedInsts;

#if TRACING_ON
        if (debug::RxuO3PipeView) {
            inst->IBandLBTick = curTick() - inst->fetchTick;
        }
#endif

    }

    // Record that IBandLB has written to the time buffer for activity
    // tracking.
    if (toDecodeIndex) {
        wroteToTimeBuffer = true;
    }

}

void 
IBandLB::updateFreeEntries()
{
    freeEntries = IBSize - instbuffer.size();
    if ((freeEntries - bpu1Queue->getWire(0)->size) <= 16) {
        toBpu1->IBandLBBlock[0] = true;
        toBpu1->IBandLBUnblock[0] = false;
        ++stats.instBufferFullEvents;
        DPRINTF(RxuIBandLB, "inst buffer is full.\n");
    } else {
        toBpu1->IBandLBBlock[0] = false;
        toBpu1->IBandLBUnblock[0] = true;
    }
    // if ((freeEntries - bpu1Queue->getWire(0)->size) <= 0) {
    //     ++stats.instBufferFullEvents;
    //     DPRINTF(RxuIBandLB, "inst buffer is full.\n");
    // }
    toBpu1->IBandLBInfo[0].freeEntries = freeEntries;
}

void
IBandLB::updateStatus()
{
    bool any_unblocking = false;

    list<ThreadID>::iterator threads = activeThreads->begin();
    list<ThreadID>::iterator end = activeThreads->end();

    while (threads != end) {
        ThreadID tid = *threads++;

        if ((lastStatus[tid] == Blocked) && (!stalls[tid].decode)) {
            any_unblocking = true;
            break;
        }
    }

    // IBandLB will have activity.
    if (any_unblocking) {
        if (_status == Inactive) {
            _status = Active;

            DPRINTF(RxuActivity, "Activating stage.\n");

            cpu->activateStage(CPU::IBandLBIdx);
        }
    } else {
        // If it's not unblocking, then IBandLB will not have any internal
        // activity.  Switch it to inactive.
        if (_status == Active) {
            _status = Inactive;
            DPRINTF(RxuActivity, "Deactivating stage.\n");

            cpu->deactivateStage(CPU::IBandLBIdx);
        }
    }
}

void
IBandLB::sortInsts()
{
    int insts_from_bpu1 = fromBpu1->size;
    DynInstPtr inst;
    for (int i = 0; i < insts_from_bpu1; ++i) {
        inst = fromBpu1->insts[i];

        if (inst->isSquashed()) {
            DPRINTF(RxuIBandLB, "[tid:%i] Instruction %i with PC %s is "
                    "squashed, skipping.\n",
                    inst->threadNumber, inst->seqNum, inst->pcState());

            ++stats.squashedInsts;
            continue;
        }

        ++stats.fromBpu1Insts;

        insts[inst->threadNumber].push_back(inst);
        // if (!LoopBufferUse) {
        //     continue;
        // }
        // // Get the this_pc and next_pc of this instruction
        // PCStateBase &this_pc = *(inst->pc);
        // std::unique_ptr<PCStateBase> next_pc(this_pc.clone());
        // set(*next_pc, inst->readPredTarg());

        // //  If this instruction is a conditional branch instruction
        // if (inst->isCondCtrl()) {
        //     DPRINTF(RxuIBandLB, "Conditional branch detected with PC = %s\n", this_pc);
        //     // Check if the current PC has been recorded by LoopTable and has the same destination PC
        //     bool isLoop = this_pc.instAddr() >= inst->readPredTarg().instAddr();
        //     // auto loop_it = cpu->ew.loopTable.find(this_pc.instAddr());
        //     // if (loop_it != cpu->ew.loopTable.end()) {
        //     //     if (loop_it->second.target_pc == inst->readPredTarg().instAddr()
        //     //         && loop_it->second.times >= 1) {
        //     //         isLoop = true;
        //     //     }
        //     // }
        //     if (isLoop && loop_PC.size() < MaxLoopNums) {
        //         // If loop_PC is empty at this time, it indicates that this is 
        //         // the outermost loop branch instruction, starting to store the loop body
        //         if (loop_PC.empty()) {
        //             loopBufferStore = true;
        //             DPRINTF(RxuIBandLB, "LoopBuffer starts store LoopBody instructions.\n");
        //             DPRINTF(RxuIBandLB, "First loop branch PC: %s.\n", this_pc);
        //         }
                
        //         // Check if loopPC has already recorded this loop branch instruction
        //         bool PC_marked = false;
        //         for (int i = 0; i < loop_PC.size(); i++) {
        //             if (this_pc.instAddr() == loop_PC.at(i)) {
        //                 PC_marked = true;
        //                 break;
        //             }
        //         }
        //         // If not recorded, initialize a layer of loop body
        //         if (!PC_marked) {
        //             loop_PC.push_back(this_pc.instAddr()); 
        //             DPRINTF(RxuIBandLB, "Current loop branch PC: %s.\n", this_pc);
        //             loopbody_stored.push_back(false); 
        //             loopLayer = loop_PC.size() - 1;  
        //             std::map<Addr, StaticInstPtr> loopBody; 
        //             LoopBuffer.push_back(loopBody);
        //         }
        //     }
        // }

        // // If the loopbuffer is storing loop body instructions
        // if (loopBufferStore) {
        //     bool stored = false;
        //     // Check if the instruction corresponding to the current PC has been stored
        //     for (int layer = 0; layer < LoopBuffer.size(); layer++) {
        //         LoopBufferIt inst_it = LoopBuffer.at(layer).find(this_pc.instAddr());
        //         if (inst_it != LoopBuffer.at(layer).end()) {
        //             stored = true;
        //             break;
        //         }
        //     }
        //     // If this instruction has not been stored, enter the storage logic
        //     if (!stored) {

        //         if (loopBufferSize >= LBSize) {
        //             // The loop body has only one layer, and it is too large to stop storing
        //             if (LoopBuffer.size() <= 1) {
        //                 DPRINTF(RxuIBandLB, "LoopBufferStore stopped due to loop body too large.\n"); 
        //                 loopBufferStore = false;
        //                 loopBufferSize = 0;
        //                 loopLayer = -1;
        //                 loopbody_stored.clear();
        //                 loop_PC.clear();
        //                 LoopBuffer.clear();
        //                 ++stats.loopBufferFullEvents;
        //             } else {
        //             // Nested loop, if the number of instructions for the outer and inner layers exceeds LBsize, discard the outermost layer
        //                 DPRINTF(RxuIBandLB, "Discard the outermost loop body.\n");
        //                 loopBufferSize -= LoopBuffer.at(0).size();
        //                 loopbody_stored.pop_front();
        //                 loop_PC.pop_front(); 
        //                 loopLayer--;
        //             }
        //         } 
        //         // After discarding the outermost loop body, if looplayer==-1, it indicates that the loop body has ended and storage is stopped
        //         if (loopLayer == -1) {
        //             loopBufferStore = false;
        //             loopBufferSize = 0;
        //             loopbody_stored.clear();
        //             LoopBuffer.clear();
        //             loop_PC.clear();
        //         } else {
        //         // Save loop body instructions to the corresponding level
        //             LoopBuffer.at(loopLayer)[this_pc.instAddr()] = inst->staticInst;
        //             loopBufferSize++;
        //         }

        //     }

        //     // Encountering the innermost loop branch instruction recorded again
        //     if (!loop_PC.empty() && (this_pc.instAddr() == loop_PC.at(loopLayer)) 
        //         && stored) {
        //         DPRINTF(RxuIBandLB, "Loop branch PC: %s done.\n", this_pc);
        //         loopbody_stored[loopLayer] = true;
        //         // Find the innermost loop level for incomplete storage
        //         for (;loopLayer >= 0; loopLayer--) {
        //             if (!loopbody_stored[loopLayer]) {
        //                 break;
        //             }
        //         }      
        //         // The outermost loop body instruction has also been saved. Activate the Loop Buffer and pass a sleep signal to the bpu1                                       
        //         if (loopLayer == -1) {
        //             loopBufferStore = false;
        //             DPRINTF(RxuIBandLB, "LoopBuffer active.\n");
        //             DPRINTF(RxuIBandLB, "Outermostloop branch PC: %s.\n", this_pc);
        //             DPRINTF(RxuIBandLB, "LoopBuffer PC nums: %i.\n", loopBufferSize);    
        //             DPRINTF(RxuIBandLB, "Loop_PC nums: %i.\n", loop_PC.size());                  
        //             loopBufferActive = true;
        //             loopbody_stored.clear();
        //             cpu->fetch.loopBufferActive = true;
        //             cpu->fetch.doSquash(*next_pc, inst, inst->threadNumber);
        //             cpu->removeInstsUntil(inst->seqNum, inst->threadNumber);
        //             ++stats.loopBufferActive;
        //         }
        //     }
        // }
    }
}

// void
// IBandLB::generateInstsLB()
// {
//     // if (!fromFetch_PC->valid) {
//     //     for (int i = 0; i < fromBpu1->size; i++) {
//     //         insts[fromBpu1->insts[i]->threadNumber].push_back(fromBpu1->insts[i]);
//     //     }
//     //     return;
//     // }
//     if (fromCommit->commitInfo[0].squash ||
//         fromDecode->decodeInfo[0].squash) {
//             return;
//     }
//     if (freeEntries <= 0) {
//         return;
//     }
//     PCStateBase &this_PC = *(cpu->fetch.pc[0]);
//     std::unique_ptr<PCStateBase> next_PC(this_PC.clone());
//     StaticInstPtr static_inst;
//     int Nums = freeEntries > LBWidth ? LBWidth : freeEntries;
//     for (int i = 0; i < Nums; i++) {
//         bool found = false;
//         for (int layer = 0; layer < LoopBuffer.size(); layer++) {
//             LoopBufferIt inst_it = LoopBuffer.at(layer).find(this_PC.instAddr());
//             if (inst_it != LoopBuffer.at(layer).end()) {
//                 found = true;
//                 static_inst = (*inst_it).second;
//                 break;
//             }
//         }
//         if (found) {
//             DynInstPtr instruction = buildInst(
//             0, static_inst, NULL, this_PC, *next_PC, true);
//             set(next_PC, this_PC);
//             lookupAndUpdateNextPC(instruction, *next_PC);
//             set(this_PC, *next_PC);
//         } else {
//             DPRINTF(RxuIBandLB, "Instruction PC %s loopbuffer miss.\n", this_PC);
//             DPRINTF(RxuIBandLB, "LoopBuffer done.\n");
//             loopBufferActive = false; 
//             LoopBuffer.clear();
//             loopBufferSize = 0;
//             loop_PC.clear();
//             loopbody_stored.clear();
//             loopLayer = -1;

//             cpu->fetch.loopBufferActive = false;
//             break;
//         }
//     }

//     set(cpu->fetch.pc[0], this_PC);
// }

// DynInstPtr
// IBandLB::buildInst(ThreadID tid, StaticInstPtr staticInst,
//         StaticInstPtr curMacroop, const PCStateBase &this_pc,
//         const PCStateBase &next_pc, bool trace)
// {
//     // Get a sequence number.
//     InstSeqNum seq = cpu->getAndIncrementInstSeq();

//     DynInst::Arrays arrays;
//     arrays.numSrcs = staticInst->numSrcRegs();
//     arrays.numDests = staticInst->numDestRegs();

//     // Create a new DynInst
//     DynInstPtr instruction = new (arrays) DynInst(
//             arrays, staticInst, curMacroop, this_pc, next_pc, seq, cpu);
//     instruction->setTid(tid);

//     instruction->setThreadState(cpu->thread[tid]);

//     DPRINTF(RxuIBandLB, "[tid:%i] Instruction PC %s created [sn:%lli].\n",
//             tid, this_pc, seq);

//     DPRINTF(RxuIBandLB, "[tid:%i] Instruction is: %s\n", tid,
//             instruction->staticInst->disassemble(this_pc.instAddr()));

// #if TRACING_ON
//     if (trace) {
//         instruction->traceData =
//             cpu->getTracer()->getInstRecord(curTick(), cpu->tcBase(tid),
//                     instruction->staticInst, this_pc, curMacroop);
//     }
// #else
//     instruction->traceData = NULL;
// #endif

//     // Add instruction to the CPU's list of instructions.
//     instruction->setInstListIt(cpu->addInst(instruction));

//     insts[tid].push_back(instruction);

//     ++stats.fromLBInsts;

//     return instruction;
// }

void
IBandLB::readStallSignals(ThreadID tid)
{
    if (fromDecode->decodeBlock[tid]) {
        stalls[tid].decode = true;
    }

    if (fromDecode->decodeUnblock[tid]) {
        stalls[tid].decode = false;
    }
}

bool
IBandLB::checkSignalsAndUpdate(ThreadID tid)
{
    readStallSignals(tid);

    if (fromCommit->commitInfo[tid].squash) {

        DPRINTF(RxuIBandLB, "[tid:%i] Squashing instructions due to squash "
                "from commit.\n", tid);

        squash(tid, fromCommit->commitInfo[tid].doneSeqNum);

        return true;
    } 

    // Check squash signals from decode.
    if (fromDecode->decodeInfo[tid].squash) {
        DPRINTF(RxuIBandLB, "[tid:%i] Squashing instructions due to squash "
                "from decode.\n",tid);

        if (IBandLBStatus[tid] != Squashing) {

            DPRINTF(RxuIBandLB, "Squashing from decode with PC = %s\n",
                *fromDecode->decodeInfo[tid].nextPC);

            IBandLBStatus[tid] = Squashing;
            squash(tid, fromDecode->decodeInfo[tid].doneSeqNum);

            return true;
        }
    }

    if (checkStall(tid)) {   
        return block(tid);
    }

    if (IBandLBStatus[tid] == Blocked) {
        DPRINTF(RxuIBandLB, "[tid:%i] Done blocking, switching to running.\n",
                tid);

        unblock(tid);

        return true;
    }

    if (IBandLBStatus[tid] == Squashing) {
        // Switch status to running if IBandLB isn't being told to block or
        // squash this cycle.
        DPRINTF(RxuIBandLB, "[tid:%i] Done squashing, switching to running.\n",
                tid);

        IBandLBStatus[tid] = Running;

        return true;
    }

    // If we've reached this point, we have not gotten any signals that
    // cause IBandLB to change its status.  IBandLB remains the same as before.
    return false;
}

bool
IBandLB::checkStall(ThreadID tid) const
{
    bool ret_val = false;

    if (stalls[tid].decode) {
        DPRINTF(RxuIBandLB,"[tid:%i] Stall from Decode stage detected.\n", tid);
        ret_val = true;
    }

    return ret_val;
}

bool
IBandLB::bpu1InstsValid()
{
    return fromBpu1->size > 0;
}

bool
IBandLB:: block(ThreadID tid)
{
    DPRINTF(RxuIBandLB, 
            "[tid:%i] IBandLB can't send instruction to decode this cycle.\n", tid);

    if (IBandLBStatus[tid] != Blocked) {
        // Set the status to Blocked.
        IBandLBStatus[tid] = Blocked;

        return true;
    }

    return false;
}

void
IBandLB::unblock(ThreadID tid)
{
    IBandLBStatus[tid] = Running;
    wroteToTimeBuffer = true;
}

void
IBandLB::squash(ThreadID tid, const InstSeqNum &seq_num)
{
    DPRINTF(RxuIBandLB, "[tid:%i] Squashing.\n",tid);

    // Set status to squashing.
    IBandLBStatus[tid] = Squashing;

    // Go through incoming instructions from bpu1 and squash them.
    for (int i = 0; i < fromBpu1->size; i++) {
        if ((fromBpu1->insts[i]->threadNumber == tid) &&
            (fromBpu1->insts[i]->seqNum > seq_num)) {

            fromBpu1->insts[i]->setSquashed();

            wroteToTimeBuffer = true;
        }
    }

    DynInstPtr inst;
    int instNums;

    instNums = insts[tid].size();

    for (int i = 0; i < instNums; i++) {

        inst = insts[tid].front();
        insts[tid].pop_front();
        if (inst->seqNum > seq_num) {
            inst->setSquashed();
            ++stats.squashedInsts;
        } else {
            insts[tid].push_back(inst);
        }
    }

    instNums = instbuffer.size();

    for (int i = 0; i < instNums; i++) {

        inst = instbuffer.front();
        instbuffer.pop_front();
        if (inst->seqNum > seq_num) {
            inst->setSquashed();
            ++stats.squashedInsts;
        } else {
            instbuffer.push_back(inst);
        }
    }
}

// bool
// IBandLB::lookupAndUpdateNextPC(const DynInstPtr &inst, PCStateBase &next_pc)
// {
//     bool predict_taken;

//     if (!inst->isControl()) {
//         inst->staticInst->advancePC(next_pc);
//         inst->setPredTarg(next_pc);
//         inst->setPredTaken(false);
//         return false;
//     }

//     ThreadID tid = inst->threadNumber;
//     predict_taken = cpu->fetch.branchPred->predict(inst->staticInst, inst->seqNum,
//                                         next_pc, tid);


//     if (predict_taken) {
//         DPRINTF(RxuIBandLB, "[tid:%i] [sn:%llu] Branch at PC %#x "
//                 "predicted to be taken to %s\n",
//                 tid, inst->seqNum, inst->pcState().instAddr(), next_pc);
//     } else {
//         DPRINTF(RxuIBandLB, "[tid:%i] [sn:%llu] Branch at PC %#x "
//                 "predicted to be not taken\n",
//                 tid, inst->seqNum, inst->pcState().instAddr());
//     }

//     DPRINTF(RxuIBandLB, "[tid:%i] [sn:%llu] Branch at PC %#x "
//             "predicted to go to %s\n",
//             tid, inst->seqNum, inst->pcState().instAddr(), next_pc);
//     inst->setPredTarg(next_pc);
//     inst->setPredTaken(predict_taken);

//     return predict_taken;
// }

} // namespace rxuo3
} // namespace gem5
// -----------------------------------------------------------------------------------