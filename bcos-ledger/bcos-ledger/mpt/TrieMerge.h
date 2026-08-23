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
 * @brief Path-level incremental MPT rebuild on a non-empty prior root (spec §5.3 path 1, §5.4)
 */
#pragma once

#include "Constants.h"
#include "Errors.h"
#include "Nibble.h"
#include "NodeDecoder.h"
#include "NodeEncoder.h"
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
#include <map>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <variant>

namespace bcos::ledger::mpt
{

/// Result of one incremental rebuild: the new 32-byte root, the hash-keyed RLP encodings of every
/// node emitted for the new trie version, and the hashes of prior-version nodes this rebuild
/// replaced (a ledger for future pruning — nothing is deleted from storage here).
struct TrieMergeResult
{
    bcos::h256 root;
    std::unordered_map<bcos::h256, bcos::bytes> newNodes;
    std::unordered_set<bcos::h256> obsoletedNodes;
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
// The whole rebuild runs in four phases (see mergeTrie at the bottom of this header):
//   1. apply every change to the overlay (mergeInsert / mergeErase, resolving lazily);
//   2. classify the overlay root: emptied trie / nothing-changed short-circuit / re-emit;
//   3. re-encode only in-memory nodes bottom-up (emitNode), splicing clean refs verbatim;
//   4. settle obsoletion by end-subtraction: resolvedHashes − re-emitted identical hashes.
//
// I/O contract: reads = nodes on changed key paths (each at most once) + one unavoidable probe
// per branch collapse (mergeNormalize); everything else is spliced back by hash, unread. This
// header only READS storage — flushing newNodes is commitTrie's caller's job (MPTBuilder
// batches one flushTrieNodes per block).
//
// Two bookkeeping fields drive correctness of the obsoletion ledger:
//   - MergeContext::resolvedHashes records every hash-addressed node loaded from the prior
//     version. Recording is NOT a verdict: a resolved node only becomes obsolete if the new
//     version does not re-emit its identical encoding (phase 4), or is explicitly un-recorded
//     when a collapse keeps it referenced un-rebuilt (mergeNormalize).
//   - MutableNode::dirty distinguishes "resolved to look at" from "actually modified". Erase
//     misses leave nodes clean, which is what makes the phase-2 short-circuit sound.

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

// Shared per-merge state: storage to resolve prior-version nodes from, and the set of hashes
// resolved so far. Obsoletion is settled at the end (see mergeTrie), not per resolve.
template <bcos::storage2::ReadableStorage<bcos::h256> Storage>
struct MergeContext
{
    std::reference_wrapper<Storage> storage;
    std::unordered_set<bcos::h256> resolvedHashes;
};

// Ensure @p slot holds an in-memory node, loading and decoding the prior-version node when the
// slot is a clean hash/inline reference. Absent slots are the caller's case to handle.
//
// This is the ONLY place the algorithm reads storage. Three cases:
//   - already boxed: return it — zero I/O. Because the resolve below REPLACES the slot with the
//     decoded node, the second key crossing this node in the same batch lands here (memoize).
//   - hash ref: readOne + decode; a miss throws (a trie referencing a missing node is storage
//     corruption, same rule as Trie::get). The hash goes into ctx.resolvedHashes — that is
//     bookkeeping, not an obsoletion verdict (settled in mergeTrie phase 4).
//   - inline ref: decode the embedded bytes; no hash of its own, never enters the ledger.
template <bcos::storage2::ReadableStorage<bcos::h256> Storage>
bcos::task::Task<MutableNode*> mergeResolve(MergeContext<Storage>& ctx, MutableChild& slot)
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
        auto raw = co_await bcos::storage2::readOne(ctx.storage.get(), refHash);
        if (!raw)
        {
            BOOST_THROW_EXCEPTION(MPTInvariantViolation{} << bcos::errinfo_comment(
                                      "mergeTrie: missing node hash; storage lacks a referenced "
                                      "node"));
        }
        ctx.resolvedHashes.insert(refHash);
        origin = refHash;
        decoded = decodeNode(bcos::ref(*raw));
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

// Insert (path → value) into the subtree at @p slot; @p path holds the key's not-yet-consumed
// nibbles (each recursion level strips what it matched). Textbook MPT insert, one case per
// node shape:
//   - absent slot            → new leaf carrying the whole remaining path;
//   - leaf, same key         → overwrite the value in place;
//   - leaf, diverging key    → fork into a branch at the divergence nibble (wrapped in an
//                              extension when a common prefix remains);
//   - extension, path covers the shared prefix → recurse into the child;
//   - extension, divergence inside the prefix  → split the extension; the untouched tail keeps
//                              its clean child reference — that subtree is re-parented, unread;
//   - branch                 → route by the next nibble and recurse.
// Every node on the way down is marked dirty: an insert always rebuilds its path.
template <bcos::storage2::ReadableStorage<bcos::h256> Storage>
bcos::task::Task<void> mergeInsert(
    MergeContext<Storage>& ctx, MutableChild& slot, bcos::bytesConstRef path, bcos::bytes value)
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
    MutableNode* node = co_await mergeResolve(ctx, slot);
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
        node->originHash.reset();  // structure changed; origin accounting stays in resolvedHashes
        co_return;
    }

