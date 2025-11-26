#ifndef __CPU_RxuO3_CPU_HH__
#define __CPU_RxuO3_CPU_HH__

#include <iostream>
#include <list>
#include <queue>
#include <set>
#include <vector>
#include <chrono>

#include "arch/generic/pcstate.hh"
#include "base/statistics.hh"
#include "cpu/rxuo3/comm.hh"
#include "cpu/rxuo3/commit.hh"
#include "cpu/rxuo3/decode.hh"
// -- add by hongfei.liu-----------------
#include "cpu/rxuo3/bpu0.hh"
#include "cpu/rxuo3/bpu1.hh"
#include "cpu/rxuo3/IBandLB.hh"
#include "cpu/rxuo3/predisq.hh"
#include "cpu/rxuo3/dispipe0.hh"
#include "cpu/rxuo3/dispipe1.hh"
#include "cpu/rxuo3/dispipe2.hh"
#include "cpu/rxuo3/dispipe3.hh"
#include "cpu/rxuo3/RMU.hh"
// --------------------------------------
#include "cpu/rxuo3/dyn_inst_ptr.hh"
#include "cpu/rxuo3/fetch.hh"
#include "cpu/rxuo3/uop_cache.hh"
#include "cpu/rxuo3/rxu_free_list.hh"
#include "cpu/rxuo3/free_list.hh"
#include "cpu/rxuo3/frm.hh"
#include "cpu/rxuo3/veccsr.hh"
#include "cpu/rxuo3/ew.hh"
#include "cpu/rxuo3/limits.hh"
#include "cpu/rxuo3/rename_unit.hh"
#include "cpu/rxuo3/rename.hh"
#include "cpu/rxuo3/rob.hh"
#include "cpu/rxuo3/scoreboard.hh"
#include "cpu/rxuo3/thread_state.hh"
#include "cpu/activity.hh"
#include "cpu/base.hh"
#include "cpu/simple_thread.hh"
#include "cpu/timebuf.hh"
#include "params/BaseRxuO3CPU.hh"
#include "sim/process.hh"
#include "debug/RxuO3CPU.hh"

#include "mem/cache/iprefetch.hh"
namespace gem5
{

template <class>
class Checker;
class ThreadContext;

class Checkpoint;
class Process;

namespace rxuo3
{

class ThreadContext;

struct mergeBufferEntry
{
    RiscvISA::VecRegContainer *data;
    int remainUnmergeEop;
    bool alreadySetOldVd;
    DynInstPtr inst; // first split uop
};

/**
 * RxuO3CPU class, has each of the stages (fetch through commit)
 * within it, as well as all of the time buffers between stages.  The
 * tick() function for the CPU is defined here.
 */
class CPU : public BaseCPU
{
  public:
    typedef std::list<DynInstPtr>::iterator ListIt;

    friend class ThreadContext;

  public:
    enum Status
    {
        Running,
        Idle,
        Halted,
        Blocked,
        SwitchedOut
    };

    BaseMMU *mmu;
    using LSQRequest = LSQ::LSQRequest;

    /** Overall CPU status. */
    Status _status;

  private:

    /** The tick event used for scheduling CPU ticks. */
    EventFunctionWrapper tickEvent;

    /** The exit event used for terminating all ready-to-exit threads */
    EventFunctionWrapper threadExitEvent;

    /** Schedule tick event, regardless of its current state. */
    void
    scheduleTickEvent(Cycles delay)
    {
        if (tickEvent.squashed())
            reschedule(tickEvent, clockEdge(delay));
        else if (!tickEvent.scheduled())
            schedule(tickEvent, clockEdge(delay));
    }

    /** Unschedule tick event, regardless of its current state. */
    void
    unscheduleTickEvent()
    {
        if (tickEvent.scheduled())
            tickEvent.squash();
    }

