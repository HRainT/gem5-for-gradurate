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
 * Definitions of a Tree-PLRU replacement policy, along with some helper
 * tree indexing functions, which map an index to the tree 2D-array.
 */

#include "mem/cache/replacement_policies/tree_plru_rp.hh"

#include <cassert>
#include <cmath>
#include <vector>

#include "base/intmath.hh"
#include "base/logging.hh"
#include "base/trace.hh"
#include "debug/CacheRepl.hh"
#include "debug/HWPrefetch.hh"
#include "debug/L2CacheReplacement.hh"
#include "mem/cache/cache_blk.hh"
#include "mem/cache/replacement_policies/replacement_context.hh"
#include "params/TreePLRURP.hh"

namespace gem5
{

namespace replacement_policy
{

using CacheBlk = gem5::CacheBlk;  // Type alias for convenience

/**
 * Get the index of the parent of the given indexed subtree.
 *
 * @param Index of the queried tree.
 * @return The index of the parent tree.
 */
static uint64_t
parentIndex(const uint64_t index)
{
    return std::floor((index-1)/2);
}

/**
 * Get index of the subtree on the left of the given indexed tree.
 *
 * @param index The index of the queried tree.
 * @return The index of the subtree to the left of the queried tree.
 */
static uint64_t
leftSubtreeIndex(const uint64_t index)
{
    return 2*index + 1;
}

/**
 * Get index of the subtree on the right of the given indexed tree.
 *
 * @param index The index of the queried tree.
 * @return The index of the subtree to the right of the queried tree.
 */
static uint64_t
rightSubtreeIndex(const uint64_t index)
{
    return 2*index + 2;
}

/**
 * Find out if the subtree at index corresponds to the right or left subtree
 * of its parent tree.
 *
 * @param index The index of the subtree.
 * @return True if it is a right subtree, false otherwise.
 */
static bool
isRightSubtree(const uint64_t index)
{
    return index%2 == 0;
}

TreePLRURP::TreePLRURPReplData::TreePLRURPReplData(
    const uint64_t index, std::shared_ptr<PLRUTree> tree)
  : index(index), tree(tree)
{
}

TreePLRURP::TreePLRURP(const BaseReplacementPolicyParams &p, unsigned cache_level, uint64_t num_leaves)
  : Base(p), numLeaves(num_leaves), count(0), treeInstance(nullptr), cacheLevel(cache_level)
{
    fatal_if(numLeaves < 1,
        "numLeaves should never be 0");
}

TreePLRURP::TreePLRURP(const Params &p)
  : TreePLRURP(p, p.cache_level, p.num_leaves)
{
}

void
TreePLRURP::invalidate(const std::shared_ptr<ReplacementData>& replacement_data)
{
    // Cast replacement data
    std::shared_ptr<TreePLRURPReplData> treePLRU_replacement_data =
        std::static_pointer_cast<TreePLRURPReplData>(replacement_data);
    PLRUTree* tree = treePLRU_replacement_data->tree.get();

    // Index of the tree entry we are currently checking
    // Make this entry the new LRU entry
    uint64_t tree_index = treePLRU_replacement_data->index;

    // Parse and update tree to make it point to the new LRU
    do {
        // Store whether we are coming from a left or right node
        const bool right = isRightSubtree(tree_index);

        // Go to the parent tree node
        tree_index = parentIndex(tree_index);

        // Update parent node to make it point to the node we just came from
        tree->at(tree_index) = right;
    } while (tree_index != 0);
}

void
TreePLRURP::touch(const std::shared_ptr<ReplacementData>& replacement_data)
const
{
    // Cast replacement data
    std::shared_ptr<TreePLRURPReplData> treePLRU_replacement_data =
        std::static_pointer_cast<TreePLRURPReplData>(replacement_data);
    PLRUTree* tree = treePLRU_replacement_data->tree.get();

    // Index of the tree entry we are currently checking
    // Make this entry the MRU entry
    uint64_t tree_index = treePLRU_replacement_data->index;

    // Parse and update tree to make every bit point away from the new MRU
    do {
        // Store whether we are coming from a left or right node
        const bool right = isRightSubtree(tree_index);

        // Go to the parent tree node
        tree_index = parentIndex(tree_index);

        // Update node to not point to the touched leaf
        tree->at(tree_index) = !right;
    } while (tree_index != 0);
}

void
TreePLRURP::reset(const std::shared_ptr<ReplacementData>& replacement_data)
const
{
    // A reset has the same functionality of a touch
    touch(replacement_data);
}

CacheBlk*
TreePLRURP::getCacheBlk(ReplaceableEntry* entry) const
{
    // Cast ReplaceableEntry to CacheBlk
    return static_cast<CacheBlk*>(entry);
}

ReplaceableEntry*
TreePLRURP::getVictim(const ReplacementCandidates& candidates) const
{
    // There must be at least one replacement candidate
    assert(candidates.size() > 0);

    // Use the accurate cache_level from Cache configuration
    if (cacheLevel != 2) {
        // Standard TreePLRU logic
        // Get tree
        const PLRUTree* tree = std::static_pointer_cast<TreePLRURPReplData>(
                candidates[0]->replacementData)->tree.get();

        // Index of the tree entry we are currently checking. Start with root.
        uint64_t tree_index = 0;

        // Parse tree
        while (tree_index < tree->size()) {
            // Go to the next tree entry
            if (tree->at(tree_index)) {
                tree_index = rightSubtreeIndex(tree_index);
            } else {
                tree_index = leftSubtreeIndex(tree_index);
            }
        }

        // The tree index is currently at the leaf of the victim displaced by the
        // number of non-leaf nodes
        ReplaceableEntry* victim = candidates.at(tree_index - (numLeaves - 1));

        return victim;
    }

    // L2 replacement logic (cacheLevel == 2)

    std::vector<ReplaceableEntry*> invalid_candidates, not_in_l1_candidates, in_l1_candidates;

    // Categorize candidates based on priority
    for (const auto& candidate : candidates) {
        CacheBlk* blk = getCacheBlk(candidate);

        // Check if entry is invalid first (highest priority)
        // For TreePLRU, we don't have a simple valid flag, so we check if the block is valid
        if (blk && !blk->isValid()) {
            invalid_candidates.push_back(candidate);
            continue;
        }

        // Get cache block to check upper cache state
        if (blk != nullptr) {
            // Check if block exists in any upper level cache (L1)
            bool exists_in_upper = blk->existsInUpperCache();
            if (!exists_in_upper) {
                not_in_l1_candidates.push_back(candidate);
            } else {
                in_l1_candidates.push_back(candidate);
            }
        } else {
            // If we can't get CacheBlk, return the candidate
            return candidate;
        }
    }

    // Select victim based on priority, using TreePLRU within each category
    ReplaceableEntry* victim = nullptr;

    if (!invalid_candidates.empty()) {
        // Highest priority: invalid entries, use TreePLRU among them
        victim = getTreePLRUVictim(invalid_candidates);
    } else if (!not_in_l1_candidates.empty()) {
        // Second priority: entries not in L1, use TreePLRU among them
        victim = getTreePLRUVictim(not_in_l1_candidates);
    } else if (!in_l1_candidates.empty()) {
        // Lowest priority: entries in L1 (last resort), use TreePLRU among them
        victim = getTreePLRUVictim(in_l1_candidates);
    } else {
        // Fallback: should not happen, but select first candidate
        victim = candidates[0];
    }

    assert(victim != nullptr);
    CacheBlk* victim_blk = getCacheBlk(victim);

    return victim;
}

ReplaceableEntry*
TreePLRURP::getTreePLRUVictim(const std::vector<ReplaceableEntry*>& candidates) const
{
    if (candidates.empty()) {
        return nullptr;
    }

    // Get tree from first candidate
    const PLRUTree* tree = std::static_pointer_cast<TreePLRURPReplData>(
            candidates[0]->replacementData)->tree.get();

    // Index of the tree entry we are currently checking. Start with root.
    uint64_t tree_index = 0;

    // Parse tree
    while (tree_index < tree->size()) {
        // Go to the next tree entry
        if (tree->at(tree_index)) {
            tree_index = rightSubtreeIndex(tree_index);
        } else {
            tree_index = leftSubtreeIndex(tree_index);
        }
    }

    // The tree index is currently at the leaf of the victim displaced by the
    // number of non-leaf nodes
    uint64_t victim_index = tree_index - (numLeaves - 1);

    // Make sure the victim index is within the candidates range
    if (victim_index < candidates.size()) {
        return candidates.at(victim_index);
    } else {
        // Fallback to first candidate if index is out of range
        return candidates[0];
    }
}

std::shared_ptr<ReplacementData>
TreePLRURP::instantiateEntry()
{
    // Generate a tree instance every numLeaves created
    if (count % numLeaves == 0) {
        treeInstance = new PLRUTree(numLeaves - 1, false);
    }

    // Create replacement data using current tree instance
    TreePLRURPReplData* treePLRUReplData = new TreePLRURPReplData(
        (count % numLeaves) + numLeaves - 1,
        std::shared_ptr<PLRUTree>(treeInstance));

    // Update instance counter
    count++;

    return std::shared_ptr<ReplacementData>(treePLRUReplData);
}

} // namespace replacement_policy
} // namespace gem5
