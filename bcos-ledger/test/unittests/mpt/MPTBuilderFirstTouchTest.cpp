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
 * @brief MPTBuilder first-touch path, slot-level commitment (spec §4.2, §5.3 path 2,
 *        Revision 2026-07-09) — M4.4
 */
#include "TestHelpers.h"
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-ledger/mpt/Account.h>
#include <bcos-ledger/mpt/Classify.h>
#include <bcos-ledger/mpt/Constants.h>
#include <bcos-ledger/mpt/Errors.h>
#include <bcos-ledger/mpt/FlatToMPT.h>
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

BOOST_AUTO_TEST_SUITE(MPTBuilderFirstTouchSuite)

namespace
{
using NodeStorage = bcos::storage2::memory_storage::MemoryStorage<bcos::h256, bcos::bytes>;

// A distinct 32-byte slot key for index i (two marker bytes cover up to 65536 slots).
bcos::h256 slotKeyAt(size_t i)
{
    bcos::h256 h{};
    h.data()[0] = static_cast<bcos::byte>(i & 0xFF);
    h.data()[1] = static_cast<bcos::byte>((i >> 8) & 0xFF);
    return h;
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

// A raw-32-byte Entry for a codeHash row (the executor stores the digest verbatim).
bcos::storage::Entry codeHashEntry(bcos::h256 const& hash)
{
    return makeEntry(
        std::string_view{reinterpret_cast<char const*>(hash.data()), bcos::h256::SIZE});
}

}  // namespace

BOOST_AUTO_TEST_CASE(FirstTouchOfBrandNewAccountWithoutStorage)
{
    NodeStorage storage;
    auto const addr = makeAddress(0xA1);

    // Brand-new account: nothing in the parent flat state, the block writes all three fields.
    FlatBackendStorage flatBackend;
    auto view = makeFlatView(flatBackend);
    writeFlatRow(view, accountFieldKey(addr, ROW_NONCE), makeEntry("1"));
    writeFlatRow(view, accountFieldKey(addr, ROW_BALANCE), makeEntry("1000"));
    writeFlatRow(view, accountFieldKey(addr, ROW_CODE_HASH), codeHashEntry(makeHash(0xC0)));

    auto output =
        bcos::task::syncWait(buildAndCollect(storage, emptyRootHash(), view, /*l2Mode=*/false));

    MPTReadView<NodeStorage> readView(storage, output.stateRoot);
    auto account = bcos::task::syncWait(readView.readAccount(addr));
    BOOST_REQUIRE(account.has_value());
    BOOST_CHECK(account->storageRoot == emptyRootHash());
    BOOST_CHECK_EQUAL(account->nonce, bcos::u256(1));
    BOOST_CHECK_EQUAL(account->balance, bcos::u256(1000));
    BOOST_CHECK(account->codeHash == makeHash(0xC0));
}

BOOST_AUTO_TEST_CASE(FirstTouchDoesNotBackfillColdSlots)
{
    // THE slot-level commitment case (spec §4.2, Revision 2026-07-09): the parent flat state
    // holds a dormant contract with pre-activation slots; the first-touch block writes ONE new
    // slot. The storage trie must start from the empty root and commit exactly this block's
    // slot — never a prefix-scan back-fill of the cold ones. The account leaf's metadata still
    // comes from the flat state (O(1) named-row reads).
    NodeStorage storage;
    auto const addr = makeAddress(0xA2);

    FlatBackendStorage flatBackend;
    writeFlatRow(flatBackend, accountFieldKey(addr, ROW_NONCE), makeEntry("7"));
    writeFlatRow(flatBackend, accountFieldKey(addr, ROW_BALANCE), makeEntry("70"));
    writeFlatRow(flatBackend, accountFieldKey(addr, ROW_CODE_HASH), codeHashEntry(makeHash(0xC2)));
    for (size_t i = 0; i < 100; ++i)  // cold pre-activation slots
    {
        writeFlatRow(flatBackend, accountSlotKey(addr, slotKeyAt(i)), slotEntry({0x11, 0x22}));
    }

    auto view = makeFlatView(flatBackend);
    auto const hotSlot = slotKeyAt(500);
    writeFlatRow(view, accountSlotKey(addr, hotSlot), slotEntry(bcos::bytes{0x99}));

    auto output =
        bcos::task::syncWait(buildAndCollect(storage, emptyRootHash(), view, /*l2Mode=*/false));

    MPTReadView<NodeStorage> readView(storage, output.stateRoot);
    auto account = bcos::task::syncWait(readView.readAccount(addr));
    BOOST_REQUIRE(account.has_value());
    // Only the hot slot is committed; the 100 cold slots stay outside the trie.
    BOOST_CHECK(account->storageRoot == storageRootOracle({{hotSlot, bcos::bytes{0x99}}}));
    // Metadata came from the flat rows, one read each.
    BOOST_CHECK_EQUAL(account->nonce, bcos::u256(7));
    BOOST_CHECK_EQUAL(account->balance, bcos::u256(70));
    BOOST_CHECK(account->codeHash == makeHash(0xC2));
    // A cold slot is genuinely absent from the trie (SlotNotInMPT territory, spec §5.9).
    Trie<NodeStorage> storageTrie(storage, account->storageRoot);
    auto cold = bcos::task::syncWait(storageTrie.get(slotKeyHash(slotKeyAt(0))));
    BOOST_CHECK(!cold.has_value());
}

BOOST_AUTO_TEST_CASE(UnwrittenFieldsComeFromFlatWrittenOnesWin)
{
    NodeStorage storage;
    auto const addr = makeAddress(0xA7);

    // Flat baseline: nonce 99 / balance 1234 / codeHash 0x77... The block only writes the
    // nonce; balance and codeHash must be filled from the flat rows, the nonce must win.
    FlatBackendStorage flatBackend;
    writeFlatRow(flatBackend, accountFieldKey(addr, ROW_NONCE), makeEntry("99"));
    writeFlatRow(flatBackend, accountFieldKey(addr, ROW_BALANCE), makeEntry("1234"));
    writeFlatRow(flatBackend, accountFieldKey(addr, ROW_CODE_HASH), codeHashEntry(makeHash(0x77)));

    auto view = makeFlatView(flatBackend);
    writeFlatRow(view, accountFieldKey(addr, ROW_NONCE), makeEntry("5"));

    auto output =
        bcos::task::syncWait(buildAndCollect(storage, emptyRootHash(), view, /*l2Mode=*/false));

    MPTReadView<NodeStorage> readView(storage, output.stateRoot);
    auto account = bcos::task::syncWait(readView.readAccount(addr));
    BOOST_REQUIRE(account.has_value());
    BOOST_CHECK_EQUAL(account->nonce, bcos::u256(5));
    BOOST_CHECK_EQUAL(account->balance, bcos::u256(1234));
    BOOST_CHECK(account->codeHash == makeHash(0x77));
}

BOOST_AUTO_TEST_CASE(MissingCodeHashRowMeansEmptyCodeHash)
{
    NodeStorage storage;
    auto const addr = makeAddress(0xA8);

    // An EOA: the flat state has nonce/balance rows but no codeHash row. The leaf must carry
    // emptyCodeHash() — a zero h256 would encode a wrong (forking) leaf.
    FlatBackendStorage flatBackend;
    writeFlatRow(flatBackend, accountFieldKey(addr, ROW_NONCE), makeEntry("3"));

    auto view = makeFlatView(flatBackend);
    writeFlatRow(view, accountFieldKey(addr, ROW_BALANCE), makeEntry("10"));

    auto output =
        bcos::task::syncWait(buildAndCollect(storage, emptyRootHash(), view, /*l2Mode=*/false));

    MPTReadView<NodeStorage> readView(storage, output.stateRoot);
    auto account = bcos::task::syncWait(readView.readAccount(addr));
    BOOST_REQUIRE(account.has_value());
    BOOST_CHECK_EQUAL(account->nonce, bcos::u256(3));
    BOOST_CHECK_EQUAL(account->balance, bcos::u256(10));
    BOOST_CHECK(account->codeHash == emptyCodeHash());
}

BOOST_AUTO_TEST_CASE(ZeroCodeHashRowInFlatThrows)
{
    NodeStorage storage;
    auto const addr = makeAddress(0xA9);

    // A corrupt flat state: a codeHash row that decodes to a zero h256 violates the executor
    // contract (codeHash = keccak(code), never zero). Committing it would fork — throw instead.
    FlatBackendStorage flatBackend;
    writeFlatRow(flatBackend, accountFieldKey(addr, ROW_CODE_HASH), codeHashEntry(bcos::h256{}));

    auto view = makeFlatView(flatBackend);
    writeFlatRow(view, accountFieldKey(addr, ROW_BALANCE), makeEntry("10"));

    BOOST_CHECK_EXCEPTION(
        bcos::task::syncWait(buildAndCollect(storage, emptyRootHash(), view, /*l2Mode=*/false)),
        MPTInvariantViolation,
        [](auto const& e) { return errinfoContains(e, "decodes to a zero h256"); });
}

BOOST_AUTO_TEST_CASE(DeleteOfNeverWrittenSlotIsNoop)
{
    NodeStorage storage;
    auto const addr = makeAddress(0xAA);

    // The block zeroes a slot the (empty) trie never held, plus writes a nonce. From an empty
    // baseline the delete resolves to a no-op — the storage root stays the empty root.
    FlatBackendStorage flatBackend;
    auto view = makeFlatView(flatBackend);
    writeFlatRow(view, accountFieldKey(addr, ROW_NONCE), makeEntry("1"));
    deleteFlatRowLogically(view, accountSlotKey(addr, slotKeyAt(3)));

    auto output =
        bcos::task::syncWait(buildAndCollect(storage, emptyRootHash(), view, /*l2Mode=*/false));

    MPTReadView<NodeStorage> readView(storage, output.stateRoot);
    auto account = bcos::task::syncWait(readView.readAccount(addr));
    BOOST_REQUIRE(account.has_value());
    BOOST_CHECK(account->storageRoot == emptyRootHash());
}

BOOST_AUTO_TEST_CASE(NextBlockContinuesIncrementallyOverThePartialTrie)
{
    // First-touch commits slot A from the empty root; the next block walks subsequent-touch and
    // adds slot B incrementally. The partial ("only ever hot slots") trie grows monotonically —
    // it never converges to the account's full flat storage (spec §4.2, OQ5).
    NodeStorage storage;
    auto const addr = makeAddress(0xB1);
    auto const slotA = slotKeyAt(1);
    auto const slotB = slotKeyAt(2);

    FlatBackendStorage flatBackend;
    writeFlatRow(flatBackend, accountFieldKey(addr, ROW_NONCE), makeEntry("1"));
    for (size_t i = 10; i < 20; ++i)  // cold slots that must never appear
    {
        writeFlatRow(flatBackend, accountSlotKey(addr, slotKeyAt(i)), slotEntry({0x77}));
    }

    // Block N: first-touch with slot A.
    auto viewN = makeFlatView(flatBackend);
    writeFlatRow(viewN, accountSlotKey(addr, slotA), slotEntry(bcos::bytes{0x0A}));
    auto outputN =
        bcos::task::syncWait(buildAndCollect(storage, emptyRootHash(), viewN, /*l2Mode=*/false));

    // Block N+1: subsequent-touch with slot B on top of block N's root.
    auto viewN1 = makeFlatView(flatBackend);
    writeFlatRow(viewN1, accountSlotKey(addr, slotB), slotEntry(bcos::bytes{0x0B}));
    auto outputN1 =
        bcos::task::syncWait(buildAndCollect(storage, outputN.stateRoot, viewN1, /*l2Mode=*/false));

    MPTReadView<NodeStorage> readView(storage, outputN1.stateRoot);
    auto account = bcos::task::syncWait(readView.readAccount(addr));
    BOOST_REQUIRE(account.has_value());
    BOOST_CHECK(account->storageRoot ==
                storageRootOracle({{slotA, bcos::bytes{0x0A}}, {slotB, bcos::bytes{0x0B}}}));
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::ledger::mpt::test
