#ifndef __CPU_RxuO3_DYN_INST_HH__
#define __CPU_RxuO3_DYN_INST_HH__

#include <algorithm>
#include <array>
// #include <deque>
#include <list>
#include <string>

#include "base/refcnt.hh"
#include "base/trace.hh"
#include "config/the_isa.hh"
#include "cpu/checker/cpu.hh"
#include "cpu/exec_context.hh"
#include "cpu/exetrace.hh"
#include "cpu/inst_res.hh"
#include "cpu/inst_seq.hh"
#include "cpu/rxuo3/cpu.hh"
#include "cpu/rxuo3/dyn_inst_ptr.hh"
#include "cpu/rxuo3/dyn_inst_xsmeta.hh"
#include "cpu/rxuo3/lsq_unit.hh"
#include "cpu/rxuo3/veccsr.hh"
#include "cpu/op_class.hh"
#include "cpu/reg_class.hh"
#include "cpu/static_inst.hh"
#include "cpu/translation.hh"
#include "arch/riscv/regs/misc.hh"
#include "debug/HtmCpu.hh"
#include "debug/RxuWTB.hh"
#include "debug/DecoupleBP.hh"
#include "debug/RxuDynInst.hh"
#include "debug/CommitTrace.hh"
#include "debug/RiscvMisc.hh"
#include "arch/riscv/pcstate.hh"

namespace gem5
{

class Packet;

namespace rxuo3
{

class DynInst : public ExecContext, public RefCounted
{
  private:
    DynInst(const StaticInstPtr &staticInst, const StaticInstPtr &macroop,
            InstSeqNum seq_num, CPU *cpu);

  public:
    // The list of instructions iterator type.
    typedef typename std::list<DynInstPtr>::iterator ListIt;

    struct Arrays
    {
        size_t numSrcs;
        size_t numDests;

        RegId *flatDestIdx;
        PhysRegIdPtr *destIdx;
        PhysRegIdPtr *prevDestIdx;
        PhysRegIdPtr *srcIdx;
        uint8_t *readySrcIdx;
    };

    static void *operator new(size_t count, Arrays &arrays);
    static void  operator delete(void* ptr);

    /** BaseDynInst constructor given a binary instruction. */
    DynInst(const Arrays &arrays, const StaticInstPtr &staticInst,
            const StaticInstPtr &macroop, InstSeqNum seq_num, CPU *cpu);

    DynInst(const Arrays &arrays, const StaticInstPtr &staticInst,
            const StaticInstPtr &macroop, const PCStateBase &pc,
            const PCStateBase &pred_pc, InstSeqNum seq_num, CPU *cpu);

    /** BaseDynInst constructor given a static inst pointer. */
    DynInst(const Arrays &arrays, const StaticInstPtr &_staticInst,
            const StaticInstPtr &_macroop);

    ~DynInst();

    /** Executes the instruction.*/
    Fault execute();

    /** Initiates the access.  Only valid for memory operations. */
    Fault initiateAcc();

    /** Completes the access.  Only valid for memory operations. */
    Fault completeAcc(PacketPtr pkt);

    /** The sequence number of the instruction. */
    InstSeqNum seqNum = 0;

    /** The StaticInst used by this BaseDynInst. */
    StaticInstPtr staticInst;

    /** Pointer to the Impl's CPU object. */
    CPU *cpu = nullptr;

        /** the xs metadata for this instruction */
    const XsDynInstMetaPtr xsMeta;

    BaseCPU *getCpuPtr() { return cpu; }

    /** Pointer to the thread state. */
    ThreadState *thread = nullptr;

    /** The kind of fault this instruction has generated. */
    Fault fault = NoFault;

    /** InstRecord that tracks this instructions. */
    trace::InstRecord *traceData = nullptr;

    /** PC state for this instruction. */
    std::unique_ptr<PCStateBase> pc;

  protected:
    enum Status
    {
        WtbEntry,                 /// Instruction is in the WTB
        RobEntry,                /// Instruction is in the ROB
        LsqEntry,                /// Instruction is in the LSQ
        Completed,               /// Instruction has completed
        ResultReady,             /// Instruction has its result
        CanIssue,                /// Instruction can issue and execute
        Issued,                  /// Instruction has issued
        Executed,                /// Instruction has executed
        CanCommit,               /// Instruction can commit
        AtCommit,                /// Instruction has reached commit
        Committed,               /// Instruction has committed
        Squashed,                /// Instruction is squashed
        SquashedInWTB,            /// Instruction is squashed in the WTB
        SquashedInLSQ,           /// Instruction is squashed in the LSQ
        SquashedInROB,           /// Instruction is squashed in the ROB
        PinnedRegsRenamed,       /// Pinned registers are renamed
        PinnedRegsWritten,       /// Pinned registers are written back
        PinnedRegsSquashDone,    /// Regs pinning status updated after squash
        RecoverInst,             /// Is a recover instruction
        BlockingInst,            /// Is a blocking instruction
        ThreadsyncWait,          /// Is a thread synchronization instruction
        SerializeBefore,         /// Needs to serialize on
                                 /// instructions ahead of it
        SerializeAfter,          /// Needs to serialize instructions behind it
        SerializeHandled,        /// Serialization has been handled
        InRemove,                /// into removelist in cpu.cc
        NumStatus
    };

    enum Flags
    {
        NotAnInst,
        TranslationStarted,
        TranslationCompleted,
        PossibleLoadViolation,
        HitExternalSnoop,
        EffAddrValid,
        RecordResult,
        Predicate,
        MemAccPredicate,
        PredTaken,
        PredTaken0,
        IsStrictlyOrdered,
        ReqMade,
        MemOpDone,
        HtmFromTransaction,
        MaxFlags
    };

  private:
    /* An amalgamation of a lot of boolean values into one */
    std::bitset<MaxFlags> instFlags;

    /** The status of this BaseDynInst.  Several bits can be set. */
    std::bitset<NumStatus> status;

  protected:
    /** The result of the instruction; assumes an instruction can have many
     *  destination registers.
     */
    std::queue<InstResult> instResult;

    /** Values to be written to the destination misc. registers. */
    std::vector<RegVal> _destMiscRegVal;

    /** Indexes of the destination misc. registers. They are needed to defer
     * the write accesses to the misc. registers until the commit stage, when
     * the instruction is out of its speculative state.
     */
    std::vector<short> _destMiscRegIdx;

    size_t _numSrcs;
    size_t _numDests;

    // Flattened register index of the destination registers of this
    // instruction.
    RegId *_flatDestIdx;

    // Physical register index of the destination registers of this
    // instruction.
    PhysRegIdPtr *_destIdx;

    // Physical register index of the previous producers of the
    // architected destinations.
    PhysRegIdPtr *_prevDestIdx;

    // Physical register index of the source registers of this instruction.
    PhysRegIdPtr *_srcIdx;

    // Whether or not the source register is ready, one bit per register.
    uint8_t *_readySrcIdx;

   public:

    unsigned rn_frm;

    unsigned prev_frm;

    unsigned rn_src_frm;

    bool sd_squash_ldst = false;

    bool frm_ready = false;

    RegVal frmVal() {
        RegVal val = cpu->frm.getVal(rn_src_frm);
        DPRINTF(RxuWTB, "Reading MiscReg FRM (%d): %#x.\n", rn_src_frm, val);
        return val;
    }

    //vector vl rename
    unsigned rn_vl;
    
    unsigned prev_vl;

    unsigned rn_src_vl;

    bool vl_ready = false;

    bool vld_had_issued = false;

    RegVal vlVal() {
        RegVal val = cpu->vecCsr.getVal(Vl,rn_src_vl);
        DPRINTF(RxuWTB, "Reading MiscReg Vl (%d): %#x.\n", rn_src_vl, val);
        return val;
    }

    //vector vtype rename
    unsigned rn_vtype;
    
    unsigned prev_vtype;

    unsigned rn_src_vtype;

    bool vtype_ready = false;

