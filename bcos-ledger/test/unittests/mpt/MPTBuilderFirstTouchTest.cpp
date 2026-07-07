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
 * @file MPTBuilderFirstTouchTest.cpp
 * @brief MPTBuilder first-touch path + preheat manifest (spec §5.3 path 2, §5.11) — M4.4
 */
#include "TestHelpers.h"
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-ledger/mpt/Account.h>
#include <bcos-ledger/mpt/AccountDelta.h>
#include <bcos-ledger/mpt/Constants.h>
#include <bcos-ledger/mpt/Errors.h>
#include <bcos-ledger/mpt/HashBuilder.h>
#include <bcos-ledger/mpt/MPTBuilder.h>
#include <bcos-ledger/mpt/MPTReadView.h>
#include <bcos-ledger/mpt/PreheatManifest.h>
#include <bcos-ledger/mpt/StorageValueCodec.h>
#include <bcos-ledger/mpt/Trie.h>
#include <bcos-task/Task.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/test/unit_test.hpp>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace bcos::ledger::mpt::test
{

BOOST_AUTO_TEST_SUITE(MPTBuilderFirstTouchSuite)

namespace
{
using NodeStorage = bcos::storage2::memory_storage::MemoryStorage<bcos::h256, bcos::bytes>;
using FlatSlots = std::vector<std::pair<bcos::h256, bcos::bytes>>;

// A distinct 32-byte slot key for index i (two marker bytes cover up to 65536 slots).
bcos::h256 slotKeyAt(size_t i)
{
    bcos::h256 h{};
    h.data()[0] = static_cast<bcos::byte>(i & 0xFF);
    h.data()[1] = static_cast<bcos::byte>((i >> 8) & 0xFF);
    return h;
}

// A short, never-all-zero raw slot value for index i.
bcos::bytes slotValueAt(size_t i)
{
    return bcos::bytes{
        static_cast<bcos::byte>(i % 251 + 1), static_cast<bcos::byte>((i / 251) % 251 + 1)};
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

// A manifest reader over a single (addr → storageRoot) entry, mimicking the preheat CLI's
// "__mpt_preheat:<hex-addr>" → 32-byte-root KV record.
PreheatManifest::BackendReader singleEntryManifest(bcos::Address const& addr, bcos::h256 root)
{
    auto key = PreheatManifest::makeKey(addr);
    return [key = std::move(key), root](
               std::string requested) -> bcos::task::Task<std::optional<bcos::bytes>> {
        if (requested == key)
        {
            co_return bcos::bytes(root.data(), root.data() + bcos::h256::SIZE);
        }
        co_return std::nullopt;
    };
}

// A scanner that must never run (manifest hit / no-storage cases); flips @p called for asserts.
std::function<bcos::task::Task<FlatSlots>(bcos::Address const&)> failingScanner(bool& called)
{
    return [&called](bcos::Address const&) -> bcos::task::Task<FlatSlots> {
        called = true;
        BOOST_FAIL("flatSlotScanner must not be called");
        co_return FlatSlots{};
    };
}

// Read every expected slot (and check absent ones) through a Trie walk over @p storage.
void checkSlotReadback(NodeStorage& storage, bcos::h256 storageRoot,
    std::map<bcos::h256, bcos::bytes> const& expectedSlots,
    std::vector<bcos::h256> const& absentSlots)
{
    Trie<NodeStorage> trie(storage, storageRoot);
    for (auto const& [slot, value] : expectedSlots)
    {
        auto leaf = bcos::task::syncWait(trie.get(slotKeyHash(slot)));
        BOOST_REQUIRE(leaf.has_value());
        BOOST_CHECK(*leaf == encodeStorageValue(bcos::ref(value)));
    }
    for (auto const& slot : absentSlots)
    {
        auto leaf = bcos::task::syncWait(trie.get(slotKeyHash(slot)));
        BOOST_CHECK(!leaf.has_value());
    }
}
}  // namespace

BOOST_AUTO_TEST_CASE(PreheatManifestKeyFormat)
{
    auto expected = std::string{"__mpt_preheat:ab"};
    expected.append(38, '0');  // the remaining 19 address bytes, lowercase hex
    BOOST_CHECK_EQUAL(PreheatManifest::makeKey(makeAddress(0xAB)), expected);
}

BOOST_AUTO_TEST_CASE(ManifestValueWrongSizeThrows)
{
    PreheatManifest::BackendReader reader =
        [](std::string) -> bcos::task::Task<std::optional<bcos::bytes>> {
        co_return bcos::bytes{0x01, 0x02};  // not a 32-byte storage root
    };
    BOOST_CHECK_THROW(bcos::task::syncWait(PreheatManifest::read(reader, makeAddress(0x01))),
        MPTInvariantViolation);
}

BOOST_AUTO_TEST_CASE(FirstTouchOfBrandNewAccountWithoutStorage)
{
    NodeStorage storage;
    auto const addr = makeAddress(0xA1);

    MPTBuilderBackends backends;
    backends.flatSlotScanner = [](bcos::Address const&) -> bcos::task::Task<FlatSlots> {
        co_return FlatSlots{};  // brand-new account: no flat slots exist
    };

    MPTBuildInput input;
    auto& delta = input.perAccount[addr];
    delta.firstTouch = true;
    delta.nonceState = AccountDelta::FieldState::Updated;
    delta.nonce = 1;
    delta.balanceState = AccountDelta::FieldState::Updated;
    delta.balance = 1000;
    delta.codeHashState = AccountDelta::FieldState::Updated;
    delta.codeHash = makeHash(0xC0);

    MPTBuilder builder(storage, emptyRootHash(), std::move(backends));
    auto output = bcos::task::syncWait(builder.buildAndCollect(input));

    BOOST_CHECK(output.preheatManifestsToDelete.empty());
    MPTReadView<NodeStorage> view(storage, output.stateRoot);
    auto account = bcos::task::syncWait(view.readAccount(addr));
    BOOST_REQUIRE(account.has_value());
    BOOST_CHECK(account->storageRoot == emptyRootHash());
    BOOST_CHECK_EQUAL(account->nonce, bcos::u256(1));
    BOOST_CHECK_EQUAL(account->balance, bcos::u256(1000));
    BOOST_CHECK(account->codeHash == makeHash(0xC0));
}

BOOST_AUTO_TEST_CASE(FirstTouchOfDormantAccountWithExistingSlots)
{
    NodeStorage storage;
    auto const addr = makeAddress(0xA2);

    // 1000 live flat slots, plus one all-zero value the bootstrap must skip.
    std::map<bcos::h256, bcos::bytes> flatSlots;
    for (size_t i = 0; i < 1000; ++i)
    {
        flatSlots[slotKeyAt(i)] = slotValueAt(i);
    }
    FlatSlots scanResult(flatSlots.begin(), flatSlots.end());
    scanResult.emplace_back(slotKeyAt(1000), bcos::bytes{0x00, 0x00});

    MPTBuilderBackends backends;
    backends.flatSlotScanner = [&scanResult](bcos::Address const&) -> bcos::task::Task<FlatSlots> {
        co_return scanResult;
    };

    MPTBuildInput input;
    auto& delta = input.perAccount[addr];
    delta.firstTouch = true;
    delta.nonceState = AccountDelta::FieldState::Updated;
    delta.nonce = 7;
    delta.balanceState = AccountDelta::FieldState::Updated;
    delta.balance = 70;
    delta.codeHashState = AccountDelta::FieldState::Updated;
    delta.codeHash = makeHash(0xC2);

    MPTBuilder builder(storage, emptyRootHash(), std::move(backends));
    auto output = bcos::task::syncWait(builder.buildAndCollect(input));

    MPTReadView<NodeStorage> view(storage, output.stateRoot);
    auto account = bcos::task::syncWait(view.readAccount(addr));
    BOOST_REQUIRE(account.has_value());
    // The zero-value slot is not in the oracle either (encodeStorageValue trims it away).
    BOOST_CHECK(account->storageRoot == storageRootOracle(flatSlots));
}

BOOST_AUTO_TEST_CASE(FirstTouchEqualsSubsequentTouchAfterPreheat)
{
    auto const addr = makeAddress(0xA3);

    // The dormant account's 50 flat slots, and this block's changes over them.
    std::map<bcos::h256, bcos::bytes> flatSlots;
    for (size_t i = 0; i < 50; ++i)
    {
        flatSlots[slotKeyAt(i)] = slotValueAt(i);
    }
    auto const overwrittenSlot = slotKeyAt(3);
    auto const deletedSlot = slotKeyAt(7);
    auto const addedSlot = slotKeyAt(200);

    auto makeDelta = [&](AccountDelta& delta) {
        delta.firstTouch = true;
        delta.nonceState = AccountDelta::FieldState::Updated;
        delta.nonce = 3;
        delta.balanceState = AccountDelta::FieldState::Updated;
        delta.balance = 30;
        delta.codeHashState = AccountDelta::FieldState::Updated;
        delta.codeHash = makeHash(0xC3);
        delta.storageChanges[overwrittenSlot] = bcos::bytes{0xEE};
        delta.storageChanges[deletedSlot] = std::nullopt;
        delta.storageChanges[addedSlot] = bcos::bytes{0xDD};
    };

    // Path A: first-touch bootstrap over the flat scan, then apply the changes.
    NodeStorage storageA;
    MPTBuilderBackends backendsA;
    backendsA.flatSlotScanner = [&flatSlots](bcos::Address const&) -> bcos::task::Task<FlatSlots> {
        co_return FlatSlots(flatSlots.begin(), flatSlots.end());
    };
    MPTBuildInput inputA;
    makeDelta(inputA.perAccount[addr]);
    MPTBuilder builderA(storageA, emptyRootHash(), std::move(backendsA));
    auto outputA = bcos::task::syncWait(builderA.buildAndCollect(inputA));

    // Path B: simulate the preheat CLI — build the identical baseline trie directly into node
    // storage, record its root in the manifest, then run first-touch with a scanner that must
    // never fire.
    NodeStorage storageB;
    HashBuilder baseline(storageB, emptyRootHash());
    for (auto const& [slot, value] : flatSlots)
    {
        bcos::task::syncWait(baseline.put(slotKeyHash(slot), encodeStorageValue(bcos::ref(value))));
    }
    auto const baselineRoot = bcos::task::syncWait(baseline.commit());

    bool scannerCalled = false;
    MPTBuilderBackends backendsB;
    backendsB.flatSlotScanner = failingScanner(scannerCalled);
    backendsB.manifestReader = singleEntryManifest(addr, baselineRoot);
    MPTBuildInput inputB;
    makeDelta(inputB.perAccount[addr]);
    MPTBuilder builderB(storageB, emptyRootHash(), std::move(backendsB));
    auto outputB = bcos::task::syncWait(builderB.buildAndCollect(inputB));

    BOOST_CHECK(!scannerCalled);
    // The invariant: both paths land on the identical state root.
    BOOST_CHECK(outputA.stateRoot == outputB.stateRoot);

    // And every slot reads back identically through both node stores.
    auto expectedSlots = flatSlots;
    expectedSlots[overwrittenSlot] = bcos::bytes{0xEE};
    expectedSlots.erase(deletedSlot);
    expectedSlots[addedSlot] = bcos::bytes{0xDD};

    MPTReadView<NodeStorage> viewA(storageA, outputA.stateRoot);
    auto accountA = bcos::task::syncWait(viewA.readAccount(addr));
    MPTReadView<NodeStorage> viewB(storageB, outputB.stateRoot);
    auto accountB = bcos::task::syncWait(viewB.readAccount(addr));
    BOOST_REQUIRE(accountA.has_value());
    BOOST_REQUIRE(accountB.has_value());
    BOOST_CHECK(accountA->storageRoot == accountB->storageRoot);
    checkSlotReadback(storageA, accountA->storageRoot, expectedSlots, {deletedSlot});
    checkSlotReadback(storageB, accountB->storageRoot, expectedSlots, {deletedSlot});
}

BOOST_AUTO_TEST_CASE(PreheatManifestHitDoesNotCallScanner)
{
    NodeStorage storage;
    auto const addr = makeAddress(0xA4);

    // Preheated baseline: two slots already in the node store, root in the manifest.
    std::map<bcos::h256, bcos::bytes> const baselineSlots{
        {slotKeyAt(0), bcos::bytes{0x10}}, {slotKeyAt(1), bcos::bytes{0x11}}};
    HashBuilder baseline(storage, emptyRootHash());
    for (auto const& [slot, value] : baselineSlots)
    {
        bcos::task::syncWait(baseline.put(slotKeyHash(slot), encodeStorageValue(bcos::ref(value))));
    }
    auto const baselineRoot = bcos::task::syncWait(baseline.commit());

    bool scannerCalled = false;
    MPTBuilderBackends backends;
    backends.flatSlotScanner = failingScanner(scannerCalled);
    backends.manifestReader = singleEntryManifest(addr, baselineRoot);

    MPTBuildInput input;
    auto& delta = input.perAccount[addr];
    delta.firstTouch = true;
    delta.nonceState = AccountDelta::FieldState::Updated;
    delta.nonce = 4;
    delta.balanceState = AccountDelta::FieldState::Updated;
    delta.balance = 40;
    delta.codeHashState = AccountDelta::FieldState::Updated;
    delta.codeHash = makeHash(0xC4);
    delta.storageChanges[slotKeyAt(2)] = bcos::bytes{0x12};

    MPTBuilder builder(storage, emptyRootHash(), std::move(backends));
    auto output = bcos::task::syncWait(builder.buildAndCollect(input));

    BOOST_CHECK(!scannerCalled);
    BOOST_REQUIRE_EQUAL(output.preheatManifestsToDelete.size(), 1U);
    BOOST_CHECK(output.preheatManifestsToDelete.front() == addr);

    auto expectedSlots = baselineSlots;
    expectedSlots[slotKeyAt(2)] = bcos::bytes{0x12};
    MPTReadView<NodeStorage> view(storage, output.stateRoot);
    auto account = bcos::task::syncWait(view.readAccount(addr));
    BOOST_REQUIRE(account.has_value());
    BOOST_CHECK(account->storageRoot == storageRootOracle(expectedSlots));
}

BOOST_AUTO_TEST_CASE(TombstonePriorityOverFirstTouch)
{
    NodeStorage storage;
    MPTBuildInput input;
    auto& delta = input.perAccount[makeAddress(0xA5)];
    delta.tombstone = true;
    delta.firstTouch = true;  // pathological input: tombstone must win (spec §5.4)

    MPTBuilder builder(storage, emptyRootHash());
    BOOST_CHECK_EXCEPTION(bcos::task::syncWait(builder.buildAndCollect(input)),
        MPTInvariantViolation, [](MPTInvariantViolation const& e) {
            return boost::diagnostic_information(e).find("PR-12") != std::string::npos;
        });
}

BOOST_AUTO_TEST_CASE(FirstTouchWithoutScannerThrows)
{
    NodeStorage storage;
    auto const addr = makeAddress(0xA6);

    MPTBuilderBackends backends;  // manifest misses, and no scanner to fall back to
    backends.manifestReader = [](std::string) -> bcos::task::Task<std::optional<bcos::bytes>> {
        co_return std::nullopt;
    };

    MPTBuildInput input;
    input.perAccount[addr].firstTouch = true;

    MPTBuilder builder(storage, emptyRootHash(), std::move(backends));
    BOOST_CHECK_THROW(bcos::task::syncWait(builder.buildAndCollect(input)), MPTInvariantViolation);
}

BOOST_AUTO_TEST_CASE(UnchangedMetaFieldsComeFromFlat)
{
    NodeStorage storage;
    auto const addr = makeAddress(0xA7);

    int metaCalls = 0;
    MPTBuilderBackends backends;
    backends.flatSlotScanner = [](bcos::Address const&) -> bcos::task::Task<FlatSlots> {
        co_return FlatSlots{};
    };
    backends.flatAccountMeta = [&metaCalls](
                                   bcos::Address const&) -> bcos::task::Task<FlatAccountMeta> {
        ++metaCalls;
        FlatAccountMeta meta;
        meta.nonce = 99;  // must lose to the delta's Updated nonce
        meta.balance = 1234;
        meta.codeHash = makeHash(0x77);
        co_return meta;
    };

    MPTBuildInput input;
    auto& delta = input.perAccount[addr];
    delta.firstTouch = true;
    delta.nonceState = AccountDelta::FieldState::Updated;
    delta.nonce = 5;
    // balance / codeHash stay Unchanged: the flat baseline must supply them.

    MPTBuilder builder(storage, emptyRootHash(), std::move(backends));
    auto output = bcos::task::syncWait(builder.buildAndCollect(input));

    BOOST_CHECK_EQUAL(metaCalls, 1);
    MPTReadView<NodeStorage> view(storage, output.stateRoot);
    auto account = bcos::task::syncWait(view.readAccount(addr));
    BOOST_REQUIRE(account.has_value());
    BOOST_CHECK_EQUAL(account->nonce, bcos::u256(5));
    BOOST_CHECK_EQUAL(account->balance, bcos::u256(1234));
    BOOST_CHECK(account->codeHash == makeHash(0x77));

    // The same delta without a flatAccountMeta backend cannot fill the Unchanged fields.
    NodeStorage storage2;
    MPTBuilderBackends noMeta;
    noMeta.flatSlotScanner = [](bcos::Address const&) -> bcos::task::Task<FlatSlots> {
        co_return FlatSlots{};
    };
    MPTBuildInput input2;
    auto& delta2 = input2.perAccount[addr];
    delta2.firstTouch = true;
    delta2.nonceState = AccountDelta::FieldState::Updated;
    delta2.nonce = 5;
    MPTBuilder builder2(storage2, emptyRootHash(), std::move(noMeta));
    BOOST_CHECK_THROW(
        bcos::task::syncWait(builder2.buildAndCollect(input2)), MPTInvariantViolation);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::ledger::mpt::test
