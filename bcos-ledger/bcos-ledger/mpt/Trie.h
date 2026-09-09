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
 * @brief Read-only walk over a path-addressed MPT (spec §5.6, §7.1; pathdb spec §8.3)
 */
#pragma once
#include "Constants.h"
#include "Errors.h"
#include "Nibble.h"
#include "NodeDecoder.h"
#include "PathKey.h"
#include "TrieNode.h"
// AnyHasher.h defines the free bcos::crypto::hasher::hash(); OpenSSLHasher.h only defines the
// hasher type. Both are needed for the keccak verification every node read performs.
#include <bcos-crypto/hasher/AnyHasher.h>
#include <bcos-crypto/hasher/OpenSSLHasher.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/FixedBytes.h>
#include <boost/throw_exception.hpp>
#include <algorithm>
#include <functional>
#include <optional>
#include <utility>
#include <variant>

namespace bcos::ledger::mpt
{
namespace detail
{
/// keccak of a node's raw RLP — the value a parent records for its child, and a header records
/// for a trie root.
template <bcos::crypto::hasher::Hasher HasherT>
bcos::h256 nodeDigest(HasherT& hasher, bcos::bytesConstRef raw)
{
    bcos::h256 digest;
    bcos::crypto::hasher::hash(hasher, raw, digest);
    return digest;
}

/// Load the node at @p key and check it against @p expected — the digest its PARENT recorded for
/// it. Under path addressing the hash no longer says WHERE the child is (the position does), so
/// this is the whole of its remaining job: proving the bytes at that position are the ones the
/// parent committed to.
/// @throws MPTInvariantViolation when the row is missing or its digest disagrees. Both mean the
/// store contradicts a node it itself holds, which is corruption, not a "not found".
template <bcos::storage2::ReadableStorage<PathKey> Storage, bcos::crypto::hasher::Hasher HasherT>
bcos::task::Task<bcos::bytes> loadNodeBytesAt(
    Storage& storage, PathKey const& key, bcos::h256 const& expected, HasherT& hasher)
{
    auto raw = co_await bcos::storage2::readOne(storage, key);
    if (!raw)
    {
        BOOST_THROW_EXCEPTION(MPTInvariantViolation{} << bcos::errinfo_comment(
                                  "Trie: storage lacks the node its parent references at position "
                                  "0x" +
                                  bcos::toHex(key.position)));
    }
    if (auto const digest = nodeDigest(hasher, bcos::ref(*raw)); digest != expected)
    {
        BOOST_THROW_EXCEPTION(
            MPTInvariantViolation{} << bcos::errinfo_comment(
                "Trie: node at position 0x" + bcos::toHex(key.position) + " hashes to " +
                digest.hex() + " but its parent references " + expected.hex()));
    }
    co_return std::move(*raw);
}

/// Load a trie ROOT: position "" of @p scope, verified against @p expectedRoot.
///
/// A path store keeps ONE version per position, so this doubles as the check that @p expectedRoot
/// is that version. A missing row or a different digest therefore is not corruption — it is the
/// caller asking about a root the store no longer holds.
/// @throws MPTHistoryUnavailable in both cases.
template <bcos::storage2::ReadableStorage<PathKey> Storage, bcos::crypto::hasher::Hasher HasherT>
bcos::task::Task<bcos::bytes> loadRootBytesAt(
    Storage& storage, TrieScope const& scope, bcos::h256 const& expectedRoot, HasherT& hasher)
{
    PathKey const rootKey{.scope = scope, .position = {}};
    auto raw = co_await bcos::storage2::readOne(storage, rootKey);
    if (!raw)
    {
        BOOST_THROW_EXCEPTION(MPTHistoryUnavailable{} << bcos::errinfo_comment(
                                  "Trie: no trie root row for state root " + expectedRoot.hex() +
                                  "; a path-addressed node store keeps only the current version, "
                                  "so historical roots need the trie-node history index"));
    }
    if (auto const digest = nodeDigest(hasher, bcos::ref(*raw)); digest != expectedRoot)
    {
        BOOST_THROW_EXCEPTION(
            MPTHistoryUnavailable{} << bcos::errinfo_comment(
                "Trie: the stored trie root hashes to " + digest.hex() + ", not the requested " +
                expectedRoot.hex() +
                "; a path-addressed node store keeps only the current version, so historical "
                "roots need the trie-node history index"));
    }
    co_return std::move(*raw);
}
}  // namespace detail

/// Whether @p storage can currently serve the trie @p scope at @p expectedRoot.
///
/// This replaces the "is this root present as a node row?" probe the hash-addressed store
/// supported. There is no row named after a root any more, so the question becomes: does position
/// "" hold bytes that hash to it? A `false` therefore covers both "no trie here at all" and "this
/// store has moved on to a newer version" — which are the same answer to the caller either way.
///
/// The EMPTY root is not stored (an empty trie has no nodes), so callers that treat it as a legal
/// "no accounts" root must short-circuit before asking.
/// @tparam HasherT the hash the chain's tries were BUILT with — the digest this compares against
/// @p expectedRoot. Defaults to keccak256, so no call site churns; an SM3 deployment names it and
/// gets an SM3 comparison (see the @todo in HashBuilder.h for what else has to move with it).
template <bcos::storage2::ReadableStorage<PathKey> Storage,
    bcos::crypto::hasher::Hasher HasherT = bcos::crypto::hasher::openssl::OpenSSL_Keccak256_Hasher>
bcos::task::Task<bool> holdsTrieRoot(
    Storage& storage, TrieScope const& scope, bcos::h256 const& expectedRoot)
{
    auto raw = co_await bcos::storage2::readOne(storage, PathKey{.scope = scope, .position = {}});
    if (!raw)
    {
        co_return false;
    }
    HasherT hasher;
    co_return detail::nodeDigest(hasher, bcos::ref(*raw)) == expectedRoot;
}

/// Read-only walk over an MPT identified by (@p scope, @p root). Nodes are fetched from @p Storage
/// — any type satisfying storage2::ReadableStorage<PathKey> — BY POSITION: the walk computes the
/// next row key from the key being looked up (spec A.1), never from the bytes it just read, and
/// uses the hash it read only to verify what the position hands back.
///
/// @tparam HasherT the hash the trie was BUILT with — used to verify every node this walk reads.
/// Defaults to keccak256. It MUST be the same hasher the caller uses for its key transform
/// (accountKeyHash / slotKeyHash): mixing them would locate nodes by one algorithm's paths and
/// verify them against another's digests, which fails as corruption at best and, where the walk
/// dead-ends first, silently reports absence.
///
/// @throws MPTHistoryUnavailable when @p root is not the version the store currently holds,
///         MPTInvariantViolation when a node the trie itself references is missing or corrupt.
template <bcos::storage2::ReadableStorage<PathKey> Storage,
    bcos::crypto::hasher::Hasher HasherT = bcos::crypto::hasher::openssl::OpenSSL_Keccak256_Hasher>
class Trie
{
public:
    /// @param scope which trie: the account trie, or one account's storage trie.
    /// @param root  the root the caller expects; it is verified, not used for addressing.
    Trie(Storage& storage, TrieScope scope, bcos::h256 root)
      : m_storage(storage), m_scope(std::move(scope)), m_root(root)
    {}

