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

#include "mem/cache/replacement_policies/lru_rp.hh"

#include <cassert>
#include <limits>
#include <memory>
#include <vector>

#include "base/trace.hh"
#include "debug/CacheRepl.hh"
#include "debug/HWPrefetch.hh"
#include "debug/L2CacheReplacement.hh"
#include "mem/cache/cache_blk.hh"

#include "params/LRURP.hh"
#include "sim/cur_tick.hh"

namespace gem5
{

namespace replacement_policy
{

using CacheBlk = gem5::CacheBlk;

LRURP::LRURP(const BaseReplacementPolicyParams &p, unsigned cache_level)
  : Base(p), cacheLevel(cache_level)
{
}

LRURP::LRURP(const Params &p)
  : Base(p), cacheLevel(p.cache_level)
{
}


void
LRURP::invalidate(const std::shared_ptr<ReplacementData>& replacement_data)
{
    // Reset last touch timestamp
    std::static_pointer_cast<LRURPReplData>(
        replacement_data)->lastTouchTick = Tick(0);


}

void
LRURP::touch(const std::shared_ptr<ReplacementData>& replacement_data) const
{
    // Update last touch timestamp
    Tick current_tick = curTick();
    std::static_pointer_cast<LRURPReplData>(
        replacement_data)->lastTouchTick = current_tick;


}

void
LRURP::reset(const std::shared_ptr<ReplacementData>& replacement_data) const
{
    // Set last touch timestamp
    Tick current_tick = curTick();
    std::static_pointer_cast<LRURPReplData>(
        replacement_data)->lastTouchTick = current_tick;


}

CacheBlk*
LRURP::getCacheBlk(ReplaceableEntry* entry) const
{
    // Cast ReplaceableEntry to CacheBlk
    return static_cast<CacheBlk*>(entry);
}

ReplaceableEntry*
LRURP::getVictim(const ReplacementCandidates& candidates) const
{
    // There must be at least one replacement candidate
    assert(candidates.size() > 0);

    // Use the accurate cache_level from Cache configuration
    if (cacheLevel != 2) {
        // Non-L2 replacement logic
        ReplaceableEntry* victim = nullptr;
        Tick victim_tick = std::numeric_limits<Tick>::max();

        for (const auto& candidate : candidates) {
            Tick candidate_tick = std::static_pointer_cast<LRURPReplData>(
                candidate->replacementData)->lastTouchTick;

            // Invalid entries have highest priority
            if (candidate_tick == Tick(0)) {
                return candidate;
            }

            if (victim == nullptr || candidate_tick < victim_tick) {
                victim = candidate;
                victim_tick = candidate_tick;
            }
        }

        return victim;
    }

    // L2 replacement logic
    // L2-Specific Priority System:
    // - Priority 1 (Highest): Invalid entries (lastTouchTick == 0)
    // - Priority 2 (Medium):  Valid entries not in L1 cache
    // - Priority 3 (Lowest):  Valid entries in L1 cache

    ReplaceableEntry* best_not_in_l1 = nullptr;
    Tick best_not_in_l1_tick = std::numeric_limits<Tick>::max();
    ReplaceableEntry* best_in_l1 = nullptr;
    Tick best_in_l1_tick = std::numeric_limits<Tick>::max();

    for (const auto& candidate : candidates) {
        auto repl_data = std::static_pointer_cast<LRURPReplData>(
            candidate->replacementData);
        Tick candidate_tick = repl_data->lastTouchTick;

        if (candidate_tick == Tick(0)) {
            return candidate;
        }

        CacheBlk* blk = getCacheBlk(candidate);
        if (blk == nullptr) {
            if (candidate_tick < best_not_in_l1_tick) {
                best_not_in_l1 = candidate;
                best_not_in_l1_tick = candidate_tick;
            }
            continue;
        }

        if (blk->existsInUpperCache()) {
            if (candidate_tick < best_in_l1_tick) {
                best_in_l1 = candidate;
                best_in_l1_tick = candidate_tick;
            }
        } else {
            if (candidate_tick < best_not_in_l1_tick) {
                best_not_in_l1 = candidate;
                best_not_in_l1_tick = candidate_tick;
            }
        }
    }

    // Select victim based on priority: prefer not in L1 over in L1
    ReplaceableEntry* victim = nullptr;
    if (best_not_in_l1 != nullptr) {
        victim = best_not_in_l1;
    } else if (best_in_l1 != nullptr) {
        victim = best_in_l1;
    }

    assert(victim != nullptr);

    return victim;
}



// ReplaceableEntry*
// LRURP::getVictim(const ReplacementCandidates& candidates) const
// {
//     // There must be at least one replacement candidate
//     assert(candidates.size() > 0);

//     // Visit all candidates to find victim
//     ReplaceableEntry* victim = candidates[0];
//     for (const auto& candidate : candidates) {
//         // Update victim entry if necessary
//         if (std::static_pointer_cast<LRURPReplData>(
//                     candidate->replacementData)->lastTouchTick <
//                 std::static_pointer_cast<LRURPReplData>(
//                     victim->replacementData)->lastTouchTick) {
//             victim = candidate;
//         }
//     }

//     return victim;
// }

std::shared_ptr<ReplacementData>
LRURP::instantiateEntry()
{
    return std::shared_ptr<ReplacementData>(new LRURPReplData());
}

} // namespace replacement_policy
} // namespace gem5
