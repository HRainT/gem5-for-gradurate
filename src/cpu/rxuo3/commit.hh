#ifndef __CPU_RxuO3_COMMIT_HH__
#define __CPU_RxuO3_COMMIT_HH__

#include <queue>

#include "base/statistics.hh"
#include "cpu/exetrace.hh"
#include "cpu/inst_seq.hh"
#include "cpu/rxuo3/comm.hh"
#include "cpu/rxuo3/dyn_inst_ptr.hh"
#include "cpu/rxuo3/ew.hh"
#include "cpu/rxuo3/limits.hh"
#include "cpu/rxuo3/rename_map.hh"
#include "cpu/rxuo3/rob.hh"
#include "cpu/timebuf.hh"
#include "enums/RxuCommitPolicy.hh"
#include "sim/probe/probe.hh"

namespace gem5
{

struct BaseRxuO3CPUParams;

namespace rxuo3
{

class ThreadState;
class Dispipe3;

/**
 * Commit handles single threaded and SMT commit. Its width is
 * specified by the parameters; each cycle it tries to commit that
 * many instructions. The SMT policy decides which thread it tries to
 * commit instructions from. Non- speculative instructions must reach
 * the head of the ROB before they are ready to execute; once they
 * reach the head, commit will broadcast the instruction's sequence
 * number to the previous stages so that they can issue/ execute the
 * instruction. Only one non-speculative instruction is handled per
 * cycle. Commit is responsible for handling all back-end initiated
 * redirects.  It receives the redirect, and then broadcasts it to all
 * stages, indicating the sequence number they should squash until,
 * and any necessary branch misprediction information as well. It
 * priortizes redirects by instruction's age, only broadcasting a
 * redirect if it corresponds to an instruction that should currently
 * be in the ROB. This is done by tracking the sequence number of the
 * youngest instruction in the ROB, which gets updated to any
 * squashing instruction's sequence number, and only broadcasting a
 * redirect if it corresponds to an older instruction. Commit also
 * supports multiple cycle squashing, to model a ROB that can only
 * remove a certain number of instructions per cycle.
 */
class Commit
{
  public:
    /** Overall commit status. Used to determine if the CPU can deschedule
     * itself due to a lack of activity.
     */
    enum CommitStatus
    {
        Active,
        Inactive
    };

    /** Individual thread status. */
    enum ThreadStatus
    {
        Running,
        Idle,
        ROBSquashing,
        TrapPending,
        FetchTrapPending,
        SquashAfterPending, //< Committing instructions before a squash.
    };

  private:
    /** Overall commit status. */
    CommitStatus _status;
    /** Next commit status, to be set at the end of the cycle. */
    CommitStatus _nextStatus;
    /** Per-thread status. */
    ThreadStatus commitStatus[MaxThreads];
    /** Commit policy used in SMT mode. */
    RxuCommitPolicy commitPolicy;

    /** Probe Points. */
    ProbePointArg<DynInstPtr> *ppCommit;
    ProbePointArg<DynInstPtr> *ppCommitStall;
    /** To probe when an instruction is squashed */
    ProbePointArg<DynInstPtr> *ppSquash;

    /** Mark the thread as processing a trap. */
    void processTrapEvent(ThreadID tid);

  public:
    /** Construct a Commit with the given parameters. */
    Commit(CPU *_cpu, const BaseRxuO3CPUParams &params);

    /** Returns the name of the Commit. */
    std::string name() const;

    /** Registers probes. */
    void regProbePoints();

    bool ifTsEmpty(ThreadID tid);

    void insert_whensq();

    void addtoROB(ThreadID tid);

    void insertInstFromTS(const DynInstPtr &inst,ThreadID tid);

    //TS(temporary storage) inst when ROB squashing
    std::list<DynInstPtr> TS_instList[MaxThreads];



    /** Sets the list of threads. */
    void setThreads(std::vector<ThreadState *> &threads);

    /** Sets the main time buffer pointer, used for backwards communication. */
    void setTimeBuffer(TimeBuffer<TimeStruct> *tb_ptr);

    void setFetchQueue(TimeBuffer<FetchStruct> *fq_ptr);

    /** Sets the pointer to the queue coming from dispipe0. */
    void setDispipe0ToRobQueue(TimeBuffer<Dispipe0ToRobStruct> *p0q_ptr);

