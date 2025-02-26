#ifndef __CPU_RxuO3_FETCH_HH__
#define __CPU_RxuO3_FETCH_HH__

#include <algorithm>
#include <cstring>
#include <list>
#include <map>
// #include <queue>

#include "arch/generic/decoder.hh"
#include "arch/generic/mmu.hh"
#include "base/statistics.hh"
#include "cpu/rxuo3/comm.hh"
#include "cpu/rxuo3/dyn_inst_ptr.hh"
#include "cpu/rxuo3/limits.hh"
#include "cpu/pc_event.hh"
#include "cpu/pred/bpred_unit.hh"
#include "cpu/timebuf.hh"
#include "cpu/translation.hh"
#include "enums/RxuSMTFetchPolicy.hh"
#include "mem/packet.hh"
#include "mem/port.hh"
#include "sim/eventq.hh"
#include "sim/probe/probe.hh"
#include "cpu/rxuo3/uop_cache.hh"

namespace gem5
{

struct BaseRxuO3CPUParams;

namespace rxuo3
{

class CPU;
class UopCache;

/**
 * Fetch class handles both single threaded and SMT fetch. Its
 * width is specified by the parameters; each cycle it tries to fetch
 * that many instructions. It supports using a branch predictor to
 * predict direction and targets.
 * It supports the idling functionality of the CPU by indicating to
 * the CPU when it is active and inactive.
 */
class Fetch
{
  public:
    /**
     * IcachePort class for instruction fetch.
     */
    class IcachePort : public RequestPort
    {
      protected:
        /** Pointer to fetch. */
        Fetch *fetch;

      public:
        /** Default constructor. */
        IcachePort(Fetch *_fetch, CPU *_cpu);

      protected:

        /** Timing version of receive.  Handles setting fetch to the
         * proper status to start fetching. */
        virtual bool recvTimingResp(PacketPtr pkt);

        /** Handles doing a retry of a failed fetch. */
        virtual void recvReqRetry();
    };

    class FetchTranslation : public BaseMMU::Translation
    {
      protected:
        Fetch *fetch;

      public:
        FetchTranslation(Fetch *_fetch) : fetch(_fetch) {}

        void markDelayed() {}

        void
        finish(const Fault &fault, const RequestPtr &req,
            gem5::ThreadContext *tc, BaseMMU::Mode mode)
        {
            assert(mode == BaseMMU::Execute);
            fetch->finishTranslation(fault, req);
            delete this;
        }
    };

    class Cam
    {
      public:

        struct CamEntry{
          Addr tag;
          bool valid;
          bool has_replaced;
          unsigned numInsts;
          Addr pc;
          std::list<Addr> pc_list;
          std::list<StaticInstPtr> insts;

          CamEntry() : tag(0), valid(false), has_replaced(false), numInsts(0), pc(0) { }
          
          void clear() {
            tag = 0;
            valid = false;
            numInsts = 0;
            pc = 0;
            pc_list.clear();
            insts.clear();
          }      
        };

        typedef std::pair<unsigned, Addr> Info;

        struct HitInfo { 
            unsigned hit = 0;
            int win_size = 0;
            std::list<Info> info;

            bool hitHead() {
              return (hit & 1) && ( // 1101 1100 0
                !info.empty() && info.front().first > 0
              );  
            }

            void advance() {
              hit = hit >> 1;
              win_size = std::max(0, win_size - 1);
              if (!info.empty()) info.pop_front();
              if (hit == 0) clear();
            }

            void advanceDuetoFetch() {
              hit = hit >> 1; // 1001
              win_size = std::max(0, win_size - 1);
            }

            unsigned pcShift(Addr pc) {
              Addr tpc = info.front().second;
              return tpc - pc;
            }

            bool empty() {
              return !(!info.empty() && info.front().first > 0);
            }

            void decrease() {
              assert(!info.empty());
              if (info.front().first > 0) {
                info.front().first--;
              }
            }

            void clear() {
              hit = 0;
              win_size = 0;
              while (!info.empty()) info.pop_front();
            }
        };

        CamEntry *cam;

        Cam(CPU *cpu, Fetch *fetch);

        std::list<StaticInstPtr> insts;

        std::list<Addr> pc_insts;        

