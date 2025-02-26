#ifndef __CPU_RxuO3_RENAME_HH__
#define __CPU_RxuO3_RENAME_HH__

#include <list>
#include <utility>

#include "base/statistics.hh"
#include "cpu/rxuo3/comm.hh"
#include "cpu/rxuo3/commit.hh"
#include "cpu/rxuo3/dyn_inst_ptr.hh"
#include "cpu/rxuo3/free_list.hh"
#include "cpu/rxuo3/frm.hh"
// #include "cpu/rxuo3/ew.hh"
#include "cpu/rxuo3/limits.hh"
#include "cpu/timebuf.hh"
#include "sim/probe/probe.hh"

namespace gem5
{

struct BaseRxuO3CPUParams;

namespace rxuo3
{

/**
 * Rename handles both single threaded and SMT rename. Its
 * width is specified by the parameters; each cycle it tries to rename
 * that many instructions. It holds onto the rename history of all
 * instructions with destination registers, storing the
 * arch. register, the new physical register, and the old physical
 * register, to allow for undoing of mappings if squashing happens, or
 * freeing up registers upon commit. Rename handles blocking if the
 * ROB, IQ, or LSQ is going to be full. Rename also handles barriers,
 * and does so by stalling on the instruction until the ROB is empty
 * and there are no instructions in flight to the ROB.
 */
class Rename
{
  public:
    // A deque is used to queue the instructions. Barrier insts must
    // be added to the front of the queue, which is the only reason for
    // using a deque instead of a queue. (Most other stages use a
    // queue)
    typedef std::deque<DynInstPtr> InstQueue;

  public:
    /** Overall rename status. Used to determine if the CPU can
     * deschedule itself due to a lack of activity.
     */
    enum RenameStatus
    {
        Active,
        Inactive
    };

    /** Individual thread status. */
    enum ThreadStatus
    {
        Running,
        Idle,
        StartSquash,
        Squashing,
        Blocked,
        Unblocking,
        SerializeStall
    };

  private:
    /** Rename status. */
    RenameStatus _status;

    /** Per-thread status. */
    ThreadStatus renameStatus[MaxThreads];

    /** Probe points. */
    typedef std::pair<InstSeqNum, PhysRegIdPtr> SeqNumRegPair;
    /** To probe when register renaming for an instruction is complete */
    ProbePointArg<DynInstPtr> *ppRename;
    /**
     * To probe when an instruction is squashed and the register mapping
     * for it needs to be undone
     */
    ProbePointArg<SeqNumRegPair> *ppSquashInRename;

  public:
    /** Rename constructor. */
    Rename(CPU *_cpu, const BaseRxuO3CPUParams &params);

    /** Returns the name of rename. */
    std::string name() const;

    /** Registers probes. */
    void regProbePoints();

    void setFrm(RxuFRMRename *frm_ptr);

    /** Sets the main backwards communication time buffer pointer. */
    void setTimeBuffer(TimeBuffer<TimeStruct> *tb_ptr);

    /** Sets pointer to time buffer used to communicate to the next stage. */
    // void setRenameQueue(TimeBuffer<RenameStruct> *rq_ptr);

    // -- add by hongfei.liu -------------------------------
    // /** Sets pointer to time buffer coming from decode. */
    // void setDecodeQueue(TimeBuffer<DecodeStruct> *dq_ptr);
    
    /** Sets pointer to time buffer coming from predisq. */
    void setPredisqQueue(TimeBuffer<PredisqStruct> *pq_ptr);
    // -----------------------------------------------------

    /** Sets pointer to IEW stage. Used only for initialization. */
    // void setIEWStage(IEW *iew_stage) { iew_ptr = iew_stage; }

    /** Sets pointer to commit stage. Used only for initialization. */
    void
    setCommitStage(Commit *commit_stage)
    {
        commit_ptr = commit_stage;
    }

  private:
    /** Pointer to IEW stage. Used only for initialization. */
    // IEW *iew_ptr;

    /** Pointer to commit stage. Used only for initialization. */
    Commit *commit_ptr;

    RxuFRMRename *frm;

  public:
    /** Initializes variables for the stage. */
    void startupStage();

    /** Clear all thread-specific states */
    void clearStates(ThreadID tid);

    /** Sets pointer to list of active threads. */
    void setActiveThreads(std::list<ThreadID> *at_ptr);

    /** Sets pointer to rename maps (per-thread structures). */
    void setRenameMap(UnifiedRenameMap rm_ptr[MaxThreads]);

