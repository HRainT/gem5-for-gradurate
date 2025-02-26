// -- add by hongfei.liu ---------------------------------------------
#ifndef __CPU_RxuO3_DISPIPE3_HH__
#define __CPU_RxuO3_DISPIPE3_HH__

#include <queue>
#include <vector>
#include "base/statistics.hh"
#include "cpu/rxuo3/comm.hh"
#include "cpu/rxuo3/dyn_inst_ptr.hh"
#include "cpu/rxuo3/wtb.hh"
#include "cpu/rxuo3/limits.hh"
#include "cpu/rxuo3/lsq.hh"
#include "cpu/timebuf.hh"
#include "debug/RxuDispipe3.hh"
#include "sim/probe/probe.hh"
#include "arch/riscv/decoder.hh"
namespace gem5
{

struct BaseRxuO3CPUParams;

namespace rxuo3
{

class RMU;

class Dispipe3
{
  public:
    /** Overall dispipe3 stage status. Used to determine if the CPU can
     * deschedule itself due to a lack of activity.
     */
    typedef std::vector<DynInstPtr> InstQueue;
    enum Dispipe3Status
    {
        Active,
        Inactive
    };

    /** Individual thread status. */
    enum ThreadStatus
    {
        Running,
        Blocked,
        Idle,
        Squashing,
        Unblocking
    };

  private:
    /** Dispipe3 status. */
    Dispipe3Status _status;
    /** Per-thread status. */
    ThreadStatus dispipe3Status[MaxThreads];

    /** Probe points. */
    ProbePointArg<DynInstPtr> *ppDispatch;

  public:
    /** Constructs a Dispipe3 with the given parameters. */
    Dispipe3(CPU *_cpu, const BaseRxuO3CPUParams &params);

    /** Returns the name of the Dispipe3 stage. */
    std::string name() const;

    // //replay the src1 not ready store
    // void replayStoreSrc1(ThreadID tid);

    /** Registers probes. */
    void regProbePoints();

    /** Initializes stage. */
    void startupStage();

    /** Clear all thread-specific states */
    void clearStates(ThreadID tid);

    /** Sets main time buffer used for backwards communication. */
    void setTimeBuffer(TimeBuffer<TimeStruct> *tb_ptr);

    /** Sets time buffer for getting instructions coming from dispipe2. */
    void setDispipe2Queue(TimeBuffer<Dispipe2Struct> *p2q_ptr);

    /** Sets pointer to time buffer used to communicate to the next stage. */
    void setWakeQueue(TimeBuffer<WakeStruct> *wq_ptr);

    /** Sets pointer to list of active threads. */
    void setActiveThreads(std::list<ThreadID> *at_ptr);

    /** Perform sanity checks after a drain. */
    void drainSanityCheck() const;

    /** Has the stage drained? */
    bool isDrained() const;

    /** Takes over from another CPU's thread. */
    void takeOverFrom();

    /** Squashes instructions in dispipe3 for a specific thread. */
    void squash(ThreadID tid);

    /** Tells memory dependence unit that a memory instruction needs to be
     * rescheduled. It will re-execute once replayMemInst() is called.
     */
    void rescheduleMemInst(const DynInstPtr &inst);

    /** Re-executes all rescheduled memory instructions. */
    void replayMemInst(const DynInstPtr &inst);

    /** Moves memory instruction onto the list of cache blocked instructions */
    void blockMemInst(const DynInstPtr &inst);

    /** Notifies that the cache has become unblocked */
    void cacheUnblocked();

    /** Updates overall dispipe3 status based on all of the stages' statuses. */
    void updateStatus();

    /** Tells the CPU to wakeup if it has descheduled itself due to no
     * activity. Used mainly by the LdWritebackEvent.
     */
    void wakeCPU();

    /** Reports to the CPU that there is activity this cycle. */
    void activityThisCycle();

    /** Tells CPU that the dispipe3 stage is active and running. */
    void activateStage();

    /** Tells CPU that the dispipe3 stage is inactive and idle. */
    void deactivateStage();