    RegVal vtypeVal() {
        RegVal val = cpu->vecCsr.getVal(Vtype,rn_src_vtype);
        DPRINTF(RxuWTB, "Reading MiscReg Vl (%d): %#x.\n", rn_src_vtype, val);
        return val;
    }

    uint32_t new_vl;
    RiscvISA::VTYPE new_vtype;

    //vector vxrm rename
    unsigned rn_vxrm;
    
    unsigned prev_vxrm;

    unsigned rn_src_vxrm;

    bool vxrm_ready = false;

    bool macro_issue = false;

    RegVal vxrmVal() {
        RegVal val = cpu->vecCsr.getVal(Vxrm,rn_src_vxrm);
        DPRINTF(RxuWTB, "Reading MiscReg Vl (%d): %#x.\n", rn_src_vtype, val);
        return val;
    }
    
    // executed vector uop cnt
    int vector_exe_cnt = 0;

    // cancommit vector uop cnt
    int vector_CanCommit_cnt =0;

    // vector special issueQ
    bool vSpecialCanIssue = false;

    //macro vector has decrease threadvecentries
    bool macroVecHasDecrease = false;

    // uop number
    int uopNumber = 0;

    // uop number
    int actualUopNumber = 0;

    // uop number
    int uopNumber_notWtb = 0;

    //instructions from which container
    std::vector<bool> fromWhichContainer = {false, false, false};

    /** uop list */
    std::vector<DynInstPtr> uopQueue;

    // if this inst already delayed
    bool alreadyDelay = false;

    bool inst_had_inReady = false;

    // original dyn_inst before split uop
    DynInstPtr ori_inst = nullptr;

    //original instruction InstSeqNum
    InstSeqNum oriSeqNum = 0;

    /** for nonsplit vector inst */
    bool macro = false;
    bool micro = false;
    bool firstMicro = false;
    bool lastMicro = false;

    /** Whether vector macro inst already redecode in dispipe0. */
    bool redecoded = false;

    /** Specify Special vector inst's number of uop who get vreg data. */
    int specialUopRdyNum = 0;

  public:
    size_t numSrcs() const { return _numSrcs; }
    void setNumSrcs(size_t numsrc) { _numSrcs = numsrc; }
    size_t numDests() const { return _numDests; }
    size_t numSrcsready() const { return _numsrcsready; }
    bool stdIssued = false;
    bool stdDataReady = false;
    bool staReady = false;
    size_t _numsrcsready = 0;
    bool had_issued = false;
    bool tlbfault = false;
    bool lbu_exfault = false;
    bool ifrenamesrc = false;
    bool ifrenamedest =  false;
    int ifhasreadyed = 0;
    bool stdReady = false;
    bool staInReadyList = false;
    bool if_exe_second = false;
    int num_exe = 0;
    bool staIssued = false;
    bool hadCommitted = false;
    bool inst_to_ew = false;
    bool stdNeedExeScd_issue = false;
    bool really_storeExe = false;
    int tlb_wait = 0;
    int cache_wait = 0;
    bool memhash = false;
    bool tryInWtb = false;
    bool ld_exe_succ = false;
    bool if_src0_ready = false;
    int stq_head = 0;
    //local data src is ready
    bool srcsLocalData[5] = {true, true, true, true, true};
    bool unlocaldata = false;
    bool had_src_data = true;
    bool had_read_RR[3] = {false, false, false};
    bool src_need_wake[4] = {false, false, false};
    bool is_vec_reg[4] = {false, false, false, false};
    bool is_vecReg_read[4] = {false, false, false, false};
    bool need_wake_sta = true;
    //vector split uop
    int get_vector_exe_cnt() const { return vector_exe_cnt; }
    int get_vector_CanCommit_cnt() const { return vector_CanCommit_cnt; }
    bool has_ori_inst = false;
    bool allUopInVector = false;
    bool macHadFifoAdd = false;
    bool macroMemRefIsErased = false;
    bool hadSplitEop = false;
    // bool waitSplitEop = false;
    bool allEopInVector = false;
    // inst really  commit
    bool inst_Real_Ready =false;
    int EopNotExe_Cnt = 0;
    int EopNotIssue = 0;
    bool hasReadyVl = false;
    bool hasAddStoresToWB = false;
    bool hasAddToVlDependGraph = false;
    bool vnopReady = false;
    //about wake register
    bool src0_ready = false;
    bool src1_ready = false;
    bool src2_ready = false;
    bool src3_ready = false;
    // Returns the flattened register index of the idx'th destination
    // register.
    const RegId &
    flattenedDestIdx(int idx) const
    {
        return _flatDestIdx[idx];
    }

    // Flattens a destination architectural register index into a logical
    // index.
    void
    flattenedDestIdx(int idx, const RegId &reg_id)
    {
        _flatDestIdx[idx] = reg_id;
    }

    // Returns the physical register index of the idx'th destination
    // register.
    PhysRegIdPtr
    renamedDestIdx(int idx) const
    {
        return _destIdx[idx];
    }

    // Set the renamed dest register id.
    void
    renamedDestIdx(int idx, PhysRegIdPtr phys_reg_id)
    {
        _destIdx[idx] = phys_reg_id;
    }

    // Returns the physical register index of the previous physical
    // register that remapped to the same logical register index.
    PhysRegIdPtr
    prevDestIdx(int idx) const
    {
        return _prevDestIdx[idx];
    }

    // Set the previous renamed dest register id.
    void
    prevDestIdx(int idx, PhysRegIdPtr phys_reg_id)
    {
        _prevDestIdx[idx] = phys_reg_id;
    }

    // Returns the physical register index of the i'th source register.
    PhysRegIdPtr
    renamedSrcIdx(int idx) const
    {
        return _srcIdx[idx];
    }

    PhysRegIdPtr
    renamedrdySrcIdx(int idx) const
    {
        return _srcIdx[idx];
    }

    void
    renamedSrcIdx(int idx, PhysRegIdPtr phys_reg_id)
    {
        _srcIdx[idx] = phys_reg_id;
    }

    bool
    readySrcIdx(int idx) const
    {
        uint8_t &byte = _readySrcIdx[idx / 8];
        return bits(byte, idx % 8);
    }

    void
    readySrcIdx(int idx, bool ready)
    {
        uint8_t &byte = _readySrcIdx[idx / 8];
        replaceBits(byte, idx % 8, ready ? 1 : 0);
    }

    bool readyFRM() const
    {
        return frm_ready;
    }

    bool readyVxrm() const
    {
        return vxrm_ready;
    }

    bool readyVl() const
    {
        return vl_ready;
    }

    bool readyVtype() const
    {
        return vtype_ready;
    }

    void
    readyFRM(bool ready)
    {
        frm_ready = ready;
    }

    void
    readyVxrm(bool ready)
    {
        vxrm_ready = ready;
    }

    void
    readyVl(bool ready)
    {
        vl_ready = ready;
    }

    void
    readyVtype(bool ready)
    {
        vtype_ready = ready;
    }        

    bool
    ifstdatardy(){
        return stdDataReady;
    }

    bool
    ifstdissue(){
        return stdIssued;
    }

    void increaseCnt () {
        if (has_ori_inst) {
            ori_inst->vector_CanCommit_cnt++;
            ori_inst->vector_exe_cnt++;
            uint32_t vl = staticInst->machInst.vl;
            if (!isVlff() && vl != 0 && isMemRef()) {
                ori_inst->uopNumber++;
                ori_inst->uopNumber_notWtb++;
            }
            else if(ori_inst->staticInst->opClass() == SimdWholeRegisterLoadOp || ori_inst->staticInst->opClass() == SimdWholeRegisterStoreOp){
                ori_inst->uopNumber = ori_inst->staticInst->machInst.nf + 1;
                ori_inst->uopNumber_notWtb = ori_inst->staticInst->machInst.nf + 1;
            }
            else if(ori_inst->staticInst->opClass() == SimdVLoadOnceOp){
                ori_inst->uopNumber++;
                ori_inst->uopNumber_notWtb++;
            }
        }
    }

    void 
    decreaseVecExeCnt() {
        if (has_ori_inst)  {
            if (ori_inst->vector_CanCommit_cnt) {
                ori_inst->vector_exe_cnt--;                
            }          
        }
    }


