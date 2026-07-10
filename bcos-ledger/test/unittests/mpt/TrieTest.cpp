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
 * @file TrieTest.cpp
 * @brief Unit tests for the read-only Trie walk (spec §5.6, §7.1)
 */

#include "TestHelpers.h"
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-ledger/mpt/Constants.h>
#include <bcos-ledger/mpt/HashBuilder.h>
#include <bcos-ledger/mpt/Trie.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <boost/test/unit_test.hpp>
#include <utility>
#include <vector>

namespace bcos::ledger::mpt::test
{

BOOST_AUTO_TEST_SUITE(TrieSuite)

namespace
{
// A 32-byte key whose every byte is `fill`, then `tailLen` trailing bytes overwritten by `tail`.
// Lets us craft keys that share a long nibble prefix (forcing an extension above a branch) while
// still differing late enough to fan out across distinct branch slots.
bcos::h256 trieKey(bcos::byte fill, bcos::byte tail, size_t tailLen)
{
    bcos::h256 key{};
    for (size_t i = 0; i < bcos::h256::SIZE; ++i)
    {
        key.data()[i] = fill;
    }
    for (size_t i = 0; i < tailLen && i < bcos::h256::SIZE; ++i)
    {
        key.data()[bcos::h256::SIZE - 1 - i] = tail;
    }
    return key;
}
}  // namespace

// A key that was inserted reads back its exact value through a freshly built root.
BOOST_AUTO_TEST_CASE(GetExistentKeyReturnsValue)
{
    bcos::storage2::memory_storage::MemoryStorage<bcos::h256, bcos::bytes> storage;

    auto const key = makeHash(0xab);
    bcos::bytes const value{0x42};
    auto const root = seedTrieFlushed(storage, emptyRootHash(), {{key, value}}).root;

    Trie trie(storage, root);
    auto got = bcos::task::syncWait(trie.get(key));
    BOOST_REQUIRE(got.has_value());
    BOOST_CHECK(*got == value);
}

// An empty trie returns nullopt for any key (no node fetch happens at all).
BOOST_AUTO_TEST_CASE(GetNonexistentKeyReturnsNullopt)
{
    bcos::storage2::memory_storage::MemoryStorage<bcos::h256, bcos::bytes> storage;
    Trie trie(storage, emptyRootHash());

    BOOST_CHECK(!bcos::task::syncWait(trie.get(makeHash(0x01))).has_value());
    BOOST_CHECK(!bcos::task::syncWait(trie.get(makeHash(0xff))).has_value());
    BOOST_CHECK(!bcos::task::syncWait(trie.get(bcos::h256{})).has_value());
}

// The real traversal test: a multi-key trie whose shape exercises extension nodes (long shared
// prefix), branch nodes (the late fan-out), hash-child loading (large subtrees), and inline-child
// decoding (tiny subtrees spliced raw). Every inserted key must read back its exact value, and a
// key that was never inserted must return nullopt.
BOOST_AUTO_TEST_CASE(MultiKeyEachKeyResolves)
{
    std::vector<std::pair<bcos::h256, bcos::bytes>> kvs;

    // Group A: all share the 0xab... prefix, differing only in the final 1-2 bytes. This forces a
    // deep extension above a branch; the differing nibbles land in distinct branch slots.
    kvs.emplace_back(trieKey(0xab, 0x00, 1), bcos::bytes{0xa0});
    kvs.emplace_back(trieKey(0xab, 0x10, 1), bcos::bytes{0xa1});
    kvs.emplace_back(trieKey(0xab, 0x20, 1), bcos::bytes(40, 0xee));  // long value -> hashed
                                                                      // subtree
    kvs.emplace_back(trieKey(0xab, 0xcd, 2), bcos::bytes{0xa3});

    // Group B: a different top prefix (0x12...) so the root itself becomes a branch/extension over
    // two top-level groups.
    kvs.emplace_back(trieKey(0x12, 0x00, 1), bcos::bytes{0xb0});
    kvs.emplace_back(trieKey(0x12, 0x34, 2), bcos::bytes(64, 0x77));  // long value -> hashed
                                                                      // subtree

    // Group C: short single-byte-distinct top nibbles that drop straight into the root branch with
    // tiny leaf subtrees (inline children).
    kvs.emplace_back(makeHash(0x30), bcos::bytes{0xc0});
    kvs.emplace_back(makeHash(0x4f), bcos::bytes{0xc1});

    bcos::storage2::memory_storage::MemoryStorage<bcos::h256, bcos::bytes> storage;
    std::map<bcos::h256, bcos::bytes> entries(kvs.begin(), kvs.end());
    auto const root = seedTrieFlushed(storage, emptyRootHash(), entries).root;

    Trie trie(storage, root);
    for (auto const& [key, value] : kvs)
    {
        auto got = bcos::task::syncWait(trie.get(key));
        BOOST_REQUIRE_MESSAGE(got.has_value(), "missing key " << key.hex());
        BOOST_CHECK_MESSAGE(*got == value, "wrong value for key " << key.hex());
    }

    // A key that was never inserted must not resolve. 0xaa... diverges from group A at the very
    // first nibble of the shared prefix, exercising the extension-mismatch early-out.
    auto const absentA = trieKey(0xaa, 0x00, 1);
    BOOST_CHECK(!bcos::task::syncWait(trie.get(absentA)).has_value());
    // 0xab...0x99 shares the long prefix but lands on an empty branch slot.
    auto const absentB = trieKey(0xab, 0x90, 1);
    BOOST_CHECK(!bcos::task::syncWait(trie.get(absentB)).has_value());
    // A completely unrelated key.
    BOOST_CHECK(!bcos::task::syncWait(trie.get(makeHash(0xfe))).has_value());
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::ledger::mpt::test
