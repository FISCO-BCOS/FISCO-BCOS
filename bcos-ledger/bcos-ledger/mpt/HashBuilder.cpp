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
#include <span>
#include <utility>

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
    if (ref.kind == NodeRef::Kind::Hash)
    {
        ctx.newNodes.emplace(ref.hash, std::move(raw));
    }
    return ref;
}

// Turn a NodeRef into the bytes an ExtensionNode.child expects: inline → raw bytes; hash → the
// 33-byte RLP byte-string 0xa0 + hash.
bcos::bytes hbRefToRaw(NodeRef const& ref)
{
    if (ref.kind == NodeRef::Kind::Inline)
    {
        return ref.inlineBytes;
    }
    bcos::bytes out;
    out.reserve(1 + bcos::h256::SIZE);
    out.push_back(0xa0);
    out.insert(out.end(), ref.hash.begin(), ref.hash.end());
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
        HBEntry const& only = entries.front();
        LeafNode leaf;
        // keyNibbles is the SUFFIX from `depth` to the end (64), not the full key.
        leaf.keyNibbles.assign(only.nibbles.begin() + depth, only.nibbles.end());
        leaf.value = only.value;
        return hbEmit(ctx, TrieNode{std::move(leaf)});
    }

    // entries are sorted → the longest common prefix at `depth` is shared by first and last.
    bcos::bytesConstRef const firstSuffix(
        entries.front().nibbles.data() + depth, entries.front().nibbles.size() - depth);
    bcos::bytesConstRef const lastSuffix(
        entries.back().nibbles.data() + depth, entries.back().nibbles.size() - depth);
    size_t const cpl = commonPrefixLen(firstSuffix, lastSuffix);

    if (cpl > 0)
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

HashBuilder::HashBuilder(NodeCache& cache, bcos::h256 priorRoot)
  : m_cache(cache), m_priorRoot(priorRoot)
{}

bcos::task::Task<void> HashBuilder::put(bcos::h256 const& keyHash, bcos::bytes value)
{
    m_changes[keyHash] = std::move(value);
    co_return;
}

bcos::task::Task<void> HashBuilder::remove(bcos::h256 const& keyHash)
{
    m_changes[keyHash] = std::nullopt;
    co_return;
}

bcos::task::Task<bcos::h256> HashBuilder::commit()
{
    if (m_priorRoot != emptyRootHash())
    {
        BOOST_THROW_EXCEPTION(
            MPTInvariantViolation{} << bcos::errinfo_comment(
                "HashBuilder incremental rebuild on non-empty root is implemented in M4 (PR-10)"));
    }

    // 1) Drop deletes (no-op from empty); collect survivors in sorted order (map is sorted).
    std::vector<HBEntry> entries;
    entries.reserve(m_changes.size());
    for (auto const& [key, maybeValue] : m_changes)
    {
        if (maybeValue.has_value())
        {
            entries.push_back(HBEntry{bytesToNibbles(key.ref()), *maybeValue});
        }
    }
    if (entries.empty())
    {
        co_return emptyRootHash();
    }

    // 2) Build the canonical node tree synchronously, recording hash-kind nodes.
    HBContext ctx{m_hasher, m_newNodes};
    NodeRef const rootRef =
        hbBuild(ctx, std::span<HBEntry const>{entries.data(), entries.size()}, /*depth=*/0);

    // 3) A trie root is ALWAYS a 32-byte hash, even when the top node encodes to < 32 bytes.
    bcos::h256 root;
    if (rootRef.kind == NodeRef::Kind::Hash)
    {
        root = rootRef.hash;
    }
    else
    {
        bcos::crypto::hasher::hash(m_hasher, bcos::ref(rootRef.inlineBytes), root);
        m_newNodes.emplace(root, rootRef.inlineBytes);
    }

    // 4) Flush every new node into the cache in one pass.
    for (auto const& [hash, raw] : m_newNodes)
    {
        co_await m_cache.put(hash, raw);
    }

    co_return root;
}

std::unordered_map<bcos::h256, bcos::bytes> HashBuilder::drainNewNodes()
{
    return std::exchange(m_newNodes, {});
}

std::unordered_set<bcos::h256> HashBuilder::drainObsoletedNodes()
{
    return std::exchange(m_obsoleted, {});
}

}  // namespace bcos::ledger::mpt
