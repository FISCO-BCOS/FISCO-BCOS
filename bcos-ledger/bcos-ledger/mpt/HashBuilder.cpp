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
 * @file HashBuilder.cpp
 * @brief Canonical MPT construction from a key-set; computes the 32-byte state root (spec §5.3)
 */

#include "HashBuilder.h"
#include "Constants.h"
#include "Errors.h"
#include "Nibble.h"
#include "NodeEncoder.h"
#include <bcos-crypto/hasher/AnyHasher.h>
#include <bcos-crypto/hasher/OpenSSLHasher.h>
#include <span>
#include <utility>
#include <vector>

namespace bcos::ledger::mpt
{

namespace
{

// One sorted entry: the full 64-nibble path and its value. Built once per key in commit().
struct HBEntry
{
    bcos::bytes nibbles;  // exactly 64 nibbles (the key's bytesToNibbles form)
    bcos::bytes value;
};

// State threaded through the synchronous recursion. emit() records every hash-kind node into
// newNodes; commit() flushes them into the cache afterwards (avoids coroutine recursion).
struct HBContext
{
    bcos::crypto::hasher::openssl::OpenSSL_Keccak256_Hasher& hasher;
    std::unordered_map<bcos::h256, bcos::bytes>& newNodes;
};

// Encode `node`, compute its ref, and record the raw bytes when the ref is a hash.
NodeRef hbEmit(HBContext& ctx, TrieNode const& node)
{
    auto [raw, ref] = NodeEncoder<>::encodeAndRef(node, ctx.hasher);
    if (ref.kind() == NodeRef::Kind::Hash)
    {
        ctx.newNodes.emplace(ref.hash(), std::move(raw));
    }
    return ref;
}

// Turn a NodeRef into the bytes an ExtensionNode.child expects: inline → raw bytes; hash → the
// 33-byte RLP byte-string 0xa0 + hash.
bcos::bytes hbRefToRaw(NodeRef const& ref)
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

NodeRef hbBuild(HBContext& ctx, std::span<HBEntry const> entries, size_t depth);

// Keys differ at `depth` (common prefix at this depth is empty) → a 16-way branch. Entries are
// sorted, so equal nibble-at-depth values form contiguous groups.
NodeRef hbBuildBranch(HBContext& ctx, std::span<HBEntry const> entries, size_t depth)
{
    BranchNode branch;
    size_t i = 0;
    while (i < entries.size())
    {
        bcos::byte const nibble = entries[i].nibbles[depth];
        size_t j = i + 1;
        while (j < entries.size() && entries[j].nibbles[depth] == nibble)
        {
            ++j;
        }
        branch.children[nibble] = hbBuild(ctx, entries.subspan(i, j - i), depth + 1);
        i = j;
    }
    // With distinct 64-nibble keys, no key terminates before depth 64 → branch value stays empty.
    return hbEmit(ctx, TrieNode{std::move(branch)});
}

// Build the subtree covering `entries`, whose paths all agree on the first `depth` nibbles.
NodeRef hbBuild(HBContext& ctx, std::span<HBEntry const> entries, size_t depth)
{
    if (entries.size() == 1)
    {
        const auto& [nibbles, value] = entries.front();
        LeafNode leaf;
        // keyNibbles is the SUFFIX from `depth` to the end (64), not the full key.
        leaf.keyNibbles.assign(nibbles.begin() + depth, nibbles.end());
        leaf.value = value;
        return hbEmit(ctx, TrieNode{std::move(leaf)});
    }

    // entries are sorted → the longest common prefix at `depth` is shared by first and last.
    bcos::bytesConstRef const firstSuffix(
        entries.front().nibbles.data() + depth, entries.front().nibbles.size() - depth);
    bcos::bytesConstRef const lastSuffix(
        entries.back().nibbles.data() + depth, entries.back().nibbles.size() - depth);

    if (size_t const cpl = commonPrefixLen(firstSuffix, lastSuffix); cpl > 0)
    {
        // Shared prefix → extension over [depth, depth+cpl) pointing at a branch below it.
        NodeRef const childRef = hbBuildBranch(ctx, entries, depth + cpl);
        ExtensionNode ext;
        ext.sharedNibbles.assign(
            entries.front().nibbles.begin() + depth, entries.front().nibbles.begin() + depth + cpl);
        ext.child = hbRefToRaw(childRef);
        return hbEmit(ctx, TrieNode{std::move(ext)});
    }

    return hbBuildBranch(ctx, entries, depth);
}

}  // namespace

namespace
{
// File-local core: build over already-normalised, sorted, unique (keyHash, value-view) entries.
// Stateless and reentrant (owns its hasher, touches no shared state) → independent inputs may be
// built concurrently.
TrieBuildResult computeTrieRootImpl(
    std::span<std::pair<bcos::h256, bcos::bytesConstRef> const> sortedEntries)
{
    if (sortedEntries.empty())
    {
        return TrieBuildResult{.root = emptyRootHash(), .newNodes = {}};
    }

    std::vector<HBEntry> entries;
    entries.reserve(sortedEntries.size());
    for (auto const& [keyHash, value] : sortedEntries)
    {
        entries.push_back(
            HBEntry{.nibbles = bytesToNibbles(keyHash.ref()), .value = value.toBytes()});
    }

    // Own hasher + own newNodes → no shared state: many computeTrieRootImpl calls may run
    // concurrently for independent inputs (coarse-grained parallelism across tries).
    bcos::crypto::hasher::openssl::OpenSSL_Keccak256_Hasher hasher;
    std::unordered_map<bcos::h256, bcos::bytes> newNodes;
    HBContext ctx{.hasher = hasher, .newNodes = newNodes};
    NodeRef const rootRef =
        hbBuild(ctx, std::span<HBEntry const>{entries.data(), entries.size()}, /*depth=*/0);

    // A trie root is ALWAYS a 32-byte hash, even when the top node encodes to < 32 bytes.
    bcos::h256 root;
    if (rootRef.kind() == NodeRef::Kind::Hash)
    {
        root = rootRef.hash();
    }
    else
    {
        bcos::crypto::hasher::hash(hasher, rootRef.inlineRef(), root);
        newNodes.emplace(root, rootRef.inlineRef().toBytes());
    }

    return TrieBuildResult{.root = root, .newNodes = std::move(newNodes)};
}
}  // namespace

TrieBuildResult computeTrieRoot(std::map<bcos::h256, bcos::bytes> const& entries)
{
    std::vector<std::pair<bcos::h256, bcos::bytesConstRef>> normalized;
    normalized.reserve(entries.size());
    for (auto const& [key, value] : entries)
    {
        normalized.emplace_back(key, bcos::ref(value));
    }
    return computeTrieRootImpl(normalized);
}

TrieBuildResult computeTrieRootFromSorted(
    std::span<std::pair<bcos::h256, bcos::bytesConstRef> const> sortedEntries)
{
    return computeTrieRootImpl(sortedEntries);
}

}  // namespace bcos::ledger::mpt
