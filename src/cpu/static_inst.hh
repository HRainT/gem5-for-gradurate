/*
 * Copyright (c) 2017, 2020, 2023 Arm Limited
 * Copyright (c) 2022-2023 The University of Edinburgh
 * All rights reserved
 *
 * The license below extends only to copyright in the software and shall
 * not be construed as granting a license to any other intellectual
 * property including but not limited to intellectual property relating
 * to a hardware implementation of the functionality of the software
 * licensed hereunder.  You may use the software subject to the license
 * terms below provided that you ensure that this notice is replicated
 * unmodified and in its entirety in all distributions of the software,
 * modified or unmodified, in source code or in binary form.
 *
 * Copyright (c) 2003-2005 The Regents of The University of Michigan
 * Copyright (c) 2013 Advanced Micro Devices, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __CPU_STATIC_INST_HH__
#define __CPU_STATIC_INST_HH__

#include <array>
#include <bitset>
#include <cstdint>
#include <memory>
#include <string>

#include "arch/generic/pcstate.hh"
#include "base/logging.hh"
#include "base/refcnt.hh"
#include "cpu/op_class.hh"
#include "cpu/reg_class.hh"
#include "cpu/static_inst_fwd.hh"
#include "enums/StaticInstFlags.hh"
#include "sim/byteswap.hh"
#include "arch/riscv/types.hh"

namespace gem5
{

// forward declarations
class Packet;

class ExecContext;
class ThreadContext;

namespace loader
{
class SymbolTable;
} // namespace loader

namespace trace
{
class InstRecord;
} // namespace trace

/**
 * Base, ISA-independent static instruction class.
 *
 * The main component of this class is the vector of flags and the
 * associated methods for reading them.  Any object that can rely
 * solely on these flags can process instructions without being
 * recompiled for multiple ISAs.
 */
class StaticInst : public RefCounted, public StaticInstFlags
{
  public:
    using RegIdArrayPtr = RegId (StaticInst:: *)[];
    uint32_t _vdRegIdx;
    std::deque<uint32_t> _vdElemIdx;
    uint32_t _vs2RegIdx;
    std::deque<uint32_t> _vs2ElemIdx;
    uint32_t _vs3RegIdx;
    std::deque<uint32_t> _vs3ElemIdx;
    std::deque<uint32_t> _microIdx_q;
    std::deque<uint32_t> _seg_regidx_q;
    std::deque<uint32_t> _seg_elemidx_q;

    uint32_t _regIdx;
    uint32_t _microVl;
    uint32_t _microIdx;
    std::deque<uint32_t> _elemIdx_q;
    uint32_t _field;
    uint32_t _field_x;
    uint32_t _numFields;
    uint32_t _numMicroops;

    /** For mask index vload/store */
    mutable bool vElemMask = false;

    /** Eliminate differences of RxuO3 and O3 commint insts log. */
    mutable bool canPrintLog = true;

  private:
    /// See srcRegIdx().
    RegIdArrayPtr _srcRegIdxPtr = nullptr;

    /// See destRegIdx().
    RegIdArrayPtr _destRegIdxPtr = nullptr;

    PhysRegIdPtr _destIdx;
    PhysRegIdPtr _vs1Idx;

  protected:

    /// Flag values for this instruction.
    std::bitset<Num_Flags> flags;

    /// See opClass().
    OpClass _opClass;

    /// See numSrcRegs().
    uint8_t _numSrcRegs = 0;

    /// See numDestRegs().
    uint8_t _numDestRegs = 0;

    std::array<uint8_t, MiscRegClass + 1> _numTypedDestRegs = {};

  public:

    /// @name Register information.
    /// The sum of the different numDestRegs([type])-s equals numDestRegs().
    /// The per-type function is used to track physical register usage.
    //@{
    /// Number of source registers.
    uint8_t numSrcRegs()  const { return _numSrcRegs; }
    /// Number of destination registers.
    uint8_t numDestRegs() const { return _numDestRegs; }
    /// Number of destination registers of a particular type.
    uint8_t
    numDestRegs(RegClassType type) const
    {
        return _numTypedDestRegs[type];
    }
    //@}

