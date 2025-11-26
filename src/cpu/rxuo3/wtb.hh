// -- add by hongfei.liu ---------------------------------------------
#ifndef __CPU_RxuO3_WTB_HH__
#define __CPU_RxuO3_WTB_HH__

#include <list>
#include <map>
#include <queue>
#include <vector>
#include <tuple>

#include "base/statistics.hh"
#include "base/types.hh"
#include "cpu/inst_seq.hh"
// #include "cpu/rxuo3/comm.hh"
#include "cpu/rxuo3/dyn_inst_ptr.hh"
#include "cpu/rxuo3/limits.hh"
#include "cpu/rxuo3/mem_dep_unit.hh"
#include "cpu/rxuo3/store_set.hh"
#include "cpu/op_class.hh"
#include "cpu/timebuf.hh"
#include "sim/eventq.hh"

// #include <unordered_map>

namespace gem5
{

struct BaseRxuO3CPUParams;

namespace memory
{
class MemInterface;
} // namespace memory

namespace rxuo3
{

class CPU;
class Dispipe3;
class RxuFUPool;

class WTB
{
  public:
    /** Int div pipeline completion event class. */
    class intDivCompletion : public Event
    {
      private:
        /** Pointer back to the WTB. */
        WTB *wtbPtr;
      public:
        /** Construct an intDivCompletion event. */
        intDivCompletion(WTB *wtb_ptr);

        virtual void process();
    };

    /** Float div pipeline completion event class. */
    class fpDivCompletion : public Event
    {
      private:
        /** Pointer back to the WTB. */
        WTB *wtbPtr;
      public:
        /** Construct a fpDivCompletion event. */
        fpDivCompletion(WTB *wtb_ptr);

        virtual void process();
    };

    /** Float sqrt pipeline completion event class. */
    class fpSqrtCompletion : public Event
    {
      private:
        /** Pointer back to the WTB. */
        WTB *wtbPtr;
      public:
        /** Construct a fpSqrtCompletion event. */
        fpSqrtCompletion(WTB *wtb_ptr);

        virtual void process();
    };

    /** Early wake up event class. */
    class earlyWakeUp : public Event
    {
      private:
        /** Pointer back to the WTB. */
        WTB *wtbPtr;

        /** Instruction. */
        DynInstPtr inst;

      public:
        /** Construct a earlyWakeUp event. */
        earlyWakeUp(WTB *wtb_ptr, DynInstPtr &_inst);

        virtual void process();
    };

    class LdstWakeToIssue : public Event
    {
      private:
        /** Pointer back to the WTB. */
        WTB *wtbPtr;

        /** Load/Store instruction. */
        DynInstPtr inst;

      public:
        /** Construct a LdstWakeToIssue event. */
        LdstWakeToIssue(WTB *wtb_ptr, DynInstPtr &_inst);

        virtual void process();
    };

    class readySrc_readReg : public Event
    {
      private:
        /** Pointer back to the WTB. */
        WTB *wtbPtr;

        /** Load/Store instruction. */
        DynInstPtr inst;

      public:
        /** Construct a readySrc_readReg event. */
        readySrc_readReg(WTB *wtb_ptr, DynInstPtr &_inst);

        virtual void process();
    };

    /** SpecialMacro delay one cycle insert to dispipe3ToEW
     *  when all uop is issued.
     */
    class SpecialMacroReady : public Event
    {
      private:
        /** Pointer back to the WTB. */
        WTB *wtbPtr;

        /** SpecialMacro instruction. */
        DynInstPtr inst;

      public:
        /** Construct a SpecialMacroReady event. */
        SpecialMacroReady(WTB *wtb_ptr, DynInstPtr &_inst);

        virtual void process();
    };

    /** Special vector issueQ. */
    class VSpecialBuffer
    {
      private:
        /** Pointer back to the WTB. */
        WTB *wtbPtr;

        std::list<DynInstPtr> insts;

        /** special issueQ can issue a inst only when last inst executed. */
        DynInstPtr lastIssueInst;
        
        int max_size = 16;

      public:
        VSpecialBuffer(WTB *wtb_ptr);

        void insertInst(DynInstPtr inst);

        DynInstPtr selectInst();

        bool entryEnough();

        int size();

        void squash(InstSeqNum sn);

        std::string name() const;
    };

    VSpecialBuffer *vSpecialBuffer;

