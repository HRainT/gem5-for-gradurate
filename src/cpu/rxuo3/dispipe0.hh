// -- add by hongfei.liu -------------------------------------------------------
#ifndef __CPU_RxuO3_DISPIPE0_HH__
#define __CPU_RxuO3_DISPIPE0_HH__

#include <list>
#include <queue>
#include <utility>
#include <vector>

#include "arch/riscv/decoder.hh"
#include "base/statistics.hh"
#include "cpu/rxuo3/comm.hh"
#include "cpu/rxuo3/dyn_inst_ptr.hh"
#include "cpu/rxuo3/limits.hh"
#include "cpu/timebuf.hh"
#include "sim/probe/probe.hh"
#include "cpu/rxuo3/commit.hh"
#include "cpu/rxuo3/dispipe3.hh"
#include "sim/eventq.hh"

namespace gem5
{

struct BaseRxuO3CPUParams;

namespace rxuo3
{

class RenameUnit;
class Rename;
class CPU;
class RMU;
class Dispipe3;


class Dispipe0
{
  public:

    typedef std::deque<DynInstPtr> InstQueue;

  public:
    /** Overall dispipe0 status. Used to determine if the CPU can
     * deschedule itself due to a lack of activity.
     */
    enum Dispipe0Status
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
    /** Dispipe0 status. */
    Dispipe0Status _status;

    /** Per-thread status. */
    ThreadStatus dispipe0Status[MaxThreads];

  public:

    /** Dispipe0 constructor. */
    Dispipe0(CPU *_cpu, const BaseRxuO3CPUParams &params);

    // -- vectorQueue funtion ------------------------------------
    class vectorQueueEvent : public Event
    {
      private:
        /** Pointer back to the WTB. */
        Dispipe0 *dispipe0Ptr;

        InstQueue buffer;

      public:
        /** Event id use to set delayReady*/
        uint64_t id;

        /** Construct an intDivCompletion event. */
        vectorQueueEvent(Dispipe0 *dispipe0Ptr, InstQueue insts);

        virtual void process();
    };

    uint64_t VQEventId = 0;

    int maxUopNum = 4;

    class VecQueue {
      private:
        /** Rename interface. */
        Dispipe0 *dispipe0Ptr; 

        /** Vector Queue */
        std::deque<InstQueue> queue;

        /** Temporarily store uops splited from macroop in VectorQueue */
        InstQueue uopBuffer;

        /** Max FiFo depth */
        int maxDepth;

        /** Max FiFo num, predisq's width is 8, so this argument dont care*/
        int maxFiFoNum;

      public:
        /** VecQueue Contructor */
        VecQueue(Dispipe0 *dispipe0_Ptr, int maxDepth, int maxFiFoNum);

        /** Check VectorQueue full */
        bool isFull();

        /** Check if macroop can split.(1.delayed   2.vl vtype register ready) */
        bool macroopCanSplit(DynInstPtr inst);

        /** Call isFll() to check before call this function */
        void addMacroop(std::deque<DynInstPtr> entry);

        /** Fetch uop from uopBuffer */
        DynInstPtr fetchUop();

        /** Guarantee queue.front() is not empty before call this function */
        void checkAndSplit();

        /** Try to pop queue's front */
        void tryPopFront();

        /** Squash due to commit unit */
        void squash(const InstSeqNum &doneSeqNum, ThreadID tid);

        std::string
        name() const
        {
            return dispipe0Ptr->name();
        }
    };

    VecQueue *vecQueue;

    /** Max number of uop that uopBuffer can send */
    int maxUopSend = 4;

    void updateFreeEntries();

   /**buffer for veccsr rename stall */    
    std::list<DynInstPtr> vecRenameStallBuffer;
    // -----------------------------------------------------------

    /** The decoder. */
    RiscvISA::Decoder *decoder[MaxThreads];

    /** Help Redecode vector */
    StaticInstPtr vecMacroop = NULL;
    StaticInstPtr vecMicroop = NULL;

    /** Redecode vector inst */
    void decodeVecInst(DynInstPtr &inst, InstQueue &uopBuffer);

    DynInstPtr buildInst(ThreadID tid, StaticInstPtr staticInst,
          StaticInstPtr curMacroop, const PCStateBase &this_pc,
          const PCStateBase &next_pc, bool trace, InstSeqNum sn);

    // /cnt use to select ldst/
    int ldst_cnt = 6;