    /// @name Flag accessors.
    /// These functions are used to access the values of the various
    /// instruction property flags.  See StaticInst::Flags for descriptions
    /// of the individual flags.
    //@{
    void setRenamedDestIdx(PhysRegIdPtr destIdx) {_destIdx = destIdx;}
    PhysRegIdPtr         getRenamedDestIdx() const {return _destIdx;}
    void setRenamedVs1Idx(PhysRegIdPtr destIdx) {_vs1Idx = destIdx;}
    PhysRegIdPtr         getRenamedVs1Idx() const {return _vs1Idx;}
    uint32_t             getVdRegIdx() {return _vdRegIdx;}
    std::deque<uint32_t> getVdElmIdx() {return _vdElemIdx;}
    uint32_t             getVs2RegIdx() {return _vs2RegIdx;}
    std::deque<uint32_t> getVs2ElmIdx() {return _vs2ElemIdx;}
    uint32_t             getVs3RegIdx() {return _vs3RegIdx;}
    std::deque<uint32_t> getVs3ElmIdx() {return _vs3ElemIdx;}
    std::deque<uint32_t> getMicroIdx() {return _microIdx_q;}
    uint32_t             getMicroIdx_vsseg() {return _microIdx;}
    uint32_t             getField_x() {return _field_x;}
    uint32_t             getRegIdx() {return _regIdx;}
    uint32_t             getMicroVl() {return _microVl;}
    uint32_t             getMicroIdx_uop() {return _microIdx;}
    std::deque<uint32_t> getElemIdx() {return _elemIdx_q;}
    uint32_t             getField() {return _field;}
    uint32_t             getNumFields() {return _numFields;}
    uint32_t             getNumMicroops() {return _numMicroops;}
    std::deque<uint32_t> getSeg_regidx() {return _seg_regidx_q;}
    std::deque<uint32_t> getSeg_elemidx() {return _seg_elemidx_q;}
    bool isOrderInst() const{return flags[IsOrder];}
    bool isNonSplitVector() const { return flags[IsNonSplitVector]; }
    bool isVlff()           const { return flags[IsVlff]; }
    bool isSplitUop()       const { return flags[IsSplitUop]; }
    bool isEop()            const { return flags[IsEop]; }
    bool isSplitMacro()     const { return flags[IsSplitMacro]; }
    bool isSpecialVector()  const { return flags[IsSpecialVector]; }
    bool isSpecialVectorNotExec()  const { return flags[IsSpecialVectorNotExec]; }
    bool isRxuVmInst()  const { return flags[IsRxuVmInst]; }
    bool isNop()          const { return flags[IsNop]; }
    bool isStrideIndex()          const { return (_opClass == 62 || _opClass == 63 || _opClass == 64 || _opClass == 65); }
    // bool isStrideIndex()        { return getName().substr(0, 7) == "vluxei8"; }
    bool
    isMemRef() const
    {
        return flags[IsLoad] || flags[IsStore] || flags[IsAtomic];
    }
    bool isLoad()         const { return flags[IsLoad]; }
    bool isStore()        const { return flags[IsStore]; }
    bool isAtomic()       const { return flags[IsAtomic]; }
    bool isStoreConditional()     const { return flags[IsStoreConditional]; }
    bool isInstPrefetch() const { return flags[IsInstPrefetch]; }
    bool isDataPrefetch() const { return flags[IsDataPrefetch]; }
    bool isPrefetch()     const { return isInstPrefetch() ||
                                         isDataPrefetch(); }

    bool isInteger()      const { return flags[IsInteger]; }
    bool isFloating()     const { return flags[IsFloating]; }
    bool isVector()       const { return flags[IsVector]; }
    bool isVset()               { return getName().substr(0, 4) == "vset";}
    bool isVsetivli()           { return getName().substr(0, 8) == "vsetivli"; }
    bool isVsetvli()            { return getName().substr(0, 7) == "vsetvli"; }
    bool isVsetvl()             { return getName().substr(0, 6) == "vsetvl"; }
    bool isVluxei8()            { return getName().substr(0, 7) == "vluxei8";}
    bool isMatrix()       const { return flags[IsMatrix]; }

