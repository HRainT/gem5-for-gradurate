/**
 * Copyright (c) 2024 Xiangshan University
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
 * Implementation of replacement context management for context-aware DPRINTF.
 */

#include "mem/cache/replacement_policies/replacement_context.hh"

#include "base/trace.hh"
#include "debug/CacheRepl.hh"
#include "debug/HWPrefetch.hh"
#include "debug/L2CacheReplacement.hh"
#include "mem/cache/cache_blk.hh"
#include "mem/cache/replacement_policies/replaceable_entry.hh"

namespace gem5
{

namespace replacement_policy
{

// Thread-local storage definitions
thread_local unsigned ReplacementContext::currentCacheLevel = 0;
thread_local bool ReplacementContext::isPrefetchOperation = false;
thread_local bool ReplacementContext::contextActive = false;

void
ReplacementContext::setContext(unsigned cache_level, bool is_prefetch)
{
#if ENABLE_REPLACEMENT_CONTEXT_DEBUG
    currentCacheLevel = cache_level;
    isPrefetchOperation = is_prefetch;
    contextActive = true;
#else
    // Performance mode: do nothing to avoid thread_local overhead
    (void)cache_level;  // Suppress unused parameter warning
    (void)is_prefetch;  // Suppress unused parameter warning
#endif
}

void
ReplacementContext::clearContext()
{
#if ENABLE_REPLACEMENT_CONTEXT_DEBUG
    currentCacheLevel = 0;
    isPrefetchOperation = false;
    contextActive = false;
#else
    // Performance mode: do nothing to avoid thread_local overhead
#endif
}

unsigned
ReplacementContext::getCacheLevel()
{
#if ENABLE_REPLACEMENT_CONTEXT_DEBUG
    return contextActive ? currentCacheLevel : 0;
#else
    // Performance mode: return default value to avoid thread_local access
    return 0;
#endif
}

bool
ReplacementContext::isPrefetch()
{
#if ENABLE_REPLACEMENT_CONTEXT_DEBUG
    return contextActive ? isPrefetchOperation : false;
#else
    // Performance mode: return default value to avoid thread_local access
    return false;
#endif
}

bool
ReplacementContext::isActive()
{
#if ENABLE_REPLACEMENT_CONTEXT_DEBUG
    return contextActive;
#else
    // Performance mode: return false to avoid thread_local access
    return false;
#endif
}


ReplacementContextGuard::ReplacementContextGuard(unsigned cache_level,
    bool is_prefetch)
    : contextWasSet(false)
{
#if ENABLE_REPLACEMENT_CONTEXT_DEBUG
    // Only set context if it's not already active to avoid nested context issues
    if (!ReplacementContext::isActive()) {
        ReplacementContext::setContext(cache_level, is_prefetch);
        contextWasSet = true;
    }
#else
    // Performance mode: do nothing to avoid thread_local overhead
    (void)cache_level;  // Suppress unused parameter warning
    (void)is_prefetch;  // Suppress unused parameter warning
#endif
}

ReplacementContextGuard::~ReplacementContextGuard()
{
#if ENABLE_REPLACEMENT_CONTEXT_DEBUG
    // Only clear context if this guard set it
    if (contextWasSet) {
        ReplacementContext::clearContext();
    }
#else
    // Performance mode: do nothing to avoid thread_local overhead
#endif
}

void
ReplacementContext::contextAwareDPRINTF(const char* policy_name,
    const ReplaceableEntry* victim,
    Tick victim_tick,
    const CacheBlk* victim_blk,
    bool include_upper_cache)
{
#if ENABLE_REPLACEMENT_CONTEXT_DEBUG
    // Get current context
    unsigned cache_level = getCacheLevel();
    bool is_prefetch = isPrefetch();

    // Prepare common victim information
    unsigned victim_set = victim->getSet();
    unsigned victim_way = victim->getWay();
    Addr victim_tag = victim_blk ? victim_blk->getTag() : 0;

    // Context-aware DPRINTF based on cache level and operation type
    if (cache_level == 2 && !is_prefetch) {
        // L2 cache normal operations -> L2CacheReplacement flag
        if (include_upper_cache && victim_blk) {
            DPRINTF(L2CacheReplacement, "[%s-L2] Final L2 victim selected: set=%d way=%d tick=%llu tag=0x%x exists_in_upper=%s\n",
                    policy_name, victim_set, victim_way, victim_tick, victim_tag,
                    victim_blk->existsInUpperCache() ? "true" : "false");
        } else {
            DPRINTF(L2CacheReplacement, "[%s-L2] Final L2 victim: set=%d way=%d tick=%llu tag=0x%x\n",
                    policy_name, victim_set, victim_way, victim_tick, victim_tag);
        }
    } else if (cache_level == 1 && !is_prefetch) {
        // L1 cache operations -> CacheRepl flag
        DPRINTF(CacheRepl, "[%s-L1] Final L1 victim: set=%d way=%d tick=%llu tag=0x%x\n",
                policy_name, victim_set, victim_way, victim_tick, victim_tag);
    } else if (is_prefetch) {
        // Prefetch operations -> HWPrefetch flag
        DPRINTF(HWPrefetch, "[%s-PF] Prefetch victim: set=%d way=%d tick=%llu tag=0x%x\n",
                policy_name, victim_set, victim_way, victim_tick, victim_tag);
    } else {
        // Other operations (L3, unknown level, etc.) -> CacheRepl flag
        DPRINTF(CacheRepl, "[%s] Final victim: set=%d way=%d tick=%llu tag=0x%x\n",
                policy_name, victim_set, victim_way, victim_tick, victim_tag);
    }
#else
    // Performance mode: do nothing to avoid thread_local overhead
    (void)policy_name;      // Suppress unused parameter warning
    (void)victim;           // Suppress unused parameter warning
    (void)victim_tick;      // Suppress unused parameter warning
    (void)victim_blk;       // Suppress unused parameter warning
    (void)include_upper_cache; // Suppress unused parameter warning
#endif
}

} // namespace replacement_policy
} // namespace gem5
