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

#include "mem/cache/replacement_policies/second_chance_rp.hh"

#include <cassert>

#include "base/trace.hh"
#include "debug/L2CacheReplacement.hh"
#include "mem/cache/cache_blk.hh"
#include "params/SecondChanceRP.hh"

namespace gem5
{

namespace replacement_policy
{

SecondChanceRP::SecondChanceRP(const Params &p)
  : FIFORP(p)
{
}

CacheBlk*
SecondChanceRP::getCacheBlk(ReplaceableEntry* entry) const
{
    // Cast ReplaceableEntry to CacheBlk for address information
    return static_cast<CacheBlk*>(entry);
}

void
SecondChanceRP::useSecondChance(
    const std::shared_ptr<SecondChanceRPReplData>& replacement_data) const
{
    // Reset FIFO data
    FIFORP::reset(replacement_data);

    // Use second chance
    replacement_data->hasSecondChance = false;
}

void
SecondChanceRP::invalidate(
    const std::shared_ptr<ReplacementData>& replacement_data)
{
    FIFORP::invalidate(replacement_data);

    // Do not give a second chance to invalid entries
    std::static_pointer_cast<SecondChanceRPReplData>(
        replacement_data)->hasSecondChance = false;
}

void
SecondChanceRP::touch(
    const std::shared_ptr<ReplacementData>& replacement_data) const
{
    FIFORP::touch(replacement_data);

    // Whenever an entry is touched, it is given a second chance
    std::static_pointer_cast<SecondChanceRPReplData>(
        replacement_data)->hasSecondChance = true;
}

void
SecondChanceRP::reset(
    const std::shared_ptr<ReplacementData>& replacement_data) const
{
    FIFORP::reset(replacement_data);

    // Entries are inserted with a second chance
    std::static_pointer_cast<SecondChanceRPReplData>(
        replacement_data)->hasSecondChance = false;
}

ReplaceableEntry*
SecondChanceRP::getVictim(const ReplacementCandidates& candidates) const
{
    // There must be at least one replacement candidate
    assert(candidates.size() > 0);

    if (cacheLevel != 2) {
        for (const auto& candidate : candidates) {
            std::shared_ptr<SecondChanceRPReplData> candidate_replacement_data =
                std::static_pointer_cast<SecondChanceRPReplData>(
                    candidate->replacementData);

            if ((candidate_replacement_data->tickInserted == Tick(0)) &&
                !candidate_replacement_data->hasSecondChance) {
                return candidate;
            }
        }

        ReplaceableEntry* victim = candidates[0];
        bool search_victim = true;
        while (search_victim) {
            victim = FIFORP::getVictim(candidates);

            std::shared_ptr<SecondChanceRPReplData> victim_replacement_data =
                std::static_pointer_cast<SecondChanceRPReplData>(
                    victim->replacementData);

            if (victim_replacement_data->hasSecondChance) {
                useSecondChance(victim_replacement_data);
            } else {
                search_victim = false;
            }
        }

        // CacheBlk* victim_blk = getCacheBlk(victim);
        return victim;
    }

    // L2 replacement logic

    std::vector<ReplaceableEntry*> invalid_candidates, not_in_l1_candidates, in_l1_candidates;

    for (const auto& candidate : candidates) {
        std::shared_ptr<SecondChanceRPReplData> candidate_replacement_data =
            std::static_pointer_cast<SecondChanceRPReplData>(
                candidate->replacementData);

        if ((candidate_replacement_data->tickInserted == Tick(0)) &&
            !candidate_replacement_data->hasSecondChance) {
            invalid_candidates.push_back(candidate);
            continue;
        }

        CacheBlk* blk = getCacheBlk(candidate);
        if (blk != nullptr) {
            bool exists_in_upper = blk->existsInUpperCache();
            if (!exists_in_upper) {
                not_in_l1_candidates.push_back(candidate);
            } else {
                in_l1_candidates.push_back(candidate);
            }
        } else {
            not_in_l1_candidates.push_back(candidate);
        }
    }

    ReplaceableEntry* victim = nullptr;

    if (!invalid_candidates.empty()) {
        victim = invalid_candidates[0];
    } else if (!not_in_l1_candidates.empty()) {
        victim = not_in_l1_candidates[0];
        bool search_victim = true;
        while (search_victim) {
            // Find FIFO victim among not_in_l1_candidates
            ReplaceableEntry* fifo_victim = not_in_l1_candidates[0];
            uint64_t oldest_tick = std::static_pointer_cast<SecondChanceRPReplData>(
                fifo_victim->replacementData)->tickInserted;

            for (const auto& candidate : not_in_l1_candidates) {
                uint64_t candidate_tick = std::static_pointer_cast<SecondChanceRPReplData>(
                    candidate->replacementData)->tickInserted;
                if (candidate_tick < oldest_tick) {
                    fifo_victim = candidate;
                    oldest_tick = candidate_tick;
                }
            }

            victim = fifo_victim;

            std::shared_ptr<SecondChanceRPReplData> victim_replacement_data =
                std::static_pointer_cast<SecondChanceRPReplData>(
                    victim->replacementData);

            // If victim has a second chance, use it and repeat search
            if (victim_replacement_data->hasSecondChance) {
                useSecondChance(victim_replacement_data);
            } else {
                search_victim = false;
            }
        }
    } else if (!in_l1_candidates.empty()) {
        victim = in_l1_candidates[0];
        bool search_victim = true;
        while (search_victim) {
            // Find FIFO victim among in_l1_candidates
            ReplaceableEntry* fifo_victim = in_l1_candidates[0];
            uint64_t oldest_tick = std::static_pointer_cast<SecondChanceRPReplData>(
                fifo_victim->replacementData)->tickInserted;

            for (const auto& candidate : in_l1_candidates) {
                uint64_t candidate_tick = std::static_pointer_cast<SecondChanceRPReplData>(
                    candidate->replacementData)->tickInserted;
                if (candidate_tick < oldest_tick) {
                    fifo_victim = candidate;
                    oldest_tick = candidate_tick;
                }
            }

            victim = fifo_victim;

            std::shared_ptr<SecondChanceRPReplData> victim_replacement_data =
                std::static_pointer_cast<SecondChanceRPReplData>(
                    victim->replacementData);

            // If victim has a second chance, use it and repeat search
            if (victim_replacement_data->hasSecondChance) {
                useSecondChance(victim_replacement_data);
            } else {
                search_victim = false;
            }
        }
    } else {
        victim = candidates[0];
    }

    assert(victim != nullptr);

    // CacheBlk* victim_blk = getCacheBlk(victim);
    // std::shared_ptr<SecondChanceRPReplData> victim_data =
    //     std::static_pointer_cast<SecondChanceRPReplData>(victim->replacementData);

    return victim;
}

std::shared_ptr<ReplacementData>
SecondChanceRP::instantiateEntry()
{
    return std::shared_ptr<ReplacementData>(new SecondChanceRPReplData());
}

} // namespace replacement_policy
} // namespace gem5