    /**
     * Check if the pipeline has drained and signal drain done.
     *
     * This method checks if a drain has been requested and if the CPU
     * has drained successfully (i.e., there are no instructions in
     * the pipeline). If the CPU has drained, it deschedules the tick
     * event and signals the drain manager.
     *
     * @return False if a drain hasn't been requested or the CPU
     * hasn't drained, true otherwise.
     */
    bool tryDrain();

    /**
     * Perform sanity checks after a drain.
     *
     * This method is called from drain() when it has determined that
     * the CPU is fully drained when gem5 is compiled with the NDEBUG
     * macro undefined. The intention of this method is to do more
     * extensive tests than the isDrained() method to weed out any
     * draining bugs.
     */
    void drainSanityCheck() const;

    /** Check if a system is in a drained state. */
    bool isCpuDrained() const;

  public:
    /** Constructs a CPU with the given parameters. */
    CPU(const BaseRxuO3CPUParams &params);

    ProbePointArg<PacketPtr> *ppInstAccessComplete;
    ProbePointArg<std::pair<DynInstPtr, PacketPtr> > *ppDataAccessComplete;

    /** Register probe points. */
    void regProbePoints() override;

    // asn: Address Space Number, asid(riscv): Address Space Identifier
    void
    demapPage(Addr vaddr, uint64_t asn)
    {
        mmu->demapPage(vaddr, asn);
    }

    /** Ticks CPU, calling tick() on each stage, and checking the overall
     *  activity to see if the CPU should deschedule itself.
     */
    void tick();

    /** Initialize the CPU */
    void init() override;

    void startup() override;

    /** Returns the Number of Active Threads in the CPU */
    int
    numActiveThreads()
    {
        return activeThreads.size();
    }

    /** Add Thread to Active Threads List */
    void activateThread(ThreadID tid);

    /** Remove Thread from Active Threads List */
    void deactivateThread(ThreadID tid);

    /** Setup CPU to insert a thread's context */
    void insertThread(ThreadID tid);

    /** Remove all of a thread's context from CPU */
    void removeThread(ThreadID tid);

    /** Count the Total Instructions Committed in the CPU. */
    Counter totalInsts() const override;

    /** Count the Total Ops (including micro ops) committed in the CPU. */
    Counter totalOps() const override;

    /** Add Thread to Active Threads List. */
    void activateContext(ThreadID tid) override;

    /** Remove Thread from Active Threads List */
    void suspendContext(ThreadID tid) override;

    /** Remove Thread from Active Threads List &&
     *  Remove Thread Context from CPU.
     */
    void haltContext(ThreadID tid) override;

    /** Update The Order In Which We Process Threads. */
    void updateThreadPriority();

    /** Is the CPU draining? */
    bool isDraining() const { return drainState() == DrainState::Draining; }

    void serializeThread(CheckpointOut &cp, ThreadID tid) const override;
    void unserializeThread(CheckpointIn &cp, ThreadID tid) override;

    /** Insert tid to the list of threads trying to exit */
    void addThreadToExitingList(ThreadID tid);

    /** Is the thread trying to exit? */
    bool isThreadExiting(ThreadID tid) const;

    /**
     *  If a thread is trying to exit and its corresponding trap event
     *  has been completed, schedule an event to terminate the thread.
     */
    void scheduleThreadExitEvent(ThreadID tid);

    /** Terminate all threads that are ready to exit */
    void exitThreads();

  public:
    /** Starts draining the CPU's pipeline of all instructions in
     * order to stop all memory accesses. */
    DrainState drain() override;

    /** Resumes execution after a drain. */
    void drainResume() override;

    /**
     * Commit has reached a safe point to drain a thread.
     *
     * Commit calls this method to inform the pipeline that it has
     * reached a point where it is not executed microcode and is about
     * to squash uncommitted instructions to fully drain the pipeline.
     */
    void commitDrained(ThreadID tid);

    /** Switches out this CPU. */
    void switchOut() override;

    /** Takes over from another CPU. */
    void takeOverFrom(BaseCPU *oldCPU) override;

    void verifyMemoryMode() const override;

