/*
 * Copyright (c) 2022 PLCT Lab
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

#ifndef __ARCH_RISCV_INSTS_VECTOR_HH__
#define __ARCH_RISCV_INSTS_VECTOR_HH__

#include <string>

#include "arch/riscv/faults.hh"
#include "arch/riscv/insts/static_inst.hh"
#include "arch/riscv/isa.hh"
#include "arch/riscv/regs/misc.hh"
#include "arch/riscv/utility.hh"
#include "cpu/exec_context.hh"
#include "cpu/static_inst.hh"

namespace gem5
{

namespace RiscvISA
{

float
getVflmul(uint32_t vlmul_encoding);

inline uint32_t
getSew(uint32_t vsew)
{
    assert(vsew <= 3);
    return (8 << vsew);
}

uint32_t
getVlmax(VTYPE vtype, uint32_t vlen);

/**
 * Base class for Vector Config operations
 */
class VConfOp : public RiscvStaticInst
{
  protected:
    uint64_t bit30;
    uint64_t bit31;
    uint64_t zimm10;
    uint64_t zimm11;
    uint64_t uimm;
    uint32_t elen;
    VConfOp(const char *mnem, ExtMachInst _extMachInst,
            uint32_t _elen, OpClass __opClass)
        : RiscvStaticInst(mnem, _extMachInst, __opClass),
          bit30(_extMachInst.bit30), bit31(_extMachInst.bit31),
          zimm10(_extMachInst.zimm_vsetivli),
          zimm11(_extMachInst.zimm_vsetvli),
          uimm(_extMachInst.uimm_vsetivli),
          elen(_elen)
    {
        this->flags[IsVector] = true;
    }

    std::string generateDisassembly(
        Addr pc, const loader::SymbolTable *symtab) const override;

    std::string generateZimmDisassembly() const;
};

inline uint8_t checked_vtype(bool vill, uint8_t vtype) {
    panic_if(vill, "vill has been set");
    const uint8_t vsew = bits(vtype, 5, 3);
    panic_if(vsew >= 0b100, "vsew: %#x not supported", vsew);
    const uint8_t vlmul = bits(vtype, 2, 0);
    panic_if(vlmul == 0b100, "vlmul: %#x not supported", vlmul);
    return vtype;
}

class VectorNonSplitInst : public RiscvStaticInst
{
  protected:
    uint32_t vl;
    uint8_t vtype;
    VectorNonSplitInst(const char* mnem, ExtMachInst _machInst,
                   OpClass __opClass)
        : RiscvStaticInst(mnem, _machInst, __opClass),
        vl(_machInst.vl),
        vtype(_machInst.vtype8)
    {
        this->flags[IsVector] = true;

        this->flags[IsNonSplitVector] = true;
    }

    std::string generateDisassembly(
        Addr pc, const loader::SymbolTable *symtab) const override;
};

class VectorMacroInst : public RiscvMacroInst
{
  protected:
    uint32_t vl;
    uint8_t vtype;
    uint32_t vlen;

    VectorMacroInst(const char* mnem, ExtMachInst _machInst,
                   OpClass __opClass, uint32_t _vlen = 256)
        : RiscvMacroInst(mnem, _machInst, __opClass),
        vl(_machInst.vl),
        vtype(_machInst.vtype8),
        vlen(_vlen)
    {
        this->flags[IsVector] = true;
    }
};

class VectorMicroInst : public RiscvMicroInst
{
protected:
    uint32_t vlen;
    uint32_t microVl;
    uint32_t microIdx;
    uint8_t vtype;
    VectorMicroInst(const char *mnem, ExtMachInst _machInst, OpClass __opClass,
      uint32_t _microVl, uint32_t _microIdx, uint32_t _vdRegIdx = 0, 
      std::deque<uint32_t> _vdElemIdx = {}, uint32_t _vs2RegIdx = 0, std::deque<uint32_t> _vs2ElemIdx = {}, 
                uint32_t _vs3RegIdx = 0, std::deque<uint32_t> _vs3ElemIdx = {},uint32_t _vlen = 128, 
                std::deque<uint32_t> _microIdx_q = {}, uint32_t _regIdx = 0, std::deque<uint32_t> _elemIdx_q = {}, uint32_t _field = 0)
        : RiscvMicroInst(mnem, _machInst, __opClass, _vdRegIdx, _vdElemIdx, _vs2RegIdx, _vs2ElemIdx, 
          _vs3RegIdx, _vs3ElemIdx, _microIdx_q, _regIdx, _microVl, _microIdx, _elemIdx_q, _field),
        vlen(_vlen),
        microVl(_microVl),
        microIdx(_microIdx),
        vtype(_machInst.vtype8)
    {
        this->flags[IsVector] = true;
    }
};

class VectorNopMicroInst : public RiscvMicroInst
{
public:
    VectorNopMicroInst(ExtMachInst _machInst)
        : RiscvMicroInst("vnop", _machInst, No_OpClass)
    {
        this->flags[IsNop] = true;
        this->flags[IsVector] = true;
    }


    Fault execute(ExecContext* xc, trace::InstRecord* traceData)
        const override
    {
        return NoFault;
    }

    std::string generateDisassembly(Addr pc, const loader::SymbolTable *symtab)
      const override
    {
        std::stringstream ss;
        ss << mnemonic;
        return ss.str();
    }
};

class VectorArithMicroInst : public VectorMicroInst
{
protected:
    VectorArithMicroInst(const char *mnem, ExtMachInst _machInst,
                         OpClass __opClass, uint32_t _microVl,
                         uint32_t _microIdx)
        : VectorMicroInst(mnem, _machInst, __opClass, _microVl, _microIdx)
    {}

    std::string generateDisassembly(
            Addr pc, const loader::SymbolTable *symtab) const override;
};

class VectorArithMacroInst : public VectorMacroInst
{
  protected:
    VectorArithMacroInst(const char* mnem, ExtMachInst _machInst,
                         OpClass __opClass, uint32_t _vlen = 256)
        : VectorMacroInst(mnem, _machInst, __opClass, _vlen)
    {
        this->flags[IsVector] = true;
    }
    std::string generateDisassembly(
            Addr pc, const loader::SymbolTable *symtab) const override;
};

