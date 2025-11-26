/*
 * Copyright (c) 2013-2015 Advanced Micro Devices, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from this
 * software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "mem/cache/replacement_policies/weighted_lru_rp.hh"

#include <cassert>
#include <vector>

#include "base/trace.hh"
#include "debug/L2CacheReplacement.hh"
#include "mem/cache/cache_blk.hh"
#include "params/WeightedLRURP.hh"
#include "sim/cur_tick.hh"

namespace gem5
{

namespace replacement_policy
{

WeightedLRURP::WeightedLRURP(const Params &p)
  : LRURP(p)
{
}

void
WeightedLRURP::touch(const std::shared_ptr<ReplacementData>& replacement_data,
    int occupancy) const
{
    LRURP::touch(replacement_data);
    std::static_pointer_cast<WeightedLRURPReplData>(replacement_data)->
        last_occ_ptr = occupancy;
}

ReplaceableEntry*
WeightedLRURP::getVictim(const ReplacementCandidates& candidates) const
{
    assert(!candidates.empty());

    // Use the accurate cache_level from Cache configuration
    if (cacheLevel != 2) {
        // Standard Weighted LRU logic with invalid prioritization
        ReplaceableEntry* victim = candidates[0];

        // Visit all candidates to search for an invalid entry
        for (const auto& candidate : candidates) {
            auto candidate_data = std::static_pointer_cast<WeightedLRURPReplData>(
                candidate->replacementData);
            if (candidate_data->lastTouchTick == Tick(0)) {
                victim = candidate;
                break;
            }
        }

        auto victim_data = std::static_pointer_cast<WeightedLRURPReplData>(
            victim->replacementData);

        // If no invalid entry found, use Weighted LRU
        if (victim_data->lastTouchTick != Tick(0)) {
            for (const auto& candidate : candidates) {
                auto candidate_data = std::static_pointer_cast<WeightedLRURPReplData>(
                    candidate->replacementData);

                if (candidate_data->last_occ_ptr < victim_data->last_occ_ptr) {
                    victim = candidate;
                    victim_data = candidate_data;
                } else if (
                    candidate_data->last_occ_ptr == victim_data->last_occ_ptr &&
                    candidate_data->lastTouchTick < victim_data->lastTouchTick) {
                    victim = candidate;
                    victim_data = candidate_data;
                }
            }
        }

        DPRINTF(L2CacheReplacement, "[WeightedLRURP] Victim way %p (L%u)\n",
            victim, cacheLevel);
        return victim;
    }

    // L2 replacement logic (cacheLevel == 2)

    std::vector<ReplaceableEntry*> invalid_candidates;
    std::vector<ReplaceableEntry*> not_in_l1_candidates;
    std::vector<ReplaceableEntry*> in_l1_candidates;

    for (const auto& candidate : candidates) {
        auto candidate_data = std::static_pointer_cast<WeightedLRURPReplData>(
            candidate->replacementData);
        CacheBlk* blk = getCacheBlk(candidate);

        if (candidate_data->lastTouchTick == Tick(0)) {
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

    auto choose_victim = [](const std::vector<ReplaceableEntry*>& list) {
        ReplaceableEntry* best = list[0];
        auto best_data = std::static_pointer_cast<WeightedLRURPReplData>(
            best->replacementData);
        for (const auto& candidate : list) {
            auto data = std::static_pointer_cast<WeightedLRURPReplData>(
                candidate->replacementData);
            if (data->last_occ_ptr < best_data->last_occ_ptr) {
                best = candidate;
                best_data = data;
            } else if (
                data->last_occ_ptr == best_data->last_occ_ptr &&
                data->lastTouchTick < best_data->lastTouchTick) {
                best = candidate;
                best_data = data;
            }
        }
        return best;
    };

    ReplaceableEntry* victim = nullptr;
    if (!invalid_candidates.empty()) {
        victim = choose_victim(invalid_candidates);
    } else if (!not_in_l1_candidates.empty()) {
        victim = choose_victim(not_in_l1_candidates);
    } else if (!in_l1_candidates.empty()) {
        victim = choose_victim(in_l1_candidates);
    } else {
        victim = candidates[0];
    }

    DPRINTF(L2CacheReplacement, "[WeightedLRURP] Victim way %p (L%u)\n",
        victim, cacheLevel);
    return victim;
}

std::shared_ptr<ReplacementData>
WeightedLRURP::instantiateEntry()
{
    return std::shared_ptr<ReplacementData>(new WeightedLRURPReplData);
}

} // namespace replacement_policy
} // namespace gem5