    void 
    decreaseVecCommitCnt() {
        if (has_ori_inst)  {
            if (ori_inst->vector_CanCommit_cnt) {
                ori_inst->vector_CanCommit_cnt--;                
            }
        }
    }    
    

    void 
    setOriFault() {
        if (has_ori_inst) {
            if (ori_inst->fault == NoFault) {
                ori_inst->fault = fault;                
            }
        }
    }


    void 
    setOriExecuted() {
        if (has_ori_inst) {
            if (ori_inst->vector_exe_cnt == 0) {
                ori_inst->status.set(Executed);
            }
        }
    }


    void 
    setOriCanCommit() {
        if (has_ori_inst)  {
            if (ori_inst->vector_CanCommit_cnt == 0) {
                ori_inst->status.set(CanCommit);                
            }
        }        
    }


    void 
    setOriInst(const DynInstPtr &inst) {
        ori_inst = inst;
        has_ori_inst = true;
        oriSeqNum = inst->seqNum;
    }

    
    /** The thread this instruction is from. */
    ThreadID threadNumber = 0;

    /** Iterator pointing to this BaseDynInst in the list of all insts. */
    ListIt instListIt;

    ////////////////////// Branch Data ///////////////
    /** Predicted PC state after this instruction. */
    std::unique_ptr<PCStateBase> predPC;

    std::unique_ptr<PCStateBase> predPC0;

    /** The Macroop if one exists */
    const StaticInstPtr macroop;

    /** How many source registers are ready. */
    uint8_t readyRegs = 0;

    /** How many source registers are ready. */
    uint8_t readyRegs_vector = 0;

  public:
    bool fromVecQueue = false;

    bool delayReady = false;

    int std_ready_number = 0;

    // -- add by hongfei.liu--------------
    bool odd_inst = true;

    bool stcandealLFST = false;

    bool ifstdready = false;

    int ibuffer_id = -1;

    InstSeqNum iid = 0;

    bool inst_had_insertq = false;

    bool pred_weak = false;

    int pred_ctr = -1;

    int bubbles = -1;
    // -----------------------------------
    /////////////////////// Load Store Data //////////////////////
    /** The effective virtual address (lds & stores only). */
    Addr effAddr = 0;

    /** The effective physical address. */
    Addr physEffAddr = 0;

    /** The memory request flags (from translation). */
    unsigned memReqFlags = 0;

    /** The size of the request */
    unsigned effSize = 0;

    /** Pointer to the data for the memory access. */
    uint8_t *memData = nullptr;

    /** Load queue index. */
    ssize_t lqIdx = -1;
    typename LSQUnit::LQIterator lqIt;

    /** Store queue index. */
    ssize_t sqIdx = -1;
    typename LSQUnit::SQIterator sqIt;


    /////////////////////// TLB Miss //////////////////////
    /**
     * Saved memory request (needed when the DTB address translation is
     * delayed due to a hw page table walk).
     */
    LSQ::LSQRequest *savedRequest;

    /////////////////////// Checker //////////////////////
    // Need a copy of main request pointer to verify on writes.
    RequestPtr reqToVerify;

  public:
    /** Records changes to result? */
    void recordResult(bool f) { instFlags[RecordResult] = f; }

    /** Is the effective virtual address valid. */
    bool effAddrValid() const { return instFlags[EffAddrValid]; }
    void effAddrValid(bool b) { instFlags[EffAddrValid] = b; }

    /** Whether or not the memory operation is done. */
    bool memOpDone() const { return instFlags[MemOpDone]; }
    void memOpDone(bool f) { instFlags[MemOpDone] = f; }

    bool notAnInst() const { return instFlags[NotAnInst]; }
    void setNotAnInst() { instFlags[NotAnInst] = true; }


    ////////////////////////////////////////////
    //
    // INSTRUCTION EXECUTION
    //
    ////////////////////////////////////////////

    void
    demapPage(Addr vaddr, uint64_t asn) override
    {
        cpu->demapPage(vaddr, asn);
    }

    Fault initiateMemRead(Addr addr, unsigned size, Request::Flags flags,
            const std::vector<bool> &byte_enable) override;

    Fault initiateMemMgmtCmd(Request::Flags flags) override;

    Fault writeMem(uint8_t *data, unsigned size, Addr addr,
                   Request::Flags flags, uint64_t *res,
                   const std::vector<bool> &byte_enable) override;

    Fault initiateMemAMO(Addr addr, unsigned size, Request::Flags flags,
                         AtomicOpFunctorPtr amo_op) override;

    /** True if the DTB address translation has started. */
    bool translationStarted() const { return instFlags[TranslationStarted]; }
    void translationStarted(bool f) { instFlags[TranslationStarted] = f; }

    /** True if the DTB address translation has completed. */
    bool
    translationCompleted() const
    {
        return instFlags[TranslationCompleted];
    }
    void translationCompleted(bool f) { instFlags[TranslationCompleted] = f; }

    /** True if this address was found to match a previous load and they issued
     * out of order. If that happend, then it's only a problem if an incoming
     * snoop invalidate modifies the line, in which case we need to squash.
     * If nothing modified the line the order doesn't matter.
     */
    bool
    possibleLoadViolation() const
    {
        return instFlags[PossibleLoadViolation];
    }
    void
    possibleLoadViolation(bool f)
    {
        instFlags[PossibleLoadViolation] = f;
    }

    /** True if the address hit a external snoop while sitting in the LSQ.
     * If this is true and a older instruction sees it, this instruction must
     * reexecute
     */
    bool hitExternalSnoop() const { return instFlags[HitExternalSnoop]; }
    void hitExternalSnoop(bool f) { instFlags[HitExternalSnoop] = f; }

    /**
     * Returns true if the DTB address translation is being delayed due to a hw
     * page table walk.
     */
    bool
    isTranslationDelayed() const
    {
        return (translationStarted() && !translationCompleted());
    }

  public:
#ifdef GEM5_DEBUG
    void dumpSNList();
#endif

    /** Renames a destination register to a physical register.  Also records
     *  the previous physical register that the logical register mapped to.
     */
    void
    renameDestReg(int idx, PhysRegIdPtr renamed_dest,
                  PhysRegIdPtr previous_rename)
    {
        renamedDestIdx(idx, renamed_dest);
        prevDestIdx(idx, previous_rename);
        if (renamed_dest->isPinned())
            setPinnedRegsRenamed();
    }

    /** Renames a source logical register to the physical register which
     *  has/will produce that logical register's result.
     *  @todo: add in whether or not the source register is ready.
     */
    void
    renameSrcReg(int idx, PhysRegIdPtr renamed_src)
    {
        renamedSrcIdx(idx, renamed_src);
    }

    void
    renameFRM(unsigned renamed_dest, unsigned previous_rename)
    {
        rn_frm = renamed_dest;
        prev_frm = previous_rename;
    }

    void
    renameFRM(unsigned pregid)
    {
        rn_src_frm = pregid;
    }

    void
    renameVl(unsigned pregid)
    {
        rn_src_vl = pregid;
    }

    void
    renameVl(unsigned renamed_dest, unsigned previous_rename)
    {
        rn_vl = renamed_dest;
        prev_vl = previous_rename;
    }

    void
    renameVtype(unsigned pregid)
    {
        rn_src_vtype = pregid;
    }

    void
    renameVtype(unsigned renamed_dest, unsigned previous_rename)
    {
        rn_vtype = renamed_dest;
        prev_vtype = previous_rename;
    }

    void
    renameVxrm(unsigned pregid)
    {
        rn_src_vxrm = pregid;
    }

    void
    renameVxrm(unsigned renamed_dest, unsigned previous_rename)
    {
        rn_vxrm = renamed_dest;
        prev_vxrm = previous_rename;
    }


    /** Dumps out contents of this BaseDynInst. */
    void dump();

    /** Dumps out contents of this BaseDynInst into given string. */
    void dump(std::string &outstring);

    /** Read this CPU's ID. */
    int cpuId() const { return cpu->cpuId(); }