        bool update(Addr pc, std::list<StaticInstPtr> &cacheLineData,
                             std::list<Addr> &pc_list);
          
        bool lookupcam(Addr _pc);

        DynInstPtr buildInst(ThreadID tid, StaticInstPtr staticInst,
                StaticInstPtr curMacroop, const PCStateBase &this_pc,
                const PCStateBase &next_pc, bool trace);
        Cam::Info process(PCStateBase &this_pc);

        bool lookupAndUpdateNextPC(const DynInstPtr &inst, PCStateBase &next_pc);

        HitInfo access(PCStateBase &pc, int num);

        Info lookupOnly(Addr _pc);

        HitInfo lookupTwo(Addr _pc);
        
        Addr alignPC(Addr addr)
        {   
            Addr addrMask = 63;
            Addr base16 = addr & ~(addrMask);
            Addr base8 = addr & ~(addrMask >> 1);
            return base8 > base16 ? base8 : base16;
        }

        std::string name() const;


        unsigned pcShift(Info info, Addr pc) {
          Addr tpc = info.second;
          return tpc - pc;
        }    

      protected:
        Fetch *fetch;
        CPU *cpu;

        
    };

  private:
    /* Event to delay delivery of a fetch translation result in case of
     * a fault and the nop to carry the fault cannot be generated
     * immediately */
    class FinishTranslationEvent : public Event
    {
      private:
        Fetch *fetch;
        Fault fault;
        RequestPtr req;

      public:
        FinishTranslationEvent(Fetch *_fetch)
            : fetch(_fetch), req(nullptr)
        {}

        void setFault(Fault _fault) { fault = _fault; }
        void setReq(const RequestPtr &_req) { req = _req; }
        RequestPtr getReq() { return req; }

        /** Process the delayed finish translation */
        void
        process()
        {
            assert(fetch->numInst < fetch->fetchWidth);
            fetch->finishTranslation(fault, req);
        }

        const char *
        description() const
        {
            return "CPU FetchFinishTranslation";
        }
      };

    // /** UopCache completion event class. */
    // class UopCacheCompletion : public Event
    // {
    //   public:
    //     Fetch *fetch;
    //     std::unique_ptr<PCStateBase> pc;
    //     int cachelines;
      
    //   public:
    //     /** Construct a FU completion event. */
    //     UopCacheCompletion(Fetch *_fetch);

    //     void setFetch(Fetch *_fetch) { fetch = _fetch; }
    //     void setPC(const PCStateBase &_pc) { set(pc, _pc); }
    //     void setNum(int num) { cachelines = num; }

    //     virtual void process();

    //     virtual const char *description() const;
    // };

// ---------- created by longting.du ---------------
    /** UopCache completion event class. */
    class UopCacheStoreCompletion : public Event
    {
      private:
        Fetch *fetch;
        Addr pc;
        std::list<StaticInstPtr> instList;
        std::list<Addr> pc_list;

      public:
        /** Construct a FU completion event. */
        UopCacheStoreCompletion(Fetch *_fetch);

        void setFetch(Fetch *_fetch) { fetch = _fetch; }
        void setPC(Addr _pc) { pc = _pc; }
        void setQueue(std::list<StaticInstPtr> _instList);

        virtual void process();

        virtual const char *description() const;
    };
// ------------------------------------------------------

  public:
    /** Overall fetch status. Used to determine if the CPU can
     * deschedule itsef due to a lack of activity.
     */
    enum FetchStatus
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
        Fetching,
        TrapPending,
        QuiescePending,
        ItlbWait,
        IcacheWaitResponse,
        IcacheWaitRetry,
        IcacheAccessComplete,
        NoGoodAddr
    };

  private:
    /** Fetch status. */
    FetchStatus _status;

    /** Per-thread status. */
    ThreadStatus fetchStatus[MaxThreads];

    /** Fetch policy. */
    RxuSMTFetchPolicy fetchPolicy;

    /** List that has the threads organized by priority. */
    std::list<ThreadID> priorityList;

    /** Probe points. */
    ProbePointArg<DynInstPtr> *ppFetch;
    /** To probe when a fetch request is successfully sent. */
    ProbePointArg<RequestPtr> *ppFetchRequestSent;

    ThreadID _tid;

  public:
    /** Fetch constructor. */
    Fetch(CPU *_cpu, const BaseRxuO3CPUParams &params);

