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

/**
 * @file
 * Declaration of a 4-bit LFSR-based pseudo-random replacement policy.
 * The victim is chosen using a 4-bit Linear Feedback Shift Register (LFSR)
 * that maintains separate state for each cache set.
 */

#ifndef __MEM_CACHE_REPLACEMENT_POLICIES_LFSR_RP_HH__
#define __MEM_CACHE_REPLACEMENT_POLICIES_LFSR_RP_HH__

#include <unordered_map>

#include "mem/cache/replacement_policies/base.hh"

namespace gem5
{

struct LFSRRPParams;
class CacheBlk;  // Forward declaration

namespace replacement_policy
{

class LFSRRP : public Base
{
  protected:
    /** LFSR-specific implementation of replacement data. */
    struct LFSRRPReplData : ReplacementData
    {
        /**
         * Flag informing if the replacement data is valid or not.
         * Invalid entries are prioritized to be evicted.
         */
        bool valid;

        /**
         * Default constructor. Invalidate data.
         */
        LFSRRPReplData() : valid(false) {}
    };

    /**
     * Per-set LFSR state storage.
     * Each cache set maintains its own 4-bit LFSR state.
     * Key: set number, Value: 4-bit LFSR state
     */
    mutable std::unordered_map<uint32_t, uint8_t> lfsrStates;

    /**
     * Update the LFSR state for a given set.
     * Implements the 4-bit LFSR algorithm:
     * new_value = {old_value[2:0], old_value[3]^old_value[2]}
     *
     * @param set The cache set number.
     * @return The updated LFSR value.
     */
    uint8_t updateLFSR(uint32_t set) const;

    /**
     * Get the current LFSR state for a given set.
     * If the set doesn't exist, initialize it with 0x1.
     *
     * @param set The cache set number.
     * @return The current LFSR value for the set.
     */
    uint8_t getLFSRState(uint32_t set) const;

  public:
    typedef LFSRRPParams Params;
    LFSRRP(const Params &p);
    ~LFSRRP() = default;

    /**
     * Invalidate replacement data to set it as the next probable victim.
     * Prioritize replacement data for victimization.
     *
     * @param replacement_data Replacement data to be invalidated.
     */
    void invalidate(const std::shared_ptr<ReplacementData>& replacement_data)
                                                                    override;

    /**
     * Touch an entry to update its replacement data.
     * Updates the LFSR state for the corresponding cache set.
     *
     * @param replacement_data Replacement data to be touched.
     */
    void touch(const std::shared_ptr<ReplacementData>& replacement_data) const
                                                                     override;

    /**
     * Reset replacement data. Used when an entry is inserted.
     * Unprioritize replacement data for victimization.
     *
     * @param replacement_data Replacement data to be reset.
     */
    void reset(const std::shared_ptr<ReplacementData>& replacement_data) const
                                                                     override;

    /**
     * Find replacement victim using LFSR-based pseudo-random selection.
     * Uses the current LFSR value to select among valid candidates.
     *
     * @param candidates Replacement candidates, selected by indexing policy.
     * @return Replacement entry to be replaced.
     */
    ReplaceableEntry* getVictim(const ReplacementCandidates& candidates) const
                                                                     override;

    /**
     * Instantiate a replacement data entry.
     *
     * @return A shared pointer to the new replacement data.
     */
    std::shared_ptr<ReplacementData> instantiateEntry() override;

  protected:
    /**
     * Get CacheBlk from ReplaceableEntry for address information.
     *
     * @param entry The replaceable entry to cast.
     * @return Pointer to CacheBlk.
     */
    CacheBlk* getCacheBlk(ReplaceableEntry* entry) const;
};

} // namespace replacement_policy
} // namespace gem5

#endif // __MEM_CACHE_REPLACEMENT_POLICIES_LFSR_RP_HH__
