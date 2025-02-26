#ifndef __CPU_RxuO3_BPU1_HH__
#define __CPU_RxuO3_BPU1_HH__

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
 * BPU1 stage
 */
class Bpu1
{
  public:
    /** Overall bpu1 stage status. Used to determine if the CPU can
     * deschedule itself due to a lack of activity.
     */
    enum Bpu1Status
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
        Blocked,
        Unblocking
    };

  private:
    /** Bpu1 status. */
    Bpu1Status _status;

    /** Per-thread status. */
    ThreadStatus bpu1Status[MaxThreads];

  public:
    /** Bpu1 constructor. */
    Bpu1(CPU *_cpu, const BaseRxuO3CPUParams &params);

    /** Initialize stage. */
    void startupStage();

    /** Clear all thread-specific states */
    void clearStates(ThreadID tid);

    void resetStage();

    /** Returns the name of bpu1. */
    std::string name() const;

    /** Sets the main backwards communication time buffer pointer. */
    void setTimeBuffer(TimeBuffer<TimeStruct> *tb_ptr);

    /** Sets pointer to time buffer used to communicate to the next stage. */
    void setBpu1Queue(TimeBuffer<Bpu1Struct> *b1q_ptr);

    /** Sets pointer to time buffer coming from bpu0. */
    void setBpu0Queue(TimeBuffer<Bpu0Struct> *b0q_ptr);

    /** Sets pointer to list of active threads. */
    void setActiveThreads(std::list<ThreadID> *at_ptr);

    /** Perform sanity checks after a drain. */
    void drainSanityCheck() const;

    /** Has the stage drained? */
    bool isDrained() const;

    /** Takes over from another CPU's thread. */
    void takeOverFrom() { resetStage(); }

    /** Ticks bpu1, processing all input signals and processing as many
     * instructions as possible.
     */
    void tick();

    /** Determines what to do based on bpu1's current status.
     * @param status_change bpu1() sets this variable if there was a status
     * change (ie switching from from blocking to unblocking).
     * @param tid Thread id to bpu1 instructions from.
     */
    void bpu1(bool &status_change, ThreadID tid);

    /** process Insts.
     */
    void processInsts(ThreadID tid);

  private:
    /** Updates overall bpu1 status based on all of the threads' statuses. */
    void updateStatus();

    /** Separates instructions from bpu0 into individual lists of instructions
     * sorted by thread.
     */
    void sortInsts();

    /** Reads all stall signals from the backwards communication timebuffer. */
    void readStallSignals(ThreadID tid);

    /** Checks all input signals and updates bpu1's status appropriately. */
    bool checkSignalsAndUpdate(ThreadID tid);

    /** Checks all stall signals, and returns if any are true. */
    bool checkStall(ThreadID tid) const;

    /** Returns if there any instructions from bpu0 on this cycle. */
    bool bpu0InstsValid();

    /** Switches bpu1 to blocking, and signals back that bpu1 has
     * become blocked.
     * @return Returns true if there is a status change.
     */
    bool block(ThreadID tid);

    /** Switches bpu1 to unblocking, and
     * signals back that bpu1 has unblocked.
     * @return Returns true if there is a status change.
     */
    bool unblock(ThreadID tid);

    /** Squash signal from bpu1. */
    void squashFromBpu1(const DynInstPtr &inst, ThreadID tid);

  public:
    /** Squashes due to commit signalling a squash. Changes status to
     * squashing and clears block/unblock signals as needed.
     */
    void squash(ThreadID tid, InstSeqNum squashedSeqNum);

    /**
     * Looks up in the branch predictor to see if the next PC should be
     * either next PC+=MachInst or a branch target.
     */
    bool lookupAndUpdateNextPC(const DynInstPtr &inst, PCStateBase &pc);

  private:
    // Interfaces to objects outside of bpu1.
    /** CPU interface. */
    CPU *cpu;

    /** Time buffer interface. */
    TimeBuffer<TimeStruct> *timeBuffer;

    /** Wire to get IBandLB's output from backwards time buffer. */
    TimeBuffer<TimeStruct>::wire fromIBandLB;

    /** Wire to get decode's information from backwards time buffer. */
    TimeBuffer<TimeStruct>::wire fromDecode;

    /** Wire to get commit's information from backwards time buffer. */
    TimeBuffer<TimeStruct>::wire fromCommit;

    /** Wire to write information heading to previous stages. */
    // Might not be the best name as not only bpu0 will read it.
    TimeBuffer<TimeStruct>::wire toBpu0;

    /** Bpu1 instruction queue. */
    TimeBuffer<Bpu1Struct> *bpu1Queue;

    /** Wire used to write any information heading to IBandLB. */
    TimeBuffer<Bpu1Struct>::wire toIBandLB;

    /** bpu0 instruction queue interface. */
    TimeBuffer<Bpu0Struct> *bpu0Queue;

    /** Wire to get bpu0's output from bpu0 queue. */
    TimeBuffer<Bpu0Struct>::wire fromBpu0;

    /** Queue of all instructions coming from bpu0 this cycle. */
    std::list<DynInstPtr> insts[MaxThreads];

    /** Variable that tracks if bpu1 has written to the time buffer this
     * cycle. Used to tell CPU if there is activity this cycle.
     */
    bool wroteToTimeBuffer;

    /** Source of possible stalls. */
    struct Stalls
    {
        bool IBandLB;
    };

    /** Tracks which stages are telling bpu1 to stall. */
    Stalls stalls[MaxThreads];

    /** IBandLB to bpu1 delay. */
    Cycles IBandLBToBpu1Delay;

    /** Decode to bpu1 delay. */
    Cycles decodeToBpu1Delay;

    /** Commit to bpu1 delay. */
    Cycles commitToBpu1Delay;

    /** Bpu0 to bpu1 delay. */
    Cycles bpu0ToBpu1Delay;

    /** The width of bpu1, in instructions. */
    unsigned bpu1Width;

    /** Index of instructions being sent to IBandLB. */
    unsigned toIBandLBIndex;

    /** number of Active Threads*/
    ThreadID numThreads;

    /** List of active thread ids */
    std::list<ThreadID> *activeThreads;

    /** BPredUnit. */
    branch_prediction::BPredUnit *branchPred;

    /** Storing UC instructions. */
    Addr ucBlockStoreAddr;

    std::list<StaticInstPtr> ucacheLineData;

    std::list<Addr> pc_insts;

     /** Storing CAM instructions. */
    Addr camBlockStoreAddr;
    
    std::list<StaticInstPtr> camcacheLineData;

    std::list<Addr> cam_pc_insts;
    /** L2BTB delay. */
    unsigned L2BTBDelay = 0;

    struct Bpu1Stats : public statistics::Group
    {
        Bpu1Stats(CPU *cpu);

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
        /** Stat for total number of bpu1ed instructions. */
        statistics::Scalar bpu1edInsts;
        /** Stat for total number of squashed instructions. */
        statistics::Scalar squashedInsts;
    } stats;
};

} // namespace rxuo3
} // namespace gem5

#endif // __CPU_RxuO3_BPU1_HH__
