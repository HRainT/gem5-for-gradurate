#ifndef __CPU_RxuO3_COMM_HH__
#define __CPU_RxuO3_COMM_HH__

#include <vector>

#include "arch/generic/pcstate.hh"
#include "base/types.hh"
#include "cpu/inst_seq.hh"
#include "cpu/rxuo3/dyn_inst_ptr.hh"
#include "cpu/rxuo3/limits.hh"
#include "sim/faults.hh"

namespace gem5
{

namespace rxuo3
{

/** Struct that defines the information passed from fetch to IBandLB. */
struct FetchStruct
{
    int size;

    DynInstPtr insts[MaxWidth];
    Fault fetchFault;
    InstSeqNum fetchFaultSN;
    bool clearFetchFault;

    int static_size;
    Addr pc;
    Addr pc_insts[MaxWidth];
    StaticInstPtr static_insts[MaxWidth];
};

/** Struct that defines the information passed from bpu0 to bpu1. */
struct Bpu0Struct
{
    int size;

    DynInstPtr insts[MaxWidth];

    int static_size;
    Addr pc;
    Addr pc_insts[MaxWidth];
    StaticInstPtr static_insts[MaxWidth];
};

/** Struct that defines the information passed from bpu1 to IBandLB. */
struct Bpu1Struct
{
    int size;

    DynInstPtr insts[MaxWidth];
};

/** Struct that defines the information passed from IBandLB to decode. */
struct IBandLBStruct
{
    int size;

    DynInstPtr insts[MaxWidth];
};


/** Struct that defines the information passed from decode to predisq. */
struct DecodeStruct
{
    int size;
    bool active;

    DynInstPtr insts[MaxWidth];
};

/** Struct that defines the information passed from predisq to dispipe0. */
struct PredisqStruct
{

    int size;

    DynInstPtr insts[MaxWidth];
};

/** Struct that defines the information passed from dispipe0 to dispipe1. */
struct Dispipe0Struct
{

    int size;

    DynInstPtr insts[MaxWidth];
};

/** Struct that defines the information passed from dispipe0 to Commit. */
struct Dispipe0ToRobStruct
{

    int size;

    DynInstPtr insts[MaxWidth];

    int sizeVec;
};


/** Struct that defines the information passed from dispipe1 to dispipe2. */
struct Dispipe1Struct
{

    int size;

    DynInstPtr insts[MaxWidth];
};

/** Struct that defines the information passed from dispipe2 to dispipe3. */
struct Dispipe2Struct
{

    int size;

    DynInstPtr insts[MaxWidth];
};

/** Struct that defines the information passed from dispipe3 to ew. */
struct WakeStruct
{

    int size;

    DynInstPtr insts[MaxWidth];
};

/** Struct that defines the information passed from EW to commit. */
struct EWStruct
{
    int size;

    DynInstPtr insts[MaxWidth];
    DynInstPtr mispredictInst[MaxThreads];
    Addr mispredPC[MaxThreads];
    InstSeqNum squashedSeqNum[MaxThreads];
    std::unique_ptr<PCStateBase> pc[MaxThreads];

    bool squash[MaxThreads];
    bool branchMispredict[MaxThreads];
    bool branchTaken[MaxThreads];
    bool includeSquashInst[MaxThreads];
};

struct IssueStruct
{
    int size;

    DynInstPtr insts[MaxWidth];
};

struct SquashVersion
{
    uint8_t version;
    const static uint8_t versionLimit = 16;
    const static uint8_t maxVersion = versionLimit - 1;
    const static uint8_t maxInflightSquash = 7;
    uint8_t getVersion() const {
        return version;
    }
    uint8_t nextVersion() const {
        return (version + 1) % versionLimit;
    }
    bool largerThan(uint8_t other) const {
        bool larger = version > other && version - other <= maxInflightSquash;
        bool wrapped_larger =
            version + versionLimit > other &&
            version + versionLimit - other <= maxInflightSquash;
        if (!(larger || wrapped_larger || (version == other))) {
            panic("SquashVersion: %d, other: %d\n", version, other);
        }
        return larger || wrapped_larger;
    }
    void update(uint8_t v) {
        version = v;
    }
};

/** Struct that defines all backwards communication. */
struct TimeStruct
{
    struct Bpu0Comm 
    {        
        std::unique_ptr<PCStateBase> pc;
        DynInstPtr branchInst;
        InstSeqNum doneSeqNum;

        bool squash;
        bool branchTaken;     
    };

    Bpu0Comm bpu0Info[MaxThreads];

    struct Bpu1Comm 
    {
        std::unique_ptr<PCStateBase> pc;
        DynInstPtr branchInst;
        InstSeqNum doneSeqNum;

        bool squash;
        bool branchTaken;
    };

    Bpu1Comm bpu1Info[MaxThreads];

    struct IBandLBComm 
    {
        unsigned freeEntries;
    };

    IBandLBComm IBandLBInfo[MaxThreads];

