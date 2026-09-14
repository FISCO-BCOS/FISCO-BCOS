/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * @brief Regression test for issue #5373: EIP-158/161 empty-account clearing. An account whose
 *        post-block state is {nonce 0, balance 0, keccak256(""), *} must not be written as a
 *        leaf; if it had a leaf it is removed and its storage trie obsoleted (reth: touched &&
 *        empty -> destroyed; storage is not consulted for emptiness but is dropped).
 */
#include "TestHelpers.h"
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-ledger/mpt/Account.h>
#include <bcos-ledger/mpt/Constants.h>
#include <bcos-ledger/mpt/MPTBuilder.h>
#include <bcos-ledger/mpt/MPTReadView.h>
#include <bcos-ledger/mpt/StorageValueCodec.h>
#include <bcos-task/Wait.h>
#include <boost/test/unit_test.hpp>
#include <map>

namespace bcos::ledger::mpt::test
{
BOOST_AUTO_TEST_SUITE(MPTBuilderEmptyAccountSuite)

namespace
{
using NodeStorage = bcos::storage2::memory_storage::MemoryStorage<bcos::h256, bcos::bytes>;

bcos::h256 buildStorageTrie(NodeStorage& storage, std::map<bcos::h256, bcos::bytes> const& slots)
{
    std::map<bcos::h256, bcos::bytes> entries;
    for (auto const& [slot, value] : slots)
    {
        entries[slotKeyHash(slot)] = encodeStorageValue(bcos::ref(value));
    }
    return seedTrieFlushed(storage, emptyRootHash(), entries).root;
}

bcos::storage::Entry codeHashEntry(bcos::h256 const& hash)
{
    return makeEntry(
        std::string_view{reinterpret_cast<char const*>(hash.data()), bcos::h256::SIZE});
}
}  // namespace

// A zero-value touch of a never-seen address writes nothing (EIP-161 c: an absent account must
// not become "present but empty").
BOOST_AUTO_TEST_CASE(EmptyFirstTouchWritesNoLeaf)
{
    NodeStorage storage;
    auto const addr = makeAddress(0xE1);
    FlatBackendStorage flatBackend;
    auto view = makeFlatView(flatBackend);
    writeFlatRow(view, accountFieldKey(addr, ROW_BALANCE), makeEntry("0"));

    auto output =
        bcos::task::syncWait(buildAndCollect(storage, emptyRootHash(), view, /*l2Mode=*/false));

    BOOST_CHECK(output.stateRoot == emptyRootHash());
    MPTReadView<NodeStorage> readView(storage, output.stateRoot);
    BOOST_CHECK(!bcos::task::syncWait(readView.readAccount(addr)).has_value());
}

// A live account drained to {0, 0, keccak256("")} loses its leaf; its storage trie (not part of
// the emptiness test) is dropped with it.
BOOST_AUTO_TEST_CASE(DrainedAccountLeafIsRemovedAndStorageObsoleted)
{
    NodeStorage storage;
    auto const drained = makeAddress(0xE2);
    auto const other = makeAddress(0xE3);

    Account before;  // nonce 0, codeHash defaults to emptyCodeHash()
    before.balance = 5;
    before.storageRoot = buildStorageTrie(storage, {{makeHash(0x01), bcos::bytes{0x10}}});
    Account otherAccount;
    otherAccount.balance = 99;
    auto const parentRoot =
        seedStateTrieFlushed(storage, {{drained, before}, {other, otherAccount}});

    FlatBackendStorage flatBackend;
    auto view = makeFlatView(flatBackend);
    writeFlatRow(view, accountFieldKey(drained, ROW_BALANCE), makeEntry("0"));

    auto output =
        bcos::task::syncWait(buildAndCollect(storage, parentRoot, view, /*l2Mode=*/false));

    MPTReadView<NodeStorage> readView(storage, output.stateRoot);
    BOOST_CHECK(!bcos::task::syncWait(readView.readAccount(drained)).has_value());
    auto alive = bcos::task::syncWait(readView.readAccount(other));
    BOOST_REQUIRE(alive.has_value());
    BOOST_CHECK_EQUAL(alive->balance, bcos::u256(99));
    BOOST_CHECK(output.obsoletedNodes.contains(before.storageRoot));
    // state root equals a trie holding only the surviving account
    BOOST_CHECK(
        output.stateRoot == referenceRoot({{accountKeyHash(other), otherAccount.encode()}}));
}

// A contract that dies in this block (codeHash written back to keccak256(""), balance drained)
// and also writes slots in the same block: the slot writes are dropped with the account, so the
// build produces exactly the same nodes as the same death without the slot writes.
BOOST_AUTO_TEST_CASE(DyingAccountSlotWritesAreDroppedWithoutOrphanNodes)
{
    NodeStorage storage;
    auto const addr = makeAddress(0xE6);
    Account before;
    before.balance = 5;
    before.codeHash = makeHash(0xC6);
    before.storageRoot = buildStorageTrie(storage, {{makeHash(0x01), bcos::bytes{0x10}}});
    auto const parentRoot = seedStateTrieFlushed(storage, {{addr, before}});

    auto death = [&](bool withSlotWrite) {
        FlatBackendStorage flatBackend;
        auto view = makeFlatView(flatBackend);
        writeFlatRow(view, accountFieldKey(addr, ROW_BALANCE), makeEntry("0"));
        writeFlatRow(view, accountFieldKey(addr, ROW_CODE_HASH), codeHashEntry(emptyCodeHash()));
        if (withSlotWrite)
        {
            writeFlatRow(
                view, accountSlotKey(addr, makeHash(0x02)), makeEntry(std::string_view{"\x77", 1}));
        }
        return bcos::task::syncWait(buildAndCollect(storage, parentRoot, view, /*l2Mode=*/false));
    };
    auto plain = death(false);
    auto withSlots = death(true);

    MPTReadView<NodeStorage> readView(storage, withSlots.stateRoot);
    BOOST_CHECK(!bcos::task::syncWait(readView.readAccount(addr)).has_value());
    BOOST_CHECK(withSlots.obsoletedNodes.contains(before.storageRoot));
    BOOST_CHECK(withSlots.stateRoot == plain.stateRoot);
    // no storage-trie node was produced for the dropped slot write
    BOOST_CHECK_EQUAL(withSlots.newNodes.size(), plain.newNodes.size());
    for (auto const& [hash, rlp] : plain.newNodes)
    {
        BOOST_CHECK(withSlots.newNodes.contains(hash));
    }
}

// nonce 0 + balance 0 but real code is NOT empty: the leaf stays.
BOOST_AUTO_TEST_CASE(ZeroBalanceContractStays)
{
    NodeStorage storage;
    auto const addr = makeAddress(0xE4);
    Account contract;
    contract.codeHash = makeHash(0xC4);
    auto const parentRoot = seedStateTrieFlushed(storage, {{addr, contract}});

    FlatBackendStorage flatBackend;
    auto view = makeFlatView(flatBackend);
    writeFlatRow(view, accountFieldKey(addr, ROW_BALANCE), makeEntry("0"));

    auto output =
        bcos::task::syncWait(buildAndCollect(storage, parentRoot, view, /*l2Mode=*/false));

    MPTReadView<NodeStorage> readView(storage, output.stateRoot);
    auto account = bcos::task::syncWait(readView.readAccount(addr));
    BOOST_REQUIRE(account.has_value());
    BOOST_CHECK(account->codeHash == makeHash(0xC4));
}

// A brand-new account that receives a nonce (CREATE sets nonce 1) is not empty.
BOOST_AUTO_TEST_CASE(NoncedFirstTouchIsKept)
{
    NodeStorage storage;
    auto const addr = makeAddress(0xE5);
    FlatBackendStorage flatBackend;
    auto view = makeFlatView(flatBackend);
    writeFlatRow(view, accountFieldKey(addr, ROW_NONCE), makeEntry("1"));
    writeFlatRow(view, accountFieldKey(addr, ROW_BALANCE), makeEntry("0"));
    writeFlatRow(view, accountFieldKey(addr, ROW_CODE_HASH), codeHashEntry(emptyCodeHash()));

    auto output =
        bcos::task::syncWait(buildAndCollect(storage, emptyRootHash(), view, /*l2Mode=*/false));
    MPTReadView<NodeStorage> readView(storage, output.stateRoot);
    BOOST_CHECK(bcos::task::syncWait(readView.readAccount(addr)).has_value());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ledger::mpt::test