class VectorVMUNARY0MicroInst : public VectorMicroInst
{
protected:
    VectorVMUNARY0MicroInst(const char *mnem, ExtMachInst _machInst,
                         OpClass __opClass, uint32_t _microVl,
                         uint32_t _microIdx)
        : VectorMicroInst(mnem, _machInst, __opClass, _microVl, _microIdx)
    {}

    std::string generateDisassembly(
            Addr pc, const loader::SymbolTable *symtab) const override;
};

class VectorVMUNARY0MacroInst : public VectorMacroInst
{
  protected:
    VectorVMUNARY0MacroInst(const char* mnem, ExtMachInst _machInst,
                         OpClass __opClass, uint32_t _vlen)
        : VectorMacroInst(mnem, _machInst, __opClass, _vlen)
    {
        this->flags[IsVector] = true;
    }

    std::string generateDisassembly(
            Addr pc, const loader::SymbolTable *symtab) const override;
};

class VectorSlideMacroInst : public VectorMacroInst
{
  protected:
    VectorSlideMacroInst(const char* mnem, ExtMachInst _machInst,
                         OpClass __opClass, uint32_t _vlen = 256)
        : VectorMacroInst(mnem, _machInst, __opClass, _vlen)
    {
        this->flags[IsVector] = true;
    }

    std::string generateDisassembly(
            Addr pc, const loader::SymbolTable *symtab) const override;
};

class VectorSlideMicroInst : public VectorMicroInst
{
  protected:
    uint32_t vdIdx;
    uint32_t vs2Idx;
    VectorSlideMicroInst(const char *mnem, ExtMachInst _machInst,
                         OpClass __opClass, uint32_t _microVl,
                         uint32_t _microIdx, uint32_t _vdIdx, uint32_t _vs2Idx)
        : VectorMicroInst(mnem, _machInst, __opClass, _microVl, _microIdx)
        , vdIdx(_vdIdx), vs2Idx(_vs2Idx)
    {}

    std::string generateDisassembly(
            Addr pc, const loader::SymbolTable *symtab) const override;
};

class VectorMemMicroInst : public VectorMicroInst
{
  protected:
    uint32_t offset; // Used to calculate EA.
    Request::Flags memAccessFlags;
    VectorMemMicroInst(const char* mnem, ExtMachInst _machInst,
                       OpClass __opClass, uint32_t _microVl,
                       uint32_t _microIdx, uint32_t _offset, 
                       uint32_t _vdRegIdx = 0, std::deque<uint32_t> _vdElemIdx = {},
                       uint32_t _vs2RegIdx = 0,std::deque<uint32_t> _vs2ElemIdx = {},
                       uint32_t _vs3RegIdx = 0,std::deque<uint32_t> _vs3ElemIdx = {},
                       std::deque<uint32_t> _microIdx_q = {}, uint32_t _regIdx = 0)
        : VectorMicroInst(mnem, _machInst, __opClass, _microVl, _microIdx, _vdRegIdx, _vdElemIdx, _vs2RegIdx, _vs2ElemIdx,_vs3RegIdx,_vs3ElemIdx,
          128,_microIdx_q, _regIdx)
        , offset(_offset)
        , memAccessFlags(0)
    {}
};

class VectorMemMacroInst : public VectorMacroInst
{
  protected:
    VectorMemMacroInst(const char* mnem, ExtMachInst _machInst,
                        OpClass __opClass, uint32_t _vlen = 256)
        : VectorMacroInst(mnem, _machInst, __opClass, _vlen)
    {}
};

class VleMacroInst : public VectorMemMacroInst
{
  protected:
    VleMacroInst(const char* mnem, ExtMachInst _machInst,
                   OpClass __opClass, uint32_t _vlen)
        : VectorMemMacroInst(mnem, _machInst, __opClass, _vlen)
    {}

    std::string generateDisassembly(
            Addr pc, const loader::SymbolTable *symtab) const override;
};

class VseMacroInst : public VectorMemMacroInst
{
  protected:
    VseMacroInst(const char* mnem, ExtMachInst _machInst,
                   OpClass __opClass, uint32_t _vlen)
        : VectorMemMacroInst(mnem, _machInst, __opClass, _vlen)
    {}

    std::string generateDisassembly(
            Addr pc, const loader::SymbolTable *symtab) const override;
};

class VleMicroInst : public VectorMicroInst
{
  public:
    mutable bool trimVl;
    mutable uint32_t faultIdx;

  protected:
    Request::Flags memAccessFlags;

    VleMicroInst(const char *mnem, ExtMachInst _machInst,OpClass __opClass,
                  uint32_t _microVl, uint32_t _microIdx, uint32_t _vlen)
        : VectorMicroInst(mnem, _machInst, __opClass, _microVl,
                            _microIdx, _vlen)
        , trimVl(false), faultIdx(_microVl)
    {
        this->flags[IsLoad] = true;
    }

    std::string generateDisassembly(
        Addr pc, const loader::SymbolTable *symtab) const override;
};

class VseMicroInst : public VectorMicroInst
{
  protected:
    Request::Flags memAccessFlags;

    VseMicroInst(const char *mnem, ExtMachInst _machInst, OpClass __opClass,
                  uint32_t _microVl, uint32_t _microIdx, uint32_t _vlen)
        : VectorMicroInst(mnem, _machInst, __opClass, _microVl,
                            _microIdx, _vlen)
    {
        this->flags[IsStore] = true;
    }

    std::string generateDisassembly(
        Addr pc, const loader::SymbolTable *symtab) const override;
};

class VlWholeMacroInst : public VectorMemMacroInst
{
  protected:
    VlWholeMacroInst(const char *mnem, ExtMachInst _machInst,
                     OpClass __opClass, uint32_t _vlen)
      : VectorMemMacroInst(mnem, _machInst, __opClass, _vlen)
    {}

    std::string generateDisassembly(
      Addr pc, const loader::SymbolTable *symtab) const override;
};

class VlWholeMicroInst : public VectorMicroInst
{
  protected:
    Request::Flags memAccessFlags;

    VlWholeMicroInst(const char *mnem, ExtMachInst _machInst,
          OpClass __opClass, uint32_t _microVl, uint32_t _microIdx,
          uint32_t _vlen)
        : VectorMicroInst(mnem, _machInst, __opClass, _microVl,
                            _microIdx, _vlen)
    {}

    std::string generateDisassembly(
      Addr pc, const loader::SymbolTable *symtab) const override;
};

