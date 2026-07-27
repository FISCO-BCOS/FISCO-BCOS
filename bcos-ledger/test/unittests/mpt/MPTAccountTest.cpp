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
 * @brief Unit tests for MPTAccount, the EVMAccount subclass reading MPT state (spec §5.13)
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
using FlatStorage = bcos::storage2::memory_storage::MemoryStorage<bcos::executor_v1::StateKey,
    bcos::storage::Entry, bcos::storage2::memory_storage::ORDERED>;
using TestMPTAccount = MPTAccount<FlatStorage, NodeStorage, FlatStorage>;

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

/// The flat row an inherited EVMAccount write left in @p storage, as a string; nullopt if none.
std::optional<std::string> flatRow(
    FlatStorage& storage, std::string_view table, std::string_view row)
{
    auto entry = bcos::task::syncWait(
        bcos::storage2::readOne(storage, bcos::executor_v1::StateKeyView{table, row}));
    if (!entry)
    {
        return std::nullopt;
    }
    return std::string{entry->get()};
}

/// A seeded historical world: one account with two storage slots, code, and abi committed into
/// nodeStorage / backendStorage; plus an empty execStorage where inherited writes land.
struct SeededState
{
    NodeStorage nodeStorage;
    FlatStorage backendStorage;  // s_code_binary + s_contract_abi, hash-addressed
    FlatStorage execStorage;     // write target of the inherited EVMAccount methods

    bcos::Address addr = makeAddress(0xab);
    bcos::Address eoaAddr = makeAddress(0xee);
    bcos::h256 slot1 = makeHash(0x01);
    bcos::h256 slot2 = makeHash(0x02);
    bcos::h256 slotAbsent = makeHash(0x7f);
    bcos::u256 val1{0x1234};
    bcos::u256 val2{0xdeadbeef};
    bcos::bytes code{0x60, 0x80, 0x60, 0x40, 0x52};
    std::string abi{R"([{"type":"function","name":"f"}])"};
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

        // Code + abi: hash-addressed rows keyed by the raw 32 hash bytes.
        bcos::crypto::hasher::openssl::OpenSSL_Keccak256_Hasher hasher;
        bcos::crypto::hasher::hash(hasher, bcos::ref(code), codeHash);
        bcos::task::syncWait(bcos::storage2::writeOne(backendStorage,
            bcos::executor_v1::StateKey{
                bcos::ledger::SYS_CODE_BINARY, bcos::concepts::bytebuffer::toView(codeHash)},
            bcos::storage::Entry{code}));
        bcos::task::syncWait(bcos::storage2::writeOne(backendStorage,
            bcos::executor_v1::StateKey{
                bcos::ledger::SYS_CONTRACT_ABI, bcos::concepts::bytebuffer::toView(codeHash)},
            bcos::storage::Entry{abi}));

        // Account trie: the contract's 4-tuple leaf at accountKeyHash(addr), plus a second
        // EOA leaf — no code (emptyCodeHash) and no storage (emptyRootHash).
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

    TestMPTAccount account() { return {execStorage, nodeStorage, backendStorage, stateRoot, addr}; }
    TestMPTAccount accountAt(bcos::Address const& address)
    {
        return {execStorage, nodeStorage, backendStorage, stateRoot, address};
    }
    std::string tableName() const { return "/apps/" + addr.hex(); }
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

    // address()/path() are INHERITED from EVMAccount: the "/apps/<hex>" table name, so
    // historical and latest accounts key transient storage and logs identically.
    BOOST_CHECK_EQUAL(account.address(), state.tableName());
    BOOST_CHECK_EQUAL(bcos::task::syncWait(account.path()), state.tableName());
    BOOST_CHECK_EQUAL(account.root(), state.stateRoot);
}