    bcos::task::Task<std::optional<bcos::bytes>> get(bcos::h256 const& keyHash) const
    {
        if (m_root == emptyRootHash<HasherT>())
        {
            co_return std::nullopt;
        }

        bcos::bytes const path = bytesToNibbles(keyHash.ref());  // 64 nibbles
        size_t pos = 0;                                          // nibbles consumed so far

        // One hash context per walk, living in this coroutine's frame: every node read below
        // verifies against it, and hashing mutates it. Keeping it here rather than in the object
        // is what makes get() honestly const — two coroutines may share one Trie and neither can
        // interleave into the other's digest state.
        HasherT hasher;

        // The entry point is a FIXED key (position "" of this scope), not the root hash.
        auto rootRaw = co_await detail::loadRootBytesAt(m_storage.get(), m_scope, m_root, hasher);
        TrieNode node = decodeNode(bcos::ref(rootRaw));
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
                // An extension consumes its whole shared prefix: child position = P || shared.
                pos += ext->sharedNibbles.size();
                auto const childRaw = bcos::ref(ext->child);
                if (childRaw.size() == HASH_REF_ENCODED_SIZE && childRaw[0] == RLP_HASH_REF_PREFIX)
                {
                    bcos::h256 const childHash(childRaw.getCroppedData(1, bcos::h256::SIZE));
                    auto next = co_await detail::loadNodeBytesAt(m_storage.get(),
                        PathKey{.scope = m_scope,
                            .position = bcos::bytes(path.begin(), path.begin() + pos)},
                        childHash, hasher);
                    TrieNode decoded = decodeNode(bcos::ref(next));
                    node = std::move(decoded);
                }
                else
                {
                    // Inline child: its complete RLP is embedded in this node, no row of its own.
                    TrieNode decoded = decodeNode(childRaw);
                    node = std::move(decoded);
                }
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
            if (child.isAbsent())
            {
                co_return std::nullopt;
            }
            // A branch consumes one nibble: child position = P || nib.
            pos += 1;
            if (child.kind() == NodeRef::Kind::Hash)
            {
                auto next = co_await detail::loadNodeBytesAt(m_storage.get(),
                    PathKey{.scope = m_scope,
                        .position = bcos::bytes(path.begin(), path.begin() + pos)},
                    child.hash(), hasher);
                TrieNode decoded = decodeNode(bcos::ref(next));
                node = std::move(decoded);
            }
            else
            {
                TrieNode decoded = decodeNode(child.inlineRef());
                node = std::move(decoded);
            }
        }
    }

    bcos::h256 root() const noexcept { return m_root; }
    TrieScope const& scope() const noexcept { return m_scope; }

private:
    std::reference_wrapper<Storage> m_storage;
    TrieScope m_scope;
    bcos::h256 m_root;
};
}  // namespace bcos::ledger::mpt