    /** Returns the name of fetch. */
    std::string name() const;

    bool useuopcache = false;

    /** Registers probes. */
    void regProbePoints();

    /** Sets the main backwards communication time buffer pointer. */
    void setTimeBuffer(TimeBuffer<TimeStruct> *time_buffer);

    /** Sets pointer to list of active threads. */
    void setActiveThreads(std::list<ThreadID> *at_ptr);

    /** Sets pointer to time buffer used to communicate to the next stage. */
    void setFetchQueue(TimeBuffer<FetchStruct> *fq_ptr);

    // void setBPUQueue(TimeBuffer<FetchStruct> *bq_qtr);

    /** Initialize stage. */
    void startupStage();

    /** Clear all thread-specific states*/
    void clearStates(ThreadID tid);

    /** Handles retrying the fetch access. */
    void recvReqRetry();

    /** Processes cache completion event. */
    void processCacheCompletion(PacketPtr pkt);

    /** Resume after a drain. */
    void drainResume();

    /** Perform sanity checks after a drain. */
    void drainSanityCheck() const;

    /** Has the stage drained? */
    bool isDrained() const;

    /** Takes over from another CPU's thread. */
    void takeOverFrom();

    /**
     * Stall the fetch stage after reaching a safe drain point.
     *
     * The CPU uses this method to stop fetching instructions from a
     * thread that has been drained. The drain stall is different from
     * all other stalls in that it is signaled instantly from the
     * commit stage (without the normal communication delay) when it
     * has reached a safe point to drain from.
     */
    void drainStall(ThreadID tid);

    /** Tells fetch to wake up from a quiesce instruction. */
    void wakeFromQuiesce();

    /** For priority-based fetch policies, need to keep update priorityList */
    void deactivateThread(ThreadID tid);
  private:
    /** Reset this pipeline stage */
    void resetStage();

    /** Changes the status of this stage to active, and indicates this
     * to the CPU.
     */
    void switchToActive();

    /** Changes the status of this stage to inactive, and indicates
     * this to the CPU.
     */
    void switchToInactive();

    /**
     * Looks up in the branch predictor to see if the next PC should be
     * either next PC+=MachInst or a branch target.
     * @param next_PC Next PC variable passed in by reference.  It is
     * expected to be set to the current PC; it will be updated with what
     * the next PC will be.
     * @param next_NPC Used for ISAs which use delay slots.
     * @return Whether or not a branch was predicted as taken.
     */
    bool lookupAndUpdateNextPC(const DynInstPtr &inst, PCStateBase &pc);

    /**
     * Fetches the cache line that contains the fetch PC.  Returns any
     * fault that happened.  Puts the data into the class variable
     * fetchBuffer, which may not hold the entire fetched cache line.
     * @param vaddr The memory address that is being fetched from.
     * @param ret_fault The fault reference that will be set to the result of
     * the icache access.
     * @param tid Thread id.
     * @param pc The actual PC of the current instruction.
     * @return Any fault that occured.
     */
    bool fetchCacheLine(Addr vaddr, ThreadID tid, Addr pc);
    void finishTranslation(const Fault &fault, const RequestPtr &mem_req);


    /** Check if an interrupt is pending and that we need to handle
     */
    bool checkInterrupt(Addr pc) { return interruptPending; }

    /** Squashes a specific thread and resets the PC. Also tells the CPU to
     * remove any instructions between fetch and decode
     *  that should be sqaushed.
     */
    void squashFromDecode(const PCStateBase &new_pc,
                          const DynInstPtr squashInst,
                          const InstSeqNum seq_num, ThreadID tid);

    /** Checks if a thread is stalled. */
    bool checkStall(ThreadID tid) const;

    /** Updates overall fetch stage status; to be called at the end of each
     * cycle. */
    FetchStatus updateFetchStatus();

// ---------- created by longting.du ---------------
    bool isCurrWindowPC(Addr _pc, Addr _begin) {
        if (_pc >= _begin && _pc <= _begin + 64 * 4) {
            return (_pc - _begin) % 64 == 0;
        }
        return false;
    }
// -------------------------------------------------

  public:
    /** Squashes a specific thread and resets the PC. */
    void doSquash(const PCStateBase &new_pc, const DynInstPtr squashInst,
            ThreadID tid);

