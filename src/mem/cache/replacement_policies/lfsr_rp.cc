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

#include "mem/cache/replacement_policies/lfsr_rp.hh"

#include <cassert>
#include <memory>

#include "base/trace.hh"
#include "debug/L2CacheReplacement.hh"
#include "mem/cache/cache_blk.hh"
#include "params/LFSRRP.hh"

namespace gem5
{

namespace replacement_policy
{

LFSRRP::LFSRRP(const Params &p)
  : Base(p)
{
}

CacheBlk*
LFSRRP::getCacheBlk(ReplaceableEntry* entry) const
{
    // Cast ReplaceableEntry to CacheBlk for address information
    return static_cast<CacheBlk*>(entry);
}

uint8_t
LFSRRP::getLFSRState(uint32_t set) const
{
    auto it = lfsrStates.find(set);
    if (it == lfsrStates.end()) {
        // Initialize LFSR state to 0x1 (0001 in binary) for new sets
        lfsrStates[set] = 0x1;
        return 0x1;
    }
    return it->second;
}

uint8_t
LFSRRP::updateLFSR(uint32_t set) const
{
    uint8_t current_state = getLFSRState(set);

    // Implement 4-bit LFSR: new_value = {old_value[2:0],
    // old_value[3]^old_value[2]}
    // Extract bits: bit 3 (MSB) and bit 2
    uint8_t bit3 = (current_state >> 3) & 0x1;
    uint8_t bit2 = (current_state >> 2) & 0x1;
    uint8_t feedback = bit3 ^ bit2;

    // Shift left by 1 and insert feedback bit at LSB
    uint8_t new_state = ((current_state << 1) & 0xE) | feedback;

    // Store the updated state
    lfsrStates[set] = new_state;

    return new_state;
}

void
LFSRRP::invalidate(const std::shared_ptr<ReplacementData>& replacement_data)
{
    // Unprioritize replacement data victimization
    std::static_pointer_cast<LFSRRPReplData>(
        replacement_data)->valid = false;
}

void
LFSRRP::touch(const std::shared_ptr<ReplacementData>& replacement_data) const
{

}

void
LFSRRP::reset(const std::shared_ptr<ReplacementData>& replacement_data) const
{
    // Unprioritize replacement data victimization
    std::static_pointer_cast<LFSRRPReplData>(
        replacement_data)->valid = true;
}

ReplaceableEntry*
LFSRRP::getVictim(const ReplacementCandidates& candidates) const
{
    // There must be at least one replacement candidate
    assert(candidates.size() > 0);

    // Visit all candidates to search for an invalid entry
    for (const auto& candidate : candidates) {
        bool candidate_valid = std::static_pointer_cast<LFSRRPReplData>(
            candidate->replacementData)->valid;

        if (!candidate_valid) {
            return candidate;
        }
    }

    // If no invalid entry found, use LFSR to select among valid candidates
    // Get the set number from the first candidate
    // (all candidates should be from the same set)
    uint32_t set = candidates[0]->getSet();

    // Update LFSR state for this set
    uint8_t lfsr_value = updateLFSR(set);

    // Use LFSR value to select victim (modulo the number of candidates)
    unsigned victim_idx = lfsr_value % candidates.size();
    ReplaceableEntry* victim = candidates[victim_idx];

    return victim;
}

std::shared_ptr<ReplacementData>
LFSRRP::instantiateEntry()
{
    return std::shared_ptr<ReplacementData>(new LFSRRPReplData());
}

} // namespace replacement_policy
} // namespace gem5