// code()/abi() resolve the leaf's codeHash through the hash-addressed backend tables and return
// the exact deployed bytes.
BOOST_AUTO_TEST_CASE(CodeAndAbiRoundTrip)
{
    SeededState state;
    auto account = state.account();

    auto const code = bcos::task::syncWait(account.code());
    BOOST_REQUIRE(code.has_value());
    auto const view = code->get();
    bcos::bytes const got{view.begin(), view.end()};
    BOOST_CHECK(got == state.code);

    auto const abi = bcos::task::syncWait(account.abi());
    BOOST_REQUIRE(abi.has_value());
    BOOST_CHECK_EQUAL(abi->get(), state.abi);
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
    BOOST_CHECK(!bcos::task::syncWait(absent.abi()).has_value());
    BOOST_CHECK_EQUAL(
        fromEvmc(bcos::task::syncWait(absent.storage(toEvmc(state.slot1)))), bcos::h256{});

    // An existing account's absent slot also reads zero; its storageEntry is nullopt.
    auto account = state.account();
    BOOST_CHECK_EQUAL(
        fromEvmc(bcos::task::syncWait(account.storage(toEvmc(state.slotAbsent)))), bcos::h256{});
    BOOST_CHECK(!bcos::task::syncWait(
        account.storageEntry(bcos::concepts::bytebuffer::toView(state.slotAbsent)))
            .has_value());

    // increaseNonce on an absent account throws, like EVMAccount.
    BOOST_CHECK_THROW(
        bcos::task::syncWait(absent.increaseNonce()), bcos::ledger::account::NonceNotInitialized);

    // An empty root has no accounts at all.
    auto emptyRootAccount = TestMPTAccount{
        state.execStorage, state.nodeStorage, state.backendStorage, emptyRootHash(), state.addr};
    BOOST_CHECK(!bcos::task::syncWait(emptyRootAccount.exists()));
    BOOST_CHECK_EQUAL(bcos::task::syncWait(emptyRootAccount.balance()), 0);
}

// Writes are the INHERITED EVMAccount methods: they land as flat rows in execStorage (in
// EVMAccount's own representation) and reads keep returning the historical trie values — a
// write in a historical call is never read back (spec §5.13 boundary). The trie/node storage
// and the backend tables never gain, lose, or change a key.
BOOST_AUTO_TEST_CASE(WritesAreSimulationOnlyAndNeverReadBack)
{
    SeededState state;
    size_t const nodeKeysBefore = storageKeyCount(state.nodeStorage);
    size_t const backendKeysBefore = storageKeyCount(state.backendStorage);
    BOOST_REQUIRE_GT(nodeKeysBefore, 0);
    BOOST_REQUIRE_EQUAL(storageKeyCount(state.execStorage), 0);

    auto account = state.account();
    auto const table = state.tableName();

    // setBalance: the flat row is EVMAccount's decimal-string representation; balance() still
    // reads the trie.
    bcos::task::syncWait(account.setBalance(bcos::u256{555}));
    BOOST_CHECK_EQUAL(bcos::task::syncWait(account.balance()), 1000);
    BOOST_CHECK_EQUAL(flatRow(state.execStorage, table, bcos::ledger::ACCOUNT_TABLE_FIELDS::BALANCE)
                          .value_or("<missing>"),
        "555");

    // setNonce writes the row; nonce() still reads the trie.
    bcos::task::syncWait(account.setNonce("42"));
    BOOST_CHECK_EQUAL(*bcos::task::syncWait(account.nonce()), "7");
    BOOST_CHECK_EQUAL(flatRow(state.execStorage, table, bcos::ledger::ACCOUNT_TABLE_FIELDS::NONCE)
                          .value_or("<missing>"),
        "42");

    // increaseNonce = trie nonce + 1 through the inherited setNonce: overwrites the row with 8
    // (7+1), NOT 43 — the previous simulated setNonce is not read back either.
    bcos::task::syncWait(account.increaseNonce());
    BOOST_CHECK_EQUAL(*bcos::task::syncWait(account.nonce()), "7");
    BOOST_CHECK_EQUAL(flatRow(state.execStorage, table, bcos::ledger::ACCOUNT_TABLE_FIELDS::NONCE)
                          .value_or("<missing>"),
        "8");

    // setStorage writes the raw 32-byte row; storage() still reads the trie.
    bcos::h256 const newValue{0xabcdef};
    bcos::task::syncWait(account.setStorage(toEvmc(state.slot1), toEvmc(newValue)));
    BOOST_CHECK_EQUAL(fromEvmc(bcos::task::syncWait(account.storage(toEvmc(state.slot1)))),
        bcos::h256(state.val1));
    auto const slotRow =
        flatRow(state.execStorage, table, bcos::concepts::bytebuffer::toView(state.slot1));
    BOOST_REQUIRE(slotRow.has_value());
    BOOST_CHECK_EQUAL(bcos::h256(bcos::bytesConstRef(
                          reinterpret_cast<const bcos::byte*>(slotRow->data()), slotRow->size())),
        newValue);

    // setCode lands the code row in execStorage's s_code_binary (inherited behavior); the
    // historical codeHash()/code() are unchanged and backendStorage is untouched.
    bcos::bytes newCode{0xfe, 0xed};
    bcos::crypto::hasher::openssl::OpenSSL_Keccak256_Hasher hasher;
    bcos::h256 newCodeHash;
    bcos::crypto::hasher::hash(hasher, bcos::ref(newCode), newCodeHash);
    bcos::task::syncWait(account.setCode(newCode, "the-abi", newCodeHash));
    BOOST_CHECK_EQUAL(bcos::task::syncWait(account.codeHash()), state.codeHash);
    auto const stillHistoricalCode = bcos::task::syncWait(account.code());
    BOOST_REQUIRE(stillHistoricalCode.has_value());
    auto const stillHistoricalView = stillHistoricalCode->get();
    BOOST_CHECK(bcos::bytes(stillHistoricalView.begin(), stillHistoricalView.end()) == state.code);
    BOOST_CHECK(flatRow(state.execStorage, bcos::ledger::SYS_CODE_BINARY,
        bcos::concepts::bytebuffer::toView(newCodeHash))
            .has_value());

    // create() on an absent account writes the s_tables row (inherited) but exists() still
    // answers from the trie.
    auto created = state.accountAt(makeAddress(0xcd));
    bcos::task::syncWait(created.create());
    BOOST_CHECK(!bcos::task::syncWait(created.exists()));
    BOOST_CHECK(flatRow(state.execStorage, bcos::ledger::SYS_TABLES,
        std::string{"/apps/"} + makeAddress(0xcd).hex())
            .has_value());

    // The historical stores are untouched.
    BOOST_CHECK_EQUAL(storageKeyCount(state.nodeStorage), nodeKeysBefore);
    BOOST_CHECK_EQUAL(storageKeyCount(state.backendStorage), backendKeysBefore);

    // A fresh MPTAccount still reads the ORIGINAL committed state.
    auto pristine = state.account();
    BOOST_CHECK_EQUAL(bcos::task::syncWait(pristine.balance()), 1000);
    BOOST_CHECK_EQUAL(*bcos::task::syncWait(pristine.nonce()), "7");
    BOOST_CHECK_EQUAL(bcos::task::syncWait(pristine.codeHash()), state.codeHash);
}

