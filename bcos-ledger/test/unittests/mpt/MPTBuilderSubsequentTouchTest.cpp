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
 * @file MPTBuilderSubsequentTouchTest.cpp
 * @brief MPTBuilder subsequent-touch path over the fork view (spec §5.3 path 1, §5.4) — M4.3
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
#include <bcos-ledger/mpt/Trie.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <boost/test/unit_test.hpp>
#include <map>
#include <optional>
#include <string_view>
#include <vector>

namespace bcos::ledger::mpt::test
{

BOOST_AUTO_TEST_SUITE(MPTBuilderSubsequentTouchSuite)

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

// The from-scratch oracle for a storage trie root over expected raw slot values.
bcos::h256 storageRootOracle(std::map<bcos::h256, bcos::bytes> const& slots)
{
    std::map<bcos::h256, bcos::bytes> entries;
    for (auto const& [slot, value] : slots)
    {
        auto encoded = encodeStorageValue(bcos::ref(value));
        if (!encoded.empty())
        {
            entries[slotKeyHash(slot)] = std::move(encoded);
        }
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

// extractTouchedAccounts over the view's delta layer — the production feeding order.
std::vector<bcos::Address> touchedOf(FlatStateView& view)
{
    return bcos::task::syncWait(extractTouchedAccounts(mutableStorage(view)));
}
}  // namespace

BOOST_AUTO_TEST_CASE(IncrementalUpdateOfExistingAccountStorage)
{
    NodeStorage storage;

    // Parent state: one account, three storage slots.
    auto const addr = makeAddress(0xAB);
    std::map<bcos::h256, bcos::bytes> const priorSlots{
        {slotKey(0x00), bcos::bytes{0x10}},
        {slotKey(0x01), bcos::bytes{0x11}},
        {slotKey(0x02), bcos::bytes{0x12}},
    };
    Account prior;
    prior.nonce = 1;
    prior.balance = 100;
    prior.storageRoot = buildStorageTrie(storage, priorSlots);
    auto const parentRoot = buildStateTrie(storage, {{addr, prior}});

    // This block's delta: bump the nonce, overwrite slot 0, delete slot 1 (storage-level
    // logical deletion), add slot 3.
    FlatBackendStorage flatBackend;
    auto view = makeFlatView(flatBackend);
    writeFlatRow(view, accountFieldKey(addr, ROW_NONCE), makeEntry("2"));
    writeFlatRow(view, accountSlotKey(addr, slotKey(0x00)), slotEntry(bcos::bytes{0xFF}));
    deleteFlatRowLogically(view, accountSlotKey(addr, slotKey(0x01)));
    writeFlatRow(view, accountSlotKey(addr, slotKey(0x03)), slotEntry(bcos::bytes{0x33}));

    auto const touched = touchedOf(view);
    BOOST_REQUIRE_EQUAL(touched.size(), 1U);
    BOOST_CHECK(touched.front() == addr);

    auto output =
        bcos::task::syncWait(buildAndCollect(storage, parentRoot, view, /*l2Mode=*/false));

    BOOST_CHECK(output.stateRoot != parentRoot);
    BOOST_REQUIRE(!output.newNodes.empty());

    // Read the updated account back through the new root.
    MPTReadView<NodeStorage> readView(storage, output.stateRoot);
    auto updated = bcos::task::syncWait(readView.readAccount(addr));
    BOOST_REQUIRE(updated.has_value());
    BOOST_CHECK_EQUAL(updated->nonce, bcos::u256(2));
    BOOST_CHECK_EQUAL(updated->balance, bcos::u256(100));  // untouched field keeps its baseline
    BOOST_CHECK(updated->codeHash == prior.codeHash);

    // The new storage root must equal a from-scratch build over the expected slot set.
    std::map<bcos::h256, bcos::bytes> const expectedSlots{
        {slotKey(0x00), bcos::bytes{0xFF}},
        {slotKey(0x02), bcos::bytes{0x12}},
        {slotKey(0x03), bcos::bytes{0x33}},
    };
    BOOST_CHECK(updated->storageRoot == storageRootOracle(expectedSlots));

    // And the flushed nodes must actually resolve: read every expected slot through a Trie.
    Trie<NodeStorage> storageTrie(storage, updated->storageRoot);
    for (auto const& [slot, value] : expectedSlots)
    {
        auto leaf = bcos::task::syncWait(storageTrie.get(slotKeyHash(slot)));
        BOOST_REQUIRE(leaf.has_value());
        BOOST_CHECK(*leaf == encodeStorageValue(bcos::ref(value)));
    }
    auto deleted = bcos::task::syncWait(storageTrie.get(slotKeyHash(slotKey(0x01))));
    BOOST_CHECK(!deleted.has_value());

    // The account trie itself must match a from-scratch build of the updated leaf.
    Account expectedAccount = prior;
    expectedAccount.nonce = 2;
    expectedAccount.storageRoot = updated->storageRoot;
    std::map<bcos::h256, bcos::bytes> stateEntries{
        {accountKeyHash(addr), expectedAccount.encode()}};
    BOOST_CHECK(output.stateRoot == computeTrieRoot(stateEntries).root);
}

BOOST_AUTO_TEST_CASE(ZeroValueWriteLeavesTheTrieLikeADelete)
{
    NodeStorage storage;
    auto const addr = makeAddress(0xCD);
    std::map<bcos::h256, bcos::bytes> const priorSlots{
        {slotKey(0x00), bcos::bytes{0x10}}, {slotKey(0x01), bcos::bytes{0x11}}};
    Account prior;
    prior.storageRoot = buildStorageTrie(storage, priorSlots);
    auto const parentRoot = buildStateTrie(storage, {{addr, prior}});

    // Writing an all-zero value trims to nothing — same trie effect as a delete (spec §5.3).
    FlatBackendStorage flatBackend;
    auto view = makeFlatView(flatBackend);
    writeFlatRow(
        view, accountSlotKey(addr, slotKey(0x01)), slotEntry(bcos::bytes{0x00, 0x00, 0x00}));

    auto output =
        bcos::task::syncWait(buildAndCollect(storage, parentRoot, view, /*l2Mode=*/false));

    MPTReadView<NodeStorage> readView(storage, output.stateRoot);
    auto updated = bcos::task::syncWait(readView.readAccount(addr));
    BOOST_REQUIRE(updated.has_value());
    BOOST_CHECK(updated->storageRoot == storageRootOracle({{slotKey(0x00), bcos::bytes{0x10}}}));
}

BOOST_AUTO_TEST_CASE(NoStorageChangesKeepsPriorStorageRoot)
{
    NodeStorage storage;
    auto const addr = makeAddress(0xEE);
    Account prior;
    prior.balance = 7;
    prior.storageRoot = buildStorageTrie(storage, {{slotKey(0x00), bcos::bytes{0x42}}});
    auto const parentRoot = buildStateTrie(storage, {{addr, prior}});

    FlatBackendStorage flatBackend;
    auto view = makeFlatView(flatBackend);
    writeFlatRow(view, accountFieldKey(addr, ROW_BALANCE), makeEntry("8"));

    auto output =
        bcos::task::syncWait(buildAndCollect(storage, parentRoot, view, /*l2Mode=*/false));

    MPTReadView<NodeStorage> readView(storage, output.stateRoot);
    auto updated = bcos::task::syncWait(readView.readAccount(addr));
    BOOST_REQUIRE(updated.has_value());
    BOOST_CHECK_EQUAL(updated->balance, bcos::u256(8));
    BOOST_CHECK(updated->storageRoot == prior.storageRoot);
}

BOOST_AUTO_TEST_CASE(MultipleAccountsInOneBlock)
{
    NodeStorage storage;
    auto const addrA = makeAddress(0x0A);
    auto const addrB = makeAddress(0x0B);
    Account priorA;
    priorA.nonce = 1;
    Account priorB;
    priorB.balance = 50;
    auto const parentRoot = buildStateTrie(storage, {{addrA, priorA}, {addrB, priorB}});

    FlatBackendStorage flatBackend;
    auto view = makeFlatView(flatBackend);
    writeFlatRow(view, accountFieldKey(addrA, ROW_NONCE), makeEntry("2"));
    writeFlatRow(view, accountSlotKey(addrB, slotKey(0x05)), slotEntry(bcos::bytes{0x55}));

    auto const touched = touchedOf(view);
    BOOST_REQUIRE_EQUAL(touched.size(), 2U);

    auto output =
        bcos::task::syncWait(buildAndCollect(storage, parentRoot, view, /*l2Mode=*/false));

    Account expectedA = priorA;
    expectedA.nonce = 2;
    Account expectedB = priorB;
    expectedB.storageRoot = storageRootOracle({{slotKey(0x05), bcos::bytes{0x55}}});
    std::map<bcos::h256, bcos::bytes> stateEntries{
        {accountKeyHash(addrA), expectedA.encode()},
        {accountKeyHash(addrB), expectedB.encode()},
    };
    BOOST_CHECK(output.stateRoot == computeTrieRoot(stateEntries).root);
}

BOOST_AUTO_TEST_CASE(DeletedFieldOutsideTombstoneThrows)
{
    NodeStorage storage;
    auto const addr = makeAddress(0x02);
    auto const parentRoot = buildStateTrie(storage, {{addr, Account{}}});

    // Only the nonce row is deleted — not the all-three-core-rows selfdestruct shape (spec
    // §5.4 treats a partial deletion as an error).
    FlatBackendStorage flatBackend;
    auto view = makeFlatView(flatBackend);
    deleteFlatRowLogically(view, accountFieldKey(addr, ROW_NONCE));

    BOOST_CHECK_THROW(
        bcos::task::syncWait(buildAndCollect(storage, parentRoot, view, /*l2Mode=*/false)),
        MPTInvariantViolation);
}

BOOST_AUTO_TEST_CASE(BcosExtensionRowSkippedInScenarioAThrowsInL2)
{
    NodeStorage storage;
    auto const addr = makeAddress(0x03);
    Account prior;
    prior.nonce = 4;
    auto const parentRoot = buildStateTrie(storage, {{addr, prior}});

    // A BCOS extension row ("abi") plus a real nonce write. "code" is skipped in BOTH modes —
    // it enters the MPT indirectly via its paired codeHash row.
    FlatBackendStorage flatBackend;
    auto view = makeFlatView(flatBackend);
    writeFlatRow(view, accountFieldKey(addr, "abi"), makeEntry("[]"));
    writeFlatRow(view, accountFieldKey(addr, ROW_CODE), makeEntry("\x60\x60"));
    writeFlatRow(view, accountFieldKey(addr, ROW_NONCE), makeEntry("5"));
    // Scenario A (native chain): the extension row is ignored, the nonce lands.
    auto output =
        bcos::task::syncWait(buildAndCollect(storage, parentRoot, view, /*l2Mode=*/false));
    MPTReadView<NodeStorage> readView(storage, output.stateRoot);
    auto updated = bcos::task::syncWait(readView.readAccount(addr));
    BOOST_REQUIRE(updated.has_value());
    BOOST_CHECK_EQUAL(updated->nonce, bcos::u256(5));

    // Scenario B (Ethereum-compatible): the same delta throws — an unrecognized row must not
    // silently fall out of the state commitment.
    BOOST_CHECK_THROW(
        bcos::task::syncWait(buildAndCollect(storage, parentRoot, view, /*l2Mode=*/true)),
        UnexpectedBCOSFieldInL2);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::ledger::mpt::test