    /** Read this CPU's Socket ID. */
    uint32_t socketId() const { return cpu->socketId(); }

    /** Read this CPU's data requestor ID */
    RequestorID requestorId() const { return cpu->dataRequestorId(); }

    /** Read this context's system-wide ID **/
    ContextID contextId() const { return thread->contextId(); }

    /** Returns the fault type. */
    Fault getFault() const { return fault; }
    /** TODO: This I added for the LSQRequest side to be able to modify the
     * fault. There should be a better mechanism in place. */
    Fault& getFault() { return fault; }

    /** Checks whether or not this instruction has had its branch target
     *  calculated yet.  For now it is not utilized and is hacked to be
     *  always false.
     *  @todo: Actually use this instruction.
     */
    bool doneTargCalc() { return false; }

    /** Set the predicted target of this current instruction. */
    void setPredTarg(const PCStateBase &pred_pc) { set(predPC, pred_pc); }

    void setPredTarg0(const PCStateBase &pred_pc) { set(predPC0, pred_pc); }

    const PCStateBase &readPredTarg() { return *predPC; }

    const PCStateBase &readPredTarg0() { return *predPC0; }

    /** Returns whether the instruction was predicted taken or not. */
    bool readPredTaken() { return instFlags[PredTaken]; }

    bool readPredTaken0() { return instFlags[PredTaken0]; }

    void
    setPredTaken(bool predicted_taken)
    {
        instFlags[PredTaken] = predicted_taken;
    }

    void
    setPredTaken0(bool predicted_taken)
    {
        instFlags[PredTaken0] = predicted_taken;
    }

    /** Returns whether the instruction mispredicted. */
    bool
    mispredicted()
    {
        std::unique_ptr<PCStateBase> next_pc(pc->clone());
        staticInst->advancePC(*next_pc);
        DPRINTF(RxuDynInst, "npc vl = %d, tpc vl = %d\n", 
        (*next_pc).as<gem5::RiscvISA::PCState>()._vl, (*predPC).as<gem5::RiscvISA::PCState>()._vl);
        bool mispred = false;
        if (!isVset()) mispred = (*next_pc)._pc != (*predPC)._pc;
        else mispred = *next_pc != *predPC;
        return mispred;
    }

    bool
    mispredicted0()
    {
        std::unique_ptr<PCStateBase> next_pc(pc->clone());
        staticInst->advancePC(*next_pc);
        return *next_pc != *predPC0;
    }

    bool hasDestReg() {
        return numDestRegs() > 0 && (destRegIdx(0).classValue() != InvalidRegClass);
    }

    void setMacroop(bool bo) { macro = bo; }
    void setMicroop(bool bo) { micro = bo; }
    void setFirstMicroop(bool bo) { firstMicro = bo; }
    void setLastMicroop(bool bo) { lastMicro = bo; }

    //
    //  Instruction types.  Forward checks to StaticInst object.
    //
    bool isNop()          const { return staticInst->isNop(); }
    bool isMemRef()       const { return staticInst->isMemRef(); }
    bool isLoad()         const { return staticInst->isLoad(); }
    bool isStore()        const { return staticInst->isStore(); }
    bool isAtomic()       const { return staticInst->isAtomic(); }
    bool isStoreConditional() const
    { return staticInst->isStoreConditional(); }
    bool isInstPrefetch() const { return staticInst->isInstPrefetch(); }
    bool isDataPrefetch() const { return staticInst->isDataPrefetch(); }
    bool isInteger()      const { return staticInst->isInteger(); }
    bool isFloating()     const { return staticInst->isFloating(); }
    bool isVector()       const { return staticInst->isVector(); }
    bool isControl()      const { return staticInst->isControl(); }
    bool isCall()         const { return staticInst->isCall(); }
    bool isReturn()       const { return staticInst->isReturn(); }
    bool isDirectCtrl()   const { return staticInst->isDirectCtrl(); }
    bool isIndirectCtrl() const { return staticInst->isIndirectCtrl(); }
    bool isCondCtrl()     const { return staticInst->isCondCtrl(); }
    bool isUncondCtrl()   const { return staticInst->isUncondCtrl(); }
    bool isVset()         const { return staticInst->isVset(); }
    bool isVluxei8()      const { return staticInst->isVluxei8(); }
    bool isVnop()         const { return staticInst->isNop() && staticInst->isVector(); }
    bool isMacroVector()  const { return isMacroop() && staticInst->isVector(); }
    bool isMicroVector()  const { return isMicroop() && staticInst->isVector(); }
    bool isMacroVectorMemRef() const
    {
        return isMacroVector() && isMemRef();
    }
    bool isNonSplitVector() const
    {
        return staticInst->isNonSplitVector();
    }
    bool isVlff() const { return staticInst->isVlff(); }
    bool isSplitUop() const {return staticInst->isSplitUop(); }
    bool isEop() const {return staticInst->isEop(); }
    bool isOrderInst() const {return staticInst->isOrderInst(); }
    bool isSplitMacro() const{ return staticInst->isSplitMacro(); }
    bool isSpecialVector() const { return staticInst->isSpecialVector(); }
    bool isSpecialMacro() const { return staticInst->isSpecialVector() && isMacroVector(); }
    bool isSpecialMicro() const { return staticInst->isSpecialVector() && isMicroVector(); }
    bool isSpecialVectorNotExec() const { return staticInst->isSpecialVectorNotExec(); }
    bool isRxuVmInst() const { return staticInst->isRxuVmInst(); }
    bool isSerializing()  const { return staticInst->isSerializing(); }
    bool
    isSerializeBefore() const
    {
        return staticInst->isSerializeBefore() || status[SerializeBefore];
    }
    bool
    isSerializeAfter() const
    {
        return staticInst->isSerializeAfter() || status[SerializeAfter];
    }
    bool isSquashAfter() const { return staticInst->isSquashAfter(); }
    bool isFullMemBarrier()   const { return staticInst->isFullMemBarrier(); }
    bool isReadBarrier() const { return staticInst->isReadBarrier(); }
    bool isWriteBarrier() const { return staticInst->isWriteBarrier(); }
    bool isNonSpeculative() const { return staticInst->isNonSpeculative(); }
    bool isQuiesce() const { return staticInst->isQuiesce(); }
    bool isUnverifiable() const { return staticInst->isUnverifiable(); }
    bool isSyscall() const { return staticInst->isSyscall(); }
    bool isMacroop() const {
        if (isNonSplitVector()) return macro;
        else return staticInst->isMacroop();
    }
    bool isMicroop() const {
        if (isNonSplitVector()) return micro;
        else return staticInst->isMicroop();
    }
    bool isDelayedCommit() const { return staticInst->isDelayedCommit(); }
    bool isLastMicroop() const { 
        if (isNonSplitVector()) return lastMicro;
        else return staticInst->isLastMicroop();}
    bool isFirstMicroop() const { 
        if(isNonSplitVector()) return firstMicro;
        else return staticInst->isFirstMicroop();}
    // hardware transactional memory
    bool isHtmStart() const { return staticInst->isHtmStart(); }
    bool isHtmStop() const { return staticInst->isHtmStop(); }
    bool isHtmCancel() const { return staticInst->isHtmCancel(); }
    bool isHtmCmd() const { return staticInst->isHtmCmd(); }

    // bool isIntNormal() const { return staticInst->isIntNormal(); }
    // bool isIntSpec() const { return staticInst->isIntSpec(); }
    // bool isFloatNormal() const { return staticInst->isFloatNormal(); }
    // bool isFloatSpec() const { return staticInst->isFloatSpec(); }
    bool isIntNormal() const { return staticInst->isRxuIntNormal() || isRandomXInt(); }
    bool isIntSpec() const { return ((staticInst->isRxuIntSpecial() && !isRandomXInt()) || isInt2Fp()) && !isFp2Int(); }
    bool isFloatNormal() const { return staticInst->isRxuFloatNormal() && !isRandomXFloat(); }
    bool isFloatSpec() const { return (staticInst->isRxuFloatSpecial() || isRandomXFloat() || isFp2Int()) && !isInt2Fp(); }
    bool isInt2Fp() const {
        if (numSrcRegs() <= 0 || numDestRegs() <= 0) {
            return false;
        } else {
            return (staticInst->getName().substr(0, 3) == "fmv"
                && (srcRegIdx(0).classValue() == IntRegClass || destRegIdx(0).classValue() == FloatRegClass))
                || (staticInst->getName().substr(0, 4) == "fcvt"
                && (srcRegIdx(0).classValue() == IntRegClass || destRegIdx(0).classValue() == FloatRegClass));
        }
    }