// The tag-taking read (BYPASS_READ_SET et al. are no-ops here) and storageEntry stay
// consistent with storage(key) — all pure trie
// reads, before AND after a simulated write to the same slot.
BOOST_AUTO_TEST_CASE(DirectAndStorageEntryConsistent)
{
    SeededState state;
    auto account = state.account();

    BOOST_CHECK_EQUAL(fromEvmc(bcos::task::syncWait(
                          account.storage(toEvmc(state.slot1), bcos::storage2::BYPASS_READ_SET))),
        bcos::h256(state.val1));
    auto entry =
        bcos::task::syncWait(account.storageEntry(bcos::concepts::bytebuffer::toView(state.slot1)));
    BOOST_REQUIRE(entry.has_value());
    auto entryView = entry->get();
    BOOST_REQUIRE_EQUAL(entryView.size(), bcos::h256::SIZE);
    BOOST_CHECK_EQUAL(bcos::h256(bcos::bytesConstRef(
                          reinterpret_cast<const bcos::byte*>(entryView.data()), entryView.size())),
        bcos::h256(state.val1));

    // After a simulated setStorage, all three paths STILL return the trie value.
    bcos::task::syncWait(account.setStorage(toEvmc(state.slot1), toEvmc(bcos::h256{0x99})));
    BOOST_CHECK_EQUAL(fromEvmc(bcos::task::syncWait(account.storage(toEvmc(state.slot1)))),
        bcos::h256(state.val1));
    BOOST_CHECK_EQUAL(fromEvmc(bcos::task::syncWait(
                          account.storage(toEvmc(state.slot1), bcos::storage2::BYPASS_READ_SET))),
        bcos::h256(state.val1));
    auto afterWrite =
        bcos::task::syncWait(account.storageEntry(bcos::concepts::bytebuffer::toView(state.slot1)));
    BOOST_REQUIRE(afterWrite.has_value());
    auto afterWriteView = afterWrite->get();
    BOOST_CHECK_EQUAL(
        bcos::h256(bcos::bytesConstRef(
            reinterpret_cast<const bcos::byte*>(afterWriteView.data()), afterWriteView.size())),
        bcos::h256(state.val1));

    // A non-32-byte key cannot exist in a storage trie: absent by construction.
    BOOST_CHECK(!bcos::task::syncWait(account.storageEntry("short-key")).has_value());
}