    /** Get the current instruction sequence number, and increment it. */
    InstSeqNum getAndIncrementInstSeq(bool incrementVector, int incrementNum) {
        if (incrementVector) globalSeqNum += incrementNum;
        else globalSeqNum++;

        return globalSeqNum;
    }

    /** Traps to handle given fault. */
    void trap(const Fault &fault, ThreadID tid, const StaticInstPtr &inst);

    /** Returns the Fault for any valid interrupt. */
    Fault getInterrupts();

    /** Processes any an interrupt fault. */
    void processInterrupts(const Fault &interrupt);

    /** Halts the CPU. */
    void halt() { panic("Halt not implemented!\n"); }

    /** Register accessors.  Index refers to the physical register index. */

    /** Reads a miscellaneous register. */
    RegVal readMiscRegNoEffect(int misc_reg, ThreadID tid) const;

    /** Reads a misc. register, including any side effects the read
     * might have as defined by the architecture.
     */
    RegVal readMiscReg(int misc_reg, ThreadID tid);

    /** Sets a miscellaneous register. */
    void setMiscRegNoEffect(int misc_reg, RegVal val, ThreadID tid);

    /** Sets a misc. register, including any side effects the write
     * might have as defined by the architecture.
     */
    void setMiscReg(int misc_reg, RegVal val, ThreadID tid);

    __uint128_t getVectorReg(PhysRegIdPtr phys_reg);
    RegVal getReg(PhysRegIdPtr phys_reg, ThreadID tid);
    void getReg(PhysRegIdPtr phys_reg, void *val, ThreadID tid);
    void *getWritableReg(PhysRegIdPtr phys_reg, ThreadID tid);

    void setReg(PhysRegIdPtr phys_reg, RegVal val, ThreadID tid);
    void setReg(PhysRegIdPtr phys_reg, const void *val, ThreadID tid);

    /** Architectural register accessors.  Looks up in the commit
     * rename table to obtain the true physical index of the
     * architected register first, then accesses that physical
     * register.
     */

    RegVal getArchReg(const RegId &reg, ThreadID tid);
    void getArchReg(const RegId &reg, void *val, ThreadID tid);
    void *getWritableArchReg(const RegId &reg, ThreadID tid);

    void setArchReg(const RegId &reg, RegVal val, ThreadID tid);
    void setArchReg(const RegId &reg, const void *val, ThreadID tid);

    /** Sets the commit PC state of a specific thread. */
    void pcState(const PCStateBase &new_pc_state, ThreadID tid);

    /** Reads the commit PC state of a specific thread. */
    const PCStateBase &pcState(ThreadID tid);

    /** Initiates a squash of all in-flight instructions for a given
     * thread.  The source of the squash is an external update of
     * state through the TC.
     */
    void squashFromTC(ThreadID tid);

    /** Function to add instruction onto the head of the list of the
     *  instructions.  Used when new instructions are fetched.
     */
    ListIt addInst(const DynInstPtr &inst);

    ListIt addMicroInst(const DynInstPtr &inst);

    /** Function to tell the CPU that an instruction has completed. */
    void instDone(ThreadID tid, const DynInstPtr &inst);

    /** Remove an instruction from the front end of the list.  There's
     *  no restriction on location of the instruction.
     */
    void removeFrontInst(const DynInstPtr &inst);

    /** Remove all instructions that are not currently in the ROB.
     *  There's also an option to not squash delay slot instructions.*/
    void removeInstsNotInROB(ThreadID tid);

    /** Remove all instructions younger than the given sequence number. */
    void removeInstsUntil(const InstSeqNum &seq_num, ThreadID tid);

    void removeInst(const InstSeqNum &seq_num, ThreadID tid);

    void removeInstsSquash(ThreadID tid);

    /** Removes the instruction pointed to by the iterator. */
    void squashInstIt(const ListIt &instIt, ThreadID tid);

    /** Cleans up all instructions on the remove list. */
    void cleanUpRemovedInsts();

