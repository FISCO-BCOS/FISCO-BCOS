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
 * @file PathKey.cpp
 * @brief Position <-> state-row-key codec for path-addressed trie nodes (pathdb spec §8)
 */

#include "PathKey.h"
#include "Errors.h"
#include "HexPrefix.h"
#include "Nibble.h"
#include <boost/throw_exception.hpp>
#include <string>
#include <string_view>

namespace bcos::ledger::mpt
{
namespace
{
/// Hex-Prefix header bits (Yellow Paper Appendix C), the two this codec uses. The leaf flag is
/// only ever CHECKED here, never set — see compactPath's contract.
constexpr bcos::byte HP_LEAF_FLAG = 0b0010'0000U;
constexpr bcos::byte HP_ODD_FLAG = 0b0001'0000U;

std::string_view tableOf(TrieKind kind)
{
    return kind == TrieKind::Account ? kMPTAccountTable : kMPTStorageTable;
}
}  // namespace

bcos::bytes compactPath(bcos::bytesConstRef position)
{
    // Deliberately not hexPrefixEncode(position, false): that function asserts a non-empty path
    // for the extension form, and the EMPTY position — every trie's root — is exactly the case
    // path addressing needs. The byte layout below is HP's, minus the leaf flag, and PathKeyTest
    // pins the two against each other for every non-empty position.
    bool const odd = (position.size() % NIBBLES_PER_BYTE == 1);
    bcos::bytes out;
    out.reserve(1 + (position.size() / NIBBLES_PER_BYTE));
    bcos::byte header = odd ? HP_ODD_FLAG : bcos::byte{0};
    if (odd)
    {
        header |= (position[0] & LOW_NIBBLE_MASK);
    }
    out.push_back(header);
    for (size_t i = odd ? 1U : 0U; i + 1 < position.size(); i += NIBBLES_PER_BYTE)
    {
        out.push_back(static_cast<bcos::byte>(((position[i] & LOW_NIBBLE_MASK) << NIBBLE_BITS) |
                                              (position[i + 1] & LOW_NIBBLE_MASK)));
    }
    return out;
}

bcos::bytes decodeCompactPath(bcos::bytesConstRef encoded)
{
    // hexPrefixDecode owns the parity/packing rules; this only rejects the one header bit a
    // position key must never carry.
    auto [nibbles, isLeaf] = hexPrefixDecode(encoded);
    if (isLeaf)
    {
        BOOST_THROW_EXCEPTION(MPTDecodeError{} << bcos::errinfo_comment(
                                  "decodeCompactPath: leaf flag set in a trie-node position key"));
    }
    return std::move(nibbles);
}

executor_v1::StateKey pathNodeStateKey(PathKey const& key)
{
    auto const packed = compactPath(bcos::ref(key.position));
    std::string row;
    if (key.scope.kind == TrieKind::Storage)
    {
        row.reserve(bcos::h256::SIZE + packed.size());
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        row.append(reinterpret_cast<char const*>(key.scope.owner.data()), bcos::h256::SIZE);
    }
    else
    {
        row.reserve(packed.size());
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    row.append(reinterpret_cast<char const*>(packed.data()), packed.size());
    return {tableOf(key.scope.kind), std::move(row)};
}

std::optional<PathKey> parsePathNodeStateKey(executor_v1::StateKey const& key)
{
    executor_v1::StateKeyView const view{key};
    TrieKind kind{};
    if (view.m_table == kMPTAccountTable)
    {
        kind = TrieKind::Account;
    }
    else if (view.m_table == kMPTStorageTable)
    {
        kind = TrieKind::Storage;
    }
    else
    {
        return std::nullopt;  // not a node row: the caller has scanned past the node tables
    }

    std::string_view row = view.m_key;
    PathKey out;
    out.scope.kind = kind;
    if (kind == TrieKind::Storage)
    {
        if (row.size() < bcos::h256::SIZE)
        {
            BOOST_THROW_EXCEPTION(
                MPTDecodeError{} << bcos::errinfo_comment(
                    "parsePathNodeStateKey: storage node row key shorter than its 32-byte owner"));
        }
        out.scope.owner = bcos::h256(bcos::bytesConstRef(
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            reinterpret_cast<bcos::byte const*>(row.data()), bcos::h256::SIZE));
        row.remove_prefix(bcos::h256::SIZE);
    }
    out.position = decodeCompactPath(bcos::bytesConstRef(
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        reinterpret_cast<bcos::byte const*>(row.data()), row.size()));
    return out;
}

}  // namespace bcos::ledger::mpt