    int vector_cnt = 22;

    /** Returns the name of dispipe0. */
    std::string name() const;

    /** Sets the main backwards communication time buffer pointer. */
    void setTimeBuffer(TimeBuffer<TimeStruct> *tb_ptr);

    /** Sets pointer to time buffer used to communicate to the next stage. */
    void setDispipe0ToRobQueue(TimeBuffer<Dispipe0ToRobStruct> *p0q_ptr);

    /** Sets pointer to time buffer used to communicate to the next stage. */
    void setDispipe0Queue(TimeBuffer<Dispipe0Struct> *p0q_ptr);

    /** Sets pointer to time buffer coming from predisq. */
    void setPredisqQueue(TimeBuffer<PredisqStruct> *pq_ptr);

    /** Sets pointer to commit stage. Used only for initialization. */
    void
    setCommitStage(Commit *commit_stage)
    {
        commit_ptr = commit_stage;
    }

  private:
    /** Pointer to commit stage. Used only for initialization. */
    Commit *commit_ptr;

  public:
    /** Sets pointer to the RMU. */
    void setRMU(RMU *rmu_ptr) { rmu = rmu_ptr; }

    void setDispipe3(Dispipe3 *Dispipe3_ptr) { dispipe3 = Dispipe3_ptr; }

    /** Sets pointer to the Rename. */
    void setRename(RenameUnit *rename_ptr) { rename = rename_ptr; }

    void setO3Rename(Rename *rename_ptr) { o3rename = rename_ptr; }

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

    /** Ticks dispipe0
     */
    void tick();

  private:
    /** Reset this pipeline stage */
    void resetStage();

    /** Determines what to do based on dispipe0's current status.
     * @param status_change dispipe0() sets this variable if there was a status
     * change (ie switching from blocking to unblocking).
     * @param tid Thread id to dispipe0 instructions from.
     */
    void dispipe0(bool &status_change, ThreadID tid);

    /** process Insts.
     */
    void processInsts(ThreadID tid);

    /** process Vector Uops.
     */
    void processUops(ThreadID tid);

    /** Separates instructions from predisq into individual lists of instructions
     * sorted by thread.
     */
    void sortInsts();

    /** Updates overall dispipe0 status based on all of the threads' statuses. */
    void updateStatus();

    /** Switches dispipe0 to blocking, and signals back that dispipe0 has become
     * blocked.
     * @return Returns true if there is a status change.
     */
    bool block(ThreadID tid);

    /** Switches dispipe0 to unblocking, and signals
     * back that rename has unblocked.
     * @return Returns true if there is a status change.
     */
    bool unblock(ThreadID tid);

    /** Executes actual squash, removing squashed instructions. */
    void doSquash(const InstSeqNum &squash_seq_num, ThreadID tid);

    /** Calculates the number of free ROB entries for a specific thread. */
    int calcFreeROBEntries(ThreadID tid);

    /** Calculates the number of free vector ROB entries for a specific thread. */
    int calcFreeVecROBEntries(ThreadID tid);    

    /** Returns the number of valid instructions coming from predisq. */
    unsigned validInsts();

    /** Reads signals telling dispipe0 to block/unblock. */
    void readStallSignals(ThreadID tid);

    /** Checks if any stages are telling dispipe0 to block. */
    std::vector<bool> checkStall(ThreadID tid);

    /** Gets the number of free entries for a specific thread. */
    void readFreeEntries(ThreadID tid);

    /** Checks the signals and updates the status. */
    bool checkSignalsAndUpdate(ThreadID tid);

    /** set iid and ibufferid of instructions. */
    void setIidAndIbufferid(const DynInstPtr &inst);

public:
    /** Pointer to CPU. */
    CPU *cpu;

    /** RMU interface. */
    RMU *rmu;
 

    Dispipe3 *dispipe3;

    /** Rename interface. */
    RenameUnit *rename;

    Rename *o3rename;

    /** Pointer to main time buffer used for backwards communication. */
    TimeBuffer<TimeStruct> *timeBuffer;

    /** Wire to get Dispipe1's output from backwards time buffer. */
    TimeBuffer<TimeStruct>::wire fromDispipe1;

    /** Wire to get Commit's output from backwards time buffer. */
    TimeBuffer<TimeStruct>::wire fromCommit;

    /** Wire to write infromation heading to previous stages. */
    TimeBuffer<TimeStruct>::wire toPredisq;

