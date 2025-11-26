/**
 * Created by longting.du
 */

#ifndef __CPU_RxuO3_UOPCACHE_HH__
#define __CPU_RxuO3_UOPCACHE_HH__

// #include <queue>
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
 * UopCache class handles both single threaded and SMT
 * uc. Its width is specified by the parameters; each cycles it
 * tries to uc that many instructions. Because instructions are
 * actually ucd when the StaticInst is created, this stage does
 * not do much other than check any PC-relative branches.
 */
class UopCache
{
  public:
    /** Overall uc stage status. Used to determine if the CPU can
     * deschedule itself due to a lack of activity.
     */

    struct UopCacheEntry{
        Addr tag;
        bool valid;
        unsigned numInsts;
        int age;
        Addr pc;
        std::list<Addr> pc_list;
        std::list<StaticInstPtr> insts;

        UopCacheEntry() : tag(0), valid(false), numInsts(0), age(0), pc(0) { }

        void clear() {
           tag = 0;
           valid = false;
           age = 0;
           numInsts = 0;
           pc = 0;
           pc_list.clear();
           insts.clear();
        }
    };

    UopCacheEntry *cache;

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

        void more(HitInfo hi) {
          hit = (hi.hit << win_size) | hit;
          win_size += hi.win_size;
          while(hi.info.size() > 0) {
            info.push_back(hi.info.front());
            hi.info.pop_front();
          }
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

    enum ReplacePolicy {
      Random = 0,
      LRU
    };

  public:
    /** UopCache constructor. */
    UopCache(CPU *_cpu, const BaseRxuO3CPUParams &params);

    /** Returns the name of uc. */
    std::string name() const;

    /** Determines what to do based on uc's current status.
     * @param status_change uc() sets this variable if there was a status
     * change (ie switching from from blocking to unblocking).
     * @param tid Thread id to uc instructions from.
     */
    void uc(bool &status_change, ThreadID tid);

    int lindex(Addr pc_in, unsigned instShiftAmt);

    int finallindex(int lindex, int lowPcBits, int way);

    bool update(Addr pc, std::list<StaticInstPtr> &cacheLineData, 
                          std::list<Addr> &pc_list);

    DynInstPtr buildInst(ThreadID tid, StaticInstPtr staticInst,
                                        StaticInstPtr curMacroop, const PCStateBase &this_pc,
                                        const PCStateBase &next_pc, bool trace);

    bool lookupAndUpdateNextPC(const DynInstPtr &inst, PCStateBase &next_pc);

    bool lookup(Addr pc);

    Info lookupOnly(Addr pc);

    Info process(PCStateBase &this_pc);

    HitInfo lookupTwo(Addr pc);

    HitInfo access(PCStateBase &pc, int num);

    void turnOff() {
        // while (!insts.empty())
        // {
        //     insts.pop_front();
        // }
        insts.clear();
        pc_insts.clear();
    } 

    bool useUopCache() { return _useUopCache; }

    unsigned pcShift(Info info, Addr pc) {
      Addr tpc = info.second;
      return tpc - pc;
    }
    
  public:
    /** Squashes due to commit signalling a squash. Changes status to
     * squashing and clears block/unblock signals as needed.
     */
    void squash(ThreadID tid);

    void squash(const DynInstPtr &inst, ThreadID tid);

    void flushUopCache();

    Addr alignPC(Addr addr)
    {   
        Addr base16 = addr & ~(addrMask);
        Addr base8 = addr & ~(addrMask >> 1);
        return base8 > base16 ? base8 : base16;
    }

    Addr align64(Addr addr) {
       return addr & ~(addrMask);
    }

  private:
    // Interfaces to objects outside of uc.
    /** CPU interface. */
    CPU *cpu;

    const unsigned logSize;
    const unsigned tagBits;
    const unsigned setBits;
    const unsigned assocBits;
    const unsigned numInstsEntry;
    const unsigned sets;
    const unsigned assoc;
    const unsigned setMask;
    const unsigned tagMask;
    const unsigned initAge;
    const unsigned shift;

    const Addr addrMask;

    bool _useUopCache = true;

    bool useHashing;

    ReplacePolicy rp;

    /** Queue of all instructions coming from uop this cycle. */
    std::list<StaticInstPtr> insts;
    
    std::list<Addr> pc_insts;

    // std::list<DynInstPtr> instQueue;

    std::unique_ptr<PCStateBase> uc_pc[MaxThreads];

    /** The width of uc, in instructions. */
    unsigned ucWidth;

    /** number of Active Threads*/
    ThreadID numThreads;

    /** List of active thread ids */
    std::list<ThreadID> *activeThreads;

    struct UopCacheStats : public statistics::Group
    {
        UopCacheStats(CPU *cpu);

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
        /** Stat for number of times a branch is resolved at uc. */
        statistics::Scalar branchResolved;
        /** Stat for number of times a branch mispredict is detected. */
        statistics::Scalar branchMispred;
        /** Stat for number of times uc detected a non-control instruction
         * incorrectly predicted as a branch.
         */
        statistics::Scalar controlMispred;
        /** Stat for total number of ucd instructions. */
        statistics::Scalar decodedInsts;
        /** Stat for total number of squashed instructions. */
        statistics::Scalar squashedInsts;
        /* Number of miss times due to BPU_predict in uc. */
        statistics::Scalar bpuMissUopCacheCount;
        /** Stat for total number of hit cycles. */
        statistics::Scalar hitCycles;
        /** Stat for total number of miss cycles. */
        statistics::Scalar missCycles;
        /** Stat for total number of uop cache full. */
        statistics::Scalar cacheFull;
        /** Stat for total number of uop cache lookup. */
        statistics::Scalar lookupCount;
        /** Stat for uop cache hit rate. */
        statistics::Formula hitRate;

        statistics::Scalar cl0_hit_count;
        statistics::Scalar cl1_hit_count;
        statistics::Scalar cl2_hit_count;
        statistics::Scalar cl3_hit_count;

        statistics::Scalar lookup_count;

        statistics::Formula cl0_hitRate;
        statistics::Formula cl1_hitRate;
        statistics::Formula cl2_hitRate;
        statistics::Formula cl3_hitRate;

        statistics::Scalar postJointPre;
        statistics::Formula postJointPreRate;
    } stats;
};

} // namespace rxuo3
} // namespace gem5

#endif
