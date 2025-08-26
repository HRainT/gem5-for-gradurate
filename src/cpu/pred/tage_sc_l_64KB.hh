/*
 * Copyright (c) 2018 Metempsy Technology Consulting
 * All rights reserved.
 *
 * Copyright (c) 2006 INRIA (Institut National de Recherche en
 * Informatique et en Automatique  / French National Research Institute
 * for Computer Science and Applied Mathematics)
 *
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
 *
 * Author: André Seznec, Pau Cabre, Javier Bueno
 *
 */

/*
 * 64KB TAGE-SC-L branch predictor (devised by Andre Seznec)
 *
 * Most of the code in this file has been adapted from cbp64KB/predictor.h in
 * http://www.jilp.org/cbp2016/code/AndreSeznecLimited.tar.gz
 */

#ifndef __CPU_PRED_TAGE_SC_L_64KB_HH__
#define __CPU_PRED_TAGE_SC_L_64KB_HH__

#include "cpu/pred/tage_sc_l.hh"
#include "params/TAGE_SC_L_64KB.hh"
#include "params/TAGE_SC_L_64KB_StatisticalCorrector.hh"
#include "params/TAGE_SC_L_TAGE_64KB.hh"

namespace gem5
{

namespace branch_prediction
{

class TAGE_SC_L_TAGE_64KB : public TAGE_SC_L_TAGE
{
    public:
    TAGE_SC_L_TAGE_64KB(const TAGE_SC_L_TAGE_64KBParams &p) : TAGE_SC_L_TAGE(p)
    {}

    int gindex_ext(int index, int bank) const override;

    uint16_t gtag(ThreadID tid, Addr pc, int bank) const override;

    void handleAllocAndUReset(
        bool alloc, bool taken, TAGEBase::BranchInfo* bi, int nrand) override;

    void handleTAGEUpdate(
        Addr branch_pc, bool taken, TAGEBase::BranchInfo* bi) override;
};

class TAGE_SC_L_64KB_StatisticalCorrector : public StatisticalCorrector
{
    const unsigned numEntriesSecondLocalHistories;
    const unsigned numEntriesThirdLocalHistories;

    // global branch history variation GEHL
    const unsigned pnb;
    const unsigned logPnb;
    std::vector<int> pm;
    std::vector<int8_t> * pgehl;
    std::vector<int8_t> wp;

    // Second local history GEHL
    const unsigned snb;
    const unsigned logSnb;
    std::vector<int> sm;
    std::vector<int8_t> * sgehl;
    std::vector<int8_t> ws;

    // Third local history GEHL
    const unsigned tnb;
    const unsigned logTnb;
    std::vector<int> tm;
    std::vector<int8_t> * tgehl;
    std::vector<int8_t> wt;

    // Second IMLI GEHL
    const unsigned imnb;
    const unsigned logImnb;
    std::vector<int> imm;
    std::vector<int8_t> * imgehl;
    std::vector<int8_t> wim;

    struct SC_64KB_ThreadHistory : public SCThreadHistory
    {
        std::vector<int64_t> imHist;
    };

    SCThreadHistory *makeThreadHistory() override;

  public:
    TAGE_SC_L_64KB_StatisticalCorrector(
        const TAGE_SC_L_64KB_StatisticalCorrectorParams &p);

    unsigned getIndBiasBank(Addr branch_pc, BranchInfo* bi, int hitBank,
        int altBank) const override;

    int gPredictions(ThreadID tid, Addr branch_pc, BranchInfo* bi,
                     int & lsum, int64_t phist) override;
    int gPredictions(ThreadID tid, Addr branch_pc, BranchInfo* bi,
                     int & lsum, int64_t phist,const StaticInstPtr &inst) override;
    int gIndexLogsSubstr(int nbr, int i) override;

    void scHistoryUpdate(Addr branch_pc, const StaticInstPtr &inst, bool taken,
                         BranchInfo * tage_bi, Addr corrTarget) override;

    void gUpdates(ThreadID tid, Addr pc, bool taken, BranchInfo* bi,
            int64_t phist) override;
    int sRPredict(ThreadID tid, Addr pc, BranchInfo* bi, const StaticInstPtr & inst);
    void rUpdates( ThreadID tid, Addr pc, bool taken, BranchInfo* bi, int64_t phist, std::vector<int8_t> & w);
    // static inline uint64_t mixBankSalt(int bank) {
    //     // Knuth/黄金分割常数的 64 位版本，作为 bank 盐值
    //     return 0x9E3779B97F4A7C15ULL * (uint64_t)(bank + 1);
    // }
    uint32_t ut_gindex(Addr pc, int bank, int logUt)
    {
        uint64_t x = (uint64_t)pc;
        // 轻度 skew，和 getIndUpds 风格相近
        x ^= (x >> 2) ^ (x >> 5) ^ (x >> 13);
        x ^= mixBankSalt(bank);
        return (uint32_t)(x & ((1u << logUt) - 1)); // logUt=8 → 256 项
    }
    struct UTEntry
    {
        uint16_t u[4];
        UTEntry() :u{0,0,0,0} { }
    };
    struct WTEntry
    {
    int8_t weight = 0;
    WTEntry() : weight(0) { }
    };
    UTEntry Utable[8][256];

    static constexpr int WT_layers = 3; // 举例：3 层 GEHL
    // 每层大小（按层 i）
    static constexpr std::array<std::size_t, WT_layers> WT_SIZE = { 1024, 512, 256 };

    using Ctr = int8_t;

    // [bank][layer][idx]
    std::array<std::array<std::vector<Ctr>, WT_layers>, 8> Wtable;

    void init_tables()
    {
        for (int b = 0; b < 8; ++b) {
            for (int i = 0; i < WT_layers; ++i) {
                Wtable[b][i].assign(WT_SIZE[i], 0); // 初始化为 0
            }
        }
    }

    
    static inline uint64_t rol64(uint64_t x, unsigned r) {
        r &= 63;
        return (x << r) | (x >> ((64 - r) & 63));
    }
    static inline uint64_t mixBankSalt(int bank) {
        return 0x9E3779B97F4A7C15ULL * (uint64_t)(bank + 1);
    }
    uint32_t wt_gindex(Addr pc,
                            uint16_t digest12,
                            int bank,
                            int logs,    // log2(size)
                            int nbr,     // 层数
                            int i)
    {
        uint64_t x = (uint64_t)pc;
        uint64_t d = (uint64_t)(digest12 & 0xFFFu);

        // 层相关的折叠与打散（模仿 gIndex 的“逐层变化”）
        x ^= rol64(d, 3 + i) ^ (d * 0x9E37u);
        x ^= (x >> (8 + i)) ^ (x >> (16 + 2*i));
        x ^= mixBankSalt(bank);
        x ^= (x >> 17) ^ (x >> 31);

        int cut = logs - gIndexLogsSubstr(nbr, i);
        return (uint32_t)(x & ((1u << cut) - 1));
    }
};

class TAGE_SC_L_64KB : public TAGE_SC_L
{
  public:
    TAGE_SC_L_64KB(const TAGE_SC_L_64KBParams &params);
};

} // namespace branch_prediction
} // namespace gem5

#endif // __CPU_PRED_TAGE_SC_L_64KB_HH__
