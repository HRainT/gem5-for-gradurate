#ifndef __CPU_RxuO3_STORE_SET_HH__
#define __CPU_RxuO3_STORE_SET_HH__

#include <list>
#include <map>
#include <utility>
#include <vector>
#include <unordered_map>

#include "base/types.hh"
#include "cpu/inst_seq.hh"

namespace gem5
{

namespace rxuo3
{

struct ltseqnum
{
    bool
    operator()(const InstSeqNum &lhs, const InstSeqNum &rhs) const
    {
        return lhs > rhs;
    }
};

/**
 * Implements a store set predictor for determining if memory
 * instructions are dependent upon each other.  See paper "Memory
 * Dependence Prediction using Store Sets" by Chrysos and Emer.  SSID
 * stands for Store Set ID, SSIT stands for Store Set ID Table, and
 * LFST is Last Fetched Store Table.
 */
class StoreSet
{
  public:
    typedef unsigned SSID;

    
    /** Last Fetched Store Table. */
    std::vector<std::list<InstSeqNum>> LFST;

    /** Bit vector to tell if the LFST has a valid entry. */
    std::vector<std::unordered_map<InstSeqNum, bool>> validLFST;

  public:
    /** Default constructor.  init() must be called prior to use. */
    StoreSet() { };

    /** Creates store set predictor with given table sizes. */
    StoreSet(uint64_t clear_period, int SSIT_size, int LFST_size);

    /** Default destructor. */
    ~StoreSet();

    /** Initializes the store set predictor with the given table sizes. */
    void init(uint64_t clear_period, int SSIT_size, int LFST_size);

    /** Records a memory ordering violation between the younger load
     * and the older store. */
    void violation(Addr store_PC, Addr load_PC);

    /** Records a memory ordering violation between the younger load
     * and the older store. */
    bool load_if_rbk(Addr load_PC, InstSeqNum load_seq_num);

    /** Clears the store set predictor every so often so that all the
     * entries aren't used and stores are constantly predicted as
     * conflicting.
     */
    void checkClear();

    /** Inserts a load into the store set predictor.  This does nothing but
     * is included in case other predictors require a similar function.
     */
    void insertLoad(Addr load_PC, InstSeqNum load_seq_num);

    /** Inserts a store into the store set predictor.  Updates the
     * LFST if the store has a valid SSID. */
    void insertStore(Addr store_PC, InstSeqNum store_seq_num, ThreadID tid);
    /** Checks if the instruction with the given PC is dependent upon
     * any store.  @return Returns the sequence number of the store
     * instruction this PC is dependent upon.  Returns 0 if none.
     */
    InstSeqNum checkInst(Addr PC);

    /** Records this PC/sequence number as issued. */
    void issued(Addr issued_PC, InstSeqNum issued_seq_num, bool is_store);

    /** Squashes for a specific thread until the given sequence number. */
    void squash(InstSeqNum squashed_num, ThreadID tid);

    /** Resets all tables. */
    void clear();

    /** Debug function to dump the contents of the store list. */
    void dump();
    /** Map of stores that have been inserted into the store set, but
     * not yet issued or squashed.
     */
    std::map<InstSeqNum, int, ltseqnum> storeList;


  private:
    /** Calculates the index into the SSIT based on the PC. */
    inline int calcIndex(Addr PC)
    { return (PC >> offsetBits) & indexMask; }
    // { return (PC >> offsetBits) & indexMask; }

    /** Calculates a Store Set ID based on the PC. */
    inline SSID calcSSID(Addr PC)
    { return ((PC ^ (PC >> 10)) % LFSTSize); }

    /** The Store Set ID Table. */
    std::vector<SSID> SSIT;

    /** Bit vector to tell if the SSIT has a valid entry. */
    std::vector<bool> validSSIT;


    
    typedef std::map<InstSeqNum, int, ltseqnum>::iterator SeqNumMapIt;

    /** Number of loads/stores to process before wiping predictor so all
     * entries don't get saturated
     */
    uint64_t clearPeriod;

    /** Store Set ID Table size, in entries. */
    int SSITSize;

    /** Last Fetched Store Table size, in entries. */
    int LFSTSize;

    /** Mask to obtain the index. */
    int indexMask;

    // HACK: Hardcoded for now.
    int offsetBits;

    /** Number of memory operations predicted since last clear of predictor */
    int memOpsPred;
};

} // namespace rxuo3
} // namespace gem5

#endif // __CPU_RxuO3_STORE_SET_HH__
