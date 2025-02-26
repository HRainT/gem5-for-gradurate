// ----------- added by longting.du --------------//

#ifndef __CPU_RxuO3_RENAME_UNIT_HH__
#define __CPU_RxuO3_RENAME_UNIT_HH__

#include <list>
#include <utility>
#include <vector>
#include <any>

#include "base/statistics.hh"
#include "cpu/rxuo3/comm.hh"
#include "cpu/rxuo3/dyn_inst_ptr.hh"
#include "cpu/rxuo3/rxu_free_list.hh"
#include "cpu/rxuo3/limits.hh"
#include "cpu/timebuf.hh"
#include "sim/probe/probe.hh"
#include "base/logging.hh"
#include "base/trace.hh"
#include "cpu/reg_class.hh"
#include "arch/riscv/pcstate.hh"

namespace gem5
{

struct BaseRxuO3CPUParams;
using RenameInfo = std::pair<PhysRegIdPtr, PhysRegIdPtr>;

using Arch2PhysMap = std::vector<PhysRegIdPtr>;

namespace rxuo3
{

class CPU;
class RxuFRMRename;
class Scoreboard;
class RxuUnifiedVecRename;

class RenameUnit
{
private:
    // const int renameWidth;

    /** Probe points. */
    typedef std::pair<InstSeqNum, PhysRegIdPtr> SeqNumRegPair;
    /** To probe when register renaming for an instruction is complete */
    ProbePointArg<DynInstPtr> *ppRename;
    /**
     * To probe when an instruction is squashed and the register mapping
     * for it needs to be undone
     */
    ProbePointArg<SeqNumRegPair> *ppSquashInRename;

public:
    /** Rename constructor. */
    RenameUnit(CPU *_cpu, const BaseRxuO3CPUParams &params);

    /** Returns the name of rename. */
    std::string name() const;

    /** Registers probes. */
    void regProbePoints();

    /** Sets pointer to rename maps (per-thread structures). */
    void setRenameMap(UnifiedRenameMap rm_ptr[MaxThreads]);

    void setFreeList(RxuUnifiedFreeList *fl_ptr);

    void setFrm(RxuFRMRename *frm_ptr);

    /** vector csr rename. */
    void setVecCsr(RxuUnifiedVecRename *vec_csr_ptr);

    std::vector<bool> checkVecCsrRenameStall();

    /** check vlvtype ready */
    bool checkVlVtypeReady(const DynInstPtr &inst);

    void getVlVtype(const DynInstPtr &inst);

    RiscvISA::VTYPE vtypeTransfer(uint64_t vtype);

    void writeImmVl(const DynInstPtr &inst, ThreadID tid);

    void writeImmVtype(const DynInstPtr &inst, ThreadID tid);

    /** microvector inherit macrovector rename mapping*/
    void microInheritMacroRename(const DynInstPtr &inst, int src_idx);

    void setScoreboard(Scoreboard *_scoreboard);

    RenameInfo rename(RegId arch_reg, unsigned inst_index);

    PhysRegIdPtr lookup(RegId arch_reg);

    void setEntry(const RegId arch_reg, PhysRegIdPtr phys_reg);

    /** Executes actual squash, removing squashed instructions. */
    void doSquash(const InstSeqNum &squashed_seq_num, ThreadID tid);

    /** Removes a committed instruction's rename history. */
    void removeFromHistory(InstSeqNum inst_seq_num, ThreadID tid);

    /** Renames the destination registers of an instruction. */
    bool renameDestRegs(const DynInstPtr &inst, ThreadID tid, unsigned inst_index);

    /** Renames the source registers of an instruction. */
    void renameSrcRegs(const DynInstPtr &inst, ThreadID tid);

    void recoverAndRelease();

    std::vector<bool> checkScalarRenameStall();

    std::vector<bool> checkVecRenameStall();

    bool checkFRMRenameStall();

    /** Debugging function used to dump history buffer of renamings. */
    void dumpHistory();

    /** Holds the information for each destination register rename. It holds
     * the instruction's sequence number, the arch register, the old physical
     * register for that arch. register, and the new physical register.
     */
    struct RenameHistory
    {
        RenameHistory(InstSeqNum _instSeqNum, const RegId &_archReg,
                        PhysRegIdPtr _newPhysReg,
                        PhysRegIdPtr _prevPhysReg,
                        bool _isMacroVector)
            : instSeqNum(_instSeqNum), archReg(_archReg),
                newPhysReg(_newPhysReg), prevPhysReg(_prevPhysReg),isMacroVector(_isMacroVector)
        {
        }

        /** The sequence number of the instruction that renamed. */
        InstSeqNum instSeqNum;
        /** The architectural register index that was renamed. */
        RegId archReg;
        /** The new physical register that the arch. register is renamed to. */
        PhysRegIdPtr newPhysReg;
        /** The old physical register that the arch. register was renamed to.
         */
        PhysRegIdPtr prevPhysReg;
        //
        bool isMacroVector;
    };

    struct FRMRenameHistory
    {
        FRMRenameHistory(InstSeqNum _instSeqNum, unsigned _newPhysReg, unsigned _prevPhysReg)
            : instSeqNum(_instSeqNum), newPhysReg(_newPhysReg), prevPhysReg(_prevPhysReg)
        {
        }

        /** The sequence number of the instruction that renamed. */
        InstSeqNum instSeqNum;
        /** The architectural register index that was renamed. */
        // RegId archReg;
        /** The new physical register that the arch. register is renamed to. */
        unsigned newPhysReg;
        /** The old physical register that the arch. register was renamed to.
         */
        unsigned prevPhysReg;
    };

