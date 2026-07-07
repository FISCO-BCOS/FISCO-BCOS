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
 * @brief MPTBuilder tombstone path (spec §5.3 path 3, §5.4) — M4.5
 */
#include "TestHelpers.h"
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-ledger/mpt/Account.h>
#include <bcos-ledger/mpt/AccountDelta.h>
#include <bcos-ledger/mpt/Constants.h>
#include <bcos-ledger/mpt/Errors.h>
#include <bcos-ledger/mpt/FlatToMPT.h>
#include <bcos-ledger/mpt/HashBuilder.h>
#include <bcos-ledger/mpt/MPTBuilder.h>
#include <bcos-ledger/mpt/MPTReadView.h>
#include <bcos-ledger/mpt/StorageValueCodec.h>
#include <bcos-task/Task.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <boost/test/unit_test.hpp>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace bcos::ledger::mpt::test
{

BOOST_AUTO_TEST_SUITE(MPTBuilderTombstoneSuite)

namespace
{
using NodeStorage = bcos::storage2::memory_storage::MemoryStorage<bcos::h256, bcos::bytes>;
using FlatSlots = std::vector<std::pair<bcos::h256, bcos::bytes>>;

// Build one account's storage trie from (slotKey → raw value) and return its root.
bcos::h256 buildStorageTrie(NodeStorage& storage, std::map<bcos::h256, bcos::bytes> const& slots)
{
    HashBuilder builder(storage, emptyRootHash());
    for (auto const& [slot, value] : slots)
    {
        auto encoded = encodeStorageValue(bcos::ref(value));
        BOOST_REQUIRE(!encoded.empty());
        bcos::task::syncWait(builder.put(slotKeyHash(slot), std::move(encoded)));
    }
    return bcos::task::syncWait(builder.commit());
}

// Build the parent state trie over (address → Account) and return its root.
bcos::h256 buildStateTrie(NodeStorage& storage, std::map<bcos::Address, Account> const& accounts)
{
    HashBuilder builder(storage, emptyRootHash());
    for (auto const& [addr, account] : accounts)
    {
        bcos::task::syncWait(builder.put(accountKeyHash(addr), account.encode()));
    }
    return bcos::task::syncWait(builder.commit());
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

// A 32-byte slot key with one marker byte.
bcos::h256 slotKey(uint8_t marker)
{
    return makeHash(marker);
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

    MPTBuildInput input;
    input.perAccount[addrA].tombstone = true;

    MPTBuilder builder(storage, parentRoot);
    auto output = bcos::task::syncWait(builder.buildAndCollect(input));

    // The destroyed account is gone; the untouched one still reads back.
    MPTReadView<NodeStorage> view(storage, output.stateRoot);
    auto goneA = bcos::task::syncWait(view.readAccount(addrA));
    BOOST_CHECK(!goneA.has_value());
    auto aliveB = bcos::task::syncWait(view.readAccount(addrB));
    BOOST_REQUIRE(aliveB.has_value());
    BOOST_CHECK_EQUAL(aliveB->balance, bcos::u256(99));

    // The prior storage root is recorded for future pathdb pruning, and the incremental
    // account-trie rebuild obsoletes the prior root node.
    BOOST_CHECK(output.obsoletedNodes.contains(accountA.storageRoot));
    BOOST_CHECK(output.obsoletedNodes.contains(parentRoot));

    // The new state trie is exactly a from-scratch build over the survivors.
    BOOST_CHECK(output.stateRoot == stateRootOracle({{addrB, accountB}}));
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

    // The same tombstone as SelfdestructRemovesAccountFromTrie, buried under slot writes and
    // field updates classify() would never emit alongside a tombstone — all ignored.
    MPTBuildInput input;
    auto& delta = input.perAccount[addrA];
    delta.tombstone = true;
    delta.nonceState = AccountDelta::FieldState::Updated;
    delta.nonce = 42;
    delta.balanceState = AccountDelta::FieldState::Updated;
    delta.balance = 4200;
    delta.codeHashState = AccountDelta::FieldState::Updated;
    delta.codeHash = makeHash(0xDE);
    delta.storageChanges[slotKey(0x00)] = bcos::bytes{0xFF};
    delta.storageChanges[slotKey(0x09)] = bcos::bytes{0x99};

    MPTBuilder builder(storage, parentRoot);
    auto output = bcos::task::syncWait(builder.buildAndCollect(input));

    MPTReadView<NodeStorage> view(storage, output.stateRoot);
    auto goneA = bcos::task::syncWait(view.readAccount(addrA));
    BOOST_CHECK(!goneA.has_value());
    BOOST_CHECK(output.obsoletedNodes.contains(accountA.storageRoot));
    BOOST_CHECK(output.stateRoot == stateRootOracle({{addrB, accountB}}));
}

BOOST_AUTO_TEST_CASE(TombstoneOfLastAccountYieldsEmptyRoot)
{
    NodeStorage storage;
    auto const addr = makeAddress(0x53);

    Account account;
    account.storageRoot = buildStorageTrie(storage, {{slotKey(0x00), bcos::bytes{0x42}}});
    auto const parentRoot = buildStateTrie(storage, {{addr, account}});

    MPTBuildInput input;
    input.perAccount[addr].tombstone = true;

    MPTBuilder builder(storage, parentRoot);
    auto output = bcos::task::syncWait(builder.buildAndCollect(input));

    BOOST_CHECK(output.stateRoot == emptyRootHash());
    BOOST_CHECK(output.obsoletedNodes.contains(account.storageRoot));
}

BOOST_AUTO_TEST_CASE(TombstoneOfAccountAbsentFromParentIsNoop)
{
    NodeStorage storage;
    auto const known = makeAddress(0x54);
    auto const absent = makeAddress(0x55);
    auto const parentRoot = buildStateTrie(storage, {{known, Account{}}});

    // The pathological combo: tombstone AND firstTouch. Tombstone wins, and removing an
    // absent leaf is a legal no-op — the first-touch machinery must never run.
    bool scannerCalled = false;
    FlatToMPTBackends backends;
    backends.flatSlotScanner = [&scannerCalled](
                                   bcos::Address const&) -> bcos::task::Task<FlatSlots> {
        scannerCalled = true;
        BOOST_FAIL("flatSlotScanner must not be called for a tombstone");
        co_return FlatSlots{};
    };

    MPTBuildInput input;
    auto& delta = input.perAccount[absent];
    delta.tombstone = true;
    delta.firstTouch = true;

    MPTBuilder builder(storage, parentRoot, std::move(backends));
    auto output = bcos::task::syncWait(builder.buildAndCollect(input));

    BOOST_CHECK(!scannerCalled);
    BOOST_CHECK(output.stateRoot == parentRoot);
    BOOST_CHECK(output.obsoletedNodes.empty());
    BOOST_CHECK(output.preheatManifestsToDelete.empty());
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

    // Block N: SELFDESTRUCT A.
    MPTBuildInput inputN;
    inputN.perAccount[addrA].tombstone = true;
    MPTBuilder builderN(storage, parentRoot);
    auto outputN = bcos::task::syncWait(builderN.buildAndCollect(inputN));
    {
        MPTReadView<NodeStorage> view(storage, outputN.stateRoot);
        auto gone = bcos::task::syncWait(view.readAccount(addrA));
        BOOST_CHECK(!gone.has_value());
    }

    // Block N+1: A is re-created. Post-selfdestruct flat state has no slots left, so the
    // first-touch scanner returns empty — the reborn account must NOT inherit the destroyed
    // storage trie.
    int scannerCalls = 0;
    auto makeBackends = [&scannerCalls] {
        FlatToMPTBackends backends;
        backends.flatSlotScanner = [&scannerCalls](
                                       bcos::Address const&) -> bcos::task::Task<FlatSlots> {
            ++scannerCalls;
            co_return FlatSlots{};
        };
        return backends;
    };
    auto makeRebornDelta = [](AccountDelta& delta) {
        delta.firstTouch = true;
        delta.nonceState = AccountDelta::FieldState::Updated;
        delta.nonce = 2;
        delta.balanceState = AccountDelta::FieldState::Updated;
        delta.balance = 5;
        delta.codeHashState = AccountDelta::FieldState::Updated;
        delta.codeHash = makeHash(0xCE);
    };

    // Without new slot writes the reborn account sits on the empty storage root.
    MPTBuildInput rebornBare;
    makeRebornDelta(rebornBare.perAccount[addrA]);
    MPTBuilder builderBare(storage, outputN.stateRoot, makeBackends());
    auto outputBare = bcos::task::syncWait(builderBare.buildAndCollect(rebornBare));
    {
        MPTReadView<NodeStorage> view(storage, outputBare.stateRoot);
        auto reborn = bcos::task::syncWait(view.readAccount(addrA));
        BOOST_REQUIRE(reborn.has_value());
        BOOST_CHECK(reborn->storageRoot == emptyRootHash());
        BOOST_CHECK_EQUAL(reborn->nonce, bcos::u256(2));
    }

    // With one new slot in the same delta, the storage root is exactly a fresh single-slot
    // trie — nothing leaks over from the pre-destruct slots.
    auto const newSlot = slotKey(0x30);
    MPTBuildInput rebornWithSlot;
    auto& delta = rebornWithSlot.perAccount[addrA];
    makeRebornDelta(delta);
    delta.storageChanges[newSlot] = bcos::bytes{0x99};
    MPTBuilder builderSlot(storage, outputN.stateRoot, makeBackends());
    auto outputSlot = bcos::task::syncWait(builderSlot.buildAndCollect(rebornWithSlot));
    {
        MPTReadView<NodeStorage> view(storage, outputSlot.stateRoot);
        auto reborn = bcos::task::syncWait(view.readAccount(addrA));
        BOOST_REQUIRE(reborn.has_value());
        std::map<bcos::h256, bcos::bytes> const oracle{
            {slotKeyHash(newSlot), encodeStorageValue(bcos::ref(bcos::bytes{0x99}))}};
        BOOST_CHECK(reborn->storageRoot == computeTrieRoot(oracle).root);
    }
    BOOST_CHECK_EQUAL(scannerCalls, 2);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::ledger::mpt::test
