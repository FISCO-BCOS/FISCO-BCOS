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
 * @file ImportedStore.h
 * @brief Hash-keyed in-memory store for engine-imported (not yet canonical) OP
 * payloads — the S5 `InsertBlockWithoutSetHead` side. Blocks land here by HASH;
 * nothing in this store touches NUMBER_2_HASH / SYS_CURRENT_STATE (design §4.2).
 */
#pragma once

#include <bcos-framework/protocol/ProtocolTypeDef.h>
#include <bcos-utilities/Common.h>

#include <cstddef>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace bcos::engine
{
struct ImportedBlock
{
    bcos::h256 hash;
    bcos::h256 parent;
    bcos::protocol::BlockNumber number{};
    bcos::bytes headerBytes;       // encoded header, read back BY HASH (never by number)
    std::vector<bcos::bytes> txs;  // ordered signed envelopes
    // Canonical-row payloads (captured at import; canonicalize writes them):
    // tars-encoded transactions / FISCO receipt encodings, keyed by
    // txHashes[i] = keccak(txs[i]) — the exact rows prewriteBlockToBuffer wrote.
    std::vector<bcos::h256> txHashes;
    std::vector<bcos::bytes> encodedTxs;
    std::vector<bcos::bytes> receipts;  // encoded FISCO receipts, index-aligned
    // Recipients, index-aligned with txHashes/encodedTxs. Captured at import from the
    // decoded transaction ("0x"-prefixed, or empty for contract creation) so canonicalize
    // can write SYS_NUMBER_2_TXS (the by-number tx list ledger::getBlockData reads)
    // without re-decoding the envelopes.
    std::vector<std::string> txRecipients;

    // Per-block storage delta relative to parent. Task 3 replaces this placeholder
    // with the real executor delta type; `put` success == the delta exists.
    std::shared_ptr<void> storageDelta;
    // S6 switch support: the MATERIALIZED full post-state of this block (every live
    // key/value on the import view). Restored wholesale when a switch-SetCanonical
    // makes this block the canonical tip at a height at/below the old tip.
    std::shared_ptr<void> postStateFlat;
    // Set by adoptCanonicalHead when a switch de-canonicalizes this block (design
    // §4.3: 丢掉未挂在新头上的 live imported 边). A detached block keeps its body
    // hash-addressable through get()/hasBlock, but must not occupy a height nor keep
    // put()'s descendant guard firing for a later legal import (review N3).
    bool detached{false};
};

/// Imported payloads only: canonical-chain lookups must go through the ledger
/// tables first, this store is the OP-side fallback (design §4.1 read path).
/// Internally synchronized (concurrent newPayload RPC threads race
/// put/get/occupantAt); decision atomicity ACROSS put/canonicalize is the
/// caller's engine lock (design §4.2).
class ImportedStore
{
public:
    /// Same-hash re-put is idempotent (newPayload replay stays VALID, no double
    /// write). A same-height occupant with imported descendants rejects the
    /// overwrite — UNLESS the occupant is canonical (@p occupantCanonical, decided
    /// by the caller via the ledger): a canonical occupant's descendants stay
    /// reachable through the canonical chain history, and the new block merely
    /// awaits its own FCU (ancestor-sibling, §4.3).
    bool put(ImportedBlock block, bool occupantCanonical = false)
    {
        std::lock_guard lock(m_mutex);
        if (m_blocks.contains(block.hash))
        {
            return true;
        }
        for (auto const& [hash, existing] : m_blocks)
        {
            if (existing.number != block.number || existing.detached)
            {
                continue;
            }
            for (auto const& [childHash, child] : m_blocks)
            {
                if (child.detached)
                {
                    // A de-canonicalized branch's links are already orphaned by the
                    // switch; they must not block the new chain (review N3).
                    continue;
                }
                if (child.parent == hash && !occupantCanonical)
                {
                    return false;
                }
            }
        }
        m_byNumber.emplace(block.number, block.hash);
        m_blocks.emplace(block.hash, std::move(block));
        return true;
    }

    /// First importer at @p number (nullopt when the height was never imported).
    [[nodiscard]] std::optional<h256> occupantAt(bcos::protocol::BlockNumber number) const
    {
        std::lock_guard lock(m_mutex);
        if (auto it = m_byNumber.find(number); it != m_byNumber.end())
        {
            return it->second;
        }
        return std::nullopt;
    }

    [[nodiscard]] bool hasBlock(const bcos::h256& hash) const
    {
        std::lock_guard lock(m_mutex);
        return m_blocks.contains(hash);
    }
    /// Every stored block was executed on its parent's post-state, so a stored
    /// block always has state (design: put 成功才 hasState).
    [[nodiscard]] bool hasState(const bcos::h256& hash) const { return hasBlock(hash); }
    [[nodiscard]] std::optional<ImportedBlock> get(const bcos::h256& hash) const
    {
        std::lock_guard lock(m_mutex);
        auto const it = m_blocks.find(hash);
        if (it == m_blocks.end())
        {
            return std::nullopt;
        }
        return it->second;
    }
    [[nodiscard]] std::size_t size() const
    {
        std::lock_guard lock(m_mutex);
        return m_blocks.size();
    }

    /// After a switch/forward FCU canonicalizes @p hash at @p number, the occupants at
    /// heights above @p number are off the new canonical chain (design §4.3: 丢掉未挂在
    /// 新头上的 live imported 边). Drop their number index — bodies stay hash-addressable
    /// via m_blocks for a later re-FCU or re-import — and re-key @p number to the new head,
    /// so the next legal import at those heights does not answer SYNCING forever because a
    /// de-canonicalized occupant looks like a live conflicting branch (review N3).
    void adoptCanonicalHead(bcos::protocol::BlockNumber number, const bcos::h256& hash)
    {
        std::lock_guard lock(m_mutex);
        // Recompute liveness for every stored block at/above the new head: a block is
        // live iff it IS the new head or descends from it. Old-branch occupants and
        // their descendants are marked detached (bodies stay hash-addressable), which
        // makes put()'s descendant guard skip them; without this, A-B-C-D → B' leaves
        // C (child D) occupying height 3 and the next legal import at 3 answers
        // SYNCING forever (review N3). Blocks below the head are canonical ancestors
        // and keep their prior state.
        for (auto& [blockHash, block] : m_blocks)
        {
            if (block.number >= number)
            {
                block.detached = !descendsFrom(blockHash, hash);
            }
        }
        std::erase_if(m_byNumber, [number](auto const& item) { return item.first > number; });
        m_byNumber[number] = hash;
    }

    /// Memory bounding (review P2): drop the materialized post-state flats of blocks
    /// ABOVE the canonical tip — dead branches are never imported onto again (their
    /// parent planes are unrecoverable) and their bodies remain hash-addressable for
    /// re-import. Live-window flats (number <= tip) stay: chained imports and
    /// canonical-below-tip ancestor siblings execute on them. Full prune remains a
    /// follow-up (design: 本里程碑不 prune).
    void pruneFlatsAbove(bcos::protocol::BlockNumber tipNumber)
    {
        std::lock_guard lock(m_mutex);
        for (auto& [hash, block] : m_blocks)
        {
            if (block.number > tipNumber)
            {
                block.postStateFlat.reset();
            }
        }
    }

    /// Memory bound (review F4): release the flats of blocks at/below the finalized
    /// marker — they are irreversible and can never serve as a new import's parent
    /// plane again.
    void pruneFlatsAtOrBelow(bcos::protocol::BlockNumber finalizedNumber)
    {
        std::lock_guard lock(m_mutex);
        for (auto& [hash, block] : m_blocks)
        {
            if (block.number <= finalizedNumber)
            {
                block.postStateFlat.reset();
            }
        }
    }

private:
    /// True iff @p candidate is @p ancestorOrSelfHash itself or a stored descendant of
    /// it. Walks parent links through m_blocks (all stored bodies keep their links), so
    /// a live child of the new head is recognised while old-branch orphans are not.
    [[nodiscard]] bool descendsFrom(
        bcos::h256 const& candidate, bcos::h256 const& ancestorOrSelfHash) const
    {
        auto cursor = candidate;
        for (std::size_t guard = 0; guard <= m_blocks.size(); ++guard)
        {
            if (cursor == ancestorOrSelfHash)
            {
                return true;
            }
            auto const it = m_blocks.find(cursor);
            if (it == m_blocks.end() || it->second.parent == cursor)
            {
                return false;
            }
            cursor = it->second.parent;
        }
        return false;
    }

    mutable std::mutex m_mutex;
    std::unordered_map<bcos::h256, ImportedBlock> m_blocks;
    std::unordered_map<bcos::protocol::BlockNumber, bcos::h256> m_byNumber;
};
}  // namespace bcos::engine
