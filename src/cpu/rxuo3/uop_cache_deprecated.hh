#ifndef __CPU_RxuO3_UOPCACHE_HH__
#define __CPU_RxuO3_UOPCACHE_HH__

#include <queue>
#include <list>

#include "base/statistics.hh"
#include "cpu/rxuo3/comm.hh"
#include "cpu/rxuo3/dyn_inst_ptr.hh"
#include "cpu/rxuo3/limits.hh"
#include "cpu/timebuf.hh"
#include "cpu/pred/bpred_unit.hh"

namespace gem5
{

struct BaseRxuO3CPUParams;

namespace rxuo3
{

class CPU;

/**
 * UopCache class handles both single threaded and SMT
 * uc. Its width is specified by the parameters; each cycles it
 * tries to uc that many instructions. Because instructions are
 * actually ucd when the StaticInst is created, this stage does
 * not do much other than check any PC-relative branches.
 */
class UopCache
{
  public:
    /** Overall uc stage status. Used to determine if the CPU can
     * deschedule itself due to a lack of activity.
     */
    enum UopCacheStatus
    {
        Active,
        LookUp,
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
        Unblocking
    };

    struct UopCacheEntry{
        Addr tag;
        bool valid;
        unsigned numInsts;
        int age;
        std::list<StaticInstPtr> insts;

        UopCacheEntry() : tag(0), valid(false), numInsts(0), age(0) { }

        void clear() {
           tag = 0;
           valid = false;
           age = 0;
           numInsts = 0;
           insts.clear();
        }
    };

    UopCacheEntry *cache;

  private:
    /** UopCache status. */
    UopCacheStatus _status;

    /** Per-thread status. */
    ThreadStatus ucStatus[MaxThreads];

  public:
    /** UopCache constructor. */
    UopCache(CPU *_cpu, const BaseRxuO3CPUParams &params);

    void startupStage();

    /** Clear all thread-specific states */
    void clearStates(ThreadID tid);

    void resetStage();

    /** Returns the name of uc. */
    std::string name() const;

    /** Sets the main backwards communication time buffer pointer. */
    void setTimeBuffer(TimeBuffer<TimeStruct> *tb_ptr);

    /** Sets pointer to time buffer used to communicate to the next stage. */
    void setUCQueue(TimeBuffer<DecodeStruct> *dq_ptr);

    /** Sets pointer to time buffer coming from fetch. */
    void setFetchQueue(TimeBuffer<UCStruct> *fq_ptr);

    void setPCQueue(TimeBuffer<PCStruct> *pq_ptr);

    /** Sets pointer to list of active threads. */
    void setActiveThreads(std::list<ThreadID> *at_ptr);

    /** Perform sanity checks after a drain. */
    void drainSanityCheck() const;

    /** Has the stage drained? */
    bool isDrained() const;

    bool ucActive() { return _status == Active; }

    /** Takes over from another CPU's thread. */
    void takeOverFrom() { resetStage(); }

    /** Ticks uc, processing all input signals and decoding as many
     * instructions as possible.
     */
    void tick();

    /** Determines what to do based on uc's current status.
     * @param status_change uc() sets this variable if there was a status
     * change (ie switching from from blocking to unblocking).
     * @param tid Thread id to uc instructions from.
     */
    void uc(bool &status_change, ThreadID tid);

    int lindex(Addr pc_in, unsigned instShiftAmt);

    int finallindex(int lindex, int lowPcBits, int way);

    void get(Addr pc, ThreadID tid);

    bool update(Addr pc, std::list<StaticInstPtr> insts);

    DynInstPtr buildInst(ThreadID tid, StaticInstPtr staticInst,
                                        StaticInstPtr curMacroop, const PCStateBase &this_pc,
                                        const PCStateBase &next_pc, bool trace);

    bool lookupAndUpdateNextPC(const DynInstPtr &inst, PCStateBase &next_pc);

    bool lookup(Addr pc);

    void process(ThreadID tid);

    void process(PCStateBase &this_pc);

    bool useUopCache() { return _useUopCache; }

    bool fetchCanSend(InstSeqNum sn);

  private:
    /** Inserts a thread's instructions into the skid buffer, to be ucd
     * once uc unblocks.
     */
    void skidInsert(ThreadID tid);

    /** Returns if all of the skid buffers are empty. */
    bool skidsEmpty();

    /** Updates overall uc status based on all of the threads' statuses. */
    void updateStatus();

    /** Separates instructions from fetch into individual lists of instructions
     * sorted by thread.
     */
    void sortInsts();

    /** Reads all stall signals from the backwards communication timebuffer. */
    void readStallSignals(ThreadID tid);

    /** Checks all input signals and updates uc's status appropriately. */
    bool checkSignalsAndUpdate(ThreadID tid);

    /** Checks all stall signals, and returns if any are true. */
    bool checkStall(ThreadID tid) const;

    /** Returns if there any instructions from fetch on this cycle. */
    bool fetchInstsValid();

    /** Switches uc to blocking, and signals back that uc has
     * become blocked.
     * @return Returns true if there is a status change.
     */
    bool block(ThreadID tid);

    /** Switches uc to unblocking if the skid buffer is empty, and
     * signals back that uc has unblocked.
     * @return Returns true if there is a status change.
     */
    bool unblock(ThreadID tid);

    /** Squashes if there is a PC-relative branch that was predicted
     * incorrectly. Sends squash information back to fetch.
     */
    void squash(const DynInstPtr &inst, ThreadID tid);