    bool isControl()      const { return flags[IsControl]; }
    bool isCall()         const { return flags[IsCall]; }
    bool isReturn()       const { return flags[IsReturn]; }
    bool isDirectCtrl()   const { return flags[IsDirectControl]; }
    bool isIndirectCtrl() const { return flags[IsIndirectControl]; }
    bool isCondCtrl()     const { return flags[IsCondControl]; }
    bool isUncondCtrl()   const { return flags[IsUncondControl]; }

    bool isSerializing()  const { return flags[IsSerializing] ||
                                      flags[IsSerializeBefore] ||
                                      flags[IsSerializeAfter]; }
    bool isSerializeBefore() const { return flags[IsSerializeBefore]; }
    bool isSerializeAfter() const { return flags[IsSerializeAfter]; }
    bool isSquashAfter() const { return flags[IsSquashAfter]; }
    bool
    isFullMemBarrier() const
    {
        return flags[IsReadBarrier] && flags[IsWriteBarrier];
    }
    bool isReadBarrier() const { return flags[IsReadBarrier]; }
    bool isWriteBarrier() const { return flags[IsWriteBarrier]; }
    bool isNonSpeculative() const { return flags[IsNonSpeculative]; }
    bool isQuiesce() const { return flags[IsQuiesce]; }
    bool isUnverifiable() const { return flags[IsUnverifiable]; }
    bool isPseudo() const { return flags[IsPseudo]; }
    bool isSyscall() const { return flags[IsSyscall]; }
    bool isMacroop() const { return flags[IsMacroop]; }
    bool isMicroop() const { return flags[IsMicroop]; }
    bool isDelayedCommit() const { return flags[IsDelayedCommit]; }
    bool isLastMicroop() const { return flags[IsLastMicroop]; }
    bool isFirstMicroop() const { return flags[IsFirstMicroop]; }
    // hardware transactional memory
    // HtmCmds must be identified as such in order
    // to provide them with necessary memory ordering semantics.
    bool isHtmStart() const { return flags[IsHtmStart]; }
    bool isHtmStop() const { return flags[IsHtmStop]; }
    bool isHtmCancel() const { return flags[IsHtmCancel]; }

    bool isInvalid() const { return flags[IsInvalid]; }

    void clearSerializeAfter() { flags.reset(IsSerializeAfter); }

    void clearSerializeBefore() { flags.reset(IsSerializeBefore); }

    bool
    isHtmCmd() const
    {
        return isHtmStart() || isHtmStop() || isHtmCancel();
    }
    //@}

    void setFirstMicroop() { flags[IsFirstMicroop] = true; }
    void setLastMicroop() { flags[IsLastMicroop] = true; }
    void setDelayedCommit() { flags[IsDelayedCommit] = true; }
    void setFlag(Flags f) { flags[f] = true; }
    void resetFlag(Flags f) { flags[f] = false; }

    bool isVxsat() const {return flags[IsVxsat];};

    // ----------------------------------------------------------------------// -------- added by longting.du ----------------------------------------
    #define _opcode machInst.opcode

    bool isCompressed() { return (_opcode & 0x3) < 0x3; }

    bool isRxuLUI() { return machInst.opcode == 0x37; }
    bool isRxuAuipc() { return machInst.opcode == 0x17; }
    bool isRxuLoad() { return machInst.opcode == 0x3; }
    bool isRxuStore() { return machInst.opcode == 0x23; }
    bool isRxuJump() { return (machInst.opcode == 0x6f) || (machInst.opcode == 0x67); }
    bool isRxuBranch() { return machInst.opcode == 0x63; }
    bool isRxuLoadFP() { return machInst.opcode == 0x7; }
    bool isRxuStoreFP() { return machInst.opcode == 0x27; }
    bool isRxuSys() { return machInst.opcode == 0x73; }
    bool isRxuMiscMem() { return machInst.opcode == 0xf; }
    uint8_t rxuFunc3() { return isRxuMiscMem() ? 0b001 : machInst.round_mode; }
    bool rxuExtraFlag() { return machInst.bit30 || rxuFunc3() == 0b010 || rxuFunc3() == 0b011; }
    bool rxuMulDivFlag() { return machInst.rl && (isRxuOp() || isRxuOp32()) && !isRxuMiscMem(); }
    uint8_t rxuFunc2() { return ((rxuExtraFlag() ? 1 : 0) << 1) | (rxuMulDivFlag() ? 1 : 0); }
    bool isRxuIntCostom() { return machInst.opcode == 0b1011 && 
                                    ((rxuFunc3() == 0 || rxuFunc3() == 1) 
                                    || (rxuFunc3() == 0b011)); }
    bool isRxuOp() { return machInst.opcode == 0b0110011; }
    bool isRxuOp32() { return machInst.opcode == 0b0111011; }
    bool isRxuOpImm() { return machInst.opcode == 0b0010011; }
    bool isRxuOpImm32() { return machInst.opcode == 0b0011011; }
    bool isRxuCsr() { return isRxuSys() && (machInst.round_mode > 0); }
    bool isRxuCal() { return isRxuOp()   || isRxuOp32()   ||
              isRxuOpImm() || isRxuOpImm32() || isRxuMiscMem();  }

