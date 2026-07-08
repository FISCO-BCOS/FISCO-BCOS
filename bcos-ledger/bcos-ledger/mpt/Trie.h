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
 * @file Trie.h
 * @brief Read-only walk over an MPT given its root hash (spec §5.6, §7.1)
 */
#pragma once
#include "Constants.h"
#include "Errors.h"
#include "Nibble.h"
#include "NodeDecoder.h"
#include "TrieNode.h"
#include <bcos-framework/storage2/Storage.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <algorithm>
#include <functional>
#include <optional>
#include <variant>

namespace bcos::ledger::mpt
{
namespace detail
{
/// Load a node by its hash from @p storage and decode it. A miss means storage lacks a node the
/// trie references — a programming/consistency error, not a "not found" — so we throw rather than
/// return nullopt.
template <bcos::storage2::ReadableStorage<bcos::h256> Storage>
bcos::task::Task<TrieNode> trieLoadByHash(Storage& storage, bcos::h256 const& hash)
{
    auto raw = co_await bcos::storage2::readOne(storage, hash);
    if (!raw)
    {
        BOOST_THROW_EXCEPTION(MPTInvariantViolation{} << bcos::errinfo_comment(
                                  "Trie: missing node hash; storage lacks a referenced node"));
    }
    co_return decodeNode(bcos::ref(*raw));
}

/// Resolve a BranchNode child reference. Absent children are handled by the caller before this is
/// reached, so only Hash and (non-empty) Inline are seen here.
template <bcos::storage2::ReadableStorage<bcos::h256> Storage>
bcos::task::Task<TrieNode> trieLoadFromRef(Storage& storage, NodeRef const& ref)
{
    if (ref.kind == NodeRef::Kind::Hash)
    {
        co_return co_await trieLoadByHash(storage, ref.hash);
    }
    // Inline (non-empty): the child's complete RLP encoding is stored directly.
    co_return decodeNode(bcos::ref(ref.inlineBytes));
}

/// Resolve an ExtensionNode.child, which is kept as a raw RLP child reference: EITHER the 33-byte
/// hash string (0xa0 followed by the 32-byte digest) OR the inline node's complete RLP encoding.
template <bcos::storage2::ReadableStorage<bcos::h256> Storage>
bcos::task::Task<TrieNode> trieLoadFromRawRef(Storage& storage, bcos::bytesConstRef childRaw)
{
    if (childRaw.size() == HASH_REF_ENCODED_SIZE && childRaw[0] == RLP_HASH_REF_PREFIX)
    {
        bcos::h256 const childHash(childRaw.getCroppedData(1, bcos::h256::SIZE));
        co_return co_await trieLoadByHash(storage, childHash);
    }
    co_return decodeNode(childRaw);
}
}  // namespace detail

/// Read-only walk over an MPT given its root hash. Nodes are fetched from @p Storage — any type
/// satisfying storage2::ReadableStorage<h256> (a MemoryStorage cache, a RocksDBStorage2, or a
/// layered MultiLayerStorage). A read that returns nullopt for a referenced hash means storage is
/// missing a node the trie references: a programming/consistency error, so it throws.
template <bcos::storage2::ReadableStorage<bcos::h256> Storage>
class Trie
{
public:
    Trie(Storage& storage, bcos::h256 root) : m_storage(storage), m_root(root) {}

    bcos::task::Task<std::optional<bcos::bytes>> get(bcos::h256 const& keyHash) const
    {
        if (m_root == emptyRootHash())
        {
            co_return std::nullopt;
        }

        bcos::bytes const path = bytesToNibbles(keyHash.ref());  // 64 nibbles
        size_t pos = 0;                                          // nibbles consumed so far

        TrieNode node = co_await detail::trieLoadByHash(m_storage.get(), m_root);
        while (true)
        {
            if (std::holds_alternative<EmptyNode>(node))
            {
                co_return std::nullopt;
            }

            if (auto const* leaf = std::get_if<LeafNode>(&node))
            {
                // The remaining path is path[pos..end]; it must equal the leaf suffix exactly.
                if (size_t const remaining = path.size() - pos;
                    remaining != leaf->keyNibbles.size())
                {
                    co_return std::nullopt;
                }
                if (!std::equal(
                        leaf->keyNibbles.begin(), leaf->keyNibbles.end(), path.begin() + pos))
                {
                    co_return std::nullopt;
                }
                co_return leaf->value;
            }

            if (auto const* ext = std::get_if<ExtensionNode>(&node))
            {
                // The remaining path must start with the shared nibbles.
                if (size_t const remaining = path.size() - pos;
                    remaining < ext->sharedNibbles.size())
                {
                    co_return std::nullopt;
                }
                if (!std::equal(
                        ext->sharedNibbles.begin(), ext->sharedNibbles.end(), path.begin() + pos))
                {
                    co_return std::nullopt;
                }
                pos += ext->sharedNibbles.size();
                node = co_await detail::trieLoadFromRawRef(m_storage.get(), bcos::ref(ext->child));
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
            node = co_await detail::trieLoadFromRef(m_storage.get(), child);
        }
    }

    bcos::h256 root() const noexcept { return m_root; }

private:
    std::reference_wrapper<Storage> m_storage;
    bcos::h256 m_root;
};
}  // namespace bcos::ledger::mpt
