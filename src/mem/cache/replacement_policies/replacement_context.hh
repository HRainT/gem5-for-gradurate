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
 * Declaration of replacement context management for context-aware DPRINTF.
 * This provides a thread-local context mechanism to track cache level and
 * operation type for replacement policy debugging.
 */

#ifndef __MEM_CACHE_REPLACEMENT_POLICIES_REPLACEMENT_CONTEXT_HH__
#define __MEM_CACHE_REPLACEMENT_POLICIES_REPLACEMENT_CONTEXT_HH__

// Configuration macro to enable/disable replacement context debugging
// Set to 0 to disable all replacement context functionality for better performance
// Set to 1 to enable full replacement context functionality
#ifndef ENABLE_REPLACEMENT_CONTEXT_DEBUG
#define ENABLE_REPLACEMENT_CONTEXT_DEBUG 0
#endif

#include "base/types.hh"

namespace gem5
{

// Forward declarations
class CacheBlk;
class ReplaceableEntry;

namespace replacement_policy
{

/**
 * Thread-local context manager for replacement policy operations.
 * This class provides a mechanism to track the context of replacement
 * operations, including cache level and whether the operation is a
 * prefetch operation. This enables context-aware debugging output.
 */
class ReplacementContext
{
  private:
    /** Thread-local cache level (1=L1, 2=L2, 3=L3, etc.) */
    static thread_local unsigned currentCacheLevel;

    /** Thread-local flag indicating if current operation is prefetch */
    static thread_local bool isPrefetchOperation;

    /** Thread-local flag indicating if context is currently set */
    static thread_local bool contextActive;

  public:
    /**
     * Set the replacement context for current thread.
     *
     * @param cache_level The cache level (1=L1, 2=L2, 3=L3, etc.)
     * @param is_prefetch True if this is a prefetch operation
     */
    static void setContext(unsigned cache_level, bool is_prefetch);

    /**
     * Clear the replacement context for current thread.
     * This should be called after replacement operations complete.
     */
    static void clearContext();

    /**
     * Get the current cache level.
     *
     * @return Current cache level, or 0 if no context is set
     */
    static unsigned getCacheLevel();

    /**
     * Check if current operation is a prefetch operation.
     *
     * @return True if current operation is prefetch, false otherwise
     */
    static bool isPrefetch();

    /**
     * Check if context is currently active.
     *
     * @return True if context is set, false otherwise
     */
    static bool isActive();

    /**
     * Context-aware DPRINTF for replacement policy victim selection.
     * This function automatically selects the appropriate debug flag based on
     * the current context (cache level and operation type) and outputs
     * formatted debug information.
     *
     * @param policy_name The replacement policy name prefix (e.g., "LRU-NEW", "LRU-OLD")
     * @param victim Pointer to the victim entry
     * @param victim_tick The last touch tick of the victim
     * @param victim_blk Pointer to the cache block (optional, can be nullptr)
     * @param include_upper_cache Whether to include upper cache existence info
     */
    static void contextAwareDPRINTF(const char* policy_name,
                                   const ReplaceableEntry* victim,
                                   Tick victim_tick,
                                   const CacheBlk* victim_blk = nullptr,
                                   bool include_upper_cache = false);
};

/**
 * RAII helper class for automatic context management.
 * This ensures proper context setup and cleanup even in case of exceptions.
 */
class ReplacementContextGuard
{
  private:
    /** Flag to track if this guard set the context */
    bool contextWasSet;

  public:
    /**
     * Constructor that sets the replacement context.
     *
     * @param cache_level The cache level (1=L1, 2=L2, 3=L3, etc.)
     * @param is_prefetch True if this is a prefetch operation
     */
    ReplacementContextGuard(unsigned cache_level, bool is_prefetch);

    /**
     * Destructor that automatically clears the context.
     */
    ~ReplacementContextGuard();

    // Disable copy and assignment to prevent misuse
    ReplacementContextGuard(const ReplacementContextGuard&) = delete;
    ReplacementContextGuard& operator=(const ReplacementContextGuard&) = delete;
};

} // namespace replacement_policy
} // namespace gem5

#endif // __MEM_CACHE_REPLACEMENT_POLICIES_REPLACEMENT_CONTEXT_HH__