    /** Debug function to print all instructions on the list. */
    void dumpInsts();

  public:
#ifndef NDEBUG
    /** Count of total number of dynamic instructions in flight. */
    int instcount;
#endif

    /** List of all the instructions in flight. */
    std::list<DynInstPtr> instList;

    /** List of all the instructions that will be removed at the end of this
     *  cycle.
     */
    std::queue<ListIt> removeList;

#ifdef GEM5_DEBUG
    /** Debug structure to keep track of the sequence numbers still in
     * flight.
     */
    std::set<InstSeqNum> snList;
#endif

    /** Records if instructions need to be removed this cycle due to
     *  being retired or squashed.
     */
    bool removeInstsThisCycle;

  public:
    /** merge buffer for vector stride/index instruction. */
    class MergeBuffer
    {
    private:
        int maxEntryNum;
        std::map<InstSeqNum, mergeBufferEntry> Buffer;

    public:
        MergeBuffer(int maxEntryNum);

        void addEntry(InstSeqNum sn, DynInstPtr inst, int eop_num);

        int getSize();

        /** For vd writeback. */
        mergeBufferEntry checkAndFetchEntry();

        bool entryEnough();

        /** For instruction execute function. */
        RiscvISA::VecRegContainer *fetchEntry(InstSeqNum sn);

        /** Indicate uop's vd if set old vd. */
        bool alreadySetOldVd(InstSeqNum sn);

        void decreaseEntryEopNum(InstSeqNum sn);

        void squash(InstSeqNum squashed_sn);

        std::string name();

        std::string reverseByteOrder(const std::string& input);
    };

    void *getMergeBufferEntry(InstSeqNum sn) {
        return mergeBuffer.fetchEntry(sn);
    };

    bool MBEntryAlreadySetOldVd(InstSeqNum sn) {
        return mergeBuffer.alreadySetOldVd(sn);
    };
    void decreaseMBEntryEopNum(InstSeqNum sn) {
        mergeBuffer.decreaseEntryEopNum(sn);
    }

  public:

    MergeBuffer mergeBuffer;

    /** ipretech ptr */
    IPrefetch *iprefetch;

    /** The fetch stage. */
    Fetch fetch;

    /** The bpu0 stage. */
    Bpu0 bpu0;

    /** The bpu1 stage. */
    Bpu1 bpu1;

    /** The IBandLB stage. */
    IBandLB iBandLB;

    /** The UopCache stage. */
    UopCache uc;

    /** The decode stage. */
    Decode decode;

    // -- add by hongfei.liu-------------
    /** The predisq stage. */
    Predisq predisq;

    /** The rename module. */
    RenameUnit rename;

    Rename o3rename;

    /** The rmu module. */
    RMU rmu;

   // LSQ lsq;

    /** The dispipe0 stage. */
    Dispipe0 dispipe0;

    /** The dispipe1 stage. */
    Dispipe1 dispipe1;

    /** The dispipe2 stage. */
    Dispipe2 dispipe2;

    /** The dispipe3 stage. */
    Dispipe3 dispipe3;
    // ----------------------------------

    /** The execute/writeback stages. */
    EW ew;

    /** The commit stage. */
    Commit commit;

    /** The register file. */
    PhysRegFile regFile;

    /** The free list. */
    RxuUnifiedFreeList freeList;

    UnifiedFreeList o3_freeList;

    RxuFRMRename frm;

    /** vector csr rename. */
    RxuUnifiedVecRename vecCsr;

    /** The rename map. */
    UnifiedRenameMap renameMap[MaxThreads];

    /** The commit rename map. */
    UnifiedRenameMap commitRenameMap[MaxThreads];

    /** The re-order buffer. */
    ROB rob;

    /** Active Threads List */
    std::list<ThreadID> activeThreads;

    /**
     *  This is a list of threads that are trying to exit. Each thread id
     *  is mapped to a boolean value denoting whether the thread is ready
     *  to exit.
     */
    std::unordered_map<ThreadID, bool> exitingThreads;

