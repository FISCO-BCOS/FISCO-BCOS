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
#include <memory>
#include <mutex>
#include <optional>
#include <string>
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
    /// overwrite — UNLESS the caller vouches THAT occupant as canonical
    /// (@p vouchedCanonicalOccupant names the occupant's hash, decided by the
    /// caller via the ledger): the waiver is occupant-scoped, not a height-wide
    /// pass — a non-canonical co-tenant at the same height keeps its guard (review
    /// F20). A canonical occupant's descendants stay reachable through the
    /// canonical chain history, and the new block merely awaits its own FCU
    /// (ancestor-sibling, §4.3).
    bool put(ImportedBlock block, std::optional<bcos::h256> vouchedCanonicalOccupant = std::nullopt)
    {
        std::lock_guard lock(m_mutex);
        if (auto const it = m_blocks.find(block.hash); it != m_blocks.end())
        {
            // Same-hash re-import re-livens a detached body (the contract above:
            // "for a later re-FCU or re-import"): clear the flag and re-key the
            // height so occupantAt/put's guard see it live again. A live block is
            // an idempotent no-op.
            if (it->second->detached)
            {
                it->second->detached = false;
                // The caller's voucher describes the OCCUPANT, not this re-livened
                // block, so the re-key arbitrates (lowest hash among live) rather than
                // handing the height to whichever copy arrived last.
                rekeyOccupantLocked(it->second->number, it->first, /*vouchedCanonical=*/false);
            }
            return true;
        }
        // Descendant guard over the height/children indexes (review F6): the old
        // shape scanned every stored block for same-height live occupants and then
        // every block again for their children (O(blocks^2) per import). Blocks are
        // never erased from this store — detached is a flag — so both indexes only
        // grow at emplace and liveness is read live through m_blocks at probe time.
        // Semantics are the previous loop's, verbatim: reject when a LIVE occupant
        // of this height has a LIVE stored child, unless that occupant is vouched.
        for (auto const& occupantHash : m_byHeight[block.number])
        {
            auto const& occupant = *m_blocks.at(occupantHash);
            if (occupant.detached)
            {
                continue;
            }
            auto const children = m_children.find(occupantHash);
            if (children == m_children.end())
            {
                continue;
            }
            for (auto const& childHash : children->second)
            {
                if (m_blocks.at(childHash)->detached)
                {
                    // A de-canonicalized branch's links are already orphaned by the
                    // switch; they must not block the new chain (review N3).
                    continue;
                }
                if (vouchedCanonicalOccupant != occupantHash)
                {
                    return false;
                }
            }
        }
        // Re-key through the store-wide arbitration (not emplace, not a bare
        // last-arrival assign): emplace kept the FIRST importer, and insert_or_assign
        // kept the LAST one — either way put's occupancy depended on import order
        // while adoptCanonicalHead arbitrated deterministically (canonical lineage,
        // then lowest hash). rekeyOccupantLocked applies the tie half of that rule, so
        // both writers answer the same heights the same way. A vouched import (any
        // voucher) is the arbitration winner here — the just-imported block in the
        // ancestor-sibling shape the engine drives.
        rekeyOccupantLocked(
            block.number, block.hash, /*vouchedCanonical=*/vouchedCanonicalOccupant.has_value());
        auto entry = std::make_shared<ImportedBlock>(std::move(block));
        m_children[entry->parent].push_back(entry->hash);
        m_byHeight[entry->number].push_back(entry->hash);
        m_blocks.emplace(entry->hash, std::move(entry));
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
    /// A shared read view, not a copy (review F6): bodies are immutable after put(),
    /// so a reader takes one pointer under the lock instead of deep-copying the
    /// header/tx/receipt vectors; the `detached`/`postStateFlat` flags it observes
    /// are the store's live values at each read (nullptr = unknown hash).
    [[nodiscard]] std::shared_ptr<const ImportedBlock> get(const bcos::h256& hash) const
    {
        std::lock_guard lock(m_mutex);
        auto const it = m_blocks.find(hash);
        return it == m_blocks.end() ? nullptr : it->second;
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
        // Whether the HEAD's body is staged at all — read before the loop: the loop
        // below re-livens the head itself, and unordered iteration may visit the
        // below-head blocks first, so the per-block flag cannot be trusted here.
        bool const headStaged = m_blocks.contains(hash);
        for (auto& [blockHash, block] : m_blocks)
        {
            if (blockHash == hash)
            {
                block->detached = false;
                continue;
            }
            if (block->number >= number)
            {
                block->detached = !descendsFrom(blockHash, hash);
            }
            else if (!headStaged)
            {
                // The head itself is not staged (a ledger-committed head, or after a
                // restart): the walk's first step is the hole, so nothing would ever
                // detach and the descendant guard would keep firing (the
                // SYNCING-forever shape this pass exists to close). With no staged
                // links to the head, no below-head stored block can be shown to be its
                // ancestor — fall back to the number rule and detach. Bodies stay
                // hash-addressable, and a later re-FCU/re-import re-livens whatever
                // turns out to be canonical (reversible by construction).
                block->detached = true;
            }
            else
            {
                // Below the new head: detach only when ancestry is DECIDED — the walk
                // from the head passed the candidate's height without finding it. A
                // sparse or cyclic staged range leaves the question open, and falsely
                // detaching a live canonical ancestor re-opens the SYNCING-forever
                // shape this fix closes (review N3/R1).
                block->detached = ancestryDecidableFromHead(hash, block->number) &&
                                  !descendsFrom(hash, blockHash);
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
            return it == m_blocks.end() || it->second->detached;
        });
        // Rebuild the index from liveness, not just the head: a block re-livened by
        // this pass (switch-back onto a still-stored chain) must regain its entry, or
        // occupantAt answers "never imported" for a live block (the mirror invariant
        // split in the opposite direction). Arbitration must not depend on
        // unordered_map iteration order: canonical-lineage blocks take their heights
        // first, off-lineage live blocks only fill still-empty heights, and the head
        // always wins its own. Within one category a height can still tie (two live
        // same-height siblings, e.g. two children of the head awaiting reorg): the
        // winner is the LOWEST hash, so the answer is identical across runs and
        // standard libraries instead of following hash-bucket order.
        auto const onCanonicalLineage = [&](bcos::h256 const& candidate) {
            return candidate == hash || descendsFrom(hash, candidate);
        };
        for (auto const& [blockHash, block] : m_blocks)
        {
            if (!block->detached && onCanonicalLineage(blockHash))
            {
                m_byNumber.insert_or_assign(block->number, blockHash);
            }
        }
        for (auto const& [blockHash, block] : m_blocks)
        {
            if (block->detached || onCanonicalLineage(blockHash))
            {
                continue;
            }
            auto const [it, inserted] = m_byNumber.try_emplace(block->number, blockHash);
            if (!inserted && !onCanonicalLineage(it->second) && blockHash < it->second)
            {
                it->second = blockHash;
            }
        }
        // Index the head only when the store actually has its body: a never-put head
        // (ledger-committed, or after a restart) would otherwise be named by
        // m_byNumber while hasBlock denies it — occupantAt would answer a block the
        // store cannot serve.
        if (headStaged)
        {
            m_byNumber[number] = hash;
        }
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
            if (block->number > tipNumber)
            {
                block->postStateFlat.reset();
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
            if (block->number <= finalizedNumber)
            {
                block->postStateFlat.reset();
            }
        }
    }

private:
    /// The height index's tie rule, shared by put()/re-liven and (for its off-lineage
    /// fill) adoptCanonicalHead. A caller-vouched canonical import
    /// (@p vouchedCanonical: the just-written block IS the canonical one) owns its
    /// height outright; otherwise the height goes to the LOWEST hash among the LIVE
    /// blocks at @p number (including @p hash itself) — order-independent by
    /// construction, so the occupant cannot follow unordered_map iteration order or
    /// RPC arrival order. adoptCanonicalHead re-arbitrates the whole index with
    /// canonical lineage when the head is known (canonical beats off-lineage, ties
    /// still by lowest hash). PRIVATE on purpose: the body assumes the caller holds
    /// m_mutex (the "Locked" suffix) — an external call would race the store by
    /// construction, and the class's own put()/adoptCanonicalHead are the only
    /// intended callers.
    void rekeyOccupantLocked(
        bcos::protocol::BlockNumber number, bcos::h256 const& hash, bool vouchedCanonical)
    {
        if (vouchedCanonical)
        {
            m_byNumber.insert_or_assign(number, hash);
            return;
        }
        auto best = hash;
        if (auto const height = m_byHeight.find(number); height != m_byHeight.end())
        {
            for (auto const& blockHash : height->second)
            {
                if (blockHash == hash || m_blocks.at(blockHash)->detached)
                {
                    continue;
                }
                if (blockHash < best)
                {
                    best = blockHash;
                }
            }
        }
        m_byNumber.insert_or_assign(number, best);
    }

    /// Can ancestry of a block at @p candidateNumber be DECIDED by walking the new
    /// head's parent links? Parent numbers strictly decrease along a staged chain, so
    /// the walk decides once it passes the candidate's height (a staged block at or
    /// below it exists). Hitting an unstaged parent ABOVE the candidate's height, a
    /// self-loop, or a guard-exhausted cycle leaves the question open (the staged range
    /// is sparse) — and the caller must not detach on an undecidable answer.
    [[nodiscard]] bool ancestryDecidableFromHead(
        bcos::h256 const& headHash, bcos::protocol::BlockNumber candidateNumber) const
    {
        auto cursor = headHash;
        for (std::size_t guard = 0; guard <= m_blocks.size(); ++guard)
        {
            auto const it = m_blocks.find(cursor);
            if (it == m_blocks.end())
            {
                return false;  // left the staged chain ABOVE the candidate's height — a hole
            }
            if (it->second->number <= candidateNumber)
            {
                return true;  // passed the candidate's height inside the staged chain
            }
            if (it->second->parent == cursor)
            {
                return false;  // self-loop: undecidable
            }
            cursor = it->second->parent;
        }
        return false;  // >=2-cycle: undecidable
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
            if (it == m_blocks.end() || it->second->parent == cursor)
            {
                return false;
            }
            cursor = it->second->parent;
        }
        return false;
    }

    mutable std::mutex m_mutex;
    std::unordered_map<bcos::h256, std::shared_ptr<ImportedBlock>> m_blocks;
    std::unordered_map<bcos::protocol::BlockNumber, bcos::h256> m_byNumber;
    // Grow-only side indexes (review F6): entries are never erased from m_blocks
    // (detached is a flag), so both are appended exactly once per emplace and their
    // vectors never dangle. Liveness is NOT cached here — probes read the live
    // `detached` flag through m_blocks.
    std::unordered_map<bcos::h256, std::vector<bcos::h256>> m_children;
    std::unordered_map<bcos::protocol::BlockNumber, std::vector<bcos::h256>> m_byHeight;
};
}  // namespace bcos::engine