    if (auto* ext = std::get_if<MutableExtension>(&node->node))
    {
        size_t const cpl = commonPrefixLen(bcos::ref(ext->shared), path);
        if (cpl == ext->shared.size())
        {
            co_await mergeInsert(ctx, ext->child, path.getCroppedData(cpl), std::move(value));
            co_return;
        }
        if (cpl == path.size())  // path exhausted inside the shared prefix: length mismatch
        {
            BOOST_THROW_EXCEPTION(MPTInvariantViolation{} << bcos::errinfo_comment(
                                      "mergeTrie: key length mismatch at an extension"));
        }
        // Split the extension at the divergence point. The untouched tail keeps its clean child
        // reference — nothing below is loaded.
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
    co_await mergeInsert(ctx, branch.children[path[0]], path.getCroppedData(1), std::move(value));
    co_return;
}

// ── Erase ──────────────────────────────────────────────────────────────────────────────────────

// After a child was removed below, fold degenerate shapes back into canonical form. The MPT
// canonical form forbids exactly two shapes, and this fixes both on the erase unwind path:
//   - an extension whose (rebuilt) child is now a leaf/extension → absorb it: leaf(shared+suffix)
//     or extension(shared+shared2). A child that is a branch, or still a clean ref, stays put.
//   - a branch left with a single child → that child prefixed with its nibble. The survivor must
//     be resolved once to learn its shape — the one unavoidable read of an unmodified node in
//     the whole algorithm. Survivor leaf/extension get merged (their standalone prior encoding
//     is superseded → they stay in resolvedHashes and obsolete correctly). A survivor BRANCH is
//     not rebuilt at all, only re-parented under a one-nibble extension:
//       · resolved just now and never modified (originHash && !dirty) → keep it referenced by
//         its prior hash and ERASE it from resolvedHashes — its encoding stays live, it must
//         not be obsoleted. The dirty guard is essential: a branch modified earlier in this
//         batch has diverged from originHash, reverting to the hash would drop those edits.
//       · otherwise (dirty, or inline-decoded with no hash) → keep the boxed node; the emit
//         phase re-encodes it.
// present == 0 is unreachable: an emptied subtree propagates as monostate in mergeErase, so the
// parent collapses there — a zero-child branch here means the invariants are already broken.
template <bcos::storage2::ReadableStorage<bcos::h256> Storage>
bcos::task::Task<void> mergeNormalize(MergeContext<Storage>& ctx, MutableNode& node)
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
    MutableNode* survivor = co_await mergeResolve(ctx, branch->children[lastNibble]);
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
            // Resolved only to learn its shape, never modified: its prior encoding stays live
            // under the new extension. Un-account the resolve and keep the hash reference.
            ctx.resolvedHashes.erase(*boxed->originHash);
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

// Erase @p path from the subtree at @p slot. Unlike mergeInsert, nodes are NOT marked dirty on
// the way down — only on the unwind of a confirmed Deleted. A miss (leaf mismatch, extension
// prefix divergence, absent branch child) leaves every resolved node clean, which is what lets
// mergeTrie's phase-2 short-circuit return the prior root untouched when a whole batch turns
// out to be no-ops. On Deleted the slot becomes monostate; an extension whose only subtree
// vanished propagates the monostate up, otherwise mergeNormalize repairs the degenerate shape.
template <bcos::storage2::ReadableStorage<bcos::h256> Storage>
bcos::task::Task<EraseOutcome> mergeErase(
    MergeContext<Storage>& ctx, MutableChild& slot, bcos::bytesConstRef path)
{
    if (std::holds_alternative<std::monostate>(slot))
    {
        co_return EraseOutcome::NotFound;
    }
    MutableNode* node = co_await mergeResolve(ctx, slot);

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
        auto outcome =
            co_await mergeErase(ctx, ext->child, path.getCroppedData(ext->shared.size()));
        if (outcome == EraseOutcome::Deleted)
        {
            node->dirty = true;
            if (std::holds_alternative<std::monostate>(ext->child))
            {
                slot = std::monostate{};  // whole extension gone with its only subtree
            }
            else
            {
                co_await mergeNormalize(ctx, *node);
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
    auto outcome = co_await mergeErase(ctx, branch.children[path[0]], path.getCroppedData(1));
    if (outcome == EraseOutcome::Deleted)
    {
        node->dirty = true;
        co_await mergeNormalize(ctx, *node);
    }
    co_return outcome;
}

// ── Emit ───────────────────────────────────────────────────────────────────────────────────────

// Encode the overlay bottom-up. Clean references splice back verbatim; only in-memory nodes are
// re-encoded (and recorded into newNodes when their encoding is hash-kind).
// The hasher is injected (NodeEncoder's convention): generic over the Hasher concept — keccak256
// on Ethereum-compatible chains, SM3 on guomi ones — owned by the caller and reused across every
// emit of this merge; never constructed down here.
template <bcos::crypto::hasher::Hasher HasherT>
struct EmitContext
{
    HasherT& hasher;
    std::unordered_map<bcos::h256, bcos::bytes> newNodes;

    NodeRef emit(TrieNode const& node)
    {
        auto [raw, ref] = NodeEncoder<HasherT>::encodeAndRef(node, hasher);
        if (ref.kind() == NodeRef::Kind::Hash)
        {
            newNodes.emplace(ref.hash(), std::move(raw));
        }
        return ref;
    }
};

template <bcos::crypto::hasher::Hasher HasherT>
NodeRef emitNode(EmitContext<HasherT>& ctx, MutableNode& node);  // NOLINT(misc-no-recursion)

// The last link of the zero-I/O guarantee: a slot still holding a clean NodeRef is returned
// verbatim — that whole prior-version subtree is never read, hashed, or re-written. Only boxed
// (resolved) nodes recurse into emitNode. Absent (monostate) slots are the branch loop's case.
template <bcos::crypto::hasher::Hasher HasherT>
NodeRef emitChild(EmitContext<HasherT>& ctx, MutableChild& child)
{
    if (auto* ref = std::get_if<NodeRef>(&child))
    {
        return *ref;
    }
    return emitNode(ctx, *std::get<std::unique_ptr<MutableNode>>(child));
}

template <bcos::crypto::hasher::Hasher HasherT>
NodeRef emitNode(EmitContext<HasherT>& ctx, MutableNode& node)  // NOLINT(misc-no-recursion)
{
    if (auto* leaf = std::get_if<MutableLeaf>(&node.node))
    {
        return ctx.emit(TrieNode{
            LeafNode{.keyNibbles = std::move(leaf->suffix), .value = std::move(leaf->value)}});
    }
    if (auto* ext = std::get_if<MutableExtension>(&node.node))
    {
        NodeRef const childRef = emitChild(ctx, ext->child);
        return ctx.emit(TrieNode{ExtensionNode{
            .sharedNibbles = std::move(ext->shared), .child = refToRawBytes(childRef)}});
    }
    auto& branch = std::get<MutableBranch>(node.node);
    BranchNode out;
    for (size_t i = 0; i < NIBBLE_RANGE; ++i)
    {
        if (!std::holds_alternative<std::monostate>(branch.children[i]))
        {
            out.children[i] = emitChild(ctx, branch.children[i]);
        }
    }
    return ctx.emit(TrieNode{std::move(out)});
}

}  // namespace detail

/// Incrementally rebuild the trie rooted at @p priorRoot by applying @p changes (value = put,
/// nullopt = delete; deleting an absent key is a no-op). Only nodes on changed paths are read
/// from @p storage; untouched subtrees are re-referenced by hash without being loaded (the
/// PrefixSet-style path-level increment of spec §5.3 path 1 step 2).
///
/// newNodes holds every node emitted for the new version (a no-net-change subtree re-emits its
/// identical encoding — harmless). obsoletedNodes = nodes of the prior version that the new root
/// no longer references: exactly the resolved-and-replaced set minus re-emitted identical hashes.
/// Storage is only read; commitTrie's caller owns flushing newNodes (flushTrieNodes) — and owns
/// @p hasher, reused across every node emission of this merge (and across merges, if the caller
/// keeps it around). @p hasher is any type satisfying the Hasher concept (keccak256, SM3, ...);
/// it is single-threaded state, same contract as NodeEncoder's injection overload.
template <bcos::storage2::ReadableStorage<bcos::h256> Storage, bcos::crypto::hasher::Hasher HasherT>
bcos::task::Task<TrieMergeResult> mergeTrie(Storage& storage, bcos::h256 priorRoot,
    std::map<bcos::h256, std::optional<bcos::bytes>> const& changes, HasherT& hasher)
{
    detail::MergeContext<Storage> ctx{.storage = storage, .resolvedHashes = {}};

    // The overlay root starts as a clean reference to the prior root node; nothing is read yet.
    detail::MutableChild root{NodeRef::fromHash(priorRoot)};

    // Phase 1 — apply. std::map iteration = h256 lexicographic = 64-nibble path order, so keys
    // sharing a prefix arrive consecutively and hit the overlay's memoized nodes. The concrete
    // std::map parameter is deliberate: the type pins ascending order and key uniqueness in the
    // signature; a generic kv range would move both invariants into caller documentation.
    for (auto const& [keyHash, valueOpt] : changes)
    {
        auto const path = bytesToNibbles(keyHash.ref());
        if (valueOpt.has_value())
        {
            co_await detail::mergeInsert(ctx, root, bcos::ref(path), *valueOpt);
        }
        else
        {
            // The outcome is deliberately ignored: deleting an absent key is a legal no-op.
            co_await detail::mergeErase(ctx, root, bcos::ref(path));
        }
    }

    // Phase 2 — classify the overlay root; phase 3 (emit) only runs for the last case.
    TrieMergeResult result;
    if (std::holds_alternative<std::monostate>(root))
    {
        // The deletes emptied the whole trie. Nothing to emit; every resolved node (including
        // the prior root) falls through to phase 4 and obsoletes. The sentinel is HasherT's
        // empty root — plain emptyRootHash() would silently pick the keccak default.
        result.root = emptyRootHash<HasherT>();
    }
    else if (std::holds_alternative<NodeRef>(root) ||
             !std::get<std::unique_ptr<detail::MutableNode>>(root)->dirty)
    {
        // Nothing changed (every change was a miss): the prior version stands as-is. Return
        // BEFORE phase 4 — nodes resolved along missed paths were not replaced, and without a
        // re-emit to subtract against, the end-subtraction would wrongly obsolete them all.
        result.root = priorRoot;
        co_return result;
    }
    else
    {
        // Phase 3 — re-encode the overlay bottom-up; clean subtrees splice back by hash.
        detail::EmitContext<HasherT> emitCtx{.hasher = hasher, .newNodes = {}};
        NodeRef const rootRef =
            detail::emitNode(emitCtx, *std::get<std::unique_ptr<detail::MutableNode>>(root));
        // A trie root is ALWAYS addressed by a 32-byte hash, even when the top node encodes
        // to fewer than 32 bytes (same rule as computeTrieRoot).
        if (rootRef.kind() == NodeRef::Kind::Hash)
        {
            result.root = rootRef.hash();
        }
        else
        {
            bcos::crypto::hasher::hash(emitCtx.hasher, rootRef.inlineRef(), result.root);
            emitCtx.newNodes.emplace(result.root, rootRef.inlineRef().toBytes());
        }
        result.newNodes = std::move(emitCtx.newNodes);
    }

    // Phase 4 — settle obsoletion by end-subtraction: every prior-version node we resolved was
    // rebuilt or absorbed — unless the rebuild re-emitted the identical encoding (same keccak,
    // so it shows up in newNodes), in which case it is still live in the new version. This one
    // subtraction covers every "changed but ended up equal" corner (no-op puts, miss paths)
    // without any per-splice old-vs-new comparison; the collapse-survivor case was already
    // un-recorded in mergeNormalize.
    for (auto const& hash : ctx.resolvedHashes)
    {
        if (!result.newNodes.contains(hash))
        {
            result.obsoletedNodes.insert(hash);
        }
    }
    co_return result;
}

}  // namespace bcos::ledger::mpt
