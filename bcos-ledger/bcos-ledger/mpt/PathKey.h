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
 * @file PathKey.h
 * @brief Path addressing for trie nodes: a node's row key is WHERE it sits in its trie
 *        (pathdb spec §8)
 */
#pragma once

#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <compare>
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>

namespace bcos::ledger::mpt
{

/// The two state TABLES trie-node rows live in. A node row is an ordinary state row whose row key
/// is the node's POSITION in its trie, so its physical key in the default ColumnFamily is the
/// StateKey serialization "<table>" ':' "<key>":
///
///   "/mptp/a:" + compactPath(position)
///   "/mptp/s:" + 32 raw owner bytes + compactPath(position)
///
/// The trie kind rides on the table name rather than a tag inside the row key. That keeps every
/// node row's row key unambiguously decodable (an owner is fixed-width, a position is not, so a
/// single shared table could not tell the two apart), and it makes one account's whole storage
/// trie a contiguous "/mptp/s:<owner>" key range.
///
/// Neither name contains ':', so the first ':' of every node row's physical key sits at a fixed
/// index — the same one for both tables, since they are the same length — and
/// StateKeyResolver::decode's split-at-first-colon reconstruction is exact for every row key,
/// including positions and owners that contain 0x3A (':') bytes.
///
/// Do NOT write these literals anywhere else — build keys with pathNodeStateKey below; the
/// physical form is produced and parsed solely by StateKeyResolver.
inline constexpr std::string_view kMPTAccountTable = "/mptp/a";
inline constexpr std::string_view kMPTStorageTable = "/mptp/s";

static_assert(kMPTAccountTable.find(':') == std::string_view::npos &&
                  kMPTStorageTable.find(':') == std::string_view::npos,
    "an MPT node table name must not contain ':' — StateKeyResolver splits a physical key at its "
    "FIRST colon, so a colon in the table name would decode node rows to a corrupted "
    "table/key split");
static_assert(kMPTAccountTable.size() == kMPTStorageTable.size(),
    "the two MPT node table names must be the same length, so the ':' StateKeyResolver inserts "
    "sits at the same offset for both");
static_assert(kMPTAccountTable != kMPTStorageTable,
    "the account and storage node tables must be distinct namespaces");

/// Which FAMILY of trie a node belongs to. The chain has exactly one account trie and one storage
/// trie per contract account, so this plus TrieScope::owner names a single trie (spec §8.1).
enum class TrieKind : uint8_t
{
    Account,
    Storage
};

/// One trie, named. `owner` is meaningful only for TrieKind::Storage, where it is the OWNING
/// ACCOUNT'S trie key — accountKeyHash(address), i.e. keccak256 of the 20 raw address bytes
/// (MPTReadView.h). That value is not invented for path addressing: it is what the account trie
/// already sorts accounts by, so a walk that reaches an account's leaf has it in hand (the walked
/// position concatenated with the leaf's suffix) with no lookup (spec §8.3).
struct TrieScope
{
    TrieKind kind = TrieKind::Account;
    bcos::h256 owner{};  ///< the owning account's trie key; unused (zero) for TrieKind::Account

    /// The chain's single account trie.
    static TrieScope account() noexcept { return TrieScope{}; }
    /// The storage trie of the account whose trie key is @p owner.
    static TrieScope storage(bcos::h256 const& owner) noexcept
    {
        return TrieScope{.kind = TrieKind::Storage, .owner = owner};
    }

    friend bool operator==(TrieScope const& lhs, TrieScope const& rhs) noexcept
    {
        return lhs.kind == rhs.kind && lhs.owner == rhs.owner;
    }
    // h256 predates operator<=>, so the ordering is spelled out rather than defaulted. It matches
    // the PHYSICAL row order the state tables use (table name, then row key), which is what makes
    // a seek-and-stop prefix scan over one scope's rows possible.
    friend std::strong_ordering operator<=>(TrieScope const& lhs, TrieScope const& rhs) noexcept
    {
        if (auto const cmp = lhs.kind <=> rhs.kind; cmp != std::strong_ordering::equal)
        {
            return cmp;
        }
        if (lhs.owner == rhs.owner)
        {
            return std::strong_ordering::equal;
        }
        return lhs.owner < rhs.owner ? std::strong_ordering::less : std::strong_ordering::greater;
    }
};

/// A node's address: which trie, and WHERE in it. `position` is the nibble string consumed walking
/// from that trie's root down to this node (spec A.1):
///
///     branch    at P                  -> child i  at P || i
///     extension at P, shared = T      -> child    at P || T
///     leaf      at P, suffix = S      -> the full key it carries is P || S
///
/// A tree has exactly one path from its root to any node, so no two live nodes share a position —
/// that is the whole basis of path addressing. The trie root always sits at the empty position.
struct PathKey
{
    TrieScope scope;
    bcos::bytes position;  ///< nibbles (each in [0, 15]), root-to-node

