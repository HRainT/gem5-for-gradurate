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

#ifndef __CPU_PRED_BTB_HH__
#define __CPU_PRED_BTB_HH__

#include "arch/generic/pcstate.hh"
#include "base/logging.hh"
#include "base/types.hh"
#include "base/intmath.hh"

namespace gem5
{

namespace branch_prediction
{

class DefaultBTB
{
  private:
    struct BTBEntry
    {
        /** The entry's tag. */
        Addr tag = 0;

        /** The entry's target. */
        std::unique_ptr<PCStateBase> target;

        /** The entry's thread id. */
        ThreadID tid;

        /** Whether or not the entry is valid. */
        bool valid = false;
    };

  public:
    /** Creates a BTB with the given number of entries, number of bits per
     *  tag, and instruction offset amount.
     *  @param numEntries Number of entries for the BTB.
     *  @param tagBits Number of bits for each tag in the BTB.
     *  @param instShiftAmt Offset amount for instructions to ignore alignment.
     */
    DefaultBTB(unsigned numEntries, unsigned tagBits,
               unsigned instShiftAmt, unsigned numThreads);

    void reset();

    /** Looks up an address in the BTB. Must call valid() first on the address.
     *  @param inst_PC The address of the branch to look up.
     *  @param tid The thread id.
     *  @return Returns the target of the branch.
     */
    const PCStateBase *lookup(Addr instPC, ThreadID tid);

    /** Checks if a branch is in the BTB.
     *  @param inst_PC The address of the branch to look up.
     *  @param tid The thread id.
     *  @return Whether or not the branch exists in the BTB.
     */
    bool valid(Addr instPC, ThreadID tid);

    /** Updates the BTB with the target of a branch.
     *  @param inst_pc The address of the branch being updated.
     *  @param target_pc The target address of the branch.
     *  @param tid The thread id.
     */
    void update(Addr inst_pc, const PCStateBase &target_pc, ThreadID tid);

  public:
    /** Returns the index into the BTB, based on the branch's PC.
     *  @param inst_PC The branch to look up.
     *  @return Returns the index into the BTB.
     */
    unsigned getIndex(Addr instPC, ThreadID tid);

    /** Returns the tag bits of a given address.
     *  @param inst_PC The branch's address.
     *  @return Returns the tag bits.
     */
    Addr getTag(Addr instPC);

    /** The actual BTB. */
    std::vector<BTBEntry> btb;

    /** The number of entries in the BTB. */
    unsigned numEntries;

    /** The index mask. */
    unsigned idxMask;

    /** The number of tag bits per entry. */
    unsigned tagBits;

    /** The tag mask. */
    unsigned tagMask;

    /** Number of bits to shift PC when calculating index. */
    unsigned instShiftAmt;

    /** Number of bits to shift PC when calculating tag. */
    unsigned tagShiftAmt;

    /** Log2 NumThreads used for hashing threadid */
    unsigned log2NumThreads;

    unsigned logBankNum;
    unsigned logNumEntries;
};

class SetAssociativeBTB {
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
    unsigned numThreads;

  public:
    SetAssociativeBTB(unsigned numEntries, unsigned ways, unsigned banks, unsigned threads)
    : numSets(numEntries / (ways * banks * threads)), associativity(ways),
        numBanks(banks), numThreads(threads)
    {
        assert(numEntries % (ways * banks * threads) == 0);

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

        unsigned bankBits = floorLog2(numBanks);
        unsigned entryBits = floorLog2(numSets);

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

    const PCStateBase *lookup(Addr instPC, ThreadID tid) {
        auto indexs = getIndexs(instPC, tid);
        unsigned entryIndex = indexs.entryIndex;
        unsigned threadIndex = indexs.threadIndex;
        unsigned bankIndex = indexs.bankIndex;
        for (int i = 0; i < associativity; ++i) {
            if (sets[entryIndex][threadIndex][bankIndex][i].valid 
                    && sets[entryIndex][threadIndex][bankIndex][i].tag == instPC 
                    && sets[entryIndex][threadIndex][bankIndex][i].tid == tid) {
                updateLRU(entryIndex, threadIndex, bankIndex, i);
                return sets[entryIndex][threadIndex][bankIndex][i].target;
            }
        }
        return nullptr;
    }

    bool valid(Addr instPC, ThreadID tid) {
        auto indexs = getIndexs(instPC, tid);
        unsigned entryIndex = indexs.entryIndex;
        unsigned threadIndex = indexs.threadIndex;
        unsigned bankIndex = indexs.bankIndex;
        for (int i = 0; i < associativity; ++i) {
            if (sets[entryIndex][threadIndex][bankIndex][i].valid 
                    && sets[entryIndex][threadIndex][bankIndex][i].tag == instPC 
                    && sets[entryIndex][threadIndex][bankIndex][i].tid == tid) {
                // updateLRU(entryIndex, threadIndex, bankIndex, i);
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

    UpdateResult update(Addr instPC, const PCStateBase &targetPC, ThreadID tid) {
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
                return result;
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

        return result;
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

};

} // namespace branch_prediction
} // namespace gem5

#endif // __CPU_PRED_BTB_HH__
