#ifndef __CPU_RxuO3_EW_HH__
#define __CPU_RxuO3_EW_HH__

#include <queue>
#include <set>
#include <map>

#include "base/statistics.hh"
#include "cpu/rxuo3/comm.hh"
#include "cpu/rxuo3/dyn_inst_ptr.hh"
#include "cpu/rxuo3/limits.hh"
#include "cpu/rxuo3/scoreboard.hh"
#include "cpu/timebuf.hh"
#include "sim/probe/probe.hh"
#include "cpu/inst_seq.hh"

namespace gem5
{

struct BaseRxuO3CPUParams;

namespace rxuo3
{

class RxuFUPool;
class Dispipe3;
class CPU;
class IBandLB;

class EW
{
  public:
      /** FU completion event class. */
    class FUCompletion : public Event
    {
      private:
        /** Executing instruction. */
        DynInstPtr inst;

        /** Index of the FU used for executing. */
        int fuIdx;

        /** Pointer back to the EW stage. */
        EW *ewPtr;

        /** Should the FU be added to the list to be freed upon
         * completing this event.
         */
        bool freeFU;

      public:
        /** Construct a FU completion event. */
        FUCompletion(const DynInstPtr &_inst, int fu_idx,
                     EW *ew_ptr);

        virtual void process();
        virtual const char *description() const;
        void setFreeFU() { freeFU = true; }
    };

      /** Load/Store DC1-DC2 event class. */
    class LoadStoreLatency : public Event
    {
      private:
        /** Load/Store instruction. */
        DynInstPtr inst;

        /** Pointer back to the EW stage. */
        EW *ewPtr;
      public:
        /** Construct a LoadStoreLatency event. */
        LoadStoreLatency(const DynInstPtr &_inst, EW *ew_ptr);

        virtual void process();
    };

      /** Load/Store early wake up event class. */
    class LdstEarlydone : public Event
    {
      private:
        /** Load/Store instruction. */
        DynInstPtr inst;

        /** Pointer back to the EW stage. */
        EW *ewPtr;
      public:
        /** Construct a LdstEarlydone event. */
        LdstEarlydone(const DynInstPtr &_inst, EW *ew_ptr);

        virtual void process();
    };

  public:
    /** Overall EW stage status. Used to determine if the CPU can
     * deschedule itself due to a lack of activity.
     */
    enum Status
    {
        Active,
        Inactive
    };

    /** Status for Execute, and Writeback stages. */
    enum ThreadStatus
    {
        Running,
        Blocked,
        Idle,
        Squashing,
        RobSquashing,
        Unblocking
    };

  private:
    /** Overall stage status. */
    Status _status;
    /** EW status. */
    ThreadStatus ewStatus[MaxThreads];

    /** Probe points. */
    ProbePointArg<DynInstPtr> *ppMispredict;
    /** To probe when instruction execution begins. */
    ProbePointArg<DynInstPtr> *ppExecute;
    /** To probe when instruction execution is complete. */
    ProbePointArg<DynInstPtr> *ppToCommit;

  public:
    /** Constructs a EW with the given parameters. */
    EW(CPU *_cpu, const BaseRxuO3CPUParams &params);

    /** Returns the name of the EW stage. */
    std::string name() const;

    /** Registers probes. */
    void regProbePoints();

    /** Dispipe3 pointer. */
    Dispipe3 *dispipe3Stage;

    /** Sets pointer to the Dispipe3. */
    void setDispipe3Satge(Dispipe3 *p3_ptr) { dispipe3Stage = p3_ptr; }

    /** Initializes stage. */
    void startupStage();

    /** Clear all thread-specific states */
    void clearStates(ThreadID tid);

    /** Sets main time buffer used for backwards communication. */
    void setTimeBuffer(TimeBuffer<TimeStruct> *tb_ptr);

    /** Sets time buffer for getting instructions coming from dispipe3. */
    void setWakeQueue(TimeBuffer<WakeStruct> *wq_ptr);

    /** Sets time buffer to pass on instructions to commit. */
    void setEWQueue(TimeBuffer<EWStruct> *eq_ptr);

    /** Sets pointer to list of active threads. */
    void setActiveThreads(std::list<ThreadID> *at_ptr);

    /** Sets pointer to the scoreboard. */
    void setScoreboard(Scoreboard *sb_ptr);

    /** Perform sanity checks after a drain. */
    void drainSanityCheck() const;

    /** Has the stage drained? */
    bool isDrained() const;

    /** Takes over from another CPU's thread. */
    void takeOverFrom();

