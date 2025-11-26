// -- add by hongfei.liu --------------------------
#ifndef __CPU_RxuO3_IBandLB_HH__
#define __CPU_RxuO3_IBandLB_HH__

// #include <queue>
#include <list>

#include "base/statistics.hh"
#include "cpu/rxuo3/comm.hh"
#include "cpu/rxuo3/dyn_inst_ptr.hh"
#include "cpu/rxuo3/limits.hh"
#include "cpu/timebuf.hh"
// #include "cpu/pred/bpred_unit.hh"

namespace gem5
{

struct BaseRxuO3CPUParams;

namespace rxuo3
{

class CPU;

/**
 * IBandLB Stage
 */
class IBandLB
{
  public:
    /** Overall IBandLB stage status. Used to determine if the CPU can
     * deschedule itself due to a lack of activity.
     */
    enum IBandLBStatus
    {
        Active,
        Inactive
    };

    /** Individual thread status. */
    enum ThreadStatus
    {
        Running,
        Idle,
        Squashing,
        Blocked
    };

  private:
    /** IBandLB status. */
    IBandLBStatus _status;

    /** Per-thread status. */
    ThreadStatus IBandLBStatus[MaxThreads];

    /** Per-thread status in last cycle. */
    ThreadStatus lastStatus[MaxThreads];

  public:
    /** IBandLB constructor. */
    IBandLB(CPU *_cpu, const BaseRxuO3CPUParams &params);

    /** Initializes variables for the stage. */ 
    void startupStage();

    /** Clear all thread-specific states */
    void clearStates(ThreadID tid);

    /** Reset this pipeline stage */
    void resetStage();

    /** Returns the name of IBandLB. */
    std::string name() const;

    /** Sets the main backwards communication time buffer pointer. */
    void setTimeBuffer(TimeBuffer<TimeStruct> *tb_ptr);

    /** Sets pointer to time buffer used to communicate to the next stage. */
    void setIBandLBQueue(TimeBuffer<IBandLBStruct> *iq_ptr);

    /** Sets pointer to time buffer coming from bpu1. */
    void setBpu1Queue(TimeBuffer<Bpu1Struct> *b1q_ptr);

    /** Sets pointer to time buffer coming from fetch's pc. */
    // void setPcQueue(TimeBuffer<PcStruct> *pq_ptr);

    /** Sets pointer to list of active threads. */
    void setActiveThreads(std::list<ThreadID> *at_ptr);

    /** Perform sanity checks after a drain. */
    void drainSanityCheck() const;

    /** Has the stage drained? */
    bool isDrained() const;

    /** Takes over from another CPU's thread. */
    void takeOverFrom() { resetStage(); }

    /** Ticks IBandLB
     */
    void tick();

    /** Determines what to do based on IBandLB's current status.
     * @param status_change iBandLB() sets this variable if there was a status
     * change (ie switching from blocking to running).
     * @param tid Thread id to IBandLB instructions from.
     */
    void iBandLB(bool &status_change, ThreadID tid);

    /** Insert instructions into InstBuffer.
     */
    void InsertInsts(ThreadID tid);

    /** Send instructions to decode.
     */
    void SendInsts(ThreadID tid);

    /** Update inst buffer free entries and notify bpu1.
     */
    void updateFreeEntries();

  private:

    /** Updates overall IBandLB status based on all of the threads' statuses. */
    void updateStatus();

    /** Separates instructions from bpu1 into individual lists of instructions
     * sorted by thread.
     */
    void sortInsts();

    // /** Build a DynInst from LoopBuffer.
    //  */
    // DynInstPtr buildInst(ThreadID tid, StaticInstPtr staticInst,
    //         StaticInstPtr curMacroop, const PCStateBase &this_pc,
    //         const PCStateBase &next_pc, bool trace);

    // /** Generate some DynInsts from LoopBuffer.
    //  */
    // void generateInstsLB();

    /** Reads all stall signals from the backwards communication timebuffer. */
    void readStallSignals(ThreadID tid);

    /** Checks all input signals and updates IBandLB's status appropriately. */
    bool checkSignalsAndUpdate(ThreadID tid);

    /** Checks all stall signals, and returns if any are true. */
    bool checkStall(ThreadID tid) const;

    /** Returns if there any instructions from bpu1 on this cycle. */
    bool bpu1InstsValid();

    /** Switches IBandLB to blocking, and signals back that IBandLB has
     * become blocked.
     * @return Returns true if there is a status change.
     */
    bool block(ThreadID tid);

    /** Switches IBandLB to running, and
     * signals back that IBandLB has unblocked.
     */
    void unblock(ThreadID tid);

  public:
    /** Squashes due to commit signalling a squash. Changes status to
     * squashing.
     */
    void squash(ThreadID tid, const InstSeqNum &seq_num);

    /**
     * Looks up in the branch predictor to see if the next PC should be
     * either next PC+=MachInst or a branch target.
     */
    // bool lookupAndUpdateNextPC(const DynInstPtr &inst, PCStateBase &pc);

    // /** BPredUnit. */
    // branch_prediction::BPredUnit *branchPred;

