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
 * @file MPTAccountTest.cpp
 * @brief Unit tests for MPTAccount, the Account-concept reader over MPT state (spec §5.13)
 */

#include "TestHelpers.h"
#include <bcos-crypto/hasher/AnyHasher.h>
#include <bcos-crypto/hasher/OpenSSLHasher.h>
#include <bcos-framework/ledger/Account.h>
#include <bcos-framework/ledger/EVMAccount.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-ledger/mpt/Account.h>
#include <bcos-ledger/mpt/Constants.h>
#include <bcos-ledger/mpt/HashBuilder.h>
#include <bcos-ledger/mpt/MPTAccount.h>
#include <bcos-ledger/mpt/MPTReadView.h>
#include <bcos-ledger/mpt/StorageValueCodec.h>
#include <bcos-task/Task.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <evmc/evmc.h>
#include <boost/test/unit_test.hpp>
#include <optional>
#include <string>
#include <string_view>

namespace bcos::ledger::mpt::test
{

using NodeStorage = bcos::storage2::memory_storage::MemoryStorage<bcos::h256, bcos::bytes>;
using CodeStorage = bcos::storage2::memory_storage::MemoryStorage<bcos::executor_v1::StateKey,
    bcos::storage::Entry, bcos::storage2::memory_storage::ORDERED>;
using TestMPTAccount = MPTAccount<NodeStorage, CodeStorage>;

// The whole point of MPTAccount: it satisfies the Account concept HostContext is coded against
// (mirrors TestTransactionExecutive.cpp's static_assert for EVMAccount).
static_assert(bcos::ledger::account::Account<TestMPTAccount>);

namespace
{

bcos::h256 fromEvmc(const evmc_bytes32& value)
{
    return bcos::h256{bcos::bytesConstRef{value.bytes, sizeof(value.bytes)}};
}

evmc_bytes32 toEvmc(bcos::h256 const& value)
{
    evmc_bytes32 out{};
    std::copy(value.begin(), value.end(), out.bytes);
    return out;
}

/// Number of keys currently in @p storage (full bucket scan).
template <class Storage>
size_t storageKeyCount(Storage& storage)
{
    return bcos::task::syncWait([](Storage& storage) -> bcos::task::Task<size_t> {
        auto range = co_await bcos::storage2::range(storage);
        size_t count = 0;
        while (co_await range.next())
        {
            ++count;
        }
        co_return count;
    }(storage));
}

/// A seeded historical world: one account with two storage slots and code, committed into
/// nodeStorage; the code bytes in codeStorage under s_code_binary[codeHash].
struct SeededState
{
    NodeStorage nodeStorage;
    CodeStorage codeStorage;

    bcos::Address addr = makeAddress(0xab);
    bcos::h256 slot1 = makeHash(0x01);
    bcos::h256 slot2 = makeHash(0x02);
    bcos::h256 slotAbsent = makeHash(0x7f);
    bcos::u256 val1{0x1234};
    bcos::u256 val2{0xdeadbeef};
    bcos::bytes code{0x60, 0x80, 0x60, 0x40, 0x52};
    bcos::h256 codeHash;
    Account seeded;  // the committed 4-tuple
    bcos::h256 stateRoot;

    SeededState()
    {
        // Storage trie: two slots, values RLP-trimmed exactly as MPTBuilder writes them.
        HashBuilder storageTrie(nodeStorage, emptyRootHash());
        bcos::task::syncWait(
            storageTrie.put(slotKeyHash(slot1), encodeStorageValue(bcos::h256(val1).ref())));
        bcos::task::syncWait(
            storageTrie.put(slotKeyHash(slot2), encodeStorageValue(bcos::h256(val2).ref())));
        auto const storageRoot = bcos::task::syncWait(storageTrie.commit());

        // Code: hash-addressed row in s_code_binary, keyed by the raw 32 hash bytes.
        bcos::crypto::hasher::openssl::OpenSSL_Keccak256_Hasher hasher;
        bcos::crypto::hasher::hash(hasher, bcos::ref(code), codeHash);
        bcos::task::syncWait(bcos::storage2::writeOne(codeStorage,
            bcos::executor_v1::StateKey{
                bcos::ledger::SYS_CODE_BINARY, bcos::concepts::bytebuffer::toView(codeHash)},
            bcos::storage::Entry{code}));

        // Account trie: the 4-tuple leaf at accountKeyHash(addr).
        seeded.nonce = 7;
        seeded.balance = 1000;
        seeded.storageRoot = storageRoot;
        seeded.codeHash = codeHash;
        HashBuilder accountTrie(nodeStorage, emptyRootHash());
        bcos::task::syncWait(accountTrie.put(accountKeyHash(addr), seeded.encode()));
        stateRoot = bcos::task::syncWait(accountTrie.commit());
    }