    /** Squashes instructions in EW for a specific thread. */
    void squash(ThreadID tid);

    /** Sends an instruction to commit through the time buffer. */
    void instToCommit(const DynInstPtr &inst);

    /** Updates overall EW status based on all of the stages' statuses. */
    void updateStatus();

    /** Tells the CPU to wakeup if it has descheduled itself due to no
     * activity. Used mainly by the LdWritebackEvent.
     */
    void wakeCPU();

    /** Reports to the CPU that there is activity this cycle. */
    void activityThisCycle();

    /** Tells CPU that the EW stage is active and running. */
    void activateStage();

    /** Tells CPU that the EW stage is inactive and idle. */
    void deactivateStage();

    /** Check misprediction  */
    void checkMisprediction(const DynInstPtr &inst);

  private:
    /** Sends commit proper information for a squash due to a branch
     * mispredict.
     */
    void squashDueToBranch(const DynInstPtr &inst, ThreadID tid);

    /** Sends commit proper information for a squash due to a memory order
     * violation.
     */
    void squashDueToMemOrder(const DynInstPtr &inst, ThreadID tid);

    /** Sets EW to blocked, and signals back to other stages to block. */
    void block(ThreadID tid);

    /** Unblocks EW, and signals back to other stages to unblock.
     */
    void unblock(ThreadID tid);

    /** Process instructions. */
    void ew(ThreadID tid);

    /** FU latency. */
    void fuProcess(ThreadID tid);

    /** Executes instructions. In the case of memory operations, it informs the
     * LSQ to execute the instructions. Also handles any redirects that occur
     * due to the executed instructions.
     */
    void executeInsts();

    void WriteFrm(DynInstPtr inst, ThreadID tid);

    void WriteVxrm(DynInstPtr inst, ThreadID tid);

    void WriteVl(DynInstPtr inst, ThreadID tid);

    void WriteVtype(DynInstPtr inst, ThreadID tid);

    void WriteCsrVlVtype(DynInstPtr inst, ThreadID tid);

    /** Writebacks instructions. In our model, the instruction's execute()
     * function atomically reads registers, executes, and writes registers.
     * Thus this writeback only wakes up dependent instructions, and informs
     * the scoreboard of registers becoming ready.
     */
    void writebackInsts();

    /** Checks if any of the stall conditions are currently true. */
    bool checkStall(ThreadID tid);

    /** Processes inputs and changes state accordingly. */
    void checkSignalsAndUpdate(ThreadID tid);

    /** Sorts instructions coming from dispipe3 into lists separated by thread. */
    void sortInsts();

  public:
    /** Ticks EW stage.
     */
    void tick();
    /** Queue of all instructions coming from dispipe3 this cycle. */
    std::deque<DynInstPtr> insts[MaxThreads];

  private:
    /** Pointer to main time buffer used for backwards communication. */
    TimeBuffer<TimeStruct> *timeBuffer;

    /** Wire to write information heading to previous stages. */
    TimeBuffer<TimeStruct>::wire toFetch;

    /** Wire to get commit's output from backwards time buffer. */
    TimeBuffer<TimeStruct>::wire fromCommit;

    /** Wire to write information heading to previous stages. */
    TimeBuffer<TimeStruct>::wire toDispipe3;

    /** Dispipe3 instruction queue interface. */
    TimeBuffer<WakeStruct> *wakeQueue;

    /** Wire to get dispipe3's output from dispipe3 queue. */
    TimeBuffer<WakeStruct>::wire fromDispipe3;

    /**
     * EW stage time buffer.  Holds ROB indices of instructions that
     * can be marked as completed.
     */
    TimeBuffer<EWStruct> *ewQueue;

    /** Wire to write infromation heading to commit. */
    TimeBuffer<EWStruct>::wire toCommit;

    
  public:
    /** Queue of instructions that are ready to be executed. */
    std::deque<DynInstPtr> instsToExecute;

    /** Queue of instructions that are ready to be written back. */
    std::deque<DynInstPtr> instsToWB;

    /** Scoreboard pointer. */
    Scoreboard* scoreboard;

  private:
    /** CPU pointer. */
    CPU *cpu;

    /** Records if EW has written to the time buffer this cycle, so that the
     * CPU can deschedule itself if there is no activity.
     */
    bool wroteToTimeBuffer;

    /** Process FU completion event. */ 
    void processFUCompletion(const DynInstPtr &inst, int fu_idx);

    /** Process LoadStoreLatency event. */ 
    void processLoadStoreLatency(const DynInstPtr &inst);