    /** Squashes a specific thread and resets the PC. Also tells the CPU to
     * remove any instructions that are not in the ROB. The source of this
     * squash should be the commit stage.
     */
    void squash(const PCStateBase &new_pc, const InstSeqNum seq_num,
                DynInstPtr squashInst, ThreadID tid);

    /** Ticks the fetch stage, processing all inputs signals and fetching
     * as many instructions as possible.
     */
    void tick();

    /** Checks all input signals and updates the status as necessary.
     *  @return: Returns if the status has changed due to input signals.
     */
    bool checkSignalsAndUpdate(ThreadID tid);

    /** Does the actual fetching of instructions and passing them on to the
     * next stage.
     * @param status_change fetch() sets this variable if there was a status
     * change (ie switching to IcacheMissStall).
     */
    void fetch(bool &status_change);

    /** Align a PC to the start of a fetch buffer block. */
    Addr fetchBufferAlignPC(Addr addr)
    {
        return (addr & ~(fetchBufferMask));
    }

    Addr fetchBufferAlignPC_half(Addr addr)
    {
        return (addr & ~(((fetchBufferMask + 1) >> 1) - 1));
    }

    /** The decoder. */
    InstDecoder *decoder[MaxThreads];

    RequestPort &getInstPort() { return icachePort; }

    // branch_prediction::BPredUnit *getBPU() { return branchPred; }

// ---------- created by longting.du ---------------
    void setUC(UopCache *_uc) { uc = _uc; }

    void lookupUopCache(const PCStateBase &pc, bool taken);

    void clearUC();

    void turnOffIC(ThreadID tid) {
        memReq[tid] = NULL;
    }

    void cancelSquash(PCStateBase &right_pc,
                      bool hit);
// -----------------------------------------------------

// ---------- created by lian.wang ---------------
    void lookupIsCam(const PCStateBase &_pc, bool taken); 
// -----------------------------------------------------
  private:
    DynInstPtr buildInst(ThreadID tid, StaticInstPtr staticInst,
            StaticInstPtr curMacroop, const PCStateBase &this_pc,
            const PCStateBase &next_pc, bool trace);

    /** Returns the appropriate thread to fetch, given the fetch policy. */
    ThreadID getFetchingThread();

    /** Returns the appropriate thread to fetch using a round robin policy. */
    ThreadID roundRobin();

    // /** Returns the appropriate thread to fetch using the IQ count policy. */
    // ThreadID iqCount();

    // /** Returns the appropriate thread to fetch using the LSQ count policy. */
    // ThreadID lsqCount();

    /** Returns the appropriate thread to fetch using the branch count
     * policy. */
    ThreadID branchCount();

    /** Pipeline the next I-cache access to the current one. */
    void pipelineIcacheAccesses(ThreadID tid);

    /** Profile the reasons of fetch stall. */
    void profileStall(ThreadID tid);

    void removeAlltoRestore(); 

// ---------- created by longting.du ---------------
    Addr alignHalfPC(Addr addr) {
      Addr base16 = addr & ~(64 - 1);
      Addr base8 = addr & ~(32 - 1);
      // if (base16 < base8) 
      //     ++fetchStats.postJointPre;
      return base8 > base16 ? base8 : base16;
    };
// -----------------------------------------------------

  public:
    /** Pointer to the RxuO3CPU. */
    CPU *cpu;

// ---------- created by longting.du ---------------
    UopCache *uc;
// ------------------------------------------------
// ---------- created by lian.wang ---------------
    Cam *cam;

    bool camsend;
// ------------------------------------------------

    /** Time buffer interface. */
    TimeBuffer<TimeStruct> *timeBuffer;

    /** Wire to get decode's information from backwards time buffer. */
    TimeBuffer<TimeStruct>::wire fromDecode;

// ---------- created by longting.du ---------------
    /** Wire to get BPU's information from backwards time buffer. */
    TimeBuffer<TimeStruct>::wire fromBP0;
    TimeBuffer<TimeStruct>::wire fromBP1;
// --------------------------------------------------

    // -- add by hongfei.liu --------------------------------------------
    /** Wire to get predisq's information from backwards time buffer. */
    // TimeBuffer<TimeStruct>::wire fromPredisq;
    // ------------------------------------------------------------------

