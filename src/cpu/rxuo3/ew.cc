#include "cpu/rxuo3/ew.hh"

#include <queue>

#include "cpu/checker/cpu.hh"
#include "cpu/rxuo3/dyn_inst.hh"
#include "cpu/rxuo3/rxu_fu_pool.hh"
#include "cpu/rxuo3/limits.hh"
#include "cpu/timebuf.hh"
#include "arch/riscv/regs/misc.hh"
#include "debug/RxuActivity.hh"
#include "debug/Drain.hh"
#include "debug/RxuEW.hh"
#include "debug/RxuRBK.hh"
#include "debug/RxuO3PipeView.hh"
#include "debug/Spike.hh"
#include "debug/RxuVector.hh"
#include "params/BaseRxuO3CPU.hh"
#include "arch/riscv/pcstate.hh"


namespace gem5
{

    namespace rxuo3
    {

        EW::FUCompletion::FUCompletion(const DynInstPtr &_inst,
                                       int fu_idx, EW *ew_ptr)
            : Event(Exe_delay, AutoDelete),
              inst(_inst), fuIdx(fu_idx), ewPtr(ew_ptr), freeFU(false)
        {
        }

        void
        EW::FUCompletion::process()
        {
            ewPtr->processFUCompletion(inst, freeFU ? fuIdx : -1);
            inst = NULL;
        }

        const char *
        EW::FUCompletion::description() const
        {
            return "Functional unit completion";
        }

        EW::LoadStoreLatency::LoadStoreLatency(const DynInstPtr &_inst,
                                               EW *ew_ptr)
            : Event(Stat_Event_Pri, AutoDelete),
              inst(_inst), ewPtr(ew_ptr)
        {
        }

        void
        EW::LoadStoreLatency::process()
        {
            ewPtr->processLoadStoreLatency(inst);
            inst = NULL;
        }

        EW::VectorLatency::VectorLatency(const DynInstPtr &_inst,
                                               EW *ew_ptr)
            : Event(Stat_Event_Pri, AutoDelete),
              inst(_inst), ewPtr(ew_ptr)
        {
        }

        void
        EW::VectorLatency::process()
        {
            ewPtr->processVectorLatency(inst);
            inst = NULL;
        }

        EW::VSpecialCommit::VSpecialCommit(const DynInstPtr &_inst,
                                               EW *ew_ptr)
            : Event(Stat_Event_Pri, AutoDelete),
              inst(_inst), ewPtr(ew_ptr)
        {
        }

        void
        EW::VSpecialCommit::process()
        {
            for (auto it : inst->uopQueue) {
                it->setCanCommit();
                it->setExecuted();
            }
            inst->uopQueue.clear();
            inst = NULL;
        }

        EW::LdstEarlydone::LdstEarlydone(const DynInstPtr &_inst,
                                         EW *ew_ptr)
            : Event(Stat_Event_Pri, AutoDelete),
              inst(_inst), ewPtr(ew_ptr)
        {
        }

        void
        EW::LdstEarlydone::process()
        {
            ewPtr->processLdstEarlydone(inst);
            inst = NULL;
        }

        EW::EW(CPU *_cpu, const BaseRxuO3CPUParams &params)
            : cpu(_cpu),
              fuPool(params.fuPool),
              commitToEWDelay(params.commitToEWDelay),
              dispipe3ToEWDelay(params.dispipe3ToEWDelay),
              wbNumInst(0),
              wbCycle(0),
              wbWidth(params.wbWidth),
              numThreads(params.numThreads),
              stats(cpu)
        {
            if (wbWidth > MaxWidth)
                fatal("wbWidth (%d) is larger than compiled limit (%d),\n"
                      "\tincrease MaxWidth in src/cpu/rxuo3/limits.hh\n",
                      wbWidth, static_cast<int>(MaxWidth));

            _status = Active;

            for (ThreadID tid = 0; tid < MaxThreads; tid++)
            {
                ewStatus[tid] = Running;
                fetchRedirect[tid] = false;
            }

            updateLSQNextCycle = false;
            // loopTable.clear();
        }

        std::string
        EW::name() const
        {
            return cpu->name() + ".ew";
        }

        void
        EW::regProbePoints()
        {
            ppMispredict = new ProbePointArg<DynInstPtr>(
                cpu->getProbeManager(), "Mispredict");
            /**
             * Probe point with dynamic instruction as the argument used to probe when
             * an instruction starts to execute.
             */
            ppExecute = new ProbePointArg<DynInstPtr>(
                cpu->getProbeManager(), "Execute");
            /**
             * Probe point with dynamic instruction as the argument used to probe when
             * an instruction execution completes and it is marked ready to commit.
             */
            ppToCommit = new ProbePointArg<DynInstPtr>(
                cpu->getProbeManager(), "ToCommit");
        }

        EW::EWStats::EWStats(CPU *cpu)
            : statistics::Group(cpu, "ew"),

              ADD_STAT(regIntRead0, statistics::units::Cycle::get(),
                       "Times of regIntRead == 0 per cycle"),
              ADD_STAT(regIntRead1, statistics::units::Cycle::get(),
                       "Times of regIntRead == 1 per cycle"),
              ADD_STAT(regIntRead2, statistics::units::Cycle::get(),
                       "Times of regIntRead == 2 per cycle"),
              ADD_STAT(regIntRead3, statistics::units::Cycle::get(),
                       "Times of regIntRead == 3 per cycle"),
              ADD_STAT(regIntRead4, statistics::units::Cycle::get(),
                       "Times of regIntRead == 4 per cycle"),
              ADD_STAT(regIntRead5, statistics::units::Cycle::get(),
                       "Times of regIntRead == 5 per cycle"),
              ADD_STAT(regIntRead6, statistics::units::Cycle::get(),
                       "Times of regIntRead == 6 per cycle"),
              ADD_STAT(regIntRead7, statistics::units::Cycle::get(),
                       "Times of regIntRead == 7 per cycle"),
              ADD_STAT(regIntRead8, statistics::units::Cycle::get(),
                       "Times of regIntRead == 8 per cycle"),
              ADD_STAT(regIntRead9, statistics::units::Cycle::get(),
                       "Times of regIntRead == 9 per cycle"),
              ADD_STAT(regIntRead10, statistics::units::Cycle::get(),
                       "Times of regIntRead == 10 per cycle"),
              ADD_STAT(regIntRead11, statistics::units::Cycle::get(),
                       "Times of regIntRead == 11 per cycle"),
              ADD_STAT(regIntRead12, statistics::units::Cycle::get(),
                       "Times of regIntRead == 12 per cycle"),
              ADD_STAT(regIntRead13, statistics::units::Cycle::get(),
                       "Times of regIntRead == 13 per cycle"),
              ADD_STAT(regIntRead14, statistics::units::Cycle::get(),
                       "Times of regIntRead == 14 per cycle"),
              ADD_STAT(regIntRead15, statistics::units::Cycle::get(),
                       "Times of regIntRead == 15 per cycle"),
              ADD_STAT(regIntRead16, statistics::units::Cycle::get(),
                       "Times of regIntRead == 16 per cycle"),
              ADD_STAT(regIntReadOver16, statistics::units::Cycle::get(),
                       "Times of regIntRead > 16 per cycle"),

              ADD_STAT(regFloatRead0, statistics::units::Cycle::get(),
                       "Times of regFloatRead == 0 per cycle"),
              ADD_STAT(regFloatRead1, statistics::units::Cycle::get(),
                       "Times of regFloatRead == 1 per cycle"),
              ADD_STAT(regFloatRead2, statistics::units::Cycle::get(),
                       "Times of regFloatRead == 2 per cycle"),
              ADD_STAT(regFloatRead3, statistics::units::Cycle::get(),
                       "Times of regFloatRead == 3 per cycle"),
              ADD_STAT(regFloatRead4, statistics::units::Cycle::get(),
                       "Times of regFloatRead == 4 per cycle"),
              ADD_STAT(regFloatRead5, statistics::units::Cycle::get(),
                       "Times of regFloatRead == 5 per cycle"),
              ADD_STAT(regFloatRead6, statistics::units::Cycle::get(),
                       "Times of regFloatRead == 6 per cycle"),
              ADD_STAT(regFloatRead7, statistics::units::Cycle::get(),
                       "Times of regFloatRead == 7 per cycle"),
              ADD_STAT(regFloatRead8, statistics::units::Cycle::get(),
                       "Times of regFloatRead == 8 per cycle"),
              ADD_STAT(regFloatRead9, statistics::units::Cycle::get(),
                       "Times of regFloatRead == 9 per cycle"),
              ADD_STAT(regFloatRead10, statistics::units::Cycle::get(),
                       "Times of regFloatRead == 10 per cycle"),
              ADD_STAT(regFloatReadOver10, statistics::units::Cycle::get(),
                       "Times of regFloatRead > 10 per cycle"),

              ADD_STAT(regIntWrite0, statistics::units::Cycle::get(),
                       "Times of regIntWrite == 0 per cycle"),
              ADD_STAT(regIntWrite1, statistics::units::Cycle::get(),
                       "Times of regIntWrite == 1 per cycle"),
              ADD_STAT(regIntWrite2, statistics::units::Cycle::get(),
                       "Times of regIntWrite == 2 per cycle"),
              ADD_STAT(regIntWrite3, statistics::units::Cycle::get(),
                       "Times of regIntWrite == 3 per cycle"),
              ADD_STAT(regIntWrite4, statistics::units::Cycle::get(),
                       "Times of regIntWrite == 4 per cycle"),
              ADD_STAT(regIntWrite5, statistics::units::Cycle::get(),
                       "Times of regIntWrite == 5 per cycle"),
              ADD_STAT(regIntWrite6, statistics::units::Cycle::get(),
                       "Times of regIntWrite == 6 per cycle"),
              ADD_STAT(regIntWrite7, statistics::units::Cycle::get(),
                       "Times of regIntWrite == 7 per cycle"),
              ADD_STAT(regIntWrite8, statistics::units::Cycle::get(),
                       "Times of regIntWrite == 8 per cycle"),
              ADD_STAT(regIntWrite9, statistics::units::Cycle::get(),
                       "Times of regIntWrite == 9 per cycle"),
              ADD_STAT(regIntWrite10, statistics::units::Cycle::get(),
                       "Times of regIntWrite == 10 per cycle"),
              ADD_STAT(regIntWriteOver10, statistics::units::Cycle::get(),
                       "Times of regIntWrite > 10 per cycle"),