class VsWholeMacroInst : public VectorMemMacroInst
{
  protected:
    VsWholeMacroInst(const char *mnem, ExtMachInst _machInst,
                     OpClass __opClass, uint32_t _vlen)
        : VectorMemMacroInst(mnem, _machInst, __opClass, _vlen)
    {}

    std::string generateDisassembly(
            Addr pc, const loader::SymbolTable *symtab) const override;
};

class VsWholeMicroInst : public VectorMicroInst
{
  protected:
    Request::Flags memAccessFlags;

    VsWholeMicroInst(const char *mnem, ExtMachInst _machInst,
                      OpClass __opClass, uint32_t _microVl,
                      uint32_t _microIdx, uint32_t _vlen)
        : VectorMicroInst(mnem, _machInst, __opClass , _microVl,
                          _microIdx, _vlen)
    {}

    std::string generateDisassembly(
        Addr pc, const loader::SymbolTable *symtab) const override;
};

class VlStrideMacroInst : public VectorMemMacroInst
{
  protected:
    VlStrideMacroInst(const char* mnem, ExtMachInst _machInst,
                   OpClass __opClass, uint32_t _vlen)
        : VectorMemMacroInst(mnem, _machInst, __opClass, _vlen)
    {}

    std::string generateDisassembly(
            Addr pc, const loader::SymbolTable *symtab) const override;
};

class VlStrideMicroInst : public VectorMemMicroInst
{
  protected:
  uint32_t regIdx;
    VlStrideMicroInst(const char *mnem, ExtMachInst _machInst,
                      OpClass __opClass, uint32_t _regIdx,
                      uint32_t _microIdx, uint32_t _microVl)
        : VectorMemMicroInst(mnem, _machInst, __opClass, _microVl,
                             _microIdx, 0)
        , regIdx(_regIdx)
    {}

    std::string generateDisassembly(
        Addr pc, const loader::SymbolTable *symtab) const override;
};

class VlStrideRxuMacroInst : public VectorMemMacroInst
{
  protected:
    VlStrideRxuMacroInst(const char* mnem, ExtMachInst _machInst,
                   OpClass __opClass, uint32_t _vlen)
        : VectorMemMacroInst(mnem, _machInst, __opClass, _vlen)
    {
      this->flags[IsSplitMacro] = true;
    }

    std::string generateDisassembly(
            Addr pc, const loader::SymbolTable *symtab) const override;
};

class VlStrideRxuMicroInst : public VectorMemMicroInst
{
  protected:
  uint32_t regIdx;
    VlStrideRxuMicroInst(const char *mnem, ExtMachInst _machInst,
                      OpClass __opClass, uint32_t _regIdx,
                      std::deque<uint32_t> _microIdx_q, uint32_t _microVl)
        : VectorMemMicroInst(mnem, _machInst, __opClass, _microVl,
                             0, 0, 0,{},0,{},0,{},_microIdx_q, _regIdx)
        , regIdx(_regIdx)
    {
      this->flags[IsSplitUop] = true;
    }

    std::string generateDisassembly(
        Addr pc, const loader::SymbolTable *symtab) const override;
};

class VlStrideRxuUopMacroInst : public VectorMemMacroInst
{
  protected:
  uint32_t regIdx;
  std::deque<uint32_t> microIdx_q;
  uint64_t seqNum;
  uint32_t microVl;
    VlStrideRxuUopMacroInst(const char* mnem, ExtMachInst _machInst,
                   OpClass __opClass, uint32_t _vlen,uint32_t _regIdx,
                   std::deque<uint32_t> _microIdx_q,uint64_t _seqNum, uint32_t _microVl)
        : VectorMemMacroInst(mnem, _machInst, __opClass, _vlen)
    {
      regIdx = _regIdx;
      microIdx_q = _microIdx_q;
      seqNum = _seqNum;
      microVl = _microVl;
    }

    std::string generateDisassembly(
            Addr pc, const loader::SymbolTable *symtab) const override;
};

class VlStrideRxuUopMicroInst : public VectorMemMicroInst
{
  protected:
  uint32_t regIdx;
  uint32_t microIdx;
  uint64_t seqNum;
    VlStrideRxuUopMicroInst(const char *mnem, ExtMachInst _machInst,
                      OpClass __opClass, uint32_t _regIdx,
                      uint32_t _microIdx, uint32_t _microVl, uint64_t _seqNum)
        : VectorMemMicroInst(mnem, _machInst, __opClass, _microVl,
                             0, 0)
        , regIdx(_regIdx)
        , microIdx(_microIdx)
        , seqNum(_seqNum)
    {
      this->flags[IsEop] = true;
    }

    std::string generateDisassembly(
        Addr pc, const loader::SymbolTable *symtab) const override;
};

class VsStrideMacroInst : public VectorMemMacroInst
{
  protected:
    VsStrideMacroInst(const char* mnem, ExtMachInst _machInst,
                   OpClass __opClass, uint32_t _vlen)
        : VectorMemMacroInst(mnem, _machInst, __opClass, _vlen)
    {}

    std::string generateDisassembly(
            Addr pc, const loader::SymbolTable *symtab) const override;
};

class VsStrideMicroInst : public VectorMemMicroInst
{
  protected:
    uint32_t regIdx;
    VsStrideMicroInst(const char *mnem, ExtMachInst _machInst,
                      OpClass __opClass, uint32_t _regIdx,
                      uint32_t _microIdx, uint32_t _microVl)
        : VectorMemMicroInst(mnem, _machInst, __opClass, _microVl,
                             _microIdx, 0)
        , regIdx(_regIdx)
    {}

    std::string generateDisassembly(
        Addr pc, const loader::SymbolTable *symtab) const override;
};

class VsStrideRxuMacroInst : public VectorMemMacroInst
{
  protected:
    VsStrideRxuMacroInst(const char* mnem, ExtMachInst _machInst,
                   OpClass __opClass, uint32_t _vlen)
        : VectorMemMacroInst(mnem, _machInst, __opClass, _vlen)
    {
      this->flags[IsSplitMacro] = true;
    }

    std::string generateDisassembly(
            Addr pc, const loader::SymbolTable *symtab) const override;
};