    // void setBPU(branch_prediction::BPredUnit *_bpu) { branchPred = _bpu; }

    void clearInsts() {
      for (ThreadID tid = 0; tid < MaxThreads; tid++)
      while(!insts.empty()) {
        insts.pop();
      }
    }

  public:
    /** Squashes due to commit signalling a squash. Changes status to
     * squashing and clears block/unblock signals as needed.
     */
    unsigned squash(ThreadID tid);

    bool checkCanSend(InstSeqNum sn);

  private:
    // Interfaces to objects outside of uc.
    /** CPU interface. */
    CPU *cpu;

    // branch_prediction::BPredUnit *branchPred;

    const unsigned logSize;
    const unsigned tagBits;
    const unsigned setBits;
    const unsigned assocBits;
    const unsigned numInstsEntry;
    const unsigned sets;
    const unsigned assoc;
    const unsigned setMask;
    const unsigned tagMask;
    const unsigned initAge;
    const unsigned shift;

    bool _useUopCache;

    bool useHashing;

    /** Time buffer interface. */
    TimeBuffer<TimeStruct> *timeBuffer;

    /** Wire to get predisq's output from backwards time buffer. */
    TimeBuffer<TimeStruct>::wire fromPredisq;

    /** Wire to write information heading to previous stages. */
    // Might not be the best name as not only fetch will read it.
    TimeBuffer<TimeStruct>::wire toFetch;

    TimeBuffer<TimeStruct>::wire fromCommit;

    TimeBuffer<TimeStruct>::wire fromDecode;

    /** UopCache instruction queue. */
    TimeBuffer<DecodeStruct> *ucQueue;

    /** Wire used to write any information heading to predisq. */
    TimeBuffer<DecodeStruct>::wire toPredisq;

    /** Fetch instruction queue interface. */
    TimeBuffer<UCStruct> *fetchQueue;

    /** Wire to get fetch's output from fetch queue. */
    TimeBuffer<UCStruct>::wire fromFetch_u;

    TimeBuffer<PCStruct>::wire fromFetch;

    /** Queue of all instructions coming from uop this cycle. */
    std::queue<StaticInstPtr> insts;

    std::deque<DynInstPtr> instQueue;

    /** Skid buffer between fetch and uc. */
    std::queue<DynInstPtr> skidBuffer[MaxThreads];

    /** Variable that tracks if uc has written to the time buffer this
     * cycle. Used to tell CPU if there is activity this cycle.
     */
    bool wroteToTimeBuffer;

    InstSeqNum sentInstSeqNum;

    std::unique_ptr<PCStateBase> uc_pc[MaxThreads];

    /** Source of possible stalls. */
    struct Stalls
    {
        bool predisq;
    };

    /** Tracks which stages are telling uc to stall. */
    Stalls stalls[MaxThreads];

    /** Fetch to uc delay. */
    Cycles fetchToUopCacheDelay;

    /** Predisq to uc delay. */
    Cycles predisqToUopCacheDelay;

    /** The width of uc, in instructions. */
    unsigned ucWidth;

    unsigned predisqWidth;

    /** Index of instructions being sent to predisq. */
    unsigned toPredisqIndex;

    /** number of Active Threads*/
    ThreadID numThreads;

    /** List of active thread ids */
    std::list<ThreadID> *activeThreads;

    /** Maximum size of the skid buffer. */
    unsigned skidBufferMax;

    /** SeqNum of Squashing Branch Delay Instruction (used for MIPS)*/
    Addr bdelayDoneSeqNum[MaxThreads];

    /** Instruction used for squashing branch (used for MIPS)*/
    DynInstPtr squashInst[MaxThreads];

    /** Tells when their is a pending delay slot inst. to send
     *  to rename. If there is, then wait squash after the next
     *  instruction (used for MIPS).
     */
    bool squashAfterDelaySlot[MaxThreads];

    struct UopCacheStats : public statistics::Group
    {
        UopCacheStats(CPU *cpu);

        /** Stat for total number of idle cycles. */
        statistics::Scalar idleCycles;
        /** Stat for total number of blocked cycles. */
        statistics::Scalar blockedCycles;
        /** Stat for total number of normal running cycles. */
        statistics::Scalar runCycles;
        /** Stat for total number of unblocking cycles. */
        statistics::Scalar unblockCycles;
        /** Stat for total number of squashing cycles. */
        statistics::Scalar squashCycles;
        /** Stat for number of times a branch is resolved at uc. */
        statistics::Scalar branchResolved;
        /** Stat for number of times a branch mispredict is detected. */
        statistics::Scalar branchMispred;
        /** Stat for number of times uc detected a non-control instruction
         * incorrectly predicted as a branch.
         */
        statistics::Scalar controlMispred;
        /** Stat for total number of ucd instructions. */
        statistics::Scalar decodedInsts;
        /** Stat for total number of squashed instructions. */
        statistics::Scalar squashedInsts;
        /* Number of miss times due to BPU_predict in uc. */
        statistics::Scalar bpuMissUopCacheCount;
        /** Stat for total number of hit cycles. */
        statistics::Scalar hitCycles;
        /** Stat for total number of miss cycles. */
        statistics::Scalar missCycles;
        /** Stat for total number of uop cache full. */
        statistics::Scalar cacheFull;
        /** Stat for total number of uop cache loopup. */
        statistics::Scalar lookupCount;
        /** Stat for uop cache hit rate. */
        statistics::Formula hitRate;
    } stats;
};

} // namespace rxuo3
} // namespace gem5

#endif