              ADD_STAT(regFloatWrite0, statistics::units::Cycle::get(),
                       "Times of regFloatWrite == 0 per cycle"),
              ADD_STAT(regFloatWrite1, statistics::units::Cycle::get(),
                       "Times of regFloatWrite == 1 per cycle"),
              ADD_STAT(regFloatWrite2, statistics::units::Cycle::get(),
                       "Times of regFloatWrite == 2 per cycle"),
              ADD_STAT(regFloatWrite3, statistics::units::Cycle::get(),
                       "Times of regFloatWrite == 3 per cycle"),
              ADD_STAT(regFloatWrite4, statistics::units::Cycle::get(),
                       "Times of regFloatWrite == 4 per cycle"),
              ADD_STAT(regFloatWrite5, statistics::units::Cycle::get(),
                       "Times of regFloatWrite == 5 per cycle"),
              ADD_STAT(regFloatWrite6, statistics::units::Cycle::get(),
                       "Times of regFloatWrite == 6 per cycle"),
              ADD_STAT(regFloatWrite7, statistics::units::Cycle::get(),
                       "Times of regFloatWrite == 7 per cycle"),
              ADD_STAT(regFloatWrite8, statistics::units::Cycle::get(),
                       "Times of regFloatWrite == 8 per cycle"),
              ADD_STAT(regFloatWrite9, statistics::units::Cycle::get(),
                       "Times of regFloatWrite == 9 per cycle"),
              ADD_STAT(regFloatWrite10, statistics::units::Cycle::get(),
                       "Times of regFloatWrite == 10 per cycle"),
              ADD_STAT(regFloatWriteOver10, statistics::units::Cycle::get(),
                       "Times of regFloatWrite > 10 per cycle"),

              ADD_STAT(idleCycles, statistics::units::Cycle::get(),
                       "Number of cycles EW is idle"),
              ADD_STAT(squashCycles, statistics::units::Cycle::get(),
                       "Number of cycles EW is squashing"),
              ADD_STAT(blockCycles, statistics::units::Cycle::get(),
                       "Number of cycles EW is blocking"),
              ADD_STAT(unblockCycles, statistics::units::Cycle::get(),
                       "Number of cycles EW is unblocking"),
              ADD_STAT(runCycles, statistics::units::Cycle::get(),
                       "Number of cycles EW is running"),
              ADD_STAT(squashedInsts, statistics::units::Cycle::get(),
                       "Number of squashed instructions"),
              ADD_STAT(memOrderViolationEvents, statistics::units::Count::get(),
                       "Number of memory order violations"),
              ADD_STAT(predictedTakenIncorrect, statistics::units::Count::get(),
                       "Number of branches that were predicted taken incorrectly"),
              ADD_STAT(predictedNotTakenIncorrect, statistics::units::Count::get(),
                       "Number of branches that were predicted not taken incorrectly"),
              ADD_STAT(branchMispredicts, statistics::units::Count::get(),
                       "Number of branch mispredicts detected at execute",
                       predictedTakenIncorrect + predictedNotTakenIncorrect),
              ADD_STAT(instsToCommit, statistics::units::Count::get(),
                       "Cumulative count of insts sent to commit"),
              ADD_STAT(writebackCount, statistics::units::Count::get(),
                       "Cumulative count of insts written-back"),
              ADD_STAT(wbRate, statistics::units::Rate<statistics::units::Count, statistics::units::Cycle>::get(),
                       "Insts written-back per cycle")
        {
            regIntRead0.prereq(regIntRead0);
            regIntRead1.prereq(regIntRead1);
            regIntRead2.prereq(regIntRead2);
            regIntRead3.prereq(regIntRead3);
            regIntRead4.prereq(regIntRead4);
            regIntRead5.prereq(regIntRead5);
            regIntRead6.prereq(regIntRead6);
            regIntRead7.prereq(regIntRead7);
            regIntRead8.prereq(regIntRead8);
            regIntRead9.prereq(regIntRead9);
            regIntRead10.prereq(regIntRead10);
            regIntRead11.prereq(regIntRead11);
            regIntRead12.prereq(regIntRead12);
            regIntRead13.prereq(regIntRead13);
            regIntRead14.prereq(regIntRead14);
            regIntRead15.prereq(regIntRead15);
            regIntRead16.prereq(regIntRead16);
            regIntReadOver16.prereq(regIntReadOver16);

            regFloatRead0.prereq(regFloatRead0);
            regFloatRead1.prereq(regFloatRead1);
            regFloatRead2.prereq(regFloatRead2);
            regFloatRead3.prereq(regFloatRead3);
            regFloatRead4.prereq(regFloatRead4);
            regFloatRead5.prereq(regFloatRead5);
            regFloatRead6.prereq(regFloatRead6);
            regFloatRead7.prereq(regFloatRead7);
            regFloatRead8.prereq(regFloatRead8);
            regFloatRead9.prereq(regFloatRead9);
            regFloatRead10.prereq(regFloatRead10);
            regFloatReadOver10.prereq(regFloatReadOver10);

            regIntWrite0.prereq(regIntWrite0);
            regIntWrite1.prereq(regIntWrite1);
            regIntWrite2.prereq(regIntWrite2);
            regIntWrite3.prereq(regIntWrite3);
            regIntWrite4.prereq(regIntWrite4);
            regIntWrite5.prereq(regIntWrite5);
            regIntWrite6.prereq(regIntWrite6);
            regIntWrite7.prereq(regIntWrite7);
            regIntWrite8.prereq(regIntWrite8);
            regIntWrite9.prereq(regIntWrite9);
            regIntWrite10.prereq(regIntWrite10);
            regIntWriteOver10.prereq(regIntWriteOver10);

            regFloatWrite0.prereq(regFloatWrite0);
            regFloatWrite1.prereq(regFloatWrite1);
            regFloatWrite2.prereq(regFloatWrite2);
            regFloatWrite3.prereq(regFloatWrite3);
            regFloatWrite4.prereq(regFloatWrite4);
            regFloatWrite5.prereq(regFloatWrite5);
            regFloatWrite6.prereq(regFloatWrite6);
            regFloatWrite7.prereq(regFloatWrite7);
            regFloatWrite8.prereq(regFloatWrite8);
            regFloatWrite9.prereq(regFloatWrite9);
            regFloatWrite10.prereq(regFloatWrite10);
            regFloatWriteOver10.prereq(regFloatWriteOver10);

            idleCycles
                .prereq(idleCycles);

            squashCycles
                .prereq(squashCycles);

            blockCycles
                .prereq(blockCycles);

            unblockCycles
                .prereq(unblockCycles);

            runCycles
                .prereq(runCycles);

            squashedInsts
                .prereq(squashedInsts);

            memOrderViolationEvents
                .prereq(memOrderViolationEvents);

            predictedTakenIncorrect
                .prereq(predictedTakenIncorrect);

            predictedNotTakenIncorrect
                .prereq(predictedNotTakenIncorrect);

            instsToCommit
                .init(cpu->numThreads)
                .flags(statistics::total);

            writebackCount
                .init(cpu->numThreads)
                .flags(statistics::total);

            wbRate
                .flags(statistics::total);
            wbRate = writebackCount / cpu->baseStats.numCycles;
        }

        void
        EW::startupStage()
        {
            for (ThreadID tid = 0; tid < numThreads; tid++)
            {
                ewStatus[tid] = Idle;
            }

            // loopTable.clear();

            cpu->activateStage(CPU::EWIdx);
        }

        void
        EW::clearStates(ThreadID tid)
        {
            ewStatus[tid] = Idle;
            // loopTable.clear();
        }

        void
        EW::setTimeBuffer(TimeBuffer<TimeStruct> *tb_ptr)
        {
            timeBuffer = tb_ptr;

            // Setup wire to read information from time buffer, from commit.
            fromCommit = timeBuffer->getWire(-commitToEWDelay);

            // Setup wire to write information back to previous stages.
            toDispipe3 = timeBuffer->getWire(0);
            toFetch = timeBuffer->getWire(0);
        }

        void
        EW::setWakeQueue(TimeBuffer<WakeStruct> *wq_ptr)
        {
            wakeQueue = wq_ptr;

            // Setup wire to read information from dispipe3 queue.
            fromDispipe3 = wakeQueue->getWire(-dispipe3ToEWDelay);
        }

        void
        EW::setEWQueue(TimeBuffer<EWStruct> *eq_ptr)
        {
            ewQueue = eq_ptr;

            // Setup wire to write instructions to commit.
            toCommit = ewQueue->getWire(0);
        }

        void
        EW::setActiveThreads(std::list<ThreadID> *at_ptr)
        {
            activeThreads = at_ptr;
        }

        void
        EW::setScoreboard(Scoreboard *sb_ptr)
        {
            scoreboard = sb_ptr;
        }

        bool
        EW::isDrained() const
        {
            bool drained = true;

            for (ThreadID tid = 0; tid < numThreads; tid++)
            {
                if (!insts[tid].empty())
                {
                    DPRINTF(Drain, "%i: Insts not empty.\n", tid);
                    drained = false;
                }
                drained = drained &&
                          (ewStatus[tid] == Running || ewStatus[tid] == Idle);
            }

            // Also check the FU pool as instructions are "stored" in FU
            // completion events until they are done and not accounted for
            // above
            if (drained && !fuPool->isDrained())
            {
                DPRINTF(Drain, "FU pool still busy.\n");
                drained = false;
            }

            return drained;
        }

        void
        EW::drainSanityCheck() const
        {
            assert(isDrained());
        }

        void
        EW::takeOverFrom()
        {
            // Reset all state.
            _status = Active;

            fuPool->takeOverFrom();

            startupStage();
            cpu->activityThisCycle();

            for (ThreadID tid = 0; tid < numThreads; tid++)
            {
                ewStatus[tid] = Running;
                fetchRedirect[tid] = false;
            }

            updateLSQNextCycle = false;
        }

        void
        EW::squash(ThreadID tid)
        {
            DPRINTF(RxuEW, "[tid:%i] Squashing all instructions.\n", tid);

            DPRINTF(RxuEW,
                    "Removing insts instructions until "
                    "[sn:%llu] [tid:%i]\n",
                    fromCommit->commitInfo[tid].doneSeqNum, tid);\
            
            cpu->mergeBuffer.squash(fromCommit->commitInfo[tid].doneSeqNum);

            DynInstPtr inst;
            int instNums;
            instNums = insts[tid].size();

            for (int i = 0; i < instNums; i++)
            {

                inst = insts[tid].front();
                insts[tid].pop_front();
                if (inst->seqNum > fromCommit->commitInfo[tid].doneSeqNum)
                {
                    if (inst->isSquashed())
                    {
                        ++stats.squashedInsts;
                        dispipe3Stage->rmu->dependGraph.remove_base(inst);
                        dispipe3Stage->rmu->dependGraph.clear_base(inst);
                        DPRINTF(RxuEW,
                                "[tid:%i] "
                                "instruction [sn:%llu] with PC %s has already been squashed before.\n",
                                tid, inst->seqNum, inst->pcState());
                    }
                    else
                    {
                        inst->setSquashed();
                        dispipe3Stage->rmu->dependGraph.remove_base(inst);
                        dispipe3Stage->rmu->dependGraph.clear_base(inst);
                        ++stats.squashedInsts;
                        DPRINTF(RxuEW,
                                "[tid:%i] "
                                "instruction [sn:%llu] with PC %s is squashed.\n",
                                tid, inst->seqNum, inst->pcState());
                    }
                }
                else
                {
                    insts[tid].push_back(inst);
                }
            }
        }

