#ifndef __CPU_RxuO3_BPU0_HH__
#define __CPU_RxuO3_BPU0_HH__

#include <list>
#include <unordered_map>
#include <set>
#include <map>
#include <vector>
#include "base/intmath.hh"

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
 * BPU0 stage
 */
class Bpu0
{
  public:
    /** Overall bpu0 stage status. Used to determine if the CPU can
     * deschedule itself due to a lack of activity.
     */
    enum Bpu0Status
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
    /** Bpu0 status. */
    Bpu0Status _status;

    /** Per-thread status. */
    ThreadStatus bpu0Status[MaxThreads];
  public:
    /** Per-thread status. */
    bool incrementVector = false;
    uint64_t useReg_ctr[32] = {0};
    int incrementNum = 0;
  public:
    /** Bpu0 constructor. */
    Bpu0(CPU *_cpu, const BaseRxuO3CPUParams &params);

    /** Initialize stage. */
    void startupStage();

    /** Clear all thread-specific states */
    void clearStates(ThreadID tid);

    void resetStage();

    /** Returns the name of bpu0. */
    std::string name() const;

    /** Sets the main backwards communication time buffer pointer. */
    void setTimeBuffer(TimeBuffer<TimeStruct> *tb_ptr);

    /** Sets pointer to time buffer used to communicate to the next stage. */
    void setBpu0Queue(TimeBuffer<Bpu0Struct> *b0q_ptr);

    /** Sets pointer to time buffer coming from fetch. */
    void setFetchQueue(TimeBuffer<FetchStruct> *fq_ptr);

    /** Sets pointer to list of active threads. */
    void setActiveThreads(std::list<ThreadID> *at_ptr);

    /** Perform sanity checks after a drain. */
    void drainSanityCheck() const;

    /** Has the stage drained? */
    bool isDrained() const;

    /** Takes over from another CPU's thread. */
    void takeOverFrom() { resetStage(); }

    /** Ticks bpu0, processing all input signals and processing as many
     * instructions as possible.
     */
    void tick();

    /** Determines what to do based on bpu0's current status.
     * @param status_change bpu0() sets this variable if there was a status
     * change (ie switching from from blocking to unblocking).
     * @param tid Thread id to bpu0 instructions from.
     */
    void bpu0(bool &status_change, ThreadID tid);

    /** process Insts.
     */
    void processInsts(ThreadID tid);
    void statRegCount(uint8_t regNum, Tick cycle);

  private:
    /** Updates overall bpu0 status based on all of the threads' statuses. */
    void updateStatus();

    /** Separates instructions from fetch into individual lists of instructions
     * sorted by thread.
     */
    void sortInsts();

    /** Reads all stall signals from the backwards communication timebuffer. */
    void readStallSignals(ThreadID tid);

    /** Checks all input signals and updates bpu0's status appropriately. */
    bool checkSignalsAndUpdate(ThreadID tid);

    /** Checks all stall signals, and returns if any are true. */
    bool checkStall(ThreadID tid) const;

    /** Returns if there any instructions from fetch on this cycle. */
    bool fetchInstsValid();

    /** Switches bpu0 to blocking, and signals back that bpu0 has
     * become blocked.
     * @return Returns true if there is a status change.
     */
    bool block(ThreadID tid);

    /** Switches bpu0 to unblocking, and
     * signals back that bpu0 has unblocked.
     * @return Returns true if there is a status change.
     */
    bool unblock(ThreadID tid);

  public:
    /** Squashes due to commit signalling a squash. Changes status to
     * squashing and clears block/unblock signals as needed.
     */
    void squash(ThreadID tid, InstSeqNum squashedSeqNum);

    /** Build a DynInst from L0Buffer/Return_cam.
     */
    DynInstPtr buildInst(ThreadID tid, StaticInstPtr staticInst,
            StaticInstPtr curMacroop, const PCStateBase &this_pc,
            const PCStateBase &next_pc, bool trace);

  public:
    // Interfaces to objects outside of bpu0.
    /** CPU interface. */
    CPU *cpu;

    /** Time buffer interface. */
    TimeBuffer<TimeStruct> *timeBuffer;

    /** Wire to get bpu1's output from backwards time buffer. */
    TimeBuffer<TimeStruct>::wire fromBpu1;

