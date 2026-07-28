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
 * @brief Unit tests for MPTAccount: EVMAccount by default, MPT reads when given a root (§5.13)
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
#include <bcos-ledger/mpt/MPTAccount.h>
#include <bcos-ledger/mpt/MPTReadView.h>
#include <bcos-ledger/mpt/StorageValueCodec.h>
#include <bcos-task/Task.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <evmc/evmc.h>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <optional>
#include <string>
#include <string_view>

namespace bcos::ledger::mpt::test
{

using NodeStorage = bcos::storage2::memory_storage::MemoryStorage<bcos::h256, bcos::bytes>;
using FlatStorage = bcos::storage2::memory_storage::MemoryStorage<bcos::executor_v1::StateKey,
    bcos::storage::Entry, bcos::storage2::memory_storage::ORDERED>;
using TestMPTAccount = MPTAccount<FlatStorage, NodeStorage, FlatStorage>;

// MPTAccount stays a drop-in for EVMAccount: every read has a default argument, so the concept
// HostContext is coded against is satisfied by the no-argument calls.
static_assert(bcos::ledger::account::Account<TestMPTAccount>);

namespace
{

bcos::h256 fromEvmc(const evmc_bytes32& value)
{
    return bcos::ledger::account::toH256(value);
}

evmc_bytes32 toEvmc(bcos::h256 const& value)
{
    return bcos::ledger::account::toEvmcBytes32(value);
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

/// A seeded historical world: a contract with two storage slots and code committed into the
/// trie, its code row in backendStorage, plus an execStorage standing in for the live flat KV.
struct SeededState
{
    NodeStorage nodeStorage;
    FlatStorage backendStorage;  // s_code_binary, hash-addressed
    FlatStorage execStorage;     // the flat KV the inherited EVMAccount behaviour uses

    bcos::Address addr = makeAddress(0xab);
    bcos::Address eoaAddr = makeAddress(0xee);
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
        auto const storageRoot = seedTrieFlushed(nodeStorage, emptyRootHash(),
            {{slotKeyHash(slot1), encodeStorageValue(bcos::h256(val1).ref())},
                {slotKeyHash(slot2), encodeStorageValue(bcos::h256(val2).ref())}})
                                     .root;

        bcos::crypto::hasher::openssl::OpenSSL_Keccak256_Hasher hasher;
        bcos::crypto::hasher::hash(hasher, bcos::ref(code), codeHash);
        bcos::task::syncWait(bcos::storage2::writeOne(backendStorage,
            bcos::executor_v1::StateKey{
                bcos::ledger::SYS_CODE_BINARY, bcos::concepts::bytebuffer::toView(codeHash)},
            bcos::storage::Entry{code}));

        // Account trie: the contract's 4-tuple leaf, plus an EOA leaf — no code
        // (emptyCodeHash) and no storage (emptyRootHash).
        seeded.nonce = 7;
        seeded.balance = 1000;
        seeded.storageRoot = storageRoot;
        seeded.codeHash = codeHash;
        Account eoa;
        eoa.nonce = 1;
        eoa.balance = 5;
        eoa.storageRoot = emptyRootHash();
        eoa.codeHash = emptyCodeHash();
        stateRoot = seedTrieFlushed(nodeStorage, emptyRootHash(),
            {{accountKeyHash(addr), seeded.encode()}, {accountKeyHash(eoaAddr), eoa.encode()}})
                        .root;
    }

    TestMPTAccount account()
    {
        return {execStorage, nodeStorage, backendStorage, addr, /*binaryAddress*/ false};
    }
    TestMPTAccount accountAt(bcos::Address const& address)
    {
        return {execStorage, nodeStorage, backendStorage, address, /*binaryAddress*/ false};
    }
    std::string tableName() const { return "/apps/" + addr.hex(); }
    /// The historical root, in the optional form every read takes.
    std::optional<bcos::h256> at() const { return stateRoot; }
};

}  // namespace

BOOST_AUTO_TEST_SUITE(MPTAccountSuite)

// Reads given a root walk the trie: the 4-tuple and both slots match what was committed, with
// MPTReadView as the oracle.
BOOST_AUTO_TEST_CASE(HistoricalReadsMatchSeededState)
{
    SeededState state;
    auto account = state.account();

    BOOST_CHECK(bcos::task::syncWait(account.exists(state.at())));
    BOOST_CHECK_EQUAL(bcos::task::syncWait(account.balance(state.at())), 1000);
    auto const nonce = bcos::task::syncWait(account.nonce(state.at()));
    BOOST_REQUIRE(nonce.has_value());
    BOOST_CHECK_EQUAL(*nonce, "7");
    BOOST_CHECK_EQUAL(bcos::task::syncWait(account.codeHash(state.at())), state.codeHash);

    BOOST_CHECK_EQUAL(
        fromEvmc(bcos::task::syncWait(account.storage(toEvmc(state.slot1), state.at()))),
        bcos::h256(state.val1));
    BOOST_CHECK_EQUAL(
        fromEvmc(bcos::task::syncWait(account.storage(toEvmc(state.slot2), state.at()))),
        bcos::h256(state.val2));
    // A plain root must resolve to the historical overload, not to the inherited tag-forwarding
    // flat read (a bare h256 is an exact match for the tag pack — see MPTAccount::storage).
    BOOST_CHECK_EQUAL(
        fromEvmc(bcos::task::syncWait(account.storage(toEvmc(state.slot1), state.stateRoot))),
        bcos::h256(state.val1));
    // The mirror case: a literal nullopt must mean "latest", i.e. the flat read (empty here),
    // not be swallowed as a storage tag.
    BOOST_CHECK_EQUAL(
        fromEvmc(bcos::task::syncWait(account.storage(toEvmc(state.slot1), std::nullopt))),
        bcos::h256{});

    auto const code = bcos::task::syncWait(account.code(state.at()));
    BOOST_REQUIRE(code.has_value());
    auto const codeView = code->get();
    BOOST_CHECK(bcos::bytes(codeView.begin(), codeView.end()) == state.code);

    // Oracle: the same leaf through MPTReadView.
    MPTReadView view(state.nodeStorage, state.stateRoot);
    auto const oracle = bcos::task::syncWait(view.readAccount(state.addr));
    BOOST_REQUIRE(oracle.has_value());
    BOOST_CHECK_EQUAL(bcos::task::syncWait(account.balance(state.at())), oracle->balance);
    BOOST_CHECK_EQUAL(bcos::u256(*bcos::task::syncWait(account.nonce(state.at()))), oracle->nonce);
    BOOST_CHECK_EQUAL(bcos::task::syncWait(account.codeHash(state.at())), oracle->codeHash);

    // address()/path() are inherited: the real table name, identical to EVMAccount's.
    BOOST_CHECK_EQUAL(account.address(), state.tableName());
    BOOST_CHECK_EQUAL(bcos::task::syncWait(account.path()), state.tableName());
}

// Without a root MPTAccount IS EVMAccount: reads go to the flat KV and see this object's own
// writes. Read-your-writes, nonce advance and create() all behave as on the latest state — the
// historical trie is not consulted at all (it holds different values for every field here).
BOOST_AUTO_TEST_CASE(DefaultReadsAreFlatWithReadYourWrites)
{
    SeededState state;
    auto account = state.account();

    // Nothing written yet: the flat KV is empty, and the trie's values do NOT leak through.
    BOOST_CHECK(!bcos::task::syncWait(account.exists()));
    BOOST_CHECK_EQUAL(bcos::task::syncWait(account.balance()), 0);
    BOOST_CHECK(!bcos::task::syncWait(account.nonce()).has_value());
    BOOST_CHECK_EQUAL(
        fromEvmc(bcos::task::syncWait(account.storage(toEvmc(state.slot1)))), bcos::h256{});

    // create() then exists(); setBalance/setNonce/setStorage then read them back.
    bcos::task::syncWait(account.create());
    BOOST_CHECK(bcos::task::syncWait(account.exists()));

    bcos::task::syncWait(account.setBalance(bcos::u256{555}));
    BOOST_CHECK_EQUAL(bcos::task::syncWait(account.balance()), 555);

    bcos::task::syncWait(account.setNonce("42"));
    BOOST_CHECK_EQUAL(*bcos::task::syncWait(account.nonce()), "42");
    bcos::task::syncWait(account.increaseNonce());  // inherited: reads the flat nonce
    BOOST_CHECK_EQUAL(*bcos::task::syncWait(account.nonce()), "43");

    bcos::h256 const newValue{0xabcdef};
    bcos::task::syncWait(account.setStorage(toEvmc(state.slot1), toEvmc(newValue)));
    BOOST_CHECK_EQUAL(
        fromEvmc(bcos::task::syncWait(account.storage(toEvmc(state.slot1)))), newValue);
    // The tag-forwarding overload HostContext uses is still the inherited flat read.
    BOOST_CHECK_EQUAL(fromEvmc(bcos::task::syncWait(
                          account.storage(toEvmc(state.slot1), bcos::storage2::BYPASS_READ_SET))),
        newValue);

    // setCode / abi(): abi is inherited outright, so it round-trips through the flat KV.
    bcos::bytes newCode{0xfe, 0xed};
    bcos::crypto::hasher::openssl::OpenSSL_Keccak256_Hasher hasher;
    bcos::h256 newCodeHash;
    bcos::crypto::hasher::hash(hasher, bcos::ref(newCode), newCodeHash);
    bcos::task::syncWait(account.setCode(newCode, "the-abi", newCodeHash));
    BOOST_CHECK_EQUAL(bcos::task::syncWait(account.codeHash()), newCodeHash);
    auto const flatCode = bcos::task::syncWait(account.code());
    BOOST_REQUIRE(flatCode.has_value());
    auto const flatCodeView = flatCode->get();
    BOOST_CHECK(bcos::bytes(flatCodeView.begin(), flatCodeView.end()) == newCode);
    auto const flatAbi = bcos::task::syncWait(account.abi());
    BOOST_REQUIRE(flatAbi.has_value());
    BOOST_CHECK_EQUAL(flatAbi->get(), "the-abi");
}

// binaryAddress is what makes the no-root path address the right table on a feature_raw_address
// chain, which is why the constructor takes it without a default. With it set, the inherited
// reads and writes must use the raw-byte table name, not the hex one.
BOOST_AUTO_TEST_CASE(BinaryAddressSelectsTheRawTable)
{
    SeededState state;
    TestMPTAccount account{
        state.execStorage, state.nodeStorage, state.backendStorage, state.addr, true};

    std::string expected{bcos::ledger::SYS_DIRECTORY::USER_APPS};
    expected.append(reinterpret_cast<const char*>(state.addr.data()), state.addr.size());
    BOOST_CHECK_EQUAL(account.address(), expected);
    BOOST_CHECK_EQUAL(bcos::task::syncWait(account.path()), expected);
    BOOST_CHECK_NE(account.address(), state.tableName());

    // The inherited write lands under that name, and the inherited read finds it there.
    bcos::task::syncWait(account.setBalance(bcos::u256{99}));
    BOOST_CHECK_EQUAL(bcos::task::syncWait(account.balance()), 99);
    auto const row = bcos::task::syncWait(bcos::storage2::readOne(state.execStorage,
        bcos::executor_v1::StateKeyView{expected, bcos::ledger::ACCOUNT_TABLE_FIELDS::BALANCE}));
    BOOST_REQUIRE(row.has_value());

    // The historical path is address-keyed, not table-keyed, so it is unaffected.
    BOOST_CHECK_EQUAL(bcos::task::syncWait(account.balance(state.at())), 1000);
}

// The two paths do not contaminate each other: after the flat writes above, reads given a root
// still return the block's committed values, and the node storage never gains a key.
BOOST_AUTO_TEST_CASE(HistoricalReadsIgnoreFlatWrites)
{
    SeededState state;
    size_t const nodeKeysBefore = storageKeyCount(state.nodeStorage);
    BOOST_REQUIRE_GT(nodeKeysBefore, 0);

    auto account = state.account();
    bcos::task::syncWait(account.setBalance(bcos::u256{555}));
    bcos::task::syncWait(account.setNonce("42"));
    bcos::task::syncWait(account.setStorage(toEvmc(state.slot1), toEvmc(bcos::h256{0xabcdef})));

    BOOST_CHECK_EQUAL(bcos::task::syncWait(account.balance(state.at())), 1000);
    BOOST_CHECK_EQUAL(*bcos::task::syncWait(account.nonce(state.at())), "7");
    BOOST_CHECK_EQUAL(
        fromEvmc(bcos::task::syncWait(account.storage(toEvmc(state.slot1), state.at()))),
        bcos::h256(state.val1));
    BOOST_CHECK_EQUAL(bcos::task::syncWait(account.codeHash(state.at())), state.codeHash);

    // The flat writes really did land — the historical path simply does not look at them.
    BOOST_CHECK_EQUAL(bcos::task::syncWait(account.balance()), 555);
    BOOST_CHECK_EQUAL(storageKeyCount(state.nodeStorage), nodeKeysBefore);
}

// Absence at a root is Ethereum semantics, not an error.
BOOST_AUTO_TEST_CASE(AbsentAccountAndSlotReadZero)
{
    SeededState state;

    auto absent = state.accountAt(makeAddress(0xcd));
    BOOST_CHECK(!bcos::task::syncWait(absent.exists(state.at())));
    BOOST_CHECK_EQUAL(bcos::task::syncWait(absent.balance(state.at())), 0);
    BOOST_CHECK(!bcos::task::syncWait(absent.nonce(state.at())).has_value());
    BOOST_CHECK_EQUAL(bcos::task::syncWait(absent.codeHash(state.at())), bcos::h256{});
    BOOST_CHECK(!bcos::task::syncWait(absent.code(state.at())).has_value());
    BOOST_CHECK_EQUAL(
        fromEvmc(bcos::task::syncWait(absent.storage(toEvmc(state.slot1), state.at()))),
        bcos::h256{});

    // An existing account's absent slot also reads zero; its storageEntry is nullopt.
    auto account = state.account();
    BOOST_CHECK_EQUAL(
        fromEvmc(bcos::task::syncWait(account.storage(toEvmc(state.slotAbsent), state.at()))),
        bcos::h256{});
    BOOST_CHECK(!bcos::task::syncWait(
        account.storageEntry(bcos::concepts::bytebuffer::toView(state.slotAbsent), state.at()))
            .has_value());

    // An empty root has no accounts at all.
    BOOST_CHECK(!bcos::task::syncWait(account.exists(emptyRootHash())));
    BOOST_CHECK_EQUAL(bcos::task::syncWait(account.balance(emptyRootHash())), 0);
}

// An EOA leaf (emptyCodeHash, emptyRootHash storage) exists with its 4-tuple values but has no
// code, and every slot reads zero through the empty-storage-trie short circuit.
BOOST_AUTO_TEST_CASE(EOALeafHasNoCodeAndEmptyStorage)
{
    SeededState state;
    auto eoa = state.accountAt(state.eoaAddr);

    BOOST_CHECK(bcos::task::syncWait(eoa.exists(state.at())));
    BOOST_CHECK_EQUAL(bcos::task::syncWait(eoa.balance(state.at())), 5);
    BOOST_CHECK_EQUAL(*bcos::task::syncWait(eoa.nonce(state.at())), "1");
    BOOST_CHECK_EQUAL(bcos::task::syncWait(eoa.codeHash(state.at())), emptyCodeHash());
    BOOST_CHECK(!bcos::task::syncWait(eoa.code(state.at())).has_value());
    BOOST_CHECK_EQUAL(
        fromEvmc(bcos::task::syncWait(eoa.storage(toEvmc(state.slot1), state.at()))), bcos::h256{});
}

// A leaf whose codeHash has no s_code_binary row falls back to the base resolution, exactly as
// EVMAccount does — that is where contracts deployed by old versions keep their code. An
// account absent from the trie does NOT fall back: it did not exist at that block.
BOOST_AUTO_TEST_CASE(MissingCodeRowFallsBackToAccountTable)
{
    SeededState state;
    Account legacy;
    legacy.nonce = 1;
    legacy.balance = 1;
    legacy.storageRoot = emptyRootHash();
    legacy.codeHash = makeHash(0x77);  // committed, but no s_code_binary row exists for it
    auto const legacyAddr = makeAddress(0xba);
    auto const root = seedTrieFlushed(
        state.nodeStorage, state.stateRoot, {{accountKeyHash(legacyAddr), legacy.encode()}})
                          .root;

    bcos::bytes const legacyCode{0x01, 0x02, 0x03};
    bcos::task::syncWait(bcos::storage2::writeOne(state.execStorage,
        bcos::executor_v1::StateKey{
            "/apps/" + legacyAddr.hex(), bcos::ledger::ACCOUNT_TABLE_FIELDS::CODE},
        bcos::storage::Entry{legacyCode}));

    // Today's flat state has this address on a DIFFERENT, hash-addressed code. The fallback must
    // not go through the base's full resolution, which starts from this flat codeHash and would
    // return bytes hashing to something other than the codeHash the leaf committed.
    bcos::bytes const todaysCode{0xaa, 0xbb};
    bcos::crypto::hasher::openssl::OpenSSL_Keccak256_Hasher hasher;
    bcos::h256 todaysCodeHash;
    bcos::crypto::hasher::hash(hasher, bcos::ref(todaysCode), todaysCodeHash);
    bcos::task::syncWait(bcos::storage2::writeOne(state.execStorage,
        bcos::executor_v1::StateKey{
            "/apps/" + legacyAddr.hex(), bcos::ledger::ACCOUNT_TABLE_FIELDS::CODE_HASH},
        bcos::storage::Entry{bcos::concepts::bytebuffer::toView(todaysCodeHash)}));
    bcos::task::syncWait(bcos::storage2::writeOne(state.execStorage,
        bcos::executor_v1::StateKey{
            bcos::ledger::SYS_CODE_BINARY, bcos::concepts::bytebuffer::toView(todaysCodeHash)},
        bcos::storage::Entry{todaysCode}));

    auto account = state.accountAt(legacyAddr);
    BOOST_CHECK_EQUAL(bcos::task::syncWait(account.codeHash(root)), makeHash(0x77));
    auto const code = bcos::task::syncWait(account.code(root));
    BOOST_REQUIRE(code.has_value());
    auto const view = code->get();
    BOOST_CHECK(bcos::bytes(view.begin(), view.end()) == legacyCode);
    BOOST_CHECK(bcos::bytes(view.begin(), view.end()) != todaysCode);

    // Absent from the trie: no code and no fallback, even though the flat row is right there.
    auto absent = state.accountAt(makeAddress(0xcd));
    bcos::task::syncWait(bcos::storage2::writeOne(state.execStorage,
        bcos::executor_v1::StateKey{
            "/apps/" + makeAddress(0xcd).hex(), bcos::ledger::ACCOUNT_TABLE_FIELDS::CODE},
        bcos::storage::Entry{legacyCode}));
    BOOST_CHECK(!bcos::task::syncWait(absent.code(root)).has_value());
}

// A committed non-empty codeHash asserts the code existed; neither store holding it is corruption,
// not absence. Reporting nullopt would run the contract as an EOA and answer calls to it with
// success and empty output, which the caller cannot tell from a correct answer.
BOOST_AUTO_TEST_CASE(CodeMissingFromBothStoresThrows)
{
    SeededState state;
    Account corrupt;
    corrupt.nonce = 1;
    corrupt.balance = 1;
    corrupt.storageRoot = emptyRootHash();
    corrupt.codeHash = makeHash(0x99);  // no s_code_binary row, and no account-table CODE row
    auto const corruptAddr = makeAddress(0xde);
    auto const root = seedTrieFlushed(
        state.nodeStorage, state.stateRoot, {{accountKeyHash(corruptAddr), corrupt.encode()}})
                          .root;

    auto account = state.accountAt(corruptAddr);
    BOOST_CHECK_EQUAL(bcos::task::syncWait(account.codeHash(root)), makeHash(0x99));
    BOOST_CHECK_THROW(
        bcos::task::syncWait(account.code(root)), bcos::ledger::mpt::MPTInvariantViolation);
}

// storageEntry: the 32-byte slot value at a root, or the base's any-field-name read without one.
BOOST_AUTO_TEST_CASE(StorageEntryHistoricalAndFlat)
{
    SeededState state;
    auto account = state.account();

    auto entry = bcos::task::syncWait(
        account.storageEntry(bcos::concepts::bytebuffer::toView(state.slot1), state.at()));
    BOOST_REQUIRE(entry.has_value());
    auto entryView = entry->get();
    BOOST_REQUIRE_EQUAL(entryView.size(), bcos::h256::SIZE);
    BOOST_CHECK_EQUAL(bcos::h256(bcos::bytesConstRef(
                          reinterpret_cast<const bcos::byte*>(entryView.data()), entryView.size())),
        bcos::h256(state.val1));

    // Only 32-byte keys can exist in a storage trie: anything else is absent by construction.
    BOOST_CHECK(!bcos::task::syncWait(account.storageEntry("short-key", state.at())).has_value());

    // A full-width slot value survives the trie's trimmed encoding and the u256 → h256 widening
    // (the seeded slots are small enough to leave the top bytes zero).
    bcos::u256 const wide{"0xff02030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20"};
    auto const wideRoot = seedTrieFlushed(state.nodeStorage, state.seeded.storageRoot,
        {{slotKeyHash(state.slotAbsent), encodeStorageValue(bcos::h256(wide).ref())}})
                              .root;
    Account widened = state.seeded;
    widened.storageRoot = wideRoot;
    auto const rootWithWideSlot = seedTrieFlushed(state.nodeStorage, state.stateRoot,
        {{accountKeyHash(state.addr),
            widened.encode()}}).root;
    BOOST_CHECK_EQUAL(
        fromEvmc(bcos::task::syncWait(account.storage(toEvmc(state.slotAbsent), rootWithWideSlot))),
        bcos::h256(wide));

    // Without a root the base reads any account-table field name, short keys included.
    bcos::task::syncWait(account.setBalance(bcos::u256{7}));
    auto const flatField =
        bcos::task::syncWait(account.storageEntry(bcos::ledger::ACCOUNT_TABLE_FIELDS::BALANCE));
    BOOST_REQUIRE(flatField.has_value());
    BOOST_CHECK_EQUAL(flatField->get(), "7");
}

namespace
{

/// PR-43's historical storage stack, shrunk to what MPTAccount's EVMAccount-shaped constructor
/// needs: a Storage that also hands out the MPT node and backend handles.
struct StubHistoricalStorage : FlatStorage
{
    NodeStorage* nodes{};
    FlatStorage* backend{};

