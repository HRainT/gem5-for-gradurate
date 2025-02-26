// -- add by hongfei.liu --------------------------
#ifndef __CPU_RxuO3_PREDISQ_HH__
#define __CPU_RxuO3_PREDISQ_HH__

#include <list>

#include "base/statistics.hh"
#include "cpu/rxuo3/comm.hh"
#include "cpu/rxuo3/dyn_inst_ptr.hh"
#include "cpu/rxuo3/limits.hh"
#include "cpu/timebuf.hh"
#include <string>
#include <algorithm>

namespace gem5
{

struct BaseRxuO3CPUParams;

namespace rxuo3
{

class CPU;

/**
 * Predisq Stage
 */
class Predisq
{
  public:
    /** Overall predisq stage status. Used to determine if the CPU can
     * deschedule itself due to a lack of activity.
     */
    enum PredisqStatus
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
    /** Predisq status. */
    PredisqStatus _status;

    /** Per-thread status. */
    ThreadStatus predisqStatus[MaxThreads];

    /** Per-thread status in last cycle. */
    ThreadStatus lastStatus[MaxThreads];

  public:
    /** Predisq constructor. */
    Predisq(CPU *_cpu, const BaseRxuO3CPUParams &params);

    /** Initializes variables for the stage. */ 
    void startupStage();

    /** Clear all thread-specific states */
    void clearStates(ThreadID tid);

    /** Reset this pipeline stage */
    void resetStage();

    /** Returns the name of predisq. */
    std::string name() const;

    /** Sets the main backwards communication time buffer pointer. */
    void setTimeBuffer(TimeBuffer<TimeStruct> *tb_ptr);

    /** Sets pointer to time buffer used to communicate to the next stage. */
    void setPredisqQueue(TimeBuffer<PredisqStruct> *pq_ptr);

    /** Sets pointer to time buffer coming from decode. */
    void setDecodeQueue(TimeBuffer<DecodeStruct> *dq_ptr);

    void setUCQueue(TimeBuffer<DecodeStruct> *uq_ptr);

    /** Sets pointer to list of active threads. */
    void setActiveThreads(std::list<ThreadID> *at_ptr);

    /** Perform sanity checks after a drain. */
    void drainSanityCheck() const;

    /** Has the stage drained? */
    bool isDrained() const;

    /** Takes over from another CPU's thread. */
    void takeOverFrom() { resetStage(); }

    /** Ticks predisq
     */
    void tick();

    /** Determines what to do based on predisq's current status.
     * @param status_change predisq() sets this variable if there was a status
     * change (ie switching from blocking to running).
     * @param tid Thread id to predisq instructions from.
     */
    void predisq(bool &status_change, ThreadID tid);

    /** Insert instructions into predisqueue/predisqGrops.
     */
    void InsertInsts(ThreadID tid);

    /** Send instructions to dispipe0.
     */
    void SendInsts(ThreadID tid);

    /** Update predisqueue/predisqGrops free entries and notify decode.
     */
    void updateFreeEntries();

  private:

    /** Updates overall predisq status based on all of the threads' statuses. */
    void updateStatus();

    /** Separates instructions from decode into individual lists of instructions
     * sorted by thread.
     */
    void sortInsts();

    /** Reads all stall signals from the backwards communication timebuffer. */
    void readStallSignals(ThreadID tid);

    /** Checks all input signals and updates predisq's status appropriately. */
    bool checkSignalsAndUpdate(ThreadID tid);

    /** Checks all stall signals, and returns if any are true. */
    bool checkStall(ThreadID tid) const;

    /** Returns if there any instructions from decode on this cycle. */
    bool decodeInstsValid();

    /** Switches predisq to blocking, and signals back that predisq has
     * become blocked.
     * @return Returns true if there is a status change.
     */
    bool block(ThreadID tid);

    /** Switches predisq to running, and
     * signals back that predisq has unblocked.
     */
    void unblock(ThreadID tid);

  public:
    /** Squashes due to commit signalling a squash. Changes status to
     * squashing.
     */
    void squash(ThreadID tid);

  private:
    // Interfaces to objects outside of predisq.
    /** CPU interface. */
    CPU *cpu;

    /** Time buffer interface. */
    TimeBuffer<TimeStruct> *timeBuffer;

    /** Wire to get dispipe0's output from backwards time buffer. */
    TimeBuffer<TimeStruct>::wire fromDispipe0;

    /** Wire to get commit's information from backwards time buffer. */
    TimeBuffer<TimeStruct>::wire fromCommit;

    /** Wire to write information heading to previous stages. */
    TimeBuffer<TimeStruct>::wire toDecode;

    /** Predisq instruction queue. */
    TimeBuffer<PredisqStruct> *predisqQueue;

    /** Wire used to write any information heading to dispipe0. */
    TimeBuffer<PredisqStruct>::wire toDispipe0;

    /** Decode instruction queue interface. */
    TimeBuffer<DecodeStruct> *decodeQueue;