    // /** Wire to get rename's information from backwards time buffer. */
    // TimeBuffer<TimeStruct>::wire fromRename;

    /** Wire to get ew's information from backwards time buffer. */
    // TimeBuffer<TimeStruct>::wire fromEW;

    /** Wire to get commit's information from backwards time buffer. */
    TimeBuffer<TimeStruct>::wire fromCommit;

// ---------- created by longting.du ---------------
    TimeBuffer<FetchStruct>::wire toBP0;

// ---------- created by longting.du -----------------
  public:

    UopCache::HitInfo ucHit;

    UopCache::HitInfo reHit;

    std::list<DynInstPtr> instsFromUC;

    Addr ucBlockLookupAddr;

    Addr ucBlockResponseAddr;

    Addr ucBlockStoreAddr;

    std::list<StaticInstPtr> ucacheLineData;
// ----------------------------------------------------
  // ---------- created by lian.wang -----------------
    std::list<DynInstPtr> instsFromCAM;

    Cam::HitInfo camHit;

// ----------------------------------------------------  
  public:
    // /** BPredUnit. */
    // branch_prediction::BPredUnit *branchPred;

    StaticInstPtr macroop[MaxThreads];

    /** Can the fetch stage redirect from an interrupt on this instruction? */
    bool delayedCommit[MaxThreads];

    /** Memory request used to access cache. */
    RequestPtr memReq[MaxThreads];

    RequestPtr anotherMemReq[MaxThreads];

    PacketPtr firstPkt[MaxThreads];

    PacketPtr secondPkt[MaxThreads];

    /** Variable that tracks if fetch has written to the time buffer this
     * cycle. Used to tell CPU if there is activity this cycle.
     */
    bool wroteToTimeBuffer;

    /** Tracks how many instructions has been fetched this cycle. */
    int numInst;

    /** Source of possible stalls. */
    struct Stalls
    {
        bool drain;
        bool bp;
    };

    /** Tracks which stages are telling fetch to stall. */
    Stalls stalls[MaxThreads];

    /** Decode to fetch delay. */
    Cycles decodeToFetchDelay;

// ---------- created by longting.du ---------------
    /** BP0 to fetch delay. */
    Cycles BPUToFetchDelay;
// -------------------------------------------------

    // -- add by hongfei.liu ------------
    /** Predisq to fetch delay. */
    // Cycles predisqToFetchDelay;
    // ----------------------------------

    // /** Rename to fetch delay. */
    // Cycles renameToFetchDelay;

    /** EW to fetch delay. */
    // Cycles ewToFetchDelay;

    /** Commit to fetch delay. */
    Cycles commitToFetchDelay;

    /** The width of fetch in instructions. */
    unsigned fetchWidth;

    unsigned bpuWidth;

    /** Is the cache blocked?  If so no threads can access it. */
    bool cacheBlocked;

    /** The packet that is waiting to be retried. */
    PacketPtr retryPkt;

    /** The thread that is waiting on the cache to tell fetch to retry. */
    ThreadID retryTid;

    /** Cache block size. */
    unsigned int cacheBlkSize;

    /** The size of the fetch buffer in bytes. The fetch buffer
     *  itself may be smaller than a cache line.
     */
    unsigned fetchBufferSize;

    /** Mask to align a fetch address to a fetch buffer boundary. */
    Addr fetchBufferMask;

    /** The fetch data that is being fetched and buffered. */
    uint8_t *fetchBuffer[MaxThreads];

    /** The PC of the first instruction loaded into the fetch buffer. */
    Addr fetchBufferPC[MaxThreads];

    /** Indicating whether the fetch request is mis-aligned*/
    bool fetchMisaligned[MaxThreads];

    /** The size of the fetch queue in micro-ops */
    unsigned fetchQueueSize;

    /** Queue of fetched instructions. Per-thread to prevent HoL blocking. */
    std::list<DynInstPtr> fetchQueue[MaxThreads];

    // std::unordered_map<Addr, StaticInstPtr> streamBuffer;

    // std::deque<DynInstPtr> restoreQueue;

    /** Whether or not the fetch buffer data is valid. */
    bool fetchBufferValid[MaxThreads];

    /** Size of instructions. */
    int instSize;

    /** Icache stall statistics. */
    Counter lastIcacheStall[MaxThreads];

