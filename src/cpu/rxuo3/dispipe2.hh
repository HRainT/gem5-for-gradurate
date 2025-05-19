// -- add by hongfei.liu -------------------------------------------------------
#ifndef __CPU_RxuO3_DISPIPE2_HH__
#define __CPU_RxuO3_DISPIPE2_HH__

#include <list>
#include <utility>
#include <queue>

#include "base/statistics.hh"
#include "cpu/rxuo3/comm.hh"
#include "cpu/rxuo3/dyn_inst_ptr.hh"
#include "cpu/rxuo3/limits.hh"
#include "cpu/timebuf.hh"

namespace gem5
{

struct BaseRxuO3CPUParams;

namespace rxuo3
{

class RMU;
class CPU;

class Dispipe2
{
  public:

    typedef std::deque<DynInstPtr> InstQueue;

  public:
    /** Overall dispipe2 status. Used to determine if the CPU can
     * deschedule itself due to a lack of activity.
     */
    enum Dispipe2Status
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
        Unblocking,
    };

  private:
    /** Dispipe2 status. */
    Dispipe2Status _status;

    /** Per-thread status. */
    ThreadStatus dispipe2Status[MaxThreads];

  public:
    /** Dispipe2 constructor. */
    Dispipe2(CPU *_cpu, const BaseRxuO3CPUParams &params);

    /** Returns the name of dispipe2. */
    std::string name() const;

    /** Sets the main backwards communication time buffer pointer. */
    void setTimeBuffer(TimeBuffer<TimeStruct> *tb_ptr);

    /** Sets pointer to time buffer used to communicate to the next stage. */
    void setDispipe2Queue(TimeBuffer<Dispipe2Struct> *p2q_ptr);

    /** Sets pointer to time buffer coming from dispipe1. */
    void setDispipe1Queue(TimeBuffer<Dispipe1Struct> *p1q_ptr);

  public:
    /** Sets pointer to the RMU. */
    void setRMU(RMU *rmu_ptr) { rmu = rmu_ptr; }

    /** Initializes variables for the stage. */
    void startupStage();

    /** Clear all thread-specific states */
    void clearStates(ThreadID tid);

    /** Sets pointer to list of active threads. */
    void setActiveThreads(std::list<ThreadID> *at_ptr);

    /** Perform sanity checks after a drain. */
    void drainSanityCheck() const;

    /** Has the stage drained? */
    bool isDrained() const;

    /** Takes over from another CPU's thread. */
    void takeOverFrom();

    /** Squashes all instructions in a thread. */
    void squash(const InstSeqNum &squash_seq_num, ThreadID tid);

    /** Ticks dispipe2
     */
    void tick();

  private:
    /** Reset this pipeline stage */
    void resetStage();

    /** Determines what to do based on dispipe2's current status.
     * @param status_change dispipe2() sets this variable if there was a status
     * change (ie switching from blocking to unblocking).
     * @param tid Thread id to dispipe2 instructions from.
     */
    void dispipe2(bool &status_change, ThreadID tid);

    /** Processes instructions from in unblocking and running and idle status.
     */
    void insertAndSendInsts(ThreadID tid);

    /** Insert instructions into disqueue in blocked status.
     */
    void blockedInsert(ThreadID tid);

    /** Process instructions.
     */
    void processInsts(ThreadID tid);

    /** Check if any disq is full.
     */
    std::vector<bool> checkDisqsFull();

    /** Check if any disq instructions is more than disqueuesize+out.
     */
    std::vector<bool> checkDisqsMoreOut();

    /** Separates instructions from dispipe1 into individual lists of instructions
     * sorted by thread.
     */
    void sortInsts();

    /** Insert a instruction into a disq if it has the most free entries. */
    void disqsInsert(DynInstPtr inst);

    /** Updates overall dispipe2 status based on all of the threads' statuses. */
    void updateStatus();

    /** Switches dispipe2 to blocking, and signals back that dispipe2 has become
     * blocked.
     * @return Returns true if there is a status change.
     */
    bool block(ThreadID tid);

    /** Switches dispipe2 to unblocking, and signals back that dispipe2 has unblocked.
     * @return Returns true if there is a status change.
     */
    bool unblock(ThreadID tid);