    /** Wire to get decode's output from backwards time buffer. */
    TimeBuffer<TimeStruct>::wire fromDecode;

    /** Wire to get commit's information from backwards time buffer. */
    TimeBuffer<TimeStruct>::wire fromCommit;

    /** Wire to write information heading to previous stages. */
    // Might not be the best name as not only fetch will read it.
    TimeBuffer<TimeStruct>::wire toFetch;

    /** Bpu0 instruction queue. */
    TimeBuffer<Bpu0Struct> *bpu0Queue;

    /** Wire used to write any information heading to bpu1. */
    TimeBuffer<Bpu0Struct>::wire toBpu1;

    /** fetch instruction queue interface. */
    TimeBuffer<FetchStruct> *fetchQueue;

    /** Wire to get fetch's output from fetch queue. */
    TimeBuffer<FetchStruct>::wire fromFetch;

    /** A cacheline from fetch. */
    struct fetchCacheline
    {
      Addr first_pc;
      unsigned size = 0;
      DynInstPtr insts[32];

      int static_size = 0;
      Addr pc;
      Addr pc_insts[MaxWidth];
      StaticInstPtr static_insts[32];
    };

    /** Queue of all instructions coming from fetch this cycle. */
    std::list<fetchCacheline> insts[MaxThreads];

    /** Has the entry for L0BTB been used before. */
    bool L0BTB_Used[128] = {false};

    /**
     * L0BTB
    */
    struct BTBEntry {
        bool valid = false;
        uint64_t pc = 0;
        uint64_t tag = 0;
        uint64_t TPC = 0;
        PCStateBase *tpc = nullptr;
        bool instructionsSaved = false;
        StaticInstPtr insts[32];
        PCStateBase *insts_pc[32] ={nullptr};
        unsigned size = 0;

        unsigned init_cnt = 1;
        unsigned corr_cnt = init_cnt;
    };

    struct LookupResult {
        bool hit;
        bool instructionsSaved;
        BTBEntry* entry;
    };

    class L0BTB {
      private:
          static const int NUM_ENTRIES = 128;
          static const int NUM_BANKS = 16;
          static const int NUM_WAYS = 1;
          static const int numSets = NUM_ENTRIES / (NUM_BANKS * NUM_WAYS);
          static const int bankBits = floorLog2(NUM_BANKS);
          static const int entryBits = floorLog2(numSets);
          std::vector<std::vector<std::vector<BTBEntry>>> sets;
          std::vector<std::vector<std::list<int>>> lruQueues;

      public:
          L0BTB() {
              sets.resize(numSets);
              lruQueues.resize(numSets);

              for (int i = 0; i < numSets; i++) {
                sets[i].resize(NUM_BANKS, std::vector<BTBEntry>(NUM_WAYS));
                lruQueues[i].resize(NUM_BANKS, std::list<int>(NUM_WAYS));

                for (int j = 0; j < NUM_BANKS; j++) {
                    int k = 0;
                    for (auto &entryIndex : lruQueues[i][j]) {
                        entryIndex = k++;
                    }
                }
              }
          }