    /** List of Active Threads */
    std::list<ThreadID> *activeThreads;

    /** Number of threads. */
    ThreadID numThreads;

    /** Number of threads that are actively fetching. */
    ThreadID numFetchingThreads;

    /** Thread ID being fetched. */
    ThreadID threadFetched;

    /** Checks if there is an interrupt pending.  If there is, fetch
     * must stop once it is not fetching PAL instructions.
     */
    bool interruptPending;

    /** Instruction port. Note that it has to appear after the fetch stage. */
    IcachePort icachePort;

    /** Set to true if a pipelined I-cache request should be issued. */
    bool issuePipelinedIfetch[MaxThreads];

    /** Event used to delay fault generation of translation faults */
    FinishTranslationEvent finishTranslationEvent;

    unsigned BPUdelay;
    unsigned initBPUDelay;
    unsigned bubbles = 0;

    std::unique_ptr<PCStateBase> pc[MaxThreads];

// ---------- created by longting.du ---------------
    /** last fetch pc. */
    std::unique_ptr<PCStateBase> lpc[MaxThreads];
// --------------------------------------------------

    Addr fetchOffset[MaxThreads];

    // bool loopBufferActive;

    // bool waitBPU;

    /** reset vl vtype vxrm value[0]. */
    int resetCnt = 0;   

  protected:
    struct FetchStatGroup : public statistics::Group
    {
        FetchStatGroup(CPU *cpu, Fetch *fetch);
        // @todo: Consider making these
        // vectors and tracking on a per thread basis.
        /** Stat for total number of predicted branches. */
        statistics::Scalar predictedBranches;
        /** Stat for total number of cycles spent fetching. */
        statistics::Scalar cycles;
        /** Stat for total number of cycles spent squashing. */
        statistics::Scalar squashCycles;
        /** Stat for total number of cycles spent waiting for translation */
        statistics::Scalar tlbCycles;
        /** Stat for total number of cycles
         *  spent blocked due to other stages in
         * the pipeline.
         */
        statistics::Scalar idleCycles;
        /** Total number of cycles spent blocked. */
        statistics::Scalar blockedCycles;
        /** Total number of cycles spent in any other state. */
        statistics::Scalar miscStallCycles;
        /** Total number of cycles spent in waiting for drains. */
        statistics::Scalar pendingDrainCycles;
        /** Total number of stall cycles caused by no active threads to run. */
        statistics::Scalar noActiveThreadStallCycles;
        /** Total number of stall cycles caused by pending traps. */
        statistics::Scalar pendingTrapStallCycles;
        /** Total number of stall cycles
         *  caused by pending quiesce instructions. */
        statistics::Scalar pendingQuiesceStallCycles;
        /** Total number of stall cycles caused by I-cache wait retrys. */
        statistics::Scalar icacheWaitRetryStallCycles;
        /** Stat for total number of fetched cache lines. */
        statistics::Scalar cacheLines;
        /** Total number of outstanding icache accesses that were dropped
         * due to a squash.
         */
        statistics::Scalar icacheSquashes;
        /** Total number of outstanding tlb accesses that were dropped
         * due to a squash.
         */
        statistics::Scalar tlbSquashes;
        /** Distribution of number of instructions fetched each cycle. */
        statistics::Distribution nisnDist;
        /** Rate of how often fetch was idle. */
        statistics::Formula idleRate;

        statistics::Scalar sendCachelineCycles;
        statistics::Scalar noCachelineCycles;
        statistics::Scalar stallCycles;

        statistics::Formula sendCachelineRate;
        statistics::Formula noCachelineRate;
        statistics::Formula stallRate;

        statistics::Distribution bubbleCount;
        statistics::Distribution fetchOutInsts;

        statistics::Scalar uopCacheHitCount;
        statistics::Scalar uopCacheMissCount;

        statistics::Scalar CAMHitCount;
        statistics::Scalar CAMMissCount;
        
        statistics::Scalar CAM_cl0_hitcount;
        statistics::Scalar CAM_cl1_hitcount;        
    } fetchStats;

  private:
    uint8_t* firstDataBuf;
    uint8_t* secondDataBuf;
};

} // namespace rxuo3
} // namespace gem5

#endif //__CPU_RxuO3_FETCH_HH__