    /** Sets pointer to the RMU. */
    void setRMU(RMU *rmu_ptr) { rmu = rmu_ptr; }

    bool insertLdStUop(DynInstPtr inst);

    RiscvISA::Decoder *decoder[MaxThreads];

    RiscvISA::VlIndexRxuMicroInst *GetRegIdx[MaxThreads];
  private:

    /** Sets dispipe3 to blocked, and signals back to other stages to block. */
    void block(ThreadID tid);

    /** Unblocks dispipe3 if per ibuffer is not full, and signals back to
     * other stages to unblock.
     */
    void unblock(ThreadID tid);

    /** Process instructions. */
    void dispipe3(ThreadID tid);

    /** Send ready instructions. */
    void sendInsts(ThreadID tid);

    /**   Insert instructions into wtb. */
    void insertInsts(ThreadID tid);

    /**   Insert instructions into wtb. */
    void handle_inst_noRport(ThreadID tid);

    /** Checks if any of the stall conditions are currently true. */
    bool checkStall(ThreadID tid);

    /** Processes inputs and changes state accordingly. */
    void checkSignalsAndUpdate(ThreadID tid);

    /** Sorts instructions coming from dispipe2 into lists separated by thread. */
    void sortInsts();

    void checkAndSplitEop();
    
    bool canSplit(const std::vector<DynInstPtr>& uopqueue);

    InstQueue eopReplace(DynInstPtr& uop);
  public:
    /** Ticks Dispipe3 stage.
     */
    void tick();

    int all_cnt = 0;
    int odd_cnt = 0;
    int fifo_cnt = 0;
    int even_cnt = 0;
    bool next_group = false;
    bool sq_to_nextgroup = false;
    bool ldst_wtb_full =false;
    bool ldst_vector_ready = true;

    std::map<int, std::vector <DynInstPtr>> vector_ldstQueue;
    std::vector<DynInstPtr> EopBuffer;
    StaticInstPtr uop = NULL;
    StaticInstPtr eop = NULL;
  public:
    /** Pointer to main time buffer used for backwards communication. */
    TimeBuffer<TimeStruct> *timeBuffer;

    /** Wire to get issue's output from backwards time buffer. */
    TimeBuffer<TimeStruct>::wire fromEw;

    /** Wire to get commit's output from backwards time buffer. */
    TimeBuffer<TimeStruct>::wire fromCommit;

    /** Wire to write information heading to previous stages. */
    TimeBuffer<TimeStruct>::wire toDispipe2;

    /** Dispipe2 instruction queue interface. */
    TimeBuffer<Dispipe2Struct> *dispipe2Queue;

    /** Wire to get dispipe2's output from dispipe2 queue. */
    TimeBuffer<Dispipe2Struct>::wire fromDispipe2;

    /** Dispipe3 stage time buffer. */
    TimeBuffer<WakeStruct> *wakeQueue;

    /** Wire to write infromation heading to issue. */
    TimeBuffer<WakeStruct>::wire toEw;

    /** Queue of all instructions coming from dispipe2 this cycle. */
    std::deque<DynInstPtr> insts[MaxThreads];

    /** Queue of all instructions coming from dispipe2 this cycle. */
    std::deque<DynInstPtr> insts_final[MaxThreads];

  private:
    /** CPU pointer. */
    CPU *cpu;

    /** Source of possible stalls. */
    struct Stalls
    {
        bool ew;
    };

    /** Tracks which stages are telling dispipe3 to stall. */
    Stalls stalls[MaxThreads];

    /** Records if Dispipe3 has written to the time buffer this cycle, so that the
     * CPU can deschedule itself if there is no activity.
     */
    bool wroteToTimeBuffer;

  public:

    int num_odd_str = 0;
    int num_even_str = 0;
    bool one = 0;
    bool counter_squash =false;
    std::vector<bool> ldstdisq;
    std::vector<bool> ldstdisq_in;
    std::vector<int> group2;
    bool encounter_flush = false;
    /** Wait buffer. */
    WTB wtb;

    /** RMU interface. */
    RMU *rmu;

    /** Load / store queue. */
    LSQ ldstQueue;