          std::tuple<bool, uint64_t, uint64_t, int> updateEntry(uint64_t pc, uint64_t newTPC, std::unique_ptr<PCStateBase> new_tpc) {
              int bankIndex = (pc >> 2) & ((1 << bankBits) - 1);
              int entryIndex = (pc >> (2 + bankBits)) & ((1 << entryBits) - 1);
              uint64_t tag = pc;

              int globalIndex = entryIndex * NUM_BANKS * NUM_WAYS + bankIndex * NUM_WAYS;

              bool updated = false;
              uint64_t prevPC = 0;
              uint64_t prevTPC = 0;

              for (int way = 0; way < NUM_WAYS; ++way) {
                BTBEntry& entry = sets[entryIndex][bankIndex][way];
                if (entry.valid && entry.tag == tag) {
                    if (entry.pc == pc && entry.TPC == newTPC)
                        return std::make_tuple(updated, prevPC, prevTPC, globalIndex + way);

                    prevPC = entry.pc;
                    prevTPC = entry.TPC;

                    entry.pc = pc;
                    entry.TPC = newTPC;
                    set(entry.tpc, new_tpc);
                    entry.valid = true;
                    entry.tag = tag;
                    entry.instructionsSaved = false;
                    entry.size = 0;

                    updated = true;
                    updateLRU(entryIndex, bankIndex, way);
                    return std::make_tuple(updated, prevPC, prevTPC, globalIndex + way);
                }

              }

              for (int way = 0; way < NUM_WAYS; ++way) {
                BTBEntry& entry = sets[entryIndex][bankIndex][way];
                if (!entry.valid) {
                    entry.pc = pc;
                    entry.TPC = newTPC;
                    set(entry.tpc, new_tpc);
                    entry.valid = true;
                    entry.tag = tag;
                    entry.instructionsSaved = false;
                    entry.size = 0;

                    updated = true;
                    updateLRU(entryIndex, bankIndex, way);
                    return std::make_tuple(updated, prevPC, prevTPC, globalIndex + way);
                }
              }

              int replaceWay = lruQueues[entryIndex][bankIndex].back();
              BTBEntry &entry = sets[entryIndex][bankIndex][replaceWay];
              prevPC = entry.pc;
              prevTPC = entry.TPC;

              entry.pc = pc;
              entry.TPC = newTPC;
              set(entry.tpc, new_tpc);
              entry.valid = true;
              entry.tag = tag;
              entry.instructionsSaved = false;
              entry.size = 0;

              updated = true;
              updateLRU(entryIndex, bankIndex, replaceWay);
              return std::make_tuple(updated, prevPC, prevTPC, globalIndex + replaceWay);
          }

        void saveInstructions(uint64_t pc, StaticInstPtr instructions[], PCStateBase *insts_PC[], int size) {
            int entryIndex = (pc >> (2 + bankBits)) & ((1 << entryBits) - 1);
            int bankIndex = (pc >> 2) & ((1 << bankBits) - 1);
            uint64_t tag = pc;

            for (int way = 0; way < NUM_WAYS; ++way) {
                BTBEntry& entry = sets[entryIndex][bankIndex][way];
                if (entry.valid && entry.tag == tag) {
                    if (!entry.instructionsSaved) {
                        entry.instructionsSaved = true;
                        for (int i = 0; i < size; i++) {
                            entry.insts[i] = instructions[i];
                            set(entry.insts_pc[i], insts_PC[i]);
                        }
                        entry.size = size;
                    }
                    break;
                }
            }
        }

          LookupResult lookup(uint64_t pc) {
              int entryIndex = (pc >> (2 + bankBits)) & ((1 << entryBits) - 1);
              int bankIndex = (pc >> 2) & ((1 << bankBits) - 1);
              uint64_t tag = pc;

              LookupResult result;
              result.hit = false;

              for (int way = 0; way < NUM_WAYS; ++way) {
                  BTBEntry& entry = sets[entryIndex][bankIndex][way];
                  if (entry.valid && entry.tag == tag) {
                      result.hit = true;
                      result.instructionsSaved = entry.instructionsSaved;
                      result.entry = &entry;
                      updateLRU(entryIndex, bankIndex, way);
                      break;
                  }
              }

              return result;
          }

          void clear() {
              for (int set = 0; set < numSets; set++) {
                  for (int bank = 0; bank < NUM_BANKS; bank++) {
                      for (int way = 0; way < NUM_WAYS; way++) {
                          sets[set][bank][way].valid = false;
                          sets[set][bank][way].instructionsSaved = false;
                          sets[set][bank][way].tag = 0;
                          sets[set][bank][way].size = 0;
                      }
                  }
              }
          }

          bool updateTPC(uint64_t pc, uint64_t newTPC, std::unique_ptr<PCStateBase> new_tpc) {
              int entryIndex = (pc >> (2 + bankBits)) & ((1 << entryBits) - 1);
              int bankIndex = (pc >> 2) & ((1 << bankBits) - 1);
              uint64_t tag = pc;

              bool updated = false;

              for (int way = 0; way < NUM_WAYS; ++way) {
                  BTBEntry& entry = sets[entryIndex][bankIndex][way];
                  if (entry.valid && entry.tag == tag) {
                      entry.TPC = newTPC;
                      set(entry.tpc, new_tpc);
                      entry.instructionsSaved = false;
                      updated = true;
                      break;
                  }
              }

              return updated;
          }