        void
        EW::squashDueToBranch(const DynInstPtr &inst, ThreadID tid)
        {
            DPRINTF(RxuEW, "[tid:%i] [sn:%llu] Squashing from a specific instruction,"
                           " PC: %s "
                           "\n",
                    tid, inst->seqNum, inst->pcState());

            if (!toCommit->squash[tid] ||
                inst->seqNum < toCommit->squashedSeqNum[tid])
            {
                toCommit->squash[tid] = true;
                if (inst->isMicroVector() && inst->has_ori_inst) {
                    if (inst->isEop()) toCommit->squashedSeqNum[tid] = inst->ori_inst->ori_inst->seqNum;
                    else toCommit->squashedSeqNum[tid] = inst->ori_inst->seqNum;                
                } else {
                    toCommit->squashedSeqNum[tid] = inst->seqNum;                    
                }
                toCommit->branchTaken[tid] = inst->pcState().branching();

                set(toCommit->pc[tid], inst->pcState());
                inst->staticInst->advancePC(*toCommit->pc[tid]);

                toCommit->mispredictInst[tid] = inst;
                toCommit->includeSquashInst[tid] = false;

                wroteToTimeBuffer = true;
            }
            DPRINTF(RxuEW, "toCommit->branchTake: %d"
                           "\n",
                    toCommit->branchTaken[tid]);
        }

        void
        EW::squashDueToMemOrder(const DynInstPtr &inst, ThreadID tid)
        {
            DPRINTF(RxuEW, "[tid:%i] Memory violation, squashing violator and younger "
                           "insts, PC: %s [sn:%llu].\n",
                    tid, inst->pcState(), inst->seqNum);
            // Need to include inst->seqNum in the following comparison to cover the
            // corner case when a branch misprediction and a memory violation for the
            // same instruction (e.g. load PC) are detected in the same cycle.  In this
            // case the memory violator should take precedence over the branch
            // misprediction because it requires the violator itself to be included in
            // the squash.
            if (!toCommit->squash[tid] ||
                inst->seqNum <= toCommit->squashedSeqNum[tid])
            {
                toCommit->squash[tid] = true;

                if(inst->isVector()){
                    if(inst->isMicroVector() && inst->isEop()){
                        toCommit->squashedSeqNum[tid] = inst->ori_inst->ori_inst->seqNum;
                    }
                    else if(inst->isMicroVector()){
                        toCommit->squashedSeqNum[tid] = inst->ori_inst->seqNum;
                    }
                    else {
                        toCommit->squashedSeqNum[tid] = inst->seqNum;
                    }
                }
                else {
                    toCommit->squashedSeqNum[tid] = inst->seqNum;
                }
                set(toCommit->pc[tid], inst->pcState());
                toCommit->mispredictInst[tid] = NULL;

                // Must include the memory violator in the squash.
                toCommit->includeSquashInst[tid] = true;

                wroteToTimeBuffer = true;
            }
        }

        void
        EW::block(ThreadID tid)
        {
            DPRINTF(RxuEW, "[tid:%i] Blocking.\n", tid);

            if (ewStatus[tid] != Blocked &&
                ewStatus[tid] != Unblocking)
            {
                toDispipe3->ewBlock[tid] = true;
                wroteToTimeBuffer = true;
            }

            ewStatus[tid] = Blocked;
        }

        void
        EW::unblock(ThreadID tid)
        {
            DPRINTF(RxuEW, "[tid:%i] Done unblocking.\n", tid);

            // Signal back to previous stages to unblock.
            // Also switch status to running.
            toDispipe3->ewUnblock[tid] = true;
            wroteToTimeBuffer = true;
            ewStatus[tid] = Running;
        }

        void
        EW::instToCommit(const DynInstPtr &inst)
        {
            // This function should not be called after writebackInsts in a
            // single cycle.  That will cause problems with an instruction
            // being added to the queue to commit without being processed by
            // writebackInsts prior to being sent to commit.

            // First check the time slot that this instruction will write
            // to.  If there are free write ports at the time, then go ahead
            // and write the instruction to that time.  If there are not,
            // keep looking back to see where's the first time there's a
            // free slot.
            if ((!inst->stdDataReady) && inst->isStore() && !inst->tlbfault && !inst->isStoreConditional())
            {
                return;
            }

            if (inst->isStore())
            {
                for (auto it = cpu->dispipe3.wtb.memDepUnit[0].stq.begin();
                     it != cpu->dispipe3.wtb.memDepUnit[0].stq.end(); ++it)
                {
                    if (it->second->seqNum == inst->seqNum)
                    {
                        cpu->dispipe3.wtb.memDepUnit[0].stq.erase(it->first);
                    }
                }
            }

            if (inst->isLoad())
            {
                for (auto itld = cpu->dispipe3.wtb.memDepUnit[0].ldq.begin();
                     itld != cpu->dispipe3.wtb.memDepUnit[0].ldq.end(); ++itld)
                {
                    if (itld->second->seqNum == inst->seqNum)
                    {
                        cpu->dispipe3.wtb.memDepUnit[0].ldq.erase(itld->first);
                    }
                }
            }

            if (inst->isSquashed())
            {
                return;
            }

            if (inst->ibuffer_id != 6 && inst->ibuffer_id != 7 && inst->ibuffer_id != 14 && inst->ibuffer_id != 15 &&
                inst->ibuffer_id != 16 && inst->ibuffer_id != 17 && inst->ibuffer_id != 18 && inst->ibuffer_id != 19)
            {   
               
                if (inst->isVector())
                {   
                    Cycles op_latency;
                    op_latency = cpu->ew.fuPool->getOpLatency(inst->opClass());
                    VectorLatency *Vector = new VectorLatency(inst, this);
                    if(op_latency <= 1){
                        cpu->schedule(Vector, cpu->clockEdge(Cycles(0)));
                    }
                    else {
                        cpu->schedule(Vector, cpu->clockEdge(Cycles(op_latency-2)));
                    }
                }
                else {
                    int sendCycle = -1;

                    while ((*ewQueue)[sendCycle].insts[wbNumInst])
                    {
                        ++wbNumInst;
                        if (wbNumInst == wbWidth)
                        {
                            ++sendCycle;
                            wbNumInst = 0;
                        }
                    }

                    DPRINTF(RxuEW, "Current wb cycle: %i, width: %i, numInst: %i\nwbActual:%i\n",
                            sendCycle + 1, wbWidth, wbNumInst, (sendCycle + 1) * wbWidth + wbNumInst);
                    // Add finished instruction to queue to commit.
                    (*ewQueue)[sendCycle].insts[wbNumInst] = inst;
                    (*ewQueue)[sendCycle].size++;
                    DPRINTF(RxuEW, "[sn:%llu] instruction is ready to WB and commit next cycle,"
                                    " PC: %s "
                                    "\n",
                                inst->seqNum, inst->pcState());
                    instsToWB.push_back(inst);
                }
            }
            else
            {
                if (inst->isLoad())
                {
                    inst->loadBackTick = curTick();
                }
                LoadStoreLatency *LDST = new LoadStoreLatency(inst, this);
                if (inst->isLoad() && (inst->loadExeTick == inst->loadBackTick))
                {
                    cpu->schedule(LDST, cpu->clockEdge(Cycles(2)));
                }
                else if (inst->isLoad())
                {
                    cpu->schedule(LDST, cpu->clockEdge(Cycles(1)));
                }
                else
                {
                    cpu->schedule(LDST, cpu->clockEdge(Cycles(2)));
                }

                

                DPRINTF(RxuEW, "[sn:%llu] load/store instruction is reached DC0,"
                               " PC: %s "
                               "\n",
                        inst->seqNum, inst->pcState());
                
                if (inst->isLoad())
                {
                    inst->ld_exe_succ = true;
                }

                LdstEarlydone *Earlydone = new LdstEarlydone(inst, this);
                if (inst->isLoad() && (inst->loadExeTick == inst->loadBackTick))
                {
                    cpu->schedule(Earlydone, cpu->clockEdge(Cycles(1)));
                }
                else if(inst->isLoad()){
                    cpu->schedule(Earlydone, cpu->clockEdge(Cycles(0)));
                }
                else {
                    cpu->schedule(Earlydone, cpu->clockEdge(Cycles(1)));
                }

                if (inst->isLoad() && (inst->numDestRegs() > 0 && !inst->destRegIdx(0).isZeroReg()))
                {
                    int delay = 3;
                    // if (inst->isLoad() && (inst->loadExeTick == inst->loadBackTick))
                    // {
                    //     delay = 5;
                    // }
                    if (inst->renamedDestIdx(0)->flatIndex() & 1)
                    {
                        // while (dispipe3Stage->wtb.wbNums.at(delay).intNormalOddWbNums <= 0) {
                        //     delay++;
                        // }
                        if(dispipe3Stage->wtb.wbNums.at(delay).intNormal_newOddWbNums >0){
                            dispipe3Stage->wtb.wbNums.at(delay).intNormal_newOddWbNums--;
                        }
                        else if(dispipe3Stage->wtb.wbNums.at(delay).intSpecialOddWbNums >0){
                            dispipe3Stage->wtb.wbNums.at(delay).intSpecialOddWbNums--;
                        }
                        // dispipe3Stage->wtb.wbNums.at(delay).intNormalOddWbNums--;
                    }
                    else
                    {
                        // while (dispipe3Stage->wtb.wbNums.at(delay).intNormalEvenWbNums <= 0) {
                        //     delay++;
                        // }
                        if(dispipe3Stage->wtb.wbNums.at(delay).intNormal_newEvenWbNums >0){
                            dispipe3Stage->wtb.wbNums.at(delay).intNormal_newEvenWbNums--;
                        }
                        else if(dispipe3Stage->wtb.wbNums.at(delay).intSpecialEvenWbNums >0){
                            dispipe3Stage->wtb.wbNums.at(delay).intSpecialEvenWbNums--;
                        }
                        // dispipe3Stage->wtb.wbNums.at(delay).intNormalEvenWbNums--;
                    }
                }
            }
        }

