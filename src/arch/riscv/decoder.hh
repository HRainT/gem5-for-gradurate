/*
 * Copyright (c) 2012 Google
 * Copyright (c) 2017 The University of Virginia
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

#ifndef __ARCH_RISCV_DECODER_HH__
#define __ARCH_RISCV_DECODER_HH__

#include "arch/generic/decode_cache.hh"
#include "arch/generic/decoder.hh"
#include "arch/riscv/insts/vector.hh"
#include "arch/riscv/types.hh"
#include "base/logging.hh"
#include "base/types.hh"
#include "cpu/static_inst.hh"
#include "debug/Decode.hh"
#include "params/RiscvDecoder.hh"

namespace gem5
{

class BaseISA;

namespace RiscvISA
{

class Decoder : public InstDecoder
{
  private:
    decode_cache::InstMap<ExtMachInst> instMap;
    bool aligned;
    bool mid;

  protected:
    //The extended machine instruction being generated
    ExtMachInst emi;
    uint32_t machInst;

    uint32_t vlen;
    uint32_t elen;

    uint32_t vs3RegIdx = 0;
    std::deque<uint32_t> vs3ElemIdx = {};
    uint32_t vdRegIdx = 0;
    std::deque<uint32_t> vdElemIdx = {};
    uint32_t vs2RegIdx = 0;
    std::deque<uint32_t> vs2ElemIdx = {};
    std::deque<uint32_t> microIdx_q = {};
    uint32_t regIdx = 0;
    uint64_t seqNum = 0;
    uint32_t microVl = 0;
    uint32_t field = 0;
    uint32_t microIdx = 0;
    std::deque<uint32_t> elemIdx_q = {};
    uint32_t micro_vl = 0;
    uint32_t numFields = 0;
    uint32_t numMicroops = 0;
    std::deque<uint32_t> seg_regidx_q = {};
    std::deque<uint32_t> seg_elemidx_q = {};
    virtual StaticInstPtr decodeInst(ExtMachInst mach_inst);

    /// Decode a machine instruction.
    /// @param mach_inst The binary instruction to decode.
    /// @retval A pointer to the corresponding StaticInst object.
    StaticInstPtr decode(ExtMachInst mach_inst, Addr addr, bool isVector = false);

  public:

    Decoder(const RiscvDecoderParams &p);

    void reset() override;

    void setEmi(ExtMachInst new_emi) { emi = new_emi; }

    inline bool compressed(ExtMachInst inst) { return inst.quadRant < 0x3; }

    //Use this to give data to the decoder. This should be used
    //when there is control flow.
    void moreBytes(const PCStateBase &pc, Addr fetchPC) override;

    StaticInstPtr decode(PCStateBase &nextPC) override;

    StaticInstPtr decodeVector(ExtMachInst mach_inst, Addr addr);

    void setVdRegIdx(uint32_t idx) { vdRegIdx = idx; }
    void setVdElemIdx(std::deque<uint32_t> idx) { vdElemIdx = idx; }
    void setVs2RegIdx(uint32_t idx) { vs2RegIdx = idx; }
    void setVs2ElemIdx(std::deque<uint32_t> idx) { vs2ElemIdx = idx; }
    void setSeqNum(uint64_t sn) { seqNum = sn; }
    void setVs3RegIdx(uint32_t idx) { vs3RegIdx = idx; }
    void setVs3ElemIdx(std::deque<uint32_t> idx) { vs3ElemIdx = idx; }
    void setMicroIdx(std::deque<uint32_t> idx) { microIdx_q = idx; }
    void setRegIdx(uint32_t idx) { regIdx = idx; }
    void setMicroVl(uint32_t vl) { microVl = vl; }
    void setMicroIdx_uop(uint32_t idx) { microIdx = idx; }
    void setElemIdx(std::deque<uint32_t> idx) { elemIdx_q = idx; }
    void setField(uint32_t f) { field = f; }
    void setNumFields(uint32_t _numFields) { numFields = _numFields; }
    void setNumMicroops(uint32_t _numMicroops) { numMicroops = _numMicroops; }
    void setSegRegIdx(std::deque<uint32_t> idx) { seg_regidx_q = idx; }
    void setSegElemIdx(std::deque<uint32_t> idx) { seg_elemidx_q = idx; }

};

} // namespace RiscvISA
} // namespace gem5

#endif // __ARCH_RISCV_DECODER_HH__