    bool isFp2Int() const {
        if (numSrcRegs() <= 0 || numDestRegs() <= 0) {
            return false;
        } else {
            return (staticInst->getName().substr(0, 3) == "fmv"
                && (destRegIdx(0).classValue() == IntRegClass || srcRegIdx(0).classValue() == FloatRegClass))
                || (staticInst->getName().substr(0, 4) == "fcvt"
                && (destRegIdx(0).classValue() == IntRegClass || srcRegIdx(0).classValue() == FloatRegClass));
        }
    }

    bool isRandomXFloat() const {
        if (staticInst->getName() == "for" || staticInst->getName() == "fxor" || staticInst->getName() == "fand") {
            return true;
        } else {
            return false;
        }
    }

    bool isRandomXInt() const {
        if (staticInst->getName() == "csrraddh" || staticInst->getName() == "csrraddl" || staticInst->getName() == "csrmul"
            || staticInst->getName() == "srrmsk" || staticInst->getName() == "slrmsk") {
            return true;
        } else {
            return false;
        }
    }

    bool isCsrFRM() const {
        if (staticInst->isCsr() && staticInst->disassemble(pc->instAddr()).find("frm") != std::string::npos) {
            return true;
        } else {
            return false;
        }
    }

    bool isCsrVtype() const {
        if (staticInst->isCsr() && staticInst->disassemble(pc->instAddr()).find("vtype") != std::string::npos) {
            return true;
        } else {
            return false;
        }
    }

    bool isCsrVl() const {
        if (staticInst->isCsr() && staticInst->disassemble(pc->instAddr()).find("vl") != std::string::npos) {
            return true;
        } else {
            return false;
        }
    }

    bool isCompressed() const {
        return staticInst->isCompressed();
    }

    bool needFRM() const {
        return (opClass() == enums::FloatAdd || opClass() == enums::FloatMultAcc 
        || opClass() == enums::FloatMult || opClass() == enums::FloatDiv 
        || opClass() == enums::FloatCvt || opClass() == enums::FloatSqrt);
    }

    bool needVxrm() const {
        return (opClass() == enums::SimdFloatAdd || opClass() == enums::SimdFloatCvt 
        || opClass() == enums::SimdFloatDiv || opClass() == enums::SimdFloatMult 
        || opClass() == enums::SimdFloatMultAcc || opClass() == enums::SimdFloatSqrt 
        || opClass() == enums::SimdFloatReduceAdd);
    }
// test for eop
    bool isVse() const {
        return (opClass() == enums::SimdUnitStrideStore);
    }
    bool isVluxeix() const {
        return (opClass() == enums::SimdIndexedLoad);
    }
    bool isVsuxeix() const {
        return (opClass() == enums::SimdIndexedStore);
    }
    bool isVlse() const {
        return (opClass() == enums::SimdStridedLoad);
    }
    bool isVsse() const {
        return (opClass() == enums::SimdStridedStore);
    }
    bool isVlsSeg() const {
        return (opClass() == enums::SimdStrideSegmentedLoad);
    }
    bool isVlseg() const {
        return (opClass() == enums::SimdUnitStrideSegmentedLoad);
    }
    bool isVsseg() const {
        return (opClass() == enums::SimdUnitStrideSegmentedStore);
    }
    bool isVluxSeg() const {
        return (opClass() == enums::SimdIndexSegmentedLoad);
    }
    bool isVssseg() const {
        return (opClass() == enums::SimdStridedSegmentedStore);
    }
    bool isVsuxseg() const {
        return (opClass() == enums::SimdIndexedSegmentedStore);
    }
    bool isVIndexORStrideLoad() const {
        return (opClass() == enums::SimdIndexedLoad || opClass() == enums::SimdStridedLoad 
             || opClass() == enums::SimdStrideSegmentedLoad || opClass() == enums::SimdIndexSegmentedLoad
             || opClass() == enums::SimdUnitStrideSegmentedLoad);
    }
    bool isVIndexORStrideStore() const {
        return (opClass() == enums::SimdIndexedStore || opClass() == enums::SimdStridedStore 
             || opClass() == enums::SimdStridedSegmentedStore || opClass() == enums::SimdIndexedSegmentedStore);
    }
    bool needEop() const {
        return (isVIndexORStrideLoad() || isVIndexORStrideStore());
    }
//end for eop
    bool isVecCsrVxrm() const {
        if (staticInst->isCsr() && staticInst->disassemble(pc->instAddr()).find("vxrm") != std::string::npos) {
            return true;
        } else {
            return false;
        }
    }
    
    bool needVlVtype() const {
        return (staticInst->isVector() && staticInst->getName().substr(0, 4) != "vset");
    }

    bool isVsetvl() const {
        if (staticInst->disassemble(pc->instAddr()).find("vsetvl")!= std::string::npos) {
            if (staticInst->getName().substr(0,7) == "vsetvli") {
                return false;
            } else {
                return true;
            }
        } else {
            return false;
        }
    }

    bool isVsetvli() const {
        if (staticInst->disassemble(pc->instAddr()).find("vsetvli")!= std::string::npos) {
            return true;
        } else {
            return false;
        }
    }

    bool isVsetivli() const {
        if (staticInst->disassemble(pc->instAddr()).find("vsetivli")!= std::string::npos) {
            return true;
        } else {
            return false;
        }
    }

    bool isVecCsrVtype() const {
        if (staticInst->disassemble(pc->instAddr()).find("vsetvl") != std::string::npos) {
            if (staticInst->getName().substr(0,7) == "vsetvli") {
                return false;
            } else {
                return true;
            }
        } else {
            return false;
        }
    }

    bool vs1FromMicro() {
        return (staticInst->getName().substr(0, 11) == "vrgather_vv"
             || staticInst->getName().substr(0, 12) == "vrgatherei16");
    }
           
 
    // bool isFenceCSR() const {
    //     if (!staticInst->isCsr()) {
    //         return false;
    //     }
    //     if (staticInst->getName() == "csrrh" || staticInst->getName() == "csrrl"
    //         || staticInst->getName() == "csrmul") {
    //         return false;
    //     }
    //     else if (staticInst->machInst.bit31_28 == 8               // uimm and uconst
    //         || staticInst->machInst.funct12 == 1               // fflags
    //         // || staticInst->machInst.funct12 == 2               // frm
    //         ) {
    //         return false;
    //     }
    //     return true;
    // }

    bool updateCsrFence() {
        bool flag = false;
        if (!staticInst->isCsr()) {
            return false;
        }
        if (staticInst->getName() == "csrrh" || staticInst->getName() == "csrrl"
            || staticInst->getName() == "csrmul") {
            return false;
        }
        if (staticInst->machInst.bit31_28 == 8               // uimm and uconst
            || staticInst->machInst.funct12 == 1               // fflags
            || staticInst->machInst.funct12 == 2               // frm
            ) {
            clearSerializeAfter();
            staticInst->clearSerializeAfter();
            clearSerializeBefore();
            staticInst->clearSerializeBefore();
            return true;
        }
        // if (staticInst->machInst.rd == 0) {
        //     clearSerializeBefore();
        //     staticInst->clearSerializeBefore();
        //     flag = true;
        // }
        // if (staticInst->machInst.rs1 == 0) {
        //     clearSerializeAfter();
        //     staticInst->clearSerializeAfter();
        //     flag = true;
        // }
        return flag;
    }

    uint64_t
    getHtmTransactionUid() const override
    {
        assert(instFlags[HtmFromTransaction]);
        return htmUid;
    }