    bool isRxuIntDiv() { return isRxuCal() && (rxuFunc2() & 1) && rxuFunc3() > 3; }

    bool isRxuInteger() {  
      return  isRxuLUI() || isRxuAuipc()  || isRxuJump() || isRxuBranch() ||
              isRxuLoad() || isRxuStore() || isRxuOp()   || isRxuOp32()   ||
              isRxuOpImm() || isRxuOpImm32() || isRxuSys() || isRxuMiscMem() || 
              isRxuIntCostom();
    }

    bool isRxuFMAdd() { return _opcode == 0b1000011; }
    bool isRxuFMSub() { return _opcode == 0b1000111; }
    bool isRxuFNMSub() { return _opcode == 0b1001011; }
    bool isRxuFNMAdd() { return _opcode == 0b1001111; }
    bool isRxuFpCustom() { return _opcode == 0b0001011 && machInst.round_mode == 0b010; }
    bool isRxuOpFp() { return _opcode == 0b1010011 || isRxuFpCustom(); }
    uint32_t rxuFunc10() {
      return isRxuFpCustom() ? (0b0011000000 | machInst.funct2) : 
                    ((machInst.vfunct5 << 5) | ((machInst.rs2 & 0b11) << 3) | (machInst.round_mode));
    }
    bool RxuAccrcy() { return machInst.rl || isRxuFpCustom(); }
    bool isRxuFpDiv() { 
      return isRxuOpFp() && ((rxuFunc10() & 0b1110000000) <= 0b1111111) && ((rxuFunc10() & 0b1100000) >= 0b1100000);
    }

    bool isRxuFloating() {
      return isRxuFMAdd() || isRxuFMSub() || isRxuFNMSub() || isRxuFNMAdd() || isRxuOpFp();
    }

    bool isDiv() { return (opClass() == IntDivOp) || (opClass() == FloatDivOp); }

    bool isJump() {
      return isCondCtrl() || isUncondCtrl();
    }

    bool isCsr() {
      bool hasDstCsr = numDestRegs(MiscRegClass) > 0;
      if (hasDstCsr)  return true;
      std::string name = getName();
      return (name.substr(0, 3) == "csr");
    }

    bool isBranch() {
      return isControl();
      // return machInst.opcode == 0x63;
    }

    bool isIntNormal() {
      return isInteger() && 
            !isBranch() && !(isLoad() || isStore()) && !isDiv() && !isCsr() && !isJump();
    }

    bool isRxuIntNormal() {
      // return isRxuInteger() && !isRxuBranch() && !(isRxuLoad() || isRxuStore()) && !isDiv() && !isRxuCsr() && !isRxuJump();
      return isRxuInteger() && 
                  (isRxuLUI() || isRxuAuipc() || (isRxuCal() && !isRxuIntDiv()));
    }

    bool isRxuIntSpecial() {
      return isRxuInteger() && 
                  ( isRxuBranch() || isRxuIntDiv() || isRxuCsr() || isRxuJump() || isRxuSys()); 
    }

    bool isRxuFloatNormal() {
      return isRxuFloating() && 
                  (isRxuFMAdd() || isRxuFMSub() || isRxuFNMAdd() || isRxuFNMSub() ||
                  (isRxuOpFp() && (rxuFunc10() < 0b0100000000))) && !isRxuFpDiv();
    }