  private:
    // Interfaces to objects outside of IBandLB.
    /** CPU interface. */
    CPU *cpu;

    /** Time buffer interface. */
    TimeBuffer<TimeStruct> *timeBuffer;

    /** Wire to get decode's output from backwards time buffer. */
    TimeBuffer<TimeStruct>::wire fromDecode;

    /** Wire to get commit's information from backwards time buffer. */
    TimeBuffer<TimeStruct>::wire fromCommit;

    /** Wire to write information heading to previous stages. */
    TimeBuffer<TimeStruct>::wire toBpu1;

    /** IBandLB instruction queue. */
    TimeBuffer<IBandLBStruct> *IBandLBQueue;

    /** Wire used to write any information heading to decode. */
    TimeBuffer<IBandLBStruct>::wire toDecode;

    /** Bpu1 instruction queue interface. */
    TimeBuffer<Bpu1Struct> *bpu1Queue;

    /** Fetch PC queue interface. */
    // TimeBuffer<PcStruct> *pcQueue;

    // /** Flag bit indicating that loopbuffer is storing instructions.*/
    // bool loopBufferStore;

    // /** Flag bit indicating that loopbuffer is active.*/
    // bool loopBufferActive;

    // /** Loopbuffer is storing the loop level of instructions.*/
    // int loopLayer;

    // /** Number of instructions loopbuffer stored. */
    // int loopBufferSize;

public:
    /** Wire to get bpu1's output from bpu1 queue. */
    TimeBuffer<Bpu1Struct>::wire fromBpu1;

private:
    /** Queue of all instructions coming from bpu1 and loopbuffer this cycle. */
    std::list<DynInstPtr> insts[MaxThreads];
public:
    /** inst buffer. */
    std::list<DynInstPtr> instbuffer;

    // /** Loop buffer. */
    // std::deque<std::map<Addr, StaticInstPtr>> LoopBuffer;

    // /** A queue for storing loop branch instructions PC. */
    // std::deque<Addr> loop_PC;

    // /** Mark whether the instructions at each loop level have been stored successfully. */
    // std::deque<bool> loopbody_stored;

    // /** Query iterators for loopbuffer. */
    // typedef std::map<Addr, StaticInstPtr>::iterator LoopBufferIt;

    /** The width of InstBuffer, in instructions. */
    unsigned IBWidth;

    // /** The width of LoopBuffer, in instructions. */
    // unsigned LBWidth;

    /** The size of the InstBuffer in instructions. */
    unsigned IBSize;

    // /** The size of the LoopBuffer in instructions. */
    // unsigned LBSize;

    // /** Maximum loop level. */
    // unsigned MaxLoopNums;

    // /** Enable bit of loopbuffer. */
    // bool LoopBufferUse;

    /** number of insts insert to instbuffer this cycle */
    int instBufferInsertNums;

private:
    /** Number of inst buffer free entries*/
    int freeEntries;

    /** Variable that tracks if IBandLB has written to the time buffer this
     * cycle. Used to tell CPU if there is activity this cycle.
     */
    bool wroteToTimeBuffer;

    /** Source of possible stalls. */
    struct Stalls
    {
        bool decode;
    };

    /** Tracks which stages are telling IBandLB to stall. */
    Stalls stalls[MaxThreads];

    /** decode to IBandLB delay. */
    Cycles decodeToIBandLBDelay;

    /** Commit to IBandLB delay. */
    Cycles commitToIBandLBDelay;

    /** Bpu1 to IBandLB delay. */
    Cycles bpu1ToIBandLBDelay;

    /** Pc to IBandLB delay. */
    Cycles pcToIBandLBDelay;

    /** Index of instructions being sent to decode. */
    unsigned toDecodeIndex;

    /** number of Active Threads*/
    ThreadID numThreads;

    /** List of active thread ids */
    std::list<ThreadID> *activeThreads;

    struct IBandLBStats : public statistics::Group
    {
        IBandLBStats(CPU *cpu);

        /** Stat for total number of idle cycles. */
        statistics::Scalar idleCycles;
        /** Stat for total number of blocked cycles. */
        statistics::Scalar blockedCycles;
        /** Stat for total number of normal running cycles. */
        statistics::Scalar runCycles;
        /** Stat for total number of squashing cycles. */
        statistics::Scalar squashCycles;
        /** Stat for total number of times that the instBuffer is full. */
        statistics::Scalar instBufferFullEvents;
        /** Stat for total number of instructions sent by IBandLB. */
        statistics::Scalar IBandLBedInsts;
        /** Stat for total number of squashed instructions. */
        statistics::Scalar squashedInsts;

        // /** Stat for total number of times that the loopBuffer is full. */
        // statistics::Scalar loopBufferFullEvents;
        /** Stat for total number of instructions from bpu1. */
        statistics::Scalar fromBpu1Insts;
        // /** Stat for total number of instructions from LoopBuffer. */
        // statistics::Scalar fromLBInsts;
        // /** Stat for active times of LoopBuffer. */
        // statistics::Scalar loopBufferActive;
    } stats;
};

} // namespace rxuo3
} // namespace gem5

#endif // __CPU_RxuO3_IBandLB_HH__
// ---------------------------------------------------------------