    /** Integer Register Scoreboard */
    Scoreboard scoreboard;

    std::vector<BaseISA *> isa;

    bool rxu_rename;

  public:
    /** Enum to give each stage a specific index, so when calling
     *  activateStage() or deactivateStage(), they can specify which stage
     *  is being activated/deactivated.
     */
    enum StageIdx
    {
        FetchIdx,
        Bpu0Idx,
        Bpu1Idx,
        IBandLBIdx,
        DecodeIdx,
        // -- add by hongfei.liu----------------
        PredisqIdx,
        Dispipe0Idx,
        Dispipe1Idx,
        Dispipe2Idx,
        Dispipe3Idx,
        // -------------------------------------
        // RenameIdx,
        EWIdx,
        CommitIdx,
        NumStages
    };

    /** The main time buffer to do backwards communication. */
    TimeBuffer<TimeStruct> timeBuffer;

    /** The fetch stage's instruction queue. */
    TimeBuffer<FetchStruct> fetchQueue;

    /** The bpu0 stage's instruction queue. */
    TimeBuffer<Bpu0Struct> bpu0Queue;

    /** The bpu1 stage's instruction queue. */
    TimeBuffer<Bpu1Struct> bpu1Queue;

    /** The IBandLB stage's instruction queue. */
    TimeBuffer<IBandLBStruct> IBandLBQueue;

    /** The decode stage's instruction queue. */
    TimeBuffer<DecodeStruct> decodeQueue;

    // -- add by hongfei.liu-----------------------
    /** The predisq stage's instruction queue. */
    TimeBuffer<PredisqStruct> predisqQueue;

    /** The dispipe0 stage's instruction queue. */
    TimeBuffer<Dispipe0Struct> dispipe0Queue;

    /** The dispipe0 to rob instruction queue. */
    TimeBuffer<Dispipe0ToRobStruct> dispipe0ToRobQueue;

    /** The dispipe1 stage's instruction queue. */
    TimeBuffer<Dispipe1Struct> dispipe1Queue;

    /** The dispipe2 stage's instruction queue. */
    TimeBuffer<Dispipe2Struct> dispipe2Queue;

    /** The dispipe3 stage's instruction queue. */
    TimeBuffer<WakeStruct> wakeQueue;
    // --------------------------------------------

    // /** The rename stage's instruction queue. */
    // TimeBuffer<RenameStruct> renameQueue;

    /** The IEW stage's instruction queue. */
    TimeBuffer<EWStruct> ewQueue;

  private:
    /** The activity recorder; used to tell if the CPU has any
     * activity remaining or if it can go to idle and deschedule
     * itself.
     */
    ActivityRecorder activityRec;

  public:
    /** Records that there was time buffer activity this cycle. */
    void activityThisCycle() { activityRec.activity(); }

    /** Changes a stage's status to active within the activity recorder. */
    void
    activateStage(const StageIdx idx)
    {
        activityRec.activateStage(idx);
    }

    /** Changes a stage's status to inactive within the activity recorder. */
    void
    deactivateStage(const StageIdx idx)
    {
        activityRec.deactivateStage(idx);
    }

    /** Wakes the CPU, rescheduling the CPU if it's not already active. */
    void wakeCPU();

    virtual void wakeup(ThreadID tid) override;

    /** Gets a free thread id. Use if thread ids change across system. */
    ThreadID getFreeTid();

    bool isOldestInstInPipe(const DynInstPtr &inst);

    uint64_t extractBits(uint64_t value, int start, int numBits);

  public:
    /** Returns a pointer to a thread context. */
    gem5::ThreadContext *
    tcBase(ThreadID tid)
    {
        return thread[tid]->getTC();
    }

    /** The global sequence number counter. */
    InstSeqNum globalSeqNum;//[MaxThreads];

    /** Pointer to the checker, which can dynamically verify
     * instruction results at run time.  This can be set to NULL if it
     * is not being used.
     */
    gem5::Checker<DynInstPtr> *checker;