class VsStrideRxuMicroInst : public VectorMemMicroInst
{
  protected:
    uint32_t regIdx;
    VsStrideRxuMicroInst(const char *mnem, ExtMachInst _machInst,
      OpClass __opClass, uint32_t _regIdx,
      std::deque<uint32_t> _microIdx_q, uint32_t _microVl)
      : VectorMemMicroInst(mnem, _machInst, __opClass, _microVl,
                      0, 0, 0,{},0,{},0,{},_microIdx_q, _regIdx)
        , regIdx(_regIdx)
    {
      this->flags[IsSplitUop] = true;
    }

    std::string generateDisassembly(
        Addr pc, const loader::SymbolTable *symtab) const override;
};

class VsStrideRxuUopMacroInst : public VectorMemMacroInst
{
  protected:
  uint32_t regIdx;
  std::deque<uint32_t> microIdx_q;
  uint64_t seqNum;
  uint32_t microVl;
    VsStrideRxuUopMacroInst(const char* mnem, ExtMachInst _machInst,
                  OpClass __opClass, uint32_t _vlen,uint32_t _regIdx,
                  std::deque<uint32_t> _microIdx_q,uint64_t _seqNum, uint32_t _microVl)
        : VectorMemMacroInst(mnem, _machInst, __opClass, _vlen)
    {
      regIdx = _regIdx;
      microIdx_q = _microIdx_q;
      seqNum = _seqNum;
      microVl = _microVl;
    }

    std::string generateDisassembly(
            Addr pc, const loader::SymbolTable *symtab) const override;
};

class VsStrideRxuUopMicroInst : public VectorMemMicroInst
{
  protected:
  uint32_t regIdx;
  uint32_t microIdx;
  uint64_t seqNum;
    VsStrideRxuUopMicroInst(const char *mnem, ExtMachInst _machInst,
                            OpClass __opClass, uint32_t _regIdx,
                            uint32_t _microIdx, uint32_t _microVl, uint64_t _seqNum)
        : VectorMemMicroInst(mnem, _machInst, __opClass, _microVl,
                            0, 0)
        , regIdx(_regIdx)
        , microIdx(_microIdx)
        , seqNum(_seqNum)
    {
      this->flags[IsEop] = true;
    }

    std::string generateDisassembly(
        Addr pc, const loader::SymbolTable *symtab) const override;
};

class VlIndexMacroInst : public VectorMemMacroInst
{
  protected:
    VlIndexMacroInst(const char* mnem, ExtMachInst _machInst,
                   OpClass __opClass, uint32_t _vlen)
        : VectorMemMacroInst(mnem, _machInst, __opClass, _vlen)
    {}

    std::string generateDisassembly(
            Addr pc, const loader::SymbolTable *symtab) const override;
};

class VlIndexMicroInst : public VectorMemMicroInst
{
  protected:
    uint32_t vdRegIdx;
    uint32_t vdElemIdx;
    uint32_t vs2RegIdx;
    uint32_t vs2ElemIdx;
    VlIndexMicroInst(const char *mnem, ExtMachInst _machInst,
                    OpClass __opClass, uint32_t _vdRegIdx, uint32_t _vdElemIdx,
                    uint32_t _vs2RegIdx, uint32_t _vs2ElemIdx)
        : VectorMemMicroInst(mnem, _machInst, __opClass, 1,
                             0, 0)
        , vdRegIdx(_vdRegIdx), vdElemIdx(_vdElemIdx)
        , vs2RegIdx(_vs2RegIdx), vs2ElemIdx(_vs2ElemIdx)
    {}

    std::string generateDisassembly(
        Addr pc, const loader::SymbolTable *symtab) const override;
};

class VsIndexMacroInst : public VectorMemMacroInst
{
  protected:
    VsIndexMacroInst(const char* mnem, ExtMachInst _machInst,
                   OpClass __opClass, uint32_t _vlen)
        : VectorMemMacroInst(mnem, _machInst, __opClass, _vlen)
    {}

    std::string generateDisassembly(
            Addr pc, const loader::SymbolTable *symtab) const override;
};

class VsIndexMicroInst : public VectorMemMicroInst
{
  protected:
    uint32_t vs3RegIdx;
    uint32_t vs3ElemIdx;
    uint32_t vs2RegIdx;
    uint32_t vs2ElemIdx;
    VsIndexMicroInst(const char *mnem, ExtMachInst _machInst,
                    OpClass __opClass, uint32_t _vs3RegIdx,
                    uint32_t _vs3ElemIdx, uint32_t _vs2RegIdx,
                    uint32_t _vs2ElemIdx)
        : VectorMemMicroInst(mnem, _machInst, __opClass, 1, 0, 0),
          vs3RegIdx(_vs3RegIdx), vs3ElemIdx(_vs3ElemIdx),
          vs2RegIdx(_vs2RegIdx), vs2ElemIdx(_vs2ElemIdx)
    {}

    std::string generateDisassembly(
        Addr pc, const loader::SymbolTable *symtab) const override;
};

class VMvWholeMacroInst : public VectorArithMacroInst
{
  protected:
    VMvWholeMacroInst(const char* mnem, ExtMachInst _machInst,
                         OpClass __opClass)
        : VectorArithMacroInst(mnem, _machInst, __opClass)
    {}

    std::string generateDisassembly(
            Addr pc, const loader::SymbolTable *symtab) const override;
};

class VMvWholeMicroInst : public VectorArithMicroInst
{
  protected:
    VMvWholeMicroInst(const char *mnem, ExtMachInst _machInst,
                         OpClass __opClass, uint32_t _microVl,
                         uint32_t _microIdx)
        : VectorArithMicroInst(mnem, _machInst, __opClass, _microVl, _microIdx)
    {}

    std::string generateDisassembly(
            Addr pc, const loader::SymbolTable *symtab) const override;
};


class VMaskMergeMicroInst : public VectorArithMicroInst
{
  private:
    RegId srcRegIdxArr[NumVecInternalRegs];
    RegId destRegIdxArr[1];

  public:
    uint32_t vlen;
    size_t elemSize;
    VMaskMergeMicroInst(ExtMachInst extMachInst,
        uint8_t _dstReg, uint8_t _numSrcs, uint32_t _vlen, size_t _elemSize);
    Fault execute(ExecContext *, trace::InstRecord *) const override;
    std::string generateDisassembly(Addr,
        const loader::SymbolTable *) const override;
};

