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
 * @file MPTBuilderTombstoneTest.cpp
 * @brief MPTBuilder tombstone path over the fork view (spec §5.3 path 3, §5.4) — M4.5
 */
#include "TestHelpers.h"
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-ledger/mpt/Account.h>
#include <bcos-ledger/mpt/Classify.h>
#include <bcos-ledger/mpt/Constants.h>
#include <bcos-ledger/mpt/Errors.h>
#include <bcos-ledger/mpt/HashBuilder.h>
#include <bcos-ledger/mpt/MPTBuilder.h>
#include <bcos-ledger/mpt/MPTReadView.h>
#include <bcos-ledger/mpt/StorageValueCodec.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <boost/test/unit_test.hpp>
#include <map>
#include <string_view>
#include <vector>

namespace bcos::ledger::mpt::test
{

BOOST_AUTO_TEST_SUITE(MPTBuilderTombstoneSuite)

namespace
{
using NodeStorage = bcos::storage2::memory_storage::MemoryStorage<bcos::h256, bcos::bytes>;

// Build one account's storage trie from (slotKey → raw value) and return its root.
bcos::h256 buildStorageTrie(NodeStorage& storage, std::map<bcos::h256, bcos::bytes> const& slots)
{
    std::map<bcos::h256, bcos::bytes> entries;
    for (auto const& [slot, value] : slots)
    {
        auto encoded = encodeStorageValue(bcos::ref(value));
        BOOST_REQUIRE(!encoded.empty());
        entries[slotKeyHash(slot)] = std::move(encoded);
    }
    return seedTrieFlushed(storage, emptyRootHash(), entries).root;
}

// Build the parent state trie over (address → Account) and return its root.
bcos::h256 buildStateTrie(NodeStorage& storage, std::map<bcos::Address, Account> const& accounts)
{
    std::map<bcos::h256, bcos::bytes> entries;
    for (auto const& [addr, account] : accounts)
    {
        entries[accountKeyHash(addr)] = account.encode();
    }
    return seedTrieFlushed(storage, emptyRootHash(), entries).root;
}

// The from-scratch oracle for the state trie root over the surviving accounts.
bcos::h256 stateRootOracle(std::map<bcos::Address, Account> const& accounts)
{
    std::map<bcos::h256, bcos::bytes> entries;
    for (auto const& [addr, account] : accounts)
    {
        entries[accountKeyHash(addr)] = account.encode();
    }
    return computeTrieRoot(entries).root;
}

// A raw-bytes Entry for a storage slot value.
bcos::storage::Entry slotEntry(bcos::bytes const& value)
{
    return makeEntry(std::string_view{reinterpret_cast<char const*>(value.data()), value.size()});
}

// A 32-byte slot key with one marker byte.
bcos::h256 slotKey(uint8_t marker)
{
    return makeHash(marker);
}

// Write the SELFDESTRUCT shape: all three core-field rows logically deleted in the delta layer
// (spec §5.2's convention — storage-level deletion is the signal the builder recognizes).
void writeTombstoneEntries(FlatStateView& view, bcos::Address const& addr)
{
    deleteFlatRowLogically(view, accountFieldKey(addr, ROW_NONCE));
    deleteFlatRowLogically(view, accountFieldKey(addr, ROW_BALANCE));
    deleteFlatRowLogically(view, accountFieldKey(addr, ROW_CODE_HASH));
}

}  // namespace

BOOST_AUTO_TEST_CASE(SelfdestructRemovesAccountFromTrie)
{
    NodeStorage storage;
    auto const addrA = makeAddress(0x51);
    auto const addrB = makeAddress(0x52);

    Account accountA;
    accountA.nonce = 1;
    accountA.balance = 10;
    accountA.storageRoot = buildStorageTrie(
        storage, {{slotKey(0x00), bcos::bytes{0x10}}, {slotKey(0x01), bcos::bytes{0x11}},
                     {slotKey(0x02), bcos::bytes{0x12}}});
    Account accountB;
    accountB.balance = 99;
    auto const parentRoot = buildStateTrie(storage, {{addrA, accountA}, {addrB, accountB}});

    FlatBackendStorage flatBackend;
    auto view = makeFlatView(flatBackend);
    writeTombstoneEntries(view, addrA);

    // A tombstone account still reaches the scan: logical deletion keeps its keys in the delta
    // layer, so the run is seen and settled as a tombstone.
    auto output =
        bcos::task::syncWait(buildAndCollect(storage, parentRoot, view, /*l2Mode=*/false));

    // The destroyed account is gone; the untouched one still reads back.
    MPTReadView<NodeStorage> readView(storage, output.stateRoot);
    auto goneA = bcos::task::syncWait(readView.readAccount(addrA));
    BOOST_CHECK(!goneA.has_value());
    auto aliveB = bcos::task::syncWait(readView.readAccount(addrB));
    BOOST_REQUIRE(aliveB.has_value());
    BOOST_CHECK_EQUAL(aliveB->balance, bcos::u256(99));

    // The prior storage root is recorded for future pathdb pruning, and the incremental
    // account-trie rebuild obsoletes the prior root node.
    BOOST_CHECK(output.obsoletedNodes.contains(accountA.storageRoot));
    BOOST_CHECK(output.obsoletedNodes.contains(parentRoot));

    // The new state trie is exactly a from-scratch build over the survivors.
    BOOST_CHECK(output.stateRoot == stateRootOracle({{addrB, accountB}}));
}

BOOST_AUTO_TEST_CASE(TombstoneOfSoleAccountEmptiesTheTrie)
{
    // Destroying the only account leaves nothing behind: the account trie collapses back to the
    // empty root, and the dead storage trie's root still reaches the prune ledger.
    NodeStorage storage;
    auto const addr = makeAddress(0x53);

    Account account;
    account.storageRoot = buildStorageTrie(storage, {{slotKey(0x00), bcos::bytes{0x42}}});
    auto const parentRoot = buildStateTrie(storage, {{addr, account}});

    FlatBackendStorage flatBackend;
    auto view = makeFlatView(flatBackend);
    deleteFlatRowLogically(view, accountFieldKey(addr, ROW_NONCE));
    deleteFlatRowLogically(view, accountFieldKey(addr, ROW_BALANCE));
    deleteFlatRowLogically(view, accountFieldKey(addr, ROW_CODE_HASH));

    auto output =
        bcos::task::syncWait(buildAndCollect(storage, parentRoot, view, /*l2Mode=*/false));

    BOOST_CHECK(output.stateRoot == emptyRootHash());  // sole account gone
    BOOST_CHECK(output.obsoletedNodes.contains(account.storageRoot));
}

BOOST_AUTO_TEST_CASE(TombstoneIgnoresStorageChanges)
{
    NodeStorage storage;
    auto const addrA = makeAddress(0x51);
    auto const addrB = makeAddress(0x52);

    Account accountA;
    accountA.nonce = 1;
    accountA.balance = 10;
    accountA.storageRoot = buildStorageTrie(
        storage, {{slotKey(0x00), bcos::bytes{0x10}}, {slotKey(0x01), bcos::bytes{0x11}},
                     {slotKey(0x02), bcos::bytes{0x12}}});
    Account accountB;
    accountB.balance = 99;
    auto const parentRoot = buildStateTrie(storage, {{addrA, accountA}, {addrB, accountB}});

    // The tombstone buried under slot writes in the same delta — all ignored, the whole leaf
    // goes away (spec §5.3 path 3).
    FlatBackendStorage flatBackend;
    auto view = makeFlatView(flatBackend);
    writeTombstoneEntries(view, addrA);
    writeFlatRow(view, accountSlotKey(addrA, slotKey(0x00)), slotEntry(bcos::bytes{0xFF}));
    writeFlatRow(view, accountSlotKey(addrA, slotKey(0x09)), slotEntry(bcos::bytes{0x99}));

    auto output =
        bcos::task::syncWait(buildAndCollect(storage, parentRoot, view, /*l2Mode=*/false));

    MPTReadView<NodeStorage> readView(storage, output.stateRoot);
    auto goneA = bcos::task::syncWait(readView.readAccount(addrA));
    BOOST_CHECK(!goneA.has_value());
    BOOST_CHECK(output.obsoletedNodes.contains(accountA.storageRoot));
    BOOST_CHECK(output.stateRoot == stateRootOracle({{addrB, accountB}}));
}

BOOST_AUTO_TEST_CASE(TombstoneOfAccountAbsentFromParentIsNoop)
{
    NodeStorage storage;
    auto const known = makeAddress(0x54);
    auto const absent = makeAddress(0x55);
    auto const parentRoot = buildStateTrie(storage, {{known, Account{}}});

    // Destroying an account the parent MPT never held: removing an absent leaf is a legal
    // no-op (commitTrie treats a delete of a missing key as such).
    FlatBackendStorage flatBackend;
    auto view = makeFlatView(flatBackend);
    writeTombstoneEntries(view, absent);

    auto output =
        bcos::task::syncWait(buildAndCollect(storage, parentRoot, view, /*l2Mode=*/false));

    BOOST_CHECK(output.stateRoot == parentRoot);
    BOOST_CHECK(output.obsoletedNodes.empty());
}

BOOST_AUTO_TEST_CASE(RebornNextBlockWalksFirstTouchWithIndependentStorage)
{
    NodeStorage storage;
    auto const addrA = makeAddress(0x56);
    auto const addrB = makeAddress(0x57);

    // Parent chain state: A holds two storage slots, B keeps the trie non-trivial after A dies.
    Account accountA;
    accountA.nonce = 1;
    accountA.storageRoot = buildStorageTrie(
        storage, {{slotKey(0x00), bcos::bytes{0x10}}, {slotKey(0x01), bcos::bytes{0x11}}});
    Account accountB;
    accountB.balance = 7;
    auto const parentRoot = buildStateTrie(storage, {{addrA, accountA}, {addrB, accountB}});

    // The flat backend still holds A's old slot rows. Under the slot-level model the reborn
    // account can NEVER inherit them — first-touch never scans the flat slots — so the
    // SELFDESTRUCT + CREATE2-redeploy fork the old preheat manifest had to guard against is
    // closed by construction.
    FlatBackendStorage flatBackend;
    writeFlatRow(flatBackend, accountSlotKey(addrA, slotKey(0x00)), slotEntry({0x10}));
    writeFlatRow(flatBackend, accountSlotKey(addrA, slotKey(0x01)), slotEntry({0x11}));

    // Block N: SELFDESTRUCT A.
    auto viewN = makeFlatView(flatBackend);
    writeTombstoneEntries(viewN, addrA);
    auto outputN =
        bcos::task::syncWait(buildAndCollect(storage, parentRoot, viewN, /*l2Mode=*/false));
    {
        MPTReadView<NodeStorage> readView(storage, outputN.stateRoot);
        auto gone = bcos::task::syncWait(readView.readAccount(addrA));
        BOOST_CHECK(!gone.has_value());
    }

    // Block N+1, variant 1: A is re-created with no slot writes — the reborn account sits on
    // the empty storage root.
    auto viewBare = makeFlatView(flatBackend);
    writeFlatRow(viewBare, accountFieldKey(addrA, ROW_NONCE), makeEntry("2"));
    writeFlatRow(viewBare, accountFieldKey(addrA, ROW_BALANCE), makeEntry("5"));
    writeFlatRow(viewBare, accountFieldKey(addrA, ROW_CODE_HASH),
        makeEntry(std::string_view{
            reinterpret_cast<char const*>(makeHash(0xCE).data()), bcos::h256::SIZE}));
    auto outputBare = bcos::task::syncWait(
        buildAndCollect(storage, outputN.stateRoot, viewBare, /*l2Mode=*/false));
    {
        MPTReadView<NodeStorage> readView(storage, outputBare.stateRoot);
        auto reborn = bcos::task::syncWait(readView.readAccount(addrA));
        BOOST_REQUIRE(reborn.has_value());
        BOOST_CHECK(reborn->storageRoot == emptyRootHash());
        BOOST_CHECK_EQUAL(reborn->nonce, bcos::u256(2));
    }

    // Block N+1, variant 2: re-created with one new slot — the storage root is exactly a fresh
    // single-slot trie; nothing leaks over from the pre-destruct slots.
    auto const newSlot = slotKey(0x30);
    auto viewSlot = makeFlatView(flatBackend);
    writeFlatRow(viewSlot, accountFieldKey(addrA, ROW_NONCE), makeEntry("2"));
    writeFlatRow(viewSlot, accountFieldKey(addrA, ROW_BALANCE), makeEntry("5"));
    writeFlatRow(viewSlot, accountFieldKey(addrA, ROW_CODE_HASH),
        makeEntry(std::string_view{
            reinterpret_cast<char const*>(makeHash(0xCE).data()), bcos::h256::SIZE}));
    writeFlatRow(viewSlot, accountSlotKey(addrA, newSlot), slotEntry(bcos::bytes{0x99}));
    auto outputSlot = bcos::task::syncWait(
        buildAndCollect(storage, outputN.stateRoot, viewSlot, /*l2Mode=*/false));
    {
        MPTReadView<NodeStorage> readView(storage, outputSlot.stateRoot);
        auto reborn = bcos::task::syncWait(readView.readAccount(addrA));
        BOOST_REQUIRE(reborn.has_value());
        std::map<bcos::h256, bcos::bytes> const oracle{
            {slotKeyHash(newSlot), encodeStorageValue(bcos::ref(bcos::bytes{0x99}))}};
        BOOST_CHECK(reborn->storageRoot == computeTrieRoot(oracle).root);
    }
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::ledger::mpt::test