     /** Dispipe0 instruction queue. */
    TimeBuffer<Dispipe0ToRobStruct> *dispipe0ToRobQueue;

    /** Wire to write any information heading to Dispipe1. */
    TimeBuffer<Dispipe0ToRobStruct>::wire toRob;

    /** Dispipe0 instruction queue. */
    TimeBuffer<Dispipe0Struct> *dispipe0Queue;

    /** Wire to write any information heading to Dispipe1. */
    TimeBuffer<Dispipe0Struct>::wire toDispipe1;

    /** Predisq instruction queue interface. */
    TimeBuffer<PredisqStruct> *predisqQueue;

    /** Wire to get predisq's output from predisq queue. */
    TimeBuffer<PredisqStruct>::wire fromPredisq;

    /** Queue of all instructions coming from predisq this cycle. */
    InstQueue insts[MaxThreads];

    /** Queue of all uops coming from Vector Queue. */
    InstQueue uops[MaxThreads];

    /** Pointer to the list of active threads. */
    std::list<ThreadID> *activeThreads;

    /** Count of instructions in progress that have been sent off to the ROB,
     *  but are not yet included in its occupancy counts.
     */
    int instsInProgress[MaxThreads];

    /** Count of vector instructions in progress that have been sent off to the ROB,
     *  but are not yet included in its occupancy counts.
     */
    int vecInstsInProgress[MaxThreads];

    /** Records number of instructions that dispipe0 sends to ROB in last cycle.
    */
    int lastSendCount[MaxThreads];

    /** Records number of vector instructions that dispipe0 sends to ROB in last cycle.
    */
    int lastSendVecCount[MaxThreads];    

    /** Records number of instructions that dispipe0 sends to ROB in last last cycle.
    */
    int lastLastSendCount[MaxThreads];

    /** Records number of instructions that dispipe0 sends to ROB in last last cycle.
    */
    int lastLastSendVecCount[MaxThreads];

    /** Variable that tracks if dispipe0 has written to the time buffer this
     * cycle. Used to tell CPU if there is activity this cycle.
     */
    bool wroteToTimeBuffer;

    struct FreeEntries
    {
        unsigned robEntries;
        unsigned vecRobEntries;
    };

    /** Per-thread tracking of the number of free entries of back-end
     * structures.
     */
    FreeEntries freeEntries[MaxThreads];


    /** Records if the ROB is empty. In SMT mode the ROB may be dynamically
     * partitioned between threads, so the ROB must tell dispipe0 when it is
     * empty.
     */
    bool emptyROB[MaxThreads];

    /** Source of possible stalls. */
    struct Stalls
    {
        bool dispipe1Vec;
        bool dispipe1Scalar;
    };

    /** Tracks which stages are telling predisq to stall. */
    Stalls stalls[MaxThreads];

    /** Delay between dispipe1 and dispipe0, in ticks. */
    int dispipe1ToDispipe0Delay;

    /** Delay between predisq and dispipe0, in ticks. */
    int predisqToDispipe0Delay;

    /** Delay between commit and dispipe0, in ticks. */
    unsigned commitToDispipe0Delay;

    /** Dispipe0 width, in instructions. */
    unsigned dispipe0Width;

    /** The index of the instruction in the time buffer to Dispipe1 that dispipe0 is
     * currently using.
     */
    unsigned toDispipe1Index;

    unsigned toRobIndex;

    /** Whether or not dispipe0 needs to block this cycle. */
    bool blockThisCycle;

    /** Whether or not dispipe0 needs to resume unblocking
     * after squashing. */
    bool resumeUnblocking;

    /** The number of threads active in dispipe0. */
    ThreadID numThreads;

    /** index used by rename. */
    unsigned rename_idx;

    struct Rotate
    {
        bool int_normal;
        bool int_special;
        bool fp_normal;
        bool fp_special;
        bool load_store_odd;
        bool load_store_even;
    };

    Rotate rotate_flags[MaxThreads];

    bool selectIbuffer8 = true;
    bool selectIbuffer10 = true;

    bool selectIbuffer11 = true;

    bool selectIbuffer4 = true;
    bool selectIbuffer0 = true;

    int brIn8 = 0;

    bool noDestOdd = true;