    /** Returns if the LSQ has any stores to writeback. */
    bool hasStoresToWB();

    /** Returns if the LSQ has any stores to writeback. */
    bool hasStoresToWB(ThreadID tid);

    // hardware transactional memory
    // For debugging purposes, it is useful to keep track of the most recent
    // htmUid that has been committed (architecturally, not transactionally)
    // to ensure that the core and the memory subsystem are observing
    // correct ordering constraints.
    void setLastRetiredHtmUid(ThreadID tid, uint64_t htmUid);

  private:
    /** Dispipe2 to Dispipe3 delay. */
    Cycles dispipe2ToDispipe3Delay;

    /** Ew to dispipe3 delay. */
    Cycles ewToDispipe3Delay;

    /** Commit to Dispipe3 delay. */
    Cycles commitToDispipe3Delay;

    /** dispipe3 width, in instructions. */
    unsigned dispipe3Width;

    /** Number of active threads. */
    ThreadID numThreads;

    /** Pointer to list of active threads. */
    std::list<ThreadID> *activeThreads;

    struct Dispipe3Stats : public statistics::Group
    {
        Dispipe3Stats(CPU *cpu);

        /** Stat for total number of idle cycles. */
        statistics::Scalar idleCycles;
        /** Stat for total number of squashing cycles. */
        statistics::Scalar squashCycles;
        /** Stat for total number of blocking cycles. */
        statistics::Scalar blockCycles;
        /** Stat for total number of unblocking cycles. */
        statistics::Scalar unblockCycles;
        /** Stat for total number of cycles spent running normally. */
        statistics::Scalar runCycles;
        /** Stat for total number of dispipe3ed instructions. */
        statistics::Scalar dispipe3Insts;
        /** Stat for total number of squashed instructions dispipe3 skips. */
        statistics::Scalar SquashedInsts;
        /** Stat for total number of dispatched load instructions. */
        statistics::Scalar dispLoadInsts;
        /** Stat for total number of dispatched store instructions. */
        statistics::Scalar dispStoreInsts;
        /** Stat for total number of dispatched non speculative insts. */
        statistics::Scalar dispNonSpecInsts;
        /** Stat for number of times the WTB becomes full. */
        statistics::Scalar wtbFullEvents;

        /** Stat for number of times the ibuffer0 becomes full. */
        statistics::Scalar ibuffer0FullEvents;
        /** Stat for number of times the ibuffer1 becomes full. */
        statistics::Scalar ibuffer1FullEvents;
        /** Stat for number of times the ibuffer4 becomes full. */
        statistics::Scalar ibuffer4FullEvents;
        /** Stat for number of times the ibuffer5 becomes full. */
        statistics::Scalar ibuffer5FullEvents;

        /** Stat for number of times the ibuffer2 becomes full. */
        statistics::Scalar ibuffer2FullEvents;
        /** Stat for number of times the ibuffer3 becomes full. */
        statistics::Scalar ibuffer3FullEvents;
        /** Stat for number of times the ibuffer20 becomes full. */
        statistics::Scalar ibuffer20FullEvents;
        /** Stat for number of times the ibuffer21 becomes full. */
        statistics::Scalar ibuffer21FullEvents;
        /** Stat for number of times the ibuffer22 becomes full. */
        statistics::Scalar ibuffer22FullEvents;
        /** Stat for number of times the ibuffer23 becomes full. */
        statistics::Scalar ibuffer23FullEvents;

        /** Stat for number of times the ibuffer6 becomes full. */
        statistics::Scalar ibuffer6FullEvents;
        /** Stat for number of times the ibuffer7 becomes full. */
        statistics::Scalar ibuffer7FullEvents;
        /** Stat for number of times the ibuffer14 becomes full. */
        statistics::Scalar ibuffer14FullEvents;
        /** Stat for number of times the ibuffer15 becomes full. */
        statistics::Scalar ibuffer15FullEvents;
        /** Stat for number of times the ibuffer16 becomes full. */
        statistics::Scalar ibuffer16FullEvents;
        /** Stat for number of times the ibuffer17 becomes full. */
        statistics::Scalar ibuffer17FullEvents;
        /** Stat for number of times the ibuffer18 becomes full. */
        statistics::Scalar ibuffer18FullEvents;
        /** Stat for number of times the ibuffer19 becomes full. */
        statistics::Scalar ibuffer19FullEvents;