    TestMPTAccount account() { return {nodeStorage, codeStorage, stateRoot, addr}; }
    TestMPTAccount accountAt(bcos::Address const& address)
    {
        return {nodeStorage, codeStorage, stateRoot, address};
    }
};

}  // namespace

BOOST_AUTO_TEST_SUITE(MPTAccountSuite)

// The 4-tuple reads (balance/nonce/codeHash) and storage(key) match the seeded values; the
// MPTReadView 4-tuple is the oracle.
BOOST_AUTO_TEST_CASE(ReadsMatchSeededState)
{
    SeededState state;
    auto account = state.account();

    BOOST_CHECK(bcos::task::syncWait(account.exists()));
    BOOST_CHECK_EQUAL(bcos::task::syncWait(account.balance()), 1000);
    auto const nonce = bcos::task::syncWait(account.nonce());
    BOOST_REQUIRE(nonce.has_value());
    BOOST_CHECK_EQUAL(*nonce, "7");
    BOOST_CHECK_EQUAL(bcos::task::syncWait(account.codeHash()), state.codeHash);

    BOOST_CHECK_EQUAL(fromEvmc(bcos::task::syncWait(account.storage(toEvmc(state.slot1)))),
        bcos::h256(state.val1));
    BOOST_CHECK_EQUAL(fromEvmc(bcos::task::syncWait(account.storage(toEvmc(state.slot2)))),
        bcos::h256(state.val2));

    // Oracle: the same leaf through MPTReadView.
    MPTReadView view(state.nodeStorage, state.stateRoot);
    auto const oracle = bcos::task::syncWait(view.readAccount(state.addr));
    BOOST_REQUIRE(oracle.has_value());
    BOOST_CHECK_EQUAL(bcos::task::syncWait(account.balance()), oracle->balance);
    BOOST_CHECK_EQUAL(bcos::u256(*bcos::task::syncWait(account.nonce())), oracle->nonce);
    BOOST_CHECK_EQUAL(bcos::task::syncWait(account.codeHash()), oracle->codeHash);

    // address()/path() are the lowercase-hex address.
    BOOST_CHECK_EQUAL(account.address(), state.addr.hex());
    BOOST_CHECK_EQUAL(bcos::task::syncWait(account.path()), state.addr.hex());
    BOOST_CHECK_EQUAL(account.root(), state.stateRoot);
}

// code() resolves the leaf's codeHash through the s_code_binary handle and returns the exact
// deployed bytes; abi() is empty (the Ethereum 4-tuple has no abi field).
BOOST_AUTO_TEST_CASE(CodeRoundTrip)
{
    SeededState state;
    auto account = state.account();

    auto const code = bcos::task::syncWait(account.code());
    BOOST_REQUIRE(code.has_value());
    auto const view = code->get();
    bcos::bytes const got{view.begin(), view.end()};
    BOOST_CHECK(got == state.code);

    BOOST_CHECK(!bcos::task::syncWait(account.abi()).has_value());
}

// Absence is Ethereum semantics, not an error: an account missing from the trie reads as
// exists false / balance 0 / nonce unset / zero codeHash / no code; a missing slot reads zero.
BOOST_AUTO_TEST_CASE(AbsentAccountAndSlotReadZero)
{
    SeededState state;

    auto absent = state.accountAt(makeAddress(0xcd));
    BOOST_CHECK(!bcos::task::syncWait(absent.exists()));
    BOOST_CHECK_EQUAL(bcos::task::syncWait(absent.balance()), 0);
    BOOST_CHECK(!bcos::task::syncWait(absent.nonce()).has_value());
    BOOST_CHECK_EQUAL(bcos::task::syncWait(absent.codeHash()), bcos::h256{});
    BOOST_CHECK(!bcos::task::syncWait(absent.code()).has_value());
    BOOST_CHECK_EQUAL(
        fromEvmc(bcos::task::syncWait(absent.storage(toEvmc(state.slot1)))), bcos::h256{});

    // An existing account's absent slot also reads zero; its storageEntry is nullopt.
    auto account = state.account();
    BOOST_CHECK_EQUAL(
        fromEvmc(bcos::task::syncWait(account.storage(toEvmc(state.slotAbsent)))), bcos::h256{});
    BOOST_CHECK(!bcos::task::syncWait(
        account.storageEntry(bcos::concepts::bytebuffer::toView(state.slotAbsent)))
            .has_value());

    // increaseNonce on an account with no nonce throws, like EVMAccount.
    BOOST_CHECK_THROW(
        bcos::task::syncWait(absent.increaseNonce()), bcos::ledger::account::NonceNotInitialized);

    // An empty root has no accounts at all.
    auto emptyRootAccount =
        TestMPTAccount{state.nodeStorage, state.codeStorage, emptyRootHash(), state.addr};
    BOOST_CHECK(!bcos::task::syncWait(emptyRootAccount.exists()));
    BOOST_CHECK_EQUAL(bcos::task::syncWait(emptyRootAccount.balance()), 0);
}

// Writes are simulation-only: they land in the overlay (subsequent reads see them) and neither
// nodeStorage nor codeStorage gains, loses, or changes a single key.
BOOST_AUTO_TEST_CASE(WritesLandInOverlayOnly)
{
    SeededState state;
    size_t const nodeKeysBefore = storageKeyCount(state.nodeStorage);
    size_t const codeKeysBefore = storageKeyCount(state.codeStorage);
    BOOST_REQUIRE_GT(nodeKeysBefore, 0);

    auto account = state.account();

    bcos::task::syncWait(account.setBalance(bcos::u256{555}));
    BOOST_CHECK_EQUAL(bcos::task::syncWait(account.balance()), 555);

    bcos::task::syncWait(account.setNonce("42"));
    BOOST_CHECK_EQUAL(*bcos::task::syncWait(account.nonce()), "42");
    bcos::task::syncWait(account.increaseNonce());
    BOOST_CHECK_EQUAL(*bcos::task::syncWait(account.nonce()), "43");

    bcos::h256 const newValue{0xabcdef};
    bcos::task::syncWait(account.setStorage(toEvmc(state.slot1), toEvmc(newValue)));
    BOOST_CHECK_EQUAL(
        fromEvmc(bcos::task::syncWait(account.storage(toEvmc(state.slot1)))), newValue);

    bcos::bytes newCode{0xfe, 0xed};
    bcos::crypto::hasher::openssl::OpenSSL_Keccak256_Hasher hasher;
    bcos::h256 newCodeHash;
    bcos::crypto::hasher::hash(hasher, bcos::ref(newCode), newCodeHash);
    bcos::task::syncWait(account.setCode(newCode, "the-abi", newCodeHash));
    BOOST_CHECK_EQUAL(bcos::task::syncWait(account.codeHash()), newCodeHash);
    auto const overlayCode = bcos::task::syncWait(account.code());
    BOOST_REQUIRE(overlayCode.has_value());
    auto const overlayView = overlayCode->get();
    BOOST_CHECK(bcos::bytes(overlayView.begin(), overlayView.end()) == newCode);
    auto const overlayAbi = bcos::task::syncWait(account.abi());
    BOOST_REQUIRE(overlayAbi.has_value());
    BOOST_CHECK_EQUAL(overlayAbi->get(), "the-abi");

    // create() on an absent account flips exists() without touching storage.
    auto created = state.accountAt(makeAddress(0xcd));
    bcos::task::syncWait(created.create());
    BOOST_CHECK(bcos::task::syncWait(created.exists()));

    // Storage untouched: same key counts, and the simulated code row never reached codeStorage.
    BOOST_CHECK_EQUAL(storageKeyCount(state.nodeStorage), nodeKeysBefore);
    BOOST_CHECK_EQUAL(storageKeyCount(state.codeStorage), codeKeysBefore);
    BOOST_CHECK(
        !bcos::task::syncWait(bcos::storage2::readOne(state.codeStorage,
                                  bcos::executor_v1::StateKeyView{bcos::ledger::SYS_CODE_BINARY,
                                      bcos::concepts::bytebuffer::toView(newCodeHash)}))
            .has_value());

    // A fresh MPTAccount over the same storages still reads the ORIGINAL committed state.
    auto pristine = state.account();
    BOOST_CHECK_EQUAL(bcos::task::syncWait(pristine.balance()), 1000);
    BOOST_CHECK_EQUAL(*bcos::task::syncWait(pristine.nonce()), "7");
    BOOST_CHECK_EQUAL(bcos::task::syncWait(pristine.codeHash()), state.codeHash);
    BOOST_CHECK_EQUAL(fromEvmc(bcos::task::syncWait(pristine.storage(toEvmc(state.slot1)))),
        bcos::h256(state.val1));
}

// The DIRECT-tagged read and storageEntry stay consistent with storage(key), both against the
// trie and against the overlay.
BOOST_AUTO_TEST_CASE(DirectAndStorageEntryConsistent)
{
    SeededState state;
    auto account = state.account();

    // Trie-backed: all three read paths agree on the committed value.
    BOOST_CHECK_EQUAL(fromEvmc(bcos::task::syncWait(
                          account.storage(toEvmc(state.slot1), bcos::storage2::DIRECT))),
        bcos::h256(state.val1));
    auto entry =
        bcos::task::syncWait(account.storageEntry(bcos::concepts::bytebuffer::toView(state.slot1)));
    BOOST_REQUIRE(entry.has_value());
    auto entryView = entry->get();
    BOOST_REQUIRE_EQUAL(entryView.size(), bcos::h256::SIZE);
    BOOST_CHECK_EQUAL(bcos::h256(bcos::bytesConstRef(
                          reinterpret_cast<const bcos::byte*>(entryView.data()), entryView.size())),
        bcos::h256(state.val1));

    // Overlay-backed: after setStorage, all three see the overlay value.
    bcos::h256 const newValue{0x99};
    bcos::task::syncWait(account.setStorage(toEvmc(state.slot1), toEvmc(newValue)));
    BOOST_CHECK_EQUAL(
        fromEvmc(bcos::task::syncWait(account.storage(toEvmc(state.slot1)))), newValue);
    BOOST_CHECK_EQUAL(fromEvmc(bcos::task::syncWait(
                          account.storage(toEvmc(state.slot1), bcos::storage2::DIRECT))),
        newValue);
    auto overlayEntry =
        bcos::task::syncWait(account.storageEntry(bcos::concepts::bytebuffer::toView(state.slot1)));
    BOOST_REQUIRE(overlayEntry.has_value());
    auto overlayEntryView = overlayEntry->get();
    BOOST_REQUIRE_EQUAL(overlayEntryView.size(), bcos::h256::SIZE);
    BOOST_CHECK_EQUAL(
        bcos::h256(bcos::bytesConstRef(
            reinterpret_cast<const bcos::byte*>(overlayEntryView.data()), overlayEntryView.size())),
        newValue);

    // A non-32-byte key cannot exist in a storage trie: absent by construction.
    BOOST_CHECK(!bcos::task::syncWait(account.storageEntry("short-key")).has_value());
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::ledger::mpt::test
