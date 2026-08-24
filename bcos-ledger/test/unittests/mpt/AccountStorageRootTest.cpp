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
 * @file AccountStorageRootTest.cpp
 * @brief accountStorageRootFromFlat: an account's storage-trie root rebuilt from the flat KV.
 *        The three-slot root is pinned against an INDEPENDENT offline reference produced by
 *        tools/opstack-genesis/gen_storage_root_fixture.py (its own RLP + trie, sharing only
 *        the keccak256 primitive), so a self-consistent-but-wrong C++ trie cannot pass.
 */
#include "TestHelpers.h"
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-ledger/mpt/AccountStorageRoot.h>
#include <bcos-ledger/mpt/Classify.h>
#include <bcos-ledger/mpt/Constants.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <boost/test/unit_test.hpp>
#include <string>
#include <string_view>

namespace bcos::ledger::mpt::test
{

BOOST_AUTO_TEST_SUITE(AccountStorageRootSuite)

namespace
{
// `asr*` name prefixes avoid an anonymous-namespace ODR clash with the other mpt test files
// under UNITY_BUILD.

/// The account whose storage trie every case below builds. Byte value is irrelevant to the
/// function — the OP L2ToL1MessagePasser identity lives in the engine, not here.
bcos::Address asrAccount()
{
    return makeAddress(0x42);
}

/// A 32-byte big-endian slot key / value for a small integer.
bcos::h256 asrWord(uint64_t value)
{
    bcos::h256 out{};
    for (size_t i = 0; i < sizeof(value); ++i)
    {
        out.data()[bcos::h256::SIZE - 1 - i] = static_cast<bcos::byte>(value >> (8 * i));
    }
    return out;
}

bcos::h256 asrAllOnes()
{
    bcos::h256 out{};
    for (size_t i = 0; i < bcos::h256::SIZE; ++i)
    {
        out.data()[i] = static_cast<bcos::byte>(0xFF);
    }
    return out;
}

/// The executor writes a slot value as its 32 RAW bytes (EVMAccount::setStorage).
bcos::storage::Entry asrSlotEntry(bcos::h256 const& value)
{
    return makeEntry(std::string_view{
        reinterpret_cast<char const*>(value.data()), static_cast<size_t>(bcos::h256::SIZE)});
}

void asrWriteSlot(auto& target, bcos::h256 const& slot, bcos::h256 const& value)
{
    writeFlatRow(target, accountSlotKey(asrAccount(), slot), asrSlotEntry(value));
}

bcos::h256 asrRootOf(FlatStateView& view)
{
    return bcos::task::syncWait(accountStorageRootFromFlat(view, asrAccount()));
}

/// gen_storage_root_fixture.py, fixture "three_slots":
///   slot 0x00..00 -> 1, slot 0x00..01 -> 0xdeadbeef, slot 0xff..ff -> 0xff
bcos::h256 const c_threeSlotsRoot{
    std::string_view{"7ce94a4a60783202e726bea0fb63adadfb79ee7903d3b30af81c4b5530b9bff5"},
    bcos::h256::FromHex};
/// gen_storage_root_fixture.py, fixture "single_slot": slot 0x00..00 -> 1.
bcos::h256 const c_singleSlotRoot{
    std::string_view{"821e2556a290c86405f8160a2d662042a431ba456b9db265c79bb837c04be5f0"},
    bcos::h256::FromHex};

/// Seed the fixture's three slots into @p target (a delta layer or a backend).
void asrSeedThreeSlots(auto& target)
{
    asrWriteSlot(target, asrWord(0), asrWord(1));
    asrWriteSlot(target, asrWord(1), asrWord(0xDEADBEEF));
    asrWriteSlot(target, asrAllOnes(), asrWord(0xFF));
}
}  // namespace

// An account with no storage row at all has the EMPTY TRIE root, not a zero h256. The
// difference matters on the wire: a zero withdrawalsRoot is not a state any Ethereum client
// would produce, so serving one is indistinguishable from a broken CL's submission.
BOOST_AUTO_TEST_CASE(no_slots_yields_empty_trie_root)
{
    FlatBackendStorage backend;
    auto view = makeFlatView(backend);
    BOOST_CHECK_EQUAL(asrRootOf(view), emptyRootHash());
    BOOST_CHECK_NE(asrRootOf(view), bcos::h256{});
}

// nonce / balance / codeHash / code rows are not storage slots and must leave the trie empty.
BOOST_AUTO_TEST_CASE(account_field_rows_are_not_slots)
{
    FlatBackendStorage backend;
    auto view = makeFlatView(backend);
    writeFlatRow(mutableStorage(view), accountFieldKey(asrAccount(), ROW_NONCE), makeEntry("7"));
    writeFlatRow(
        mutableStorage(view), accountFieldKey(asrAccount(), ROW_BALANCE), makeEntry("1000"));
    writeFlatRow(mutableStorage(view), accountFieldKey(asrAccount(), ROW_CODE), makeEntry("\x60"));
    BOOST_CHECK_EQUAL(asrRootOf(view), emptyRootHash());
}

// The load-bearing vector: three slots, root pinned against the offline python reference.
BOOST_AUTO_TEST_CASE(three_slots_match_offline_reference)
{
    FlatBackendStorage backend;
    auto view = makeFlatView(backend);
    asrSeedThreeSlots(mutableStorage(view));
    BOOST_CHECK_EQUAL(asrRootOf(view), c_threeSlotsRoot);
}

// A slot whose value is all zeros is NOT in the trie (Ethereum: zero == does not exist), so
// adding one leaves the three-slot root untouched.
BOOST_AUTO_TEST_CASE(zero_valued_slot_is_not_in_the_trie)
{
    FlatBackendStorage backend;
    auto view = makeFlatView(backend);
    asrSeedThreeSlots(mutableStorage(view));
    asrWriteSlot(mutableStorage(view), asrWord(2), asrWord(0));
    BOOST_CHECK_EQUAL(asrRootOf(view), c_threeSlotsRoot);
}

// Slots committed in the parent state are read through the view, and a row rewritten in this
// block's delta shadows the committed one — the merge-sort iterator returns the delta's value
// for a duplicated key.
BOOST_AUTO_TEST_CASE(delta_layer_shadows_the_committed_slot)
{
    FlatBackendStorage backend;
    asrWriteSlot(backend, asrWord(0), asrWord(1));
    asrWriteSlot(backend, asrWord(1), asrWord(0xDEADBEEF));
    asrWriteSlot(backend, asrAllOnes(), asrWord(0xFF));
    {
        auto committedView = makeFlatView(backend);
        BOOST_CHECK_EQUAL(asrRootOf(committedView), c_threeSlotsRoot);
    }

    // This block zeroes the two extra slots: what remains is the single-slot fixture.
    auto view = makeFlatView(backend);
    asrWriteSlot(mutableStorage(view), asrWord(1), asrWord(0));
    asrWriteSlot(mutableStorage(view), asrAllOnes(), asrWord(0));
    BOOST_CHECK_EQUAL(asrRootOf(view), c_singleSlotRoot);
}

// A logically deleted slot row must not resurrect the slot.
BOOST_AUTO_TEST_CASE(deleted_slot_row_leaves_the_trie)
{
    FlatBackendStorage backend;
    asrWriteSlot(backend, asrWord(0), asrWord(1));
    asrWriteSlot(backend, asrWord(1), asrWord(0xDEADBEEF));
    asrWriteSlot(backend, asrAllOnes(), asrWord(0xFF));

    auto view = makeFlatView(backend);
    deleteFlatRowLogically(view, accountSlotKey(asrAccount(), asrWord(1)));
    deleteFlatRowLogically(view, accountSlotKey(asrAccount(), asrAllOnes()));
    BOOST_CHECK_EQUAL(asrRootOf(view), c_singleSlotRoot);
}

// Another account's slots never enter this account's trie: the scan is a prefix range over
// one table, and a neighbouring /apps/ table ends it.
BOOST_AUTO_TEST_CASE(other_accounts_slots_are_excluded)
{
    FlatBackendStorage backend;
    auto view = makeFlatView(backend);
    asrSeedThreeSlots(mutableStorage(view));
    for (auto firstByte : {bcos::byte{0x01}, bcos::byte{0x41}, bcos::byte{0x43}, bcos::byte{0xFE}})
    {
        auto const other = makeAddress(firstByte);
        writeFlatRow(mutableStorage(view),
            bcos::executor_v1::StateKey{accountTableName(other),
                std::string_view{
                    reinterpret_cast<char const*>(asrWord(0).data()), bcos::h256::SIZE}},
            asrSlotEntry(asrWord(0x99)));
    }
    BOOST_CHECK_EQUAL(asrRootOf(view), c_threeSlotsRoot);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::ledger::mpt::test