    uint64_t
    newHtmTransactionUid() const override
    {
        panic("Not yet implemented\n");
        return 0;
    }

    bool
    inHtmTransactionalState() const override
    {
        return instFlags[HtmFromTransaction];
    }

    uint64_t
    getHtmTransactionalDepth() const override
    {
        if (inHtmTransactionalState())
            return htmDepth;
        else
            return 0;
    }

    void
    setHtmTransactionalState(uint64_t htm_uid, uint64_t htm_depth)
    {
        instFlags.set(HtmFromTransaction);
        htmUid = htm_uid;
        htmDepth = htm_depth;
    }

    void
    clearHtmTransactionalState()
    {
        if (inHtmTransactionalState()) {
            DPRINTF(HtmCpu,
                "clearing instuction's transactional state htmUid=%u\n",
                getHtmTransactionUid());

            instFlags.reset(HtmFromTransaction);
            htmUid = -1;
            htmDepth = 0;
        }
    }

    /** Temporarily sets this instruction as a serialize before instruction. */
    void setSerializeBefore() { status.set(SerializeBefore); }

    /** Clears the serializeBefore part of this instruction. */
    void clearSerializeBefore() { status.reset(SerializeBefore); }

    /** Checks if this serializeBefore is only temporarily set. */
    bool isTempSerializeBefore() { return status[SerializeBefore]; }

    /** Temporarily sets this instruction as a serialize after instruction. */
    void setSerializeAfter() { status.set(SerializeAfter); }

    /** Clears the serializeAfter part of this instruction.*/
    void clearSerializeAfter() { status.reset(SerializeAfter); }

    /** Checks if this serializeAfter is only temporarily set. */
    bool isTempSerializeAfter() { return status[SerializeAfter]; }

    /** Sets the serialization part of this instruction as handled. */
    void setSerializeHandled() { status.set(SerializeHandled); }

    /** Checks if the serialization part of this instruction has been
     *  handled.  This does not apply to the temporary serializing
     *  state; it only applies to this instruction's own permanent
     *  serializing state.
     */
    bool isSerializeHandled() { return status[SerializeHandled]; }

    void setInRemove() { status.set(InRemove); }

    void clearInRemove() { status.reset(InRemove); }

    bool isInRemove() const { return status[InRemove]; }

    /** Returns the opclass of this instruction. */
    OpClass opClass() const { return staticInst->opClass(); }

    /** Returns the branch target address. */
    std::unique_ptr<PCStateBase>
    branchTarget() const
    {
        return staticInst->branchTarget(*pc);
    }

    /** Returns the number of source registers. */
    size_t numSrcRegs() const { return numSrcs(); }

    /** Returns the number of destination registers. */
    size_t numDestRegs() const { return numDests(); }

    size_t
    numDestRegs(RegClassType type) const
    {
        return staticInst->numDestRegs(type);
    }

    /** Returns the logical register index of the i'th destination register. */
    const RegId& destRegIdx(int i) const { return staticInst->destRegIdx(i); }

    /** Returns the logical register index of the i'th source register. */
    const RegId& srcRegIdx(int i) const { return staticInst->srcRegIdx(i); }

    /** Return the size of the instResult queue. */
    uint8_t resultSize() { return instResult.size(); }

    /** Pops a result off the instResult queue.
     * If the result stack is empty, return the default value.
     * */
    InstResult
    popResult(InstResult dflt=InstResult())
    {
        if (!instResult.empty()) {
            InstResult t = instResult.front();
            instResult.pop();
            return t;
        }
        return dflt;
    }

    /** Pushes a result onto the instResult queue. */
    /** @{ */
    template<typename T>
    void
    setResult(const RegClass &reg_class, T &&t)
    {
        if (instFlags[RecordResult]) {
            instResult.emplace(reg_class, std::forward<T>(t));
        }
    }
    /** @} */

    /** Records that one of the source registers is ready. */
    void markSrcRegReady();

    /** Marks a specific register as ready. */
    void markSrcRegReady(RegIndex src_idx);

    /** Marks a specific register as ready. */
    void markSrcRegReady(int src_idx,bool srcready,int srcx);

    void markSrcRegReady(int src_idx,bool srcready,int srcx,bool is_store,bool is_index_stride);

    /** Sets this instruction as completed. */
    void setCompleted() { status.set(Completed); }

    /** Returns whether or not this instruction is completed. */
    bool isCompleted() const { return status[Completed]; }

    /** Marks the result as ready. */
    void setResultReady() { status.set(ResultReady); }

    /** Returns whether or not the result is ready. */
    bool isResultReady() const { return status[ResultReady]; }

    /** Sets this instruction as ready to issue. */
    void setCanIssue() { status.set(CanIssue); }

    /** Returns whether or not this instruction is ready to issue. */
    bool readyToIssue() const { return status[CanIssue]; }

    /** Clears this instruction being able to issue. */
    void clearCanIssue() { status.reset(CanIssue); }

    /** Sets this instruction as issued from the WTB. */
    void setIssued() { status.set(Issued); }

    /** Returns whether or not this instruction has issued. */
    bool isIssued() const { return status[Issued]; }

    /** Clears this instruction as being issued. */
    void clearIssued() { status.reset(Issued); }

    /** Sets this instruction as executed. */
    void setExecuted() { 
        if (isMicroVector()) {
            status.set(Executed);
            DPRINTF(RxuDynInst,"exe_cnt[%i]. \n", ori_inst->vector_exe_cnt);
            decreaseVecExeCnt();
            setOriExecuted();
        } else {
            status.set(Executed); 
        }
    }

    /** Returns whether or not this instruction has executed. */
    bool isExecuted() const { return status[Executed]; }

    /** Sets this instruction as ready to commit. */
    void setCanCommit() {
        if (isMicroVector() && !isSpecialVector()) {
            status.set(CanCommit);
            DPRINTF(RxuDynInst,"exe_cnt[%i]. \n", ori_inst->vector_CanCommit_cnt);
            decreaseVecCommitCnt();
            setOriCanCommit();
        } else {
            status.set(CanCommit); 
        }
    }

    /** Clears this instruction as being ready to commit. */
    void clearCanCommit() { status.reset(CanCommit); }

    /** Returns whether or not this instruction is ready to commit. */
    bool readyToCommit() const { return status[CanCommit]; }

    void setAtCommit() { status.set(AtCommit); }

    bool isAtCommit() { return status[AtCommit]; }

    /** Sets this instruction as committed. */
    void setCommitted() { status.set(Committed); }

    /** Returns whether or not this instruction is committed. */
    bool isCommitted() const { return status[Committed]; }

    /** Sets this instruction as squashed. */
    void setSquashed();

    /** Returns whether or not this instruction is squashed. */
    bool isSquashed() const { return status[Squashed]; }

    //Instruction Queue Entry
    //-----------------------
    /** Sets this instruction as a entry the WTB. */
    void setInWTB() { status.set(WtbEntry); }

    /** Sets this instruction as a entry the WTB. */
    void clearInWTB() { status.reset(WtbEntry); }

    /** Returns whether or not this instruction has issued. */
    bool isInWTB() const { return status[WtbEntry]; }

    /** Sets this instruction as squashed in the WTB. */
    void setSquashedInWTB() { status.set(SquashedInWTB); status.set(Squashed);}

    /** Returns whether or not this instruction is squashed in the WTB. */
    bool isSquashedInWTB() const { return status[SquashedInWTB]; }


    //Load / Store Queue Functions
    //-----------------------
    /** Sets this instruction as a entry the LSQ. */
    void setInLSQ() { status.set(LsqEntry); }

    /** Sets this instruction as a entry the LSQ. */
    void removeInLSQ() { status.reset(LsqEntry); }

    /** Returns whether or not this instruction is in the LSQ. */
    bool isInLSQ() const { return status[LsqEntry]; }

    /** Sets this instruction as squashed in the LSQ. */
    void setSquashedInLSQ() { status.set(SquashedInLSQ); status.set(Squashed);}

