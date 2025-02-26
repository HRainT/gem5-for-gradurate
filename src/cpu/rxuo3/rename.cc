#include "cpu/rxuo3/rename.hh"

#include <list>

#include "cpu/rxuo3/cpu.hh"
#include "cpu/rxuo3/dyn_inst.hh"
#include "cpu/rxuo3/limits.hh"
#include "cpu/reg_class.hh"
#include "debug/RxuActivity.hh"
#include "debug/RxuO3PipeView.hh"
#include "debug/RxuRename.hh"
#include "params/BaseRxuO3CPU.hh"

namespace gem5
{

namespace rxuo3
{

Rename::Rename(CPU *_cpu, const BaseRxuO3CPUParams &params)
    : cpu(_cpu),
    //   iewToRenameDelay(params.iewToRenameDelay),
    //   // -- add by hongfei.liu --------------------------
    //   // decodeToRenameDelay(params.decodeToRenameDelay),
    //   predisqToRenameDelay(params.predisqToRenameDelay),
    //   // ------------------------------------------------
    //   commitToRenameDelay(params.commitToRenameDelay),
      renameWidth(params.dispipe0Width),
      numThreads(params.numThreads),
      stats(_cpu)
{
    if (renameWidth > MaxWidth)
        fatal("renameWidth (%d) is larger than compiled limit (%d),\n"
             "\tincrease MaxWidth in src/cpu/rxuo3/limits.hh\n",
             renameWidth, static_cast<int>(MaxWidth));

    // @todo: Make into a parameter.
    // -- add by hongfei.liu ---------------------------------------
    //  skidBufferMax = (decodeToRenameDelay + 1) * params.decodeWidth;
        skidBufferMax = (predisqToRenameDelay + 1) * params.predisqWidth;
    // -------------------------------------------------------------
    for (uint32_t tid = 0; tid < MaxThreads; tid++) {
        renameStatus[tid] = Idle;
        renameMap[tid] = nullptr;
        instsInProgress[tid] = 0;
        loadsInProgress[tid] = 0;
        storesInProgress[tid] = 0;
        freeEntries[tid] = {0, 0, 0, 0};
        emptyROB[tid] = true;
        stalls[tid] = {false, false};
        serializeInst[tid] = nullptr;
        serializeOnNextInst[tid] = false;
    }
}

std::string
Rename::name() const
{
    return cpu->name() + ".rename";
}

Rename::RenameStats::RenameStats(statistics::Group *parent)
    : statistics::Group(parent, "rename"),
      ADD_STAT(squashCycles, statistics::units::Cycle::get(),
               "Number of cycles rename is squashing"),
      ADD_STAT(idleCycles, statistics::units::Cycle::get(),
               "Number of cycles rename is idle"),
      ADD_STAT(blockCycles, statistics::units::Cycle::get(),
               "Number of cycles rename is blocking"),
      ADD_STAT(serializeStallCycles, statistics::units::Cycle::get(),
               "count of cycles rename stalled for serializing inst"),
      ADD_STAT(runCycles, statistics::units::Cycle::get(),
               "Number of cycles rename is running"),
      ADD_STAT(unblockCycles, statistics::units::Cycle::get(),
               "Number of cycles rename is unblocking"),
      ADD_STAT(renamedInsts, statistics::units::Count::get(),
               "Number of instructions processed by rename"),
      ADD_STAT(squashedInsts, statistics::units::Count::get(),
               "Number of squashed instructions processed by rename"),
      ADD_STAT(ROBFullEvents, statistics::units::Count::get(),
               "Number of times rename has blocked due to ROB full"),
      ADD_STAT(IQFullEvents, statistics::units::Count::get(),
               "Number of times rename has blocked due to IQ full"),
      ADD_STAT(LQFullEvents, statistics::units::Count::get(),
               "Number of times rename has blocked due to LQ full" ),
      ADD_STAT(SQFullEvents, statistics::units::Count::get(),
               "Number of times rename has blocked due to SQ full"),
      ADD_STAT(fullRegistersEvents, statistics::units::Count::get(),
               "Number of times there has been no free registers"),
      ADD_STAT(renamedOperands, statistics::units::Count::get(),
               "Number of destination operands rename has renamed"),
      ADD_STAT(lookups, statistics::units::Count::get(),
               "Number of register rename lookups that rename has made"),
      ADD_STAT(intLookups, statistics::units::Count::get(),
               "Number of integer rename lookups"),
      ADD_STAT(fpLookups, statistics::units::Count::get(),
               "Number of floating rename lookups"),
      ADD_STAT(vecLookups, statistics::units::Count::get(),
               "Number of vector rename lookups"),
      ADD_STAT(vecPredLookups, statistics::units::Count::get(),
               "Number of vector predicate rename lookups"),
      ADD_STAT(matLookups, statistics::units::Count::get(),
               "Number of matrix rename lookups"),
      ADD_STAT(committedMaps, statistics::units::Count::get(),
               "Number of HB maps that are committed"),
      ADD_STAT(undoneMaps, statistics::units::Count::get(),
               "Number of HB maps that are undone due to squashing"),
      ADD_STAT(serializing, statistics::units::Count::get(),
               "count of serializing insts renamed"),
      ADD_STAT(tempSerializing, statistics::units::Count::get(),
               "count of temporary serializing insts renamed"),
      ADD_STAT(skidInsts, statistics::units::Count::get(),
               "count of insts added to the skid buffer")
{
    squashCycles.prereq(squashCycles);
    idleCycles.prereq(idleCycles);
    blockCycles.prereq(blockCycles);
    serializeStallCycles.flags(statistics::total);
    runCycles.prereq(idleCycles);
    unblockCycles.prereq(unblockCycles);

    renamedInsts.prereq(renamedInsts);
    squashedInsts.prereq(squashedInsts);

    ROBFullEvents.prereq(ROBFullEvents);
    IQFullEvents.prereq(IQFullEvents);
    LQFullEvents.prereq(LQFullEvents);
    SQFullEvents.prereq(SQFullEvents);
    fullRegistersEvents.prereq(fullRegistersEvents);

    renamedOperands.prereq(renamedOperands);
    lookups.prereq(lookups);
    intLookups.prereq(intLookups);
    fpLookups.prereq(fpLookups);
    vecLookups.prereq(vecLookups);
    vecPredLookups.prereq(vecPredLookups);
    matLookups.prereq(matLookups);

    committedMaps.prereq(committedMaps);
    undoneMaps.prereq(undoneMaps);
    serializing.flags(statistics::total);
    tempSerializing.flags(statistics::total);
    skidInsts.flags(statistics::total);
}

void
Rename::regProbePoints()
{
    ppRename = new ProbePointArg<DynInstPtr>(
            cpu->getProbeManager(), "Rename");
    ppSquashInRename = new ProbePointArg<SeqNumRegPair>(cpu->getProbeManager(),
                                                        "SquashInRename");
}

void 
Rename::setFrm(RxuFRMRename *frm_ptr)
{
    frm = frm_ptr;
}

void
Rename::setTimeBuffer(TimeBuffer<TimeStruct> *tb_ptr)
{
    timeBuffer = tb_ptr;

    // Setup wire to read information from time buffer, from IEW stage.
    fromIEW = timeBuffer->getWire(-iewToRenameDelay);

    // Setup wire to read infromation from time buffer, from commit stage.
    fromCommit = timeBuffer->getWire(-commitToRenameDelay);

    // Setup wire to write information to previous stages.
    // -- add by hongfei.liu --------------
    // toDecode = timeBuffer->getWire(0);
    toPredisq = timeBuffer->getWire(0);
    // ------------------------------------
}

// void
// Rename::setRenameQueue(TimeBuffer<RenameStruct> *rq_ptr)
// {
//     renameQueue = rq_ptr;

//     // Setup wire to write information to future stages.
//     toIEW = renameQueue->getWire(0);
// }

// -- add by hongfei.liu ------------------------------------
// void
// Rename::setDecodeQueue(TimeBuffer<DecodeStruct> *dq_ptr)
// {
//     decodeQueue = dq_ptr;

//     // Setup wire to get information from decode.
//     fromDecode = decodeQueue->getWire(-decodeToRenameDelay);
// }

void
Rename::setPredisqQueue(TimeBuffer<PredisqStruct> *pq_ptr)
{
    predisqQueue = pq_ptr;

    // Setup wire to get information from predisq.
    fromPredisq = predisqQueue->getWire(-predisqToRenameDelay);
}
// ----------------------------------------------------------

void
Rename::startupStage()
{
    resetStage();
}

void
Rename::clearStates(ThreadID tid)
{
    renameStatus[tid] = Idle;

    // freeEntries[tid].iqEntries = iew_ptr->instQueue.numFreeEntries(tid);
    // freeEntries[tid].lqEntries = iew_ptr->ldstQueue.numFreeLoadEntries(tid);
    // freeEntries[tid].sqEntries = iew_ptr->ldstQueue.numFreeStoreEntries(tid);
    // freeEntries[tid].robEntries = commit_ptr->numROBFreeEntries(tid);
    emptyROB[tid] = true;

    stalls[tid].iew = false;
    serializeInst[tid] = NULL;

    instsInProgress[tid] = 0;
    loadsInProgress[tid] = 0;
    storesInProgress[tid] = 0;

    serializeOnNextInst[tid] = false;
}

void
Rename::resetStage()
{
    // _status = Inactive;

    resumeSerialize = false;
    resumeUnblocking = false;

    // // Grab the number of free entries directly from the stages.
    for (ThreadID tid = 0; tid < numThreads; tid++) {
    //     renameStatus[tid] = Idle;

    //     freeEntries[tid].iqEntries = iew_ptr->instQueue.numFreeEntries(tid);
    //     freeEntries[tid].lqEntries =
    //         iew_ptr->ldstQueue.numFreeLoadEntries(tid);
    //     freeEntries[tid].sqEntries =
    //         iew_ptr->ldstQueue.numFreeStoreEntries(tid);
    //     freeEntries[tid].robEntries = commit_ptr->numROBFreeEntries(tid);
    //     emptyROB[tid] = true;

    //     stalls[tid].iew = false;
    //     serializeInst[tid] = NULL;

    //     instsInProgress[tid] = 0;
    //     loadsInProgress[tid] = 0;
    //     storesInProgress[tid] = 0;

        serializeOnNextInst[tid] = false;
    }
}

void
Rename::setActiveThreads(std::list<ThreadID> *at_ptr)
{
    activeThreads = at_ptr;
}


void
Rename::setRenameMap(UnifiedRenameMap rm_ptr[])
{
    for (ThreadID tid = 0; tid < numThreads; tid++)
        renameMap[tid] = &rm_ptr[tid];
}

void
Rename::setFreeList(UnifiedFreeList *fl_ptr)
{
    freeList = fl_ptr;
}

void
Rename::setScoreboard(Scoreboard *_scoreboard)
{
    scoreboard = _scoreboard;
}

bool
Rename::isDrained() const
{
    for (ThreadID tid = 0; tid < numThreads; tid++) {
        if (instsInProgress[tid] != 0 ||
            !historyBuffer[tid].empty() ||
            !skidBuffer[tid].empty() ||
            !insts[tid].empty() ||
            (renameStatus[tid] != Idle && renameStatus[tid] != Running))
            return false;
    }
    return true;
}

void
Rename::takeOverFrom()
{
    resetStage();
}

void
Rename::drainSanityCheck() const
{
    for (ThreadID tid = 0; tid < numThreads; tid++) {
        assert(historyBuffer[tid].empty());
        assert(insts[tid].empty());
        assert(skidBuffer[tid].empty());
        assert(instsInProgress[tid] == 0);
    }
}

void
Rename::squash(const InstSeqNum &squash_seq_num, ThreadID tid)
{
    DPRINTF(RxuRename, "[tid:%i] [squash sn:%llu] Squashing instructions.\n",
        tid,squash_seq_num);

    // Clear the stall signal if rename was blocked or unblocking before.
    // If it still needs to block, the blocking should happen the next
    // cycle and there should be space to hold everything due to the squash.
    // if (renameStatus[tid] == Blocked ||
    //     renameStatus[tid] == Unblocking) {
    //     // -- add by hongfei.liu ------------
    //     // toDecode->renameUnblock[tid] = 1;
    //     toPredisq->renameUnblock[tid] = 1;
    //     // ----------------------------------

    //     resumeSerialize = false;
    //     serializeInst[tid] = NULL;
    // } else if (renameStatus[tid] == SerializeStall) {
    //     if (serializeInst[tid]->seqNum <= squash_seq_num) {
    //         DPRINTF(RxuRename, "[tid:%i] [squash sn:%llu] "
    //             "Rename will resume serializing after squash\n",
    //             tid,squash_seq_num);
    //         resumeSerialize = true;
    //         assert(serializeInst[tid]);
    //     } else {
    //         resumeSerialize = false;
    //         // -- add by hongfei.liu -------------
    //         // toDecode->renameUnblock[tid] = 1;
    //         toPredisq->renameUnblock[tid] = 1;
    //         // -----------------------------------

    //         serializeInst[tid] = NULL;
    //     }
    // }

    // // Set the status to Squashing.
    // renameStatus[tid] = Squashing;

    // // -- add by hongfei.liu -------------------------------------
    // // // Squash any instructions from decode.
    // // for (int i=0; i<fromDecode->size; i++) {
    // //     if (fromDecode->insts[i]->threadNumber == tid &&
    // //         fromDecode->insts[i]->seqNum > squash_seq_num) {
    // //         fromDecode->insts[i]->setSquashed();
    // //         wroteToTimeBuffer = true;
    // //     }

    // // }
    // // Squash any instructions from predisq.
    // for (int i=0; i<fromPredisq->size; i++) {
    //     if (fromPredisq->insts[i]->threadNumber == tid &&
    //         fromPredisq->insts[i]->seqNum > squash_seq_num) {
    //         fromPredisq->insts[i]->setSquashed();
    //         wroteToTimeBuffer = true;
    //     }

    // }
    // // ----------------------------------------------------------

    // // Clear the instruction list and skid buffer in case they have any
    // // insts in them.
    // insts[tid].clear();

    // // Clear the skid buffer in case it has any data in it.
    // skidBuffer[tid].clear();

    doSquash(squash_seq_num, tid);
}

void
Rename::tick()
{
    // wroteToTimeBuffer = false;

    // blockThisCycle = false;

    // bool status_change = false;

    // toIEWIndex = 0;

    // sortInsts();

    // std::list<ThreadID>::iterator threads = activeThreads->begin();
    // std::list<ThreadID>::iterator end = activeThreads->end();

    // // Check stall and squash signals.
    // while (threads != end) {
    //     ThreadID tid = *threads++;

    //     DPRINTF(RxuRename, "Processing [tid:%i]\n", tid);

    //     status_change = checkSignalsAndUpdate(tid) || status_change;

    //     rename(status_change, tid);
    // }

    // if (status_change) {
    //     updateStatus();
    // }

    // if (wroteToTimeBuffer) {
    //     DPRINTF(RxuActivity, "Activity this cycle.\n");
    //     cpu->activityThisCycle();
    // }

    // threads = activeThreads->begin();

    // while (threads != end) {
    //     ThreadID tid = *threads++;

    //     // If we committed this cycle then doneSeqNum will be > 0
    //     if (fromCommit->commitInfo[tid].doneSeqNum != 0 &&
    //         !fromCommit->commitInfo[tid].squash &&
    //         renameStatus[tid] != Squashing) {

    //         removeFromHistory(fromCommit->commitInfo[tid].doneSeqNum,
    //                               tid);
    //     }
    // }

    // // @todo: make into updateProgress function
    // for (ThreadID tid = 0; tid < numThreads; tid++) {
    //     instsInProgress[tid] -= fromIEW->iewInfo[tid].dispatched;
    //     loadsInProgress[tid] -= fromIEW->iewInfo[tid].dispatchedToLQ;
    //     storesInProgress[tid] -= fromIEW->iewInfo[tid].dispatchedToSQ;
    //     assert(loadsInProgress[tid] >= 0);
    //     assert(storesInProgress[tid] >= 0);
    //     assert(instsInProgress[tid] >=0);
    // }

}

void
Rename::rename(bool &status_change, ThreadID tid)
{
    // If status is Running or idle,
    //     call renameInsts()
    // If status is Unblocking,
    //     buffer any instructions coming from predisq
    //     continue trying to empty skid buffer
    //     check if stall conditions have passed

    if (renameStatus[tid] == Blocked) {
        ++stats.blockCycles;
    } else if (renameStatus[tid] == Squashing) {
        ++stats.squashCycles;
    } else if (renameStatus[tid] == SerializeStall) {
        ++stats.serializeStallCycles;
        // If we are currently in SerializeStall and resumeSerialize
        // was set, then that means that we are resuming serializing
        // this cycle.  Tell the previous stages to block.
        if (resumeSerialize) {
            resumeSerialize = false;
            block(tid);
            // -- add by hongfei.liu --------------
            // toDecode->renameUnblock[tid] = false;
            // toPredisq->renameUnblock[tid] = false;
            // ------------------------------------
        }
    } else if (renameStatus[tid] == Unblocking) {
        if (resumeUnblocking) {
            block(tid);
            resumeUnblocking = false;
            // -- add by hongfei.liu --------------
            // toDecode->renameUnblock[tid] = false;
            // toPredisq->renameUnblock[tid] = false;
            // ------------------------------------
        }
    }

    if (renameStatus[tid] == Running ||
        renameStatus[tid] == Idle) {
        DPRINTF(RxuRename,
                "[tid:%i] "
                "Not blocked, so attempting to run stage.\n",
                tid);

        renameInsts(tid);
    } else if (renameStatus[tid] == Unblocking) {
        renameInsts(tid);

        if (validInsts()) {
            // Add the current inputs to the skid buffer so they can be
            // reprocessed when this stage unblocks.
            skidInsert(tid);
        }

        // If we switched over to blocking, then there's a potential for
        // an overall status change.
        status_change = unblock(tid) || status_change || blockThisCycle;
    }
}

void
Rename::renameInsts(ThreadID tid)
{
    // Instructions can be either in the skid buffer or the queue of
    // instructions coming from predisq, depending on the status.
    int insts_available = renameStatus[tid] == Unblocking ?
        skidBuffer[tid].size() : insts[tid].size();

    // Check the predisq queue to see if instructions are available.
    // If there are no available instructions to rename, then do nothing.
    if (insts_available == 0) {
        DPRINTF(RxuRename, "[tid:%i] Nothing to do, breaking out early.\n",
                tid);
        // Should I change status to idle?
        ++stats.idleCycles;
        return;
    } else if (renameStatus[tid] == Unblocking) {
        ++stats.unblockCycles;
    } else if (renameStatus[tid] == Running) {
        ++stats.runCycles;
    }

    // Will have to do a different calculation for the number of free
    // entries.
    int free_rob_entries = calcFreeROBEntries(tid);
    int free_iq_entries  = calcFreeIQEntries(tid);
    int min_free_entries = free_rob_entries;

    FullSource source = ROB;

    if (free_iq_entries < min_free_entries) {
        min_free_entries = free_iq_entries;
        source = IQ;
    }

    // Check if there's any space left.
    if (min_free_entries <= 0) {
        DPRINTF(RxuRename,
                "[tid:%i] Blocking due to no free ROB/IQ/ entries.\n"
                "ROB has %i free entries.\n"
                "IQ has %i free entries.\n",
                tid, free_rob_entries, free_iq_entries);

        blockThisCycle = true;

        block(tid);

        incrFullStat(source);

        return;
    } else if (min_free_entries < insts_available) {
        DPRINTF(RxuRename,
                "[tid:%i] "
                "Will have to block this cycle. "
                "%i insts available, "
                "but only %i insts can be renamed due to ROB/IQ/LSQ limits.\n",
                tid, insts_available, min_free_entries);

        insts_available = min_free_entries;

        blockThisCycle = true;

        incrFullStat(source);
    }

    InstQueue &insts_to_rename = renameStatus[tid] == Unblocking ?
        skidBuffer[tid] : insts[tid];

    DPRINTF(RxuRename,
            "[tid:%i] "
            "%i available instructions to send iew.\n",
            tid, insts_available);

    // DPRINTF(RxuRename,
    //         "[tid:%i] "
    //         "%i insts pipelining from Rename | "
    //         "%i insts dispatched to IQ last cycle.\n",
    //         tid, instsInProgress[tid], fromIEW->iewInfo[tid].dispatched);

    // Handle serializing the next instruction if necessary.
    if (serializeOnNextInst[tid]) {
        if (emptyROB[tid] && instsInProgress[tid] == 0) {
            // ROB already empty; no need to serialize.
            serializeOnNextInst[tid] = false;
        } else if (!insts_to_rename.empty()) {
            insts_to_rename.front()->setSerializeBefore();
        }
    }

    int renamed_insts = 0;

    while (insts_available > 0 &&  toIEWIndex < renameWidth) {
        DPRINTF(RxuRename, "[tid:%i] Sending instructions to IEW.\n", tid);

        assert(!insts_to_rename.empty());

        DynInstPtr inst = insts_to_rename.front();

        //For all kind of instructions, check ROB and IQ first For load
        //instruction, check LQ size and take into account the inflight loads
        //For store instruction, check SQ size and take into account the
        //inflight stores

        if (inst->isLoad()) {
            if (calcFreeLQEntries(tid) <= 0) {
                DPRINTF(RxuRename, "[tid:%i] Cannot rename due to no free LQ\n",
                        tid);
                source = LQ;
                incrFullStat(source);
                break;
            }
        }

        if (inst->isStore() || inst->isAtomic()) {
            if (calcFreeSQEntries(tid) <= 0) {
                DPRINTF(RxuRename, "[tid:%i] Cannot rename due to no free SQ\n",
                        tid);
                source = SQ;
                incrFullStat(source);
                break;
            }
        }

        insts_to_rename.pop_front();

        if (renameStatus[tid] == Unblocking) {
            DPRINTF(RxuRename,
                    "[tid:%i] "
                    "Removing [sn:%llu] PC:%s from rename skidBuffer\n",
                    tid, inst->seqNum, inst->pcState());
        }

        if (inst->isSquashed()) {
            DPRINTF(RxuRename,
                    "[tid:%i] "
                    "instruction %i with PC %s is squashed, skipping.\n",
                    tid, inst->seqNum, inst->pcState());

            ++stats.squashedInsts;

            // Decrement how many instructions are available.
            --insts_available;

            continue;
        }

        DPRINTF(RxuRename,
                "[tid:%i] "
                "Processing instruction [sn:%llu] with PC %s.\n",
                tid, inst->seqNum, inst->pcState());

        // Check here to make sure there are enough destination registers
        // to rename to.  Otherwise block.
        if (!renameMap[tid]->canRename(inst)) {
            DPRINTF(RxuRename,
                    "Blocking due to "
                    " lack of free physical registers to rename to.\n");
            blockThisCycle = true;
            insts_to_rename.push_front(inst);
            ++stats.fullRegistersEvents;

            break;
        }

        // Handle serializeAfter/serializeBefore instructions.
        // serializeAfter marks the next instruction as serializeBefore.
        // serializeBefore makes the instruction wait in rename until the ROB
        // is empty.

        // In this model, IPR accesses are serialize before
        // instructions, and store conditionals are serialize after
        // instructions.  This is mainly due to lack of support for
        // out-of-order operations of either of those classes of
        // instructions.
        if (inst->isSerializeBefore() && !inst->isSerializeHandled()) {
            DPRINTF(RxuRename, "Serialize before instruction encountered.\n");

            if (!inst->isTempSerializeBefore()) {
                stats.serializing++;
                inst->setSerializeHandled();
            } else {
                stats.tempSerializing++;
            }

            // Change status over to SerializeStall so that other stages know
            // what this is blocked on.
            renameStatus[tid] = SerializeStall;

            serializeInst[tid] = inst;

            blockThisCycle = true;

            break;
        } else if ((inst->isStoreConditional() || inst->isSerializeAfter()) &&
                   !inst->isSerializeHandled()) {
            DPRINTF(RxuRename, "Serialize after instruction encountered.\n");

            stats.serializing++;

            inst->setSerializeHandled();

            serializeAfter(insts_to_rename, tid);
        }

        renameSrcRegs(inst, inst->threadNumber);

        renameDestRegs(inst, inst->threadNumber);

        if (inst->isAtomic() || inst->isStore()) {
            storesInProgress[tid]++;
        } else if (inst->isLoad()) {
            loadsInProgress[tid]++;
        }

        ++renamed_insts;
        // Notify potential listeners that source and destination registers for
        // this instruction have been renamed.
        ppRename->notify(inst);

        // Put instruction in rename queue.
        // toIEW->insts[toIEWIndex] = inst;
        // ++(toIEW->size);

        // Increment which instruction we're on.
        ++toIEWIndex;

        // Decrement how many instructions are available.
        --insts_available;
    }

    instsInProgress[tid] += renamed_insts;
    stats.renamedInsts += renamed_insts;

    // If we wrote to the time buffer, record this.
    if (toIEWIndex) {
        wroteToTimeBuffer = true;
    }

    // Check if there's any instructions left that haven't yet been renamed.
    // If so then block.
    if (insts_available) {
        blockThisCycle = true;
    }

    if (blockThisCycle) {
        block(tid);
        // -- add by hongfei.liu ----------------
        // toDecode->renameUnblock[tid] = false;
        // toPredisq->renameUnblock[tid] = false;
        // --------------------------------------
    }
}

void
Rename::skidInsert(ThreadID tid)
{
    DynInstPtr inst = NULL;

    while (!insts[tid].empty()) {
        inst = insts[tid].front();

        insts[tid].pop_front();

        assert(tid == inst->threadNumber);

        DPRINTF(RxuRename, "[tid:%i] Inserting [sn:%llu] PC: %s into Rename "
                "skidBuffer\n", tid, inst->seqNum, inst->pcState());

        ++stats.skidInsts;

        skidBuffer[tid].push_back(inst);
    }

    if (skidBuffer[tid].size() > skidBufferMax) {
        InstQueue::iterator it;
        warn("Skidbuffer contents:\n");
        for (it = skidBuffer[tid].begin(); it != skidBuffer[tid].end(); it++) {
            warn("[tid:%i] %s [sn:%llu].\n", tid,
                    (*it)->staticInst->disassemble(
                        inst->pcState().instAddr()),
                    (*it)->seqNum);
        }
        panic("Skidbuffer Exceeded Max Size");
    }
}

void
Rename::sortInsts()
{
    // -- add by hongfei.liu -----------------------------
    // int insts_from_decode = fromDecode->size;
    // for (int i = 0; i < insts_from_decode; ++i) {
    //     const DynInstPtr &inst = fromDecode->insts[i];
    int insts_from_predisq = fromPredisq->size;
    for (int i = 0; i < insts_from_predisq; ++i) {
        const DynInstPtr &inst = fromPredisq->insts[i];
    // ---------------------------------------------------
        insts[inst->threadNumber].push_back(inst);
#if TRACING_ON
        if (debug::RxuO3PipeView) {
            inst->renameTick = curTick() - inst->fetchTick;
        }
#endif
    }
}

bool
Rename::skidsEmpty()
{
    std::list<ThreadID>::iterator threads = activeThreads->begin();
    std::list<ThreadID>::iterator end = activeThreads->end();

    while (threads != end) {
        ThreadID tid = *threads++;

        if (!skidBuffer[tid].empty())
            return false;
    }

    return true;
}

void
Rename::updateStatus()
{
    // bool any_unblocking = false;

    std::list<ThreadID>::iterator threads = activeThreads->begin();
    std::list<ThreadID>::iterator end = activeThreads->end();

    while (threads != end) {
        ThreadID tid = *threads++;

        if (renameStatus[tid] == Unblocking) {
            // any_unblocking = true;
            break;
        }
    }

    // Rename will have activity if it's unblocking.
    // if (any_unblocking) {
    //     if (_status == Inactive) {
    //         _status = Active;

    //         DPRINTF(RxuActivity, "Activating stage.\n");

    //         cpu->activateStage(CPU::RenameIdx);
    //     }
    // } else {
    //     // If it's not unblocking, then rename will not have any internal
    //     // activity.  Switch it to inactive.
    //     if (_status == Active) {
    //         _status = Inactive;
    //         DPRINTF(RxuActivity, "Deactivating stage.\n");

    //         cpu->deactivateStage(CPU::RenameIdx);
    //     }
    // }
}

bool
Rename::block(ThreadID tid)
{
    DPRINTF(RxuRename, "[tid:%i] Blocking.\n", tid);

    // Add the current inputs onto the skid buffer, so they can be
    // reprocessed when this stage unblocks.
    skidInsert(tid);

    // Only signal backwards to block if the previous stages do not think
    // rename is already blocked.
    if (renameStatus[tid] != Blocked) {
        // If resumeUnblocking is set, we unblocked during the squash,
        // but now we're have unblocking status. We need to tell earlier
        // stages to block.
        if (resumeUnblocking || renameStatus[tid] != Unblocking) {
            // -- add by hongfei.liu ----------------
            // toDecode->renameBlock[tid] = true;
            // toDecode->renameUnblock[tid] = false;
            // toPredisq->renameBlock[tid] = true;
            // toPredisq->renameUnblock[tid] = false;
            // --------------------------------------
            wroteToTimeBuffer = true;
        }

        // Rename can not go from SerializeStall to Blocked, otherwise
        // it would not know to complete the serialize stall.
        if (renameStatus[tid] != SerializeStall) {
            // Set status to Blocked.
            renameStatus[tid] = Blocked;
            return true;
        }
    }

    return false;
}

bool
Rename::unblock(ThreadID tid)
{
    DPRINTF(RxuRename, "[tid:%i] Trying to unblock.\n", tid);

    // Rename is done unblocking if the skid buffer is empty.
    if (skidBuffer[tid].empty() && renameStatus[tid] != SerializeStall) {

        DPRINTF(RxuRename, "[tid:%i] Done unblocking.\n", tid);

        // -- add by hongfei.liu -----------------
        // toDecode->renameUnblock[tid] = true;
        // toPredisq->renameUnblock[tid] = true;
        // ---------------------------------------
        wroteToTimeBuffer = true;

        renameStatus[tid] = Running;
        return true;
    }

    return false;
}

void
Rename::doSquash(const InstSeqNum &squashed_seq_num, ThreadID tid)
{
    auto hb_it = historyBuffer[tid].begin();

    // After a syscall squashes everything, the history buffer may be empty
    // but the ROB may still be squashing instructions.
    // Go through the most recent instructions, undoing the mappings
    // they did and freeing up the registers.
    while (!historyBuffer[tid].empty() &&
           hb_it->instSeqNum > squashed_seq_num) {
        assert(hb_it != historyBuffer[tid].end());

        DPRINTF(RxuRename, "[tid:%i] Removing history entry with sequence "
                "number %i (archReg: %d, newPhysReg: %d, prevPhysReg: %d).\n",
                tid, hb_it->instSeqNum, hb_it->archReg.index(),
                hb_it->newPhysReg->index(), hb_it->prevPhysReg->index());

        // Undo the rename mapping only if it was really a change.
        // Special regs that are not really renamed (like misc regs
        // and the zero reg) can be recognized because the new mapping
        // is the same as the old one.  While it would be merely a
        // waste of time to update the rename table, we definitely
        // don't want to put these on the free list.
        if (hb_it->newPhysReg != hb_it->prevPhysReg) {
            // Tell the rename map to set the architected register to the
            // previous physical register that it was renamed to.
            renameMap[tid]->setEntry(hb_it->archReg, hb_it->prevPhysReg);

            // Put the renamed physical register back on the free list.
            freeList->addReg(hb_it->newPhysReg);
        }

        // Notify potential listeners that the register mapping needs to be
        // removed because the instruction it was mapped to got squashed. Note
        // that this is done before hb_it is incremented.
        // ppSquashInRename->notify(std::make_pair(hb_it->instSeqNum,
        //                                         hb_it->newPhysReg));

        historyBuffer[tid].erase(hb_it++);

        ++stats.undoneMaps;
    }

    // ----------------- frm recover ----------------------------------

    auto frm_hb_it = frmHistoryBuffer[tid].begin();

    // After a syscall squashes everything, the history buffer may be empty
    // but the ROB may still be squashing instructions.
    // Go through the most recent instructions, undoing the mappings
    // they did and freeing up the registers.
    while (!frmHistoryBuffer[tid].empty() &&
           frm_hb_it->instSeqNum > squashed_seq_num) {
        assert(frm_hb_it != frmHistoryBuffer[tid].end());

        DPRINTF(RxuRename, "[tid:%i] Removing FRM history entry with sequence "
                "number %i (archReg: FRM, newPhysReg: %d, prevPhysReg: %d).\n",
                tid, frm_hb_it->instSeqNum, 
                frm_hb_it->newPhysReg, frm_hb_it->prevPhysReg);

        // Undo the rename mapping only if it was really a change.
        // Special regs that are not really renamed (like misc regs
        // and the zero reg) can be recognized because the new mapping
        // is the same as the old one.  While it would be merely a
        // waste of time to update the rename table, we definitely
        // don't want to put these on the free list.
        if (frm_hb_it->newPhysReg != frm_hb_it->prevPhysReg) {
            frm->setRecCnt();
        }

        // Notify potential listeners that the register mapping needs to be
        // removed because the instruction it was mapped to got squashed. Note
        // that this is done before hb_it is incremented.
        // ppSquashInRename->notify(std::make_pair(frm_hb_it->instSeqNum,
        //                                         frm_hb_it->newPhysReg));

        frmHistoryBuffer[tid].erase(frm_hb_it++);

        // ++stats.undoneMaps;
    }

    // ----------------- frm recover --------------------------------
}

void
Rename::removeFromHistory(InstSeqNum inst_seq_num, ThreadID tid)
{
    DPRINTF(RxuRename, "[tid:%i] Removing a committed instruction from the "
            "history buffer %u (size=%i), until [sn:%llu].\n",
            tid, tid, historyBuffer[tid].size(), inst_seq_num);

    auto hb_it = historyBuffer[tid].end();

    --hb_it;

    if (historyBuffer[tid].empty()) {
        DPRINTF(RxuRename, "[tid:%i] History buffer is empty.\n", tid);
        return;
    } else if (hb_it->instSeqNum > inst_seq_num) {
        DPRINTF(RxuRename, "[tid:%i] [sn:%llu] "
                "Old sequence number encountered. "
                "Ensure that a syscall happened recently.\n",
                tid,inst_seq_num);
        return;
    }

    // Commit all the renames up until (and including) the committed sequence
    // number. Some or even all of the committed instructions may not have
    // rename histories if they did not have destination registers that were
    // renamed.
    while (!historyBuffer[tid].empty() &&
           hb_it != historyBuffer[tid].end() &&
           hb_it->instSeqNum <= inst_seq_num) {

        DPRINTF(RxuRename, "[tid:%i] Freeing up older rename of reg %i (%s), "
                "[sn:%llu].\n",
                tid, hb_it->prevPhysReg->index(),
                hb_it->prevPhysReg->className(),
                hb_it->instSeqNum);

        // Don't free special phys regs like misc and zero regs, which
        // can be recognized because the new mapping is the same as
        // the old one.
        if (hb_it->newPhysReg != hb_it->prevPhysReg) {
            freeList->addReg(hb_it->prevPhysReg);
        }

        ++stats.committedMaps;

        historyBuffer[tid].erase(hb_it--);
    }

     // --------------- frm release ------------
    DPRINTF(RxuRename, "[tid:%i] Removing a committed instruction from the "
            "FRM history buffer %u (size=%i), until [sn:%llu].\n",
            tid, tid, frmHistoryBuffer[tid].size(), inst_seq_num);

    auto frm_hb_it = frmHistoryBuffer[tid].end();

    --frm_hb_it;

    if (frmHistoryBuffer[tid].empty()) {
        DPRINTF(RxuRename, "[tid:%i] FRM History buffer is empty.\n", tid);
        return;
    } else if (frm_hb_it->instSeqNum > inst_seq_num) {
        DPRINTF(RxuRename, "[tid:%i] [sn:%llu] "
                "Old sequence number encountered. "
                "Ensure that a syscall happened recently.\n",
                tid, inst_seq_num);
        return;
    }

    // Commit all the renames up until (and including) the committed sequence
    // number. Some or even all of the committed instructions may not have
    // rename histories if they did not have destination registers that were
    // renamed.
    while (!frmHistoryBuffer[tid].empty() &&
           frm_hb_it != frmHistoryBuffer[tid].end() &&
           frm_hb_it->instSeqNum <= inst_seq_num) {

        DPRINTF(RxuRename, "[tid:%i] Freeing up older rename of reg %i (FRM) , "
                "[sn:%llu].\n",
                tid, frm_hb_it->prevPhysReg,
                frm_hb_it->instSeqNum);

        // Don't free special phys regs like misc and zero regs, which
        // can be recognized because the new mapping is the same as
        // the old one.
        if (frm_hb_it->newPhysReg != frm_hb_it->prevPhysReg) {
            frm->setRelCnt();
        }

        frmHistoryBuffer[tid].erase(frm_hb_it--);
    }

    // ---------------- frm release ----------------------------------------
}

void
Rename::renameSrcRegs(const DynInstPtr &inst, ThreadID tid)
{
    gem5::ThreadContext *tc = inst->tcBase();
    UnifiedRenameMap *map = renameMap[tid];
    unsigned num_src_regs = inst->numSrcRegs();
    auto *isa = tc->getIsaPtr();

    if (inst->needFRM()) {
        unsigned curFRMIndex = frm->curPhysFRM();
        inst->renameFRM(curFRMIndex);
        
        DPRINTF(RxuRename,
                "[tid:%i] "
                "Looking up frm, got phys reg %i.\n",
                tid, curFRMIndex);
        if (frm->isReady(curFRMIndex)) {
            DPRINTF(RxuRename,
                    "[tid:%i] "
                    "Register %d (FRM) is ready.\n",
                    tid, curFRMIndex);
            inst->readyFRM(true);
        } else {
            DPRINTF(RxuRename,
                    "[tid:%i] "
                    "Register %d (FRM) is not ready.\n",
                    tid, curFRMIndex);
        }
        frm->addConsumer(curFRMIndex, inst);
    }

    // Get the architectual register numbers from the source and
    // operands, and redirect them to the right physical register.
    for (int src_idx = 0; src_idx < num_src_regs; src_idx++) {
        const RegId& src_reg = inst->srcRegIdx(src_idx);
        const RegId flat_reg = src_reg.flatten(*isa);
        PhysRegIdPtr renamed_reg;

        renamed_reg = map->lookup(flat_reg);
        switch (flat_reg.classValue()) {
          case InvalidRegClass:
            break;
          case IntRegClass:
            stats.intLookups++;
            break;
          case FloatRegClass:
            stats.fpLookups++;
            break;
          case VecRegClass:
          case VecElemClass:
            stats.vecLookups++;
            break;
          case VecPredRegClass:
            stats.vecPredLookups++;
            break;
          case MatRegClass:
            stats.matLookups++;
            break;
          case CCRegClass:
          case MiscRegClass:
            break;

          default:
            panic("Invalid register class: %d.", flat_reg.classValue());
        }

        DPRINTF(RxuRename,
                "[tid:%i] "
                "Looking up %s arch reg %i, got phys reg %i (%s)\n",
                tid, flat_reg.className(),
                src_reg.index(), renamed_reg->index(),
                renamed_reg->className());

        inst->renameSrcReg(src_idx, renamed_reg);

        // See if the register is ready or not.
        if (scoreboard->getReg(renamed_reg)) {
            DPRINTF(RxuRename,
                    "[tid:%i] "
                    "Register %d (flat: %d) (%s) is ready.\n",
                    tid, renamed_reg->index(), renamed_reg->flatIndex(),
                    renamed_reg->className());

            inst->markSrcRegReady(src_idx);
            PhysRegIdPtr srcreg =
            inst->renamedSrcIdx(src_idx);
            if(inst->isMemRef() && srcreg->classValue() == InvalidRegClass){
                if(src_idx == 1){
                    inst->ifstdataready = true;
                }
            }
            else if(inst->isMemRef()){
                bool srcready = inst->readySrcIdx(src_idx);
                inst->markSrcRegReady(src_idx,srcready,src_idx);
                if(src_idx == 1){
                    inst->ifstdataready = true;
                }
            }
        } else {
            DPRINTF(RxuRename,
                    "[tid:%i] "
                    "Register %d (flat: %d) (%s) is not ready.\n",
                    tid, renamed_reg->index(), renamed_reg->flatIndex(),
                    renamed_reg->className());
        }

        ++stats.lookups;
    }
}

void
Rename::renameDestRegs(const DynInstPtr &inst, ThreadID tid)
{
    gem5::ThreadContext *tc = inst->tcBase();
    UnifiedRenameMap *map = renameMap[tid];
    unsigned num_dest_regs = inst->numDestRegs();
    auto *isa = tc->getIsaPtr();

    if (inst->isCsrFRM()) {
     //   std::string message = "Instruction [sn:" + std::to_string(inst->seqNum) + "] is " + inst->staticInst->disassemble(inst->pc->instAddr());
    //    panic(message);

        unsigned prev_rn_frm = frm->curPhysFRM();
        unsigned new_rn_frm = frm->get();
        frm->unsetReady(new_rn_frm);
        frm->addProducer(new_rn_frm);

        DPRINTF(RxuRename,
                "[tid:%i] "
                "Renaming arch reg FRM to physical reg %i. "
                "Old mapping is %i.\n",
                tid, new_rn_frm, prev_rn_frm);

        FRMRenameHistory frm_hb_entry(inst->seqNum, new_rn_frm, prev_rn_frm);
        frmHistoryBuffer[tid].push_front(frm_hb_entry);

        DPRINTF(RxuRename, "[tid:%i] [sn:%llu] "
                            "Adding instruction to frm history buffer (size=%i).\n",
                tid, (*frmHistoryBuffer[tid].begin()).instSeqNum,
                frmHistoryBuffer[tid].size());

        inst->renameFRM(new_rn_frm, prev_rn_frm);
    }

    // Rename the destination registers.
    for (int dest_idx = 0; dest_idx < num_dest_regs; dest_idx++) {
        const RegId& dest_reg = inst->destRegIdx(dest_idx);
        UnifiedRenameMap::RenameInfo rename_result;

        RegId flat_dest_regid = dest_reg.flatten(*isa);
        flat_dest_regid.setNumPinnedWrites(dest_reg.getNumPinnedWrites());

        rename_result = map->rename(flat_dest_regid);

        inst->flattenedDestIdx(dest_idx, flat_dest_regid);

        scoreboard->unsetReg(rename_result.first);

        DPRINTF(RxuRename,
                "[tid:%i] "
                "Renaming arch reg %i (%s) to physical reg %i (%i).\n",
                tid, dest_reg.index(), dest_reg.className(),
                rename_result.first->index(),
                rename_result.first->flatIndex());

        // Record the rename information so that a history can be kept.
        RenameHistory hb_entry(inst->seqNum, flat_dest_regid,
                               rename_result.first,
                               rename_result.second);

        historyBuffer[tid].push_front(hb_entry);

        DPRINTF(RxuRename, "[tid:%i] [sn:%llu] "
                "Adding instruction to history buffer (size=%i).\n",
                tid,(*historyBuffer[tid].begin()).instSeqNum,
                historyBuffer[tid].size());

        // Tell the instruction to rename the appropriate destination
        // register (dest_idx) to the new physical register
        // (rename_result.first), and record the previous physical
        // register that the same logical register was renamed to
        // (rename_result.second).
        inst->renameDestReg(dest_idx,
                            rename_result.first,
                            rename_result.second);

        ++stats.renamedOperands;
    }

    inst->ifrenamedest = true;
}

int
Rename::calcFreeROBEntries(ThreadID tid)
{
    // int num_free = freeEntries[tid].robEntries -
    //               (instsInProgress[tid] - fromIEW->iewInfo[tid].dispatched);

    // //DPRINTF(RxuRename,"[tid:%i] %i rob free\n",tid,num_free);

    // return num_free;
    return 0;
}

int
Rename::calcFreeIQEntries(ThreadID tid)
{
    // int num_free = freeEntries[tid].iqEntries -
    //               (instsInProgress[tid] - fromIEW->iewInfo[tid].dispatched);

    // //DPRINTF(RxuRename,"[tid:%i] %i iq free\n",tid,num_free);

    // return num_free;
    return 0;
}

int
Rename::calcFreeLQEntries(ThreadID tid)
{
//         int num_free = freeEntries[tid].lqEntries -
//             (loadsInProgress[tid] - fromIEW->iewInfo[tid].dispatchedToLQ);
//         DPRINTF(RxuRename,
//                 "calcFreeLQEntries: free lqEntries: %d, loadsInProgress: %d, "
//                 "loads dispatchedToLQ: %d\n",
//                 freeEntries[tid].lqEntries, loadsInProgress[tid],
//                 fromIEW->iewInfo[tid].dispatchedToLQ);
//         return num_free;
    return 0;
}

int
Rename::calcFreeSQEntries(ThreadID tid)
{
        // int num_free = freeEntries[tid].sqEntries -
        //     (storesInProgress[tid] - fromIEW->iewInfo[tid].dispatchedToSQ);
        // DPRINTF(RxuRename, "calcFreeSQEntries: free sqEntries: %d, "
        //         "storesInProgress: %d, stores dispatchedToSQ: %d\n",
        //         freeEntries[tid].sqEntries, storesInProgress[tid],
        //         fromIEW->iewInfo[tid].dispatchedToSQ);
        // return num_free;
    return 0;
}

unsigned
Rename::validInsts()
{
    unsigned inst_count = 0;

    // -- add by hongfei.liu ----------------------
    // for (int i=0; i<fromDecode->size; i++) {
    //     if (!fromDecode->insts[i]->isSquashed())
    for (int i=0; i<fromPredisq->size; i++) {
        if (!fromPredisq->insts[i]->isSquashed())
    // --------------------------------------------
            inst_count++;
    }

    return inst_count;
}

void
Rename::readStallSignals(ThreadID tid)
{
    // if (fromIEW->iewBlock[tid]) {
    //     stalls[tid].iew = true;
    // }

    // if (fromIEW->iewUnblock[tid]) {
    //     assert(stalls[tid].iew);
    //     stalls[tid].iew = false;
    // }
}

bool
Rename::checkStall(ThreadID tid)
{
    bool ret_val = false;

    if (stalls[tid].iew) {
        DPRINTF(RxuRename,"[tid:%i] Stall from IEW stage detected.\n", tid);
        ret_val = true;
    } else if (calcFreeROBEntries(tid) <= 0) {
        DPRINTF(RxuRename,"[tid:%i] Stall: ROB has 0 free entries.\n", tid);
        ret_val = true;
    } else if (calcFreeIQEntries(tid) <= 0) {
        DPRINTF(RxuRename,"[tid:%i] Stall: IQ has 0 free entries.\n", tid);
        ret_val = true;
    } else if (calcFreeLQEntries(tid) <= 0 && calcFreeSQEntries(tid) <= 0) {
        DPRINTF(RxuRename,"[tid:%i] Stall: LSQ has 0 free entries.\n", tid);
        ret_val = true;
    } else if (renameMap[tid]->numFreeEntries() <= 0) {
        DPRINTF(RxuRename,"[tid:%i] Stall: RenameMap has 0 free entries.\n", tid);
        ret_val = true;
    } else if (renameStatus[tid] == SerializeStall &&
               (!emptyROB[tid] || instsInProgress[tid])) {
        DPRINTF(RxuRename,"[tid:%i] Stall: Serialize stall and ROB is not "
                "empty.\n",
                tid);
        ret_val = true;
    }

    return ret_val;
}

void
Rename::readFreeEntries(ThreadID tid)
{
    // if (fromIEW->iewInfo[tid].usedIQ)
    //     freeEntries[tid].iqEntries = fromIEW->iewInfo[tid].freeIQEntries;

    // if (fromIEW->iewInfo[tid].usedLSQ) {
    //     freeEntries[tid].lqEntries = fromIEW->iewInfo[tid].freeLQEntries;
    //     freeEntries[tid].sqEntries = fromIEW->iewInfo[tid].freeSQEntries;
    // }

    // if (fromCommit->commitInfo[tid].usedROB) {
    //     freeEntries[tid].robEntries =
    //         fromCommit->commitInfo[tid].freeROBEntries;
    //     emptyROB[tid] = fromCommit->commitInfo[tid].emptyROB;
    // }

    // DPRINTF(RxuRename, "[tid:%i] Free IQ: %i, Free ROB: %i, "
    //                 "Free LQ: %i, Free SQ: %i, FreeRM %i(%i %i %i %i %i %i %i)\n",
    //         tid,
    //         freeEntries[tid].iqEntries,
    //         freeEntries[tid].robEntries,
    //         freeEntries[tid].lqEntries,
    //         freeEntries[tid].sqEntries,
    //         renameMap[tid]->numFreeEntries(),
    //         renameMap[tid]->numFreeEntries(IntRegClass),
    //         renameMap[tid]->numFreeEntries(FloatRegClass),
    //         renameMap[tid]->numFreeEntries(VecRegClass),
    //         renameMap[tid]->numFreeEntries(VecElemClass),
    //         renameMap[tid]->numFreeEntries(VecPredRegClass),
    //         renameMap[tid]->numFreeEntries(MatRegClass),
    //         renameMap[tid]->numFreeEntries(CCRegClass));

    // DPRINTF(RxuRename, "[tid:%i] %i instructions not yet in ROB\n",
    //         tid, instsInProgress[tid]);
}

bool
Rename::checkSignalsAndUpdate(ThreadID tid)
{
    // Check if there's a squash signal, squash if there is
    // Check stall signals, block if necessary.
    // If status was blocked
    //     check if stall conditions have passed
    //         if so then go to unblocking
    // If status was Squashing
    //     check if squashing is not high.  Switch to running this cycle.
    // If status was serialize stall
    //     check if ROB is empty and no insts are in flight to the ROB

    readFreeEntries(tid);
    readStallSignals(tid);

    if (fromCommit->commitInfo[tid].squash) {
        DPRINTF(RxuRename, "[tid:%i] Squashing instructions due to squash from "
                "commit.\n", tid);

        squash(fromCommit->commitInfo[tid].doneSeqNum, tid);

        return true;
    }

    if (checkStall(tid)) {
        return block(tid);
    }

    if (renameStatus[tid] == Blocked) {
        DPRINTF(RxuRename, "[tid:%i] Done blocking, switching to unblocking.\n",
                tid);

        renameStatus[tid] = Unblocking;

        unblock(tid);

        return true;
    }

    if (renameStatus[tid] == Squashing) {
        // Switch status to running if rename isn't being told to block or
        // squash this cycle.
        if (resumeSerialize) {
            DPRINTF(RxuRename,
                    "[tid:%i] Done squashing, switching to serialize.\n", tid);

            renameStatus[tid] = SerializeStall;
            return true;
        } else if (resumeUnblocking) {
            DPRINTF(RxuRename,
                    "[tid:%i] Done squashing, switching to unblocking.\n",
                    tid);
            renameStatus[tid] = Unblocking;
            return true;
        } else {
            DPRINTF(RxuRename, "[tid:%i] Done squashing, switching to running.\n",
                    tid);
            renameStatus[tid] = Running;
            return false;
        }
    }

    if (renameStatus[tid] == SerializeStall) {
        // Stall ends once the ROB is free.
        DPRINTF(RxuRename, "[tid:%i] Done with serialize stall, switching to "
                "unblocking.\n", tid);

        DynInstPtr serial_inst = serializeInst[tid];

        renameStatus[tid] = Unblocking;

        unblock(tid);

        DPRINTF(RxuRename, "[tid:%i] Processing instruction [%lli] with "
                "PC %s.\n", tid, serial_inst->seqNum, serial_inst->pcState());

        // Put instruction into queue here.
        serial_inst->clearSerializeBefore();

        if (!skidBuffer[tid].empty()) {
            skidBuffer[tid].push_front(serial_inst);
        } else {
            insts[tid].push_front(serial_inst);
        }

        DPRINTF(RxuRename, "[tid:%i] Instruction must be processed by rename."
                " Adding to front of list.\n", tid);

        serializeInst[tid] = NULL;

        return true;
    }

    // If we've reached this point, we have not gotten any signals that
    // cause rename to change its status.  Rename remains the same as before.
    return false;
}

void
Rename::serializeAfter(InstQueue &inst_list, ThreadID tid)
{
    if (inst_list.empty()) {
        // Mark a bit to say that I must serialize on the next instruction.
        serializeOnNextInst[tid] = true;
        return;
    }

    // Set the next instruction as serializing.
    inst_list.front()->setSerializeBefore();
}

void
Rename::incrFullStat(const FullSource &source)
{
    switch (source) {
      case ROB:
        ++stats.ROBFullEvents;
        break;
      case IQ:
        ++stats.IQFullEvents;
        break;
      case LQ:
        ++stats.LQFullEvents;
        break;
      case SQ:
        ++stats.SQFullEvents;
        break;
      default:
        panic("Rename full stall stat should be incremented for a reason!");
        break;
    }
}

void
Rename::recoverAndRelease()
{
    frm->recover();
    frm->release();
}

bool
Rename::checkFRMRenameStall() {
    return frm->stall();
}

void
Rename::dumpHistory()
{
    std::list<RenameHistory>::iterator buf_it;

    for (ThreadID tid = 0; tid < numThreads; tid++) {

        buf_it = historyBuffer[tid].begin();

        while (buf_it != historyBuffer[tid].end()) {
            cprintf("Seq num: %i\nArch reg[%s]: %i New phys reg:"
                    " %i[%s] Old phys reg: %i[%s]\n",
                    (*buf_it).instSeqNum,
                    (*buf_it).archReg.className(),
                    (*buf_it).archReg.index(),
                    (*buf_it).newPhysReg->index(),
                    (*buf_it).newPhysReg->className(),
                    (*buf_it).prevPhysReg->index(),
                    (*buf_it).prevPhysReg->className());

            buf_it++;
        }
    }
}

} // namespace rxuo3
} // namespace gem5