    //vector csr vxrm renamehistory
    struct VxrmRenameHistory
    {
        VxrmRenameHistory(InstSeqNum _instSeqNum, unsigned _newPhysReg, unsigned _prevPhysReg)
            : instSeqNum(_instSeqNum), newPhysReg(_newPhysReg), prevPhysReg(_prevPhysReg)
        {
        }

        /** The sequence number of the instruction that renamed. */
        InstSeqNum instSeqNum;
        /** The architectural register index that was renamed. */
        // RegId archReg;
        /** The new physical register that the arch. register is renamed to. */
        unsigned newPhysReg;
        /** The old physical register that the arch. register was renamed to.
         */
        unsigned prevPhysReg;
    };

    //vector csr vl renamehistory
    struct VlRenameHistory
    {
        VlRenameHistory(InstSeqNum _instSeqNum, unsigned _newPhysReg, unsigned _prevPhysReg)
            : instSeqNum(_instSeqNum), newPhysReg(_newPhysReg), prevPhysReg(_prevPhysReg)
        {
        }

        /** The sequence number of the instruction that renamed. */
        InstSeqNum instSeqNum;
        /** The architectural register index that was renamed. */
        // RegId archReg;
        /** The new physical register that the arch. register is renamed to. */
        unsigned newPhysReg;
        /** The old physical register that the arch. register was renamed to.
         */
        unsigned prevPhysReg;
    };

    //vector csr vtype renamehistory
    struct VtypeRenameHistory
    {
        VtypeRenameHistory(InstSeqNum _instSeqNum, unsigned _newPhysReg, unsigned _prevPhysReg)
            : instSeqNum(_instSeqNum), newPhysReg(_newPhysReg), prevPhysReg(_prevPhysReg)
        {
        }

        /** The sequence number of the instruction that renamed. */
        InstSeqNum instSeqNum;
        /** The architectural register index that was renamed. */
        // RegId archReg;
        /** The new physical register that the arch. register is renamed to. */
        unsigned newPhysReg;
        /** The old physical register that the arch. register was renamed to.
         */
        unsigned prevPhysReg;
    };

    /** A per-thread list of all destination register renames, used to either
     * undo rename mappings or free old physical registers.
     */
    std::list<RenameHistory> historyBuffer[MaxThreads];

    std::list<FRMRenameHistory> frmHistoryBuffer[MaxThreads];

    std::list<VxrmRenameHistory> vxrmHistoryBuffer[MaxThreads];

    std::list<VlRenameHistory> vlHistoryBuffer[MaxThreads];

    std::list<VtypeRenameHistory> vtypeHistoryBuffer[MaxThreads];

    //microvector add to history buffer
    void microAddToHistory(RenameHistory hb_entry, const DynInstPtr &inst, ThreadID tid);

    std::list<RenameHistory>::iterator micro_it = historyBuffer[0].begin();

    //microvector history buffer
    std::list<RenameHistory> microHistoryBuffer[MaxThreads];

    /** Pointer to CPU. */
    CPU *cpu;

    /** Rename map interface. */
    UnifiedRenameMap *renameMap[MaxThreads];

    /** The acutal arch-to-phys register map */
    Arch2PhysMap map;

    /** Free list interface. */
    RxuUnifiedFreeList *freeList;

    RxuFRMRename *frm;

    /** vector csr rename. */
    RxuUnifiedVecRename *vecCsr;

    /** Pointer to the list of active threads. */
    std::list<ThreadID> *activeThreads;

    /** Pointer to the scoreboard. */
    Scoreboard *scoreboard;

    // RenameTop *rename_top;

    /** The number of threads active in rename. */
    ThreadID numThreads;

    /**
     * The register file object is used only to get PhysRegIdPtr
     * on MiscRegs, as they are stored in it.
     */
    PhysRegFile *regFile;

    // struct RenameStats : public statistics::Group
    // {
    //     RenameStats(statistics::Group *parent);

    //     /** Stat for total number of cycles spent running normally. */
    //     statistics::Scalar runCycles;
    //     /** Stat for total number of cycles spent unblocking. */
    //     statistics::Scalar unblockCycles;
    //     /** Stat for total number of renamed instructions. */
    //     statistics::Scalar renamedInsts;
    //     /** Stat for total number of squashed instructions that rename
    //      * discards. */
    //     statistics::Scalar squashedInsts;
    //     /** Stat for total number of times that rename runs out of free
    //      *  registers to use to rename. */
    //     statistics::Scalar fullRegistersEvents;
    //     /** Stat for total number of renamed destination registers. */
    //     statistics::Scalar renamedOperands;
    //     /** Stat for total number of source register rename lookups. */
    //     statistics::Scalar lookups;
    //     statistics::Scalar intLookups;
    //     statistics::Scalar fpLookups;
    //     statistics::Scalar vecLookups;
    //     statistics::Scalar vecPredLookups;
    //     statistics::Scalar matLookups;
    //     /** Stat for total number of committed renaming mappings. */
    //     statistics::Scalar committedMaps;
    //     /** Stat for total number of mappings that were undone due to a
    //      *  squash. */
    //     statistics::Scalar undoneMaps;
    // } stats;
};



} // namespace rxuo3

} // namespace gem5

#endif // __CPU_RxuO3_RENAME_UNIT_HH__
