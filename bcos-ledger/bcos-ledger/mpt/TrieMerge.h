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
 * @file TrieMerge.h
 * @brief Path-level incremental MPT rebuild on a non-empty prior root, emitting by POSITION
 *        (spec §5.3 path 1, §5.4; pathdb spec §9 and appendix A)
 */
#pragma once

#include "Constants.h"
#include "Errors.h"
#include "Nibble.h"
#include "NodeDecoder.h"
#include "NodeEncoder.h"
#include "PathDiff.h"
#include "PathKey.h"
#include "Trie.h"  // detail::loadNodeBytesAt — the one verified node read both sides share
#include "TrieNode.h"
// AnyHasher.h defines the free bcos::crypto::hasher::hash(); OpenSSLHasher.h only defines the
// hasher type. Both are reachable via NodeEncoder.h, but keep them explicit — a unity build can
// mask a missing include until CI regroups TUs.
#include <bcos-crypto/hasher/AnyHasher.h>
#include <bcos-crypto/hasher/OpenSSLHasher.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <boost/throw_exception.hpp>
#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <utility>
#include <variant>

namespace bcos::ledger::mpt
{

/// Result of one rebuild of ONE trie: the new 32-byte root plus the row-level changes that make
/// the store hold it. Positions, not hashes — see PathDiff.h for what each field promises.
struct PathMergeResult
{
    bcos::h256 root;
    std::map<PathKey, bcos::bytes> upserts;
    std::set<PathKey> deletes;
    std::map<PathKey, std::optional<bcos::bytes>> preimages;
};

namespace detail
{

// ── Mutable overlay ────────────────────────────────────────────────────────────────────────────
// mergeTrie() applies puts/removes one key at a time to an in-memory overlay of the prior trie.
// A child slot is either absent, a clean reference into the prior version (never loaded), or a
// resolved/dirty in-memory node. Resolving replaces the NodeRef slot with the decoded node, so a
// node on several dirty paths is read from storage exactly once — subsequent keys traverse the
// overlay. Untouched siblings stay as clean NodeRefs and are spliced back by hash, unread.
//
// The rebuild runs in three phases (see mergeTrie at the bottom of this header):
//   1. apply every change to the overlay (mergeInsert / mergeErase, resolving lazily);
//   2. classify the overlay root: emptied trie / nothing-changed short-circuit / re-emit;
//   3. re-encode only in-memory nodes bottom-up (emitNode), splicing clean refs verbatim, and
//      settle the row changes.
//
// EVERY node carries its POSITION — the nibbles consumed walking to it from this trie's root
// (spec A.1: branch child i is at P||i, extension child at P||shared). The position is what the
// row is keyed by, so it is threaded through resolve and emit alike; the tree-shape logic in
// phase 1 is untouched by path addressing and reads exactly as it did before.
//
// I/O contract: reads = nodes on changed key paths (each at most once) + one unavoidable probe
// per branch collapse (mergeNormalize); everything else is spliced back by hash, unread. This
// header only READS storage — applying the resulting diff is commitTrie's caller's job
// (MPTBuilder batches one flush per block).
//
// How the row changes fall out (phase 3):
//   - upserts   = every position re-emitted as a hash-kind node. Overwriting a position IS the
//                 whole statement; there is nothing to reference-count.
//   - deletes   = positions this rebuild READ minus positions it re-emitted. Everything in it is
//                 provably a row that existed (we read it) and provably has no node any more (the
//                 rebuild did not put one back), which is what makes the four delete sources of
//                 spec A.6 fall out of one subtraction: a branch collapse absorbing its survivor
//                 (A.6-1), an extension merging with its child (A.6-2) and a node shrinking below
//                 the 32-byte inline threshold (A.6-3) all read the node and then fail to re-emit
//                 it. (A.6-4, a whole storage trie disappearing, is not one trie's rebuild and is
//                 handled by MPTBuilder.) The survivor-subtree-never-moves theorem (spec A.5) is
//                 what makes this sound: an untouched subtree keeps its positions, so a position
//                 we never read cannot have become vacant. The mirror of that — a position we DID
//                 read that keeps its node anyway — happens in exactly one place, mergeNormalize's
//                 clean-branch-survivor arm, which un-records the survivor's whole subtree
//                 (unrecordSubtree).
//   - preimages = the bytes each touched position held before, captured at resolve time (spec §9:
//                 the resolved set is exactly the overwritten set, so this costs one copy and no
//                 extra I/O). A position that had no row records nullopt (spec A.7).
//
// MutableNode::dirty distinguishes "resolved to look at" from "actually modified". Erase misses
// leave nodes clean, which is what makes the phase-2 short-circuit sound.

struct MutableNode;
using MutableChild = std::variant<std::monostate,  // absent
    NodeRef,                                       // clean ref into the prior version
    std::unique_ptr<MutableNode>>;                 // resolved (possibly modified) node

struct MutableLeaf
{
    bcos::bytes suffix;  // remaining nibbles below this node's position
    bcos::bytes value;
};
struct MutableExtension
{
    bcos::bytes shared;
    MutableChild child;
};
struct MutableBranch
{
    std::array<MutableChild, NIBBLE_RANGE> children;
};
struct MutableNode
{
    std::variant<MutableLeaf, MutableExtension, MutableBranch> node;
    /// Set when this node was resolved from a hash reference: the prior-version hash.
    std::optional<bcos::h256> originHash;
    /// True once anything at or below this node was modified. A clean resolved branch that
    /// survives a sibling's deletion is re-referenced by originHash instead of being rebuilt
    /// (see mergeNormalize) — that shortcut is only sound while dirty == false.
    bool dirty = false;
};

/// The position of a child reached by consuming @p consumed nibbles from the node at @p parent
/// (spec A.1). One helper for both shapes: a branch consumes a single nibble, an extension its
/// whole shared prefix.
inline bcos::bytes childPosition(bcos::bytes const& parent, bcos::bytesConstRef consumed)
{
    bcos::bytes out;
    out.reserve(parent.size() + consumed.size());
    out.insert(out.end(), parent.begin(), parent.end());
    out.insert(out.end(), consumed.begin(), consumed.end());
    return out;
}
inline bcos::bytes childPosition(bcos::bytes const& parent, bcos::byte nibble)
{
    bcos::bytes out;
    out.reserve(parent.size() + 1);
    out.insert(out.end(), parent.begin(), parent.end());
    out.push_back(nibble);
    return out;
}

// Parse an ExtensionNode.child raw encoding (33-byte 0xa0||hash string, or an inline node's
// complete RLP) into a NodeRef. Inverse of refToRawBytes below.
inline NodeRef refFromRawBytes(bcos::bytes const& raw)
{
    if (raw.size() == HASH_REF_ENCODED_SIZE && raw[0] == RLP_HASH_REF_PREFIX)
    {
        return NodeRef::fromHash(bcos::h256(bcos::bytesConstRef(raw.data() + 1, bcos::h256::SIZE)));
    }
    // Yellow Paper §D 32-byte rule: an inline child ref's RLP is < 32 bytes; anything else
    // here means the stored trie is malformed.
    if (raw.size() >= bcos::h256::SIZE)
    {
        BOOST_THROW_EXCEPTION(MPTInvariantViolation{} << bcos::errinfo_comment(
                                  "refFromRawBytes: inline child ref of >= 32 bytes"));
    }
    return NodeRef::fromInline(bcos::bytesConstRef(raw.data(), raw.size()));
}

// Turn a NodeRef into the bytes an ExtensionNode.child expects. Mirrors the file-local helper in
// HashBuilder.cpp (thin glue over the shared NodeEncoder rules; kept local to each TU).
inline bcos::bytes refToRawBytes(NodeRef const& ref)
{
    if (ref.kind() == NodeRef::Kind::Inline)
    {
        return ref.inlineRef().toBytes();
    }
    bcos::bytes out;
    out.reserve(HASH_REF_ENCODED_SIZE);
    out.push_back(RLP_HASH_REF_PREFIX);
    out.insert(out.end(), ref.payload.begin(), ref.payload.end());
    return out;
}

// Lift a decoded (immutable) TrieNode into the mutable overlay. Branch children and extension
// children become clean NodeRefs — nothing below is loaded.
inline std::unique_ptr<MutableNode> liftNode(TrieNode decoded, std::optional<bcos::h256> origin)
{
    auto out = std::make_unique<MutableNode>();
    out->originHash = origin;
    if (auto* leaf = std::get_if<LeafNode>(&decoded))
    {
        out->node =
            MutableLeaf{.suffix = std::move(leaf->keyNibbles), .value = std::move(leaf->value)};
    }
    else if (auto* ext = std::get_if<ExtensionNode>(&decoded))
    {
        out->node = MutableExtension{
            .shared = std::move(ext->sharedNibbles), .child = refFromRawBytes(ext->child)};
    }
    else if (auto* branch = std::get_if<BranchNode>(&decoded))
    {
        // 64-nibble secure-trie keys never terminate inside a branch (spec §5.4): a decoded
        // branch carrying a value means the storage holds a trie this module did not build.
        if (!branch->value.empty())
        {
            BOOST_THROW_EXCEPTION(MPTInvariantViolation{} << bcos::errinfo_comment(
                                      "mergeTrie: branch node with value (non-64-nibble trie)"));
        }
        MutableBranch mut;
        for (size_t i = 0; i < NIBBLE_RANGE; ++i)
        {
            if (auto& child = branch->children[i]; child.isAbsent())
            {
                mut.children[i] = std::monostate{};
            }
            else
            {
                mut.children[i] = std::move(child);
            }
        }
        out->node = MutableBranch{std::move(mut)};
    }
    else  // EmptyNode never nests inside a trie
    {
        BOOST_THROW_EXCEPTION(
            MPTInvariantViolation{} << bcos::errinfo_comment("mergeTrie: nested empty node"));
    }
    return out;
}

// Shared per-merge state: which trie is being rebuilt, the storage to resolve its prior-version
// nodes from, and the read ledger the row changes are settled against (see the phase notes above).
template <bcos::storage2::ReadableStorage<PathKey> Storage, bcos::crypto::hasher::Hasher HasherT>
struct MergeContext
{
    std::reference_wrapper<Storage> storage;
    TrieScope scope;
    /// The hasher used both to verify what a position hands back and to encode the new version —
    /// one function, so a mismatch can only mean the data disagrees, never the algorithm.
    std::reference_wrapper<HasherT> hasher;
    /// Positions whose rows were read from the prior version.
    std::set<bcos::bytes> resolvedPositions;
    /// What each of those positions held: the preimages of spec §9, free at resolve time.
    std::map<bcos::bytes, bcos::bytes> priorBytes;
};

/// Forget every read this rebuild made at or below @p position — the subtree is going back to
/// disk exactly as it came off it, so none of it may reach the phase-3 subtraction.
///
/// A position IS its own prefix, so `position` itself goes too. Positions are ordered nibble
/// strings, which makes a subtree a CONTIGUOUS range: every position having @p position as a
/// prefix sorts at or after it, and the first position that does not have it as a prefix ends
/// the range. So one lower_bound and a walk covers it — no tree traversal, and no chance of
/// missing a descendant that was resolved several keys earlier in the batch.
///
/// The prior bytes go with the positions: settle only reads priorBytes for positions that end up
/// in upserts or deletes, so leaving them would be harmless, but a read ledger and its payload
/// that can disagree is a trap for the next person.
template <bcos::storage2::ReadableStorage<PathKey> Storage, bcos::crypto::hasher::Hasher HasherT>
void unrecordSubtree(MergeContext<Storage, HasherT>& ctx, bcos::bytes const& position)
{
    auto isBelow = [&position](bcos::bytes const& candidate) {
        return candidate.size() >= position.size() &&
               std::equal(position.begin(), position.end(), candidate.begin());
    };
    for (auto it = ctx.resolvedPositions.lower_bound(position);
        it != ctx.resolvedPositions.end() && isBelow(*it);)
    {
        ctx.priorBytes.erase(*it);
        it = ctx.resolvedPositions.erase(it);
    }
}

// Ensure @p slot holds an in-memory node, loading and decoding the prior-version node at
// @p position when the slot is a clean hash/inline reference. Absent slots are the caller's case.
//
// This is the ONLY place the algorithm reads storage. Three cases:
//   - already boxed: return it — zero I/O. Because the resolve below REPLACES the slot with the
//     decoded node, the second key crossing this node in the same batch lands here (memoize).
//   - hash ref: read the row at @p position and verify keccak(bytes) against the hash the PARENT
//     recorded (loadNodeBytesAt). A miss or a mismatch throws — a trie whose stored node
//     contradicts its own parent is corruption, same rule as Trie::get. The position is recorded
//     with its prior bytes: that is the read ledger phase 3 subtracts against, and the preimage.
//   - inline ref: decode the embedded bytes. An inline child has NO row of its own, so it enters
//     neither ledger — which is also why the inline threshold (spec A.6-3) needs no special case:
//     a node that grows past 32 bytes simply starts being emitted, and one that shrinks below it
//     was read at its position and is not re-emitted, so the subtraction deletes its row.
template <bcos::storage2::ReadableStorage<PathKey> Storage, bcos::crypto::hasher::Hasher HasherT>
bcos::task::Task<MutableNode*> mergeResolve(
    MergeContext<Storage, HasherT>& ctx, MutableChild& slot, bcos::bytes const& position)
{
    if (auto* boxed = std::get_if<std::unique_ptr<MutableNode>>(&slot))
    {
        co_return boxed->get();
    }
    auto const& ref = std::get<NodeRef>(slot);
    std::optional<bcos::h256> origin;
    TrieNode decoded;
    if (ref.kind() == NodeRef::Kind::Hash)
    {
        bcos::h256 const refHash = ref.hash();
        auto raw = co_await loadNodeBytesAt(ctx.storage.get(),
            PathKey{.scope = ctx.scope, .position = position}, refHash, ctx.hasher.get());
        ctx.resolvedPositions.insert(position);
        ctx.priorBytes.insert_or_assign(position, raw);
        origin = refHash;
        decoded = decodeNode(bcos::ref(raw));
    }
    else
    {
        decoded = decodeNode(ref.inlineRef());
    }
    slot = liftNode(std::move(decoded), origin);
    co_return std::get<std::unique_ptr<MutableNode>>(slot).get();
}

// ── Insert ─────────────────────────────────────────────────────────────────────────────────────

// A fresh leaf carrying the remaining @p suffix nibbles. The terminal of every insert.
inline std::unique_ptr<MutableNode> makeLeaf(bcos::bytesConstRef suffix, bcos::bytes value)
{
    auto out = std::make_unique<MutableNode>();
    out->node = MutableLeaf{.suffix = suffix.toBytes(), .value = std::move(value)};
    return out;
}

// Insert (path → value) into the subtree at @p slot, which sits at @p position; @p path holds the
// key's not-yet-consumed nibbles (each recursion level strips what it matched). Textbook MPT
// insert, one case per node shape:
//   - absent slot            → new leaf carrying the whole remaining path;
//   - leaf, same key         → overwrite the value in place;
//   - leaf, diverging key    → fork into a branch at the divergence nibble (wrapped in an
//                              extension when a common prefix remains);
//   - extension, path covers the shared prefix → recurse into the child;
//   - extension, divergence inside the prefix  → split the extension; the untouched tail keeps
//                              its clean child reference — that subtree is re-parented, unread;
//   - branch                 → route by the next nibble and recurse.
// Every node on the way down is marked dirty: an insert always rebuilds its path.
//
// No insert case produces a delete (spec A.5 case A): the nodes a fork creates land on positions
// that were VACANT — the old leaf sinks to P||suffix-prefix, which the extension above it used to
// skip over — and the position it vacated is immediately occupied by the new extension/branch.
template <bcos::storage2::ReadableStorage<PathKey> Storage, bcos::crypto::hasher::Hasher HasherT>
bcos::task::Task<void> mergeInsert(MergeContext<Storage, HasherT>& ctx, MutableChild& slot,
    bcos::bytes const& position, bcos::bytesConstRef path, bcos::bytes value)
{
    if (std::holds_alternative<std::monostate>(slot))
    {
        // A fresh leaf landing in an EMPTIED subtree becomes the (new) root of that
        // subtree. It must be dirty or mergeTrie's phase-2 short-circuit would see an
        // untouched overlay and return the PRIOR root, silently dropping the insert.
        // Concretely: a commitTrie batch that deletes the trie's last remaining leaf
        // (slot -> monostate) and then inserts a new key hits this path — the delete
        // emptied the trie, the insert repopulates it, and without dirty=true the new
        // root would never be re-emitted (forked storageRoot, wrong state root).
        auto leaf = makeLeaf(path, std::move(value));
        leaf->dirty = true;
        slot = std::move(leaf);
        co_return;
    }
    MutableNode* node = co_await mergeResolve(ctx, slot, position);
    node->dirty = true;  // every insert modifies this subtree

    if (auto* leaf = std::get_if<MutableLeaf>(&node->node))
    {
        size_t const cpl = commonPrefixLen(bcos::ref(leaf->suffix), path);
        if (cpl == leaf->suffix.size() && cpl == path.size())
        {
            leaf->value = std::move(value);  // same key: overwrite
            co_return;
        }
        // Distinct equal-length (64-nibble) keys diverge strictly inside both suffixes; one
        // being a prefix of the other means the trie holds keys of a different length.
        if (cpl == leaf->suffix.size() || cpl == path.size())
        {
            BOOST_THROW_EXCEPTION(MPTInvariantViolation{} << bcos::errinfo_comment(
                                      "mergeTrie: key length mismatch at a leaf"));
        }
        MutableBranch fork;
        fork.children[leaf->suffix[cpl]] = makeLeaf(
            bcos::bytesConstRef(leaf->suffix.data() + cpl + 1, leaf->suffix.size() - cpl - 1),
            std::move(leaf->value));
        fork.children[path[cpl]] = makeLeaf(path.getCroppedData(cpl + 1), std::move(value));
        if (cpl == 0)
        {
            node->node = std::move(fork);
        }
        else
        {
            auto boxed = std::make_unique<MutableNode>();
            boxed->node = std::move(fork);
            node->node = MutableExtension{
                .shared = bcos::bytes(path.data(), path.data() + cpl), .child = std::move(boxed)};
        }
        node->originHash.reset();  // structure changed; the read ledger already recorded it
        co_return;
    }

    if (auto* ext = std::get_if<MutableExtension>(&node->node))
    {
        size_t const cpl = commonPrefixLen(bcos::ref(ext->shared), path);
        if (cpl == ext->shared.size())
        {
            auto const below = childPosition(position, bcos::ref(ext->shared));
            co_await mergeInsert(
                ctx, ext->child, below, path.getCroppedData(cpl), std::move(value));
            co_return;
        }
        if (cpl == path.size())  // path exhausted inside the shared prefix: length mismatch
        {
            BOOST_THROW_EXCEPTION(MPTInvariantViolation{} << bcos::errinfo_comment(
                                      "mergeTrie: key length mismatch at an extension"));
        }
        // Split the extension at the divergence point. The untouched tail keeps its clean child
        // reference — nothing below is loaded, and (spec A.5 case A) that subtree's root stays at
        // the very same position: the nibbles above it are redistributed between the new
        // extension, branch and inner extension, never dropped.
        MutableBranch fork;
        bcos::byte const extNibble = ext->shared[cpl];
        if (cpl + 1 == ext->shared.size())
        {
            fork.children[extNibble] = std::move(ext->child);
        }
        else
        {
            auto tail = std::make_unique<MutableNode>();
            tail->node = MutableExtension{
                .shared = bcos::bytes(ext->shared.begin() + cpl + 1, ext->shared.end()),
                .child = std::move(ext->child)};
            fork.children[extNibble] = std::move(tail);
        }
        fork.children[path[cpl]] = makeLeaf(path.getCroppedData(cpl + 1), std::move(value));
        if (cpl == 0)
        {
            node->node = std::move(fork);
        }
        else
        {
            auto boxed = std::make_unique<MutableNode>();
            boxed->node = std::move(fork);
            node->node = MutableExtension{
                .shared = bcos::bytes(path.data(), path.data() + cpl), .child = std::move(boxed)};
        }
        node->originHash.reset();
        co_return;
    }

    auto& branch = std::get<MutableBranch>(node->node);
    // 64-nibble keys cannot exhaust the path at a branch.
    if (path.empty())
    {
        BOOST_THROW_EXCEPTION(MPTInvariantViolation{}
                              << bcos::errinfo_comment("mergeTrie: path exhausted at a branch"));
    }
    auto const below = childPosition(position, path[0]);
    co_await mergeInsert(
        ctx, branch.children[path[0]], below, path.getCroppedData(1), std::move(value));
    co_return;
}

// ── Erase ──────────────────────────────────────────────────────────────────────────────────────

// After a child was removed below, fold degenerate shapes back into canonical form. The MPT
// canonical form forbids exactly two shapes, and this fixes both on the erase unwind path:
//   - an extension whose (rebuilt) child is now a leaf/extension → absorb it: leaf(shared+suffix)
//     or extension(shared+shared2). A child that is a branch, or still a clean ref, stays put.
//   - a branch left with a single child → that child prefixed with its nibble. The survivor must
//     be resolved once to learn its shape — the one unavoidable read of an unmodified node in
//     the whole algorithm. Survivor leaf/extension get merged, so nothing is emitted at their
//     position any more and the phase-3 subtraction deletes that row (spec A.6-1/-2). A survivor
//     BRANCH is not rebuilt at all, only re-parented under a one-nibble extension:
//       · resolved just now and never modified (originHash && !dirty) → keep it referenced by
//         its prior hash and UN-RECORD its whole subtree: those rows stay live and must not be
//         subtracted into deletes. The dirty guard is essential: a branch modified earlier in
//         this batch has diverged from originHash, reverting to the hash would drop those edits.
//       · otherwise (dirty, or inline-decoded with no hash) → keep the boxed node; the emit
//         phase re-encodes it.
//
// In every collapse the survivor SUBTREE keeps its positions (spec A.5's theorem): the extension
// absorbs the eliminated nibble into its own `shared`, so position arithmetic below it is
// unchanged and not one of those rows has to move. That is what makes deleting an account cost
// a handful of rows instead of a re-key of everything under it.
//
// present == 0 is unreachable: an emptied subtree propagates as monostate in mergeErase, so the
// parent collapses there — a zero-child branch here means the invariants are already broken.
template <bcos::storage2::ReadableStorage<PathKey> Storage, bcos::crypto::hasher::Hasher HasherT>
bcos::task::Task<void> mergeNormalize(
    MergeContext<Storage, HasherT>& ctx, MutableNode& node, bcos::bytes const& position)
{
    if (auto* ext = std::get_if<MutableExtension>(&node.node))
    {
        auto* boxed = std::get_if<std::unique_ptr<MutableNode>>(&ext->child);
        if (boxed == nullptr)
        {
            co_return;  // clean ref below: untouched
        }
        if (auto* childLeaf = std::get_if<MutableLeaf>(&(*boxed)->node))
        {
            bcos::bytes suffix = ext->shared;
            suffix.insert(suffix.end(), childLeaf->suffix.begin(), childLeaf->suffix.end());
            node.node =
                MutableLeaf{.suffix = std::move(suffix), .value = std::move(childLeaf->value)};
            node.originHash.reset();
        }
        else if (auto* childExt = std::get_if<MutableExtension>(&(*boxed)->node))
        {
            bcos::bytes shared = ext->shared;
            shared.insert(shared.end(), childExt->shared.begin(), childExt->shared.end());
            node.node =
                MutableExtension{.shared = std::move(shared), .child = std::move(childExt->child)};
            node.originHash.reset();
        }
        co_return;  // child is a branch: canonical as-is
    }

    auto* branch = std::get_if<MutableBranch>(&node.node);
    if (branch == nullptr)
    {
        co_return;  // leaves are always canonical
    }
    size_t present = 0;
    size_t lastNibble = 0;
    for (size_t i = 0; i < NIBBLE_RANGE; ++i)
    {
        if (!std::holds_alternative<std::monostate>(branch->children[i]))
        {
            ++present;
            lastNibble = i;
        }
    }
    if (present >= 2)
    {
        co_return;
    }
    if (present == 0)
    {
        BOOST_THROW_EXCEPTION(MPTInvariantViolation{} << bcos::errinfo_comment(
                                  "mergeTrie: branch emptied without collapse"));
    }

    // One survivor: absorb it. Resolving is unavoidable — its shape decides the merged form.
    auto const survivorPosition = childPosition(position, static_cast<bcos::byte>(lastNibble));
    MutableNode* survivor =
        co_await mergeResolve(ctx, branch->children[lastNibble], survivorPosition);
    if (auto* childLeaf = std::get_if<MutableLeaf>(&survivor->node))
    {
        bcos::bytes suffix;
        suffix.reserve(1 + childLeaf->suffix.size());
        suffix.push_back(static_cast<bcos::byte>(lastNibble));
        suffix.insert(suffix.end(), childLeaf->suffix.begin(), childLeaf->suffix.end());
        node.node = MutableLeaf{.suffix = std::move(suffix), .value = std::move(childLeaf->value)};
    }
    else if (auto* childExt = std::get_if<MutableExtension>(&survivor->node))
    {
        bcos::bytes shared;
        shared.reserve(1 + childExt->shared.size());
        shared.push_back(static_cast<bcos::byte>(lastNibble));
        shared.insert(shared.end(), childExt->shared.begin(), childExt->shared.end());
        node.node =
            MutableExtension{.shared = std::move(shared), .child = std::move(childExt->child)};
    }
    else  // survivor is a branch: not merged into the parent — only re-parented under an ext
    {
        MutableChild& survivorSlot = branch->children[lastNibble];
        auto& boxed = std::get<std::unique_ptr<MutableNode>>(survivorSlot);
        if (boxed->originHash.has_value() && !boxed->dirty)
        {
            // Resolved only to learn its shape, never modified: its prior encoding stays live at
            // the same position under the new extension. Reverting the slot to a hash reference
            // discards the boxed node, so NOTHING in that subtree is re-emitted — which makes the
            // whole subtree the ONE case where positions we read keep nodes we do not write, and
            // un-recording only the survivor's own position would subtract the rest into deletes.
            //
            // dirty == false is what licenses this: any change at or below a node marks it dirty
            // on the way down (mergeInsert) or on the unwind of a confirmed delete (mergeErase),
            // so a clean survivor is byte-identical on disk all the way down and every one of its
            // descendants is still exactly where the survivor's unchanged hash says it is.
            unrecordSubtree(ctx, survivorPosition);
            NodeRef const keep = NodeRef::fromHash(*boxed->originHash);
            node.node = MutableExtension{.shared = bcos::bytes{static_cast<bcos::byte>(lastNibble)},
                .child = MutableChild{keep}};
        }
        else
        {
            node.node = MutableExtension{.shared = bcos::bytes{static_cast<bcos::byte>(lastNibble)},
                .child = std::move(survivorSlot)};
        }
    }
    node.originHash.reset();
    co_return;
}

/// Whether the erase changed anything (a miss is a legal no-op: deleting an absent key).
enum class EraseOutcome : uint8_t
{
    NotFound,
    Deleted
};

// Erase @p path from the subtree at @p slot, which sits at @p position. Unlike mergeInsert, nodes
// are NOT marked dirty on the way down — only on the unwind of a confirmed Deleted. A miss (leaf
// mismatch, extension prefix divergence, absent branch child) leaves every resolved node clean,
// which is what lets mergeTrie's phase-2 short-circuit return the prior root untouched when a
// whole batch turns out to be no-ops. On Deleted the slot becomes monostate; an extension whose
// only subtree vanished propagates the monostate up, otherwise mergeNormalize repairs the
// degenerate shape.
template <bcos::storage2::ReadableStorage<PathKey> Storage, bcos::crypto::hasher::Hasher HasherT>
bcos::task::Task<EraseOutcome> mergeErase(MergeContext<Storage, HasherT>& ctx, MutableChild& slot,
    bcos::bytes const& position, bcos::bytesConstRef path)
{
    if (std::holds_alternative<std::monostate>(slot))
    {
        co_return EraseOutcome::NotFound;
    }
    MutableNode* node = co_await mergeResolve(ctx, slot, position);

    if (auto* leaf = std::get_if<MutableLeaf>(&node->node))
    {
        if (leaf->suffix.size() != path.size() ||
            !std::equal(leaf->suffix.begin(), leaf->suffix.end(), path.begin()))
        {
            co_return EraseOutcome::NotFound;  // node stays clean: a miss modifies nothing
        }
        slot = std::monostate{};
        co_return EraseOutcome::Deleted;
    }

    if (auto* ext = std::get_if<MutableExtension>(&node->node))
    {
        if (path.size() < ext->shared.size() ||
            !std::equal(ext->shared.begin(), ext->shared.end(), path.begin()))
        {
            co_return EraseOutcome::NotFound;
        }
        auto const below = childPosition(position, bcos::ref(ext->shared));
        auto outcome =
            co_await mergeErase(ctx, ext->child, below, path.getCroppedData(ext->shared.size()));
        if (outcome == EraseOutcome::Deleted)
        {
            node->dirty = true;
            if (std::holds_alternative<std::monostate>(ext->child))
            {
                slot = std::monostate{};  // whole extension gone with its only subtree
            }
            else
            {
                co_await mergeNormalize(ctx, *node, position);
            }
        }
        co_return outcome;
    }

    auto& branch = std::get<MutableBranch>(node->node);
    if (path.empty())
    {
        BOOST_THROW_EXCEPTION(MPTInvariantViolation{}
                              << bcos::errinfo_comment("mergeTrie: path exhausted at a branch"));
    }
    auto const below = childPosition(position, path[0]);
    auto outcome =
        co_await mergeErase(ctx, branch.children[path[0]], below, path.getCroppedData(1));
    if (outcome == EraseOutcome::Deleted)
    {
        node->dirty = true;
        co_await mergeNormalize(ctx, *node, position);
    }
    co_return outcome;
}

// ── Emit ───────────────────────────────────────────────────────────────────────────────────────

// Encode the overlay bottom-up, carrying each node's POSITION down by the three rules of spec A.1.
// Clean references splice back verbatim; only in-memory nodes are re-encoded (and recorded into
// newNodes, keyed by position, when their encoding is hash-kind — an encoding under 32 bytes is
// inlined into its parent and owns no row).
// The hasher is injected (NodeEncoder's convention): generic over the Hasher concept — keccak256
// on Ethereum-compatible chains, SM3 on guomi ones — owned by the caller and reused across every
// emit of this merge; never constructed down here.
template <bcos::crypto::hasher::Hasher HasherT>
struct EmitContext
{
    HasherT& hasher;
    std::map<bcos::bytes, bcos::bytes> newNodes;  ///< position → raw RLP