    /** Process LdstEarlydone event. */ 
    void processLdstEarlydone(const DynInstPtr &inst);

  public:
    /** Pointer to the functional unit pool. */
    RxuFUPool *fuPool;
    /** Records if the LSQ needs to be updated on the next cycle, so that
     * EW knows if there will be activity on the next cycle.
     */
    bool updateLSQNextCycle;

    struct loopEntry 
    {
      Addr target_pc;
      int times;
    };

    std::map<Addr, loopEntry> loopTable;

  private:
    /** Records if there is a fetch redirect on this cycle for each thread. */
    bool fetchRedirect[MaxThreads];

    /** Commit to EW delay. */
    Cycles commitToEWDelay;

    /** Dispipe3 to EW delay. */
    Cycles dispipe3ToEWDelay;

    /** Index into queue of instructions being written back. */
    unsigned wbNumInst;

    /** Cycle number within the queue of instructions being written back.
     * Used in case there are too many instructions writing back at the current
     * cycle and writesbacks need to be scheduled for the future. See comments
     * in instToCommit().
     */
    unsigned wbCycle;

    /** Writeback width. */
    unsigned wbWidth;

    /** Number of active threads. */
    ThreadID numThreads;

    /** Pointer to list of active threads. */
    std::list<ThreadID> *activeThreads;

    /** Comparer */
    struct PqCompare
    {
        bool operator()(const DynInstPtr &lhs, const DynInstPtr &rhs) const;
    };

    /** Priority queue */
    typedef std::priority_queue<
        DynInstPtr, std::vector<DynInstPtr>, PqCompare> orderQueue;

    /** Instructions are sorted by SeqNum. */
    orderQueue OrderQueue;

    struct EWStats : public statistics::Group
    {
        EWStats(CPU *cpu);

        /** Stat for times of regIntRead == 0 per cycle. */
        statistics::Scalar regIntRead0;
        /** Stat for times of regIntRead == 1 per cycle. */
        statistics::Scalar regIntRead1;
        /** Stat for times of regIntRead == 2 per cycle. */
        statistics::Scalar regIntRead2;
        /** Stat for times of regIntRead == 3 per cycle. */
        statistics::Scalar regIntRead3;
        /** Stat for times of regIntRead == 4 per cycle. */
        statistics::Scalar regIntRead4;
        /** Stat for times of regIntRead == 5 per cycle. */
        statistics::Scalar regIntRead5;
        /** Stat for times of regIntRead == 6 per cycle. */
        statistics::Scalar regIntRead6;
        /** Stat for times of regIntRead == 7 per cycle. */
        statistics::Scalar regIntRead7;
        /** Stat for times of regIntRead == 8 per cycle. */
        statistics::Scalar regIntRead8;
        /** Stat for times of regIntRead == 9 per cycle. */
        statistics::Scalar regIntRead9;
        /** Stat for times of regIntRead == 10 per cycle. */
        statistics::Scalar regIntRead10;
        /** Stat for times of regIntRead == 11 per cycle. */
        statistics::Scalar regIntRead11;
        /** Stat for times of regIntRead == 12 per cycle. */
        statistics::Scalar regIntRead12;
        /** Stat for times of regIntRead == 13 per cycle. */
        statistics::Scalar regIntRead13;
        /** Stat for times of regIntRead == 14 per cycle. */
        statistics::Scalar regIntRead14;
        /** Stat for times of regIntRead == 15 per cycle. */
        statistics::Scalar regIntRead15;
        /** Stat for times of regIntRead == 16 per cycle. */
        statistics::Scalar regIntRead16;
        /** Stat for times of regIntRead > 16 per cycle. */
        statistics::Scalar regIntReadOver16;

        /** Stat for times of regFloatRead == 0 per cycle. */
        statistics::Scalar regFloatRead0;
        /** Stat for times of regFloatRead == 1 per cycle. */
        statistics::Scalar regFloatRead1;
        /** Stat for times of regFloatRead == 2 per cycle. */
        statistics::Scalar regFloatRead2;
        /** Stat for times of regFloatRead == 3 per cycle. */
        statistics::Scalar regFloatRead3;
        /** Stat for times of regFloatRead == 4 per cycle. */
        statistics::Scalar regFloatRead4;
        /** Stat for times of regFloatRead == 5 per cycle. */
        statistics::Scalar regFloatRead5;
        /** Stat for times of regFloatRead == 6 per cycle. */
        statistics::Scalar regFloatRead6;
        /** Stat for times of regFloatRead == 7 per cycle. */
        statistics::Scalar regFloatRead7;
        /** Stat for times of regFloatRead == 8 per cycle. */
        statistics::Scalar regFloatRead8;
        /** Stat for times of regFloatRead == 9 per cycle. */
        statistics::Scalar regFloatRead9;
        /** Stat for times of regFloatRead == 10 per cycle. */
        statistics::Scalar regFloatRead10;
        /** Stat for times of regFloatRead > 10 per cycle. */
        statistics::Scalar regFloatReadOver10;