  /**Wake up Vsetvli event class. */
  class wakeUpVsetvli : public Event
  {
    private:
      /** Pointer back to the WTB. */
      WTB *wtbPtr;

      /** Instruction. */
      DynInstPtr inst;

    public:
      /** Construct a wakeUpVsetvli event. */
      wakeUpVsetvli(WTB *wtb_ptr, DynInstPtr &_inst);

      virtual void process();
  };

  public:
    // Typedef of iterator through the list of instructions.
    typedef typename std::list<DynInstPtr>::iterator ListIt;

    /** Constructs an WTB. */
    WTB(CPU *cpu_ptr, Dispipe3 *p3_ptr, const BaseRxuO3CPUParams &params);

    /** Destructs the WTB. */
    ~WTB();

    /** Returns the name of the WTB. */
    std::string name() const;



    /** Resets all WTB state. */
    void resetState();

    /** Sets active threads list. */
    void setActiveThreads(std::list<ThreadID> *at_ptr);

    /** Determine if we are drained. */
    bool isDrained() const;

    /** Perform sanity checks after a drain. */
    void drainSanityCheck() const;

    /** Takes over execution from another CPU's thread. */
    void takeOverFrom();

    /** Returns number of free entries for a ibuffer. */
    unsigned numFreeEntries(ThreadID tid, int index);

    /** Returns whether or not a ibuffer is full. */
    bool isFull(ThreadID tid, int index);

    /** Returns whether or not a ibuffer is full. */
    bool isFull(ThreadID tid, int index, bool isStore, bool ismemRef);

    /** Returns whether or not any ibuffer is full. */
    bool isFull(ThreadID tid);

    /** Returns if there are any ready instructions in the WTB. */
    bool hasReadyInsts();

    /** Inserts a new instruction into the WTB. */
    void insert(DynInstPtr &new_inst);

    /** Inserts a new, non-speculative instruction into the WTB. */
    void insertNonSpec(const DynInstPtr &new_inst);

    /** Inserts a memory or write barrier into the WTB to make sure
     *  loads and stores are ordered properly.
     */
    void insertBarrier(const DynInstPtr &barr_inst);

    void handle_inst_noRport();

    /**
     * Schedules ready instructions, sending to ew stage.
     */
    void scheduleReadyInsts();

    /** Schedules a single specific non-speculative instruction. */
    void scheduleNonSpec(const InstSeqNum &inst);

    /** Wakes all dependents of a completed instruction. */
    int wakeDependents(const DynInstPtr &completed_inst);

    /** Adds a ready memory instruction to the ready list. */
    void addReadyMemInst(const DynInstPtr &ready_inst);

    /**
     * Reschedules a memory instruction. It will be ready to issue once
     * replayMemInst() is called.
     */
    void rescheduleMemInst(const DynInstPtr &resched_inst);

    /** Replays a memory instruction. It must be rescheduled first. */
    void replayMemInst(const DynInstPtr &replay_inst);

    /**
     * Defers a memory instruction when its DTB translation incurs a hw
     * page table walk.
     */
    void deferMemInst(const DynInstPtr &deferred_inst);

    /**  Defers a memory instruction when it is cache blocked. */
    void blockMemInst(const DynInstPtr &blocked_inst);

    /**  Notify instruction queue that a previous blockage has resolved */
    void cacheUnblocked();

    /** Gets a memory instruction that was referred due to a delayed DTB
     *  translation if it is now ready to execute.  NULL if none available.
     */
    DynInstPtr getDeferredMemInstToExecute();

    /** Gets a memory instruction that was blocked on the cache. NULL if none
     *  available.
     */
    DynInstPtr getBlockedMemInstToExecute();

    DynInstPtr getSCtoExecute();

    /**
     * Squashes instructions for a thread. Squashing information is obtained
     * from the time buffer.
     */
    void squash(ThreadID tid, InstSeqNum SeqNum);

    /** Process int div completion event. */
    void processIntDivCompletion();

    /** Process float div completion event. */
    void processFpDivCompletion();

    /** Process float sqrt completion event. */
    void processFpSqrtCompletion();

    /** Process early wake up event. */
    void processEarlyWakeUp(DynInstPtr &inst);

    /** Process load/store from wake to issue event. */
    void processLdstWakeToIssue(DynInstPtr &inst);

    /** Process nolocaldata src read reg. */
    void processreadySrc_readReg(DynInstPtr &inst);

