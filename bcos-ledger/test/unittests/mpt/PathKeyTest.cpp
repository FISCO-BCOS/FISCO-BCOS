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
 * @file PathKeyTest.cpp
 * @brief Position <-> row-key codec: the invariants path addressing rests on (pathdb spec §8)
 */
#include "TestHelpers.h"
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-ledger/mpt/HexPrefix.h>
#include <bcos-ledger/mpt/Nibble.h>
#include <bcos-ledger/mpt/NodeDecoder.h>
#include <bcos-ledger/mpt/PathKey.h>
#include <bcos-ledger/mpt/TrieNode.h>
#include <bcos-utilities/Common.h>
#include <boost/test/unit_test.hpp>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "bcos-ledger/test/unittests/ExceptionCheck.h"

namespace bcos::ledger::mpt::test
{

BOOST_AUTO_TEST_SUITE(PathKeySuite)

namespace
{
/// A nibble string from a hex literal: "a70" -> {0x0a, 0x07, 0x00}.
bcos::bytes nibbles(std::string_view hex)
{
    bcos::bytes out;
    out.reserve(hex.size());
    for (char const digit : hex)
    {
        out.push_back(static_cast<bcos::byte>(
            digit >= 'a' ? (digit - 'a' + 10) : (digit >= 'A' ? (digit - 'A' + 10) : digit - '0')));
    }
    return out;
}

/// The physical row key StateKeyResolver would store, "<table>:<row>".
std::string physicalKey(executor_v1::StateKey const& key)
{
    executor_v1::StateKeyView const view{key};
    return std::string{view.m_table} + ":" + std::string{view.m_key};
}
}  // namespace

// The whole point of the parity header byte: an odd position and its zero-extended even sibling
// are DIFFERENT positions and must be different rows. Zero-padding would collide them.
BOOST_AUTO_TEST_CASE(OddAndZeroExtendedPositionsGetDistinctKeys)
{
    auto const odd = compactPath(bcos::ref(nibbles("a70")));
    auto const even = compactPath(bcos::ref(nibbles("a700")));
    BOOST_CHECK(odd != even);
    BOOST_CHECK(odd == bcos::bytes({0x1a, 0x70}));
    BOOST_CHECK(even == bcos::bytes({0x00, 0xa7, 0x00}));

    // ...and the row keys they produce differ too, in both tables.
    BOOST_CHECK(physicalKey(pathNodeStateKey(
                    PathKey{.scope = TrieScope::account(), .position = nibbles("a70")})) !=
                physicalKey(pathNodeStateKey(
                    PathKey{.scope = TrieScope::account(), .position = nibbles("a700")})));
    auto const owner = makeHash(0x5a);
    BOOST_CHECK(physicalKey(pathNodeStateKey(
                    PathKey{.scope = TrieScope::storage(owner), .position = nibbles("a70")})) !=
                physicalKey(pathNodeStateKey(
                    PathKey{.scope = TrieScope::storage(owner), .position = nibbles("a700")})));
}

// Round trip over every length parity, the empty position (every trie's root) included.
BOOST_AUTO_TEST_CASE(CompactPathRoundTrips)
{
    for (std::string_view const hex :
        {"", "a", "a7", "a7c", "a7c1", "0", "00", "000", "f", "ff", "fff", "0f0f0f0"})
    {
        auto const position = nibbles(hex);
        BOOST_CHECK_MESSAGE(
            decodeCompactPath(bcos::ref(compactPath(bcos::ref(position)))) == position,
            "round trip failed for position \"" << hex << '"');
    }
}

// The empty position is what makes a trie root findable without knowing its hash. hexPrefixEncode
// rejects it for the extension form (an empty extension path is malformed), which is exactly why
// compactPath is its own function.
BOOST_AUTO_TEST_CASE(EmptyPositionEncodesToTheRootKey)
{
    BOOST_CHECK(compactPath(bcos::bytesConstRef{}) == bcos::bytes({0x00}));
    BOOST_CHECK(decodeCompactPath(bcos::ref(compactPath(bcos::bytesConstRef{}))).empty());
    BOOST_CHECK(physicalKey(pathNodeStateKey(accountRootPathKey())) ==
                std::string{ledger::mpt::kMPTAccountTable} + ":" + std::string(1, '\0'));
}

// For every NON-empty position, compactPath must agree byte for byte with the repository's
// Hex-Prefix encoder in its extension form. That pins the codec to the Yellow Paper packing
// instead of letting it drift into a private format.
BOOST_AUTO_TEST_CASE(MatchesHexPrefixExtensionForm)
{
    for (std::string_view const hex : {"a", "a7", "a7c", "a7c1", "0", "00", "0f0f0f0", "ffff"})
    {
        auto const position = nibbles(hex);
        BOOST_CHECK_MESSAGE(compactPath(bcos::ref(position)) ==
                                hexPrefixEncode(bcos::ref(position), /*isLeaf=*/false),
            "compactPath diverged from hexPrefixEncode for \"" << hex << '"');
    }
}

// A key that carries the LEAF flag is not a position key: decoding must refuse it rather than
// silently accept a row whose key means something else.
BOOST_AUTO_TEST_CASE(LeafFlaggedKeyIsRejected)
{
    auto const leafForm = hexPrefixEncode(bcos::ref(nibbles("a7c")), /*isLeaf=*/true);
    BOOST_CHECK_EXCEPTION(decodeCompactPath(bcos::ref(leafForm)), MPTDecodeError,
        [](auto const& e) { return bcos::test::errinfoContains(e, "leaf flag"); });
}

// THE invariant of path addressing: a position is a position. Whatever kind of node currently
// sits there — and an insert really does turn a leaf into an extension mid-write (TrieMerge's
// leaf split) — the row key must not move. Driven through the real builder rather than asserted
// on the codec alone, because the failure this guards against is a row RENAMING itself between
// two blocks.
BOOST_AUTO_TEST_CASE(SameKeyForLeafExtensionAndBranch)
{
    auto keyWith = [](bcos::byte second, bcos::byte last) {
        bcos::h256 out{};
        out.data()[0] = static_cast<bcos::byte>(0xa0U | second);
        out.data()[bcos::h256::SIZE - 1] = last;
        return out;
    };

    NodeMemoryStorage storage;
    // Block 1: one key — the whole trie is a single LEAF, and it lives at position "".
    auto const first =
        seedTrieFlushed(storage, emptyRootHash(), {{keyWith(0x07, 0x01), bcos::bytes(40, 0x11)}});
    BOOST_REQUIRE(first.upserts.contains(accountRootPathKey()));
    auto const asLeaf = first.upserts.at(accountRootPathKey());
    BOOST_CHECK(std::holds_alternative<LeafNode>(decodeNode(bcos::ref(asLeaf))));

    // Block 2: a key diverging at the SECOND nibble. Position "" now holds an EXTENSION over the
    // shared "a", with a branch below it.
    auto const second =
        seedTrieFlushed(storage, first.root, {{keyWith(0x09, 0x02), bcos::bytes(40, 0x22)}});
    BOOST_REQUIRE(second.upserts.contains(accountRootPathKey()));
    auto const asExtension = second.upserts.at(accountRootPathKey());
    BOOST_CHECK(std::holds_alternative<ExtensionNode>(decodeNode(bcos::ref(asExtension))));

    // Same row, rewritten in place: the node kind changed, the key did not, and the store holds
    // the new encoding at exactly the old key.
    BOOST_CHECK(asLeaf != asExtension);
    auto const stored =
        bcos::task::syncWait(bcos::storage2::readOne(storage, accountRootPathKey()));
    BOOST_REQUIRE(stored.has_value());
    BOOST_CHECK(*stored == asExtension);
    // ...and nothing was deleted: the leaf did not vacate a position, it was overwritten.
    BOOST_CHECK(second.deletes.empty());
}

// The two node tables are separate namespaces of the same shape: same length, so the ':'
// StateKeyResolver inserts sits at the same offset for both, and a storage row key is the owner
// followed by exactly the account form.
BOOST_AUTO_TEST_CASE(BothTablesShareTheColonOffset)
{
    auto const owner = makeHash(0x3a);  // 0x3a IS ':': a raw colon inside a row key is legal
    auto const accountKey =
        pathNodeStateKey(PathKey{.scope = TrieScope::account(), .position = nibbles("a7c")});
    auto const storageKey =
        pathNodeStateKey(PathKey{.scope = TrieScope::storage(owner), .position = nibbles("a7c")});

    BOOST_CHECK_EQUAL(physicalKey(accountKey).find(':'), physicalKey(storageKey).find(':'));
    BOOST_CHECK_EQUAL(physicalKey(accountKey).find(':'), ledger::mpt::kMPTAccountTable.size());

    executor_v1::StateKeyView const storageView{storageKey};
    BOOST_REQUIRE_GE(storageView.m_key.size(), bcos::h256::SIZE);
    BOOST_CHECK(storageView.m_key.substr(0, bcos::h256::SIZE) ==
                std::string_view(reinterpret_cast<char const*>(owner.data()), bcos::h256::SIZE));
    BOOST_CHECK(storageView.m_key.substr(bcos::h256::SIZE) ==
                std::string_view{executor_v1::StateKeyView{accountKey}.m_key});
}

// Row keys round-trip back into PathKeys — the property TrieIndex (PR-C) and any offline verifier
// need, and the one that lets a prefix scan classify what it finds.
BOOST_AUTO_TEST_CASE(StateKeyRoundTrips)
{
    auto const owner = makeHash(0xbe);
    for (auto const& key : std::vector<PathKey>{
             accountRootPathKey(),
             storageRootPathKey(owner),
             PathKey{.scope = TrieScope::account(), .position = nibbles("a7c")},
             PathKey{.scope = TrieScope::storage(owner), .position = nibbles("9c")},
         })
    {
        auto const parsed = parsePathNodeStateKey(pathNodeStateKey(key));
        BOOST_REQUIRE(parsed.has_value());
        BOOST_CHECK(*parsed == key);
    }
}

// A row of any other table is not a node row: the prefix scan relies on that answer to know it
// has walked off the end.
BOOST_AUTO_TEST_CASE(ForeignTableIsNotANodeRow)
{
    BOOST_CHECK(!parsePathNodeStateKey(executor_v1::StateKey{"/apps/abcd", "balance"}).has_value());
    BOOST_CHECK(!parsePathNodeStateKey(executor_v1::StateKey{"/mpt/", "x"}).has_value());
}

// A storage row key shorter than its owner is corrupt, not "some other table's row" — the scan
// must not swallow it.
BOOST_AUTO_TEST_CASE(TruncatedStorageRowKeyThrows)
{
    BOOST_CHECK_THROW(
        parsePathNodeStateKey(executor_v1::StateKey{ledger::mpt::kMPTStorageTable, "short"}),
        MPTDecodeError);
}

// Ordering must group rows the way the physical store does — by table, then owner, then position
// — or a seek-and-stop prefix scan over one owner's trie would miss rows or run past them.
BOOST_AUTO_TEST_CASE(OrderingGroupsByScopeThenPosition)
{
    auto const ownerLow = makeHash(0x01);
    auto const ownerHigh = makeHash(0x02);
    BOOST_CHECK(accountRootPathKey() < storageRootPathKey(ownerLow));
    BOOST_CHECK(storageRootPathKey(ownerLow) < storageRootPathKey(ownerHigh));
    BOOST_CHECK(storageRootPathKey(ownerLow) <
                (PathKey{.scope = TrieScope::storage(ownerLow), .position = nibbles("0")}));
    BOOST_CHECK((PathKey{.scope = TrieScope::storage(ownerLow), .position = nibbles("f")}) <
                storageRootPathKey(ownerHigh));
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::ledger::mpt::test