    friend bool operator==(PathKey const& lhs, PathKey const& rhs) noexcept = default;
    friend std::strong_ordering operator<=>(PathKey const& lhs, PathKey const& rhs) noexcept
    {
        if (auto const cmp = lhs.scope <=> rhs.scope; cmp != std::strong_ordering::equal)
        {
            return cmp;
        }
        return lhs.position <=> rhs.position;
    }
};

/// The root of the account trie: position "" of the chain's single account trie.
inline PathKey accountRootPathKey()
{
    return PathKey{.scope = TrieScope::account(), .position = {}};
}
/// The root of @p owner's storage trie: position "" of that trie.
inline PathKey storageRootPathKey(bcos::h256 const& owner)
{
    return PathKey{.scope = TrieScope::storage(owner), .position = {}};
}

/// Pack a nibble position into bytes, unambiguously.
///
/// Padding an odd nibble count with a zero would collide ("a70" and "a700" both pack to a7 00), so
/// the parity goes into a header byte, exactly like the Yellow Paper's Hex-Prefix encoding:
///
///     bits [7:6] = 0
///     bit  5     = 0   ALWAYS (see below)
///     bit  4     = 1 when the nibble count is odd
///     bits [3:0] = the first nibble when odd, else 0
///     then       = the remaining nibbles packed two per byte
///
/// **The leaf flag is pinned to 0.** Hex-Prefix's bit 5 distinguishes a leaf from an extension,
/// which is information about the NODE, not about the position. Letting it into a row key would
/// break the one invariant path addressing rests on — that a position IS the key — because the
/// same position would encode as 0x3a while a leaf sits there and 0x1a once an insert turns that
/// leaf into an extension (TrieMerge.h's leaf-split), i.e. the row would rename itself mid-write.
/// So for every non-empty position this produces exactly `hexPrefixEncode(position, false)`
/// (pinned by PathKeyTest), and it additionally accepts the EMPTY position — which
/// hexPrefixEncode rejects, since an empty path is meaningless for a real extension node but is
/// precisely where every trie root lives (spec §8.1).
bcos::bytes compactPath(bcos::bytesConstRef position);

/// Inverse of compactPath.
/// @throws MPTDecodeError on empty input, or when the leaf flag is set (not a position key).
bcos::bytes decodeCompactPath(bcos::bytesConstRef encoded);

/// The state row a node at @p key lives in (spec §8.2):
///
///     account node:  StateKey{"/mptp/a", compactPath(position)}
///     storage node:  StateKey{"/mptp/s", owner (32 raw bytes) || compactPath(position)}
///
/// The trie kind is carried by the TABLE, not by a tag inside the row key, so the only ':' in the
/// physical key is the one StateKeyResolver inserts — and the two table names are the same length,
/// so that colon sits at a fixed offset for both.
executor_v1::StateKey pathNodeStateKey(PathKey const& key);

/// Inverse of pathNodeStateKey: nullopt when @p key is not a trie-node row at all (any other
/// table), which is how a prefix scan recognises that it has walked off the end of the node rows.
/// @throws MPTDecodeError when the row IS in a node table but its row key is malformed — a
/// corrupt node row must not read as "some other table's row".
std::optional<PathKey> parsePathNodeStateKey(executor_v1::StateKey const& key);

}  // namespace bcos::ledger::mpt

template <>
struct std::hash<bcos::ledger::mpt::TrieScope>
{
    std::size_t operator()(bcos::ledger::mpt::TrieScope const& scope) const noexcept
    {
        std::size_t seed = std::hash<std::uint8_t>{}(static_cast<std::uint8_t>(scope.kind));
        seed ^= std::hash<bcos::h256>{}(scope.owner) + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
        return seed;
    }
};

template <>
struct std::hash<bcos::ledger::mpt::PathKey>
{
    std::size_t operator()(bcos::ledger::mpt::PathKey const& key) const noexcept
    {
        std::size_t seed = std::hash<bcos::ledger::mpt::TrieScope>{}(key.scope);
        for (auto const nibble : key.position)
        {
            seed ^= std::hash<bcos::byte>{}(nibble) + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
        }
        return seed;
    }
};