        /** Stat for number of times the ibuffer8 becomes full. */
        statistics::Scalar ibuffer8FullEvents;
        /** Stat for number of times the ibuffer9 becomes full. */
        statistics::Scalar ibuffer9FullEvents;
        /** Stat for number of times the ibuffer10 becomes full. */
        statistics::Scalar ibuffer10FullEvents;
        /** Stat for number of times the ibuffer13 becomes full. */
        statistics::Scalar ibuffer13FullEvents;

        /** Stat for number of times the ibuffer11 becomes full. */
        statistics::Scalar ibuffer11FullEvents;
        /** Stat for number of times the ibuffer12 becomes full. */
        statistics::Scalar ibuffer12FullEvents;

        /** Stat for number of times the LSQ becomes full. */
        statistics::Scalar lsqFullEvents;

        /** Stat for total number of instructions into ibuffer 0. */
        statistics::Scalar ibuffer0Insts;
        /** Stat for total number of instructions into ibuffer 1. */
        statistics::Scalar ibuffer1Insts;
        /** Stat for total number of instructions into ibuffer 4. */
        statistics::Scalar ibuffer4Insts;
        /** Stat for total number of instructions into ibuffer 5. */
        statistics::Scalar ibuffer5Insts;

        /** Stat for total number of instructions into ibuffer 2. */
        statistics::Scalar ibuffer2Insts;
        /** Stat for total number of instructions into ibuffer 3. */
        statistics::Scalar ibuffer3Insts;
        /** Stat for total number of instructions into ibuffer 20. */
        statistics::Scalar ibuffer20Insts;
        /** Stat for total number of instructions into ibuffer 21. */
        statistics::Scalar ibuffer21Insts;
        /** Stat for total number of instructions into ibuffer 22. */
        statistics::Scalar ibuffer22Insts;
        /** Stat for total number of instructions into ibuffer 23. */
        statistics::Scalar ibuffer23Insts;

        /** Stat for total number of instructions into ibuffer 6. */
        statistics::Scalar ibuffer6Insts;
        /** Stat for total number of instructions into ibuffer 7. */
        statistics::Scalar ibuffer7Insts;
        /** Stat for total number of instructions into ibuffer 14. */
        statistics::Scalar ibuffer14Insts;
        /** Stat for total number of instructions into ibuffer 15. */
        statistics::Scalar ibuffer15Insts;
         /** Stat for total number of instructions into ibuffer 16. */
        statistics::Scalar ibuffer16Insts;
        /** Stat for total number of instructions into ibuffer 17. */
        statistics::Scalar ibuffer17Insts;
        /** Stat for total number of instructions into ibuffer 18. */
        statistics::Scalar ibuffer18Insts;
        /** Stat for total number of instructions into ibuffer 19. */
        statistics::Scalar ibuffer19Insts;

        /** Stat for total number of instructions into ibuffer 8. */
        statistics::Scalar ibuffer8Insts;
        /** Stat for total number of instructions into ibuffer 9. */
        statistics::Scalar ibuffer9Insts;
        /** Stat for total number of instructions into ibuffer 10. */
        statistics::Scalar ibuffer10Insts;
        /** Stat for total number of instructions into ibuffer 13. */
        statistics::Scalar ibuffer13Insts;

        /** Stat for total number of instructions into ibuffer 11. */
        statistics::Scalar ibuffer11Insts;
        /** Stat for total number of instructions into ibuffer 12. */
        statistics::Scalar ibuffer12Insts;

        /** Stat for total number of instructions into ibuffer 12. */
        statistics::Scalar vectorLDST_full;
    } stats;
};

} // namespace rxuo3
} // namespace gem5

#endif // __CPU_RxuO3_DISPIPE3_HH__
// ----------------------------------------------------------------------------