    /** Process wakeUpVsetvli event. */
    void processWakeUpVsetvli(DynInstPtr &inst);    

    /** The memory dependence unit, which tracks/predicts memory dependences
     *  between instructions.
     */
    MemDepUnit memDepUnit[MaxThreads];

  public:
    /** Does the actual squashing. */
    void doSquash(ThreadID tid);

    /////////////////////////
    // Various pointers
    /////////////////////////

    /** Pointer to the CPU. */
    CPU *cpu;

    /** Cache interface. */
    memory::MemInterface *dcacheInterface;

    /** Pointer to dispipe3 stage. */
    Dispipe3 *dispipe3Stage;

    /** The number of entries per ibuffer. */
    int numWTBEntries;

    int numVectorEntries;

    int numIntSpecialEntries;

    int LdStEntries;

    bool need_readR_again = true;

    /** The number of int normal odd regfile read ports. */
    unsigned intNormalOddRegReadNums;

    /** The number of int special odd regfile read ports. */
    unsigned intSpecialOddRegReadNums;

    /** The number of int normal even regfile read ports. */
    unsigned intNormalEvenRegReadNums;

    /** The number of int special even regfile read ports. */
    unsigned intSpecialEvenRegReadNums;

    /** The number of int normal odd regfile write ports. */
    unsigned intNormalOddRegWriteNums;

    /** The number of int special odd regfile write ports. */
    unsigned intSpecialOddRegWriteNums;

    /** The number of int normal even regfile write ports. */
    unsigned intNormalEvenRegWriteNums;

    /** The number of int special even regfile write ports. */
    unsigned intSpecialEvenRegWriteNums;

    /** The number of load/store regfile read ports. */
    unsigned ldstOddRegReadNums;
    unsigned ldstEvenRegReadNums;

    /** The number of float store regfile read ports. */
    unsigned fstRegReadNums;

    /** The number of float normal regfile read ports. */
    unsigned fpNormalRegReadNums;

    /** The number of float special regfile read ports. */
    unsigned fpSpecialRegReadNums;

    /** The number of float regfile write ports. */
    unsigned fpRegWriteNums;

    /** The number of float load regfile write ports. */
    unsigned fldRegWriteNums;

    /** The number of Vector unldst regfile read ports. */
    unsigned vectorReadRegNums;

    /** The number of float load regfile write ports. */
    unsigned vectorLDSTReadRegNums;

    /** The number of int normal odd regfile free read ports. */
    unsigned intNormalOddRegReadFreeNums;

    /** The number of int special odd regfile free read ports. */
    unsigned intSpecialOddRegReadFreeNums;

    /** The number of int normal even regfile free read ports. */
    unsigned intNormalEvenRegReadFreeNums;

    /** The number of int special even regfile free read ports. */
    unsigned intSpecialEvenRegReadFreeNums;

    /** The number of load/store regfile free read ports. */
    unsigned ldstOddRegReadFreeNums;
    unsigned ldstEvenRegReadFreeNums;

    /** The number of float store regfile free read ports. */
    unsigned fstRegReadFreeNums;

    /** The number of float normal regfile free read ports. */
    unsigned fpNormalRegReadFreeNums;

    /** The number of float special regfile free read ports. */
    unsigned fpSpecialRegReadFreeNums;

    /** The number of float normal regfile free read ports. */
    unsigned vectorReadRegFreeNums;

    /** The number of float normal regfile free read ports. */
    unsigned vectorLDSTReadRegFreeNums;

    //////////////////////////////////////
    // Instruction buffers and lists
    //////////////////////////////////////

    /** ibuffer. */
    std::list<DynInstPtr> ibuffer[MaxThreads][28];

    std::vector<DynInstPtr> st_request;

    std::vector<DynInstPtr> ld_needwait;

    std::vector<DynInstPtr> ld_had_rbk;

    /** List of instructions waiting for their DTB translation to
     *  complete (hw page table walk in progress).
     */
    std::list<DynInstPtr> deferredMemInsts;

    /** List of instructions that have been cache blocked. */
    std::list<DynInstPtr> blockedMemInsts;

    /** List of instructions that were cache blocked, but a retry has been seen
     * since, so they can now be retried. May fail again go on the blocked list.
     */
    std::list<DynInstPtr> retryMemInsts;