          unsigned getEntryCtr(uint64_t pc) {
              int entryIndex = (pc >> (2 + bankBits)) & ((1 << entryBits) - 1);
              int bankIndex = (pc >> 2) & ((1 << bankBits) - 1);
              uint64_t tag = pc;

              for (int way = 0; way < NUM_WAYS; ++way) {
                  BTBEntry& entry = sets[entryIndex][bankIndex][way];
                  if (entry.valid && entry.tag == tag) {
                      return entry.corr_cnt;
                  }
              }

              return 0;
          }

          void entryCtrDown(uint64_t pc) {
              int entryIndex = (pc >> (2 + bankBits)) & ((1 << entryBits) - 1);
              int bankIndex = (pc >> 2) & ((1 << bankBits) - 1);
              uint64_t tag = pc;

              for (int way = 0; way < NUM_WAYS; ++way) {
                  BTBEntry& entry = sets[entryIndex][bankIndex][way];
                  if (entry.valid && entry.tag == tag) {
                      if (entry.corr_cnt)
                          entry.corr_cnt--;
                      break;
                  }
              }
          }

          bool entryCtrReset(uint64_t pc) {
              int entryIndex = (pc >> (2 + bankBits)) & ((1 << entryBits) - 1);
              int bankIndex = (pc >> 2) & ((1 << bankBits) - 1);
              uint64_t tag = pc;

              for (int way = 0; way < NUM_WAYS; ++way) {
                  BTBEntry& entry = sets[entryIndex][bankIndex][way];
                  if (entry.valid && entry.tag == tag) {
                      entry.corr_cnt = entry.init_cnt;
                      return true;
                  }
              }
              return false;
          }

          void invalidateEntry(uint64_t pc) {
              int entryIndex = (pc >> (2 + bankBits)) & ((1 << entryBits) - 1);
              int bankIndex = (pc >> 2) & ((1 << bankBits) - 1);
              uint64_t tag = pc;

              for (int way = 0; way < NUM_WAYS; ++way) {
                  BTBEntry& entry = sets[entryIndex][bankIndex][way];
                  if (entry.valid && entry.tag == tag) {
                      entry.valid = false;
                      entry.instructionsSaved = false;
                      break;
                  }
              }
          }

          void updateLRU(int setIndex, int bankIndex, int accessedWay) {
              std::list<int>& lruQueue = lruQueues[setIndex][bankIndex];

              auto it = std::find(lruQueue.begin(), lruQueue.end(), accessedWay);
              if (it != lruQueue.end()) {
                  lruQueue.splice(lruQueue.end(), lruQueue, it);
              } else {
                  lruQueue.push_back(accessedWay);
              }

              while (lruQueue.size() > NUM_WAYS) {
                  lruQueue.pop_front();
              }
          } 
    };

    L0BTB l0btb;

    /**
     * Return Cam
    */
    
    class Return_Cam {
    private:
        struct ReturnEntry {
            uint64_t TPC;
            PCStateBase *tpc = nullptr;
            bool instructionsSaved;
            StaticInstPtr insts[32];
            PCStateBase *insts_pc[32] ={nullptr};
            unsigned size;

            ReturnEntry(uint64_t TPC, std::unique_ptr<PCStateBase> new_tpc) : 
                    TPC(TPC), instructionsSaved(false), size(0) {
                        set(tpc, new_tpc);
            }
        };

    public:
        struct QueryResult {
            bool hit;
            bool instructionsSaved;
            ReturnEntry* entry;

            QueryResult(bool hit, bool instructionsSaved, ReturnEntry* entry) : hit(hit), instructionsSaved(instructionsSaved), entry(entry) {}
        };

    private:
        std::unordered_map<uint64_t, std::list<ReturnEntry>::iterator> cacheMap;
        std::list<ReturnEntry> cacheList;
        int capacity;

    public:
        Return_Cam(int capacity = 16) : capacity(capacity) {}

        QueryResult query(uint64_t TPC, std::unique_ptr<PCStateBase> new_tpc) {
            if (cacheMap.find(TPC) != cacheMap.end()) {
                auto it = cacheMap[TPC];
                // Cache hit
                updateLRU(it);
                return QueryResult(true, it->instructionsSaved, &(*it));
            }

            // Cache miss
            if (cacheList.size() >= capacity) {
                // Remove the least recently used entry
                uint64_t lru_TPC = cacheList.back().TPC;
                cacheMap.erase(lru_TPC);
                cacheList.pop_back();
            }
            cacheList.push_front(ReturnEntry(TPC, std::move(new_tpc)));
            cacheMap[TPC] = cacheList.begin();
            return QueryResult(false, false, nullptr);
        }

