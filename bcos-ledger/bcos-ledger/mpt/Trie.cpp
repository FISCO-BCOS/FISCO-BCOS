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
 * @file Trie.cpp
 * @brief Read-only walk over an MPT given its root hash (spec §5.6, §7.1)
 */
#include "Trie.h"
#include "Constants.h"
#include "Errors.h"
#include "Nibble.h"
#include "NodeDecoder.h"
#include "TrieNode.h"
#include <algorithm>
#include <variant>

namespace bcos::ledger::mpt
{
namespace
{
/// Load a node by its hash from the cache and decode it. A miss means the caller failed to
/// prefetch a referenced node — a programming error, not a "not found" — so we throw rather than
/// return nullopt.
bcos::task::Task<TrieNode> trieLoadByHash(NodeCache& cache, bcos::h256 const& hash)
{
    auto raw = co_await cache.get(hash);
    if (!raw)
    {
        BOOST_THROW_EXCEPTION(MPTInvariantViolation{} << bcos::errinfo_comment(
                                  "Trie: cache miss for node hash; cache must be prefetched"));
    }
    co_return decodeNode(bcos::ref(*raw));
}

/// Resolve a BranchNode child reference. Absent children are handled by the caller before this is
/// reached, so only Hash and (non-empty) Inline are seen here.
bcos::task::Task<TrieNode> trieLoadFromRef(NodeCache& cache, NodeRef const& ref)
{
    if (ref.kind == NodeRef::Kind::Hash)
    {
        co_return co_await trieLoadByHash(cache, ref.hash);
    }
    // Inline (non-empty): the child's complete RLP encoding is stored directly.
    co_return decodeNode(bcos::ref(ref.inlineBytes));
}

/// Resolve an ExtensionNode.child, which is kept as a raw RLP child reference: EITHER the 33-byte
/// hash string (0xa0 followed by the 32-byte digest) OR the inline node's complete RLP encoding.
bcos::task::Task<TrieNode> trieLoadFromRawRef(NodeCache& cache, bcos::bytes const& childRaw)
{
    if (childRaw.size() == HASH_REF_ENCODED_SIZE && childRaw[0] == RLP_HASH_REF_PREFIX)
    {
        bcos::h256 const childHash(childRaw.data() + 1, bcos::h256::SIZE);
        co_return co_await trieLoadByHash(cache, childHash);
    }
    co_return decodeNode(bcos::ref(childRaw));
}
}  // namespace

Trie::Trie(NodeCache& cache, bcos::h256 root) : m_cache(cache), m_root(root) {}

bcos::task::Task<std::optional<bcos::bytes>> Trie::get(bcos::h256 const& keyHash) const
{
    if (m_root == emptyRootHash())
    {
        co_return std::nullopt;
    }

    bcos::bytes const path = bytesToNibbles(keyHash.ref());  // 64 nibbles
    size_t pos = 0;                                          // nibbles consumed so far

    TrieNode node = co_await trieLoadByHash(m_cache.get(), m_root);
    while (true)
    {
        if (std::holds_alternative<EmptyNode>(node))
        {
            co_return std::nullopt;
        }

        if (auto const* leaf = std::get_if<LeafNode>(&node))
        {
            // The remaining path is path[pos..end]; it must equal the leaf suffix exactly.
            if (size_t const remaining = path.size() - pos; remaining != leaf->keyNibbles.size())
            {
                co_return std::nullopt;
            }
            if (!std::equal(leaf->keyNibbles.begin(), leaf->keyNibbles.end(), path.begin() + pos))
            {
                co_return std::nullopt;
            }
            co_return leaf->value;
        }

        if (auto const* ext = std::get_if<ExtensionNode>(&node))
        {
            // The remaining path must start with the shared nibbles.
            if (size_t const remaining = path.size() - pos; remaining < ext->sharedNibbles.size())
            {
                co_return std::nullopt;
            }
            if (!std::equal(
                    ext->sharedNibbles.begin(), ext->sharedNibbles.end(), path.begin() + pos))
            {
                co_return std::nullopt;
            }
            pos += ext->sharedNibbles.size();
            node = co_await trieLoadFromRawRef(m_cache.get(), ext->child);
            continue;
        }

        // BranchNode
        const auto& [children, value] = std::get<BranchNode>(node);
        if (pos == path.size())
        {
            // All nibbles consumed at a branch: the value (if any) belongs to this key.
            co_return value.empty() ? std::nullopt : std::optional<bcos::bytes>{value};
        }
        bcos::byte const nib = path.at(pos);
        NodeRef const& child = children.at(nib);
        // Absent child == Inline with empty inlineBytes.
        if (child.kind == NodeRef::Kind::Inline && child.inlineBytes.empty())
        {
            co_return std::nullopt;
        }
        pos += 1;
        node = co_await trieLoadFromRef(m_cache.get(), child);
    }
}
}  // namespace bcos::ledger::mpt
