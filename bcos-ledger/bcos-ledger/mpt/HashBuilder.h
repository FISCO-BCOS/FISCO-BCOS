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

#include "Constants.h"
#include "TrieMerge.h"
#include <bcos-framework/storage2/Storage.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

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
/// Touches no object state and no storage, so independent inputs may be built concurrently
/// (coarse-grained parallelism across tries). Internally single-threaded.
/// @note Synchronous (returns a value, not a Task), unlike HashBuilder::commit().
TrieBuildResult computeTrieRoot(std::map<bcos::h256, bcos::bytes> const& entries);

/// Stateless core over already-sorted, unique (keyHash, value-view) entries. HashBuilder::commit()
/// calls this after resolving its change-set; the header cannot pull in the build internals, so
/// this free function is the seam. Callers guarantee ascending, deduplicated keys.
TrieBuildResult computeTrieRootFromSorted(
    std::span<std::pair<bcos::h256, bcos::bytesConstRef> const> sortedEntries);

/// Accumulates put/remove operations keyed by a 32-byte keccak path, then builds the canonical
/// Ethereum MPT and returns its 32-byte root.
///
/// Keys are full 32-byte hashes (the "secure trie" key transform happens upstream): their
/// 64-nibble paths are all the same length, so no key ever terminates inside a branch — branch
/// nodes never carry a value here. Because 32-byte lexicographic order equals 64-nibble order,
/// the std::map of changes already yields paths in canonical sorted order.
///
/// Templated on @p Storage — any type satisfying storage2::ReadWriteStorage<h256, bytes>:
/// commit() reads prior-version nodes through it for the incremental path and flushes every
/// produced node back via storage2::writeSome; the storage layer (cache, backing store, or a
/// layered stack) owns persistence and cache policy.
///
/// Two build paths, selected by the prior root:
///  - m_priorRoot == emptyRootHash(): from-empty build over the stateless computeTrieRoot core.
///  - otherwise: path-level incremental rebuild via mergeTrie() (spec §5.3 path 1) — only nodes
///    on changed key paths are read; untouched subtrees are re-referenced by hash, unread.
template <bcos::storage2::ReadWriteStorage<bcos::h256, bcos::bytes> Storage>
class HashBuilder
{
public:
    HashBuilder(Storage& storage, bcos::h256 priorRoot) : m_storage(storage), m_priorRoot(priorRoot)
    {}

    /// Records a key→value insertion. Coroutine for call-style symmetry; body is trivial.
    bcos::task::Task<void> put(bcos::h256 const& keyHash, bcos::bytes value)
    {
        m_changes[keyHash] = std::move(value);
        co_return;
    }

    /// Records a key deletion (a no-op when the key is absent from the prior trie).
    bcos::task::Task<void> remove(bcos::h256 const& keyHash)
    {
        m_changes[keyHash] = std::nullopt;
        co_return;
    }

    /// Builds the canonical trie from the accumulated changes and returns the 32-byte root:
    /// from-empty via the stateless core, incremental via mergeTrie() (reads prior nodes from
    /// the storage; a missing referenced node throws MPTInvariantViolation).
    bcos::task::Task<bcos::h256> commit()
    {
        if (m_priorRoot != emptyRootHash())
        {
            auto merged = co_await mergeTrie(m_storage.get(), m_priorRoot, m_changes);
            m_newNodes = std::move(merged.newNodes);
            m_obsoleted = std::move(merged.obsoletedNodes);
            co_await flushNewNodes();
            co_return merged.root;
        }

        // Resolve deletes (no-op from an empty trie); m_changes is sorted, so survivors stay
        // sorted.
        std::vector<std::pair<bcos::h256, bcos::bytesConstRef>> survivors;
        survivors.reserve(m_changes.size());
        for (auto const& [key, maybeValue] : m_changes)
        {
            if (maybeValue.has_value())
            {
                survivors.emplace_back(key, bcos::ref(*maybeValue));
            }
        }

        // Build the trie through the stateless core, then take ownership of the produced nodes.
        auto result = computeTrieRootFromSorted(survivors);
        m_newNodes = std::move(result.newNodes);

        co_await flushNewNodes();
        co_return result.root;
    }

    /// Hash-keyed RLP encodings of every newly produced (>=32 byte) node, transferred out.
    std::unordered_map<bcos::h256, bcos::bytes> drainNewNodes()
    {
        return std::exchange(m_newNodes, {});
    }

    /// Hashes of nodes made obsolete by this commit (always empty for from-empty builds).
    std::unordered_set<bcos::h256> drainObsoletedNodes() { return std::exchange(m_obsoleted, {}); }

private:
    /// Flush m_newNodes into the storage in one batched writeSome — a single storage-layer
    /// round-trip instead of one per node (a real win for a RocksDB-backed store). m_newNodes is
    /// retained for drainNewNodes(), so the batch copies the entries.
    bcos::task::Task<void> flushNewNodes()
    {
        std::vector<std::tuple<bcos::h256, bcos::bytes>> batch;
        batch.reserve(m_newNodes.size());
        for (auto const& [hash, raw] : m_newNodes)
        {
            batch.emplace_back(hash, raw);
        }
        co_await bcos::storage2::writeSome(m_storage.get(), std::move(batch));
    }

    std::reference_wrapper<Storage> m_storage;
    bcos::h256 m_priorRoot;
    /// key → value, or nullopt for a recorded delete. Sorted by 32-byte key == 64-nibble path.
    std::map<bcos::h256, std::optional<bcos::bytes>> m_changes;
    std::unordered_map<bcos::h256, bcos::bytes> m_newNodes;
    std::unordered_set<bcos::h256> m_obsoleted;
};

}  // namespace bcos::ledger::mpt