    /** Sets the pointer to the queue coming from EW. */
    void setEWQueue(TimeBuffer<EWStruct> *eq_ptr);

    /** Sets the pointer to the EW stage. */
    void setEWStage(EW *ew_stage);

    /** Sets the pointer to the EW stage. */
    void setDispipe3Stage(Dispipe3 *p3_ptr);

    /** The pointer to the EW stage. Used solely to ensure that
     * various events (traps, interrupts, syscalls) do not occur until
     * all stores have written back.
     */
    EW *ewStage;

    Dispipe3 *dispipe3Stage;

    /** Sets pointer to list of active threads. */
    void setActiveThreads(std::list<ThreadID> *at_ptr);

    /** Sets pointer to the commited state rename map. */
    void setRenameMap(UnifiedRenameMap rm_ptr[MaxThreads]);

    /** Sets pointer to the ROB. */
    void setROB(ROB *rob_ptr);

    /** Initializes stage by sending back the number of free entries. */
    void startupStage();

    /** Clear all thread-specific states */
    void clearStates(ThreadID tid);

    /** Initializes the draining of commit. */
    void drain();

    /** Resumes execution after draining. */
    void drainResume();

    /** Perform sanity checks after a drain. */
    void drainSanityCheck() const;

    /** Has the stage drained? */
    bool isDrained() const;

    /** Takes over from another CPU's thread. */
    void takeOverFrom();

    /** Deschedules a thread from scheduling */
    void deactivateThread(ThreadID tid);

    /** Is the CPU currently processing a HTM transaction? */
    bool executingHtmTransaction(ThreadID) const;

    /* Reset HTM tracking, e.g. after an abort */
    void resetHtmStartsStops(ThreadID);

    /** Ticks the commit stage, which tries to commit instructions. */
    void tick();

    /** Handles any squashes that are sent from EW, and adds instructions
     * to the ROB and tries to commit instructions.
     */
    void commit();

    /** Returns the number of free ROB entries for a specific thread. */
    size_t numROBFreeEntries(ThreadID tid);

    /** Returns the number of free vector ROB entries for a specific thread. */
    size_t numROBFreeVecEntries(ThreadID tid);

    /** Generates an event to schedule a squash due to a trap. */
    void generateTrapEvent(ThreadID tid, Fault inst_fault);

    /** Records that commit needs to initiate a squash due to an
     * external state update through the TC.
     */
    void generateTCEvent(ThreadID tid);
    
    /* Uop Index/Stride ST tobe Commit */
    std::vector<DynInstPtr> UopList;

  private:
    /** Updates the overall status of commit with the nextStatus, and
     * tell the CPU if commit is active/inactive.
     */
    void updateStatus();

    /** Returns if any of the threads have the number of ROB entries changed
     * on this cycle. Used to determine if the number of free ROB entries needs
     * to be sent back to previous stages.
     */
    bool changedROBEntries();

    /** Squashes all in flight instructions. */
    void squashAll(ThreadID tid);

    /** Handles squashing due to a trap. */
    void squashFromTrap(ThreadID tid);

    /** Handles squashing due to an TC write. */
    void squashFromTC(ThreadID tid);

    /** Handles a squash from a squashAfter() request. */
    void squashFromSquashAfter(ThreadID tid);

    /**
     * Handle squashing from instruction with SquashAfter set.
     *
     * This differs from the other squashes as it squashes following
     * instructions instead of the current instruction and doesn't
     * clean up various status bits about traps/tc writes
     * pending. Since there might have been instructions committed by
     * the commit stage before the squashing instruction was reached
     * and we can't commit and squash in the same cycle, we have to
     * squash in two steps:
     *
     * <ol>
     *   <li>Immediately set the commit status of the thread of
     *       SquashAfterPending. This forces the thread to stop
     *       committing instructions in this cycle. The last
     *       instruction to be committed in this cycle will be the
     *       SquashAfter instruction.
     *   <li>In the next cycle, commit() checks for the
     *       SquashAfterPending state and squashes <i>all</i>
     *       in-flight instructions. Since the SquashAfter instruction
     *       was the last instruction to be committed in the previous
     *       cycle, this causes all subsequent instructions to be
     *       squashed.
     * </ol>
     *
     * @param tid ID of the thread to squash.
     * @param head_inst Instruction that requested the squash.
     */
    void squashAfter(ThreadID tid, const DynInstPtr &head_inst);

