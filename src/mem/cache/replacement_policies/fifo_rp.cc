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

#include "mem/cache/replacement_policies/fifo_rp.hh"

#include <cassert>
#include <memory>
#include <vector>

#include "base/trace.hh"
#include "debug/CacheRepl.hh"
#include "debug/HWPrefetch.hh"
#include "debug/L2CacheReplacement.hh"
#include "mem/cache/cache_blk.hh"
#include "mem/cache/replacement_policies/replacement_context.hh"
#include "params/FIFORP.hh"
#include "sim/cur_tick.hh"

namespace gem5
{

namespace replacement_policy
{

using CacheBlk = gem5::CacheBlk;  // Type alias for convenience

FIFORP::FIFORP(const BaseReplacementPolicyParams &p, unsigned cache_level)
  : Base(p), cacheLevel(cache_level), timeTicks(0)
{
}

FIFORP::FIFORP(const Params &p)
  : FIFORP(p, p.cache_level)
{
}

void
FIFORP::invalidate(const std::shared_ptr<ReplacementData>& replacement_data)
{
    // Reset insertion tick
    std::static_pointer_cast<FIFORPReplData>(
        replacement_data)->tickInserted = ++timeTicks;
}

void
FIFORP::touch(const std::shared_ptr<ReplacementData>& replacement_data) const
{
    // A touch does not modify the insertion tick
}

void
FIFORP::reset(const std::shared_ptr<ReplacementData>& replacement_data) const
{
    // Set insertion tick
    std::static_pointer_cast<FIFORPReplData>(
        replacement_data)->tickInserted = ++timeTicks;
}

CacheBlk*
FIFORP::getCacheBlk(ReplaceableEntry* entry) const
{
    // Cast ReplaceableEntry to CacheBlk
    return static_cast<CacheBlk*>(entry);
}

ReplaceableEntry*
FIFORP::getVictim(const ReplacementCandidates& candidates) const
{
    // There must be at least one replacement candidate
    assert(candidates.size() > 0);

    if (cacheLevel != 2) {
        ReplaceableEntry* victim = candidates[0];

        for (const auto& candidate : candidates) {
            uint64_t candidate_tick = std::static_pointer_cast<FIFORPReplData>(
                candidate->replacementData)->tickInserted;

            if (candidate_tick == 0) {
                victim = candidate;
                break;
            }
        }

        if (std::static_pointer_cast<FIFORPReplData>(
                    victim->replacementData)->tickInserted != 0) {
            uint64_t victim_tick = std::static_pointer_cast<FIFORPReplData>(
                victim->replacementData)->tickInserted;

            for (const auto& candidate : candidates) {
                uint64_t candidate_tick = std::static_pointer_cast<FIFORPReplData>(
                    candidate->replacementData)->tickInserted;

                if (candidate_tick < victim_tick) {
                    victim = candidate;
                    victim_tick = candidate_tick;
                }
            }
        }

        return victim;
    }

    // L2 replacement logic

    std::vector<ReplaceableEntry*> invalid_candidates, not_in_l1_candidates, in_l1_candidates;

    for (const auto& candidate : candidates) {
        uint64_t candidate_tick = std::static_pointer_cast<FIFORPReplData>(
            candidate->replacementData)->tickInserted;
        CacheBlk* blk = getCacheBlk(candidate);

        if (candidate_tick == 0) {
            invalid_candidates.push_back(candidate);
            continue;
        }

        if (blk != nullptr) {
            bool exists_in_upper = blk->existsInUpperCache();
            if (!exists_in_upper) {
                not_in_l1_candidates.push_back(candidate);
            } else {
                in_l1_candidates.push_back(candidate);
            }
        } else {
            return candidate;
        }
    }

    ReplaceableEntry* victim = nullptr;

    if (!invalid_candidates.empty()) {
        victim = invalid_candidates[0];
        uint64_t victim_tick = std::static_pointer_cast<FIFORPReplData>(
            victim->replacementData)->tickInserted;

        for (const auto& candidate : invalid_candidates) {
            uint64_t candidate_tick = std::static_pointer_cast<FIFORPReplData>(
                candidate->replacementData)->tickInserted;
            if (candidate_tick < victim_tick) {
                victim = candidate;
                victim_tick = candidate_tick;
            }
        }
    } else if (!not_in_l1_candidates.empty()) {
        victim = not_in_l1_candidates[0];
        uint64_t victim_tick = std::static_pointer_cast<FIFORPReplData>(
            victim->replacementData)->tickInserted;

        for (const auto& candidate : not_in_l1_candidates) {
            uint64_t candidate_tick = std::static_pointer_cast<FIFORPReplData>(
                candidate->replacementData)->tickInserted;
            if (candidate_tick < victim_tick) {
                victim = candidate;
                victim_tick = candidate_tick;
            }
        }
    } else if (!in_l1_candidates.empty()) {
        victim = in_l1_candidates[0];
        uint64_t victim_tick = std::static_pointer_cast<FIFORPReplData>(
            victim->replacementData)->tickInserted;

        for (const auto& candidate : in_l1_candidates) {
            uint64_t candidate_tick = std::static_pointer_cast<FIFORPReplData>(
                candidate->replacementData)->tickInserted;
            if (candidate_tick < victim_tick) {
                victim = candidate;
                victim_tick = candidate_tick;
            }
        }
    } else {
        victim = candidates[0];
    }

    assert(victim != nullptr);

    return victim;
}

std::shared_ptr<ReplacementData>
FIFORP::instantiateEntry()
{
    return std::shared_ptr<ReplacementData>(new FIFORPReplData());
}

} // namespace replacement_policy
} // namespace gem5
