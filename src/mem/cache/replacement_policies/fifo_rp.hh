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
 * Declaration of a cache-level aware First In First Out replacement policy.
 * The victim is chosen using the timestamp, with smart logic for L2 cache.
 */

#ifndef __MEM_CACHE_REPLACEMENT_POLICIES_FIFO_RP_HH__
#define __MEM_CACHE_REPLACEMENT_POLICIES_FIFO_RP_HH__

#include "base/types.hh"
#include "mem/cache/replacement_policies/base.hh"

namespace gem5
{

struct FIFORPParams;
class CacheBlk;  // Forward declaration

namespace replacement_policy
{

class FIFORP : public Base
{
  protected:
    /** FIFO-specific implementation of replacement data. */
    struct FIFORPReplData : ReplacementData
    {
        /** Tick on which the entry was inserted. */
        Tick tickInserted;
        /**
         * Default constructor. Invalidate data.
         */
        FIFORPReplData() : tickInserted(0) {}
    };

    /** Cache level this replacement policy is associated with */
    const unsigned cacheLevel;

  private:
    /**
     * A counter that tracks the number of
     * ticks since being created to avoid a tie
     */
    mutable Tick timeTicks;

  protected:
    // Protected constructor for derived classes
    FIFORP(const BaseReplacementPolicyParams &p, unsigned cache_level);

  public:
    typedef FIFORPParams Params;
    FIFORP(const Params &p);
    ~FIFORP() = default;

    /**
     * Invalidate replacement data to set it as the next probable victim.
     * Reset insertion tick to 0.
     *
     * @param replacement_data Replacement data to be invalidated.
     */
    void invalidate(const std::shared_ptr<ReplacementData>& replacement_data)
                                                                    override;

    /**
     * Touch an entry to update its replacement data.
     * Does not modify the replacement data.
     *
     * @param replacement_data Replacement data to be touched.
     */
    void touch(const std::shared_ptr<ReplacementData>& replacement_data) const
                                                                     override;

    /**
     * Reset replacement data. Used when an entry is inserted.
     * Sets its insertion tick.
     *
     * @param replacement_data Replacement data to be reset.
     */
    void reset(const std::shared_ptr<ReplacementData>& replacement_data) const
                                                                     override;

    /**
     * Find replacement victim with smart logic for L2 Cache.
     * For L2 Cache (cache_level == 2):
     * 1. First priority: Replace invalid lines
     * 2. Second priority: Replace lines that don't exist in L1 Cache
     * 3. Third priority: FIFO replacement
     * For other cache levels: Standard FIFO replacement with invalid prioritization
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
     * Helper function to get cache block from replaceable entry.
     * This function performs the necessary casting to access CacheBlk.
     *
     * @param entry The replaceable entry to cast.
     * @return Pointer to CacheBlk or nullptr if casting fails.
     */
    gem5::CacheBlk* getCacheBlk(ReplaceableEntry* entry) const;
};

} // namespace replacement_policy
} // namespace gem5

#endif // __MEM_CACHE_REPLACEMENT_POLICIES_FIFO_RP_HH__