    /** Handles processing an interrupt. */
    void handleInterrupt();

    /** Get fetch redirecting so we can handle an interrupt */
    void propagateInterrupt();

    /** Commits as many instructions as possible. */
    void commitInsts();

    /** Tries to commit the head ROB instruction passed in.
     * @param head_inst The instruction to be committed.
     */
    bool commitHead(const DynInstPtr &head_inst, unsigned inst_num);

    /** Gets instructions from dispipe0 and inserts them into the ROB. */
    void getInsts();

    /** Marks completed instructions using information sent from EW. */
    void markCompletedInsts();

    /** Gets the thread to commit, based on the SMT policy. */
    ThreadID getCommittingThread();

    /** Returns the thread ID to use based on a round robin policy. */
    ThreadID roundRobin();

    /** Returns the thread ID to use based on an oldest instruction policy. */
    ThreadID oldestReady();

  public:
    /** Reads the PC of a specific thread. */
    const PCStateBase &pcState(ThreadID tid) { return *pc[tid]; }

    /** Sets the PC of a specific thread. */
    void pcState(const PCStateBase &val, ThreadID tid) { set(pc[tid], val); }

  private:
    /** Time buffer interface. */
    TimeBuffer<TimeStruct> *timeBuffer;

    /** Wire to write information heading to previous stages. */
    TimeBuffer<TimeStruct>::wire toEW;

    /** Wire to read information from EW (for ROB). */
    TimeBuffer<TimeStruct>::wire robInfoFromEW;

    TimeBuffer<FetchStruct> *fetchQueue;

    TimeBuffer<FetchStruct>::wire fromFetch;

    /** EW instruction queue interface. */
    TimeBuffer<EWStruct> *ewQueue;

    /** Wire to read information from EW queue. */
    TimeBuffer<EWStruct>::wire fromEW;

    /** Dispipe0 instruction queue interface, for ROB. */
    TimeBuffer<Dispipe0ToRobStruct> *dispipe0Queue;

    /** Wire to read information from dispipe0 queue. */
    TimeBuffer<Dispipe0ToRobStruct>::wire fromDispipe0;

  public:
    /** ROB interface. */
    ROB *rob;

    /** Records if the number of ROB entries has changed this cycle. If it has,
     * then the number of free entries must be re-broadcast.
     */
    bool changedROBNumEntries[MaxThreads];

  private:
    /** Pointer to RxuO3CPU. */
    CPU *cpu;

    /** Vector of all of the threads. */
    std::vector<ThreadState *> thread;

    /** Records that commit has written to the time buffer this cycle. Used for
     * the CPU to determine if it can deschedule itself if there is no activity.
     */
    bool wroteToTimeBuffer;

    /** Records if a thread has to squash this cycle due to a trap. */
    bool trapSquash[MaxThreads];

    /** Records if a thread has to squash this cycle due to an XC write. */
    bool tcSquash[MaxThreads];

    /**
     * Instruction passed to squashAfter().
     *
     * The squash after implementation needs to buffer the instruction
     * that caused a squash since this needs to be passed to the fetch
     * stage once squashing starts.
     */
    DynInstPtr squashAfterInst[MaxThreads];

    /** Priority List used for Commit Policy */
    std::list<ThreadID> priority_list;

    /** EW to Commit delay. */
    const Cycles ewToCommitDelay;

    /** Commit to EW delay. */
    const Cycles commitToEWDelay;

    /** Dispipe0 to ROB delay. */
    const Cycles dispipe0ToROBDelay;

    const Cycles fetchToCommitDelay;

    /** Dispipe0 width, in instructions.  Used so ROB knows how many
     *  instructions to get from the dispipe0 instruction queue.
     */
    const unsigned dispipe0Width;

    /** Commit width, in instructions. */
    unsigned commitWidth;

    unsigned commitPtr;

    /** Number of Active Threads */
    const ThreadID numThreads;

    /** Is a drain pending? Commit is looking for an instruction boundary while
     * there are no pending interrupts
     */
    bool drainPending;