    /** Returns whether or not this instruction is squashed in the LSQ. */
    bool isSquashedInLSQ() const { return status[SquashedInLSQ]; }


    //Reorder Buffer Functions
    //-----------------------
    /** Sets this instruction as a entry the ROB. */
    void setInROB() { status.set(RobEntry); }

    /** Sets this instruction as a entry the ROB. */
    void clearInROB() { status.reset(RobEntry); }

    /** Returns whether or not this instruction is in the ROB. */
    bool isInROB() const { return status[RobEntry]; }

    /** Sets this instruction as squashed in the ROB. */
    void setSquashedInROB() { status.set(SquashedInROB); }

    /** Returns whether or not this instruction is squashed in the ROB. */
    bool isSquashedInROB() const { return status[SquashedInROB]; }

    /** Returns whether pinned registers are renamed */
    bool isPinnedRegsRenamed() const { return status[PinnedRegsRenamed]; }

    /** Sets the destination registers as renamed */
    void
    setPinnedRegsRenamed()
    {
        assert(!status[PinnedRegsSquashDone]);
        assert(!status[PinnedRegsWritten]);
        status.set(PinnedRegsRenamed);
    }

    /** Returns whether destination registers are written */
    bool isPinnedRegsWritten() const { return status[PinnedRegsWritten]; }

    /** Sets destination registers as written */
    void
    setPinnedRegsWritten()
    {
        assert(!status[PinnedRegsSquashDone]);
        assert(status[PinnedRegsRenamed]);
        status.set(PinnedRegsWritten);
    }

    /** Return whether dest registers' pinning status updated after squash */
    bool
    isPinnedRegsSquashDone() const
    {
        return status[PinnedRegsSquashDone];
    }

    /** Sets dest registers' status updated after squash */
    void
    setPinnedRegsSquashDone()
    {
        assert(!status[PinnedRegsSquashDone]);
        status.set(PinnedRegsSquashDone);
    }

    /** Read the PC state of this instruction. */
    const PCStateBase &
    pcState() const override
    {
        return *pc;
    }

    /** Set the PC state of this instruction. */
    void pcState(const PCStateBase &val) override { set(pc, val); }

    bool readPredicate() const override { return instFlags[Predicate]; }

    void
    setPredicate(bool val) override
    {
        instFlags[Predicate] = val;

        if (traceData) {
            traceData->setPredicate(val);
        }
    }

    bool
    readMemAccPredicate() const override
    {
        return instFlags[MemAccPredicate];
    }

    void
    setMemAccPredicate(bool val) override
    {
        instFlags[MemAccPredicate] = val;
    }

    /** Sets the thread id. */
    void setTid(ThreadID tid) { threadNumber = tid; }

    /** Sets the pointer to the thread state. */
    void setThreadState(ThreadState *state) { thread = state; }

    /** Returns the thread context. */
    gem5::ThreadContext *tcBase() const override { return thread->getTC(); }

  public:
    /** Is this instruction's memory access strictly ordered? */
    bool strictlyOrdered() const { return instFlags[IsStrictlyOrdered]; }
    void strictlyOrdered(bool so) { instFlags[IsStrictlyOrdered] = so; }

    /** Has this instruction generated a memory request. */
    bool hasRequest() const { return instFlags[ReqMade]; }
    /** Assert this instruction has generated a memory request. */
    void setRequest() { instFlags[ReqMade] = true; }

    /** Returns iterator to this instruction in the list of all insts. */
    ListIt &getInstListIt() { return instListIt; }

    /** Sets iterator for this instruction in the list of all insts. */
    void setInstListIt(ListIt _instListIt) { instListIt = _instListIt; }

  public:

    int ststdstat = -1;

    bool ld_need_tr = false;

    int num_stqldq = 0;

    int ldfindvio = 0;

    bool arrivewtb = false;

    bool ibufferid = false;

    bool hadfifo_in = false;

    // load pa
    Addr load_once = 0;

    std::vector<DynInstPtr> depStore;

    int num_dep = 0;

    bool all_depReady = false;

    bool stronglyTaken = false;

    /** Returns the number of consecutive store conditional failures. */
    unsigned int
    readStCondFailures() const override
    {
        return thread->storeCondFailures;
    }

    /** Sets the number of consecutive store conditional failures. */
    void
    setStCondFailures(unsigned int sc_failures) override
    {
        thread->storeCondFailures = sc_failures;
    }

  public:
    // monitor/mwait funtions
    void
    armMonitor(Addr address) override
    {
        cpu->armMonitor(threadNumber, address);
    }
    bool
    mwait(PacketPtr pkt) override
    {
        return cpu->mwait(threadNumber, pkt);
    }
    void
    mwaitAtomic(gem5::ThreadContext *tc) override
    {
        return cpu->mwaitAtomic(threadNumber, tc, cpu->mmu);
    }
    AddressMonitor *
    getAddrMonitor() override
    {
        return cpu->getCpuAddrMonitor(threadNumber);
    }

  private:
    // hardware transactional memory
    uint64_t htmUid = -1;
    uint64_t htmDepth = 0;

  public:
#if TRACING_ON
    // // Value -1 indicates that particular phase
    // // hasn't happened (yet).
    // /** Tick records used for the pipeline activity viewer. */
    // Tick fetchTick = -1;      // instruction fetch is completed.
    // int32_t decodeTick = -1;  // instruction enters decode phase
    // // -- add by hongfei.liu ------------------------------------
    // int32_t predisqTick = -1;  // instruction enters predisq phase
    // int32_t dispipe0Tick = -1;  // instruction enters dispipe0 phase
    // int32_t dispipe1Tick = -1;  // instruction enters dispipe1 phase
    // int32_t dispipe2Tick = -1;  // instruction enters dispipe2 phase
    // int32_t dispipe3Tick = -1;  // instruction enters dispipe3 phase
    // // ----------------------------------------------------------
    // int32_t renameTick = -1;  // instruction enters rename phase
    // int32_t dispatchTick = -1;
    // int32_t issueTick = -1;
    // int32_t completeTick = -1;
    // int32_t commitTick = -1;
    // int32_t storeTick = -1;
#endif
 // Value -1 indicates that particular phase
    // hasn't happened (yet).
    /** Tick records used for the pipeline activity viewer. */
    Tick fetchTick = -1;      // instruction fetch is completed.
    int32_t bpu0Tick = -1;  // instruction enters bpu0 phase
    int32_t bpu1Tick = -1;  // instruction enters bpu1 phase
    int32_t IBandLBTick = -1;  // instruction enters IBandLB phase
    int32_t decodeTick = -1;  // instruction enters decode phase
    // -- add by hongfei.liu ------------------------------------
    int32_t predisqTick = -1;  // instruction enters predisq phase
    int32_t dispipe0Tick = -1;  // instruction enters dispipe0 phase
    int32_t dispipe1Tick = -1;  // instruction enters dispipe1 phase
    int32_t dispipe2Tick = -1;  // instruction enters dispipe2 phase
    int32_t dispipe3Tick = -1;  // instruction enters dispipe3 phase
    // ----------------------------------------------------------
    int32_t renameTick = -1;  // instruction enters rename phase
    int32_t dispatchTick = -1;
    int32_t issueTick = -1;
    int32_t completeTick = -1;
    int32_t commitTick = -1;
    int32_t storeTick = -1;

    int32_t robHeadTick = -1;

    int32_t loadExeTick = -1;
    int32_t loadBackTick = -1;

    int32_t intoWTBTick = -1;
    int32_t intoReadyInstsTick = -1;

    /* Values used by LoadToUse stat */
    Tick firstIssue = -1;
    Tick arrive_dc0 = -1;
    Tick lastWakeDependents = -1;
    Tick translatedTick = -1;

    Tick arrive_wtb = -1;
    Tick issued_tick = -1;
    //use to circle_inVectorQ
    Tick arrive_vectorQFront = -1;
    Tick readyToIssueQ = -1;

    Tick readyTick = -1;
    Tick completionTick = -1;

    int32_t arrive_predisq = -1;