    /** Sets pointer to the free list. */
    void setFreeList(UnifiedFreeList *fl_ptr);

    /** Sets pointer to the scoreboard. */
    void setScoreboard(Scoreboard *_scoreboard);

    /** Perform sanity checks after a drain. */
    void drainSanityCheck() const;

    /** Has the stage drained? */
    bool isDrained() const;

    /** Takes over from another CPU's thread. */
    void takeOverFrom();

    /** Squashes all instructions in a thread. */
    void squash(const InstSeqNum &squash_seq_num, ThreadID tid);

    /** Ticks rename, which processes all input signals and attempts to rename
     * as many instructions as possible.
     */
    void tick();

    void recoverAndRelease();

    /** Debugging function used to dump history buffer of renamings. */
    void dumpHistory();

  public:
    /** Reset this pipeline stage */
    void resetStage();

    /** Determines what to do based on rename's current status.
     * @param status_change rename() sets this variable if there was a status
     * change (ie switching from blocking to unblocking).
     * @param tid Thread id to rename instructions from.
     */
    void rename(bool &status_change, ThreadID tid);

    /** Renames instructions for the given thread. Also handles serializing
     * instructions.
     */
    void renameInsts(ThreadID tid);

    /** Inserts unused instructions from a given thread into the skid buffer,
     * to be renamed once rename unblocks.
     */
    void skidInsert(ThreadID tid);

    /** Separates instructions from predisq into individual lists of instructions
     * sorted by thread.
     */
    void sortInsts();

    /** Returns if all of the skid buffers are empty. */
    bool skidsEmpty();

    /** Updates overall rename status based on all of the threads' statuses. */
    void updateStatus();

    /** Switches rename to blocking, and signals back that rename has become
     * blocked.
     * @return Returns true if there is a status change.
     */
    bool block(ThreadID tid);

    /** Switches rename to unblocking if the skid buffer is empty, and signals
     * back that rename has unblocked.
     * @return Returns true if there is a status change.
     */
    bool unblock(ThreadID tid);

    /** Executes actual squash, removing squashed instructions. */
    void doSquash(const InstSeqNum &squash_seq_num, ThreadID tid);

    /** Removes a committed instruction's rename history. */
    void removeFromHistory(InstSeqNum inst_seq_num, ThreadID tid);

    /** Renames the source registers of an instruction. */
    void renameSrcRegs(const DynInstPtr &inst, ThreadID tid);

    /** Renames the destination registers of an instruction. */
    void renameDestRegs(const DynInstPtr &inst, ThreadID tid);

    /** Calculates the number of free ROB entries for a specific thread. */
    int calcFreeROBEntries(ThreadID tid);

    /** Calculates the number of free IQ entries for a specific thread. */
    int calcFreeIQEntries(ThreadID tid);

    /** Calculates the number of free LQ entries for a specific thread. */
    int calcFreeLQEntries(ThreadID tid);

    /** Calculates the number of free SQ entries for a specific thread. */
    int calcFreeSQEntries(ThreadID tid);

    /** Returns the number of valid instructions coming from predisq. */
    unsigned validInsts();

    /** Reads signals telling rename to block/unblock. */
    void readStallSignals(ThreadID tid);

    /** Checks if any stages are telling rename to block. */
    bool checkStall(ThreadID tid);

    bool checkFRMRenameStall();

    /** Gets the number of free entries for a specific thread. */
    void readFreeEntries(ThreadID tid);

    /** Checks the signals and updates the status. */
    bool checkSignalsAndUpdate(ThreadID tid);

    /** Either serializes on the next instruction available in the InstQueue,
     * or records that it must serialize on the next instruction to enter
     * rename.
     * @param inst_list The list of younger, unprocessed instructions for the
     * thread that has the serializeAfter instruction.
     * @param tid The thread id.
     */
    void serializeAfter(InstQueue &inst_list, ThreadID tid);

    /** Holds the information for each destination register rename. It holds
     * the instruction's sequence number, the arch register, the old physical
     * register for that arch. register, and the new physical register.
     */
    struct RenameHistory
    {
        RenameHistory(InstSeqNum _instSeqNum, const RegId& _archReg,
                      PhysRegIdPtr _newPhysReg,
                      PhysRegIdPtr _prevPhysReg)
            : instSeqNum(_instSeqNum), archReg(_archReg),
              newPhysReg(_newPhysReg), prevPhysReg(_prevPhysReg)
        {
        }