    /** Pointer to the system. */
    System *system;

    /** Pointers to all of the threads in the CPU. */
    std::vector<ThreadState *> thread;

    /** Threads Scheduled to Enter CPU */
    std::list<int> cpuWaitList;

    /** The cycle that the CPU was last running, used for statistics. */
    Cycles lastRunningCycle;

    /** The cycle that the CPU was last activated by a new thread*/
    Tick lastActivatedCycle;

    /** Mapping for system thread id to cpu id */
    std::map<ThreadID, unsigned> threadMap;

    /** Available thread ids in the cpu*/
    std::vector<ThreadID> tids;

    /** Current loop index. */
    unsigned loopIndex;

    unsigned loopxhIndex = 0;

    unsigned loopxhIndex_m = 0;

    /** Loop PC. */
    Addr loopPC;

    /** Loopstart PC. */
    Addr loopstartPC;
    /** Loopend PC. */
    Addr loopendPC;

    bool vsetBranch;

    /** Current loop index. */
    unsigned loopendIndex;

    /** Current loop index. */
    unsigned loopstartIndex;

    // curMacroCommitInsts
    uint64_t macroNumber;

   // curMicroCommitInsts
    uint64_t microNumber;

    uint64_t curMacroCommitInsts = 0;

    uint64_t lastMacroCommitInsts = 0;

    uint64_t curMicroCommitInsts = 0;

    uint64_t lastMicroCommitInsts = 0;    

    uint64_t lowIPCCount = 0;

    /** CPU pushRequest function, forwards request to LSQ. */
    Fault
    pushRequest(const DynInstPtr& inst, bool isLoad, uint8_t *data,
                unsigned int size, Addr addr, Request::Flags flags,
                uint64_t *res, AtomicOpFunctorPtr amo_op = nullptr,
                const std::vector<bool>& byte_enable=std::vector<bool>())

    {
        return dispipe3.ldstQueue.pushRequest(inst, isLoad, data, size, addr,
                flags, res, std::move(amo_op), byte_enable);
    }

    /** Used by the fetch unit to get a hold of the instruction port. */
    Port &
    getInstPort() override
    {
        return fetch.getInstPort();
    }

    /** Get the dcache port (used to find block size for translations). */
    Port &
    getDataPort() override
    {
        return dispipe3.ldstQueue.getDataPort();
    }

    // struct frm_Ready 
    // {
    //   InstSeqNum NewSeqNum;
    //   bool ready;
    //   std::deque<DynInstPtr> FloatInsts;
    // };

    // std::deque<frm_Ready> frm_Insts;

    struct CPUStats : public statistics::Group
    {
        CPUStats(CPU *cpu);

        /** Stat for total number of times the CPU is descheduled. */
        statistics::Scalar timesIdled;
        /** Stat for total number of cycles the CPU spends descheduled. */
        statistics::Scalar idleCycles;
        /** Stat for total number of cycles the CPU spends descheduled due to a
         * quiesce operation or waiting for an interrupt. */
        statistics::Scalar quiesceCycles;
        // -- add by hongfei.liu --------------------
        statistics::Vector committedInsts;
        /** Stat for total number of committed macro vector insts. */
        statistics::Scalar committedMacroVectorInsts;
        /** Stat for the number of committed ops (including micro ops) per
         *  thread. */
        statistics::Vector committedOps;
        /** Stat for the CPI per thread. */
        statistics::Formula cpi;
        /** Stat for the total CPI. */
        statistics::Formula totalCpi;
        /** Stat for the IPC per thread. */
        statistics::Formula ipc;
        /** Stat for the Macro vector IPC per thread. */
        statistics::Formula macroVectorIpc;
        /** Stat for the total IPC. */
        statistics::Formula totalIpc;

