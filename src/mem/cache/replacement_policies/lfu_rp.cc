/**
 * Copyright (c) 2018-2020 Inria
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

#include "mem/cache/replacement_policies/lfu_rp.hh"

#include <cassert>
#include <memory>
#include <vector>

#include "base/trace.hh"
#include "debug/L2CacheReplacement.hh"
#include "mem/cache/cache_blk.hh"
#include "params/LFURP.hh"

namespace gem5
{

namespace replacement_policy
{

using CacheBlk = gem5::CacheBlk;  // Type alias for convenience

LFURP::LFURP(const BaseReplacementPolicyParams &p, unsigned cache_level)
  : Base(p), cacheLevel(cache_level)
{
}

LFURP::LFURP(const Params &p)
  : LFURP(p, p.cache_level)
{
}

void
LFURP::invalidate(const std::shared_ptr<ReplacementData>& replacement_data)
{
    // Reset reference count
    std::static_pointer_cast<LFURPReplData>(replacement_data)->refCount = 0;
}

void
LFURP::touch(const std::shared_ptr<ReplacementData>& replacement_data) const
{
    // Update reference count
    std::static_pointer_cast<LFURPReplData>(replacement_data)->refCount++;
}

void
LFURP::reset(const std::shared_ptr<ReplacementData>& replacement_data) const
{
    // Reset reference count
    std::static_pointer_cast<LFURPReplData>(replacement_data)->refCount = 1;
}

CacheBlk*
LFURP::getCacheBlk(ReplaceableEntry* entry) const
{
    return static_cast<CacheBlk*>(entry);
}

ReplaceableEntry*
LFURP::getVictim(const ReplacementCandidates& candidates) const
{
    // There must be at least one replacement candidate
    assert(candidates.size() > 0);

    if (cacheLevel != 2) {
        ReplaceableEntry* victim = candidates[0];

        for (const auto& candidate : candidates) {
            unsigned candidate_count = std::static_pointer_cast<LFURPReplData>(
                candidate->replacementData)->refCount;

            if (candidate_count == 0) {
                victim = candidate;
                break;
            }
        }

        auto victim_data = std::static_pointer_cast<LFURPReplData>(
            victim->replacementData);
        if (victim_data->refCount != 0) {
            for (const auto& candidate : candidates) {
                auto candidate_data = std::static_pointer_cast<LFURPReplData>(
                    candidate->replacementData);
                if (candidate_data->refCount < victim_data->refCount) {
                    victim = candidate;
                    victim_data = candidate_data;
                }
            }
        }

        DPRINTF(L2CacheReplacement, "[LFURP] Victim way %p (L%u)\n", victim,
            cacheLevel);
        return victim;
    }

    // L2 replacement logic

    std::vector<ReplaceableEntry*> invalid_candidates;
    std::vector<ReplaceableEntry*> not_in_l1_candidates;
    std::vector<ReplaceableEntry*> in_l1_candidates;

    for (const auto& candidate : candidates) {
        auto candidate_data = std::static_pointer_cast<LFURPReplData>(
            candidate->replacementData);
        CacheBlk* blk = getCacheBlk(candidate);

        if (candidate_data->refCount == 0) {
            invalid_candidates.push_back(candidate);
            continue;
        }

        if (blk != nullptr) {
            if (!blk->existsInUpperCache()) {
                not_in_l1_candidates.push_back(candidate);
            } else {
                in_l1_candidates.push_back(candidate);
            }
        } else {
            return candidate;
        }
    }

    auto choose_lfu = [](const std::vector<ReplaceableEntry*>& entries) {
        ReplaceableEntry* best = entries[0];
        auto best_data = std::static_pointer_cast<LFURPReplData>(
            best->replacementData);
        for (const auto& entry : entries) {
            auto data = std::static_pointer_cast<LFURPReplData>(
                entry->replacementData);
            if (data->refCount < best_data->refCount) {
                best = entry;
                best_data = data;
            }
        }
        return best;
    };

    ReplaceableEntry* victim = nullptr;
    if (!invalid_candidates.empty()) {
        victim = choose_lfu(invalid_candidates);
    } else if (!not_in_l1_candidates.empty()) {
        victim = choose_lfu(not_in_l1_candidates);
    } else if (!in_l1_candidates.empty()) {
        victim = choose_lfu(in_l1_candidates);
    } else {
        victim = candidates[0];
    }

    DPRINTF(L2CacheReplacement, "[LFURP] Victim way %p (L%u)\n", victim,
        cacheLevel);

    return victim;
}

std::shared_ptr<ReplacementData>
LFURP::instantiateEntry()
{
    return std::shared_ptr<ReplacementData>(new LFURPReplData());
}

} // namespace replacement_policy
} // namespace gem5