        void
        EW::updateStatus()
        {
            bool any_unblocking = false;

            std::list<ThreadID>::iterator threads = activeThreads->begin();
            std::list<ThreadID>::iterator end = activeThreads->end();

            while (threads != end)
            {
                ThreadID tid = *threads++;

                if (ewStatus[tid] == Unblocking)
                {
                    any_unblocking = true;
                    break;
                }
            }

            if (_status == Active &&
                !dispipe3Stage->ldstQueue.willWB() && !any_unblocking)
            {
                DPRINTF(RxuEW, "EW switching to idle\n");

                deactivateStage();

                _status = Inactive;
            }
            else if (_status == Inactive && (dispipe3Stage->ldstQueue.willWB() ||
                                             any_unblocking))
            {
                // Otherwise there is internal activity.  Set to active.
                DPRINTF(RxuEW, "EW switching to active\n");

                activateStage();

                _status = Active;
            }
        }

        bool
        EW::checkStall(ThreadID tid)
        {
            bool ret_val(false);

            if (fromCommit->commitInfo[tid].robSquashing)
            {
                DPRINTF(RxuEW, "[tid:%i] Stall from Commit stage detected.\n", tid);
                DPRINTF(RxuEW, "[tid:%i] ROB is still squashing.\n", tid);
                ret_val = true;
            }

            return ret_val;
        }

        void
        EW::checkSignalsAndUpdate(ThreadID tid)
        {
            if (fromCommit->commitInfo[tid].squash)
            {
                squash(tid);

                if (ewStatus[tid] == Blocked ||
                    ewStatus[tid] == Unblocking)
                {
                    toDispipe3->ewUnblock[tid] = true;
                    wroteToTimeBuffer = true;
                }

                ewStatus[tid] = Squashing;
                fetchRedirect[tid] = false;
                return;
            }

            if (checkStall(tid))
            {
                block(tid);
                ewStatus[tid] = RobSquashing;
                return;
            }

            if (ewStatus[tid] == Blocked)
            {
                // Status from previous cycle was blocked, but there are no more stall
                // conditions.  Switch over to unblocking.
                DPRINTF(RxuEW, "[tid:%i] Done blocking, switching to unblocking.\n",
                        tid);

                ewStatus[tid] = Unblocking;

                unblock(tid);

                return;
            }

            if (ewStatus[tid] == Squashing || ewStatus[tid] == RobSquashing)
            {
                // Switch status to running if EW isn't being told to block or
                // squash this cycle.
                DPRINTF(RxuEW, "[tid:%i] Done squashing, switching to running.\n",
                        tid);

                ewStatus[tid] = Running;

                return;
            }
        }

        void
        EW::sortInsts()
        {
            int intSrcReadNums = 0;
            int fpSrcReadNums = 0;
            int insts_from_dispipe3 = fromDispipe3->size;
            for (int i = 0; i < insts_from_dispipe3; ++i)
            {
                if (fromDispipe3->insts[i]->ibuffer_id == 6 || fromDispipe3->insts[i]->ibuffer_id == 7 || fromDispipe3->insts[i]->ibuffer_id == 14 || fromDispipe3->insts[i]->ibuffer_id == 15 ||
                    fromDispipe3->insts[i]->ibuffer_id == 16 || fromDispipe3->insts[i]->ibuffer_id == 17 || fromDispipe3->insts[i]->ibuffer_id == 18 || fromDispipe3->insts[i]->ibuffer_id == 19)
                {
                    instsToExecute.push_back(fromDispipe3->insts[i]);
                }
                else
                {
                    insts[fromDispipe3->insts[i]->threadNumber].push_back(fromDispipe3->insts[i]);
                    // instsToExecute.push_back(fromDispipe3->insts[i]);
                }

                DPRINTF(RxuEW,
                        "instruction [sn:%llu] with PC %s is reached EW stage.\n",
                        fromDispipe3->insts[i]->seqNum, fromDispipe3->insts[i]->pcState());

                if (fromDispipe3->insts[i]->numSrcRegs() > 0)
                {
                    if (fromDispipe3->insts[i]->srcRegIdx(0).classValue() == FloatRegClass)
                    {
                        fpSrcReadNums += fromDispipe3->insts[i]->numSrcRegs();
                    }
                    else
                    {
                        intSrcReadNums += fromDispipe3->insts[i]->numSrcRegs();
                    }
                }
            }

            if (intSrcReadNums == 0)
            {
                stats.regIntRead0++;
            }
            else if (intSrcReadNums == 1)
            {
                stats.regIntRead1++;
            }
            else if (intSrcReadNums == 2)
            {
                stats.regIntRead2++;
            }
            else if (intSrcReadNums == 3)
            {
                stats.regIntRead3++;
            }
            else if (intSrcReadNums == 4)
            {
                stats.regIntRead4++;
            }
            else if (intSrcReadNums == 5)
            {
                stats.regIntRead5++;
            }
            else if (intSrcReadNums == 6)
            {
                stats.regIntRead6++;
            }
            else if (intSrcReadNums == 7)
            {
                stats.regIntRead7++;
            }
            else if (intSrcReadNums == 8)
            {
                stats.regIntRead8++;
            }
            else if (intSrcReadNums == 9)
            {
                stats.regIntRead9++;
            }
            else if (intSrcReadNums == 10)
            {
                stats.regIntRead10++;
            }
            else if (intSrcReadNums == 11)
            {
                stats.regIntRead11++;
            }
            else if (intSrcReadNums == 12)
            {
                stats.regIntRead12++;
            }
            else if (intSrcReadNums == 13)
            {
                stats.regIntRead13++;
            }
            else if (intSrcReadNums == 14)
            {
                stats.regIntRead14++;
            }
            else if (intSrcReadNums == 15)
            {
                stats.regIntRead15++;
            }
            else if (intSrcReadNums == 16)
            {
                stats.regIntRead16++;
            }
            else
            {
                stats.regIntReadOver16++;
            }

            if (fpSrcReadNums == 0)
            {
                stats.regFloatRead0++;
            }
            else if (fpSrcReadNums == 1)
            {
                stats.regFloatRead1++;
            }
            else if (fpSrcReadNums == 2)
            {
                stats.regFloatRead2++;
            }
            else if (fpSrcReadNums == 3)
            {
                stats.regFloatRead3++;
            }
            else if (fpSrcReadNums == 4)
            {
                stats.regFloatRead4++;
            }
            else if (fpSrcReadNums == 5)
            {
                stats.regFloatRead5++;
            }
            else if (fpSrcReadNums == 6)
            {
                stats.regFloatRead6++;
            }
            else if (fpSrcReadNums == 7)
            {
                stats.regFloatRead7++;
            }
            else if (fpSrcReadNums == 8)
            {
                stats.regFloatRead8++;
            }
            else if (fpSrcReadNums == 9)
            {
                stats.regFloatRead9++;
            }
            else if (fpSrcReadNums == 10)
            {
                stats.regFloatRead10++;
            }
            else
            {
                stats.regFloatReadOver10++;
            }
        }

        void
        EW::wakeCPU()
        {
            cpu->wakeCPU();
        }

        void
        EW::activityThisCycle()
        {
            DPRINTF(RxuActivity, "Activity this cycle.\n");
            cpu->activityThisCycle();
        }

        void
        EW::activateStage()
        {
            DPRINTF(RxuActivity, "Activating stage.\n");
            cpu->activateStage(CPU::EWIdx);
        }

        void
        EW::deactivateStage()
        {
            DPRINTF(RxuActivity, "Deactivating stage.\n");
            cpu->deactivateStage(CPU::EWIdx);
        }

        void
        EW::processFUCompletion(const DynInstPtr &inst, int fu_idx)
        {
            DPRINTF(RxuEW, "Processing FU completion [sn:%llu]\n", inst->seqNum);
            assert(!cpu->switchedOut());
            // The CPU could have been sleeping until this op completed (*extremely*
            // long latency op).  Wake it if it was.  This may be overkill.
            wakeCPU();

            if (fu_idx > -1)
                fuPool->freeUnitNextCycle(fu_idx);

            instsToExecute.push_back(inst);
        }

        void
        EW::processLoadStoreLatency(const DynInstPtr &inst)
        {
            assert(!cpu->switchedOut());
            wakeCPU();
            DPRINTF(RxuEW,
                    "load/store instruction [sn:%llu] with PC %s is executing DC2, ready to WB/commit next cycle.\n",
                    inst->seqNum, inst->pcState());
        
            if (inst->firstIssue == -1)
            {
                inst->firstIssue = curTick() + 500;
            }
            int sendCycle = -1;
            while ((*ewQueue)[sendCycle].insts[wbNumInst])
            {
                ++wbNumInst;
                if (wbNumInst == wbWidth)
                {
                    ++sendCycle;
                    wbNumInst = 0;
                }
            }

            DPRINTF(RxuEW, "Current wb cycle: %i, width: %i, numInst: %i\nwbActual:%i\n",
                    sendCycle + 1, wbWidth, wbNumInst, (sendCycle + 1) * wbWidth + wbNumInst);
            // Add finished instruction to queue to commit.
            (*ewQueue)[sendCycle].insts[wbNumInst] = inst;
            (*ewQueue)[sendCycle].size++;

            instsToWB.push_back(inst);
        }

        void
        EW::processVectorLatency(const DynInstPtr &inst)
        {
            assert(!cpu->switchedOut());
            wakeCPU();
            DPRINTF(RxuEW,
                    "vector instruction [sn:%llu] with PC %s is executing arrive latency, ready to WB/commit next cycle.\n",
                    inst->seqNum, inst->pcState());

            int sendCycle = -1;

            while ((*ewQueue)[sendCycle].insts[wbNumInst])
            {
                ++wbNumInst;
                if (wbNumInst == wbWidth)
                {
                    ++sendCycle;
                    wbNumInst = 0;
                }
            }

            DPRINTF(RxuEW, "Current wb cycle: %i, width: %i, numInst: %i\nwbActual:%i\n",
                    sendCycle + 1, wbWidth, wbNumInst, (sendCycle + 1) * wbWidth + wbNumInst);
            // Add finished instruction to queue to commit.
            (*ewQueue)[sendCycle].insts[wbNumInst] = inst;
            (*ewQueue)[sendCycle].size++;
            DPRINTF(RxuEW, "[sn:%llu] instruction is ready to WB and commit next cycle,"
                               " PC: %s "
                               "\n",
                        inst->seqNum, inst->pcState());
            instsToWB.push_back(inst);
        }

