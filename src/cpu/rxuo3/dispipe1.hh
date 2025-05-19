// -- add by hongfei.liu --------------------------------------------------------
#ifndef __CPU_RxuO3_DISPIPE1_HH__
#define __CPU_RxuO3_DISPIPE1_HH__

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

class CPU;
class RMU;

class Dispipe1
{
  public:
    typedef std::deque<DynInstPtr> InstQueue;

  public:
    /** Overall dispipe1 status. Used to determine if the CPU can
     * deschedule itself due to a lack of activity.
     */
    enum Dispipe1Status
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
    /** Dispipe1 status. */
    Dispipe1Status _status;

    /** Per-thread status. */
    ThreadStatus dispipe1Status[MaxThreads];

  public:
    /** Dispipe1 constructor. */
    Dispipe1(CPU *_cpu, const BaseRxuO3CPUParams &params);

    /** Returns the name of dispipe1. */
    std::string name() const;

    /** Sets the main backwards communication time buffer pointer. */
    void setTimeBuffer(TimeBuffer<TimeStruct> *tb_ptr);

    /** Sets pointer to time buffer used to communicate to the next stage. */
    void setDispipe1Queue(TimeBuffer<Dispipe1Struct> *p1q_ptr);

    /** Sets pointer to time buffer coming from dispipe0. */
    void setDispipe0Queue(TimeBuffer<Dispipe0Struct> *p0q_ptr);

    /** Sets pointer to time buffer coming from dispipe2. */
    void setDispipe2Queue(TimeBuffer<Dispipe2Struct> *p2q_ptr);

  public:
    /** Sets pointer to the RMU. */
    void setRMU(RMU *rmu_ptr) { rmu = rmu_ptr; }

    /** Initializes variables for the stage. */
    void startupStage();

    /** Clear all thread-specific states. */
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

    /** Ticks dispipe1
     */
    void tick();

  private:
    /** Reset this pipeline stage */
    void resetStage();

    /** Determines what to do based on dispipe1's current status.
     * @param status_change dispipe1() sets this variable if there was a status
     * change (ie switching from blocking to unblocking).
     * @param tid Thread id to dispipe1 instructions from.
     */
    void dispipe1(bool &status_change, ThreadID tid);

    /** Process instructions.
     */
    void processInsts(ThreadID tid);

    void sortInsts();

    /** Updates overall dispipe1 status based on all of the threads' statuses. */
    void updateStatus();

    /** Switches dispipe1 to blocking, and signals back that dispipe1 has become
     * blocked.
     * @return Returns true if there is a status change.
     */
    bool block(ThreadID tid);

    /** Switches dispipe1 to unblocking, and signals back that dispipe1 has unblocked.
     * @return Returns true if there is a status change.
     */
    bool unblock(ThreadID tid);

    /** Returns the number of valid instructions coming from dispipe0. */
    unsigned validInsts();

    /** Reads signals telling dispipe1 to block/unblock. */
    void readStallSignals(ThreadID tid);

    /** Checks if any stages are telling dispipe1 to block. */
    bool checkStall(ThreadID tid);

    /** Checks the signals and updates the status. */
    bool checkSignalsAndUpdate(ThreadID tid);

public:
    /** Pointer to CPU. */
    CPU *cpu;

    /** RMU interface. */
    RMU *rmu;

    /** Pointer to main time buffer used for backwards communication. */
    TimeBuffer<TimeStruct> *timeBuffer;

    /** Wire to get dispipe2's output from backwards time buffer. */
    TimeBuffer<TimeStruct>::wire fromDispipe2;

    /** Wire to get commit's output from backwards time buffer. */
    TimeBuffer<TimeStruct>::wire fromCommit;

    /** Wire to write infromation heading to previous stages. */
    TimeBuffer<TimeStruct>::wire toDispipe0;

    /** Dispipe2 instruction queue. */
    TimeBuffer<Dispipe2Struct> *dispipe2Queue;

    /** Dispipe1 instruction queue. */
    TimeBuffer<Dispipe1Struct> *dispipe1Queue;

    /** Wire to write any information heading to Dispipe2. */
    TimeBuffer<Dispipe1Struct>::wire toDispipe2;

    /** Dispipe1 instruction queue interface. */
    TimeBuffer<Dispipe0Struct> *dispipe0Queue;

    /** Wire to get dispipe0's output from dispipe0 queue. */
    TimeBuffer<Dispipe0Struct>::wire fromDispipe0;

    /** Queue of all instructions coming from dispipe0 this cycle. */
    InstQueue insts[MaxThreads];

    /** Pointer to the list of active threads. */
    std::list<ThreadID> *activeThreads;

    /** Variable that tracks if dispipe1 has written to the time buffer this
     * cycle. Used to tell CPU if there is activity this cycle.
     */
    bool wroteToTimeBuffer;

    /** Source of possible stalls. */
    struct Stalls
    {
        bool dispipe2;
    };

    /** Tracks which stages are telling dispipe1 to stall. */
    Stalls stalls[MaxThreads];

    /** Delay between dispipe2 and dispipe1, in ticks. */
    int dispipe2ToDispipe1Delay;

    /** Delay between dispipe3 and dispipe1, in ticks. */
    int dispipe3ToDispipe1Delay;

    /** Delay between dispipe0 and dispipe1, in ticks. */
    int dispipe0ToDispipe1Delay;

    /** Delay between commit and dispipe1, in ticks. */
    unsigned commitToDispipe1Delay;

    /** Dispipe1 width, in instructions. */
    unsigned dispipe1Width;

    /** The index of the instruction in the time buffer to dispipe2 that dispipe1 is
     * currently using.
     */
    unsigned toDispipe2Index;

    /** Whether or not dispipe1 needs to block this cycle. */
    bool blockThisCycle;

    /** The number of threads active in dispipe1. */
    ThreadID numThreads;

    struct Dispipe1Stats : public statistics::Group
    {
        Dispipe1Stats(statistics::Group *parent);

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
        /** Stat for total number of dispipe1ed instructions. */
        statistics::Scalar dispipe1Insts;
        /** Stat for total number of squashed instructions that dispipe1
         * discards. */
        statistics::Scalar squashedInsts;

    } stats;
};

} // namespace rxuo3
} // namespace gem5

#endif // __CPU_RxuO3_DISPIPE1_HH__
// ------------------------------------------------------------------------------