        bool saveInstructions(uint64_t TPC, StaticInstPtr instructions[], PCStateBase *insts_PC[], int size) {
            if (cacheMap.find(TPC) != cacheMap.end()) {
                auto it = cacheMap[TPC];
                if (it->instructionsSaved) {
                    return false;
                }
                for (int i = 0; i < size; ++i) {
                    it->insts[i] = instructions[i];
                    set(it->insts_pc[i], insts_PC[i]);
                }
                it->size = size;
                it->instructionsSaved = true;
                // updateLRU(it);
            }
                
            return false;
        }

        void clear() {
            cacheMap.clear();
            cacheList.clear();
        }

    private:
        void updateLRU(std::list<ReturnEntry>::iterator it) {
            cacheList.splice(cacheList.begin(), cacheList, it);
        }
    };

    Return_Cam return_Cam;

    /** Mapping table between TPC and PC. */
    class TpcPcMapper {
    private:
        std::multimap<Addr, Addr> tpc_to_pc;

    public:
        void addMapping(const Addr& tpc, const Addr& pc) {
            for (auto it = tpc_to_pc.begin(); it != tpc_to_pc.end(); ) {
                if (it->second == pc) {
                    it = tpc_to_pc.erase(it);
                } else {
                    ++it;
                }
            }
            tpc_to_pc.insert({tpc, pc});
        }

        bool removeMapping(const Addr& tpc, const Addr& pc) {
            auto range = tpc_to_pc.equal_range(tpc);
            for (auto it = range.first; it != range.second; ++it) {
                if (it->second == pc) {
                    tpc_to_pc.erase(it);
                    return true;
                }
            }
            return false;
        }

        std::vector<Addr> getPcsForTpcAndRemoveTpc(const Addr& tpc) {
            std::vector<Addr> pcs;
            auto range = tpc_to_pc.equal_range(tpc);
            for (auto it = range.first; it != range.second; ++it) {
                pcs.push_back(it->second);
            }

            tpc_to_pc.erase(tpc);
            return pcs;
        }

        bool hasMapping() const {
            return !tpc_to_pc.empty();
        }
    };

    TpcPcMapper mapper_l0btb;

    /** Queue of all instructions coming from L0BTB/Return_cam this cycle. */
    std::list<DynInstPtr> waitSendInsts[MaxThreads];

    /** Variable that tracks if bpu0 has written to the time buffer this
     * cycle. Used to tell CPU if there is activity this cycle.
     */
    bool wroteToTimeBuffer;

    /** fetch pc. */
    std::unique_ptr<PCStateBase> pc[MaxThreads];

    /** Need to query UopCache this cycle? */
    bool lookupUopCacheFlag;

    /** Need to query UopCache this cycle? */
    bool lookupCamFlag;

    /** L0BTB not taken, BPU taken, waiting squash. */
    bool waitBpuSquash = false;

    /** Source of possible stalls. */
    struct Stalls
    {
        bool bpu1;
        bool IBandLB;
    };

    /** Tracks which stages are telling bpu0 to stall. */
    Stalls stalls[MaxThreads];

    /** Bpu1 to bpu0 delay. */
    Cycles bpu1ToBpu0Delay;

    /** Decode to bpu0 delay. */
    Cycles decodeToBpu0Delay;

    /** Commit to bpu0 delay. */
    Cycles commitToBpu0Delay;

    /** Fetch to bpu0 delay. */
    Cycles fetchToBpu0Delay;

    /** The width of bpu0, in instructions. */
    unsigned bpu0Width;

    /** Index of instructions being sent to bpu1. */
    unsigned toBpu1Index;

    /** number of Active Threads*/
    ThreadID numThreads;

    /** List of active thread ids */
    std::list<ThreadID> *activeThreads;

    /** BPredUnit. */
    branch_prediction::BPredUnit *branchPred;

    /** Record the number of bubbles between two sendings. */
    unsigned bubbles = 0;

