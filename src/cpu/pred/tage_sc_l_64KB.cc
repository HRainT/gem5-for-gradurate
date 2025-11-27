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
 */

#include "cpu/pred/tage_sc_l_64KB.hh"
#include "debug/NewTage.hh"
#include <random>  
#include <iostream>
namespace gem5
{

namespace branch_prediction
{

TAGE_SC_L_64KB_StatisticalCorrector::TAGE_SC_L_64KB_StatisticalCorrector(
    const TAGE_SC_L_64KB_StatisticalCorrectorParams &p)
  : StatisticalCorrector(p),
    numEntriesSecondLocalHistories(p.numEntriesSecondLocalHistories),
    numEntriesThirdLocalHistories(p.numEntriesThirdLocalHistories),
    pnb(p.pnb),
    logPnb(p.logPnb),
    pm(p.pm),
    snb(p.snb),
    logSnb(p.logSnb),
    sm(p.sm),
    tnb(p.tnb),
    logTnb(p.logTnb),
    tm(p.tm),
    imnb(p.imnb),
    logImnb(p.logImnb),
    imm(p.imm)
{
    initGEHLTable(pnb, pm, pgehl, logPnb, wp, 7);
    initGEHLTable(snb, sm, sgehl, logSnb, ws, 7);
    initGEHLTable(tnb, tm, tgehl, logTnb, wt, 7);
    initGEHLTable(imnb, imm, imgehl, logImnb, wim, 0);
    init_tables();
}

TAGE_SC_L_64KB_StatisticalCorrector::SCThreadHistory*
TAGE_SC_L_64KB_StatisticalCorrector::makeThreadHistory()
{
    SC_64KB_ThreadHistory *sh = new SC_64KB_ThreadHistory();

    sh->setNumOrdinalHistories(3);
    sh->initLocalHistory(1, numEntriesFirstLocalHistories, 2);
    sh->initLocalHistory(2, numEntriesSecondLocalHistories, 5);
    sh->initLocalHistory(3, numEntriesThirdLocalHistories, logTnb);

    sh->imHist.resize(1 << im[0]);
    return sh;
}

unsigned
TAGE_SC_L_64KB_StatisticalCorrector::getIndBiasBank(Addr branch_pc,
        BranchInfo* bi, int hitBank, int altBank) const
{
    return (bi->predBeforeSC + (((hitBank+1)/4)<<4) + (bi->highConf<<1) +
            (bi->lowConf <<2) + ((altBank!=0)<<3) +
            ((branch_pc^(branch_pc>>2))<<7)) & ((1<<logBias) -1);
}

// int
// TAGE_SC_L_64KB_StatisticalCorrector::sRPredict(ThreadID tid, Addr pc, BranchInfo* bi, const StaticInstPtr & inst){
//     uint32_t ut_index[8];
//     RegIndex regid[8];
//     uint16_t Digest[8];
//     int16_t u[8] = {0,0,0,0,0,0,0,0};
//     int cnt[8] = {4,4,4,4,4,4,4,4};
//     int8_t weight[8] = {0,0,0,0,0,0,0,0};
//     uint64_t key[8] = {0,0,0,0,0,0,0,0};
//     uint32_t wt_index[8][3];
//     int8_t result = 0;
//     int8_t hit_ctr = 0;
//     int8_t alt_ctr = 0;
//     bool use_reg_pred = true;
//     if(use_reg_pred){
//         for(int i = 0; i < 8; i++) {
//             ut_index[i] = ut_gindex(pc, i, 8);
//             bi->ut_index[i] = ut_index[i];
//             for(int j = 0; j < 4; j++) {
//                 if(inst->regtable[i*4 + j] != 0) {
//                     bi->ut_valid[i][j] = true;
//                     if(u[i] <= Utable[i][ut_index[i]].u[j]) {
//                         u[i] = Utable[i][ut_index[i]].u[j];
//                         regid[i] = i*4 + j;
//                         Digest[i] = inst->digestMap[i*4 + j];
//                         bi->ut_j[i] = j;
//                     }
//                     else{
//                         cnt[i]--;
//                     }
//                 }
//                 else{
//                     cnt[i]--;
//                 }
//             }
//             if(cnt[i]){
//                 bi->ut_bank_vld[i] = true;
//                 int prebank = 0;
//                 for(int j = 0; j < 3; ++j){
//                     wt_index[i][j] = wt_gindex(pc, Digest[i], i, std::log2(WT_SIZE[j]), 3, j);
//                     int8_t ctr = Wtable[i][j][wt_index[i][j]];
//                     prebank += (2*ctr + 1);
//                     bi->wt_index[i][j] = wt_index[i][j];
//                     bi->wt_ctr[i][j] = ctr;
//                 }
//                 bi->per_bank[i] = prebank;
//                 result += prebank;
//             }
//             else{
//                 bi->ut_bank_vld[i] = false;
//             }
//         }
//     }
//     else{
//         result = 0;
//     }
//     result = result >> 2;
//     bi->pre_result = result;
//     result = (1 + (wr[getIndUpds(pc)] >= 0)) * result;
//     bi->weight = (wr[getIndUpds(pc)] >= 0) ? 2 : 1;
//     bi->result = result;
//     return result;
// }

int
TAGE_SC_L_64KB_StatisticalCorrector::sRPredict(ThreadID tid, Addr pc, BranchInfo* bi, const StaticInstPtr & inst){
    uint32_t ut_index[8];
    RegIndex regid[8];
    uint16_t Digest[8];
    int16_t u_reg[32] = {0};
    int16_t u[8] = {0,0,0,0,0,0,0,0};
    int cnt[8] = {4,4,4,4,4,4,4,4};
    int8_t weight[8] = {0,0,0,0,0,0,0,0};
    uint64_t key[8] = {0,0,0,0,0,0,0,0};
    uint32_t wt_index[8][3];
    int8_t result = 0;
    int8_t hit_ctr = 0;
    int8_t alt_ctr = 0;
    int8_t per_bank[8] = {0,0,0,0,0,0,0,0};
    bool use_reg_pred = true;
    if(use_reg_pred){
        for(int i = 0; i < 8; i++) {
            for(int j = 0; j < 4; j++) {
                if(inst->regtable[i*4 + j] != 0) {
                    bi->ut_valid[i][j] = true;
                    int reg_id = i*4 + j; 
                    // size_t idx1 = reg_id * 8 + ((pc ^ (pc >> 2)) % 8);
                    // size_t idx2 = reg_id * 8 + ((pc ^ (pc >> 4)) % 8);
                    // size_t idx3 = reg_id * 8 + ((pc ^ (pc >> 6)) % 8);
                    uint32_t idx1 = ut_index1(1, pc, reg_id);
                    uint32_t idx2 = ut_index1(2, pc, reg_id);
                    uint32_t idx3 = ut_index1(3, pc, reg_id);
                    u_reg[reg_id] = RunLts::Utable[0][idx1].u + RunLts::Utable[1][idx2].u + RunLts::Utable[2][idx3].u;
                    bi->digest[reg_id] = inst->digestMap[reg_id];
                    if(u_reg[reg_id] > u[i]) {
                        u[i] = u_reg[reg_id];
                        Digest[i] = inst->digestMap[reg_id];
                        bi->ut_j[i] = j;
                        bi->ut_index1[i][0] = idx1;
                        bi->ut_index1[i][1] = idx2;
                        bi->ut_index1[i][2] = idx3;
                    }
                    else{
                        cnt[i]--;
                    }
                }
                else{
                    cnt[i]--;
                }
            }
            if(cnt[i]){
                bi->ut_bank_vld[i] = true;
                // uint16_t PR = (pc ^ (pc >> 8) ^ Digest[i]) % 4096;
                uint8_t reg_id = i*4 + bi->ut_j[i];
                // uint16_t i1 = (PR + reg_id*633) & (512-1);
                // uint16_t i2 = (((PR >> 2) ^ (PR << 6)) + reg_id) & (256-1);
                // uint16_t i3 = (PR ^ (reg_id << 3)) & (128-1);
                uint32_t i1 = wt_index1(pc, reg_id, Digest[i]);
                uint32_t i2 = wt_index2(pc, reg_id, Digest[i]);
                uint32_t i3 = wt_index3(pc, reg_id, Digest[i]);
                bi->wt_index1[i][0] = i1;
                bi->wt_index1[i][1] = i2;
                bi->wt_index1[i][2] = i3;
                per_bank[i] = RunLts::Wtable0[i][i1].weight + RunLts::Wtable1[i][i2].weight + RunLts::Wtable2[i][i3].weight;
                bi->per_bank[i] = per_bank[i];
                result += per_bank[i];
            }
            else{
                bi->ut_bank_vld[i] = false;
            }
        }
    }
    else{
        result = 0;
    }
    // result = result >> 2;
    bi->pre_result = result;
    result = Scale * result;
    // bi->weight = (wr[getIndUpds(pc)] >= 0) ? 2 : 1;
    bi->result = result;
    return result;
}

int
TAGE_SC_L_64KB_StatisticalCorrector::gPredictions(ThreadID tid, Addr branch_pc,
        BranchInfo* bi, int & lsum, int64_t pathHist, const StaticInstPtr &inst)
{
    SC_64KB_ThreadHistory *sh =
        static_cast<SC_64KB_ThreadHistory *>(scHistory);
    DPRINTF(NewTage, "pc %lx Begin; tage pred:%d, lsum:%d\n", branch_pc, bi->predBeforeSC, lsum);
    int trans;

    trans = gPredict(
        (branch_pc << 1) + bi->predBeforeSC, sh->bwHist, bwm,
        bwgehl, bwnb, logBwnb, wbw);
    DPRINTF(NewTage, "pc %lx, bwm:%d\n", branch_pc, trans);
    lsum += trans;

    trans = gPredict(
        branch_pc, pathHist, pm, pgehl, pnb, logPnb, wp);
    DPRINTF(NewTage, "pc %lx, wp:%d\n", branch_pc, trans);
    lsum += trans;

    trans = gPredict(
        branch_pc, sh->getLocalHistory(1, branch_pc), lm,
        lgehl, lnb, logLnb, wl);
    DPRINTF(NewTage, "pc %lx, lm:%d\n", branch_pc, trans);
    lsum += trans;

    trans = gPredict(
        branch_pc, sh->getLocalHistory(2, branch_pc), sm,
        sgehl, snb, logSnb, ws);
    DPRINTF(NewTage, "pc %lx, sm:%d\n", branch_pc, trans);
    lsum += trans;

    trans = gPredict(
        branch_pc, sh->getLocalHistory(3, branch_pc), tm,
        tgehl, tnb, logTnb, wt);
    DPRINTF(NewTage, "pc %lx, tm:%d\n", branch_pc, trans);
    lsum += trans;

    trans = gPredict(
        branch_pc, sh->imHist[scHistory->imliCount], imm,
        imgehl, imnb, logImnb, wim);
    DPRINTF(NewTage, "pc %lx, imm:%d\n", branch_pc, trans);
    lsum += trans;

    trans = gPredict(
        branch_pc, sh->imliCount, im, igehl, inb, logInb, wi);
    DPRINTF(NewTage, "pc %lx, wi:%d\n", branch_pc, trans);
    lsum += trans;

    trans = sRPredict(tid, branch_pc, bi, inst);
    DPRINTF(NewTage, "pc %lx, SR:%d\n", branch_pc, trans);
    lsum += trans;

    int thres = (updateThreshold>>3) + pUpdateThreshold[getIndUpd(branch_pc)]
      + 12*((wb[getIndUpds(branch_pc)] >= 0) + (wp[getIndUpds(branch_pc)] >= 0)
      + (ws[getIndUpds(branch_pc)] >= 0) + (wt[getIndUpds(branch_pc)] >= 0)
      + (wl[getIndUpds(branch_pc)] >= 0) + (wbw[getIndUpds(branch_pc)] >= 0)
      + (wi[getIndUpds(branch_pc)] >= 0)) + Scale * 6;
    DPRINTF(NewTage, "pc %lx End; tage pred:%d, lsum:%d, thres:%d\n", branch_pc, bi->predBeforeSC, lsum, thres);
    return thres;
}

int
TAGE_SC_L_64KB_StatisticalCorrector::gPredictions(ThreadID tid, Addr branch_pc,
        BranchInfo* bi, int & lsum, int64_t pathHist)
{
    SC_64KB_ThreadHistory *sh =
        static_cast<SC_64KB_ThreadHistory *>(scHistory);

    lsum += gPredict(
        (branch_pc << 1) + bi->predBeforeSC, sh->bwHist, bwm,
        bwgehl, bwnb, logBwnb, wbw);

    lsum += gPredict(
        branch_pc, pathHist, pm, pgehl, pnb, logPnb, wp);

    lsum += gPredict(
        branch_pc, sh->getLocalHistory(1, branch_pc), lm,
        lgehl, lnb, logLnb, wl);

    lsum += gPredict(
        branch_pc, sh->getLocalHistory(2, branch_pc), sm,
        sgehl, snb, logSnb, ws);

    lsum += gPredict(
        branch_pc, sh->getLocalHistory(3, branch_pc), tm,
        tgehl, tnb, logTnb, wt);

    lsum += gPredict(
        branch_pc, sh->imHist[scHistory->imliCount], imm,
        imgehl, imnb, logImnb, wim);

    lsum += gPredict(
        branch_pc, sh->imliCount, im, igehl, inb, logInb, wi);

    int thres = (updateThreshold>>3) + pUpdateThreshold[getIndUpd(branch_pc)]
      + 12*((wb[getIndUpds(branch_pc)] >= 0) + (wp[getIndUpds(branch_pc)] >= 0)
      + (ws[getIndUpds(branch_pc)] >= 0) + (wt[getIndUpds(branch_pc)] >= 0)
      + (wl[getIndUpds(branch_pc)] >= 0) + (wbw[getIndUpds(branch_pc)] >= 0)
      + (wi[getIndUpds(branch_pc)] >= 0));

    return thres;
}

int
TAGE_SC_L_64KB_StatisticalCorrector::gIndexLogsSubstr(int nbr, int i)
{
    return (i >= (nbr - 2)) ? 1 : 0;
}

void
TAGE_SC_L_64KB_StatisticalCorrector::scHistoryUpdate(Addr branch_pc,
        const StaticInstPtr &inst, bool taken, BranchInfo* tage_bi,
        Addr corrTarget)
{
    int brtype = inst->isDirectCtrl() ? 0 : 2;
    if (! inst->isUncondCtrl()) {
        ++brtype;
    }
    // Non speculative SC histories update
    if (brtype & 1) {
        SC_64KB_ThreadHistory *sh =
            static_cast<SC_64KB_ThreadHistory *>(scHistory);
        int64_t imliCount = sh->imliCount;
        sh->imHist[imliCount] = (sh->imHist[imliCount] << 1)
                                + taken;
        sh->updateLocalHistory(2, branch_pc, taken, branch_pc & 15);
        sh->updateLocalHistory(3, branch_pc, taken);
    }

    StatisticalCorrector::scHistoryUpdate(branch_pc, inst, taken, tage_bi,
                                          corrTarget);
}

void
TAGE_SC_L_64KB_StatisticalCorrector::gUpdates(ThreadID tid, Addr pc,
        bool taken, BranchInfo* bi, int64_t phist)
{
    SC_64KB_ThreadHistory *sh =
        static_cast<SC_64KB_ThreadHistory *>(scHistory);

    gUpdate((pc << 1) + bi->predBeforeSC, taken, sh->bwHist, bwm,
            bwgehl, bwnb, logBwnb, wbw, bi);

    gUpdate(pc, taken, phist, pm,
            pgehl, pnb, logPnb, wp, bi);

    gUpdate(pc, taken, sh->getLocalHistory(1, pc), lm,
            lgehl, lnb, logLnb, wl, bi);

    gUpdate(pc, taken, sh->getLocalHistory(2, pc), sm,
            sgehl, snb, logSnb, ws, bi);

    gUpdate(pc, taken, sh->getLocalHistory(3, pc), tm,
            tgehl, tnb, logTnb, wt, bi);

    gUpdate(pc, taken, sh->imHist[scHistory->imliCount], imm,
            imgehl, imnb, logImnb, wim, bi);

    gUpdate(pc, taken, sh->imliCount, im,
            igehl, inb, logInb, wi, bi);
    rUpdates(tid, pc, taken, bi, phist, wr);
}

// void
// TAGE_SC_L_64KB_StatisticalCorrector::rUpdates(ThreadID tid, Addr pc, bool taken, BranchInfo* bi, int64_t phist, std::vector<int8_t> & w)
// {
//     DPRINTF(NewTage, "pc %lx Update; taken = %d\n", pc, taken);
//     // int xsum = bi->lsum - ((wr[getIndUpds(pc)] >= 0)) * bi->pre_result;
//     // if ((xsum + bi->pre_result >= 0) != (xsum >= 0)) {
//     //     ctrUpdate(wr[getIndUpds(pc)], ((bi->pre_result >= 0) == taken),
//     //               extraWeightsWidth);
//     // }
//     int lsum_w1 = bi->lsum - bi->result + 1 * bi->pre_result;
//     int lsum_w2 = bi->lsum - bi->result + 2 * bi->pre_result;
//     if ((lsum_w1 >= 0) != (lsum_w2 >= 0)) {
//         ctrUpdate(wr[getIndUpds(pc)], ((bi->pre_result >= 0) == taken), extraWeightsWidth);
//     }
//     for(int i = 0; i < 8; i++){
//         if(!bi->ut_bank_vld[i]){
//             std::vector<int> valid_indices;
//             for(int j = 0; j < 4; j++){
//                 if(!bi->ut_valid[i][j]){
//                     continue;
//                 }
//                 else{
//                     valid_indices.push_back(j);
//                 }
//             }
//             if(valid_indices.size() == 0){
//                 continue;
//             }
//             static std::random_device rd;
//             static std::mt19937 gen(rd());
//             std::uniform_int_distribution<size_t> dist(0, valid_indices.size() - 1);      
//             size_t random_index = dist(gen);
//             int selected_j = valid_indices[random_index];          
//             if(Utable[i][bi->ut_index[i]].u[selected_j] <31)
//                 Utable[i][bi->ut_index[i]].u[selected_j] += 1;
//         }
//         else{
//             for(int j = 0; j < 3; ++j){
//                 if(Wtable[i][j][bi->wt_index[i][j]] < 8 && taken)
//                     Wtable[i][j][bi->wt_index[i][j]] += 1;
//                 else if(Wtable[i][j][bi->wt_index[i][j]] > -8 && !taken)
//                     Wtable[i][j][bi->wt_index[i][j]] -= 1;
//             }

//             int base = bi->lsum - bi->result; 
//             int bank_scaled = bi->per_bank[i] >> 2;
//             int bank_weighted = bi->weight * bank_scaled;
//             bool influence = ((base + bank_weighted) >= 0) != (base >= 0);
//             // if(!influence)
//             //     continue;   
//             int selected_j = bi->ut_j[i];
//             if((taken && (bi->per_bank[i] > 0)) || (!taken && (bi->per_bank[i] < 0))){
//                 if(Utable[i][bi->ut_index[i]].u[selected_j] <31)
//                     Utable[i][bi->ut_index[i]].u[selected_j] += 1;
//             }
//             else if((!taken && (bi->per_bank[i] > 0)) || (taken && (bi->per_bank[i] < 0))){
//                 if(Utable[i][bi->ut_index[i]].u[selected_j] > -32)
//                     Utable[i][bi->ut_index[i]].u[selected_j] -= 1;
//             }
//         }
//     }
//     return;
// }

void
TAGE_SC_L_64KB_StatisticalCorrector::rUpdates(ThreadID tid, Addr pc, bool taken, BranchInfo* bi, int64_t phist, std::vector<int8_t> & w)
{
    DPRINTF(NewTage, "pc %lx Update; taken = %d\n", pc, taken);
    // int xsum = bi->lsum - ((wr[getIndUpds(pc)] >= 0)) * bi->pre_result;
    // if ((xsum + bi->pre_result >= 0) != (xsum >= 0)) {
    //     ctrUpdate(wr[getIndUpds(pc)], ((bi->pre_result >= 0) == taken),
    //               extraWeightsWidth);
    // }
    
    // int lsum_w1 = bi->lsum - bi->result + 1 * bi->pre_result;
    // int lsum_w2 = bi->lsum - bi->result + 2 * bi->pre_result;
    // if ((lsum_w1 >= 0) != (lsum_w2 >= 0)) {
    //     ctrUpdate(wr[getIndUpds(pc)], ((bi->pre_result >= 0) == taken), extraWeightsWidth);
    // }
    for(int i = 0; i < 8; i++){
        if(!bi->ut_bank_vld[i]){
            std::vector<int> valid_indices;
            for(int j = 0; j < 4; j++){
                if(!bi->ut_valid[i][j]){
                    continue;
                }
                else{
                    valid_indices.push_back(j);
                }
            }
            if(valid_indices.size() == 0){
                continue;
            }
            static std::random_device rd;
            static std::mt19937 gen(rd());
            std::uniform_int_distribution<size_t> dist(0, valid_indices.size() - 1);      
            size_t random_index = dist(gen);
            int selected_j = valid_indices[random_index];    
            uint8_t reg_id = i*4 + selected_j;  

            // uint16_t PR = (pc ^ (pc >> 8) ^ bi->digest[reg_id]) & 4095;
            // uint16_t i1 = (PR + reg_id*633) & (512-1);
            // uint16_t i2 = (((PR >> 2) ^ (PR << 6)) + reg_id) & (256-1);
            // uint16_t i3 = (PR ^ (reg_id << 3)) & (128-1);
            uint32_t i1 = wt_index1(pc, reg_id, bi->digest[reg_id]);
            uint32_t i2 = wt_index2(pc, reg_id, bi->digest[reg_id]);
            uint32_t i3 = wt_index3(pc, reg_id, bi->digest[reg_id]);
            int bank_scaled = RunLts::Wtable0[i][i1].weight + RunLts::Wtable1[i][i2].weight + RunLts::Wtable2[i][i3].weight;
            RunLts::WtableUpdate(i1, i2, i3, i, taken);
            
            // uint8_t idx1 = (uint8_t)(reg_id * 8 + ((pc ^ (pc >> 2)) & 7));
            // uint8_t idx2 = (uint8_t)(reg_id * 8 + ((pc ^ (pc >> 4)) & 7));
            // uint8_t idx3 = (uint8_t)(reg_id * 8 + ((pc ^ (pc >> 6)) & 7));
            uint32_t idx1 = ut_index1(1, pc, reg_id);
            uint32_t idx2 = ut_index1(2, pc, reg_id);
            uint32_t idx3 = ut_index1(3, pc, reg_id);            
            int c = bank_scaled;
            int ut = RunLts::Utable[0][idx1].u + RunLts::Utable[1][idx2].u + RunLts::Utable[2][idx3].u;
            int XSUM = bi->lsum - c * Scale * (ut >= 0);
            if((XSUM + Scale * c >= 0) != (XSUM >= 0)){
                if((c >= 0) == taken){
                    if(RunLts::Utable[0][idx1].u <31)
                        RunLts::Utable[0][idx1].u += 1;
                    if(RunLts::Utable[1][idx2].u <31)
                        RunLts::Utable[1][idx2].u += 1;
                    if(RunLts::Utable[2][idx3].u <31)
                        RunLts::Utable[2][idx3].u += 1;
                }
                else{
                    if(RunLts::Utable[0][idx1].u > -32)
                        RunLts::Utable[0][idx1].u -= 1;
                    if(RunLts::Utable[1][idx2].u > -32)
                        RunLts::Utable[1][idx2].u -= 1;
                    if(RunLts::Utable[2][idx3].u > -32)
                        RunLts::Utable[2][idx3].u -= 1;
                }
            }
        }
        else{
            RunLts::WtableUpdate(bi->wt_index1[i][0], bi->wt_index1[i][1], bi->wt_index1[i][2], i, taken);
            int c = bi->per_bank[i];
            int XSUM = bi->lsum - c * Scale;
            if((XSUM + Scale * c >= 0) != (XSUM >= 0)){
                int selected_j = bi->ut_j[i];
                uint8_t reg_id = i*4 + selected_j;
                if((c >= 0) == taken){
                    if(RunLts::Utable[0][bi->ut_index1[i][0]].u < 31)
                        RunLts::Utable[0][bi->ut_index1[i][0]].u += 1;
                    if(RunLts::Utable[1][bi->ut_index1[i][1]].u < 31)
                        RunLts::Utable[1][bi->ut_index1[i][1]].u += 1;
                    if(RunLts::Utable[2][bi->ut_index1[i][2]].u < 31)
                        RunLts::Utable[2][bi->ut_index1[i][2]].u += 1;
                }
                else{
                    if(RunLts::Utable[0][bi->ut_index1[i][0]].u > -32)
                        RunLts::Utable[0][bi->ut_index1[i][0]].u -= 1;
                    if(RunLts::Utable[1][bi->ut_index1[i][1]].u > -32)
                        RunLts::Utable[1][bi->ut_index1[i][1]].u -= 1;
                    if(RunLts::Utable[2][bi->ut_index1[i][2]].u > -32)
                        RunLts::Utable[2][bi->ut_index1[i][2]].u -= 1;
                }         
            }
        }
    }
    return;
}

int
TAGE_SC_L_TAGE_64KB::gindex_ext(int index, int bank) const
{
    return index;
}

uint16_t
TAGE_SC_L_TAGE_64KB::gtag(ThreadID tid, Addr pc, int bank) const
{
    // very similar to the TAGE implementation, but w/o shifting the pc
    const uint8_t * ghr = threadHistory[tid].gHist;
    int tag = pc ^
              foledGHR ( ghr, histLengths[bank], tagTableTagWidths[bank] ) ^
              ( foledGHR ( ghr, histLengths[bank], tagTableTagWidths[bank] - 1 ) << 1 );

    return (tag & ((1ULL << tagTableTagWidths[bank]) - 1));
}

void
TAGE_SC_L_TAGE_64KB::handleAllocAndUReset(
    bool alloc, bool taken, TAGEBase::BranchInfo* bi, int nrand)
{
    if (! alloc) {
        return;
    }

    int penalty = 0;
    int numAllocated = 0;
    bool maxAllocReached = false;

    for (int I = calcDep(bi); I < nHistoryTables; I += 2) {
        // Handle the 2-way associativity for allocation
        for (int j = 0; j < 2; ++j) {
            int i = ((j == 0) ? I : (I ^ 1)) + 1;
            if (noSkip[i]) {
                if (gtable[i][bi->tableIndices[i]].u == 0) {
                    int8_t ctr = gtable[i][bi->tableIndices[i]].ctr;
                    if (abs (2 * ctr + 1) <= 3) {
                        gtable[i][bi->tableIndices[i]].tag = bi->tableTags[i];
                        gtable[i][bi->tableIndices[i]].ctr = taken ? 0 : -1;
                        numAllocated++;
                        maxAllocReached = (numAllocated == maxNumAlloc);
                        I += 2;
                        break;
                    } else {
                        if (gtable[i][bi->tableIndices[i]].ctr > 0) {
                            gtable[i][bi->tableIndices[i]].ctr--;
                        } else {
                            gtable[i][bi->tableIndices[i]].ctr++;
                        }
                    }
                } else {
                    penalty++;
                }
            }
        }
        if (maxAllocReached) {
            break;
        }
    }

    tCounter += (penalty - 2 * numAllocated);

    handleUReset();
}

void
TAGE_SC_L_TAGE_64KB::handleTAGEUpdate(Addr branch_pc, bool taken,
                                 TAGEBase::BranchInfo* bi)
{
    if (bi->hitBank > 0) {
        if (abs (2 * gtable[bi->hitBank][bi->hitBankIndex].ctr + 1) == 1) {
            if (bi->longestMatchPred != taken) {
                // acts as a protection
                if (bi->altBank > 0) {
                    ctrUpdate(gtable[bi->altBank][bi->altBankIndex].ctr, taken,
                              tagTableCounterBits);
                }
                if (bi->altBank == 0){
                    baseUpdate(branch_pc, taken, bi);
                }
            }
        }

        ctrUpdate(gtable[bi->hitBank][bi->hitBankIndex].ctr, taken,
                  tagTableCounterBits);

        //sign changes: no way it can have been useful
        if (abs (2 * gtable[bi->hitBank][bi->hitBankIndex].ctr + 1) == 1) {
            gtable[bi->hitBank][bi->hitBankIndex].u = 0;
        }

        if (bi->altTaken == taken) {
            if (bi->altBank > 0) {
                int8_t ctr = gtable[bi->altBank][bi->altBankIndex].ctr;
                if (abs (2 * ctr + 1) == 7) {
                    if (gtable[bi->hitBank][bi->hitBankIndex].u == 1) {
                        if (bi->longestMatchPred == taken) {
                          gtable[bi->hitBank][bi->hitBankIndex].u = 0;
                        }
                    }
                }
            }
        }
    } else {
        baseUpdate(branch_pc, taken, bi);
    }

    if ((bi->longestMatchPred != bi->altTaken) &&
        (bi->longestMatchPred == taken) &&
        (gtable[bi->hitBank][bi->hitBankIndex].u < (1 << tagTableUBits) -1)) {
            gtable[bi->hitBank][bi->hitBankIndex].u++;
    }
}

TAGE_SC_L_64KB::TAGE_SC_L_64KB(const TAGE_SC_L_64KBParams &params)
  : TAGE_SC_L(params)
{
}

} // namespace branch_prediction
} // namespace gem5