    NodeStorage& mptNodeStorage() { return *nodes; }
    FlatStorage& mptBackendStorage() { return *backend; }
};
static_assert(HistoricalStorageContext<StubHistoricalStorage>);
static_assert(!HistoricalStorageContext<FlatStorage>);

}  // namespace

// The (storage, address, binaryAddress) constructor — HostContext's fixed construction shape —
// pulls the trie handles off the storage type and reads the same state both ways.
BOOST_AUTO_TEST_CASE(HostContextConstructionShape)
{
    SeededState state;
    StubHistoricalStorage stub;
    stub.nodes = &state.nodeStorage;
    stub.backend = &state.backendStorage;

    evmc_address evmcAddr{};
    std::copy(state.addr.begin(), state.addr.end(), std::begin(evmcAddr.bytes));

    MPTAccount<StubHistoricalStorage, NodeStorage, FlatStorage> account{
        stub, evmcAddr, /*binaryAddress*/ false};
    BOOST_CHECK_EQUAL(bcos::task::syncWait(account.balance(state.stateRoot)), 1000);
    BOOST_CHECK_EQUAL(*bcos::task::syncWait(account.nonce(state.stateRoot)), "7");
    BOOST_CHECK_EQUAL(
        fromEvmc(bcos::task::syncWait(account.storage(toEvmc(state.slot1), state.stateRoot))),
        bcos::h256(state.val1));
    BOOST_CHECK_EQUAL(account.address(), state.tableName());

    // Writes and default reads use the stub itself, unchanged EVMAccount behaviour.
    bcos::task::syncWait(account.setBalance(bcos::u256{1}));
    BOOST_CHECK_EQUAL(bcos::task::syncWait(account.balance()), 1);
    BOOST_CHECK_EQUAL(bcos::task::syncWait(account.balance(state.stateRoot)), 1000);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::ledger::mpt::test