        /** The sequence number of the instruction that renamed. */
        InstSeqNum instSeqNum;
        /** The architectural register index that was renamed. */
        RegId archReg;
        /** The new physical register that the arch. register is renamed to. */
        PhysRegIdPtr newPhysReg;
        /** The old physical register that the arch. register was renamed to.
         */
        PhysRegIdPtr prevPhysReg;
    };

    struct FRMRenameHistory
    {
        FRMRenameHistory(InstSeqNum _instSeqNum, unsigned _newPhysReg, unsigned _prevPhysReg)
            : instSeqNum(_instSeqNum), newPhysReg(_newPhysReg), prevPhysReg(_prevPhysReg)
        {
        }

        /** The sequence number of the instruction that renamed. */
        InstSeqNum instSeqNum;
        /** The architectural register index that was renamed. */
        // RegId archReg;
        /** The new physical register that the arch. register is renamed to. */
        unsigned newPhysReg;
        /** The old physical register that the arch. register was renamed to.
         */
        unsigned prevPhysReg;
    };

    /** A per-thread list of all destination register renames, used to either
     * undo rename mappings or free old physical registers.
     */
    std::list<RenameHistory> historyBuffer[MaxThreads];

    std::list<FRMRenameHistory> frmHistoryBuffer[MaxThreads];

    /** Pointer to CPU. */
    CPU *cpu;

    /** Pointer to main time buffer used for backwards communication. */
    TimeBuffer<TimeStruct> *timeBuffer;

    /** Wire to get IEW's output from backwards time buffer. */
    TimeBuffer<TimeStruct>::wire fromIEW;

    /** Wire to get commit's output from backwards time buffer. */
    TimeBuffer<TimeStruct>::wire fromCommit;

    // -- add by hongfei.liu ------------------------------------
    // /** Wire to write infromation heading to previous stages. */
    // TimeBuffer<TimeStruct>::wire toDecode;
    /** Wire to write infromation heading to previous stages. */
    TimeBuffer<TimeStruct>::wire toPredisq;
    // ----------------------------------------------------------

    /** Rename instruction queue. */
    // TimeBuffer<RenameStruct> *renameQueue;

    // /** Wire to write any information heading to IEW. */
    // TimeBuffer<RenameStruct>::wire toIEW;

    // -- add by hongfei.liu ----------------------------------------
    // /** Decode instruction queue interface. */
    // TimeBuffer<DecodeStruct> *decodeQueue;

    // /** Wire to get decode's output from decode queue. */
    // TimeBuffer<DecodeStruct>::wire fromDecode;
    /** Decode instruction queue interface. */
    TimeBuffer<PredisqStruct> *predisqQueue;

    /** Wire to get predisq's output from predisq queue. */
    TimeBuffer<PredisqStruct>::wire fromPredisq;
    // --------------------------------------------------------------

    /** Queue of all instructions coming from predisq this cycle. */
    InstQueue insts[MaxThreads];

    /** Skid buffer between rename and predisq. */
    InstQueue skidBuffer[MaxThreads];

    /** Rename map interface. */
    UnifiedRenameMap *renameMap[MaxThreads];

    /** Free list interface. */
    UnifiedFreeList *freeList;

    /** Pointer to the list of active threads. */
    std::list<ThreadID> *activeThreads;

    /** Pointer to the scoreboard. */
    Scoreboard *scoreboard;

    /** Count of instructions in progress that have been sent off to the IQ
     * and ROB, but are not yet included in their occupancy counts.
     */
    int instsInProgress[MaxThreads];

    /** Count of Load instructions in progress that have been sent off to the
     * IQ and ROB, but are not yet included in their occupancy counts.
     */
    int loadsInProgress[MaxThreads];

    /** Count of Store instructions in progress that have been sent off to the
     * IQ and ROB, but are not yet included in their occupancy counts.
     */
    int storesInProgress[MaxThreads];

    /** Variable that tracks if rename has written to the time buffer this
     * cycle. Used to tell CPU if there is activity this cycle.
     */
    bool wroteToTimeBuffer;

    /** Structures whose free entries impact the amount of instructions that
     * can be renamed.
     */
    struct FreeEntries
    {
        unsigned iqEntries;
        unsigned robEntries;
        unsigned lqEntries;
        unsigned sqEntries;
    };

    /** Per-thread tracking of the number of free entries of back-end
     * structures.
     */
    FreeEntries freeEntries[MaxThreads];

    /** Records if the ROB is empty. In SMT mode the ROB may be dynamically
     * partitioned between threads, so the ROB must tell rename when it is
     * empty.
     */
    bool emptyROB[MaxThreads];

