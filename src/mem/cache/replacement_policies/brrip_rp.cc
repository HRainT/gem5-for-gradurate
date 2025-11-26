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

#include "mem/cache/replacement_policies/brrip_rp.hh"

#include <cassert>
#include <memory>
#include <vector>

#include "base/logging.hh"
#include "base/random.hh"
#include "base/trace.hh"
#include "debug/L2CacheReplacement.hh"
#include "mem/cache/cache_blk.hh"
#include "params/BRRIPRP.hh"

namespace gem5
{

namespace replacement_policy
{

using CacheBlk = gem5::CacheBlk;  // Type alias for convenience

BRRIPRP::BRRIPRP(const BaseReplacementPolicyParams &p, unsigned cache_level,
                 unsigned num_rrpv_bits, bool hit_priority, unsigned bimodal_tp)
  : Base(p), numRRPVBits(num_rrpv_bits), hitPriority(hit_priority),
    btp(bimodal_tp), cacheLevel(cache_level)
{
    fatal_if(numRRPVBits <= 0, "There should be at least one bit per RRPV.\n");
}

BRRIPRP::BRRIPRP(const Params &p)
  : BRRIPRP(p, p.cache_level, p.num_bits, p.hit_priority, p.btp)
{
}

void
BRRIPRP::invalidate(const std::shared_ptr<ReplacementData>& replacement_data)
{
    std::shared_ptr<BRRIPRPReplData> casted_replacement_data =
        std::static_pointer_cast<BRRIPRPReplData>(replacement_data);

    // Invalidate entry
    casted_replacement_data->valid = false;
}

void
BRRIPRP::touch(const std::shared_ptr<ReplacementData>& replacement_data) const
{
    std::shared_ptr<BRRIPRPReplData> casted_replacement_data =
        std::static_pointer_cast<BRRIPRPReplData>(replacement_data);

    int old_rrpv = casted_replacement_data->rrpv;

    // Update RRPV if not 0 yet
    // Every hit in HP mode makes the entry the last to be evicted, while
    // in FP mode a hit makes the entry less likely to be evicted
    if (hitPriority) {
        casted_replacement_data->rrpv.reset();
    } else {
        casted_replacement_data->rrpv--;
    }
}

void
BRRIPRP::reset(const std::shared_ptr<ReplacementData>& replacement_data) const
{
    std::shared_ptr<BRRIPRPReplData> casted_replacement_data =
        std::static_pointer_cast<BRRIPRPReplData>(replacement_data);

    // Reset RRPV
    // Replacement data is inserted as "long re-reference" if lower than btp,
    // "distant re-reference" otherwise
    casted_replacement_data->rrpv.saturate();
    unsigned random_val = random_mt.random<unsigned>(1, 100);
    if (random_val <= btp) {
        casted_replacement_data->rrpv--;
    }

    // Mark entry as ready to be used
    casted_replacement_data->valid = true;
}

CacheBlk*
BRRIPRP::getCacheBlk(ReplaceableEntry* entry) const
{
    // Cast ReplaceableEntry to CacheBlk
    return static_cast<CacheBlk*>(entry);
}

ReplaceableEntry*
BRRIPRP::getVictim(const ReplacementCandidates& candidates) const
{
    // There must be at least one replacement candidate
    assert(candidates.size() > 0);

    // Use the accurate cache_level from Cache configuration
    if (cacheLevel != 2) {
        ReplaceableEntry* victim = candidates[0];
        int victim_rrpv = std::static_pointer_cast<BRRIPRPReplData>(
                                victim->replacementData)->rrpv;

        for (const auto& candidate : candidates) {
            auto candidate_data = std::static_pointer_cast<BRRIPRPReplData>(
                candidate->replacementData);
            if (!candidate_data->valid) {
                DPRINTF(L2CacheReplacement, "[BRRIPRP] Victim invalid %p (L%u)\n",
                    candidate, cacheLevel);
                return candidate;
            }
        }

        for (const auto& candidate : candidates) {
            auto candidate_data = std::static_pointer_cast<BRRIPRPReplData>(
                candidate->replacementData);
            if (candidate_data->rrpv > victim_rrpv) {
                victim = candidate;
                victim_rrpv = candidate_data->rrpv;
            }
        }

        int diff = std::static_pointer_cast<BRRIPRPReplData>(
            victim->replacementData)->rrpv.saturate();
        if (diff > 0) {
            for (const auto& candidate : candidates) {
                std::static_pointer_cast<BRRIPRPReplData>(
                    candidate->replacementData)->rrpv += diff;
            }
        }

        DPRINTF(L2CacheReplacement, "[BRRIPRP] Victim way %p (L%u)\n", victim,
            cacheLevel);
        return victim;
    }

    // L2 replacement logic (cacheLevel == 2)

    std::vector<ReplaceableEntry*> invalid_candidates;
    std::vector<ReplaceableEntry*> not_in_l1_candidates;
    std::vector<ReplaceableEntry*> in_l1_candidates;

    for (const auto& candidate : candidates) {
        auto candidate_data = std::static_pointer_cast<BRRIPRPReplData>(
            candidate->replacementData);
        CacheBlk* blk = getCacheBlk(candidate);

        if (!candidate_data->valid) {
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

    auto choose_brrip = [](const std::vector<ReplaceableEntry*>& list) {
        ReplaceableEntry* best = list[0];
        int best_rrpv = std::static_pointer_cast<BRRIPRPReplData>(
            best->replacementData)->rrpv;
        for (const auto& candidate : list) {
            int candidate_rrpv = std::static_pointer_cast<BRRIPRPReplData>(
                candidate->replacementData)->rrpv;
            if (candidate_rrpv > best_rrpv) {
                best = candidate;
                best_rrpv = candidate_rrpv;
            }
        }
        return best;
    };

    ReplaceableEntry* victim = nullptr;
    if (!invalid_candidates.empty()) {
        victim = choose_brrip(invalid_candidates);
    } else if (!not_in_l1_candidates.empty()) {
        victim = choose_brrip(not_in_l1_candidates);
    } else if (!in_l1_candidates.empty()) {
        victim = choose_brrip(in_l1_candidates);
    } else {
        victim = candidates[0];
    }

    int diff = std::static_pointer_cast<BRRIPRPReplData>(
        victim->replacementData)->rrpv.saturate();
    if (diff > 0) {
        for (const auto& candidate : candidates) {
            std::static_pointer_cast<BRRIPRPReplData>(
                candidate->replacementData)->rrpv += diff;
        }
    }

    DPRINTF(L2CacheReplacement, "[BRRIPRP] Victim way %p (L%u)\n", victim,
        cacheLevel);

    return victim;
}

std::shared_ptr<ReplacementData>
BRRIPRP::instantiateEntry()
{
    return std::shared_ptr<ReplacementData>(new BRRIPRPReplData(numRRPVBits));
}

} // namespace replacement_policy
} // namespace gem5