    /** Is a drain imminent? Commit has found an instruction boundary while no
     * interrupts were present or in flight.  This was the last architecturally
     * committed instruction.  Interrupts disabled and pipeline flushed.
     * Waiting for structures to finish draining.
     */
    bool drainImminent;

    /** The latency to handle a trap.  Used when scheduling trap
     * squash event.
     */
    const Cycles trapLatency;

    /** The interrupt fault. */
    Fault interrupt;

    /** The commit PC state of each thread.  Refers to the instruction that
     * is currently being processed/committed.
     */
    std::unique_ptr<PCStateBase> pc[MaxThreads];

    /** The sequence number of the youngest valid instruction in the ROB. */
    InstSeqNum youngestSeqNum[MaxThreads];

    /** The sequence number of the last commited instruction. */
    InstSeqNum lastCommitedSeqNum[MaxThreads];

    /** Records if there is a trap currently in flight. */
    bool trapInFlight[MaxThreads];

    /** Records if there were any stores committed this cycle. */
    bool committedStores[MaxThreads];

    /** Records if commit should check if the ROB is truly empty (see
        commit_impl.hh). */
    bool checkEmptyROB[MaxThreads];

    /** Pointer to the list of active threads. */
    std::list<ThreadID> *activeThreads;

    /** Rename map interface. */
    UnifiedRenameMap *renameMap[MaxThreads];

    /** True if last committed microop can be followed by an interrupt */
    bool canHandleInterrupts;

    /** Have we had an interrupt pending and then seen it de-asserted because
        of a masking change? In this case the variable is set and the next time
        interrupts are enabled and pending the pipeline will squash to avoid
        a possible livelock senario.  */
    bool avoidQuiesceLiveLock;

    /** Updates commit stats based on this instruction. */
    void updateComInstStats(const DynInstPtr &inst);

    // HTM
    int htmStarts[MaxThreads];
    int htmStops[MaxThreads];

    InstSeqNum cannotCommitInst;

    int cannotCommitCount;

    int commitBranchIndex = 0;

  public:
    struct CommitStats : public statistics::Group
    {
        CommitStats(CPU *cpu, Commit *commit);
        /** Stat for the total number of squashed instructions discarded by
         * commit.
         */
        statistics::Scalar commitSquashedInsts;
        /** Stat for the total number of times commit has had to stall due
         * to a non-speculative instruction reaching the head of the ROB.
         */
        statistics::Scalar commitNonSpecStalls;
        /** Stat for the total number of branch mispredicts that caused a
         * squash.
         */
        statistics::Scalar branchMispredicts;

        /** Stat for the total number of branch mispredicts that caused a
         * squash when pred is weak.
         */
        statistics::Scalar branchMispredicts_predWeak;

        /** Distribution of the number of committed instructions each cycle. */
        statistics::Distribution numCommittedDist;

        /** Stat for the total number of committed atomics. */
        statistics::Vector amos;
        /** Total number of committed memory barriers. */
        statistics::Vector membars;
        /** */
        statistics::Vector scs;
        /** */
        statistics::Vector lrs;
        /** Total number of function calls */
        statistics::Vector functionCalls;
        /** Committed instructions by instruction type (OpClass) */
        statistics::Vector2d committedInstType;

        /** Number of cycles where the commit bandwidth limit is reached. */
        statistics::Scalar commitEligibleSamples;
        statistics::Scalar commitRetiredInsts;
        statistics::Formula totalRecoverRate;

        /* Number of squash times due to rollback. */
        statistics::Scalar rbkCount;
        /* Number of retired branch insts processed by commit. */
        statistics::Scalar retiredBranchInsts;
        // /* BpuMissRate: bpuMissCommitCount / retiredBranchInsts in ROB. */
        // statistics::Formula totalBpuMissRate;

        statistics::Distribution cannotCommit;

        statistics::Distribution bpuCtr;

        statistics::Distribution bpuCtr_true;

        statistics::Distribution bpuCtr_false;

        statistics::Distribution bubblesInstscommit;

        statistics::Scalar retiredBubble;
    } stats;
};

} // namespace rxuo3
} // namespace gem5

#endif // __CPU_RxuO3_COMMIT_HH__
