/*
 * Copyright (c) 2004-2005 The Regents of The University of Michigan
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
#ifndef __CPU_PRED_NEW_BTB_HH__ 
#define __CPU_PRED_NEW_BTB_HH__

#include "arch/generic/pcstate.hh"
#include "base/logging.hh"
#include "base/types.hh"
#include "base/intmath.hh"
#include "cpu/static_inst.hh"
#include "cpu/pred/btb.hh"
#include "params/NewBTB.hh"

namespace gem5
{

namespace branch_prediction
{
class NewBTB : public BranchTargetBuffer{
  private:
    struct BTBEntry
    {
        /** The entry's tag. */
        Addr tag = 0;

        /** The entry's target. */
        PCStateBase *target = nullptr;

        /** The entry's thread id. */
        ThreadID tid;

        /** Whether or not the entry is valid. */
        bool valid = false;
    };

    struct Indexes {
        unsigned threadIndex;
        unsigned bankIndex;
        unsigned entryIndex;
    };

  private:
    std::vector<std::vector<std::vector<std::vector<BTBEntry>>>> sets;
    std::vector<std::vector<std::vector<std::list<int>>>> lruQueues;

    unsigned numSets;
    unsigned associativity;
    unsigned numBanks;
    
    typedef NewBTBParams Params;
  public:
    NewBTB(const Params &params)
        : BranchTargetBuffer(params),
          associativity(params.BTBWays),
          numBanks(params.BTBBanks)
    {
        unsigned totalEntries = params.BTBEntries;
        unsigned numThreads = params.numThreads;
        if (associativity * numBanks * numThreads == 0) {
            fatal("BTB configuration invalid: ways, banks or threads cannot be 0");
        }
        
        numSets = totalEntries / (associativity * numBanks * numThreads);

        sets.resize(numSets);
        lruQueues.resize(numSets);

        for (unsigned i = 0; i < numSets; ++i) {
            sets[i].resize(numThreads);
            lruQueues[i].resize(numThreads);

            for (unsigned j = 0; j < numThreads; ++j) {
                sets[i][j].resize(numBanks, std::vector<BTBEntry>(associativity));
                lruQueues[i][j].resize(numBanks, std::list<int>(associativity));

                for (unsigned k = 0; k < numBanks; ++k) {
                    int l = 0;
                    for (auto &entryIndex : lruQueues[i][j][k]) {
                        entryIndex = l++;
                    }
                }
            }
        }
    }

    Indexes getIndexs(Addr instPC, ThreadID tid) const {

        unsigned bankBits = (numBanks > 1) ? floorLog2(numBanks) : 0;
        unsigned entryBits = (numSets > 1) ? floorLog2(numSets) : 0;

        unsigned threadIndex = tid;

        unsigned bankIndex = (instPC >> 2) & ((1 << bankBits) - 1);

        unsigned entryIndex = (instPC >> (2 + bankBits)) & ((1 << entryBits) - 1);

        return {threadIndex, bankIndex, entryIndex};
    }

    void updateLRU(unsigned entryIndex, unsigned threadIndex, unsigned bankIndex, int accessedIndex) {

        auto &lruQueue = lruQueues[entryIndex][threadIndex][bankIndex];

        auto it = std::find(lruQueue.begin(), lruQueue.end(), accessedIndex);
        if (it != lruQueue.end()) {
            lruQueue.erase(it);
        }
        lruQueue.push_front(accessedIndex);
    }

    void reset() {
        for (int j = 0; j < numSets; j++) {
            for (int l = 0; l < numThreads; l++) {
                for (int k = 0; k < numBanks; k++) {
                    for (int i = 0; i < associativity; i++) {
                        sets[j][l][k][i].valid = false;
                    }
                }
            }
        }

        for (auto &set : lruQueues) {
            for (auto &thread : set) {
                for (auto &bank : thread) {
                    bank.clear();
                    for (int i = 0; i < associativity; ++i) {
                        bank.push_back(i);
                    }
                }
            }
        }
    }

    const PCStateBase *lookup(ThreadID tid, Addr instPC,
                              BranchType type = BranchType::NoBranch) override
    {
        auto indexs = getIndexs(instPC, tid);
        unsigned entryIndex = indexs.entryIndex;
        unsigned threadIndex = indexs.threadIndex;
        unsigned bankIndex = indexs.bankIndex;

        for (int i = 0; i < associativity; ++i) {
            if (sets[entryIndex][threadIndex][bankIndex][i].valid &&
                sets[entryIndex][threadIndex][bankIndex][i].tag == instPC &&
                sets[entryIndex][threadIndex][bankIndex][i].tid == tid) {
                
                updateLRU(entryIndex, threadIndex, bankIndex, i);
                return sets[entryIndex][threadIndex][bankIndex][i].target;
            }
        }
        return nullptr;
    }

    bool valid(ThreadID tid, Addr instPC) override
    {
        auto indexs = getIndexs(instPC, tid);
        unsigned entryIndex = indexs.entryIndex;
        unsigned threadIndex = indexs.threadIndex;
        unsigned bankIndex = indexs.bankIndex;

        for (int i = 0; i < associativity; ++i) {
            if (sets[entryIndex][threadIndex][bankIndex][i].valid &&
                sets[entryIndex][threadIndex][bankIndex][i].tag == instPC &&
                sets[entryIndex][threadIndex][bankIndex][i].tid == tid) {
                return true;
            }
        }
        return false;
    }

    struct UpdateResult {
        int index;
        bool conflict;
        BTBEntry evictedEntry;
    };

    void update(ThreadID tid, Addr instPC, const PCStateBase &targetPC,
            BranchType type = BranchType::NoBranch,
            StaticInstPtr inst = nullptr) override {
        auto indexs = getIndexs(instPC, tid);
        unsigned entryIndex = indexs.entryIndex;
        unsigned threadIndex = indexs.threadIndex;
        unsigned bankIndex = indexs.bankIndex;
        UpdateResult result;
        result.conflict = false;

        int globalIndex = entryIndex * numThreads * numBanks * associativity + threadIndex * numBanks * associativity + bankIndex * associativity;

        for (int i = 0; i < associativity; ++i) {
            if (sets[entryIndex][threadIndex][bankIndex][i].valid && sets[entryIndex][threadIndex][bankIndex][i].tag == instPC) {
                set(sets[entryIndex][threadIndex][bankIndex][i].target, targetPC);
                updateLRU(entryIndex, threadIndex, bankIndex, i);
                result.index = globalIndex + i;
                return;
            }
        }

        int replaceIndex = lruQueues[entryIndex][threadIndex][bankIndex].back();
        if (sets[entryIndex][threadIndex][bankIndex][replaceIndex].valid && sets[entryIndex][threadIndex][bankIndex][replaceIndex].tag != instPC) {
            result.conflict = true;
            result.evictedEntry = sets[entryIndex][threadIndex][bankIndex][replaceIndex];
        }

        sets[entryIndex][threadIndex][bankIndex][replaceIndex].tag = instPC;
        set(sets[entryIndex][threadIndex][bankIndex][replaceIndex].target, targetPC);
        sets[entryIndex][threadIndex][bankIndex][replaceIndex].tid = tid;
        sets[entryIndex][threadIndex][bankIndex][replaceIndex].valid = true;
        updateLRU(entryIndex, threadIndex, bankIndex, replaceIndex);

        result.index = globalIndex + replaceIndex;

        return;
    }

    bool removeEntry(Addr instPC, ThreadID tid) {
        auto indexes = getIndexs(instPC, tid);
        unsigned entryIndex = indexes.entryIndex;
        unsigned threadIndex = indexes.threadIndex;
        unsigned bankIndex = indexes.bankIndex;
        bool removed = false;

        for (int i = 0; i < associativity; ++i) {
            auto &entry = sets[entryIndex][threadIndex][bankIndex][i];
            if (entry.valid && entry.tag == instPC && entry.tid == tid) {
                entry.valid = false;  // Mark the entry as invalid
                updateLRUOnRemove(entryIndex, threadIndex, bankIndex, i);  // Update LRU queue if necessary
                removed = true;
                break;
            }
        }

        return removed;
    }

    void updateLRUOnRemove(unsigned entryIndex, unsigned threadIndex, unsigned bankIndex, int accessedIndex) {
        auto &lruQueue = lruQueues[entryIndex][threadIndex][bankIndex];
        lruQueue.remove(accessedIndex);  // Remove the accessed index from LRU
        lruQueue.push_back(accessedIndex);  // Append at the end as least recently used
    }
    void
    memInvalidate() override
    {
        for (int j = 0; j < numSets; j++) {
            for (int l = 0; l < numThreads; l++) {
                for (int k = 0; k < numBanks; k++) {
                    for (int i = 0; i < associativity; i++) {
                        sets[j][l][k][i].valid = false;
                    }
                    // 重置 LRU
                    auto &lruQueue = lruQueues[j][l][k];
                    lruQueue.clear();
                    for (int i = 0; i < associativity; ++i) {
                        lruQueue.push_back(i);
                    }
                }
            }
        }
    }
    const StaticInstPtr getInst(ThreadID tid, Addr instPC) override
    {
        return nullptr;
    }


};

} // namespace branch_prediction
} // namespace gem5
#endif // __CPU_PRED_NEW_BTB_HH__