class VxsatMicroInst : public VectorArithMicroInst
{
  private:
    bool* vxsat;
  public:
    VxsatMicroInst(bool* Vxsat, ExtMachInst extMachInst)
        : VectorArithMicroInst("vxsat_micro", extMachInst,
          SimdMiscOp, 0, 0)
    {
        vxsat = Vxsat;
    }
    Fault execute(ExecContext *, trace::InstRecord *) const override;
    std::string generateDisassembly(Addr, const loader::SymbolTable *)
        const override;
};

class VlFFTrimVlMicroOp : public VectorMicroInst
{
  private:
    RegId srcRegIdxArr[8];
    RegId destRegIdxArr[0];
    std::vector<StaticInstPtr>& microops;

  public:
    VlFFTrimVlMicroOp(ExtMachInst _machInst, uint32_t _microVl,
        uint32_t _microIdx, uint32_t _vlen,
        std::vector<StaticInstPtr>& _microops);
    uint32_t calcVl() const;
    Fault execute(ExecContext *, trace::InstRecord *) const override;
    std::unique_ptr<PCStateBase> branchTarget(ThreadContext *) const override;
    std::string generateDisassembly(Addr, const loader::SymbolTable *)
        const override;
};

class VlSegMacroInst : public VectorMemMacroInst
{
  protected:
    VlSegMacroInst(const char* mnem, ExtMachInst _machInst,
                   OpClass __opClass, uint32_t _vlen)
        : VectorMemMacroInst(mnem, _machInst, __opClass, _vlen)
    {}

    std::string generateDisassembly(
            Addr pc, const loader::SymbolTable *symtab) const override;
};

class VlSegMicroInst : public VectorMicroInst
{
  protected:
    Request::Flags memAccessFlags;
    uint8_t regIdx;

    VlSegMicroInst(const char *mnem, ExtMachInst _machInst,
                   OpClass __opClass, uint32_t _microVl,
                   uint32_t _microIdx, uint32_t _numMicroops,
                   uint32_t _field, uint32_t _numFields,
                   uint32_t _vlen)
        : VectorMicroInst(mnem, _machInst, __opClass, _microVl,
                          _microIdx, _vlen)
    {
      this->flags[IsLoad] = true;
    }

    std::string generateDisassembly(
        Addr pc, const loader::SymbolTable *symtab) const override;
};

class VlsSegRxuMacroInst : public VectorMemMacroInst
{
  protected:
  VlsSegRxuMacroInst(const char* mnem, ExtMachInst _machInst,
                   OpClass __opClass, uint32_t _vlen)
        : VectorMemMacroInst(mnem, _machInst, __opClass, _vlen)
    {
      this->flags[IsSplitMacro] = true;
    }

    std::string generateDisassembly(
            Addr pc, const loader::SymbolTable *symtab) const override;
};

class VlsSegRxuMicroInst : public VectorMicroInst
{
  protected:
    Request::Flags memAccessFlags;
    uint8_t regIdx;
    std::deque<uint32_t> elemIdx_q;
    VlsSegRxuMicroInst(const char *mnem, ExtMachInst _machInst,
                   OpClass __opClass, uint32_t _microVl,
                   uint32_t _microIdx, uint32_t _numMicroops,
                   uint32_t _field, uint32_t _numFields,
                   uint32_t _vlen, std::deque<uint32_t> _elemIdx_q)
        : VectorMicroInst(mnem, _machInst, __opClass, _microVl,
                          _microIdx, 0,{},0,{},0,{},_vlen,{},0,_elemIdx_q,_field)
    {
      elemIdx_q = _elemIdx_q;
      this->flags[IsSplitUop] = true;
    }

    std::string generateDisassembly(
        Addr pc, const loader::SymbolTable *symtab) const override;
};

class VlsSegRxuUopMacroInst : public VectorMemMacroInst
{
  uint32_t field;
  uint32_t microIdx;
  uint64_t seqNum;
  protected:
  VlsSegRxuUopMacroInst(const char* mnem, ExtMachInst _machInst,
                   OpClass __opClass, uint32_t _vlen, uint32_t _field, uint32_t _microIdx, uint64_t _seqNum)
        : VectorMemMacroInst(mnem, _machInst, __opClass, _vlen)
    {
      field = _field;
      microIdx = _microIdx;
      seqNum = _seqNum;
    }

    std::string generateDisassembly(
            Addr pc, const loader::SymbolTable *symtab) const override;
};

class VlsSegRxuUopMicroInst : public VectorMicroInst
{
  protected:
    Request::Flags memAccessFlags;
    uint8_t regIdx;
    uint32_t elemIdx;
    uint64_t seqNum;
    uint32_t field;
    uint32_t numFields;
    uint32_t numMicroops;
    VlsSegRxuUopMicroInst(const char *mnem, ExtMachInst _machInst,
                   OpClass __opClass, uint32_t _microVl,
                   uint32_t _microIdx, uint32_t _numMicroops,
                   uint32_t _field, uint32_t _numFields,
                   uint32_t _vlen,uint32_t _elemIdx, uint64_t _seqNum)
        : VectorMicroInst(mnem, _machInst, __opClass, _microVl,
                          _microIdx, _vlen)
    {
      elemIdx = _elemIdx;
      seqNum = _seqNum;
      this->flags[IsEop] = true;
      this->flags[IsLoad] = true;
      field = _field;
      numFields = _numFields;
      numMicroops = _numMicroops;
    }

    std::string generateDisassembly(
        Addr pc, const loader::SymbolTable *symtab) const override;
};

class VlSegDeIntrlvMicroInst : public VectorArithMicroInst
{
  private:
    RegId srcRegIdxArr[NumVecInternalRegs];
    RegId destRegIdxArr[1];
    uint32_t numSrcs;
    uint32_t numMicroops;
    uint32_t field;
    uint32_t sizeOfElement;
    uint32_t micro_vl;

  public:
    uint32_t vlen;

    VlSegDeIntrlvMicroInst(ExtMachInst extMachInst, uint32_t _micro_vl,
                            uint32_t _dstReg, uint32_t _numSrcs,
                            uint32_t _microIdx, uint32_t _numMicroops,
                            uint32_t _field, uint32_t _vlen,
                            uint32_t _sizeOfElement);

    Fault execute(ExecContext *, trace::InstRecord *) const override;

    std::string generateDisassembly(Addr,
        const loader::SymbolTable *)  const override;
};