    bool isRxuFloatSpecial() {
      return isRxuFloating() && isRxuOpFp() && 
                  (isRxuFpDiv() || ((rxuFunc10() & 0b1100000000) > 0b11111111));
    }

    bool isIntSpec() {
      return isInteger() && !isIntNormal();
    }

    bool isFloatNormal() {
      return isFloating() &&
            ((opClass() == FloatAddOp) || (opClass() == FloatMultOp) || isJump());
    }

    bool isFloatSpec() {
      return isFloating() && !isFloatNormal();
    }

    bool isFence_i() {
      return machInst.all == 0x100f;
    }

    // ----------------------------------------------------------------------

    /// Operation class.  Used to select appropriate function unit in issue.
    OpClass opClass() const { return _opClass; }


    /// Return logical index (architectural reg num) of i'th destination reg.
    /// Only the entries from 0 through numDestRegs()-1 are valid.
    const RegId &destRegIdx(int i) const { return (this->*_destRegIdxPtr)[i]; }

    void
    setDestRegIdx(int i, const RegId &val)
    {
        (this->*_destRegIdxPtr)[i] = val;
    }

    /// Return logical index (architectural reg num) of i'th source reg.
    /// Only the entries from 0 through numSrcRegs()-1 are valid.
    const RegId &srcRegIdx(int i) const { return (this->*_srcRegIdxPtr)[i]; }

    void
    setSrcRegIdx(int i, const RegId &val)
    {
        (this->*_srcRegIdxPtr)[i] = val;
    }

    /// Pointer to a statically allocated "null" instruction object.
    static StaticInstPtr nullStaticInstPtr;

    virtual uint64_t getEMI() const { return 0; }

  protected:

    /**
     * Set the pointers which point to the arrays of source and destination
     * register indices. These will be defined in derived classes which know
     * what size they need to be, and installed here so they can be accessed
     * with the base class accessors.
     */
    void
    setRegIdxArrays(RegIdArrayPtr src, RegIdArrayPtr dest)
    {
        _srcRegIdxPtr = src;
        _destRegIdxPtr = dest;
    }

    /**
     * Instruction size in bytes. Necessary for dynamic instruction sizes
     */
    size_t _size = 0;

    /**
     * Base mnemonic (e.g., "add").  Used by generateDisassembly()
     * methods.  Also useful to readily identify instructions from
     * within the debugger when #cachedDisassembly has not been
     * initialized.
     */
    const char *mnemonic;

    /**
     * String representation of disassembly (lazily evaluated via
     * disassemble()).
     */
    mutable std::unique_ptr<std::string> cachedDisassembly;

    /**
     * Internal function to generate disassembly string.
     */
    virtual std::string generateDisassembly(
            Addr pc, const loader::SymbolTable *symtab) const = 0;

    /// Constructor.
    /// It's important to initialize everything here to a sane
    /// default, since the decoder generally only overrides
    /// the fields that are meaningful for the particular
    /// instruction.
    StaticInst(const char *_mnemonic, OpClass op_class)
        : _opClass(op_class), mnemonic(_mnemonic)
    {}

    StaticInst(const char *_mnemonic, OpClass op_class, RiscvISA::ExtMachInst _machInst
              ,uint32_t _vdRegIdx = 0, std::deque<uint32_t> _vdElemIdx = {},uint32_t _vs2RegIdx = 0
              , std::deque<uint32_t> _vs2ElemIdx = {},uint32_t _vs3RegIdx = 0
              , std::deque<uint32_t> _vs3ElemIdx = {}, std::deque<uint32_t> _microIdx_q = {}, uint32_t _regIdx = 0, uint32_t _microVl = 0,
              uint32_t _microIdx = 0, std::deque<uint32_t> _elemIdx_q = {}, uint32_t _field = 0, uint32_t _numMicroops = 0, uint32_t _numFields = 0,
              std::deque<uint32_t> _seg_regidx_q = {}, std::deque<uint32_t> _seg_elemidx_q = {},uint32_t _field_x = 0)
        : _opClass(op_class), mnemonic(_mnemonic), machInst(_machInst), _vdRegIdx(_vdRegIdx)
        , _vdElemIdx(_vdElemIdx), _vs2RegIdx(_vs2RegIdx), _vs2ElemIdx(_vs2ElemIdx), _vs3RegIdx(_vs3RegIdx), _vs3ElemIdx(_vs3ElemIdx),
          _microIdx_q(_microIdx_q), _regIdx(_regIdx), _microVl(_microVl), _microIdx(_microIdx), _elemIdx_q(_elemIdx_q), _field(_field),
           _numMicroops(_numMicroops), _numFields(_numFields), _seg_regidx_q(_seg_regidx_q), _seg_elemidx_q(_seg_elemidx_q),_field_x(_field_x)
    {}