    /** Returns the number of valid instructions coming from dispipe2. */
    unsigned validInsts();

    /** Reads signals telling dispipe2 to block/unblock. */
    void readStallSignals(ThreadID tid);

    void setDisqsLimits();

    /** Checks if any stages are telling dispipe2 to block. */
    bool checkStall(ThreadID tid);

    /** Gets the number of wtb free entries. */
    void readFreeEntries();

    /** Checks the signals and updates the status. */
    bool checkSignalsAndUpdate(ThreadID tid);

    void statDisqueueFull(int ibuffer_id);

private:
    /** Pointer to CPU. */
    CPU *cpu;

    /** RMU interface. */
    RMU *rmu;

    /** Pointer to main time buffer used for backwards communication. */
    TimeBuffer<TimeStruct> *timeBuffer;

    /** Wire to get Dispipe3's output from backwards time buffer. */
    TimeBuffer<TimeStruct>::wire fromDispipe3;

    /** Wire to get commit's output from backwards time buffer. */
    TimeBuffer<TimeStruct>::wire fromCommit;

    /** Wire to write infromation heading to previous stages. */
    TimeBuffer<TimeStruct>::wire toDispipe1;

    /** Dispipe2 instruction queue. */
    TimeBuffer<Dispipe2Struct> *dispipe2Queue;

    /** Wire to write any information heading to Dispipe3. */
    TimeBuffer<Dispipe2Struct>::wire toDispipe3;

    /** Dispipe1 instruction queue interface. */
    TimeBuffer<Dispipe1Struct> *dispipe1Queue;

    /** Wire to get dispipe1's output from dispipe1 queue. */
    TimeBuffer<Dispipe1Struct>::wire fromDispipe1;

    /** Queue of all instructions coming from dispipe1 this cycle. */
    InstQueue insts[MaxThreads];

    /** Disqs */
    // Index corresponding to ibuffer
    // i0  i1  i2  i3  i4  i6  ls7  ls8  f0  f1  f2  f3  f4  f5  ls3  ls4  ls5  ls6  ls7  ls8
    // 0   1   2   3   4   5   6    7    8   9   10  11  12  13  14   15   16   17   18   19 
    InstQueue disqs[24];

   // InstQueue stdb[2];

    /** Comparer */
    struct PqCompare
    {
        bool operator()(const DynInstPtr &lhs, const DynInstPtr &rhs) const;
    };

    /** Priority queue */
    typedef std::priority_queue<
        DynInstPtr, std::vector<DynInstPtr>, PqCompare> ReadyInstQueue;

    /** Instructions are sorted by SeqNum. */
    ReadyInstQueue outSortQueue;

    /** Limits of disqs push and pop. */
    struct DisqsLimits {
      int in[24];
      int out[24];
    } disqsLimits;

    /** Pointer to the list of active threads. */
    std::list<ThreadID> *activeThreads;

    /** Variable that tracks if dispipe2 has written to the time buffer this
     * cycle. Used to tell CPU if there is activity this cycle.
     */
    bool wroteToTimeBuffer;

    /** Ibuffer free entries. */
    int wtbFreeEntries[24];

    /** Instructions that dispipe1 sends to wtb last cycle. */
    int lastSendWtbEntries[24];

    //std buffer number
    int storedatabuffer[2];

    /** Source of possible stalls. */
    struct Stalls
    {
        bool dispipe3;
    };

    /** Tracks which stages are telling dispipe2 to stall. */
    Stalls stalls[MaxThreads];

    /** Delay between dispipe3 and dispipe2, in ticks. */
    int dispipe3ToDispipe2Delay;

    /** Delay between dispipe1 and dispipe2, in ticks. */
    int dispipe1ToDispipe2Delay;

    /** Delay between commit and dispipe2, in ticks. */
    unsigned commitToDispipe2Delay;

    /** Dispipe2 width, in instructions. */
    unsigned dispipe2Width;

    /** The size of the disqueue in instructions. */
    unsigned disqueueSize;

    /** The size of the branch disqueue in instructions. */
    unsigned disqueueSizeBranch;

    /** The size of the int special disqueue in instructions. */
    unsigned disqueueSizeIntSpecial;