    /**
     * Struct for comparing entries to be added to the priority queue.
     * This gives reverse ordering to the instructions in terms of
     * sequence numbers: the instructions with smaller sequence
     * numbers (and hence are older) will be at the top of the
     * priority queue.
     */
    struct PqCompare
    {
        bool operator()(const DynInstPtr &lhs, const DynInstPtr &rhs) const;
    };

    typedef std::priority_queue<
        DynInstPtr, std::vector<DynInstPtr>, PqCompare> ReadyInstQueue;

    /** List of ready instructions, per ibuffer.
     * 6,7 represents ls7 and ls8, which are currently not used.
     * However, in order to correspond with ibuffer, they are still defined here.
     */
    ReadyInstQueue readyInsts[24][2];

    ReadyInstQueue readyVecInst0;

    ReadyInstQueue readyVecInst1;

    /** List of src0 ready ldst instructions. */
    ReadyInstQueue readyLDSTodd0;

    std::list<DynInstPtr> inst_without_rPort;

    std::vector<DynInstPtr> TSbuffer;

    /** List of src0 ready ldst instructions. */
    ReadyInstQueue sc_queue;

    /** List of src0 ready ldst instructions. */
    ReadyInstQueue readyLDSTeven0;

    // /** List of src0 ready ldst instructions. */
    // ReadyInstQueue readyLDSTodd1;

    // /** List of src0 ready ldst instructions. */
    // ReadyInstQueue readyLDSTeven1;

    /** List of src0 ready ldst instructions. */
    ReadyInstQueue readyLD_Dst_odd;

    /** List of src0 ready ldst instructions. */
    ReadyInstQueue readyLD_Dst_even;

    /** List of src0 ready ldst instructions. */
    ReadyInstQueue readyStore;

    /** List of src1 ready st instructions. */
    ReadyInstQueue readySTdata0;

    /** List of src1 ready st instructions. */
    ReadyInstQueue readySTdata1;

    std::vector<DynInstPtr> stsrc1notrdy;

    /** List of ready instructions that they will be issued this cycle. */
    ReadyInstQueue ibufferToEW;

    ReadyInstQueue arbiterTmpQ;

    /** Struct of recording number of instructions that will write back per cycle. */
    struct WbNums
    {
        unsigned intNormalOddWbNums;
        unsigned intSpecialOddWbNums;
        unsigned intNormalEvenWbNums;
        unsigned intNormal_newEvenWbNums;
        unsigned intNormal_newOddWbNums;
        unsigned intSpecialEvenWbNums;
        unsigned fpWbNums;
        unsigned fldWbNums;
        unsigned vecttorWbNums;
    };

    enum src1type {
      odd,
      even,
      unknow
    };

    /** Queue of recording number of instructions that will write back. */
    std::deque<WbNums> wbNums;

    // struct Srcs
    // {
    //     bool src[3];
    // };

    // std::deque<std::unordered_map<InstSeqNum, Srcs>> fwdInsts;

    /** Struct of recording number of special pipeline that can use. */
    struct PipelineUseNums
    {
        unsigned intDivNums;
        unsigned fpDivNums;
        unsigned fpSqrtNums;
    };

    PipelineUseNums pipelineUseNums;

    std::deque<DynInstPtr> fldFIFO;

    std::deque<std::tuple<InstSeqNum, DynInstPtr>> ldWait;

    /** List of non-speculative instructions that will be scheduled
     *  once the WTB gets a signal from commit.  While it's redundant to
     *  have the key be a part of the value (the sequence number is stored
     *  inside of DynInst), when these instructions are woken up only
     *  the sequence number will be available.  Thus it is most efficient to be
     *  able to search by the sequence number alone.
     */
    std::map<InstSeqNum, DynInstPtr> nonSpecInsts;

    typedef std::map<InstSeqNum, DynInstPtr>::iterator NonSpecMapIt;

    std::map<int, std::vector<DynInstPtr>> vec_read_network_36_18;

    std::map<int, std::vector<DynInstPtr>> vec_read_network_18_9;

    std::map<int, std::vector<DynInstPtr>> vec_read_network_9;

    int cnt_vector_network_round = 0;

    //////////////////////////////////////
    // Various parameters
    //////////////////////////////////////

    /** Number of Total Threads*/
    ThreadID numThreads;

    /** Pointer to list of active threads. */
    std::list<ThreadID> *activeThreads;