        //number of integer register file accesses
        statistics::Scalar intRegfileReads;
        statistics::Scalar intRegfileWrites;
        //number of float register file accesses
        statistics::Scalar fpRegfileReads;
        statistics::Scalar fpRegfileWrites;
        //number of vector register file accesses
        mutable statistics::Scalar vecRegfileReads;
        statistics::Scalar vecRegfileWrites;
        //number of predicate register file accesses
        mutable statistics::Scalar vecPredRegfileReads;
        statistics::Scalar vecPredRegfileWrites;
        //number of CC register file accesses
        statistics::Scalar ccRegfileReads;
        statistics::Scalar ccRegfileWrites;
        //number of misc
        statistics::Scalar miscRegfileReads;
        statistics::Scalar miscRegfileWrites;
        // ------------------------------------------
    } cpuStats;

    struct LoopCPUStats: public statistics::Group
    {
        LoopCPUStats(statistics::Group *parent, int loop_id);

        /* Number of squashed insts skipped by commit per loop. */
        statistics::Scalar commitSquashedInsts;
        /* Number of retired insts processed by commit per loop. */
        statistics::Scalar commitRetiredInsts;
        /* RecoverRate: SquashedInsts / (SquashedInsts + RetiredInsts) in ROB per loop. */
        statistics::Formula recoverRate;

        /* Number of cpu cycles simulated per loop. */
        statistics::Scalar numCycles;
        /* CPI/IPC per loop. */
        statistics::Formula cpi;
        statistics::Formula ipc;

        /* Number of squash times due to BPU0_miss in commit per loop. */
        statistics::Scalar bpu0MissCommitCount;
        /* Number of squash times due to BPU0_miss in decode per loop. */
        statistics::Scalar bpu0MissDecodeCount;
        /* Number of squash times due to BPU1_miss in commit per loop. */
        statistics::Scalar bpu1MissCommitCount;
        /* Number of squash times due to BPU1_miss in decode per loop. */
        statistics::Scalar bpu1MissDecodeCount;
        /* Number of squash times due to rollback per loop. */
        statistics::Scalar rbkCount;

        /* Number of retired branch insts processed by commit per loop. */
        statistics::Scalar retiredBranchInsts;

        /* BpuMissRate: bpu0MissCommitCount / retiredBranchInsts in ROB per loop. */
        statistics::Formula bpu0MissRate;
        /* BpuMissRate: bpu1MissCommitCount / retiredBranchInsts in ROB per loop. */
        statistics::Formula bpu1MissRate;

        /* Number of squashed insts skipped by commit per loop. */
        statistics::Scalar xhcommitSquashedInsts;
        /* Number of retired insts processed by commit per loop. */
        statistics::Scalar xhcommitRetiredInsts;
        /* RecoverRate: SquashedInsts / (SquashedInsts + RetiredInsts) in ROB per loop. */
        statistics::Formula xhrecoverRate;

        /* Number of cpu cycles simulated per loop. */
        statistics::Scalar xhnumCycles;
        /* CPI/IPC per loop. */
        statistics::Formula xhcpi;
        statistics::Formula xhipc;

        /* Number of squash times due to BPU_miss in commit per loop. */
        statistics::Scalar xhbpuMissCommitCount;
        /* Number of squash times due to BPU_miss in decode per loop. */
        statistics::Scalar xhbpuMissDecodeCount;
        /* Number of squash times due to rollback per loop. */
        statistics::Scalar xhrbkCount;

        /* Number of retired branch insts processed by commit per loop. */
        statistics::Scalar xhretiredBranchInsts;

        /* BpuMissRate: bpuMissCommitCount / retiredBranchInsts in ROB per loop. */
        statistics::Formula xhbpuMissRate;
    };

    std::vector<std::unique_ptr<LoopCPUStats>> loopStats;

  public:
    // hardware transactional memory
    void htmSendAbortSignal(ThreadID tid, uint64_t htm_uid,
                            HtmFailureFaultCause cause) override;
};

} // namespace rxuo3
} // namespace gem5

#endif // __CPU_RxuO3_CPU_HH__
