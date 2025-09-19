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
////////////////////////////////////////////////////////////////////////////////////////////
static inline uint64_t mix64(uint64_t x) {
    x += 0x9e3779b97f4a7c15ull;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    x ^= x >> 31;
    return x;
}
static inline uint64_t rotl64(uint64_t x, int r) {
    return (x << r) | (x >> (64 - r));
}
// 常量
int NREG = 32;
int CTX  = 64;     // 每寄存器 64 个上下文槽
// 三个不同盐（常量）
uint64_t UT_SALT[3] = {
    0x243F6A8885A308D3ull, 0x13198A2E03707344ull, 0xA4093822299F31D0ull
};
// 计算单个 UT 索引（t ∈ {0,1,2}）
uint32_t ut_index1(int t, uint64_t pc, int reg_id) {
    // ctx 只由 PC + 盐 生成（不混入 reg_id，便于每寄存器分块）
    uint64_t h = mix64(pc ^ UT_SALT[t]);
    uint32_t ctx = h & (CTX - 1); // 0..63
    return uint32_t(reg_id) * CTX + ctx; // 0..(NREG*CTX-1)
}
// 用法示例：Utable[t][ ut_index(t, pc, reg_id) ]

// 常量
 static const int Scale = 3;
 static const int NBANK = 8;
 static const int WT1_PER_BANK = 8192;
 static const int WT2_PER_BANK = 4096;
 static const int WT3_PER_BANK = 2048;

// 每张表的全局盐
 uint64_t WT_SALT1 = 0x9E3779B97F4A7C15ull;
 uint64_t WT_SALT2 = 0xBF58476D1CE4E5B9ull;
 uint64_t WT_SALT3 = 0x94D049BB133111EBull;

// 可选：每个 bank 再给一个不同盐（常量表）
uint64_t BANK_SALT1[NBANK] = {
    0xC2B2AE3D27D4EB4Full,0x165667B19E3779F9ull,0x85EBCA77C2B2AE63ull,0x27D4EB2F165667C5ull,
    0x9E3779B185EBCA87ull,0xC2B2AE3D27D4EB4Bull,0x165667B19E3779F1ull,0x85EBCA77C2B2AE61ull
};
uint64_t BANK_SALT2[NBANK] = {
    0xA24BAED4963EE407ull,0x9FB21C651E98DF25ull,0xC13FA9A902A6328Full,0x91E10DA5C79E7B1Dull,
    0xD1B54A32D192ED03ull,0xCA5A826395121157ull,0x7F4A7C15F39CC060ull,0x24D9E8A1BBD01AF3ull
};
uint64_t BANK_SALT3[NBANK] = {
    0xD6E8FEB86659FD93ull,0xA24BAED4963EE407ull,0x9FB21C651E98DF25ull,0xC13FA9A902A6328Full,
    0x91E10DA5C79E7B1Dull,0xD1B54A32D192ED03ull,0xCA5A826395121157ull,0x7F4A7C15F39CC060ull
};

// 组合 key（仅 PC/reg_id/digest）
uint64_t base_key(uint64_t pc, int reg_id, uint16_t digest12) {
    // 组合后混洗，避免线性低位碰撞
    uint64_t x = (pc << 17) ^ (pc >> 7) ^ (uint64_t(reg_id) << 9) ^ uint64_t(digest12);
    return mix64(x);
}

// 返回 bank 与各表内的行号
int wt_bank(int reg_id) {
    return reg_id & (NBANK - 1);
}
uint32_t wt_index1(uint64_t pc, int reg_id, uint16_t digest12) {
    int bank = wt_bank(reg_id);
    uint64_t k = base_key(pc, reg_id, digest12);
    uint64_t h = mix64(k ^ WT_SALT1 ^ BANK_SALT1[bank]);
    return h & (WT1_PER_BANK - 1); // 0..4095
}
uint32_t wt_index2(uint64_t pc, int reg_id, uint16_t digest12) {
    int bank = wt_bank(reg_id);
    uint64_t k = base_key(pc, reg_id, digest12);
    // 与 idx1 去相关：旋转与不同盐
    uint64_t h = mix64(rotl64(k, 13) ^ WT_SALT2 ^ BANK_SALT2[bank]);
    return h & (WT2_PER_BANK - 1); // 0..2047
}
uint32_t wt_index3(uint64_t pc, int reg_id, uint16_t digest12) {
    int bank = wt_bank(reg_id);
    uint64_t k = base_key(pc, reg_id, digest12);
    uint64_t h = mix64((k ^ (k >> 23)) ^ WT_SALT3 ^ BANK_SALT3[bank]);
    return h & (WT3_PER_BANK - 1); // 0..1023
}
// 访问示例：Wtable1[bank][ wt_index1(pc, reg_id, digest12) ] 等

//////////////////////////////////////////////////////////////////////////////////////////////

    class RunLts{
        public:
            struct UTEntry
            {
                int16_t u;
                UTEntry() :u{-8} { }
            };
            struct WTEntry
            {
                int8_t weight = 0;
                WTEntry() : weight(0) { }
            };
            inline static UTEntry Utable[3][2048] = {};
            inline static WTEntry Wtable0[8][8192] = {};
            inline static WTEntry Wtable1[8][4096] = {};
            inline static WTEntry Wtable2[8][2048] = {};
            static void WtableUpdate(uint32_t i1, uint32_t i2, uint32_t i3, int i, bool taken){
                if(RunLts::Wtable0[i][i1].weight < 31 && taken)
                    RunLts::Wtable0[i][i1].weight++;
                if(RunLts::Wtable1[i][i2].weight < 31 && taken)
                    RunLts::Wtable1[i][i2].weight++;
                if(RunLts::Wtable2[i][i3].weight < 31 && taken)
                    RunLts::Wtable2[i][i3].weight++;                    
                if(RunLts::Wtable0[i][i1].weight > -32 && !taken)
                    RunLts::Wtable0[i][i1].weight--;
                if(RunLts::Wtable1[i][i2].weight > -32 && !taken)
                    RunLts::Wtable1[i][i2].weight--;
                if(RunLts::Wtable2[i][i3].weight > -32 && !taken)
                    RunLts::Wtable2[i][i3].weight--;    
            }
    };
    struct UTEntry
    {
        int16_t u[4];
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