    TimeBuffer<DecodeStruct> *ucQueue;

public:
    /** Wire to get fetch's output from fetch queue. */
    TimeBuffer<DecodeStruct>::wire fromDecode;

private:
    /** Queue of all instructions coming from decode this cycle. */
    std::list<DynInstPtr> insts[MaxThreads];

    /** predisqueue */
    std::list<DynInstPtr> predisqueue;

    /** The width of predisq, in instructions. */
    unsigned predisqWidth;

    /** The size of the predisqueue queue in instructions */
    unsigned predisqueueSize;

    /** number of insts insert to predisqueue this cycle */
    int predisqueueInsertNums;

    /** Number of predisqueue groups */
    unsigned predisqGroupNums;

    struct csrFence
    {
        bool sent;

        DynInstPtr inst;
    };
    csrFence csrFenceInfo[MaxThreads];

public:
    /** Does predisq adopt compression mechanism */
    bool compressFlag;

    InstSeqNum pred_weak_youngest = 0;

    bool in_order = false;


private:
    struct GroupStruct
    {
        int size;

        DynInstPtr insts[groupSize];
    };

    /** predisqueue groups*/
    std::list<GroupStruct> predisqGroups;

    /** Number of predisqueue free entries*/
    unsigned freeEntries;

    /** Number of predisqueue free groups*/
    unsigned freeGroups;

    /** Variable that tracks if predisq has written to the time buffer this
     * cycle. Used to tell CPU if there is activity this cycle.
     */
    bool wroteToTimeBuffer;

    /** Source of possible stalls. */
    struct Stalls
    {
        bool dispipe0;
    };

    /** Tracks which stages are telling predisq to stall. */
    Stalls stalls[MaxThreads];

    /** dispipe0 to predisq delay. */
    Cycles dispipe0ToPredisqDelay;

    /** Commit to predisq delay. */
    Cycles commitToPredisqDelay;

    /** Decode to predisq delay. */
    Cycles decodeToPredisqDelay;

    /** Index of instructions being sent to dispipe0. */
    unsigned toDispipe0Index;

    /** number of Active Threads*/
    ThreadID numThreads;

    /** List of active thread ids */
    std::list<ThreadID> *activeThreads;

    struct PredisqStats : public statistics::Group
    {
        PredisqStats(CPU *cpu);

        /** Stat for total number of idle cycles. */
        statistics::Scalar idleCycles;
        /** Stat for total number of blocked cycles. */
        statistics::Scalar blockedCycles;
        /** Stat for total number of normal running cycles. */
        statistics::Scalar runCycles;
        /** Stat for total number of squashing cycles. */
        statistics::Scalar squashCycles;
        /** Stat for total number of waiting csr fence instruction cycles. */
        statistics::Scalar csrFenceCycles;
        /** Stat for total number of times that the predisqueue starts a stall. */
        statistics::Scalar predisqFullEvents;
        /** Stat for total number of predisqed instructions. */
        statistics::Scalar predisqedInsts;
        /** Stat for total number of squashed instructions. */
        statistics::Scalar squashedInsts;

        /** Stat for total times of instructions == 8. */
        statistics::Scalar Out8;
        statistics::Scalar Out8Stall;
        statistics::Scalar Out7;
        statistics::Scalar Out7Stall;
        statistics::Scalar Out6;
        statistics::Scalar Out6Stall;
        statistics::Scalar Out5;
        statistics::Scalar Out5Stall;
        statistics::Scalar Out4;
        statistics::Scalar Out4Stall;
        statistics::Scalar Out3;
        statistics::Scalar Out3Stall;
        statistics::Scalar Out2;
        statistics::Scalar Out2Stall;
        statistics::Scalar Out1;
        statistics::Scalar Out1Stall;
        /** Stat for total times of instructions == 0 in stall status. */
        /** statistics::Scalar Out0Stall; */
        /** Stat for total times of instructions == 0 not in stall status. */
        statistics::Scalar Out0NoStall;
        statistics::Scalar StallButZero;
        /** Stat for rate of instructions == 8. */
        statistics::Formula Out8Rate;
        statistics::Formula Out8StallRate;
        statistics::Formula Out7Rate;
        statistics::Formula Out7StallRate;
        statistics::Formula Out6Rate;
        statistics::Formula Out6StallRate;
        statistics::Formula Out5Rate;
        statistics::Formula Out5StallRate;
        statistics::Formula Out4Rate;
        statistics::Formula Out4StallRate;
        statistics::Formula Out3Rate;
        statistics::Formula Out3StallRate;
        statistics::Formula Out2Rate;
        statistics::Formula Out2StallRate;
        statistics::Formula Out1Rate;
        statistics::Formula Out1StallRate;
        /** Stat for rate of instructions == 0 in stall status. */
        /** statistics::Formula Out0StallRate; */
        /** Stat for rate of instructions == 0 not in stall status. */
        statistics::Formula Out0NoStallRate;
    } stats;
};

} // namespace rxuo3
} // namespace gem5

#endif // __CPU_RxuO3_PREDISQ_HH__
// ---------------------------------------------------------------