class VsSegMacroInst : public VectorMemMacroInst
{
  protected:
    VsSegMacroInst(const char* mnem, ExtMachInst _machInst,
                   OpClass __opClass, uint32_t _vlen)
        : VectorMemMacroInst(mnem, _machInst, __opClass, _vlen)
    {}

    std::string generateDisassembly(
            Addr pc, const loader::SymbolTable *symtab) const override;
};

class VsSegMicroInst : public VectorMicroInst
{
  protected:
    Request::Flags memAccessFlags;
    uint8_t regIdx;

    VsSegMicroInst(const char *mnem, ExtMachInst _machInst,
                   OpClass __opClass, uint32_t _microVl,
                   uint32_t _microIdx, uint32_t _numMicroops,
                   uint32_t _field, uint32_t _numFields,
                   uint32_t _vlen)
        : VectorMicroInst(mnem, _machInst, __opClass, _microVl,
                          _microIdx, _vlen)
    {
      this->flags[IsStore] = true;
    }

    std::string generateDisassembly(
        Addr pc, const loader::SymbolTable *symtab) const override;
};

class VsSegIntrlvMicroInst : public VectorArithMicroInst
{
  private:
    RegId srcRegIdxArr[NumVecInternalRegs];
    RegId destRegIdxArr[1];
    uint32_t numSrcs;
    uint32_t numMicroops;
    uint32_t field;
    uint32_t sizeOfElement;
    uint32_t micro_vl;

  public:
    uint32_t vlen;

    VsSegIntrlvMicroInst(ExtMachInst extMachInst, uint32_t _micro_vl,
                            uint32_t _dstReg, uint32_t _numSrcs,
                            uint32_t _microIdx, uint32_t _numMicroops,
                            uint32_t _field, uint32_t _vlen,
                            uint32_t _sizeOfElement);

    Fault execute(ExecContext *, trace::InstRecord *) const override;

    std::string generateDisassembly(Addr,
        const loader::SymbolTable *)  const override;
};

class VCompressPopcMicroInst : public VectorArithMicroInst
{
private:
    RegId srcRegIdxArr[8];  // vm
    RegId destRegIdxArr[1]; // vcnt

  public:
    VCompressPopcMicroInst(ExtMachInst extMachInst, int32_t _microVl)
        : VectorArithMicroInst("VPopCount", extMachInst,
          SimdMiscOp, _microVl, 0)
    {
        setRegIdxArrays(reinterpret_cast<RegIdArrayPtr>(
            &std::remove_pointer_t<decltype(this)>::srcRegIdxArr),
        reinterpret_cast<RegIdArrayPtr>(
            &std::remove_pointer_t<decltype(this)>::destRegIdxArr));
        _numSrcRegs = 0;
        _numDestRegs = 0;
        setDestRegIdx(_numDestRegs++, RegId(vecRegClass, VecCompressCntReg));
        _numTypedDestRegs[VecRegClass]++;
        setSrcRegIdx(_numSrcRegs++, RegId(vecRegClass, extMachInst.vs1));
    }

    Fault execute(ExecContext* xc, trace::InstRecord* traceData) const override
    {
        int64_t vlmul = vtype_vlmul(machInst.vtype8);
        const int countN = vlen / (8 << machInst.vtype8.vsew);
        const int numVRegs = 1 << std::max<int64_t>(0, vlmul);

        vreg_t vs1;
        xc->getRegOperand(this, 0, &vs1);
        int rVl = this->microVl;
        auto &vd = *(vreg_t *)xc->getWritableRegOperand(this, 0);

        int popCnt = 0;
        int cnt[8] = {0};
        for (int i=0; i<numVRegs; i++) {
            cnt[i] = popcount_in_byte(vs1.as<uint64_t>(), i * countN, (i+1) * countN);
            popCnt += cnt[i];
        }
        popCnt = std::min(popCnt, rVl);

        // vd [popCount(vs1 + numVRegs)...] + [num of each Vd should compress]
        for (int i=0; i<numVRegs; i++) {
            vd.as<uint8_t>()[i] = std::min(popCnt, countN);
            popCnt = std::max(0, popCnt - countN);
        }

        for (int i=0; i<numVRegs; i++) {
            vd.as<uint8_t>()[i+8] = std::min(rVl, cnt[i]);
        }

        xc->setRegOperand(this, 0, &vd);
        if (traceData)
            traceData->setData(vecRegClass, &vd);
        return NoFault;
    }

    std::string generateDisassembly(Addr pc, const loader::SymbolTable *symtab)
        const override
    {
        std::stringstream ss;
        ss << mnemonic << ' ' << registerName(destRegIdx(0)) << ", "
        << registerName(srcRegIdx(0)) << ", ";
        return ss.str();
    }

};

template<typename Type>
class VCompressMicroInst : public VectorArithMicroInst
{
  private:
    RegId srcRegIdxArr[8];  // vs, vcnt, vm, old_vd
    RegId destRegIdxArr[1]; // vd
    uint8_t vsIdx;
    uint8_t vdIdx;
  public:
    VCompressMicroInst(ExtMachInst extMachInst, int32_t _microVl,
        uint8_t microIdx, uint8_t _vsIdx, uint8_t _vdIdx)
        : VectorArithMicroInst("Vcompress_micro", extMachInst,
          SimdMiscOp, _microVl, microIdx)
        , vsIdx(_vsIdx), vdIdx(_vdIdx)
    {
        setRegIdxArrays(
            reinterpret_cast<RegIdArrayPtr>(
                &std::remove_pointer_t<decltype(this)>::srcRegIdxArr),
            reinterpret_cast<RegIdArrayPtr>(
                &std::remove_pointer_t<decltype(this)>::destRegIdxArr));

        _numSrcRegs = 0;
        _numDestRegs = 0;
        setDestRegIdx(_numDestRegs++, RegId(vecRegClass, extMachInst.vd + _vdIdx));
        _numTypedDestRegs[VecRegClass]++;
        // vs
        setSrcRegIdx(_numSrcRegs++, RegId(vecRegClass, extMachInst.vs2 + _vsIdx));
        // vcnt
        setSrcRegIdx(_numSrcRegs++, RegId(vecRegClass, VecCompressCntReg));
        // vm
        setSrcRegIdx(_numSrcRegs++, RegId(vecRegClass, extMachInst.vs1));
        // old_vd
        setSrcRegIdx(_numSrcRegs++, RegId(vecRegClass, extMachInst.vd + _vdIdx));
        // rVl
        setSrcRegIdx(_numSrcRegs++, VecRenamedVLReg);
    }