        /** Stat for times of regIntWrite == 0 per cycle. */
        statistics::Scalar regIntWrite0;
        /** Stat for times of regIntWrite == 1 per cycle. */
        statistics::Scalar regIntWrite1;
        /** Stat for times of regIntWrite == 2 per cycle. */
        statistics::Scalar regIntWrite2;
        /** Stat for times of regIntWrite == 3 per cycle. */
        statistics::Scalar regIntWrite3;
        /** Stat for times of regIntWrite == 4 per cycle. */
        statistics::Scalar regIntWrite4;
        /** Stat for times of regIntWrite == 5 per cycle. */
        statistics::Scalar regIntWrite5;
        /** Stat for times of regIntWrite == 6 per cycle. */
        statistics::Scalar regIntWrite6;
        /** Stat for times of regIntWrite == 7 per cycle. */
        statistics::Scalar regIntWrite7;
        /** Stat for times of regIntWrite == 8 per cycle. */
        statistics::Scalar regIntWrite8;
        /** Stat for times of regIntWrite == 9 per cycle. */
        statistics::Scalar regIntWrite9;
        /** Stat for times of regIntWrite == 10 per cycle. */
        statistics::Scalar regIntWrite10;
        /** Stat for times of regIntWrite > 10 per cycle. */
        statistics::Scalar regIntWriteOver10;

        /** Stat for times of regFloatWrite == 0 per cycle. */
        statistics::Scalar regFloatWrite0;
        /** Stat for times of regFloatWrite == 1 per cycle. */
        statistics::Scalar regFloatWrite1;
        /** Stat for times of regFloatWrite == 2 per cycle. */
        statistics::Scalar regFloatWrite2;
        /** Stat for times of regFloatWrite == 3 per cycle. */
        statistics::Scalar regFloatWrite3;
        /** Stat for times of regFloatWrite == 4 per cycle. */
        statistics::Scalar regFloatWrite4;
        /** Stat for times of regFloatWrite == 5 per cycle. */
        statistics::Scalar regFloatWrite5;
        /** Stat for times of regFloatWrite == 6 per cycle. */
        statistics::Scalar regFloatWrite6;
        /** Stat for times of regFloatWrite == 7 per cycle. */
        statistics::Scalar regFloatWrite7;
        /** Stat for times of regFloatWrite == 8 per cycle. */
        statistics::Scalar regFloatWrite8;
        /** Stat for times of regFloatWrite == 9 per cycle. */
        statistics::Scalar regFloatWrite9;
        /** Stat for times of regFloatWrite == 10 per cycle. */
        statistics::Scalar regFloatWrite10;
        /** Stat for times of regFloatWrite > 10 per cycle. */
        statistics::Scalar regFloatWriteOver10;

        /** Stat for total number of idle cycles. */
        statistics::Scalar idleCycles;
        /** Stat for total number of squashing cycles. */
        statistics::Scalar squashCycles;
        /** Stat for total number of blocking cycles. */
        statistics::Scalar blockCycles;
        /** Stat for total number of unblocking cycles. */
        statistics::Scalar unblockCycles;
        /** Stat for total number of running cycles. */
        statistics::Scalar runCycles;
        /** Stat for total number of squashed instructions. */
        statistics::Scalar squashedInsts;
        /** Stat for total number of memory ordering violation events. */
        statistics::Scalar memOrderViolationEvents;
        /** Stat for total number of incorrect predicted taken branches. */
        statistics::Scalar predictedTakenIncorrect;
        /** Stat for total number of incorrect predicted not taken branches. */
        statistics::Scalar predictedNotTakenIncorrect;
        /** Stat for total number of mispredicted branches detected at
         *  execute. */
        statistics::Formula branchMispredicts;
        /** Number of instructions sent to commit. */
        statistics::Vector instsToCommit;
        /** Number of instructions that writeback. */
        statistics::Vector writebackCount;
        /** Number of instructions per cycle written back. */
        statistics::Formula wbRate;
    } stats;
};

} // namespace rxuo3
} // namespace gem5

#endif // __CPU_RxuO3_EW_HH__
