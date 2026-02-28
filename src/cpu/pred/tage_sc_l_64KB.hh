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
                     int & lsum, int64_t phist, const std::map<RegIndex, uint64_t> &RegSnMap,
                     const std::vector<bool> &regtable, const std::map<RegIndex, uint16_t> &digestMap) override;
    int gIndexLogsSubstr(int nbr, int i) override;

    void scHistoryUpdate(Addr branch_pc, const StaticInstPtr &inst, bool taken,
                         BranchInfo * tage_bi, Addr corrTarget) override;

    void gUpdates(ThreadID tid, Addr pc, bool taken, BranchInfo* bi,
            int64_t phist) override;
    int sRPredict(ThreadID tid, Addr pc, BranchInfo* bi, 
                const std::map<RegIndex, uint64_t> &RegSnMap, const std::vector<bool> &regtable, const std::map<RegIndex, uint16_t> &digestMap);
    void rUpdates(ThreadID tid, Addr pc, bool taken, BranchInfo* bi, int64_t phist, std::vector<int8_t> & w) override;
    uint32_t ut_index1(int t, uint64_t pc, int reg_id) {
        return reg_id * 16 + ((pc ^ (pc >> (2 * t))) % 16);
    }

    class RunLts{
        public:
            struct UTEntry
            {
                int16_t u;
                UTEntry() :u{-4} { }
            };
            struct WTEntry
            {
                int8_t weight = 0;
                WTEntry() : weight(0) { }
            };
            inline static UTEntry Utable[4][496] = {};
            inline static WTEntry Wtable0[4][1024] = {};
            inline static WTEntry Wtable1[4][512] = {};
            static void WtableUpdate(uint32_t i1, uint32_t i2 , int i, bool taken){
                if(RunLts::Wtable0[i][i1].weight < 31 && taken)
                    RunLts::Wtable0[i][i1].weight++;
                if(RunLts::Wtable1[i][i2].weight < 31 && taken)
                    RunLts::Wtable1[i][i2].weight++;               
                if(RunLts::Wtable0[i][i1].weight > -32 && !taken)
                    RunLts::Wtable0[i][i1].weight--;
                if(RunLts::Wtable1[i][i2].weight > -32 && !taken)
                    RunLts::Wtable1[i][i2].weight--;
            }
    };

};

class TAGE_SC_L_64KB : public TAGE_SC_L
{
  public:
    TAGE_SC_L_64KB(const TAGE_SC_L_64KBParams &params);
};

} // namespace branch_prediction
} // namespace gem5

#endif // __CPU_PRED_TAGE_SC_L_64KB_HH__