    struct DecodeComm
    {
        std::unique_ptr<PCStateBase> nextPC;
        DynInstPtr mispredictInst;
        DynInstPtr squashInst;
        InstSeqNum doneSeqNum;
        Addr mispredPC;
        uint64_t branchAddr;
        unsigned branchCount;
        bool squash;
        bool predIncorrect;
        bool branchMispredict;
        bool branchTaken;
    };

    DecodeComm decodeInfo[MaxThreads];


    struct PredisqComm 
    {
        unsigned freeGroups;
        unsigned freeEntries;
    };

    PredisqComm PredisqInfo[MaxThreads];


    struct Dispipe0Comm {
        bool vecQueueFull;
        bool vecRenameStall;
    };

    Dispipe0Comm Dispipe0Info[MaxThreads];

    struct Dispipe1Comm {
        int wtbInFlight[28];
    };

    Dispipe1Comm Dispipe1Info[MaxThreads];

    struct Dispipe2Comm 
    {
        int wtbInFlight[28];
    };

    Dispipe2Comm Dispipe2Info[MaxThreads];

    struct Dispipe3Comm 
    {
        int wtbFreeEntries[28];

        bool brInSpecialFull = false;

        int wtbInFlight[28];

        bool fpNormalBusy = false;
    };

    Dispipe3Comm Dispipe3Info[MaxThreads];

    struct EwComm {};

    EwComm ewInfo[MaxThreads];

    struct CommitComm
    {
        /////////////////////////////////////////////////////////////////////
        // This code has been re-structured for better packing of variables
        // instead of by stage which is the more logical way to arrange the
        // data.
        // F = Fetch
        // D = Decode
        // I = IEW
        // R = Rename
        // As such each member is annotated with who consumes it
        // e.g. bool variable name // *F,R for Fetch and Rename
        /////////////////////////////////////////////////////////////////////

        /// The pc of the next instruction to execute. This is the next
        /// instruction for a branch mispredict, but the same instruction for
        /// order violation and the like
        std::unique_ptr<PCStateBase> pc; // *F

        /// Provide fetch the instruction that mispredicted, if this
        /// pointer is not-null a misprediction occured
        DynInstPtr mispredictInst;  // *F

        /// Instruction that caused the a non-mispredict squash
        DynInstPtr squashInst; // *F

        /// Hack for now to send back a strictly ordered access to the
        /// IEW stage.
        DynInstPtr strictlyOrderedLoad; // *I

        /// Communication specifically to the IQ to tell the IQ that it can
        /// schedule a non-speculative instruction.
        InstSeqNum nonSpecSeqNum; // *I

        /// Represents the instruction that has either been retired or
        /// squashed.  Similar to having a single bus that broadcasts the
        /// retired or squashed sequence number.
        InstSeqNum doneSeqNum; // *F, I

        /// Tell Rename how many free entries it has in the ROB
        unsigned freeROBEntries; // *R

        /// Tell Rename how many free vector entries it has in the ROB
        unsigned freeVecROBEntries;

        bool squash; // *F, D, R, I
        bool robSquashing; // *F, D, R, I

        /// Rename should re-read number of free rob entries
        bool usedROB; // *R

        /// Notify Rename that the ROB is empty
        bool emptyROB; // *R

        /// Was the branch taken or not
        bool branchTaken; // *F
        /// If an interrupt is pending and fetch should stall
        bool interruptPending; // *F
        /// If the interrupt ended up being cleared before being handled
        bool clearInterrupt; // *F

        /// Hack for now to send back an strictly ordered access to
        /// the IEW stage.
        bool strictlyOrdered; // *I

    };

    CommitComm commitInfo[MaxThreads];

    bool ucBlock[MaxThreads];
    bool ucUnblock[MaxThreads];

    bool bpu0Block[MaxThreads];
    bool bpu0Unblock[MaxThreads];

    bool bpu1Block[MaxThreads];
    bool bpu1Unblock[MaxThreads];

    bool IBandLBBlock[MaxThreads];
    bool IBandLBUnblock[MaxThreads];

    bool decodeBlock[MaxThreads];
    bool decodeUnblock[MaxThreads];

    bool predisqBlock[MaxThreads];
    bool predisqUnblock[MaxThreads];

    bool dispipe0Block[MaxThreads];
    bool dispipe0Unblock[MaxThreads];

    bool dispipe1Block[MaxThreads];
    bool dispipe1Unblock[MaxThreads];

    bool dispipe1VecBlock[MaxThreads];
    bool dispipe1VecUnblock[MaxThreads];

    bool dispipe2Block[MaxThreads];
    bool dispipe2Unblock[MaxThreads];

    bool dispipe3Block[MaxThreads];
    bool dispipe3Unblock[MaxThreads];

    bool ewBlock[MaxThreads];
    bool ewUnblock[MaxThreads];


};

} // namespace rxuo3
} // namespace gem5

#endif //__CPU_RxuO3_COMM_HH__