        void
        EW::processLdstEarlydone(const DynInstPtr &inst)
        {
            if (inst->isSquashed())
            {
                return;
            }
            if (inst->isLoad() && inst->destRegIdx(0).classValue() == FloatRegClass)
            {
                dispipe3Stage->wtb.fldFIFO.push_back(inst);
            }
            else
            {
                DPRINTF(RxuEW, "Load/store early wake up.\n");
                DPRINTF(RxuEW, "Instruction [sn:%i] is: %s.\n", inst->seqNum,
                        inst->staticInst->disassemble(inst->pc->instAddr()));

                dispipe3Stage->rmu->wakeDependents(inst);

                for (int i = 0; i < inst->numDestRegs(); i++)
                {
                    // Mark register as ready if not pinned
                    if (inst->renamedDestIdx(i)->getNumPinnedWritesToComplete() == 0)
                    {
                        DPRINTF(RxuEW, "Setting Destination Register %i (%s)\n",
                                inst->renamedDestIdx(i)->index(),
                                inst->renamedDestIdx(i)->className());
                        scoreboard->setReg(inst->renamedDestIdx(i));
                    }
                }
            }
        }

        void
        EW::fuProcess(ThreadID tid)
        {
            std::deque<DynInstPtr> &process_List = insts[tid];
            int available_insts = process_List.size();
            DPRINTF(RxuEW,
                    "%i instructions is entering FU.\n", available_insts);
            for (int i = 0; i < available_insts; i++)
            {

                DynInstPtr inst = process_List.front();
                process_List.pop_front();

                if (inst->isSquashed())
                {
                    DPRINTF(RxuEW,
                            "[tid:%i] "
                            "instruction [sn:%llu] with PC %s is squashed, skipping.\n",
                            tid, inst->seqNum, inst->pcState());
                    ++stats.squashedInsts;
                    continue;
                }

                OpClass op_class = inst->opClass();
                int idx = RxuFUPool::NoCapableFU;
                Cycles op_latency = Cycles(1);

                if (op_class != No_OpClass)
                {
                    idx = fuPool->getUnit(op_class);
                    if (idx > RxuFUPool::NoFreeFU)
                    {
                        if (op_class == 3 || op_class == 9 || op_class == 11)
                        {
                            op_latency = Cycles(dispipe3Stage->wtb.initFdivDelay);
                        }
                        else
                        {
                            op_latency = fuPool->getOpLatency(op_class);
                        }
                    }
                }

                if (idx != RxuFUPool::NoFreeFU)
                {
                    if (op_latency == Cycles(1))
                    {

                        instsToExecute.push_back(inst);

                        DPRINTF(RxuEW,
                                "[tid:%i] "
                                "instruction [sn:%llu] with PC %s is ready to execute next cycle.\n",
                                tid, inst->seqNum, inst->pcState());

                        if (idx >= 0)
                            fuPool->freeUnitNextCycle(idx);
                    }
                    else
                    {
                        bool pipelined = fuPool->isPipelined(op_class);

                        FUCompletion *execution = new FUCompletion(inst,
                                                                   idx, this);

                        cpu->schedule(execution,
                                      cpu->clockEdge(Cycles(op_latency - 2)));

                        if (!pipelined)
                        {
                            execution->setFreeFU();
                        }
                        else
                        {
                            fuPool->freeUnitNextCycle(idx);
                        }
                    }
                    DPRINTF(RxuEW,
                            "[tid:%i] "
                            "instruction [sn:%llu] with PC %s enters FU.\n",
                            tid, inst->seqNum, inst->pcState());
                }
                else
                {
                    process_List.push_back(inst);
                    DPRINTF(RxuEW,
                            "[tid:%i] "
                            "instruction [sn:%llu] with PC %s has no FU.\n",
                            tid, inst->seqNum, inst->pcState());
                }
            }
        }

        void
        EW::executeInsts()
        {
            while (!instsToExecute.empty())
            {
                OrderQueue.push(instsToExecute.front());
                instsToExecute.pop_front();
            }

            while (!OrderQueue.empty())
            {
                instsToExecute.push_back(OrderQueue.top());
                OrderQueue.pop();
            }

            wbNumInst = 0;
            wbCycle = 0;

            DynInstPtr inst_rbk = nullptr;

            std::list<ThreadID>::iterator threads = activeThreads->begin();
            std::list<ThreadID>::iterator end = activeThreads->end();

            while (threads != end)
            {
                ThreadID tid = *threads++;
                fetchRedirect[tid] = false;
            }

            // Uncomment this if you want to see all available instructions.
            // @todo This doesn't actually work anymore, we should fix it.
            //    printAvailableInsts();

            // Execute/writeback any instructions that are available.
            int insts_to_execute = instsToExecute.size();
            int inst_num = 0;
            for (; inst_num < insts_to_execute; ++inst_num)
            {   

                DPRINTF(RxuEW, "Execute: Executing instructions.\n");

                DynInstPtr inst = instsToExecute.front();
                instsToExecute.pop_front();

                if(inst_rbk){
                    if(inst->seqNum > inst_rbk->seqNum){
                        DPRINTF(RxuEW, "inst [sn:%lli] squashed because of rbk from [sn:%lli].\n",inst->seqNum,
                        inst_rbk->seqNum);
                        inst->setSquashed();
                        continue;
                    }
                }
                DPRINTF(RxuEW, "Execute: Processing PC %s, [tid:%i] [sn:%llu].\n",
                        inst->pcState(), inst->threadNumber, inst->seqNum);

                // Notify potential listeners that this instruction has started
                // executing
                ppExecute->notify(inst);

                // Check if the instruction is squashed; if so then skip it
                if (inst->isSquashed())
                {
                    DPRINTF(RxuEW, "Execute: Instruction was squashed. PC: %s, [tid:%i]"
                                   " [sn:%llu]\n",
                            inst->pcState(), inst->threadNumber,
                            inst->seqNum);

                    // Consider this instruction executed so that commit can go
                    // ahead and retire the instruction.
                    inst->setExecuted();

                    // Not sure if I should set this here or just let commit try to
                    // commit any squashed instructions.  I like the latter a bit more.
                    inst->setCanCommit();

                    ++stats.squashedInsts;

                    continue;
                }

                Fault fault = NoFault;

                // Execute instruction.
                // Note that if the instruction faults, it will be handled
                // at the commit stage.
                if (inst->isMemRef())
                {
                    DPRINTF(RxuEW, "Execute: Calculating address for memory "
                                   "reference.\n");

                    DPRINTF(Spike, "Execute: Calculating address for memory "
                                   "reference.\n");

                    // Tell the LDSTQ to execute this instruction (if it is a load).
                    if (inst->isAtomic())
                    {
                        // AMOs are treated like store requests
                        fault = dispipe3Stage->ldstQueue.executeStore(inst);

                        if (inst->isTranslationDelayed() &&
                            fault == NoFault)
                        {
                            // A hw page table walk is currently going on; the
                            // instruction must be deferred.
                            DPRINTF(RxuEW, "Execute: Delayed translation, deferring "
                                           "store.\n");
                            dispipe3Stage->wtb.deferMemInst(inst);
                            continue;
                        }
                    }
                    else if (inst->isLoad())
                    {
                        // Loads will mark themselves as executed, and their writeback
                        // event adds the instruction to the queue to commit
                        inst->loadExeTick = curTick();
                        fault = dispipe3Stage->ldstQueue.executeLoad(inst);

                        if (inst->isTranslationDelayed() &&
                            fault == NoFault)
                        {
                            // A hw page table walk is currently going on; the
                            // instruction must be deferred.
                            DPRINTF(RxuEW, "Execute: Delayed translation, deferring "
                                           "load.\n");
                            dispipe3Stage->wtb.deferMemInst(inst);
                            continue;
                        }

                        if (inst->isDataPrefetch() || inst->isInstPrefetch())
                        {
                            inst->fault = NoFault;
                        }
                    }
                    else if (inst->isStore())
                    {
                        fault = dispipe3Stage->ldstQueue.executeStore(inst);

                        if (inst->isTranslationDelayed() &&
                            fault == NoFault)
                        {
                            // A hw page table walk is currently going on; the
                            // instruction must be deferred.
                            DPRINTF(RxuEW, "Execute: Delayed translation, deferring "
                                           "store.\n");
                            dispipe3Stage->wtb.deferMemInst(inst);
                            continue;
                        }

                        // If the store had a fault then it may not have a mem req
                        if (((fault != NoFault || !inst->readPredicate() ||
                              !inst->isStoreConditional()) &&
                             (inst->stdIssued)) ||
                            inst->isNonSpeculative() ||
                            inst->tlbfault ||
                            inst->staticInst->vElemMask)
                        {
                            // If the instruction faulted, then we need to send it
                            // along to commit without the instruction completing.
                            // Send this instruction to commit, also make sure iew
                            // stage realizes there is activity.
                            if (!(inst->if_exe_second && inst->num_exe != 2) || inst->isNonSpeculative() || fault != NoFault || inst->tlbfault || inst->staticInst->vElemMask)
                            {
                                inst->setExecuted();
                                instToCommit(inst);
                                activityThisCycle();
                            }
                        }

                        // Store conditionals will mark themselves as
                        // executed, and their writeback event will add the
                        // instruction to the queue to commit.
                    }
                    else
                    {
                        panic("Unexpected memory type!\n");
                    }
                }
                else
                {
                    // If the instruction has already faulted, then skip executing it.
                    // Such case can happen when it faulted during ITLB translation.
                    // If we execute the instruction (even if it's a nop) the fault
                    // will be replaced and we will lose it.
                    if (inst->getFault() == NoFault)
                    {
                        if (!inst->isSpecialVectorNotExec())
                            inst->execute();
                            if (inst->numDestRegs() > 0 && !inst->destRegIdx(0).isZeroReg()){
                                PhysRegIdPtr phys_reg = inst->renamedDestIdx(0);
                                volatile __uint128_t dest_val = 0;
                                dest_val = cpu->getReg(phys_reg, 0);
                                if(cpu->regtable[inst->destRegIdx(0)] == false && cpu->RegSnMap[inst->destRegIdx(0)] == inst->seqNum) {
                                    cpu->regtable[inst->destRegIdx(0)] = true;
                                    cpu->digestMap[inst->destRegIdx(0)] = make_int_digest((uint64_t)dest_val, inst->destRegIdx(0));
                                } 
                            }
                        if (inst->isMicroVector() && inst->ori_inst->isSpecialVectorNotExec()) {
                            cpu->dispipe3.rmu->wakeDependents(inst);
                        } else if (inst->isSpecialMacro() && !inst->isSpecialVectorNotExec()) {
                            for (auto uop : inst->uopQueue) {
                                cpu->dispipe3.rmu->wakeDependents(uop);
                            }
                            VSpecialCommit *vSpecialCommit = new VSpecialCommit(inst, this);
                            cpu->schedule(vSpecialCommit, cpu->clockEdge(Cycles(1)));
                        }
                        
                        if (!inst->readPredicate())
                            inst->forwardOldRegs();
                    }

                    inst->setExecuted();

                    instToCommit(inst);
                }

                // updateExeInstStats(inst);

                // Check if branch prediction was correct, if not then we need
                // to tell commit to squash in flight instructions.  Only
                // handle this if there hasn't already been something that
                // redirects fetch in this group of instructions.

                // This probably needs to prioritize the redirects if a different
                // scheduler is used.  Currently the scheduler schedules the oldest
                // instruction first, so the branch resolution order will be correct.
                ThreadID tid = inst->threadNumber;

                // if (inst->staticInst->isCondCtrl() && ((*inst->predPC).instAddr() <= (*inst->pc).instAddr())) {
                //     auto loop_it = loopTable.find((*inst->pc).instAddr());
                //     if (loop_it != loopTable.end()) {
                //         if (loop_it->second.target_pc == (*inst->predPC).instAddr()) {
                //             loop_it->second.times++;
                //         }
                //     } else {
                //         loopEntry loop;
                //         loop.times = 1;
                //         loop.target_pc = (*inst->predPC).instAddr();
                //         loopTable[(*inst->pc).instAddr()] = loop;
                //     }
                // }

                if (!fetchRedirect[tid] ||
                    !toCommit->squash[tid] ||
                    toCommit->squashedSeqNum[tid] > inst->seqNum)
                {

                    // Prevent testing for misprediction on load instructions,
                    // that have not been executed.
                    bool loadNotExecuted = !inst->isExecuted() && inst->isLoad();

                    if (inst->isControl() && inst->mispredicted() && !loadNotExecuted)
                    {
                        fetchRedirect[tid] = true;

                        if (inst->isVset()) DPRINTF(RxuEW, "vset mispredict[sn:%llu], vl = %d\n", 
                        inst->seqNum, inst->pcState().as<gem5::RiscvISA::PCState>()._vl);

                        DPRINTF(RxuEW, "[tid:%i] [sn:%llu] Execute: "
                                       "Branch mispredict detected.\n",
                                tid, inst->seqNum);
                        DPRINTF(RxuEW, "[tid:%i] [sn:%llu] "
                                       "Predicted target was PC: %s\n",
                                tid, inst->seqNum, inst->readPredTarg());
                        DPRINTF(RxuEW, "[tid:%i] [sn:%llu] Execute: "
                                       "Redirecting fetch to PC: %s\n",
                                tid, inst->seqNum, inst->pcState());
                        // If incorrect, then signal the ROB that it must be squashed.
                        squashDueToBranch(inst, tid);

                        ppMispredict->notify(inst);

                        if (inst->readPredTaken())
                        {
                            stats.predictedTakenIncorrect++;
                        }
                        else
                        {
                            stats.predictedNotTakenIncorrect++;
                        }
                    }
                    else if (dispipe3Stage->ldstQueue.violation(tid))
                    {
                        assert(inst->isMemRef());
                        // If there was an ordering violation, then get the
                        // DynInst that caused the violation.  Note that this
                        // clears the violation signal.
                        DynInstPtr violator;
                        violator = dispipe3Stage->ldstQueue.getMemDepViolator(tid);

                        DPRINTF(RxuEW, "LDSTQ detected a violation. Violator PC: %s "
                                       "[sn:%lli], inst PC: %s [sn:%lli]. Addr is: %#x.\n",
                                violator->pcState(), violator->seqNum,
                                inst->pcState(), inst->seqNum, inst->physEffAddr);

                        DPRINTF(RxuRBK, "LDSTQ detected a violation. Violator PC: %s "
                                        "[sn:%lli], inst PC: %s [sn:%lli]. Addr is: %#x.\n",
                                violator->pcState(), violator->seqNum,
                                inst->pcState(), inst->seqNum, inst->physEffAddr);

                        fetchRedirect[tid] = true;

                        // Tell the instruction queue that a violation has occured.
                        dispipe3Stage->wtb.memDepUnit[tid].violation(inst, violator);

                        // Squash.
                        squashDueToMemOrder(violator, tid);

                        if(violator->isVector()){
                            if(violator->isMicroVector() && violator->isEop()){
                                inst_rbk = violator->ori_inst->ori_inst;
                            }
                            else if(violator->isMicroVector()){
                                inst_rbk = violator->ori_inst;
                            }
                            else {
                                inst_rbk = violator;
                            }
                        }

                        ++stats.memOrderViolationEvents;
                        cpu->loopStats[cpu->loopIndex]->rbkCount++;
                        cpu->loopStats[cpu->loopxhIndex]->xhrbkCount++;
                        cpu->commit.stats.rbkCount++;
                    }
                }
                else
                {
                    // Reset any state associated with redirects that will not
                    // be used.
                    if (dispipe3Stage->ldstQueue.violation(tid))
                    {
                        assert(inst->isMemRef());

                        DynInstPtr violator = dispipe3Stage->ldstQueue.getMemDepViolator(tid);

                        DPRINTF(RxuEW, "LDSTQ detected a violation.  Violator PC: "
                                       "%s, inst PC: %s.  Addr is: %#x.\n",
                                violator->pcState(), inst->pcState(),
                                inst->physEffAddr);
                        DPRINTF(RxuRBK, "LDSTQ detected a violation.  Violator PC: "
                                        "%s, inst PC: %s.  Addr is: %#x.\n",
                                violator->pcState(), inst->pcState(),
                                inst->physEffAddr);
                        DPRINTF(RxuEW, "Violation will not be handled because "
                                       "already squashing\n");

                        ++stats.memOrderViolationEvents;
                        cpu->loopStats[cpu->loopIndex]->rbkCount++;
                        cpu->loopStats[cpu->loopxhIndex]->xhrbkCount++;
                        cpu->commit.stats.rbkCount++;
                    }
                }
            }

            // Update and record activity if we processed any instructions.
            if (inst_num)
            {
                cpu->activityThisCycle();
            }

            // Need to reset this in case a writeback event needs to write into the
            // iew queue.  That way the writeback event will write into the correct
            // spot in the queue.
            wbNumInst = 0;
        }

