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

    // Per-block storage delta relative to parent (type-erased placeholder until Task 3
    // lands the real executor delta type). NOTE: put() does NOT verify this member —
    // the "put succeeded" arrow must be drawn by the ENGINE (it attaches the delta it
    // just executed), not assumed from store membership.
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
/// put/get/occupantAt); decision atomicity ACROSS put/adoptCanonicalHead is the
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
        // insert_or_assign, NOT emplace: an allowed same-height coexistence (detached
        // or canonical occupant above) must re-key the index to the block just
        // imported — emplace kept the FIRST importer, so put's occupancy notion
        // (every same-height non-detached block) and occupantAt's (first importer)
        // were two predicates over one state, and a child of the second sibling saw
        // occupantAt(parentHeight) name a block the engine no longer considers the
        // occupant (the N3 "SYNCING forever" shape, between the import and the FCU).
        m_byNumber.insert_or_assign(block.number, block.hash);
        m_blocks.emplace(block.hash, std::move(block));
        return true;
    }

    /// The most recent import at @p number (nullopt when the height was never
    /// imported). Mirrors put's occupancy exactly: same-height coexistences re-key.
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
    /// Membership in this store == a payload was imported and accepted. It is NOT a
    /// state-plane guarantee by itself: hasState mirrors hasBlock, and the delta plane
    /// is whatever the engine attached at put() (see storageDelta above — unverified
    /// here). Callers must not read this as "the post-state is materialized".
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
        // Recompute liveness for EVERY stored block — at/above AND below the new
        // head. At/above: live iff the block descends from the head. Below: live iff
        // the HEAD descends from it (a canonical ancestor of the switched-to chain) —
        // a switch at/below the old tip (§4.3) makes some below-head blocks NOT
        // ancestors, and leaving them live kept old-branch occupants indexed at
        // heights the new chain occupies, the same SYNCING-forever shape (review N3)
        // one FCU later. Detached bodies stay hash-addressable for re-import.
        for (auto& [blockHash, block] : m_blocks)
        {
            if (blockHash == hash)
            {
                block.detached = false;
                continue;
            }
            if (block.number >= number)
            {
                block.detached = !descendsFrom(blockHash, hash);
            }
            else
            {
                // descendsFrom(head, block) walks in-store parent links; a sparse store
                // (intermediate body never staged) can leave the walk unanswerable —
                // answerable=false means "do NOT detach": falsely detaching a live
                // canonical ancestor re-opens the SYNCING-forever shape this fix closes.
                const auto walkable = descendsFromWalkable(blockHash);
                block.detached = walkable && !descendsFrom(hash, blockHash);
            }
        }
        // Heights above the new head have no canonical occupant; below/at it, a
        // detached block must not occupy its height either.
        std::erase_if(m_byNumber, [this, number](auto const& item) {
            if (item.first > number)
            {
                return true;
            }
            auto const it = m_blocks.find(item.second);
            return it == m_blocks.end() || it->second.detached;
        });
        // Rebuild the index from liveness, not just the head: a block re-livened by
        // this pass (switch-back onto a still-stored chain) must regain its entry, or
        // occupantAt answers "never imported" for a live block (the mirror invariant
        // split in the opposite direction).
        for (auto const& [hash, block] : m_blocks)
        {
            if (!block.detached)
            {
                m_byNumber.insert_or_assign(block.number, hash);
            }
        }
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
    /// True iff a walk FROM @p blockHash can terminate: every parent link resolvable
    /// in m_blocks. Unwalkable (sparse) links mean ancestry cannot be decided from this
    /// store at all — callers must not detach on that basis.
    [[nodiscard]] bool descendsFromWalkable(bcos::h256 const& blockHash) const
    {
        auto cursor = blockHash;
        for (std::size_t guard = 0; guard <= m_blocks.size(); ++guard)
        {
            auto const it = m_blocks.find(cursor);
            if (it == m_blocks.end())
            {
                return false;  // sparse: parent body never imported
            }
            if (it->second.parent == cursor)
            {
                return false;  // self-loop guard: cannot terminate
            }
            cursor = it->second.parent;
        }
        return true;
    }

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