    // /** Per Thread ibuffer count */
    // int count[MaxThreads][16];

    // /** Per Thread ibuffer free entries */
    // int freeEntries[MaxThreads][16];

    /** The sequence number of the squashed instruction. */
    InstSeqNum squashedSeqNum[MaxThreads];

    unsigned initFdivNums;

    unsigned initFdivDelay;

    // std::deque<DynInstPtr> waitFRM;

    // std::deque<DynInstPtr> waitFloat;

    /** Moves an instruction to the ready queue if it is ready. */
    void addIfReady(DynInstPtr &inst);

    // /** Moves a store instruction to the ready queue if its src1 is ready. */
    // void addstdIfReady(const DynInstPtr &inst);

    /** Choose the oldest an instruction from ibuffer_id1 and ibuffer_id2. */
    void selectOne(int ibuffer_id1, int ibuffer_id2);

    void arbiterFloat();

    /** Choose the oldest an odd instruction
     * and an even instruction from ibuffer_id1 and ibuffer_id2.
     * */
    void selectTwo(int ibuffer_id1, int ibuffer_id2);

    void selectVec();

    void arbiterInt();

    void arbiterVec();

    void arbiterIntSpecialAndBranch();

    void insert_vector_read_network36(bool from_disq, DynInstPtr inst0, DynInstPtr inst1, DynInstPtr inst2, DynInstPtr inst3);

    void select_network36to18();

    void select_network18to9();

    void read_vectorReg();

    void select_network2to1_36(int first, int second);

    void select_network2to1_18(int first, int second);

    /** Choose the oldest an store instruction
     * and two load instructions.
     * */
    void selectLdSt();

    /** Choose the oldest an store instruction
     * and two load instructions.
     * */
    void selectLdSt_dst();

    /** Sets free regfile read ports this cycle. */
    void setFreeReadRegPorts();

    /** Update WbNums every cycle. */
    void updateWbNums();

    // void wakeWaitFRM();

    // void wakeWaitFloat();

    struct WTBStats : public statistics::Group
    {
        WTBStats(CPU *cpu);

        /** Stat for number of all instructions issued. */
        statistics::Scalar instsIssued;
        /** Stat for number of integer instructions issued. */
        statistics::Scalar intInstsIssued;
        /** Stat for number of integer normal instructions issued. */
        statistics::Scalar intNorInstsIssued;
        /** Stat for number of integer special instructions issued. */
        statistics::Scalar intSpeInstsIssued;
        /** Stat for number of floating point instructions issued. */
        statistics::Scalar fpInstsIssued;
        /** Stat for number of floating point normal instructions issued. */
        statistics::Scalar fpNorInstsIssued;
        /** Stat for number of floating point special instructions issued. */
        statistics::Scalar fpSpeInstsIssued;
        /** Stat for number of branch instructions issued. */
        statistics::Scalar branchInstsIssued;
        /** Stat for number of memory instructions issued. */
        statistics::Scalar memInstsIssued;
        /** Stat for number of load instructions issued. */
        statistics::Scalar loadInstsIssued;
        /** Stat for number of store instructions issued. */
        statistics::Scalar storeInstsIssued;
        /** Stat for number of squashed instructions in WTB. */
        statistics::Scalar squashedInsts;
        /** Number of times an instruction could not be issued because a
         * FU was busy.
         */
        statistics::Vector statFuBusy;
        /** Number of cycles instructions fail to issue due to the FU busy. */
        statistics::Scalar fuBusyCycles;
        /** Number of times instructions fail to issue due to the int regReadPort busy. */
        statistics::Scalar intRegReadBusy;

        statistics::Scalar intRegReadFree;

        /** Number of times instructions fail to issue due to the int regWritePort busy. */
        statistics::Scalar intRegWriteBusy;
        /** Number of times instructions fail to issue due to the float regReadPort busy. */
        statistics::Scalar fpRegReadBusy;

        statistics::Scalar fpRegReadFree;
        /** Number of times instructions fail to issue due to the float regWritePort busy. */
        statistics::Scalar fpRegWriteBusy;
        statistics::Scalar vectorRegWriteBusy;

        statistics::Scalar ibuffer0Utilize;
        statistics::Scalar ibuffer1Utilize;
        statistics::Scalar ibuffer4Utilize;
        statistics::Scalar ibuffer5Utilize;