    Fault execute(ExecContext* xc, trace::InstRecord* traceData) const override
    {
        uint32_t VLEN = vlen;
        uint32_t VLENB = vlen / 8;
        int sew = (8 << machInst.vtype8.vsew);
        const int uvlmax = VLEN / sew;
        uint32_t elem_num_per_vreg = VLEN / sew;

        vreg_t vs, vcnt, vm, old_vd;
        uint32_t rVl;
        xc->getRegOperand(this, 0, &vs);
        xc->getRegOperand(this, 1, &vcnt);
        xc->getRegOperand(this, 2, &vm);
        xc->getRegOperand(this, 3, &old_vd);
        rVl = this->microVl;

        auto &vd = *(vreg_t *)xc->getWritableRegOperand(this, 0);
        memcpy(vd.as<uint8_t>(), old_vd.as<uint8_t>(), VLENB);


        uint16_t vd_should_compress = vcnt.as<uint8_t>()[vdIdx];
        uint16_t vs2_popCnt = vcnt.as<uint8_t>()[8 + vsIdx];

        uint32_t vd_has_compressed = 0;
        uint32_t cur_compressed = 0;
        uint32_t lower_num = 0;
        uint32_t upper_num = 0;
        for (int i=0; i<8; i++) {
            if (i < vdIdx) {
                vd_has_compressed += vcnt.as<uint8_t>()[i];
            }
            if (i < vsIdx) {
                lower_num += vcnt.as<uint8_t>()[8+i];
            }
        }
        upper_num = lower_num + vcnt.as<uint8_t>()[8+vsIdx];
        cur_compressed = vd_has_compressed + vcnt.as<uint8_t>()[vdIdx];

        bool satisfaction = (cur_compressed > lower_num) && (cur_compressed - lower_num <= uvlmax);
        if (satisfaction) {
            uint32_t vs2rs = vd_has_compressed > lower_num ? vd_has_compressed - lower_num : 0;
            uint32_t need_compress_num = cur_compressed > upper_num ?
                  cur_compressed - upper_num : cur_compressed - lower_num;
            uint32_t vdrs = lower_num > vd_has_compressed ? lower_num - vd_has_compressed : 0;
            assert(need_compress_num <= uvlmax);
            assert(vdrs < uvlmax);

            uint32_t compressed = 0;
            for (int i=0; i < uvlmax && compressed < need_compress_num; i++) {
                uint32_t ei = vd_has_compressed + vdrs + compressed;
                uint32_t vdElemIdx = vdrs + compressed;
                uint32_t vs2ElemIdx = vs2rs + i;
                if ((ei < rVl) && elem_mask(vm.as<uint8_t>(), ei)) {
                    vd.as<Type>()[vdElemIdx] = vs.as<Type>()[i];
                    compressed++;
                }
            }
        }

        xc->setRegOperand(this, 0, &vd);
        if (traceData)
            traceData->setData(vecRegClass, &vd);
        return NoFault;
    }

    std::string generateDisassembly(Addr pc, const loader::SymbolTable *symtab)
        const override
    {
        std::stringstream ss;
        ss << mnemonic << ' ' << registerName(destRegIdx(0)) << ", "
        << registerName(srcRegIdx(0)) << ", "
        << registerName(srcRegIdx(1)) << ", "
        << registerName(srcRegIdx(2)) << ", "
        << registerName(srcRegIdx(3)) << ", ";
        return ss.str();
    }
};

template<typename Type>
class Vcompress_vm : public VectorArithMacroInst
{
  private:
    RegId srcRegIdxArr[8];  // vs, vm
    RegId destRegIdxArr[1]; // vd
  public:
    Vcompress_vm(ExtMachInst _machInst, uint32_t _vlen)
        : VectorArithMacroInst("vcompress_vm", _machInst, SimdMiscOp, _vlen)
    {
        uint32_t VLEN = vlen;
        int sew = (8 << machInst.vtype8.vsew);
        int8_t vlmul = vtype_vlmul(_machInst.vtype8);
        float vflmul = ( vlmul < 0 ? (1.0 / (1 << (-vlmul))) : (1 << vlmul) );

        const uint32_t num_microops = vflmul < 1 ? 1 : vflmul;
        const int32_t vlmax = VLEN / sew * vflmul;
        int32_t tmp_vl = this->vl;
        const int32_t micro_vlmax = vtype_VLMAX(_machInst.vtype8, vlen, true);
        int32_t micro_vl = std::min(tmp_vl, micro_vlmax);

        StaticInstPtr microop;
        microop = new VCompressPopcMicroInst(_machInst, micro_vl);
        this->microops.push_back(microop);

        int8_t microIdx = 0;
        for (int i = 0; i < num_microops; ++i) {
            for (int j = 0; j <= i; ++j) {
                microop = new VCompressMicroInst<Type>(
                    _machInst, micro_vl, microIdx++, i, j);
                microop->setDelayedCommit();
                this->microops.push_back(microop);
            }
        }
        this->microops.front()->setFirstMicroop();
        this->microops.back()->setLastMicroop();
    }

    std::string generateDisassembly(Addr pc, const loader::SymbolTable *symtab)
        const override
    {
        std::stringstream ss;
        ss << mnemonic << ' ' << registerName(destRegIdx(0)) << ", "
        << registerName(srcRegIdx(1)) << ", "
        << registerName(srcRegIdx(0));
        return ss.str();
    }
};

// For Rxu eop
class VlIndexRxuMacroInst : public VectorMemMacroInst
{
  protected:
    VlIndexRxuMacroInst(const char* mnem, ExtMachInst _machInst,
                   OpClass __opClass, uint32_t _vlen)
        : VectorMemMacroInst(mnem, _machInst, __opClass, _vlen)
    {
      this->flags[IsSplitMacro] = true;
    }

    std::string generateDisassembly(
            Addr pc, const loader::SymbolTable *symtab) const override;
};

