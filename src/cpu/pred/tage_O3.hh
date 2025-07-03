/*
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
  * Copyright (c) 2014 The University of Wisconsin
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
  */
 
 /* @file
  * Implementation of a TAGE branch predictor. TAGE is a global-history based
  * branch predictor. It features a PC-indexed bimodal predictor and N
  * partially tagged tables, indexed with a hash of the PC and the global
  * branch history. The different lengths of global branch history used to
  * index the partially tagged tables grow geometrically. A small path history
  * is also used in the hash.
  *
  * All TAGE tables are accessed in parallel, and the one using the longest
  * history that matches provides the prediction (some exceptions apply).
  * Entries are allocated in components using a longer history than the
  * one that predicted when the prediction is incorrect.
  */
 
  #ifndef __CPU_PRED_TAGE_HH__
  #define __CPU_PRED_TAGE_HH__
  
  #include <vector>
  #include <cstdio>
  #include "base/random.hh"
  #include "base/types.hh"
  #include "cpu/pred/bpred_unit.hh"
  #include "cpu/pred/tage_base_O3.hh"
  #include "params/TAGE.hh"
  
  namespace gem5
  {
  
  namespace branch_prediction
  {
  
  class TAGE: public BPredUnit
  {
    protected:
      TAGEBase *tage;
  
      struct TageBranchInfo
      {
          TAGEBase::BranchInfo *tageBranchInfo;
  
          TageBranchInfo(TAGEBase &tage) : tageBranchInfo(tage.makeBranchInfo())
          {}
  
          virtual ~TageBranchInfo()
          {
              delete tageBranchInfo;
          }
      };
  
      virtual bool predict(ThreadID tid, Addr branch_pc, bool cond_branch,
                           void* &b);
      static constexpr std::array<uint64_t, 14> astar_branchNetPCs = {
        73560,
        115804,
        107268,
        115256,
        110728,
        90904,
        107240,
        111652,
        115432,
        85408,
        111680,
        73552,
        115240,
        107380
      };

      bool useBranchNet; // 是否启用BranchNet
      FILE* branchNetService; // BranchNet服务进程句柄
      FILE* branchNetServiceIn;   // 用于从Python读取
      FILE* branchNetServiceOut;  // 用于向Python写入
      float branchNetConfidenceThreshold; // 置信度阈值
      
      // BranchNet通信方法
      void initBranchNetService();
      bool queryBranchNet(Addr pc, bool& prediction, float& confidence);
      void closeBranchNetService();
      
      // 获取最近分支历史
      void getBranchNetHistory(ThreadID tid,std::vector<uint16_t>& out,unsigned needLen /*=212*/);
    public:
  
      TAGE(const TAGEParams &params);
  
      // Base class methods.
      bool lookup(ThreadID tid, Addr pc, void* &bp_history) override;
      void updateHistories(ThreadID tid, Addr pc, bool uncond, bool taken,
                           Addr target,  void * &bp_history);
      void update(ThreadID tid, Addr pc, bool taken,
                  void *bp_history, bool squashed,
                  const StaticInstPtr & inst, Addr target)override;
      virtual void squash(ThreadID tid, void *bp_history)override;
      void uncondBranch(ThreadID tid, Addr br_pc, void* &bp_history) override;
      void btbUpdate(ThreadID tid, Addr branch_addr, void* &bp_history) override;
      bool isBranchNetPC(Addr pc) const {
        return std::find(std::begin(astar_branchNetPCs), 
                        std::end(astar_branchNetPCs), pc) != std::end(astar_branchNetPCs);
      }
      ~TAGE();
  };
  
  } // namespace branch_prediction
  } // namespace gem5
  
  #endif // __CPU_PRED_TAGE_HH__