  public:
    RiscvISA::ExtMachInst machInst;

    virtual ~StaticInst() {};

    virtual Fault execute(ExecContext *xc,
            trace::InstRecord *traceData) const = 0;

    virtual Fault
    initiateAcc(ExecContext *xc, trace::InstRecord *traceData) const
    {
        panic("initiateAcc not defined!");
    }

    virtual Fault
    completeAcc(Packet *pkt, ExecContext *xc,
            trace::InstRecord *trace_data) const
    {
        panic("completeAcc not defined!");
    }

    virtual void advancePC(PCStateBase &pc_state) const = 0;
    virtual void advancePC(ThreadContext *tc) const;

    virtual std::unique_ptr<PCStateBase>
    buildRetPC(const PCStateBase &cur_pc, const PCStateBase &call_pc) const
    {
        panic("buildRetPC not defined!");
    }

    size_t size() const
    {
        if (_size == 0) fatal(
            "Instruction size for this instruction not set! It's size is "
            "required for the decoupled front-end. Either use the standard "
            "front-end or this ISA needs to be extended with the instruction "
            "size. Refer to the X86, Arm or RiscV decoders for an example.");
        return _size;
    }
    virtual void size(size_t newSize) { _size = newSize; }

    /**
     * Return the microop that goes with a particular micropc. This should
     * only be defined/used in macroops which will contain microops
     */
    virtual StaticInstPtr fetchMicroop(MicroPC upc) const;

    /**
     * Return the target address for a PC-relative branch.
     * Invalid if not a PC-relative branch (i.e. isDirectCtrl()
     * should be true).
     */
    virtual std::unique_ptr<PCStateBase> branchTarget(
            const PCStateBase &pc) const;

    /**
     * Return the target address for an indirect branch (jump).  The
     * register value is read from the supplied thread context, so
     * the result is valid only if the thread context is about to
     * execute the branch in question.  Invalid if not an indirect
     * branch (i.e. isIndirectCtrl() should be true).
     */
    virtual std::unique_ptr<PCStateBase> branchTarget(
            ThreadContext *tc) const;

    /**
     * Return string representation of disassembled instruction.
     * The default version of this function will call the internal
     * virtual generateDisassembly() function to get the string,
     * then cache it in #cachedDisassembly.  If the disassembly
     * should not be cached, this function should be overridden directly.
     */
    virtual const std::string &disassemble(Addr pc,
        const loader::SymbolTable *symtab=nullptr) const;

    /**
     * Print a separator separated list of this instruction's set flag
     * names on the given stream.
     */
    void printFlags(std::ostream &outs, const std::string &separator) const;

    /// Return name of machine instruction
    std::string getName() { return mnemonic; }

  protected:
    template<typename T>
    size_t
    simpleAsBytes(void *buf, size_t max_size, const T &t)
    {
        size_t size = sizeof(T);
        if (size <= max_size)
            *reinterpret_cast<T *>(buf) = htole<T>(t);
        return size;
    }

  public:
    /**
     * Instruction classes can override this function to return a
     * a representation of themselves as a blob of bytes, generally assumed to
     * be that instructions ExtMachInst.
     *
     * buf is a buffer to hold the bytes.
     * max_size is the size allocated for that buffer by the caller.
     * The return value is how much data was actually put into the buffer,
     * zero if no data was put in the buffer, or the necessary size of the
     * buffer if there wasn't enough space.
     */
    virtual size_t asBytes(void *buf, size_t max_size) { return 0; }
};

} // namespace gem5

#endif // __CPU_STATIC_INST_HH__
