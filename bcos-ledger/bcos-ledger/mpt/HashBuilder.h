/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file HashBuilder.h
 * @brief Canonical MPT construction from a key-set; computes the 32-byte state root (spec §5.3)
 */
#pragma once

#include "NodeCache.h"
#include "TrieNode.h"
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <functional>
#include <map>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace bcos::ledger::mpt
{

/// Result of a stateless trie build: the 32-byte root plus the hash-keyed RLP encodings of every
/// node that must be persisted (the hash-kind nodes produced during the build).
struct TrieBuildResult
{
    bcos::h256 root;
    std::unordered_map<bcos::h256, bcos::bytes> newNodes;
};

/// Stateless, reentrant entry point: build one canonical MPT from an ordered keyHash -> value map
/// and return {root, newNodes}. The input is std::map specifically: its type already pins down
/// everything the build relies on — ascending iteration (red-black invariant), the canonical order
/// (default std::less<h256> == 32-byte lexicographic == 64-nibble path order), and key uniqueness
/// (not a multimap). So there is no sort, no is_sorted check, no dup handling — just one O(n) walk.
/// Touches no object state and no NodeCache, so independent inputs may be built concurrently
/// (coarse-grained parallelism across tries). Internally single-threaded.
TrieBuildResult computeTrieRoot(std::map<bcos::h256, bcos::bytes> const& entries);

/// Accumulates put/remove operations keyed by a 32-byte keccak path, then builds the canonical
/// Ethereum MPT and returns its 32-byte root.
///
/// Keys are full 32-byte hashes (the "secure trie" key transform happens upstream): their
/// 64-nibble paths are all the same length, so no key ever terminates inside a branch — branch
/// nodes never carry a value here. Because 32-byte lexicographic order equals 64-nibble order,
/// the std::map of changes already yields paths in canonical sorted order.
///
/// SCOPE: only the from-empty build is implemented (m_priorRoot == emptyRootHash()). Incremental
/// rebuild on a non-empty prior root throws MPTInvariantViolation and is deferred to M4 (PR-10).
class HashBuilder
{
public:
    HashBuilder(NodeCache& cache, bcos::h256 priorRoot);

    /// Records a key→value insertion. Coroutine for call-style symmetry; body is trivial.
    bcos::task::Task<void> put(bcos::h256 const& keyHash, bcos::bytes value);

    /// Records a key deletion (no-op from an empty trie).
    bcos::task::Task<void> remove(bcos::h256 const& keyHash);

    /// Builds the canonical trie from the accumulated changes and returns the 32-byte root.
    /// @throws MPTInvariantViolation when m_priorRoot != emptyRootHash() (M4 functionality).
    bcos::task::Task<bcos::h256> commit();

    /// Hash-keyed RLP encodings of every newly produced (>=32 byte) node, transferred out.
    std::unordered_map<bcos::h256, bcos::bytes> drainNewNodes();

    /// Hashes of nodes made obsolete by this commit (always empty for from-empty builds).
    std::unordered_set<bcos::h256> drainObsoletedNodes();

private:
    std::reference_wrapper<NodeCache> m_cache;
    bcos::h256 m_priorRoot;
    /// key → value, or nullopt for a recorded delete. Sorted by 32-byte key == 64-nibble path.
    std::map<bcos::h256, std::optional<bcos::bytes>> m_changes;
    std::unordered_map<bcos::h256, bcos::bytes> m_newNodes;
    std::unordered_set<bcos::h256> m_obsoleted;
};

}  // namespace bcos::ledger::mpt
