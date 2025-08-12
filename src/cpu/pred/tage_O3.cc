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
  * Implementation of a TAGE branch predictor
  */
 
  #include "cpu/pred/tage_O3.hh"
 
  #include "base/intmath.hh"
  #include "base/logging.hh"
  #include "base/random.hh"
  #include "base/trace.hh"
  #include "debug/Fetch.hh"
  #include "debug/Tage.hh"
  #include "debug/BranchNet.hh"
  #include "debug/Debug.hh"
  #include "debug/NewTage.hh"
  #include <unistd.h>
  #include <fcntl.h>
  #include <sys/types.h>
  #include <sys/wait.h>
  #include <unordered_set>

  namespace gem5
  {
  
  namespace branch_prediction
  {
  
  TAGE::TAGE(const TAGEParams &params) : BPredUnit(params), tage(params.tage)
  {
    useBranchNet = false;
    branchNetConfidenceThreshold = 0.6;
    if (useBranchNet) {
        initBranchNetService();
    }
    
  }

  TAGE::~TAGE()
  {
      // 清理分支预测服务
      if (branchNetServiceIn) {
          // 发送退出命令
          fprintf(branchNetServiceOut, "EXIT\n");
          fflush(branchNetServiceOut);
          
          fclose(branchNetServiceIn);
          branchNetServiceIn = nullptr;
      }
      
      if (branchNetServiceOut) {
          fclose(branchNetServiceOut);
          branchNetServiceOut = nullptr;
      }
  }
  


void TAGE::initBranchNetService()
{
    // 创建两个管道：一个用于gem5->Python，一个用于Python->gem5
    int pipe_to_child[2];   // [0]: read end, [1]: write end
    int pipe_from_child[2]; // [0]: read end, [1]: write end
    
    if (pipe(pipe_to_child) < 0 || pipe(pipe_from_child) < 0) {
        warn("Failed to create pipes for BranchNet service");
        return;
    }
    
    pid_t pid = fork();
    if (pid < 0) {
        warn("Failed to fork for BranchNet service");
        close(pipe_to_child[0]);
        close(pipe_to_child[1]);
        close(pipe_from_child[0]);
        close(pipe_from_child[1]);
        return;
    }
    
    if (pid == 0) { // 子进程 (Python)
        // 关闭不需要的管道端
        close(pipe_to_child[1]); // 关闭父进程写端
        close(pipe_from_child[0]); // 关闭父进程读端
        
        // 重定向标准输入输出
        dup2(pipe_to_child[0], STDIN_FILENO);
        dup2(pipe_from_child[1], STDOUT_FILENO);
        
        // 关闭原始管道端
        close(pipe_to_child[0]);
        close(pipe_from_child[1]);
        
        // 执行Python脚本
        execlp("python3", "python3", "/Data3/yutong.han/My_G5Project/BranchNet/bin/gem5_run.py", (char*)NULL);
        
        // 如果执行到这里，说明execlp失败
        perror("execlp failed");
        exit(1);
    } else { // 父进程 (gem5)
        // 关闭不需要的管道端
        close(pipe_to_child[0]); // 关闭子进程读端
        close(pipe_from_child[1]); // 关闭子进程写端
        
        // 创建FILE指针用于读写
        branchNetServiceOut = fdopen(pipe_to_child[1], "w");
        branchNetServiceIn = fdopen(pipe_from_child[0], "r");
        
        if (!branchNetServiceIn || !branchNetServiceOut) {
            warn("Failed to create FILE streams for BranchNet service");
            if (branchNetServiceIn) fclose(branchNetServiceIn);
            if (branchNetServiceOut) fclose(branchNetServiceOut);
            branchNetServiceIn = nullptr;
            branchNetServiceOut = nullptr;
            return;
        }
        
        // 等待服务就绪
        char buffer[128];
        if (!fgets(buffer, sizeof(buffer), branchNetServiceIn)) {
            warn("BranchNet service did not start properly");
            fclose(branchNetServiceIn);
            fclose(branchNetServiceOut);
            branchNetServiceIn = nullptr;
            branchNetServiceOut = nullptr;
            return;
        }
        
        // 检查是否收到"READY"
        if (strstr(buffer, "READY") == nullptr) {
            warn("BranchNet service not ready: %s", buffer);
            fclose(branchNetServiceIn);
            fclose(branchNetServiceOut);
            branchNetServiceIn = nullptr;
            branchNetServiceOut = nullptr;
        }
    }
}

//   void TAGE::initBranchNetService()
//   {
//       // 启动服务进程
//       setenv("PYTHONPATH", "/Data3/yutong.han/My_G5Project/BranchNet:$PYTHONPATH", 1);
//       std::string cmd = "/usr/bin/python3 /Data3/yutong.han/My_G5Project/BranchNet/bin/gem5_run.py";
//       branchNetService = popen(cmd.c_str(), "r");
//       if (!branchNetService) {
//           perror("popen() error details");
//           warn("Failed to start BranchNet service");
//           return;
//       }
      
//       // 等待服务就绪
//       char buffer[128];
//       if (!fgets(buffer, sizeof(buffer), branchNetService)) {
//           warn("BranchNet service did not start properly");
//           pclose(branchNetService);
//           branchNetService = nullptr;
//           return;
//       }
      
//       // 检查是否收到"READY"
//       if (strstr(buffer, "READY") == nullptr) {
//           warn("BranchNet service not ready: %s", buffer);
//           pclose(branchNetService);
//           branchNetService = nullptr;
//       }
//   }
  
bool TAGE::queryBranchNet(Addr pc, bool& prediction, float& confidence)
{
    if (!branchNetServiceIn || !branchNetServiceOut) {
        return false;
    }
    
    // 转换历史为整数数组：0表示NT, 1表示T
    std::vector<uint64_t> intHistory;
    getBranchNetHistory(0, intHistory, 212);
    
    std::ostringstream oss;
    oss  << std::hex << "0x" << pc          // PC
         << std::dec << ' ' << intHistory.size(); // 长度
    for (auto v : intHistory) oss << ' ' << v;
    oss << '\n';

    fputs(oss.str().c_str(), branchNetServiceOut);
    fflush(branchNetServiceOut);

    /* 读取结果 */
    char status[16];
    int  pred;
    float conf;
    if (fscanf(branchNetServiceIn, "%15s %d %f", status, &pred, &conf) != 3)
        return false;

    if (!strcmp(status, "SUCCESS")) {
        prediction = (pred == 1);
        confidence = conf;
        return true;
    }
    return false;
}


// 关闭BranchNet服务
void TAGE::closeBranchNetService()
{
    if (branchNetService) {
        fprintf(branchNetService, "EXIT\n");
        fflush(branchNetService);
        pclose(branchNetService);
        branchNetService = nullptr;
    }
}

void TAGE::getBranchNetHistory(ThreadID tid,std::vector<uint64_t>& out,unsigned needLen)
{
    const TAGEBase::ThreadHistory &th = tage->threadHistory[tid];
    out.resize(needLen, 0);
    out.clear();
    out.reserve(needLen);

    /* newest 条目的索引是 (bnHead - 1) */
    int head = (th.bnHead - 1) & (tage->kBnHistLen - 1);

    /* 逆向遍历 needLen-1 … 0 ，最后 push_back 得到 oldest→newest */
    for (int i = needLen - 1; i >= 0; --i) {
        int idx = (head - i) & (tage->kBnHistLen - 1);
        out.push_back(th.branchNetHist[idx]);
    }
}

  // PREDICTOR UPDATE
  void
  TAGE::update(ThreadID tid, Addr pc, bool taken, void *bp_history,
                bool squashed, const StaticInstPtr & inst, Addr target)
  {
      TAGEBase::ThreadHistory& tHist = tage->threadHistory[tid];
        /* BranchNet 特征写入 */
      tHist.branchNetHist[tHist.bnHead] = tage->encodeBranchNet(pc, taken);
      tHist.bnHead = (tHist.bnHead + 1) & (tage->kBnHistLen - 1);   // 位运算取模
      if (tHist.bnCount < tage->kBnHistLen) 
            ++tHist.bnCount;
      assert(bp_history);
      tage->GHRRecord(taken);
      TageBranchInfo *bi = static_cast<TageBranchInfo*>(bp_history);
      TAGEBase::BranchInfo *tage_bi = bi->tageBranchInfo;
      /********update reg pred*********/
      uint32_t ut_index[8];
      uint32_t wt_index[8];
      for(int i = 0; i < 8; i++){
        ut_index[i] = tage_bi->ut_index[i];
        wt_index[i] = tage_bi->wt_index[i];
        std::vector<int> valid_indices;
        for (int j = 0; j < 4; ++j) {
            if (inst->regtable[i*4 + j] != 0) {
                valid_indices.push_back(j);
            }
        }
        if (valid_indices.empty()) {
            continue;;
        }
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_int_distribution<size_t> dist(0, valid_indices.size() - 1);
        
        size_t random_index = dist(gen);
        int selected_j = valid_indices[random_index];
        if(tage->Utable[i][ut_index[i]].u[selected_j] > 0 && squashed){
            tage->Utable[i][ut_index[i]].u[selected_j] --;
        }
        else if(tage->Utable[i][ut_index[i]].u[selected_j] < 8 && (taken == 1 && tage_bi->result > 0 || taken == 0 && tage_bi->result <= 0)){
            tage->Utable[i][ut_index[i]].u[selected_j] ++;
        }
        if(tage->Wtable[i][wt_index[i]].weight > -8 && (taken == 1 && tage_bi->result <= 0 || taken == 0 && tage_bi->result > 0)){
            tage->Wtable[i][wt_index[i]].weight --;
        }
        else if(tage->Wtable[i][wt_index[i]].weight < 8 && (taken == 1 && tage_bi->result > 0 || taken == 0 && tage_bi->result <= 0)){
            tage->Wtable[i][wt_index[i]].weight ++;
        }
        // if(tage->Utable[i][ut_index[i]].u[selected_j] > 0 && (taken == 1 && tage_bi->result <= 0 || taken == 0 && tage_bi->result > 0)){
        //     tage->Utable[i][ut_index[i]].u[selected_j] --;
        // }
        // else if(tage->Utable[i][ut_index[i]].u[selected_j] < 8 && (taken == 1 && tage_bi->result > 0 || taken == 0 && tage_bi->result <= 0)){
        //     tage->Utable[i][ut_index[i]].u[selected_j] ++;
        // }
        // if(tage->Wtable[i][wt_index[i]].weight > -8 && (taken == 1 && tage_bi->result <= 0 || taken == 0 && tage_bi->result > 0)){
        //     tage->Wtable[i][wt_index[i]].weight --;
        // }
        // else if(tage->Wtable[i][wt_index[i]].weight < 8 && (taken == 1 && tage_bi->result > 0 || taken == 0 && tage_bi->result <= 0)){
        //     tage->Wtable[i][wt_index[i]].weight ++;
        // }
    }
        if(squashed){
            DPRINTF(NewTage, "Squash for pc:%lx; taken?:%d, tage_pred?:%d, tage_ctr:%d, reg_pred?:%d, reg_ctr:%d\n",
                  pc, taken, tage_bi->provider == 1?tage_bi->longestMatchPred:tage_bi->altTaken, 
                  tage_bi->provider == 1?tage_bi->hit_ctr:(tage_bi->provider == 2?tage_bi->alt_ctr:0),
                  tage_bi->result > 0?1:0, tage_bi->result);
        }
       /***********end************/
      bool needSquashed = squashed && !tage_bi->usebranchnet 
            || squashed && tage_bi->usebranchnet && !tage_bi->diffpred
            || !squashed && tage_bi->usebranchnet && tage_bi->diffpred;
      if(isBranchNetPC(pc)){
        if(predcnt.find(pc) == predcnt.end())
            assert(0);
        else{
            if(needSquashed && tage_bi->diffpred)
                predcnt[pc] --;
            else if(!needSquashed && tage_bi->diffpred)
                predcnt[pc] ++;
        }
      }
      if (squashed) {
          // This restores the global history, then update it
          // and recomputes the folded histories.
          DPRINTF(BranchNet, "Squash PC: %lx, Tage pred %s, Final pred %s, Use %s\n",
                    pc,bi->tageBranchInfo->tagePred? 1:0, squashed? "Fault":"Correct",tage_bi->usebranchnet? "BranchNet" : "TAGE");
          tage->squash(tid, taken, tage_bi, target);
          return;
      }
  
      int nrand = 0;
      if (bi->tageBranchInfo->condBranch) {
          DPRINTF(Tage, "Updating tables for branch:%lx; taken?:%d\n",
                  pc, taken);
          tage->updateStats(taken, bi->tageBranchInfo);
          tage->condBranchUpdate(tid, pc, taken, tage_bi, nrand,
                                 target, bi->tageBranchInfo->tagePred);
      }
  
      // optional non speculative update of the histories
      tage->updateHistories(tid, pc, taken, tage_bi, false, inst, target);
      delete bi; //pred failed, tage currect
      bp_history = nullptr; 
  }
  
  void
  TAGE::squash(ThreadID tid, void *bp_history)
  {
      TageBranchInfo *bi = static_cast<TageBranchInfo*>(bp_history);
      DPRINTF(Tage, "Deleting branch info: %lx\n", bi->tageBranchInfo->branchPC);
      delete bi;
      bp_history = nullptr; 
  }
  
  bool
  TAGE::predict(ThreadID tid, Addr pc, bool cond_branch, void* &b)
  {
      TageBranchInfo *bi = new TageBranchInfo(*tage);//nHistoryTables+1);
      b = (void*)(bi);
      return tage->tagePredict(tid, pc, cond_branch, bi->tageBranchInfo);
  }
bool
  TAGE::predict(ThreadID tid, Addr pc, bool cond_branch, void* &b, const StaticInstPtr & inst)
  {
      TageBranchInfo *bi = new TageBranchInfo(*tage);//nHistoryTables+1);
      b = (void*)(bi);
      return tage->tagePredict(tid, pc, cond_branch, bi->tageBranchInfo,inst);
  }
  
  bool
  TAGE::lookup(ThreadID tid, Addr pc, void* &bp_history)
  {
    bool prediction;
    bool retval = predict(tid, pc, true, bp_history);
    DPRINTF(BranchNet, "TAGE Lookup branch PC: %lx; predict:%d\n", pc, retval);
    TageBranchInfo *bi = static_cast<TageBranchInfo*>(bp_history);
    if(isBranchNetPC(pc)){
        if(predcnt.find(pc) == predcnt.end()){
            predcnt[pc] = 0;
        }
    }
    // 首先尝试BranchNet预测
    if (useBranchNet && branchNetService && isBranchNetPC(pc)) {
        float confidence;
        
        if (queryBranchNet(pc, prediction, confidence)) {
            if(retval != prediction){
                bi->tageBranchInfo->diffpred = true; // 标记预测不一致
            } else {
                bi->tageBranchInfo->diffpred = false; // 标记预测一致
            }
            DPRINTF(BranchNet, "BranchNet prediction for PC %#x: %s (conf %.2f)\n",
                pc, prediction ? "TAKEN" : "NOT TAKEN", confidence);
            // 检查置信度是否足够
            if (confidence >= branchNetConfidenceThreshold || 
                (1 - confidence) >= branchNetConfidenceThreshold) {
                // 使用BranchNet预测结果
                if(predcnt[pc] <= 0){
                    DPRINTF(BranchNet, "Use BranchNet for BN PC %#x: %s (conf %.2f)\n",
                    pc, prediction ? "TAKEN" : "NOT TAKEN", confidence);
                    DPRINTF(Debug, "Use BranchNet for BN PC %#x: %s (conf %.2f)\n",
                        pc, prediction ? "TAKEN" : "NOT TAKEN", confidence);
                    bi->tageBranchInfo->usebranchnet = true; // 标记使用了BranchNet
                    tage->updateHistories(tid, pc, prediction, bi->tageBranchInfo, true);
                    return prediction;
                }
            }
        }
    }
    if(isBranchNetPC(pc)){
        DPRINTF(BranchNet, "Use Tage for BN PC %#x: %s\n",
            pc, retval ? "TAKEN" : "NOT TAKEN");
        DPRINTF(Debug, "Use Tage for BN PC %#x: %s\n",
            pc, retval ? "TAKEN" : "NOT TAKEN");
    }
    tage->updateHistories(tid, pc, retval, bi->tageBranchInfo, true);
    return retval;
  }
  
bool
  TAGE::lookup(ThreadID tid, Addr pc, void* &bp_history, const StaticInstPtr & inst)
  {
    bool prediction;
    bool retval = predict(tid, pc, true, bp_history, inst);
    DPRINTF(BranchNet, "TAGE Lookup branch PC: %lx; predict:%d\n", pc, retval);
    TageBranchInfo *bi = static_cast<TageBranchInfo*>(bp_history);
    if(isBranchNetPC(pc)){
        if(predcnt.find(pc) == predcnt.end()){
            predcnt[pc] = 0;
        }
    }
    // 首先尝试BranchNet预测
    if (useBranchNet && branchNetService && isBranchNetPC(pc)) {
        float confidence;
        
        if (queryBranchNet(pc, prediction, confidence)) {
            if(retval != prediction){
                bi->tageBranchInfo->diffpred = true; // 标记预测不一致
            } else {
                bi->tageBranchInfo->diffpred = false; // 标记预测一致
            }
            DPRINTF(BranchNet, "BranchNet prediction for PC %#x: %s (conf %.2f)\n",
                pc, prediction ? "TAKEN" : "NOT TAKEN", confidence);
            // 检查置信度是否足够
            if (confidence >= branchNetConfidenceThreshold || 
                (1 - confidence) >= branchNetConfidenceThreshold) {
                // 使用BranchNet预测结果
                if(predcnt[pc] <= 0){
                    DPRINTF(BranchNet, "Use BranchNet for BN PC %#x: %s (conf %.2f)\n",
                    pc, prediction ? "TAKEN" : "NOT TAKEN", confidence);
                    DPRINTF(Debug, "Use BranchNet for BN PC %#x: %s (conf %.2f)\n",
                        pc, prediction ? "TAKEN" : "NOT TAKEN", confidence);
                    bi->tageBranchInfo->usebranchnet = true; // 标记使用了BranchNet
                    tage->updateHistories(tid, pc, prediction, bi->tageBranchInfo, true);
                    return prediction;
                }
            }
        }
    }
    if(isBranchNetPC(pc)){
        DPRINTF(BranchNet, "Use Tage for BN PC %#x: %s\n",
            pc, retval ? "TAKEN" : "NOT TAKEN");
        DPRINTF(Debug, "Use Tage for BN PC %#x: %s\n",
            pc, retval ? "TAKEN" : "NOT TAKEN");
    }
    tage->updateHistories(tid, pc, retval, bi->tageBranchInfo, true);
    return retval;
  }
  void
  TAGE::updateHistories(ThreadID tid, Addr pc, bool uncond,
                           bool taken, Addr target, void * &bp_history)
  {
      assert(uncond || bp_history);
      if (uncond) {
          DPRINTF(Tage, "UnConditionalBranch: %lx\n", pc);
          predict(tid, pc, false, bp_history);
      }
  
      // Update the global history for all branches
      TageBranchInfo *bi = static_cast<TageBranchInfo*>(bp_history);
      tage->updateHistories(tid, pc, taken, bi->tageBranchInfo, true);
  }
  
  void 
  TAGE::uncondBranch(ThreadID tid, Addr br_pc, void* &bp_history)
  {
    DPRINTF(Tage, "UnConditionalBranch: %lx\n", br_pc);
    predict(tid, br_pc, false, bp_history);
    TageBranchInfo *bi = static_cast<TageBranchInfo*>(bp_history);
    tage->updateHistories(tid, br_pc, true, bi->tageBranchInfo, true);
}
  
  void
  TAGE::btbUpdate(ThreadID tid, Addr branch_addr, void* &bp_history)
{
        TageBranchInfo *bi = static_cast<TageBranchInfo*>(bp_history);
        tage->btbUpdate(tid, branch_addr, bi->tageBranchInfo);
}
  int 
  TAGE::get_random_0_to_3() {
    // 静态对象只初始化一次
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<int> dist(0, 3);
    
    return dist(gen);
}
  } // namespace branch_prediction
  } // namespace gem5