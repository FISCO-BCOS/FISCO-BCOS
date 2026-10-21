/*
 * Copyright 2026 FISCO BCOS.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 * @file TestEESTPostState.cpp
 * @brief Regression coverage for the EEST runner's surplus-account verdict.
 */

// Exercise the runner's actual checker without copying its implementation or
// exporting a test-only API from the standalone executable.
#define main eestFixtureRunnerMain
#include "EESTRunner.cpp"
#undef main

#include "bcos-framework/ledger/GenesisConfig.h"
#include "bcos-ledger/GenesisStateRoot.h"
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::executor_v1;

namespace
{
evmc_address surplusAddress()
{
    evmc_address address{};
    address.bytes[18] = 0x12;
    address.bytes[19] = 0x34;
    return address;
}

constexpr std::string_view c_surplusHex = "0000000000000000000000000000000000001234";
constexpr std::string_view c_knownHex = "0000000000000000000000000000000000000055";

struct PostStateFixture
{
    EESTRunner runner;
    MutableStorage storage;
    std::map<std::string, bcos::test::EESTAccount> expected;

    PostStateFixture()
    {
        evmc_address knownAddress{};
        knownAddress.bytes[19] = 0x55;
        auto known = executor_v1::eth::ethViewAccount(storage, knownAddress);
        task::tbb::syncWait([&]() -> task::Task<void> {
            co_await known.create();
            co_await known.setNonce("7");
        }());
        auto& account = expected[std::string(c_knownHex)];
        account.nonce = "0x7";
        account.balance = "0x0";
        account.code = "0x";
    }

    std::string verify() { return runner.verifyPostState(storage, expected); }
};

evmc_bytes32 slotWith(std::size_t index, uint8_t value)
{
    evmc_bytes32 slot{};
    slot.bytes[31] = static_cast<uint8_t>(index);
    slot.bytes[0] = value;
    return slot;
}
}  // namespace

BOOST_FIXTURE_TEST_SUITE(EESTPostStateSuite, PostStateFixture)

BOOST_AUTO_TEST_CASE(unexpectedStorageWithoutCodeIsRejected)
{
    auto account = executor_v1::eth::ethViewAccount(storage, surplusAddress());
    evmc_bytes32 zeroSlot{}, laterSlot{}, value{};
    laterSlot.bytes[31] = 1;
    value.bytes[31] = 42;
    task::tbb::syncWait([&]() -> task::Task<void> {
        co_await account.create();
        co_await account.setNonce("1");
        co_await account.setStorage(zeroSlot, {});
        co_await account.setStorage(laterSlot, value);
    }());
    BOOST_REQUIRE(!task::tbb::syncWait(account.code()));
    BOOST_REQUIRE_EQUAL(task::tbb::syncWait(account.storage(laterSlot)).bytes[31], 42);

    auto errors = verify();
    auto diagnostic = "SURPLUS " + std::string(c_surplusHex);
    BOOST_REQUIRE_MESSAGE(errors.find(diagnostic) != std::string::npos,
        "A surplus account with nonzero storage and no code must fail post-state validation");
    BOOST_CHECK_EQUAL(errors.find(diagnostic, errors.find(diagnostic) + 1), std::string::npos);
}

BOOST_AUTO_TEST_CASE(zeroSlotsAndAccountMetadataAreIgnored)
{
    auto account = executor_v1::eth::ethViewAccount(storage, surplusAddress());
    task::tbb::syncWait([&]() -> task::Task<void> {
        co_await account.create();
        co_await account.setNonce("0");
        co_await account.setBalance(100);
        co_await account.setStorage(slotWith(1, 0), {});
        co_await account.setCode({}, "", runner.cryptoSuite->hashImpl()->hash(std::string_view{}));
    }());
    BOOST_CHECK_MESSAGE(verify().empty(), "Fee artifacts and zero-valued slots are not surplus");
}

BOOST_AUTO_TEST_CASE(unexpectedCodeRemainsRejected)
{
    auto account = executor_v1::eth::ethViewAccount(storage, surplusAddress());
    bytes code{0x00};
    task::tbb::syncWait([&]() -> task::Task<void> {
        co_await account.create();
        co_await account.setCode(code, "",
            runner.cryptoSuite->hashImpl()->hash(bytesConstRef{code.data(), code.size()}));
    }());
    auto errors = verify();
    auto diagnostic = "SURPLUS " + std::string(c_surplusHex);
    BOOST_REQUIRE_NE(errors.find(diagnostic), std::string::npos);
    BOOST_CHECK_EQUAL(errors.find(diagnostic, errors.find(diagnostic) + 1), std::string::npos);
}