        void
        EW::writebackInsts()
        {
            // Loop through the head of the time buffer and wake any
            // dependents.  These instructions are about to write back.  Also
            // mark scoreboard that this instruction is finally complete.
            // Either have IEW have direct access to scoreboard, or have this
            // as part of backwards communication.
            int destIntWriteNums = 0;
            int destFloatWriteNums = 0;
            int size = instsToWB.size();
            // int fpWbWidth = 4;
            // int intWbWidth = 6;
            for (int inst_num = 0; inst_num < size; inst_num++)
            {
                if (instsToWB.empty())
                {
                    break;
                }
                DynInstPtr inst = instsToWB.front();
                instsToWB.pop_front();

                if (inst->isSquashed())
                {
                    DPRINTF(RxuEW,
                            "[tid:%i] "
                            "instruction [sn:%llu] with PC %s is squashed, skipping in writebackInsts.\n",
                            inst->threadNumber, inst->seqNum, inst->pcState());
                    ++stats.squashedInsts;
                    continue;
                }

                DPRINTF(RxuEW, "Instruction write back, [sn:%lli] PC %s.\n",
                        inst->seqNum, inst->pcState());
                ThreadID tid = inst->threadNumber;
                if(inst->isEop()) {
                    cpu->dispipe3.wtb.memDepUnit[tid].completeInst(inst);
                }
                stats.instsToCommit[tid]++;

                // Notify potential listeners that execution is complete for this
                // instruction.
                ppToCommit->notify(inst);

                // Some instructions will be sent to commit without having
                // executed because they need commit to handle them.
                // E.g. Strictly ordered loads have not actually executed when they
                // are first sent to commit.  Instead commit must tell the LSQ
                // when it's ready to execute the strictly ordered load.
                if (!inst->isSquashed() && inst->isExecuted() &&
                    inst->getFault() == NoFault)
                {
                    stats.writebackCount[tid]++;
                    if (inst->numDestRegs() > 0 && !inst->destRegIdx(0).isZeroReg())
                    {
                        if (inst->destRegIdx(0).classValue() == FloatRegClass)
                        {
                            destFloatWriteNums += inst->numDestRegs();
                        }
                        else
                        {
                            destIntWriteNums += inst->numDestRegs();
                        }
                    }

                    if (inst->isCsrFRM())
                    {
                        WriteFrm(inst, tid);

                        cpu->frm.setReady(inst->rn_frm);
                        DPRINTF(RxuEW, "waking up frm (%d): ready.\n", inst->rn_frm);
                        cpu->frm.wakeDependences(inst->rn_frm);
                    }

                    if (inst->isVecCsrVxrm())
                    {
                        WriteVxrm(inst, tid);

                        cpu->vecCsr.setReady(Vxrm, inst->rn_vxrm);
                        DPRINTF(RxuEW, "waking up vxrm (%d): ready.\n", inst->rn_vxrm);
                        cpu->vecCsr.wakeDependences(Vxrm, inst->rn_vxrm);
                    }

                    if (!cpu->vsetBranch) {
                        if (inst->isVecCsrVtype()) {
                            WriteVtype(inst, tid);
    
                            cpu->vecCsr.setReady(Vtype, inst->rn_vtype);
                            DPRINTF(RxuEW, "waking up vtype (%d): ready.\n", inst->rn_vtype);
                            cpu->vecCsr.wakeDependences(Vtype, inst->rn_vtype);    
                        }
    
                        if (inst->isVsetvl() || inst->isVlff() && inst->isLastMicroop()) {
                            WriteVl(inst, tid);
    
                            cpu->vecCsr.setReady(Vl, inst->rn_vl);
                            DPRINTF(RxuEW, "waking up vl (%d): ready.\n", inst->rn_vl);
                            cpu->vecCsr.wakeDependences(Vl, inst->rn_vl);    
                        }
    
                        if (inst->isCsrVl()) {
                            WriteCsrVl(inst, tid);
    
                            cpu->vecCsr.setReady(Vl, inst->rn_vl);
                            DPRINTF(RxuEW, "waking up csr vl (%d): ready.\n", inst->rn_vl);
                            cpu->vecCsr.wakeDependences(Vl, inst->rn_vl);    
                        }
    
                        if (inst->isCsrVtype()) {
                            WriteCsrVtype(inst, tid);
    
                            cpu->vecCsr.setReady(Vtype, inst->rn_vtype);
                            DPRINTF(RxuEW, "waking up csr vtype (%d): ready.\n", inst->rn_vtype);
                            cpu->vecCsr.wakeDependences(Vtype, inst->rn_vtype);    
                        }
                    }
                }
            }

            while (1) {
                mergeBufferEntry merge_buffer_entry = cpu->mergeBuffer.checkAndFetchEntry();
                RiscvISA::VecRegContainer *data = merge_buffer_entry.data;
                if (data == NULL) {
                    break;
                }
                DynInstPtr inst = merge_buffer_entry.inst;
                auto vd_value = (*data).as<uint8_t>();
                StaticInstPtr static_inst = inst->staticInst;
                auto &tmp_d0 = *(RiscvISA::VecRegContainer *)cpu->getWritableReg(inst->renamedDestIdx(0), inst->threadNumber);
                auto Vd = tmp_d0.as<uint8_t>();
                memcpy(Vd, vd_value, 16);

                inst->setExecuted();
                inst->setCanCommit();
                DPRINTF(RxuEW, "[sn:%lli] mergebuffer entry writeback\n", inst->seqNum);
                DynInstPtr inst_ptr = DynInstPtr(inst);
                cpu->dispipe3.rmu->wakeDependents(inst);
                delete data;
            }

            if (destIntWriteNums == 0)
            {
                stats.regIntWrite0++;
            }
            else if (destIntWriteNums == 1)
            {
                stats.regIntWrite1++;
            }
            else if (destIntWriteNums == 2)
            {
                stats.regIntWrite2++;
            }
            else if (destIntWriteNums == 3)
            {
                stats.regIntWrite3++;
            }
            else if (destIntWriteNums == 4)
            {
                stats.regIntWrite4++;
            }
            else if (destIntWriteNums == 5)
            {
                stats.regIntWrite5++;
            }
            else if (destIntWriteNums == 6)
            {
                stats.regIntWrite6++;
            }
            else if (destIntWriteNums == 7)
            {
                stats.regIntWrite7++;
            }
            else if (destIntWriteNums == 8)
            {
                stats.regIntWrite8++;
            }
            else if (destIntWriteNums == 9)
            {
                stats.regIntWrite9++;
            }
            else if (destIntWriteNums == 10)
            {
                stats.regIntWrite10++;
            }
            else
            {
                stats.regIntWriteOver10++;
            }

            if (destFloatWriteNums == 0)
            {
                stats.regFloatWrite0++;
            }
            else if (destFloatWriteNums == 1)
            {
                stats.regFloatWrite1++;
            }
            else if (destFloatWriteNums == 2)
            {
                stats.regFloatWrite2++;
            }
            else if (destFloatWriteNums == 3)
            {
                stats.regFloatWrite3++;
            }
            else if (destFloatWriteNums == 4)
            {
                stats.regFloatWrite4++;
            }
            else if (destFloatWriteNums == 5)
            {
                stats.regFloatWrite5++;
            }
            else if (destFloatWriteNums == 6)
            {
                stats.regFloatWrite6++;
            }
            else if (destFloatWriteNums == 7)
            {
                stats.regFloatWrite7++;
            }
            else if (destFloatWriteNums == 8)
            {
                stats.regFloatWrite8++;
            }
            else if (destFloatWriteNums == 9)
            {
                stats.regFloatWrite9++;
            }
            else if (destFloatWriteNums == 10)
            {
                stats.regFloatWrite10++;
            }
            else
            {
                stats.regFloatWriteOver10++;
            }
        }