    /** The index of the instruction in the time buffer to dispipe3 that dispipe2 is
     * currently using.
     */
    unsigned toDispipe3Index;

    /** Whether or not dispipe2 needs to block this cycle. */
    bool blockThisCycle;

    /** The number of threads active in dispipe2. */
    ThreadID numThreads;

    // InstQueue disqs[MaxThreads][16];

    struct Dispipe2Stats : public statistics::Group
    {
        Dispipe2Stats(statistics::Group *parent);

        /** Stat for total number of cycles spent squashing. */
        statistics::Scalar squashCycles;
        /** Stat for total number of cycles spent idle. */
        statistics::Scalar idleCycles;
        /** Stat for total number of cycles spent blocking. */
        statistics::Scalar blockCycles;
        /** Stat for total number of cycles spent running normally. */
        statistics::Scalar runCycles;
        /** Stat for total number of cycles spent unblocking. */
        statistics::Scalar unblockCycles;
        /** Stat for total number of dispipe2ed instructions. */
        statistics::Scalar dispipe2Insts;
        /** Stat for total number of squashed instructions that dispipe2
         * discards. */
        statistics::Scalar squashedInsts;
        /** Stat for total number of times that the Disqs starts a stall in
         * dispipe1. */
        statistics::Scalar DisqFullEvents;

        /** Stat for number of times the disqueue0 becomes full. */
        statistics::Scalar disqueue0FullEvents;
        /** Stat for number of times the disqueue1 becomes full. */
        statistics::Scalar disqueue1FullEvents;
        /** Stat for number of times the disqueue4 becomes full. */
        statistics::Scalar disqueue4FullEvents;
        /** Stat for number of times the disqueue5 becomes full. */
        statistics::Scalar disqueue5FullEvents;

        /** Stat for number of times the disqueue2 becomes full. */
        statistics::Scalar disqueue2FullEvents;
        /** Stat for number of times the disqueue3 becomes full. */
        statistics::Scalar disqueue3FullEvents;

        /** Stat for number of times the disqueue6 becomes full. */
        statistics::Scalar disqueue6FullEvents;
        /** Stat for number of times the disqueue7 becomes full. */
        statistics::Scalar disqueue7FullEvents;
        /** Stat for number of times the disqueue14 becomes full. */
        statistics::Scalar disqueue14FullEvents;
        /** Stat for number of times the disqueue15 becomes full. */
        statistics::Scalar disqueue15FullEvents;
        /** Stat for number of times the disqueue16 becomes full. */
        statistics::Scalar disqueue16FullEvents;
        /** Stat for number of times the disqueue17 becomes full. */
        statistics::Scalar disqueue17FullEvents;
        /** Stat for number of times the disqueue18 becomes full. */
        statistics::Scalar disqueue18FullEvents;
        /** Stat for number of times the disqueue19 becomes full. */
        statistics::Scalar disqueue19FullEvents;

        /** Stat for number of times the disqueue8 becomes full. */
        statistics::Scalar disqueue8FullEvents;
        /** Stat for number of times the disqueue9 becomes full. */
        statistics::Scalar disqueue9FullEvents;
        /** Stat for number of times the disqueue10 becomes full. */
        statistics::Scalar disqueue10FullEvents;
        /** Stat for number of times the disqueue13 becomes full. */
        statistics::Scalar disqueue13FullEvents;

        /** Stat for number of times the disqueue11 becomes full. */
        statistics::Scalar disqueue11FullEvents;
        /** Stat for number of times the disqueue12 becomes full. */
        statistics::Scalar disqueue12FullEvents;

        /** Stat for number of times the disqueue20 becomes full. */
        statistics::Scalar disqueue20FullEvents;
        /** Stat for number of times the disqueue21 becomes full. */
        statistics::Scalar disqueue21FullEvents;
        /** Stat for number of times the disqueue22 becomes full. */
        statistics::Scalar disqueue22FullEvents;
        /** Stat for number of times the disqueue23 becomes full. */
        statistics::Scalar disqueue23FullEvents;
    } stats;
};

} // namespace rxuo3
} // namespace gem5

#endif // __CPU_RxuO3_DISPIPE2_HH__
// ---------------------------------------------------------------------------------------