    /** Source of possible stalls. */
    struct Stalls
    {
        bool iew;
        bool commit;
    };

    /** Tracks which stages are telling predisq to stall. */
    Stalls stalls[MaxThreads];

    /** The serialize instruction that rename has stalled on. */
    DynInstPtr serializeInst[MaxThreads];

    /** Records if rename needs to serialize on the next instruction for any
     * thread.
     */
    bool serializeOnNextInst[MaxThreads];

    /** Delay between iew and rename, in ticks. */
    int iewToRenameDelay;

    // -- add by hongfei.liu -----------------------------
    // /** Delay between decode and rename, in ticks. */
    // int decodeToRenameDelay;
    /** Delay between predisq and rename, in ticks. */
    int predisqToRenameDelay;
    // ---------------------------------------------------

    /** Delay between commit and rename, in ticks. */
    unsigned commitToRenameDelay;

    /** Rename width, in instructions. */
    unsigned renameWidth;

    /** The index of the instruction in the time buffer to IEW that rename is
     * currently using.
     */
    unsigned toIEWIndex;

    /** Whether or not rename needs to block this cycle. */
    bool blockThisCycle;

    /** Whether or not rename needs to resume a serialize instruction
     * after squashing. */
    bool resumeSerialize;

    /** Whether or not rename needs to resume clearing out the skidbuffer
     * after squashing. */
    bool resumeUnblocking;

    /** The number of threads active in rename. */
    ThreadID numThreads;

    /** The maximum skid buffer size. */
    unsigned skidBufferMax;

    /** Enum to record the source of a structure full stall.  Can come from
     * either ROB, IQ, LSQ, and it is priortized in that order.
     */
    enum FullSource
    {
        ROB,
        IQ,
        LQ,
        SQ,
        NONE
    };

    /** Function used to increment the stat that corresponds to the source of
     * the stall.
     */
    void incrFullStat(const FullSource &source);

    struct RenameStats : public statistics::Group
    {
        RenameStats(statistics::Group *parent);

        /** Stat for total number of cycles spent squashing. */
        statistics::Scalar squashCycles;
        /** Stat for total number of cycles spent idle. */
        statistics::Scalar idleCycles;
        /** Stat for total number of cycles spent blocking. */
        statistics::Scalar blockCycles;
        /** Stat for total number of cycles spent stalling for a serializing
         *  inst. */
        statistics::Scalar serializeStallCycles;
        /** Stat for total number of cycles spent running normally. */
        statistics::Scalar runCycles;
        /** Stat for total number of cycles spent unblocking. */
        statistics::Scalar unblockCycles;
        /** Stat for total number of renamed instructions. */
        statistics::Scalar renamedInsts;
        /** Stat for total number of squashed instructions that rename
         * discards. */
        statistics::Scalar squashedInsts;
        /** Stat for total number of times that the ROB starts a stall in
         * rename. */
        statistics::Scalar ROBFullEvents;
        /** Stat for total number of times that the IQ starts a stall in
         *  rename. */
        statistics::Scalar IQFullEvents;
        /** Stat for total number of times that the LQ starts a stall in
         *  rename. */
        statistics::Scalar LQFullEvents;
        /** Stat for total number of times that the SQ starts a stall in
         *  rename. */
        statistics::Scalar SQFullEvents;
        /** Stat for total number of times that rename runs out of free
         *  registers to use to rename. */
        statistics::Scalar fullRegistersEvents;
        /** Stat for total number of renamed destination registers. */
        statistics::Scalar renamedOperands;
        /** Stat for total number of source register rename lookups. */
        statistics::Scalar lookups;
        statistics::Scalar intLookups;
        statistics::Scalar fpLookups;
        statistics::Scalar vecLookups;
        statistics::Scalar vecPredLookups;
        statistics::Scalar matLookups;
        /** Stat for total number of committed renaming mappings. */
        statistics::Scalar committedMaps;
        /** Stat for total number of mappings that were undone due to a
         *  squash. */
        statistics::Scalar undoneMaps;
        /** Number of serialize instructions handled. */
        statistics::Scalar serializing;
        /** Number of instructions marked as temporarily serializing. */
        statistics::Scalar tempSerializing;
        /** Number of instructions inserted into skid buffers. */
        statistics::Scalar skidInsts;
    } stats;
};

} // namespace rxuo3
} // namespace gem5

#endif // __CPU_RxuO3_RENAME_HH__