BOOST_AUTO_TEST_CASE(expectedStorageIsNotSurplus)
{
    auto account = executor_v1::eth::ethViewAccount(storage, surplusAddress());
    evmc_bytes32 value{};
    value.bytes[31] = 42;
    task::tbb::syncWait([&]() -> task::Task<void> {
        co_await account.create();
        co_await account.setNonce("1");
        co_await account.setStorage(slotWith(1, 0), value);
    }());
    auto& expectedAccount = expected[std::string(c_surplusHex)];
    expectedAccount.nonce = "0x1";
    expectedAccount.balance = "0x0";
    expectedAccount.code = "0x";
    expectedAccount.storage["0x0000000000000000000000000000000000000000000000000000000000000001"] =
        "0x2a";
    BOOST_CHECK_MESSAGE(verify().empty(), "Storage belonging to an expected account must match");
}

BOOST_AUTO_TEST_CASE(nonAccountRowsAreIgnored)
{
    std::string slot(sizeof(evmc_bytes32), '\0');
    std::string value(sizeof(evmc_bytes32), '\1');
    task::tbb::syncWait(
        storage2::writeOne(storage, StateKey{"/tables/non-account", slot}, storage::Entry{value}));
    BOOST_CHECK(verify().empty());
}

// The flat-storage root calculator must agree with the genesis state-root path, which
// is the independent implementation of the same Ethereum secure-trie rule. The two
// paths take different inputs (allocs vs account rows), so agreement pins the mapping:
// nonce/balance/codeHash/storage -> account leaf and storage trie.
BOOST_AUTO_TEST_CASE(postStateRootMatchesGenesisOracle)
{
    MutableStorage storage;
    EESTRunner runner;

    evmc_address eoa{};
    eoa.bytes[19] = 0x55;
    evmc_address contract{};
    contract.bytes[19] = 0x66;
    auto eoaAccount = executor_v1::eth::ethViewAccount(storage, eoa);
    auto contractAccount = executor_v1::eth::ethViewAccount(storage, contract);
    bcos::bytes code{0x60, 0x00};
    evmc_bytes32 slot{}, slotValue{};
    slot.bytes[31] = 1;
    slotValue.bytes[31] = 42;
    task::tbb::syncWait([&]() -> task::Task<void> {
        co_await eoaAccount.create();
        co_await eoaAccount.setNonce("7");
        co_await eoaAccount.setBalance(100);
        co_await contractAccount.create();
        co_await contractAccount.setNonce("0");
        co_await contractAccount.setBalance(5);
        co_await contractAccount.setStorage(slot, slotValue);
        co_await contractAccount.setCode(code, "",
            runner.cryptoSuite->hashImpl()->hash(bytesConstRef{code.data(), code.size()}));
    }());

    auto makeAlloc = [](std::string address, std::string nonce, u256 balance, std::string codeHex) {
        bcos::ledger::Alloc alloc;
        alloc.address = std::move(address);
        alloc.nonce = std::move(nonce);
        alloc.balance = balance;
        alloc.code = std::move(codeHex);
        return alloc;
    };
    bcos::ledger::GenesisConfig genesis;
    genesis.m_executorVersion = bcos::ledger::ETHEREUM_EXECUTOR_VERSION;
    auto eoaAlloc = makeAlloc("0x0000000000000000000000000000000000000055", "7", 100, "0x");
    auto contractAlloc = makeAlloc("0x0000000000000000000000000000000000000066", "0", 5, "0x6000");
    contractAlloc.storage.emplace_back(
        "0x0000000000000000000000000000000000000000000000000000000000000001",
        "0x000000000000000000000000000000000000000000000000000000000000002a");
    genesis.m_allocs = {std::move(eoaAlloc), std::move(contractAlloc)};

    auto expected = task::tbb::syncWait(bcos::ledger::computeGenesisStateTrie(genesis)).root;
    auto actual = runner.computePostStateRoot(storage, EVMC_CANCUN);
    BOOST_CHECK_EQUAL(actual.hex(), expected.hex());
}

BOOST_AUTO_TEST_CASE(wrongPostStateRootIsRejected)
{
    // The fixture already holds the known account; a mismatching root must fail even
    // when every per-account field matches.
    auto errors = runner.verifyPostState(storage, expected, std::string(66, '0'));
    BOOST_CHECK(errors.find("STATE_ROOT") != std::string::npos);
}

BOOST_AUTO_TEST_SUITE_END()
