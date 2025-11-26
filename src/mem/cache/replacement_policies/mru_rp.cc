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

#include "mem/cache/replacement_policies/mru_rp.hh"

#include <cassert>
#include <memory>
#include <vector>

#include "base/trace.hh"
#include "debug/L2CacheReplacement.hh"
#include "mem/cache/cache_blk.hh"
#include "params/MRURP.hh"
#include "sim/cur_tick.hh"

namespace gem5
{

namespace replacement_policy
{

using CacheBlk = gem5::CacheBlk;  // Type alias for convenience

MRURP::MRURP(const BaseReplacementPolicyParams &p, unsigned cache_level)
  : Base(p), cacheLevel(cache_level)
{
}

MRURP::MRURP(const Params &p)
  : MRURP(p, p.cache_level)
{
}

void
MRURP::invalidate(const std::shared_ptr<ReplacementData>& replacement_data)
{
    // Reset last touch timestamp
    std::static_pointer_cast<MRURPReplData>(
        replacement_data)->lastTouchTick = Tick(0);
}

void
MRURP::touch(const std::shared_ptr<ReplacementData>& replacement_data) const
{
    // Update last touch timestamp
    Tick current_tick = curTick();
    std::static_pointer_cast<MRURPReplData>(
        replacement_data)->lastTouchTick = current_tick;
}

void
MRURP::reset(const std::shared_ptr<ReplacementData>& replacement_data) const
{
    // Set last touch timestamp
    Tick current_tick = curTick();
    std::static_pointer_cast<MRURPReplData>(
        replacement_data)->lastTouchTick = current_tick;
}

CacheBlk*
MRURP::getCacheBlk(ReplaceableEntry* entry) const
{
    // Cast ReplaceableEntry to CacheBlk
    return static_cast<CacheBlk*>(entry);
}

ReplaceableEntry*
MRURP::getVictim(const ReplacementCandidates& candidates) const
{
    // There must be at least one replacement candidate
    assert(candidates.size() > 0);

    // Use the accurate cache_level from Cache configuration
    if (cacheLevel != 2) {
        // Standard MRU logic with invalid prioritization
        ReplaceableEntry* victim = candidates[0];

        // Visit all candidates to search for an invalid entry
        for (const auto& candidate : candidates) {
            auto candidate_data = std::static_pointer_cast<MRURPReplData>(
                candidate->replacementData);
            if (candidate_data->lastTouchTick == Tick(0)) {
                victim = candidate;
                break;
            }
        }

        auto victim_data = std::static_pointer_cast<MRURPReplData>(
            victim->replacementData);

        // If no invalid entry found, use MRU
        if (victim_data->lastTouchTick != Tick(0)) {
            for (const auto& candidate : candidates) {
                auto candidate_data = std::static_pointer_cast<MRURPReplData>(
                    candidate->replacementData);
                if (candidate_data->lastTouchTick > victim_data->lastTouchTick) {
                    victim = candidate;
                    victim_data = candidate_data;
                }
            }
        }

        DPRINTF(L2CacheReplacement, "[MRURP] Victim way %p (L%u)\n", victim,
            cacheLevel);
        return victim;
    }

    // L2 replacement logic (cacheLevel == 2)

    std::vector<ReplaceableEntry*> invalid_candidates;
    std::vector<ReplaceableEntry*> not_in_l1_candidates;
    std::vector<ReplaceableEntry*> in_l1_candidates;

    // Categorize candidates based on priority
    for (const auto& candidate : candidates) {
        auto candidate_data = std::static_pointer_cast<MRURPReplData>(
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

    auto choose_mru = [](const std::vector<ReplaceableEntry*>& list) {
        ReplaceableEntry* best = list[0];
        auto best_data = std::static_pointer_cast<MRURPReplData>(
            best->replacementData);
        for (const auto& candidate : list) {
            auto data = std::static_pointer_cast<MRURPReplData>(
                candidate->replacementData);
            if (data->lastTouchTick > best_data->lastTouchTick) {
                best = candidate;
                best_data = data;
            }
        }
        return best;
    };

    ReplaceableEntry* victim = nullptr;
    if (!invalid_candidates.empty()) {
        victim = choose_mru(invalid_candidates);
    } else if (!not_in_l1_candidates.empty()) {
        victim = choose_mru(not_in_l1_candidates);
    } else if (!in_l1_candidates.empty()) {
        victim = choose_mru(in_l1_candidates);
    } else {
        victim = candidates[0];
    }

    DPRINTF(L2CacheReplacement, "[MRURP] Victim way %p (L%u)\n", victim,
        cacheLevel);
    return victim;
}

std::shared_ptr<ReplacementData>
MRURP::instantiateEntry()
{
    return std::shared_ptr<ReplacementData>(new MRURPReplData());
}

} // namespace replacement_policy
} // namespace gem5
