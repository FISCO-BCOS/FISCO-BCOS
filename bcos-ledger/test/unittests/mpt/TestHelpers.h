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
 * @file TestHelpers.h
 * @brief Shared MPT test utilities + an independent reference trie oracle (spec §5.3, §5.5)
 */
#pragma once

#include <bcos-crypto/hasher/AnyHasher.h>
#include <bcos-crypto/hasher/OpenSSLHasher.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-ledger/mpt/Classify.h>
#include <bcos-ledger/mpt/Constants.h>
#include <bcos-ledger/mpt/HashBuilder.h>
#include <bcos-ledger/mpt/Nibble.h>
#include <bcos-ledger/mpt/NodeEncoder.h>
#include <bcos-ledger/mpt/TrieNode.h>
#include <bcos-task/Task.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace bcos::ledger::mpt::test
{

/// A zero h256 with only its first byte set to @p firstByte.
inline bcos::h256 makeHash(uint8_t firstByte)
{
    bcos::h256 h{};
    h.data()[0] = firstByte;
    return h;
}

/// Fixed-seed RNG so random-batch tests are reproducible.
inline std::mt19937 seededRng(uint32_t s)
{
    return std::mt19937{s};
}

/// Synchronous test wrapper around commitTrie(): commit @p changes over @p priorRoot AND flush
/// the produced nodes into @p storage. Production flushes once per block from the aggregated
/// MPTDeltaLayer; tests flush per build so readback (Trie / MPTReadView) works immediately.
template <typename Storage>
TrieMergeResult commitTrieFlushed(Storage& storage, bcos::h256 priorRoot,
    std::map<bcos::h256, std::optional<bcos::bytes>> const& changes)
{
    auto result = bcos::task::syncWait(commitTrie(storage, priorRoot, changes));
    bcos::task::syncWait(flushTrieNodes(storage, result.newNodes));
    return result;
}

/// commitTrieFlushed over insert-only entries (no deletes) — the common baseline-building shape.
/// A distinct name, not an overload: a braced-init-list argument could match either map type.
template <typename Storage>
TrieMergeResult seedTrieFlushed(
    Storage& storage, bcos::h256 priorRoot, std::map<bcos::h256, bcos::bytes> const& entries)
{
    std::map<bcos::h256, std::optional<bcos::bytes>> changes;
    for (auto const& [key, value] : entries)
    {
        changes[key] = value;
    }
    return commitTrieFlushed(storage, priorRoot, changes);
}

// ---------------------------------------------------------------------------
// Account / classify test helpers (PR-08+)
// ---------------------------------------------------------------------------

/// A 20-byte address with only its first byte set to @p firstByte.
inline bcos::Address makeAddress(uint8_t firstByte)
{
    bcos::Address addr{};
    addr.data()[0] = firstByte;
    return addr;
}

/// A NORMAL Entry holding @p value as its raw stored bytes.
inline bcos::storage::Entry makeEntry(std::string_view value)
{
    bcos::storage::Entry entry;
    entry.set(std::string(value));
    return entry;
}

// ---------------------------------------------------------------------------
// Flat-state fixture (Revision 2026-07-09b): MPTBuilder consumes the block's fork view, so the
// tests feed it the production-shaped layers instead of hand-filled AccountDelta structs.
// ---------------------------------------------------------------------------

/// The block's delta layer — same MemoryStorage shape as production
/// (libinitializer/GlobalStateStorageInitializer.h:14-17).
using FlatMutableStorage =
    bcos::storage2::memory_storage::MemoryStorage<bcos::executor_v1::StateKey,
        bcos::executor_v1::StateValue,
        bcos::storage2::memory_storage::ORDERED | bcos::storage2::memory_storage::LOGICAL_DELETION>;
/// The parent flat state the view reads through to (first-touch metadata).
using FlatBackendStorage =
    bcos::storage2::memory_storage::MemoryStorage<bcos::executor_v1::StateKey,
        bcos::executor_v1::StateValue, bcos::storage2::memory_storage::ORDERED>;
/// The production View template over the two layers (what coCommitBlock forks).
using FlatStateView = bcos::storage2::View<FlatMutableStorage, void, FlatBackendStorage>;

/// A view over @p backend with a fresh mutable delta layer, ready for this block's writes.
inline FlatStateView makeFlatView(FlatBackendStorage& backend)
{
    FlatStateView view(&backend);
    view.newMutable();
    return view;
}

/// "<table>:<row>" StateKey for a named account field row.
inline bcos::executor_v1::StateKey accountFieldKey(bcos::Address const& addr, std::string_view row)
{
    return bcos::executor_v1::StateKey{accountTableName(addr), row};
}

/// "<table>:<32-byte-binary-slot>" StateKey for a storage slot row.
inline bcos::executor_v1::StateKey accountSlotKey(bcos::Address const& addr, bcos::h256 const& slot)
{
    return bcos::executor_v1::StateKey{accountTableName(addr),
        std::string_view{reinterpret_cast<char const*>(slot.data()), bcos::h256::SIZE}};
}

/// Write one row into @p target (a view's delta layer or a backend flat state).
inline void writeFlatRow(auto& target, bcos::executor_v1::StateKey key, bcos::storage::Entry entry)
{
    bcos::task::syncWait(bcos::storage2::writeOne(target, std::move(key), std::move(entry)));
}

/// Delete one row via storage-level logical deletion (the removeSome path): the delta layer
/// keeps the key with a DELETED_TYPE marker — the deletion signal MPTBuilder recognizes.
inline void deleteFlatRowLogically(FlatStateView& view, bcos::executor_v1::StateKey key)
{
    bcos::task::syncWait(bcos::storage2::removeOne(mutableStorage(view), std::move(key)));
}

// ---------------------------------------------------------------------------
// Independent reference trie (correctness oracle).
//
// This is deliberately a DIFFERENT algorithm than commitTrie's build cores: it inserts keys one
// at a time into a mutable pointer tree (leaf-split-on-first-differing-nibble, descend through
// extension/branch), then encodes the finished tree through the trusted NodeEncoder and hashes
// the root exactly like commitTrie does. A bug shared by both implementations is therefore
// unlikely.
// ---------------------------------------------------------------------------
namespace reftrie
{

struct RefNode;
using RefNodePtr = std::shared_ptr<RefNode>;

// A mutable node. Empty = null pointer.
//   Leaf:      keyNibbles (suffix) + value
//   Extension: sharedNibbles + a single child node
//   Branch:    16 child nodes (+ value, unused here since all keys are 64 nibbles)
struct RefNode
{
    enum class Kind
    {
        Leaf,
        Extension,
        Branch
    };
    Kind kind{Kind::Leaf};
    bcos::bytes path;                             // leaf keyNibbles or extension sharedNibbles
    bcos::bytes value;                            // leaf/branch value
    RefNodePtr extChild;                          // extension child
    std::array<RefNodePtr, 16> branchChildren{};  // branch children
};

inline RefNodePtr makeLeaf(bcos::bytes path, bcos::bytes value)
{
    auto n = std::make_shared<RefNode>();
    n->kind = RefNode::Kind::Leaf;
    n->path = std::move(path);
    n->value = std::move(value);
    return n;
}

// Insert (suffix → value) into the subtree rooted at `node` (may be null). Returns the new subtree.
inline RefNodePtr refInsert(RefNodePtr node, bcos::bytes const& suffix, bcos::bytes const& value)
{
    if (!node)
    {
        return makeLeaf(suffix, value);
    }

    if (node->kind == RefNode::Kind::Leaf)
    {
        if (node->path == suffix)
        {
            node->value = value;  // overwrite
            return node;
        }
        size_t const cpl =
            commonPrefixLen(bcos::bytesConstRef(node->path.data(), node->path.size()),
                bcos::bytesConstRef(suffix.data(), suffix.size()));
        // Build a branch at depth cpl holding the two distinct suffixes.
        auto branch = std::make_shared<RefNode>();
        branch->kind = RefNode::Kind::Branch;
        {
            bcos::bytes existingRest(node->path.begin() + cpl, node->path.end());
            bcos::byte n0 = existingRest.front();
            branch->branchChildren[n0] =
                makeLeaf(bcos::bytes(existingRest.begin() + 1, existingRest.end()), node->value);
        }
        {
            bcos::bytes newRest(suffix.begin() + cpl, suffix.end());
            bcos::byte n1 = newRest.front();
            branch->branchChildren[n1] =
                makeLeaf(bcos::bytes(newRest.begin() + 1, newRest.end()), value);
        }
        if (cpl == 0)
        {
            return branch;
        }
        auto ext = std::make_shared<RefNode>();
        ext->kind = RefNode::Kind::Extension;
        ext->path.assign(suffix.begin(), suffix.begin() + cpl);
        ext->extChild = branch;
        return ext;
    }

    if (node->kind == RefNode::Kind::Extension)
    {
        size_t const cpl =
            commonPrefixLen(bcos::bytesConstRef(node->path.data(), node->path.size()),
                bcos::bytesConstRef(suffix.data(), suffix.size()));
        if (cpl == node->path.size())
        {
            // Descend fully through the extension.
            bcos::bytes rest(suffix.begin() + cpl, suffix.end());
            node->extChild = refInsert(node->extChild, rest, value);
            return node;
        }
        // Split the extension at cpl into a branch.
        auto branch = std::make_shared<RefNode>();
        branch->kind = RefNode::Kind::Branch;
        // Existing extension's remaining path → a child of the branch.
        {
            bcos::bytes existingRest(node->path.begin() + cpl, node->path.end());
            bcos::byte n0 = existingRest.front();
            if (existingRest.size() == 1)
            {
                branch->branchChildren[n0] = node->extChild;
            }
            else
            {
                auto innerExt = std::make_shared<RefNode>();
                innerExt->kind = RefNode::Kind::Extension;
                innerExt->path.assign(existingRest.begin() + 1, existingRest.end());
                innerExt->extChild = node->extChild;
                branch->branchChildren[n0] = innerExt;
            }
        }
        // New suffix → another child of the branch.
        {
            bcos::bytes newRest(suffix.begin() + cpl, suffix.end());
            bcos::byte n1 = newRest.front();
            branch->branchChildren[n1] = refInsert(
                branch->branchChildren[n1], bcos::bytes(newRest.begin() + 1, newRest.end()), value);
        }
        if (cpl == 0)
        {
            return branch;
        }
        auto ext = std::make_shared<RefNode>();
        ext->kind = RefNode::Kind::Extension;
        ext->path.assign(suffix.begin(), suffix.begin() + cpl);
        ext->extChild = branch;
        return ext;
    }

    // Branch: route by the first nibble.
    bcos::byte const n = suffix.front();
    node->branchChildren[n] =
        refInsert(node->branchChildren[n], bcos::bytes(suffix.begin() + 1, suffix.end()), value);
    return node;
}

// Encode a reference subtree to a NodeRef via the trusted NodeEncoder.
inline NodeRef refEncode(
    RefNodePtr const& node, bcos::crypto::hasher::openssl::OpenSSL_Keccak256_Hasher& hasher)
{
    if (!node)
    {
        return NodeRef::absent();
    }
    if (node->kind == RefNode::Kind::Leaf)
    {
        LeafNode leaf;
        leaf.keyNibbles = node->path;
        leaf.value = node->value;
        return NodeEncoder<>::encodeAndRef(TrieNode{leaf}, hasher).second;
    }
    if (node->kind == RefNode::Kind::Extension)
    {
        NodeRef const childRef = refEncode(node->extChild, hasher);
        bcos::bytes childRaw;
        if (childRef.kind() == NodeRef::Kind::Inline)
        {
            childRaw = childRef.inlineRef().toBytes();
        }
        else
        {
            childRaw.push_back(0xa0);
            childRaw.insert(childRaw.end(), childRef.payload.begin(), childRef.payload.end());
        }
        ExtensionNode ext;
        ext.sharedNibbles = node->path;
        ext.child = childRaw;
        return NodeEncoder<>::encodeAndRef(TrieNode{ext}, hasher).second;
    }
    BranchNode branch;
    for (size_t i = 0; i < 16; ++i)
    {
        branch.children[i] = refEncode(node->branchChildren[i], hasher);
    }
    branch.value = node->value;
    return NodeEncoder<>::encodeAndRef(TrieNode{branch}, hasher).second;
}

}  // namespace reftrie

/// Build the canonical MPT root for @p kvs by one-at-a-time insertion into a mutable reference
/// trie, then encoding through the trusted NodeEncoder. Used as the correctness oracle.
inline bcos::h256 referenceRoot(std::vector<std::pair<bcos::h256, bcos::bytes>> const& kvs)
{
    if (kvs.empty())
    {
        return emptyRootHash();
    }
    reftrie::RefNodePtr rootNode;
    for (auto const& [key, value] : kvs)
    {
        bcos::bytes const nibbles = bytesToNibbles(key.ref());
        rootNode = reftrie::refInsert(rootNode, nibbles, value);
    }

    bcos::crypto::hasher::openssl::OpenSSL_Keccak256_Hasher hasher;
    NodeRef const rootRef = reftrie::refEncode(rootNode, hasher);
    bcos::h256 root;
    if (rootRef.kind() == NodeRef::Kind::Hash)
    {
        root = rootRef.hash();
    }
    else
    {
        bcos::crypto::hasher::hash(hasher, rootRef.inlineRef(), root);
    }
    return root;
}

}  // namespace bcos::ledger::mpt::test