        void
        EW::ew(ThreadID tid)
        {
            if (ewStatus[tid] == Blocked)
            {
                ++stats.blockCycles;
                writebackInsts();
                fuProcess(tid);
                executeInsts();
                
                DPRINTF(RxuEW, "[tid:%i] ewStatus: Blocked.\n", tid);
            }
            else if (ewStatus[tid] == Squashing || ewStatus[tid] == RobSquashing)
            {
                ++stats.squashCycles;
                writebackInsts();
                fuProcess(tid);
                executeInsts();
                
                DPRINTF(RxuEW, "[tid:%i] ewStatus: Squashing.\n", tid);
            }
            else if (ewStatus[tid] == Running || ewStatus[tid] == Idle)
            {

                if (instsToExecute.size() == 0 && instsToWB.size() == 0)
                {
                    ++stats.idleCycles;
                    DPRINTF(RxuEW, "[tid:%i] ewStatus: Idle.\n", tid);
                }
                else
                {
                    ++stats.runCycles;
                    DPRINTF(RxuEW, "[tid:%i] ewStatus: Running.\n", tid);
                }

                writebackInsts();
                fuProcess(tid);
                executeInsts();
                
            }
            else if (ewStatus[tid] == Unblocking)
            {

                ++stats.unblockCycles;
                DPRINTF(RxuEW, "[tid:%i] ewStatus: Unblocking.\n", tid);

                writebackInsts();
                executeInsts();
                fuProcess(tid);

                unblock(tid);
            }

            // writebackInsts();
            // executeInsts();
            // fuProcess(tid);
        }

        void
        EW::tick()
        {
            wbNumInst = 0;
            wbCycle = 0;

            wroteToTimeBuffer = false;

            sortInsts();

            // Free function units marked as being freed this cycle.
            fuPool->processFreeUnits();

            std::list<ThreadID>::iterator threads = activeThreads->begin();
            std::list<ThreadID>::iterator end = activeThreads->end();

            // Check stall and squash signals, process any instructions.
            while (threads != end)
            {
                ThreadID tid = *threads++;

                DPRINTF(RxuEW, "EW: Processing [tid:%i]\n", tid);

                checkSignalsAndUpdate(tid);

                ew(tid);
            }

            bool broadcast_free_entries = false;

            if (updateLSQNextCycle)
            {
                updateLSQNextCycle = false;

                broadcast_free_entries = true;
            }

            // Writeback any stores using any leftover bandwidth.
            dispipe3Stage->ldstQueue.writebackStores();

            // Check the committed load/store signals to see if there's a load
            // or store to commit.  Also check if it's being told to execute a
            // nonspeculative instruction.
            // This is pretty inefficient...

            threads = activeThreads->begin();
            while (threads != end)
            {
                ThreadID tid = (*threads++);

                DPRINTF(RxuEW, "Processing [tid:%i]\n", tid);

                // Update structures based on instructions committed.
                if (fromCommit->commitInfo[tid].doneSeqNum != 0 &&
                    !fromCommit->commitInfo[tid].squash &&
                    !fromCommit->commitInfo[tid].robSquashing)
                {

                    dispipe3Stage->ldstQueue.commitStores(fromCommit->commitInfo[tid].doneSeqNum, tid);

                    dispipe3Stage->ldstQueue.commitLoads(fromCommit->commitInfo[tid].doneSeqNum, tid);

                    updateLSQNextCycle = true;
                }

                if (fromCommit->commitInfo[tid].nonSpecSeqNum != 0)
                {

                    DPRINTF(RxuEW, "NonspecInst from thread %i.\n", tid);
                    if (fromCommit->commitInfo[tid].strictlyOrdered)
                    {
                        dispipe3Stage->wtb.replayMemInst(
                            fromCommit->commitInfo[tid].strictlyOrderedLoad);
                        fromCommit->commitInfo[tid].strictlyOrderedLoad->setAtCommit();
                    }
                    else
                    {
                        dispipe3Stage->wtb.scheduleNonSpec(
                            fromCommit->commitInfo[tid].nonSpecSeqNum);
                    }
                }

                if (broadcast_free_entries)
                {
                    wroteToTimeBuffer = true;
                }
            }

            updateStatus();

            if (wroteToTimeBuffer)
            {
                DPRINTF(RxuActivity, "Activity this cycle.\n");
                cpu->activityThisCycle();
            }
        }

        void
        EW::checkMisprediction(const DynInstPtr &inst)
        {
            ThreadID tid = inst->threadNumber;

            if (!fetchRedirect[tid] ||
                !toCommit->squash[tid] ||
                toCommit->squashedSeqNum[tid] > inst->seqNum)
            {

                if (inst->isControl() && inst->mispredicted())
                {
                    fetchRedirect[tid] = true;

                    if (inst->isVset()) DPRINTF(RxuEW, "vset mispredict[sn:%llu], vl = %d\n", 
                    inst->seqNum, inst->pcState().as<gem5::RiscvISA::PCState>()._vl);

                    DPRINTF(RxuEW, "[tid:%i] [sn:%llu] Execute: "
                                   "Branch mispredict detected.\n",
                            tid, inst->seqNum);
                    DPRINTF(RxuEW, "[tid:%i] [sn:%llu] Predicted target was PC: %s\n",
                            tid, inst->seqNum, inst->readPredTarg());
                    DPRINTF(RxuEW, "[tid:%i] [sn:%llu] Execute: "
                                   "Redirecting fetch to PC: %s\n",
                            tid, inst->seqNum, inst->pcState());
                    // If incorrect, then signal the ROB that it must be squashed.
                    squashDueToBranch(inst, tid);

                    if (inst->readPredTaken())
                    {
                        stats.predictedTakenIncorrect++;
                    }
                    else
                    {
                        stats.predictedNotTakenIncorrect++;
                    }
                }
            }
        }

        bool
        EW::PqCompare::operator()(
            const DynInstPtr &lhs, const DynInstPtr &rhs) const
        {
            return lhs->seqNum > rhs->seqNum;
        }

        void
        EW::WriteFrm(DynInstPtr inst, ThreadID tid)
        {
            RegVal imm = inst->staticInst->machInst.vecimm;
            std::string inst_name = inst->staticInst->getName();
            RegVal newFRMVal = 0;
            RegVal oldFRMVal = cpu->frm.getVal(inst->rn_frm);

            if ((inst_name == "csrrs" || inst_name == "csrrc") && inst->srcRegIdx(0).isZeroReg()
                || (inst_name == "csrrsi" || inst_name == "csrrci") && (imm == 0))
            {
                cpu->frm.setVal(inst->rn_frm, oldFRMVal);
                DPRINTF(RxuEW, "Writing MiscReg frm (%d): %#x.\n", inst->rn_frm, oldFRMVal);
                return;
            }

            if (inst_name == "csrrw")
            {
                newFRMVal = cpu->getReg(inst->renamedSrcIdx(0), tid);
            }
            else if (inst_name == "csrrwi")
            {
                newFRMVal = imm;
            }
            else if (inst_name == "csrrs")
            {
                newFRMVal = cpu->getReg(inst->renamedSrcIdx(0), tid);
                newFRMVal |= oldFRMVal;
            }
            else if (inst_name == "csrrsi")
            {
                newFRMVal = imm;
                newFRMVal |= oldFRMVal;
            }
            else if (inst_name == "csrrc")
            {
                newFRMVal = cpu->getReg(inst->renamedSrcIdx(0), tid);
                newFRMVal = ~newFRMVal & oldFRMVal;
            }
            else if (inst_name == "csrrci")
            {
                newFRMVal = imm;
                newFRMVal = ~newFRMVal & oldFRMVal;
            }

            cpu->frm.setVal(inst->rn_frm, newFRMVal);
            DPRINTF(RxuEW, "Writing MiscReg frm (%d): %#x.\n", inst->rn_frm, newFRMVal);
        }