        statistics::Scalar ibuffer2Utilize;
        statistics::Scalar ibuffer3Utilize;
        statistics::Scalar ibuffer20Utilize;
        statistics::Scalar ibuffer21Utilize;
        statistics::Scalar ibuffer22Utilize;
        statistics::Scalar ibuffer23Utilize;

        statistics::Scalar ibuffer6Utilize;
        statistics::Scalar ibuffer7Utilize;
        statistics::Scalar ibuffer14Utilize;
        statistics::Scalar ibuffer15Utilize;
        statistics::Scalar ibuffer16Utilize;
        statistics::Scalar ibuffer17Utilize;
        statistics::Scalar ibuffer18Utilize;
        statistics::Scalar ibuffer19Utilize;

        statistics::Scalar ibuffer8Utilize;
        statistics::Scalar ibuffer9Utilize;
        statistics::Scalar ibuffer10Utilize;
        statistics::Scalar ibuffer13Utilize;

        statistics::Scalar ibuffer11Utilize;
        statistics::Scalar ibuffer12Utilize;

        /** ibuffer0 utilization rate. */
        statistics::Formula ibuffer0_UtilizationRate;
        /** ibuffer1 utilization rate. */
        statistics::Formula ibuffer1_UtilizationRate;
        /** ibuffer4 utilization rate. */
        statistics::Formula ibuffer4_UtilizationRate;
        /** ibuffer5 utilization rate. */
        statistics::Formula ibuffer5_UtilizationRate;

        /** ibuffer2 utilization rate. */
        statistics::Formula ibuffer2_UtilizationRate;
        /** ibuffer3 utilization rate. */
        statistics::Formula ibuffer3_UtilizationRate;
        /** ibuffer20 utilization rate. */
        statistics::Formula ibuffer20_UtilizationRate;
        /** ibuffer21 utilization rate. */
        statistics::Formula ibuffer21_UtilizationRate;
        /** ibuffer22 utilization rate. */
        statistics::Formula ibuffer22_UtilizationRate;
        /** ibuffer23 utilization rate. */
        statistics::Formula ibuffer23_UtilizationRate;

        /** ibuffer6 utilization rate. */
        statistics::Formula ibuffer6_UtilizationRate;
        /** ibuffer7 utilization rate. */
        statistics::Formula ibuffer7_UtilizationRate;
        /** ibuffer14 utilization rate. */
        statistics::Formula ibuffer14_UtilizationRate;
        /** ibuffer15 utilization rate. */
        statistics::Formula ibuffer15_UtilizationRate;
        /** ibuffer16 utilization rate. */
        statistics::Formula ibuffer16_UtilizationRate;
        /** ibuffer17 utilization rate. */
        statistics::Formula ibuffer17_UtilizationRate;
        /** ibuffer18 utilization rate. */
        statistics::Formula ibuffer18_UtilizationRate;
        /** ibuffer19 utilization rate. */
        statistics::Formula ibuffer19_UtilizationRate;

        /** ibuffer8 utilization rate. */
        statistics::Formula ibuffer8_UtilizationRate;
        /** ibuffer9 utilization rate. */
        statistics::Formula ibuffer9_UtilizationRate;
        /** ibuffer10 utilization rate. */
        statistics::Formula ibuffer10_UtilizationRate;
        /** ibuffer13 utilization rate. */
        statistics::Formula ibuffer13_UtilizationRate;

        /** ibuffer11 utilization rate. */
        statistics::Formula ibuffer11_UtilizationRate;
        /** ibuffer12 utilization rate. */
        statistics::Formula ibuffer12_UtilizationRate;

        statistics::Distribution ibuffer2Out;
        statistics::Distribution ibuffer3Out;

        statistics::Scalar intnormal_lockRpork;
        statistics::Scalar intspecial_lockRpork;
        statistics::Scalar fnormal_lockRpork;
        statistics::Scalar fspecial_lockRpork;
        statistics::Scalar vector_lockRpork;
        statistics::Scalar ldst_lockRpork;
        statistics::Scalar circleover3;
        statistics::Scalar circleover3_noRport;

        statistics::Scalar earlywakeupvsetvli;
        statistics::Scalar readywakeupvsetvli;


        statistics::Distribution circle_inwtb;


    } stats;
};

} // namespace rxuo3
} // namespace gem5

#endif //__CPU_RxuO3_WTB_HH__
// ----------------------------------------------------------------------------
