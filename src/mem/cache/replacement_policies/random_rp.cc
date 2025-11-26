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

#include "mem/cache/replacement_policies/random_rp.hh"

#include <cassert>
#include <memory>
#include <vector>

#include "base/random.hh"
#include "base/trace.hh"
#include "debug/L2CacheReplacement.hh"
#include "mem/cache/cache_blk.hh"
#include "params/RandomRP.hh"

namespace gem5
{

namespace replacement_policy
{

using CacheBlk = gem5::CacheBlk;  // Type alias for convenience

RandomRP::RandomRP(const BaseReplacementPolicyParams &p, unsigned cache_level)
  : Base(p), cacheLevel(cache_level)
{
}

RandomRP::RandomRP(const Params &p)
  : RandomRP(p, p.cache_level)
{
}

void
RandomRP::invalidate(const std::shared_ptr<ReplacementData>& replacement_data)
{
    // Unprioritize replacement data victimization
    std::static_pointer_cast<RandomRPReplData>(
        replacement_data)->valid = false;
}

void
RandomRP::touch(const std::shared_ptr<ReplacementData>& replacement_data) const
{
}

void
RandomRP::reset(const std::shared_ptr<ReplacementData>& replacement_data) const
{
    // Unprioritize replacement data victimization
    std::static_pointer_cast<RandomRPReplData>(
        replacement_data)->valid = true;
}

CacheBlk*
RandomRP::getCacheBlk(ReplaceableEntry* entry) const
{
    // Cast ReplaceableEntry to CacheBlk
    return static_cast<CacheBlk*>(entry);
}

ReplaceableEntry*
RandomRP::getVictim(const ReplacementCandidates& candidates) const
{
    // There must be at least one replacement candidate
    assert(candidates.size() > 0);

    // Use the accurate cache_level from Cache configuration
    if (cacheLevel != 2) {
        // Standard Random logic with invalid prioritization
        // Visit all candidates to search for an invalid entry
        for (const auto& candidate : candidates) {
            if (!std::static_pointer_cast<RandomRPReplData>(
                    candidate->replacementData)->valid) {
                return candidate;
            }
        }

        unsigned random_idx = random_mt.random<unsigned>(0,
            candidates.size() - 1);
        ReplaceableEntry* victim = candidates[random_idx];
        DPRINTF(L2CacheReplacement, "[RandomRP] Victim way %p (L%u)\n", victim,
            cacheLevel);
        return victim;
    }

    // L2 replacement logic (cacheLevel == 2)

    std::vector<ReplaceableEntry*> invalid_candidates;
    std::vector<ReplaceableEntry*> not_in_l1_candidates;
    std::vector<ReplaceableEntry*> in_l1_candidates;

    for (const auto& candidate : candidates) {
        bool candidate_valid = std::static_pointer_cast<RandomRPReplData>(
            candidate->replacementData)->valid;
        CacheBlk* blk = getCacheBlk(candidate);

        if (!candidate_valid) {
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

    auto pick_random = [&](const std::vector<ReplaceableEntry*>& list) {
        unsigned idx = random_mt.random<unsigned>(0, list.size() - 1);
        return list[idx];
    };

    ReplaceableEntry* victim = nullptr;
    if (!invalid_candidates.empty()) {
        victim = pick_random(invalid_candidates);
    } else if (!not_in_l1_candidates.empty()) {
        victim = pick_random(not_in_l1_candidates);
    } else if (!in_l1_candidates.empty()) {
        victim = pick_random(in_l1_candidates);
    } else {
        victim = candidates[0];
    }

    DPRINTF(L2CacheReplacement, "[RandomRP] Victim way %p (L%u)\n", victim,
        cacheLevel);
    return victim;
}

std::shared_ptr<ReplacementData>
RandomRP::instantiateEntry()
{
    return std::shared_ptr<ReplacementData>(new RandomRPReplData());
}

} // namespace replacement_policy
} // namespace gem5