    /** Reads a misc. register, including any side-effects the read
     * might have as defined by the architecture.
     */
    RegVal
    readMiscReg(int misc_reg) override
    {
        if (needFRM() && misc_reg == RiscvISA::MISCREG_FRM && rn_src_frm != 0)
            return frmVal();
        if (needVxrm() && misc_reg == RiscvISA::MISCREG_VXRM && rn_src_vxrm != 0)
            return vxrmVal();
        if (isCsrVl() && misc_reg == RiscvISA::MISCREG_VL && rn_src_vl != 0)
            return vlVal();
        if (isCsrVtype() && misc_reg == RiscvISA::MISCREG_VTYPE && rn_src_vtype != 0)
            return vtypeVal();
        
        return cpu->readMiscReg(misc_reg, threadNumber);
    }

    /** Sets a misc. register, including any side-effects the write
     * might have as defined by the architecture.
     */
    void
    setMiscReg(int misc_reg, RegVal val) override
    {
        /** Writes to misc. registers are recorded and deferred until the
         * commit stage, when updateMiscRegs() is called. First, check if
         * the misc reg has been written before and update its value to be
         * committed instead of making a new entry. If not, make a new
         * entry and record the write.
         */
        for (auto &idx: _destMiscRegIdx) {
            if (idx == misc_reg)
                return;
        }

        _destMiscRegIdx.push_back(misc_reg);
        _destMiscRegVal.push_back(val);
    }

    /** Reads a misc. register, including any side-effects the read
     * might have as defined by the architecture.
     */
    RegVal
    readMiscRegOperand(const StaticInst *si, int idx) override
    {
        const RegId& reg = si->srcRegIdx(idx);
        assert(reg.is(MiscRegClass));
        return cpu->readMiscReg(reg.index(), threadNumber);
    }

    /** Sets a misc. register, including any side-effects the write
     * might have as defined by the architecture.
     */
    void
    setMiscRegOperand(const StaticInst *si, int idx, RegVal val) override
    {
        const RegId& reg = si->destRegIdx(idx);
        assert(reg.is(MiscRegClass));
        setMiscReg(reg.index(), val);
    }

    /** Called at the commit stage to update the misc. registers. */
    void
    updateMiscRegs()
    {
        // @todo: Pretty convoluted way to avoid squashing from happening when
        // using the TC during an instruction's execution (specifically for
        // instructions that have side-effects that use the TC).  Fix this.
        // See cpu/rxuo3/dyn_inst_impl.hh.
        bool no_squash_from_TC = thread->noSquashFromTC;
        thread->noSquashFromTC = true;

        for (int i = 0; i < _destMiscRegIdx.size(); i++)
            cpu->setMiscReg(
                _destMiscRegIdx[i], _destMiscRegVal[i], threadNumber);

        thread->noSquashFromTC = no_squash_from_TC;
    }

    void
    forwardOldRegs()
    {

        for (int idx = 0; idx < numDestRegs(); idx++) {
            PhysRegIdPtr prev_phys_reg = prevDestIdx(idx);
            const RegId& original_dest_reg = staticInst->destRegIdx(idx);
            const auto bytes = original_dest_reg.regClass().regBytes();

            // Registers which aren't renamed don't need to be forwarded.
            if (!original_dest_reg.isRenameable())
                continue;

            if (bytes == sizeof(RegVal)) {
                setRegOperand(staticInst.get(), idx,
                        cpu->getReg(prev_phys_reg, threadNumber));
            } else {
                uint8_t val[original_dest_reg.regClass().regBytes()];
                cpu->getReg(prev_phys_reg, val, threadNumber);
                setRegOperand(staticInst.get(), idx, val);
            }
        }
    }
    /** Traps to handle specified fault. */
    void trap(const Fault &fault);

  public:

    // The register accessor methods provide the index of the
    // instruction's operand (e.g., 0 or 1), not the architectural
    // register index, to simplify the implementation of register
    // renaming.  We find the architectural register index by indexing
    // into the instruction's own operand index table.  Note that a
    // raw pointer to the StaticInst is provided instead of a
    // ref-counted StaticInstPtr to redice overhead.  This is fine as
    // long as these methods don't copy the pointer into any long-term
    // storage (which is pretty hard to imagine they would have reason
    // to do).

    RegVal
    getRegOperand(const StaticInst *si, int idx) override
    {
        const PhysRegIdPtr reg = renamedSrcIdx(idx);
        if (reg->is(InvalidRegClass))
            return 0;
        return cpu->getReg(reg, threadNumber);
    }

    void
    getRegOperand(const StaticInst *si, int idx, void *val) override
    {
        PhysRegIdPtr reg = renamedSrcIdx(idx);
        if (reg->is(InvalidRegClass))
            return;
        cpu->getReg(reg, val, threadNumber);
    }

    void
    getVs1RegOperand(const StaticInst *si, void *val) override
    {
        PhysRegIdPtr reg = si->getRenamedVs1Idx();
        if (reg->is(InvalidRegClass))
            return;
        cpu->getReg(reg, val, threadNumber);
    }

    void *
    getWritableRegOperand(const StaticInst *si, int idx) override
    {
        if (isSpecialVector() && isMacroVector()) {
            return cpu->getWritableReg(si->getRenamedDestIdx(), threadNumber);
        }
        return cpu->getWritableReg(renamedDestIdx(idx), threadNumber);
    }

    void *getMergeBufferEntry(InstSeqNum sn) {
        return cpu->getMergeBufferEntry(sn);
    }

    bool MBEntryAlreadySetOldVd(InstSeqNum sn) {
        return cpu->MBEntryAlreadySetOldVd(sn);
    }

    void decreaseMBEntryEopNum(InstSeqNum sn) {
        cpu->decreaseMBEntryEopNum(sn);
    }

    /** @todo: Make results into arrays so they can handle multiple dest
     *  registers.
     */
    void
    setRegOperand(const StaticInst *si, int idx, RegVal val) override
    {
        const PhysRegIdPtr reg = renamedDestIdx(idx);
        if (reg->is(InvalidRegClass))
            return;
        cpu->setReg(reg, val, threadNumber);
        setResult(reg->regClass(), val);
    }

    void
    setRegOperand(const StaticInst *si, int idx, const void *val) override
    {
        const PhysRegIdPtr reg = renamedDestIdx(idx);
        if (reg->is(InvalidRegClass))
            return;
        cpu->setReg(reg, val, threadNumber);
        setResult(reg->regClass(), val);
    }
     /// Increment the reference count
    void incref() const { ++count;
    //DPRINTF(RxuDynInst,"DynInst: [sn:%lli],mac:%i.count : %i->%i\n",seqNum, staticInst->isMacroop(),count-1,count);
    }

    /// Decrement the reference count and destroy the object if all
    /// references are gone.
    void
    decref() const
    {   
        //DPRINTF(RxuDynInst,"DynInst: [sn:%lli],mac:%i.count : %i->%i\n",seqNum, staticInst->isMacroop(), count,count-1);
        if (--count <= 0)
            delete this;
    }


    void printDisassembly() const
    {
        DPRINTF(CommitTrace,
                "[sn:%lu] pc:%#lx %s, rdy: %lu, comp: %lu, addr: %#lx\n",
                seqNum, pcState().instAddr(),
                staticInst->disassemble(pcState().instAddr()).c_str(),
                readyTick, completionTick, physEffAddr);
    }

    unsigned getInstBytes()
    {
        RiscvISA::PCState rpc = pc->as<RiscvISA::PCState>();
        return rpc.compressed() ? 2 : 4;
    }

  protected:
    SquashVersion squashVer;

  public:
    void setVersion(const SquashVersion &ver)
    {
        squashVer.update(ver.getVersion());
    }
    uint8_t getVersion()
    {
        return squashVer.getVersion();
    }


    Addr getPC()
    {
        return pc->instAddr();
    }

    Addr getNPC()
    {
        return pc->as<RiscvISA::PCState>().npc();
    }

    bool branching()
    {
        return pc->as<RiscvISA::PCState>().branching();
    }
};

} // namespace rxuo3
} // namespace gem5

#endif // __CPU_RxuO3_DYN_INST_HH__