class VlIndexRxuMicroInst : public VectorMemMicroInst
{
  protected:
    VlIndexRxuMicroInst(const char *mnem, ExtMachInst _machInst,
                    OpClass __opClass, uint32_t _vdRegIdx = 0, std::deque<uint32_t> _vdElemIdx = {},
                    uint32_t _vs2RegIdx = 0, std::deque<uint32_t> _vs2ElemIdx = {})
        : VectorMemMicroInst(mnem, _machInst, __opClass, 1,
                             0, 0, _vdRegIdx, _vdElemIdx, _vs2RegIdx, _vs2ElemIdx, 0, {})
    {
        this->flags[IsSplitUop] = true;
    }
    std::string generateDisassembly(
        Addr pc, const loader::SymbolTable *symtab) const override;
};

class VlIndexRxuUopMacroInst : public VectorMemMacroInst
{
  protected:
    uint32_t vdRegIdx;
    std::deque<uint32_t> vdElemIdx;
    uint32_t vs2RegIdx;
    std::deque<uint32_t> vs2ElemIdx;
    uint64_t seqNum;
    VlIndexRxuUopMacroInst(const char* mnem, ExtMachInst _machInst,
                   OpClass __opClass, uint32_t _vlen,
                   uint32_t _vdRegIdx, std::deque<uint32_t> _vdElemIdx,
                   uint32_t _vs2RegIdx, std::deque<uint32_t> _vs2ElemIdx,
                   uint64_t _seqNum)
        : VectorMemMacroInst(mnem, _machInst, __opClass, _vlen)
    {
        vdRegIdx = _vdRegIdx;
        vdElemIdx = _vdElemIdx;
        vs2RegIdx = _vs2RegIdx;
        vs2ElemIdx = _vs2ElemIdx;
        seqNum = _seqNum;
    }

    std::string generateDisassembly(
            Addr pc, const loader::SymbolTable *symtab) const override;
};

class VlIndexRxuUopMicroInst : public VectorMemMicroInst
{
  protected:
    uint32_t vdRegIdx;
    uint32_t vdElemIdx;
    uint32_t vs2RegIdx;
    uint32_t vs2ElemIdx;
    uint64_t seqNum;
    VlIndexRxuUopMicroInst(const char *mnem, ExtMachInst _machInst,
                    OpClass __opClass, uint32_t _vdRegIdx, uint32_t _vdElemIdx,
                    uint32_t _vs2RegIdx, uint32_t _vs2ElemIdx, uint64_t _seqNum)
        : VectorMemMicroInst(mnem, _machInst, __opClass, 1,
                             0, 0)
        , vdRegIdx(_vdRegIdx), vdElemIdx(_vdElemIdx)
        , vs2RegIdx(_vs2RegIdx), vs2ElemIdx(_vs2ElemIdx), seqNum(_seqNum)
    {
        this->flags[IsEop] = true;
    }

    std::string generateDisassembly(
        Addr pc, const loader::SymbolTable *symtab) const override;
};

class VsIndexRxuMacroInst : public VectorMemMacroInst
{
  protected:
    VsIndexRxuMacroInst(const char* mnem, ExtMachInst _machInst,
                   OpClass __opClass, uint32_t _vlen)
        : VectorMemMacroInst(mnem, _machInst, __opClass, _vlen)
    {
      this->flags[IsSplitMacro] = true;
    }

    std::string generateDisassembly(
            Addr pc, const loader::SymbolTable *symtab) const override;
};

class VsIndexRxuMicroInst : public VectorMemMicroInst
{
  protected:
    VsIndexRxuMicroInst(const char *mnem, ExtMachInst _machInst,
                    OpClass __opClass, uint32_t _vs3RegIdx,
                    std::deque<uint32_t> _vs3ElemIdx, uint32_t _vs2RegIdx,
                    std::deque<uint32_t> _vs2ElemIdx)
        : VectorMemMicroInst(mnem, _machInst, __opClass, 1, 0, 0, 0, {}, _vs2RegIdx, _vs2ElemIdx, _vs3RegIdx, _vs3ElemIdx)
    {
      this->flags[IsSplitUop] = true;
    }

    std::string generateDisassembly(
        Addr pc, const loader::SymbolTable *symtab) const override;
};

class VsIndexRxuUopMacroInst : public VectorMemMacroInst
{
  protected:
    uint32_t vs3RegIdx;
    std::deque<uint32_t> vs3ElemIdx;
    uint32_t vs2RegIdx;
    std::deque<uint32_t> vs2ElemIdx;
    uint64_t seqNum;
    VsIndexRxuUopMacroInst(const char* mnem, ExtMachInst _machInst,
                   OpClass __opClass, uint32_t _vlen,
                   uint32_t _vs3RegIdx, std::deque<uint32_t> _vs3ElemIdx,
                   uint32_t _vs2RegIdx, std::deque<uint32_t> _vs2ElemIdx,
                   uint64_t _seqNum)
        : VectorMemMacroInst(mnem, _machInst, __opClass, _vlen)
    {
        vs3RegIdx = _vs3RegIdx;
        vs3ElemIdx = _vs3ElemIdx;
        vs2RegIdx = _vs2RegIdx;
        vs2ElemIdx = _vs2ElemIdx;
        seqNum = _seqNum;
    }

    std::string generateDisassembly(
            Addr pc, const loader::SymbolTable *symtab) const override;
};

class VsIndexRxuUopMicroInst : public VectorMemMicroInst
{
  protected:
    uint32_t vs3RegIdx;
    uint32_t vs3ElemIdx;
    uint32_t vs2RegIdx;
    uint32_t vs2ElemIdx;
    uint64_t seqNum;
    VsIndexRxuUopMicroInst(const char *mnem, ExtMachInst _machInst,
                    OpClass __opClass, uint32_t _vs3RegIdx, uint32_t _vs3ElemIdx,
                    uint32_t _vs2RegIdx, uint32_t _vs2ElemIdx, uint64_t _seqNum)
        : VectorMemMicroInst(mnem, _machInst, __opClass, 1,
                             0, 0)
        , vs3RegIdx(_vs3RegIdx), vs3ElemIdx(_vs3ElemIdx)
        , vs2RegIdx(_vs2RegIdx), vs2ElemIdx(_vs2ElemIdx), seqNum(_seqNum)
    {
      this->flags[IsEop] = true;
    }

    std::string generateDisassembly(
        Addr pc, const loader::SymbolTable *symtab) const override;
};

} // namespace RiscvISA
} // namespace gem5


#endif // __ARCH_RISCV_INSTS_VECTOR_HH__