// An EOA leaf (emptyCodeHash, emptyRootHash storage): exists() with its 4-tuple values, but has
// no code/abi (the emptyCodeHash branch) and every slot reads zero through the
// empty-storage-trie short circuit — no trie descent is even attempted.
BOOST_AUTO_TEST_CASE(EOALeafHasNoCodeAndEmptyStorage)
{
    SeededState state;
    auto eoa = state.accountAt(state.eoaAddr);

    BOOST_CHECK(bcos::task::syncWait(eoa.exists()));
    BOOST_CHECK_EQUAL(bcos::task::syncWait(eoa.balance()), 5);
    BOOST_CHECK_EQUAL(*bcos::task::syncWait(eoa.nonce()), "1");
    BOOST_CHECK_EQUAL(bcos::task::syncWait(eoa.codeHash()), emptyCodeHash());
    BOOST_CHECK(!bcos::task::syncWait(eoa.code()).has_value());
    BOOST_CHECK(!bcos::task::syncWait(eoa.abi()).has_value());
    BOOST_CHECK_EQUAL(
        fromEvmc(bcos::task::syncWait(eoa.storage(toEvmc(state.slot1)))), bcos::h256{});
    BOOST_CHECK(
        !bcos::task::syncWait(eoa.storageEntry(bcos::concepts::bytebuffer::toView(state.slot1)))
            .has_value());
}

namespace
{

/// PR-43's historical storage stack, shrunk to what MPTAccount's HostContext-shaped constructor
/// needs: a Storage carrying the MPT read context via the three getters.
struct StubHistoricalStorage : FlatStorage
{
    NodeStorage* nodes{};
    FlatStorage* backend{};
    bcos::h256 root;

    NodeStorage& mptNodeStorage() { return *nodes; }
    FlatStorage& mptBackendStorage() { return *backend; }
    bcos::h256 mptStateRoot() const { return root; }
};
static_assert(HistoricalStorageContext<StubHistoricalStorage>);
static_assert(!HistoricalStorageContext<FlatStorage>);

}  // namespace

// The (storage, address, binaryAddress) constructor — HostContext's fixed construction shape
// (getAccount / executeCreate build accounts with exactly these three arguments) — pulls the
// trie context off the storage type and reads the same historical state.
BOOST_AUTO_TEST_CASE(HostContextConstructionShape)
{
    SeededState state;
    StubHistoricalStorage stub;
    stub.nodes = &state.nodeStorage;
    stub.backend = &state.backendStorage;
    stub.root = state.stateRoot;

    evmc_address evmcAddr{};
    std::copy(state.addr.begin(), state.addr.end(), evmcAddr.bytes);

    MPTAccount<StubHistoricalStorage, NodeStorage, FlatStorage> account{
        stub, evmcAddr, /*binaryAddress*/ false};
    BOOST_CHECK_EQUAL(bcos::task::syncWait(account.balance()), 1000);
    BOOST_CHECK_EQUAL(*bcos::task::syncWait(account.nonce()), "7");
    BOOST_CHECK_EQUAL(fromEvmc(bcos::task::syncWait(account.storage(toEvmc(state.slot1)))),
        bcos::h256(state.val1));
    BOOST_CHECK_EQUAL(account.address(), state.tableName());
    BOOST_CHECK_EQUAL(account.root(), state.stateRoot);

    // Writes go through the inherited EVMAccount methods into the stub itself.
    bcos::task::syncWait(account.setBalance(bcos::u256{1}));
    BOOST_CHECK_EQUAL(bcos::task::syncWait(account.balance()), 1000);
    BOOST_CHECK(
        flatRow(stub, state.tableName(), bcos::ledger::ACCOUNT_TABLE_FIELDS::BALANCE).has_value());
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::ledger::mpt::test