        void
        EW::WriteVxrm(DynInstPtr inst, ThreadID tid)
        {
            RegVal imm = inst->staticInst->machInst.vecimm;
            std::string inst_name = inst->staticInst->getName();
            RegVal newVxrmVal = 0;
            RegVal oldVxrmVal = cpu->vecCsr.getVal(Vxrm, inst->rn_vxrm);

            if ((inst_name == "csrrs" || inst_name == "csrrc") && inst->srcRegIdx(0).isZeroReg()
                || (inst_name == "csrrsi" || inst_name == "csrrci") && (imm == 0))
            {
                cpu->vecCsr.setVal(Vxrm, inst->rn_vxrm, oldVxrmVal);
                DPRINTF(RxuEW, "Writing MiscReg vxrm (%d): %#x.\n", inst->rn_vl, oldVxrmVal);
                return;
            }

            if (inst_name == "csrrw")
            {
                newVxrmVal = cpu->getReg(inst->renamedSrcIdx(0), tid);
            }
            else if (inst_name == "csrrwi")
            {
                newVxrmVal = imm;
            }
            else if (inst_name == "csrrs")
            {
                newVxrmVal = cpu->getReg(inst->renamedSrcIdx(0), tid);
                newVxrmVal |= oldVxrmVal;
            }
            else if (inst_name == "csrrsi")
            {
                newVxrmVal = imm;
                newVxrmVal |= oldVxrmVal;
            }
            else if (inst_name == "csrrc")
            {
                newVxrmVal = cpu->getReg(inst->renamedSrcIdx(0), tid);
                newVxrmVal = ~newVxrmVal & oldVxrmVal;
            }
            else if (inst_name == "csrrci")
            {
                newVxrmVal = imm;
                newVxrmVal = ~newVxrmVal & oldVxrmVal;
            }

            cpu->vecCsr.setVal(Vxrm, inst->rn_vxrm, newVxrmVal);
            DPRINTF(RxuEW, "Writing MiscReg vxrm (%d): %#x.\n", inst->rn_vxrm, newVxrmVal);
        }

        void
        EW::WriteVl(DynInstPtr inst, ThreadID tid)
        {   
            if (inst->isVlff() && inst->isLastMicroop()) {
                RegVal vlValue = (inst->pcState()).as<gem5::RiscvISA::PCState>().vl();
                cpu->vecCsr.setVal(Vl, inst->rn_vl, vlValue);
                DPRINTF(RxuEW, "Vlff writing MiscReg vl (%d): %#x.\n", inst->rn_vl, vlValue);                                                
                return;
            }
            std::string inst_name = inst->staticInst->getName();

            RegVal newVlVal = 0;
            RegVal oldVlVal = cpu->vecCsr.getVal(Vl, inst->prev_vl);
            RegVal reqVl = 0;
            
            if (!inst->srcRegIdx(0).isZeroReg()) {
                reqVl =  cpu->getReg(inst->renamedSrcIdx(0), tid);                
            }

            RegVal zimm_vsetivli = inst->staticInst->machInst.zimm_vsetivli;
            RegVal uimm_vsetivli = inst->staticInst->machInst.uimm_vsetivli;
            RegVal zimm_vsetvli = inst->staticInst->machInst.zimm_vsetvli;

            RegVal vsew = inst_name == "vsetvli" ? 8 << (cpu->extractBits(zimm_vsetvli, 3, 3)) : 
            8 << (cpu->extractBits(cpu->getReg(inst->renamedSrcIdx(1), tid), 3, 3));

            RegVal zero = inst_name == "vsetvli" ? cpu->extractBits(zimm_vsetvli, 8, 3) :
            cpu->extractBits(cpu->getReg(inst->renamedSrcIdx(1), tid), 8, 56);

            int vlmul = inst_name == "vsetvli" ? cpu->extractBits(zimm_vsetvli, 0, 3): 
            cpu->extractBits(cpu->getReg(inst->renamedSrcIdx(1), tid), 0, 3);

            if (vlmul >= 5) vlmul = -(8-vlmul);

            float vflmul = vlmul >= 0 ? 1 << vlmul : 1.0 / (1 << -vlmul);

            RegVal newVill = !(vflmul >= 0.125 && vflmul <= 8) || vsew > std::min(vflmul, 1.0f) * 64 ||
                             zero != 0;
            RegVal vlmax = newVill ? 0 : (128/vsew) * vflmul;


            if (inst_name == "vsetvli" || inst_name == "vsetvl") {
                if (vlmax == 0) {
                    newVlVal = 0;
                } else if (!inst->srcRegIdx(0).isZeroReg()) {
                    newVlVal = reqVl > vlmax ? vlmax : reqVl;
                } else if (!inst->destRegIdx(0).isZeroReg()) {
                    newVlVal = vlmax;  
                } else {
                    newVlVal = oldVlVal > vlmax ? vlmax : oldVlVal;
                }
                cpu->vecCsr.setVal(Vl, inst->rn_vl, newVlVal);
                DPRINTF(RxuEW, "Writing MiscReg vl (%d): %#x.\n", inst->rn_vl, newVlVal);               
            }

        }

        void
        EW::WriteVtype(DynInstPtr inst, ThreadID tid)
        {
            std::string inst_name = inst->staticInst->getName();

            RegVal newVtypeVal = 0;
            RegVal oldVtypeVal = cpu->vecCsr.getVal(Vtype, inst->rn_vtype);
            RegVal reqVtype =  cpu->getReg(inst->renamedSrcIdx(1), tid);

            RegVal vsew = 8 << (cpu->extractBits(reqVtype, 3, 3));

            int vlmul =cpu->extractBits(reqVtype, 0, 3);

            if (vlmul >= 5) vlmul = -(8-vlmul);

            RegVal zero = cpu->extractBits(reqVtype, 8, 56);

            float vflmul = vlmul >= 0 ? 1 << vlmul : 1.0 / (1 << -vlmul);

            RegVal newVill = !(vflmul >= 0.125 && vflmul <= 8) || vsew > std::min(vflmul, 1.0f) * 64 ||
                             zero != 0;
            
            if (newVill) {
                newVtypeVal = 1ULL << 63;
            } else if (oldVtypeVal == reqVtype) {
                newVtypeVal = oldVtypeVal;
            } else {
                newVtypeVal = reqVtype; 
            }

            cpu->vecCsr.setVal(Vtype, inst->rn_vtype, newVtypeVal);
            DPRINTF(RxuEW, "Writing MiscReg vtype (%d): %#x.\n", inst->rn_vtype, newVtypeVal);  
            
        }

        void
        EW::WriteCsrVl(DynInstPtr inst, ThreadID tid)
        {
            RegVal imm = inst->staticInst->machInst.vecimm;
            std::string inst_name = inst->staticInst->getName();
            RegVal newVlVal = 0;
            RegVal oldVlVal = cpu->vecCsr.getVal(Vl, inst->prev_vl);

            if ((inst_name == "csrrs" || inst_name == "csrrc") && inst->srcRegIdx(0).isZeroReg()
                || (inst_name == "csrrsi" || inst_name == "csrrci") && (imm == 0))
            {
                cpu->vecCsr.setVal(Vl, inst->rn_vl, oldVlVal);
                DPRINTF(RxuEW, "Writing MiscReg csr vl (%d): %#x.\n", inst->rn_vl, oldVlVal);
                return;
            }

            if (inst_name == "csrrw")
            {
                newVlVal = cpu->getReg(inst->renamedSrcIdx(0), tid);
            }
            else if (inst_name == "csrrwi")
            {
                newVlVal = imm;
            }
            else if (inst_name == "csrrs")
            {
                newVlVal = cpu->getReg(inst->renamedSrcIdx(0), tid);
                newVlVal |= oldVlVal;
            }
            else if (inst_name == "csrrsi")
            {
                newVlVal = imm;
                newVlVal |= oldVlVal;
            }
            else if (inst_name == "csrrc")
            {
                newVlVal = cpu->getReg(inst->renamedSrcIdx(0), tid);
                newVlVal = ~newVlVal & oldVlVal;
            }
            else if (inst_name == "csrrci")
            {
                newVlVal = imm;
                newVlVal = ~newVlVal & oldVlVal;
            }

            cpu->vecCsr.setVal(Vl, inst->rn_vl, newVlVal);
            DPRINTF(RxuEW, "Writing MiscReg csr vl (%d): %#x.\n", inst->rn_vl, newVlVal);          
        }

        void
        EW::WriteCsrVtype(DynInstPtr inst, ThreadID tid)
        {
            RegVal imm = inst->staticInst->machInst.vecimm;
            std::string inst_name = inst->staticInst->getName();
            RegVal newVtypeVal = 0;
            RegVal oldVtypeVal = cpu->vecCsr.getVal(Vtype, inst->rn_vtype);

            if ((inst_name == "csrrs" || inst_name == "csrrc") && inst->srcRegIdx(0).isZeroReg()
                || (inst_name == "csrrsi" || inst_name == "csrrci") && (imm == 0))
            {
                cpu->vecCsr.setVal(Vtype, inst->rn_vtype, oldVtypeVal);
                DPRINTF(RxuEW, "Writing MiscReg csr vtype (%d): %#x.\n", inst->rn_vtype, oldVtypeVal);
                return;
            }

            if (inst_name == "csrrw")
            {
                newVtypeVal = cpu->getReg(inst->renamedSrcIdx(0), tid);
            }
            else if (inst_name == "csrrwi")
            {
                newVtypeVal = imm;
            }
            else if (inst_name == "csrrs")
            {
                newVtypeVal = cpu->getReg(inst->renamedSrcIdx(0), tid);
                newVtypeVal |= oldVtypeVal;
            }
            else if (inst_name == "csrrsi")
            {
                newVtypeVal = imm;
                newVtypeVal |= oldVtypeVal;
            }
            else if (inst_name == "csrrc")
            {
                newVtypeVal = cpu->getReg(inst->renamedSrcIdx(0), tid);
                newVtypeVal = ~newVtypeVal & oldVtypeVal;
            }
            else if (inst_name == "csrrci")
            {
                newVtypeVal = imm;
                newVtypeVal = ~newVtypeVal & oldVtypeVal;
            }

            cpu->vecCsr.setVal(Vtype, inst->rn_vtype, newVtypeVal);
            DPRINTF(RxuEW, "Writing MiscReg csr vtype (%d): %#x.\n", inst->rn_vtype, newVtypeVal);                
        }
                       
    } // namespace rxuo3

} // namespace gem5