    struct Dispipe0Stats : public statistics::Group
    {
        Dispipe0Stats(statistics::Group *parent);

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
        /** Stat for total number of dispipe0ed instructions. */
        statistics::Scalar dispipe0Insts;
        /** Stat for total number of squashed instructions that dispipe0
         * discards. */
        statistics::Scalar squashedInsts;
        /** Stat for total number of times that the ROB starts a stall in
         * dispipe0. */
        statistics::Scalar ROBFullEvents;
        /** Stat for total number of times that the vector ROB starts a stall in
         * dispipe0. */
        statistics::Scalar vecROBFullEvents;
        /** Stat for total number of times that the dispipe0 starts a stall due to
         * rename(Blocking due to lack of vector free physical registers to rename). */
        statistics::Scalar renameVecStallLackRegs;
        /** Stat for total number of times that the dispipe0 starts a stall due to
         * rename(Blocking because vector recover is doing). */
        statistics::Scalar renameVecStallRecover;
        /** Stat for total number of times that the dispipe0 starts a stall due to
         * rename(Blocking because vector mapping table size > 56). */
        statistics::Scalar renameVecStallSizeOver56;
        /** Stat for total number of times that the dispipe0 starts a stall due to
         * rename(Blocking due to lack of free scalar physical registers to rename). */
        statistics::Scalar renameScalarStallLackRegs;
        /** Stat for total number of times that the dispipe0 starts a stall due to
         * rename(Blocking because scalar recover is doing). */
        statistics::Scalar renameScalarStallRecover;
        /** Stat for total number of times that the dispipe0 starts a stall due to
         * rename(Blocking because scalar mapping table size > 56). */
        statistics::Scalar renameScalarStallSizeOver56;
        /** Stat for total number of times that the dispipe0 starts a stall due to
         * vector csr rename lack stall. */
        statistics::Scalar vecCsrRenameLackStall;
        /** Stat for total number of times that the dispipe0 starts a stall due to
         * vector csr rename recover stall. */
        statistics::Scalar vecCsrRenameRecStall;
      
        /** Number of odd dest reg instructions. */
        statistics::Scalar DestOdd;
        /** Number of even dest reg instructions. */
        statistics::Scalar DestEven;
        /** Number of odd dest reg int normal instructions. */
        statistics::Scalar DestOddIntNormal;
        /** Number of even dest reg int normal instructions. */
        statistics::Scalar DestEvenIntNormal;
        /** Number of odd dest reg int special instructions. */
        statistics::Scalar DestOddIntSpecial;
        /** Number of even dest reg int special instructions. */
        statistics::Scalar DestEvenIntSpecial;
        /** Number of odd dest reg fp normal instructions. */
        statistics::Scalar DestOddFpNormal;
        /** Number of even dest reg fp normal instructions. */
        statistics::Scalar DestEvenFpNormal;
        /** Number of odd src0 reg load/store instructions. */
        statistics::Scalar Src0OddLdst;
        /** Number of even src0 reg load/store instructions. */
        statistics::Scalar Src0EvenLdst;
        /** Number of odd src1 reg load/store instructions. */
        statistics::Scalar Src1OddLdst;
        /** Number of even src1 reg load/store instructions. */
        statistics::Scalar Src1EvenLdst;

        statistics::Scalar ibuffer6Insts;
        statistics::Scalar ibuffer7Insts;
        statistics::Scalar ibuffer14Insts;
        statistics::Scalar ibuffer15Insts;
        statistics::Scalar ibuffer16Insts;
        statistics::Scalar ibuffer17Insts;
        statistics::Scalar ibuffer18Insts;
        statistics::Scalar ibuffer19Insts;
        statistics::Scalar ibuffer20Insts;
        statistics::Scalar ibuffer21Insts;
        statistics::Scalar ibuffer22Insts;
        statistics::Scalar ibuffer23Insts;

        statistics::Scalar noDestControlNum;
        statistics::Scalar ControlNum;
    } stats;

    struct VectorQueueStats : public statistics::Group
      {
          VectorQueueStats(CPU *cpu);

          statistics::Distribution circle_invectorQ;
          statistics::Scalar number_instIssue;
          statistics::Formula number_instIssueOneCircle;
          
          /** Number of vset instructions to dispipe1. */
          statistics::Distribution VsetToDispipe1;
      } stats_vector;
};

} // namespace rxuo3
} // namespace gem5

#endif // __CPU_RxuO3_DISPIPE0_HH__
// ---------------------------------------------------------------------------------------
