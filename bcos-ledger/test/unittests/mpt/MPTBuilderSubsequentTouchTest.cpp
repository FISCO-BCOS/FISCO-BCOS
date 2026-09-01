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

#include "bcos-ledger/test/unittests/ExceptionCheck.h"

namespace bcos::ledger::mpt::test
{
using bcos::test::errinfoContains;

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

BOOST_AUTO_TEST_CASE(NonAccountTablesDoNotSplitOrPolluteAccountRuns)
{
    // The delta is scanned in one pass, so a foreign table must neither be folded into an
    // account nor cut a run short. The load-bearing cases are the ones that (table, key) order
    // places BETWEEN the two accounts: a table whose name is addrA's plus a suffix sorts right
    // after addrA's run and before addrB's, yet parseAccountTable rejects it (not 40 hex).
    // "/app", "/apps/shortname" and "/sys/config" cover the head and tail of the scan.
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
    writeFlatRow(view, bcos::executor_v1::StateKey{"/app", "before"}, makeEntry("x"));
    writeFlatRow(view, accountFieldKey(addrA, ROW_NONCE), makeEntry("2"));
    writeFlatRow(view, accountSlotKey(addrB, slotKey(0x05)), slotEntry(bcos::bytes{0x55}));
    writeFlatRow(view, bcos::executor_v1::StateKey{"/sys/config", "after"}, makeEntry("y"));
    // A short-name BCOS contract table: inside no account, parseAccountTable rejects it.
    writeFlatRow(view, bcos::executor_v1::StateKey{"/apps/shortname", "abi"}, makeEntry("z"));
    // The interleaving pair: same prefix as addrA's table, so they sort between the two
    // accounts' runs. Were their rows mistaken for account rows, addrB would inherit them.
    auto const addrATable = accountTableName(addrA);
    writeFlatRow(view, bcos::executor_v1::StateKey{addrATable + "0", ROW_NONCE}, makeEntry("999"));
    writeFlatRow(
        view, bcos::executor_v1::StateKey{addrATable + "zz", ROW_BALANCE}, makeEntry("888"));

    auto output =
        bcos::task::syncWait(buildAndCollect(storage, parentRoot, view, /*l2Mode=*/false));

    // Exactly the two real accounts moved, with exactly the values their own rows carried.
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

    BOOST_CHECK_EXCEPTION(
        bcos::task::syncWait(buildAndCollect(storage, parentRoot, view, /*l2Mode=*/false)),
        MPTInvariantViolation,
        [](auto const& e) { return errinfoContains(e, "deleted outside a tombstone"); });
}

BOOST_AUTO_TEST_CASE(ExtensionOnlyAccountAbsentFromParentStaysOutOfTheMPT)
{
    // An account the block touched ONLY through a BCOS extension row carries no Ethereum state
    // change. Inventing a leaf for it would read Yellow Paper defaults from flat (nonce 0,
    // balance 0, emptyCodeHash) and insert an EIP-161 empty account — which no Ethereum
    // implementation keeps in state, so the root would fork.
    NodeStorage storage;
    auto const existing = makeAddress(0x0A);
    auto const extensionOnly = makeAddress(0x0B);
    Account prior;
    prior.nonce = 1;
    auto const parentRoot = buildStateTrie(storage, {{existing, prior}});

    FlatBackendStorage flatBackend;
    auto view = makeFlatView(flatBackend);
    writeFlatRow(view, accountFieldKey(extensionOnly, "status"), makeEntry("1"));
    writeFlatRow(view, accountFieldKey(extensionOnly, "abi"), makeEntry("[]"));

    auto output =
        bcos::task::syncWait(buildAndCollect(storage, parentRoot, view, /*l2Mode=*/false));

    BOOST_CHECK(output.stateRoot == parentRoot);  // nothing entered the trie
    MPTReadView<NodeStorage> readView(storage, output.stateRoot);
    BOOST_CHECK(!bcos::task::syncWait(readView.readAccount(extensionOnly)).has_value());
}

BOOST_AUTO_TEST_CASE(CodeOnlyAccountAbsentFromParentStaysOutOfTheMPT)
{
    // Same rule for a bare code row. Code never enters the trie (the leaf commits to codeHash),
    // so a run holding only a code row means nothing the MPT commits to has changed — including
    // under EIP-7702, where a delegation designator is code whose keccak IS the codeHash.
    NodeStorage storage;
    auto const existing = makeAddress(0x0A);
    auto const codeOnly = makeAddress(0x0C);
    Account prior;
    prior.nonce = 1;
    auto const parentRoot = buildStateTrie(storage, {{existing, prior}});

    FlatBackendStorage flatBackend;
    auto view = makeFlatView(flatBackend);
    writeFlatRow(view, accountFieldKey(codeOnly, ROW_CODE), makeEntry("\x60\x80\x60\x40"));

    auto output =
        bcos::task::syncWait(buildAndCollect(storage, parentRoot, view, /*l2Mode=*/false));

    BOOST_CHECK(output.stateRoot == parentRoot);
    MPTReadView<NodeStorage> readView(storage, output.stateRoot);
    BOOST_CHECK(!bcos::task::syncWait(readView.readAccount(codeOnly)).has_value());
}

BOOST_AUTO_TEST_CASE(ExtensionRowAlongsideACoreFieldStillCommitsTheAccount)
{
    // The guard keys off "no Ethereum row at all", not "an extension row was present": an
    // account whose run mixes an extension row with a real core-field write must still commit.
    NodeStorage storage;
    auto const addr = makeAddress(0x0D);
    Account prior;
    prior.nonce = 1;
    auto const parentRoot = buildStateTrie(storage, {{addr, prior}});

    FlatBackendStorage flatBackend;
    auto view = makeFlatView(flatBackend);
    writeFlatRow(view, accountFieldKey(addr, "status"), makeEntry("1"));
    writeFlatRow(view, accountFieldKey(addr, ROW_NONCE), makeEntry("7"));

    auto output =
        bcos::task::syncWait(buildAndCollect(storage, parentRoot, view, /*l2Mode=*/false));

    BOOST_CHECK(output.stateRoot != parentRoot);
    MPTReadView<NodeStorage> readView(storage, output.stateRoot);
    auto updated = bcos::task::syncWait(readView.readAccount(addr));
    BOOST_REQUIRE(updated.has_value());
    BOOST_CHECK_EQUAL(updated->nonce, bcos::u256(7));
}

BOOST_AUTO_TEST_CASE(Eip7702DelegatedEoaCommitsLikeAnyOtherAccount)
{
    // EIP-7702 gives an EOA a delegation designator (0xef0100 || address) as its code, and a
    // delegated call operates on the AUTHORITY's own storage. For the MPT that is not a special
    // case at all — the leaf is the same four-tuple: the designator itself stays out of the trie
    // (code never enters it), its keccak lands in codeHash, the authorization bumps nonce, and
    // the slots the delegated call writes build the EOA's storage trie. This test pins that the
    // builder needs no 7702-specific handling to produce the standard Ethereum leaf.
    NodeStorage storage;
    auto const eoa = makeAddress(0x77);
    Account prior;  // a plain EOA: nonce 0, no code, no storage
    auto const parentRoot = buildStateTrie(storage, {{eoa, prior}});

    // keccak of a 23-byte designator — the value the executor would write into the codeHash row.
    bcos::crypto::hasher::openssl::OpenSSL_Keccak256_Hasher hasher;
    bcos::bytes const designator{0xef, 0x01, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
        0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x02, 0x03, 0x04, 0x05};
    hasher.update(designator);
    bcos::h256 designatorHash;
    hasher.final(designatorHash);

    FlatBackendStorage flatBackend;
    auto view = makeFlatView(flatBackend);
    writeFlatRow(view, accountFieldKey(eoa, ROW_NONCE), makeEntry("1"));  // authorization bump
    writeFlatRow(view, accountFieldKey(eoa, ROW_CODE_HASH),
        makeEntry(std::string_view(
            reinterpret_cast<char const*>(designatorHash.data()), bcos::h256::SIZE)));
    // The designator bytes themselves: skipped, they are represented by the codeHash row.
    writeFlatRow(view, accountFieldKey(eoa, ROW_CODE),
        makeEntry(
            std::string_view(reinterpret_cast<char const*>(designator.data()), designator.size())));
    // A delegated call writing the EOA's OWN storage.
    writeFlatRow(view, accountSlotKey(eoa, slotKey(0x01)), slotEntry(bcos::bytes{0x2a}));

    auto output =
        bcos::task::syncWait(buildAndCollect(storage, parentRoot, view, /*l2Mode=*/false));

    MPTReadView<NodeStorage> readView(storage, output.stateRoot);
    auto updated = bcos::task::syncWait(readView.readAccount(eoa));
    BOOST_REQUIRE(updated.has_value());
    BOOST_CHECK_EQUAL(updated->nonce, bcos::u256(1));
    BOOST_CHECK(updated->codeHash == designatorHash);
    BOOST_CHECK(updated->storageRoot == storageRootOracle({{slotKey(0x01), bcos::bytes{0x2a}}}));

    // The whole leaf equals a from-scratch build of the standard four-tuple.
    Account expected;
    expected.nonce = 1;
    expected.codeHash = designatorHash;
    expected.storageRoot = storageRootOracle({{slotKey(0x01), bcos::bytes{0x2a}}});
    std::map<bcos::h256, bcos::bytes> stateEntries{{accountKeyHash(eoa), expected.encode()}};
    BOOST_CHECK(output.stateRoot == computeTrieRoot(stateEntries).root);
}

BOOST_AUTO_TEST_CASE(ClearingA7702DelegationWritesEmptyCodeHash)
{
    // The standard shape for clearing a delegation: "code-less" is a VALUE, so the executor
    // writes keccak256("") into the codeHash row. Nothing special happens here — it merges like
    // any other written codeHash — and the account ends up as a plain EOA leaf.
    NodeStorage storage;
    auto const eoa = makeAddress(0x7E);
    Account prior;  // an EOA currently carrying a delegation designator
    prior.nonce = 3;
    prior.codeHash =
        bcos::h256{"0x00000000000000000000000000000000000000000000000000000000000000ff"};
    auto const parentRoot = buildStateTrie(storage, {{eoa, prior}});

    FlatBackendStorage flatBackend;
    auto view = makeFlatView(flatBackend);
    auto const empty = emptyCodeHash();
    writeFlatRow(view, accountFieldKey(eoa, ROW_CODE_HASH),
        makeEntry(std::string_view(reinterpret_cast<char const*>(empty.data()), bcos::h256::SIZE)));
    writeFlatRow(view, accountFieldKey(eoa, ROW_NONCE), makeEntry("4"));

    auto output =
        bcos::task::syncWait(buildAndCollect(storage, parentRoot, view, /*l2Mode=*/false));

    MPTReadView<NodeStorage> readView(storage, output.stateRoot);
    auto updated = bcos::task::syncWait(readView.readAccount(eoa));
    BOOST_REQUIRE(updated.has_value());
    BOOST_CHECK(updated->codeHash == emptyCodeHash());
    BOOST_CHECK_EQUAL(updated->nonce, bcos::u256(4));
}

BOOST_AUTO_TEST_CASE(DeletedCodeHashRowOnALiveAccountThrows)
{
    // A deleted codeHash row outside a tombstone is not a shape Ethereum produces (clearing a
    // delegation WRITES emptyCodeHash — see above). Resolving it to emptyCodeHash() instead
    // would silently turn this contract into a code-less account and fork the state root, so the
    // invariant fires. It also guards the corrupt-tombstone case: if nonce/balance deletions were
    // lost and only this one survived, the leaf would keep the destroyed contract's storageRoot.
    NodeStorage storage;
    auto const addr = makeAddress(0x0E);
    Account prior;
    prior.nonce = 3;
    prior.codeHash =
        bcos::h256{"0x00000000000000000000000000000000000000000000000000000000000000ff"};
    auto const parentRoot = buildStateTrie(storage, {{addr, prior}});

    FlatBackendStorage flatBackend;
    auto view = makeFlatView(flatBackend);
    deleteFlatRowLogically(view, accountFieldKey(addr, ROW_CODE_HASH));
    writeFlatRow(view, accountFieldKey(addr, ROW_NONCE), makeEntry("4"));

    BOOST_CHECK_EXCEPTION(bcos::task::syncWait(buildAndCollect(storage, parentRoot, view,
                              /*l2Mode=*/false)),
        MPTInvariantViolation,
        [](auto const& e) { return errinfoContains(e, "deleted outside a tombstone"); });
}

BOOST_AUTO_TEST_CASE(LoneDeletedCodeHashOnAFirstTouchAccountThrowsInsteadOfInsertingAnEmptyLeaf)
{
    // Same invariant, first-touch side. Without it this account would reach the leaf write as
    // {0, 0, emptyCodeHash, emptyRoot} — an EIP-161 empty account, exactly what sawEthereumRow
    // keeps out for code/extension-only runs. A deleted codeHash row IS an Ethereum-state row,
    // so that guard does not cover this; requireNotDeleted does.
    NodeStorage storage;
    auto const addr = makeAddress(0x0F);
    auto const parentRoot = buildStateTrie(storage, {});

    FlatBackendStorage flatBackend;
    auto view = makeFlatView(flatBackend);
    deleteFlatRowLogically(view, accountFieldKey(addr, ROW_CODE_HASH));

    BOOST_CHECK_EXCEPTION(bcos::task::syncWait(buildAndCollect(storage, parentRoot, view,
                              /*l2Mode=*/false)),
        MPTInvariantViolation,
        [](auto const& e) { return errinfoContains(e, "deleted outside a tombstone"); });
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
    BOOST_CHECK_EXCEPTION(
        bcos::task::syncWait(buildAndCollect(storage, parentRoot, view, /*l2Mode=*/true)),
        UnexpectedBCOSFieldInL2,
        [](auto const& e) { return errinfoContains(e, "BCOS extension field present in L2"); });
}

BOOST_AUTO_TEST_CASE(UnclassifiedRowFieldThrowsInBothModes)
{
    // A field name that is neither Ethereum state nor a known BCOS extension. Unlike "abi", it
    // does NOT get the scenario-A skip: nobody has judged whether it carries state, so skipping
    // it could drop state from the commitment with no signal. Both modes throw, and the two
    // failures are distinguishable — UnknownAccountRowField, not UnexpectedBCOSFieldInL2.
    NodeStorage storage;
    auto const addr = makeAddress(0x03);
    Account prior;
    prior.nonce = 4;
    auto const parentRoot = buildStateTrie(storage, {{addr, prior}});

    FlatBackendStorage flatBackend;
    auto view = makeFlatView(flatBackend);
    writeFlatRow(view, accountFieldKey(addr, "someFutureField"), makeEntry("1"));
    writeFlatRow(view, accountFieldKey(addr, ROW_NONCE), makeEntry("5"));

    BOOST_CHECK_EXCEPTION(
        bcos::task::syncWait(buildAndCollect(storage, parentRoot, view, /*l2Mode=*/false)),
        UnknownAccountRowField,
        [](auto const& e) { return errinfoContains(e, "unclassified account row field"); });
    BOOST_CHECK_EXCEPTION(
        bcos::task::syncWait(buildAndCollect(storage, parentRoot, view, /*l2Mode=*/true)),
        UnknownAccountRowField,
        [](auto const& e) { return errinfoContains(e, "unclassified account row field"); });
}

BOOST_AUTO_TEST_CASE(DeleteLastLeafThenInsertNewSlotRecomputesStorageRoot)
{
    // Regression (el-sync, block 1971298): the storage trie holds exactly ONE slot. The same
    // block clears it (SSTORE 0 -> trie empties to monostate) and then writes a DIFFERENT slot.
    // mergeTrie's phase-2 short-circuit must NOT return the prior root just because the fresh
    // leaf created by the insert wasn't marked dirty — otherwise the storageRoot silently stays
    // the old one and the state root forks (the pre-fix bug wrote priorRoot back).
    NodeStorage storage;
    auto const addr = makeAddress(0x1F);
    std::map<bcos::h256, bcos::bytes> const priorSlots{{slotKey(0x00), bcos::bytes{0x10}}};
    Account prior;
    prior.nonce = 1;
    prior.storageRoot = buildStorageTrie(storage, priorSlots);
    auto const parentRoot = buildStateTrie(storage, {{addr, prior}});

    // This block: clear the only slot 0x00 (all-zero write = delete), then write slot 0x01.
    FlatBackendStorage flatBackend;
    auto view = makeFlatView(flatBackend);
    writeFlatRow(view, accountFieldKey(addr, ROW_NONCE), makeEntry("2"));
    writeFlatRow(view, accountSlotKey(addr, slotKey(0x00)), slotEntry(bcos::bytes{0x00, 0x00}));
    writeFlatRow(view, accountSlotKey(addr, slotKey(0x01)), slotEntry(bcos::bytes{0x22}));

    auto output =
        bcos::task::syncWait(buildAndCollect(storage, parentRoot, view, /*l2Mode=*/false));

    // The storage trie must now hold ONLY the new slot — never the prior root, never empty.
    MPTReadView<NodeStorage> readView(storage, output.stateRoot);
    auto updated = bcos::task::syncWait(readView.readAccount(addr));
    BOOST_REQUIRE(updated.has_value());
    BOOST_CHECK(updated->storageRoot != prior.storageRoot);
    BOOST_CHECK(updated->storageRoot == storageRootOracle({{slotKey(0x01), bcos::bytes{0x22}}}));

    Trie<NodeStorage> storageTrie(storage, updated->storageRoot);
    auto newSlot = bcos::task::syncWait(storageTrie.get(slotKeyHash(slotKey(0x01))));
    BOOST_REQUIRE(newSlot.has_value());
    BOOST_CHECK(*newSlot == encodeStorageValue(bcos::ref(bcos::bytes{0x22})));
    auto oldSlot = bcos::task::syncWait(storageTrie.get(slotKeyHash(slotKey(0x00))));
    BOOST_CHECK(!oldSlot.has_value());
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::ledger::mpt::test