    struct Bpu0Stats : public statistics::Group
    {
        Bpu0Stats(CPU *cpu);

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
        /** Stat for total number of bpu0ed instructions. */
        statistics::Scalar bpu0edInsts;
        /** Stat for total number of squashed instructions. */
        statistics::Scalar squashedInsts;
        /** Stat for total times of L0BTB conflict. */
        statistics::Scalar L0BTB_conflict;

        /** Stat for number of branch instructions handled by L0BTB. */
        statistics::Scalar L0BTB_branchInsts;
        /** Stat for number of miss branch instructions handled by L0BTB. */
        statistics::Scalar L0BTB_missInsts;
        /** L0BTB_missRate: L0BTB_missInsts / L0BTB_branchInsts. */
        statistics::Formula L0BTB_missRate;
        /** Number of hit but no have insts branch instructions handled by L0BTB. */
        statistics::Scalar L0BTB_hit_nohaveInsts;

        /** Distribution of number of instructions sent by bpu0. */
        statistics::Distribution cachelineOutInsts;

        /** Number of cycles L0BTB can send a cacheline. */
        statistics::Scalar sendCachelineCycles;
        /** Number of cycles L0BTB can't send a cacheline due to bubble. */
        statistics::Scalar noCachelineCycles;
        /** Number of cycles L0BTB can't send a cacheline due to stall/squash. */
        statistics::Scalar stallCycles;

        /** Rate of L0BTB can send a cacheline. */
        statistics::Formula sendCachelineRate;
        /** Rate of L0BTB can't send a cacheline due to bubble. */
        statistics::Formula noCachelineRate;
        /** Rate of L0BTB can't send a cacheline due to stall/squash. */
        statistics::Formula stallRate;

        /** Distribution of number of bubble detected by bpu0. */
        statistics::Distribution bubbleCount;
        /** Distribution of number of bubble insts detected by bpu0. */
        statistics::Distribution bubbleInstsCount;
        //reg use count
        statistics::Distribution UseReg0Count;
        statistics::Distribution UseReg1Count;
        statistics::Distribution UseReg2Count;
        statistics::Distribution UseReg3Count;
        statistics::Distribution UseReg4Count;
        statistics::Distribution UseReg5Count;
        statistics::Distribution UseReg6Count;
        statistics::Distribution UseReg7Count;
        statistics::Distribution UseReg8Count;
        statistics::Distribution UseReg9Count;
        statistics::Distribution UseReg10Count;
        statistics::Distribution UseReg11Count;
        statistics::Distribution UseReg12Count;
        statistics::Distribution UseReg13Count;
        statistics::Distribution UseReg14Count;
        statistics::Distribution UseReg15Count;
        statistics::Distribution UseReg16Count;
        statistics::Distribution UseReg17Count;
        statistics::Distribution UseReg18Count;
        statistics::Distribution UseReg19Count;
        statistics::Distribution UseReg20Count;
        statistics::Distribution UseReg21Count;
        statistics::Distribution UseReg22Count;
        statistics::Distribution UseReg23Count;
        statistics::Distribution UseReg24Count;
        statistics::Distribution UseReg25Count;
        statistics::Distribution UseReg26Count;
        statistics::Distribution UseReg27Count;
        statistics::Distribution UseReg28Count;
        statistics::Distribution UseReg29Count;
        statistics::Distribution UseReg30Count;
        statistics::Distribution UseReg31Count;

        /** Number of missing return instructions handled by Return_cam. */
        statistics::Scalar Return_cam_missInsts;

        /** Number of branch instructions corrected by Bpu. */
        statistics::Scalar correctByBpu;

        /** Number of entries of L0BTB used. */
        statistics::Scalar L0BTB_Used;
        /** L0BTB Util Ratio. */
        statistics::Formula L0BTB_Util_Rate;

        /** L0BTB/CAM Hit Count. */
        statistics::Scalar L0BTB_CAM_Hit_Count;
        /** L0BTB/CAM Miss Count. */
        statistics::Scalar L0BTB_CAM_Miss_Count;

        /** Number of bubble inst. */
        statistics::Scalar bubbleInsts;
    } stats;
};

} // namespace rxuo3
} // namespace gem5

#endif // __CPU_RxuO3_BPU0_HH__