    NodeRef emit(bcos::bytes position, TrieNode const& node)
    {
        auto [raw, ref] = NodeEncoder<HasherT>::encodeAndRef(node, hasher);
        if (ref.kind() == NodeRef::Kind::Hash)
        {
            newNodes.insert_or_assign(std::move(position), std::move(raw));
        }
        return ref;
    }
};

template <bcos::crypto::hasher::Hasher HasherT>
NodeRef emitNode(EmitContext<HasherT>& ctx, MutableNode& node,  // NOLINT(misc-no-recursion)
    bcos::bytes const& position);

// The last link of the zero-I/O guarantee: a slot still holding a clean NodeRef is returned
// verbatim — that whole prior-version subtree is never read, hashed, or re-written, and (spec
// A.5) it stays exactly where it was, so not one of its rows needs touching. Only boxed
// (resolved) nodes recurse into emitNode. Absent (monostate) slots are the branch loop's case.
template <bcos::crypto::hasher::Hasher HasherT>
NodeRef emitChild(EmitContext<HasherT>& ctx, MutableChild& child, bcos::bytes const& position)
{
    if (auto* ref = std::get_if<NodeRef>(&child))
    {
        return *ref;
    }
    return emitNode(ctx, *std::get<std::unique_ptr<MutableNode>>(child), position);
}

template <bcos::crypto::hasher::Hasher HasherT>
NodeRef emitNode(EmitContext<HasherT>& ctx, MutableNode& node,  // NOLINT(misc-no-recursion)
    bcos::bytes const& position)
{
    if (auto* leaf = std::get_if<MutableLeaf>(&node.node))
    {
        return ctx.emit(position, TrieNode{LeafNode{.keyNibbles = std::move(leaf->suffix),
                                      .value = std::move(leaf->value)}});
    }
    if (auto* ext = std::get_if<MutableExtension>(&node.node))
    {
        NodeRef const childRef =
            emitChild(ctx, ext->child, childPosition(position, bcos::ref(ext->shared)));
        return ctx.emit(position, TrieNode{ExtensionNode{.sharedNibbles = std::move(ext->shared),
                                      .child = refToRawBytes(childRef)}});
    }
    auto& branch = std::get<MutableBranch>(node.node);
    BranchNode out;
    for (size_t i = 0; i < NIBBLE_RANGE; ++i)
    {
        if (!std::holds_alternative<std::monostate>(branch.children[i]))
        {
            out.children[i] = emitChild(
                ctx, branch.children[i], childPosition(position, static_cast<bcos::byte>(i)));
        }
    }
    return ctx.emit(position, TrieNode{std::move(out)});
}

}  // namespace detail

/// Incrementally rebuild the trie @p scope rooted at @p priorRoot by applying @p changes
/// (value = put, nullopt = delete; deleting an absent key is a no-op). Only nodes on changed paths
/// are read from @p storage; untouched subtrees are re-referenced by hash without being loaded
/// (the PrefixSet-style path-level increment of spec §5.3 path 1 step 2).
///
/// The returned diff is row-level and complete for this trie: upserts are the positions the new
/// version occupies with new bytes, deletes the positions it no longer occupies, preimages what
/// each of those held before. Storage is only READ — applying the diff is commitTrie's caller's
/// job — and so is @p hasher, reused across every node emission and every read verification of
/// this merge. @p hasher is any type satisfying the Hasher concept (keccak256, SM3, ...); it is
/// single-threaded state, same contract as NodeEncoder's injection overload.
///
/// @throws MPTInvariantViolation when a node the prior version references is missing from
///         @p storage or its bytes disagree with the hash its parent recorded.
template <bcos::storage2::ReadableStorage<PathKey> Storage, bcos::crypto::hasher::Hasher HasherT>
bcos::task::Task<PathMergeResult> mergeTrie(Storage& storage, TrieScope scope, bcos::h256 priorRoot,
    std::map<bcos::h256, std::optional<bcos::bytes>> const& changes, HasherT& hasher)
{
    detail::MergeContext<Storage, HasherT> ctx{.storage = storage,
        .scope = std::move(scope),
        .hasher = hasher,
        .resolvedPositions = {},
        .priorBytes = {}};

    // The overlay root starts as a clean reference to the prior root node at position "";
    // nothing is read yet.
    detail::MutableChild root{NodeRef::fromHash(priorRoot)};
    bcos::bytes const rootPosition;

    // Phase 1 — apply. std::map iteration = h256 lexicographic = 64-nibble path order, so keys
    // sharing a prefix arrive consecutively and hit the overlay's memoized nodes. The concrete
    // std::map parameter is deliberate: the type pins ascending order and key uniqueness in the
    // signature; a generic kv range would move both invariants into caller documentation.
    for (auto const& [keyHash, valueOpt] : changes)
    {
        auto const path = bytesToNibbles(keyHash.ref());
        if (valueOpt.has_value())
        {
            co_await detail::mergeInsert(ctx, root, rootPosition, bcos::ref(path), *valueOpt);
        }
        else
        {
            // The outcome is deliberately ignored: deleting an absent key is a legal no-op.
            co_await detail::mergeErase(ctx, root, rootPosition, bcos::ref(path));
        }
    }

    // Phase 2 — classify the overlay root; phase 3 (emit) only runs for the last case.
    PathMergeResult result;
    std::map<bcos::bytes, bcos::bytes> newNodes;
    if (std::holds_alternative<std::monostate>(root))
    {
        // The deletes emptied the whole trie. Nothing to emit; every position we read falls
        // through to the subtraction below and is deleted — and that is exactly the whole trie,
        // because emptying it required erasing every leaf, and every internal node lies on some
        // leaf's path. The sentinel is HasherT's empty root — plain emptyRootHash() would
        // silently pick the keccak default.
        result.root = emptyRootHash<HasherT>();
    }
    else if (std::holds_alternative<NodeRef>(root) ||
             !std::get<std::unique_ptr<detail::MutableNode>>(root)->dirty)
    {
        // Nothing changed (every change was a miss): the prior version stands as-is. Return
        // BEFORE the subtraction — nodes resolved along missed paths were not replaced, and with
        // no re-emit to subtract against every one of them would be wrongly deleted.
        result.root = priorRoot;
        co_return result;
    }
    else
    {
        // Phase 3 — re-encode the overlay bottom-up; clean subtrees splice back by hash.
        detail::EmitContext<HasherT> emitCtx{.hasher = hasher, .newNodes = {}};
        NodeRef const rootRef = detail::emitNode(
            emitCtx, *std::get<std::unique_ptr<detail::MutableNode>>(root), rootPosition);
        // A trie root is ALWAYS addressed by a 32-byte hash, even when the top node encodes to
        // fewer than 32 bytes (same rule as computeTrieRoot) — and it always occupies position
        // "", which is the fixed entry point every reader starts from.
        if (rootRef.kind() == NodeRef::Kind::Hash)
        {
            result.root = rootRef.hash();
        }
        else
        {
            bcos::crypto::hasher::hash(emitCtx.hasher, rootRef.inlineRef(), result.root);
            emitCtx.newNodes.insert_or_assign(rootPosition, rootRef.inlineRef().toBytes());
        }
        newNodes = std::move(emitCtx.newNodes);
    }

    // Settle the rows (see the phase notes at the top of detail): a position we read and did not
    // re-emit no longer holds a node.
    for (auto& [position, raw] : newNodes)
    {
        auto prior = ctx.priorBytes.find(position);
        result.preimages.emplace(PathKey{.scope = ctx.scope, .position = position},
            prior == ctx.priorBytes.end() ? std::nullopt :
                                            std::optional<bcos::bytes>{prior->second});
        result.upserts.emplace(PathKey{.scope = ctx.scope, .position = position}, std::move(raw));
    }
    for (auto const& position : ctx.resolvedPositions)
    {
        if (newNodes.contains(position))
        {
            continue;
        }
        PathKey key{.scope = ctx.scope, .position = position};
        // Every resolved position has prior bytes by construction (mergeResolve records both).
        result.preimages.emplace(key, ctx.priorBytes.at(position));
        result.deletes.emplace(std::move(key));
    }
    co_return result;
}

}  // namespace bcos::ledger::mpt
