/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief End-to-end smoke for EIP-7702 on transaction-executor (type-4, executor v1 path).
 *  @file CompatEip7702SmokeTest.cpp
 */

#include "../bcos-transaction-executor/TransactionExecutorImpl.h"
#include "Eip7702TestHelpers.h"
#include "TestMemoryStorage.h"
#include "bcos-executor/src/Web3Eip7702Apply.h"
#include "bcos-framework/ledger/EVMAccount.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-executor/src/Common.h>
#include <bcos-framework/protocol/Protocol.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-task/Wait.h>
#include <boost/test/unit_test.hpp>
#include <cstring>

using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::executor_v1;
using namespace bcos::test::eip7702;
using bcos::ledger::account::EVMAccount;

namespace bcos::test
{

class CompatEip7702SmokeFixture
{
public:
    MutableStorage storage;
    ledger::LedgerConfig ledgerConfig;
    std::shared_ptr<crypto::CryptoSuite> cryptoSuite = std::make_shared<crypto::CryptoSuite>(
        std::make_shared<crypto::Keccak256>(), nullptr, nullptr);
    bcostars::protocol::TransactionReceiptFactoryImpl receiptFactory{cryptoSuite};
    PrecompiledManager precompiledManager{cryptoSuite->hashImpl()};
    TransactionExecutorImpl executor{receiptFactory, cryptoSuite->hashImpl(), precompiledManager};
    bcostars::protocol::BlockHeaderImpl blockHeader;

    CompatEip7702SmokeFixture()
    {
        executor::GlobalHashImpl::g_hashImpl = cryptoSuite->hashImpl();
        setPragueFeatures(ledgerConfig);
        setLedgerChainId(ledgerConfig, 1);
        ledgerConfig.setGasPrice({"1", 0});
        blockHeader.setVersion(static_cast<uint32_t>(protocol::BlockVersion::MAX_VERSION));
        blockHeader.calculateHash(*cryptoSuite->hashImpl());
    }

    task::Task<void> fundSender(evmc_address const& sender, u256 balance = u256(1) << 96)
    {
        EVMAccount<decltype(storage)> account(storage, sender, false);
        if (!co_await account.exists())
        {
            co_await account.create();
        }
        co_await account.setBalance(balance);
        co_await account.setNonce("0");
    }
};

BOOST_FIXTURE_TEST_SUITE(CompatEip7702Smoke, CompatEip7702SmokeFixture)

BOOST_AUTO_TEST_CASE(type4_apply_delegation_smoke)
{
    task::syncWait([this]() -> task::Task<void> {
        auto const keyPair = testAuthorityKeyPair();
        auto const authority = authorityAddressFromKey(cryptoSuite->hashImpl(), keyPair);
        auto const authorityEvmc = executor::addressToEvmc(authority);

        EVMAccount<decltype(storage)> authorityAccount(storage, authorityEvmc, false);
        co_await authorityAccount.create();
        co_await authorityAccount.setNonce("0");

        evmc_address targetEvmc{};
        targetEvmc.bytes[19] = 0x42;
        Address target{};
        std::memcpy(target.data(), targetEvmc.bytes, sizeof(targetEvmc.bytes));
        auto auth = signAuthorizationTuple(cryptoSuite->hashImpl(), keyPair, 1, target, 0);

        evmc_address sender{};
        sender.bytes[19] = 0x99;
        co_await fundSender(sender);

        auto tx = makeWeb3Type4Transaction(*cryptoSuite, {auth}, sender, std::nullopt, bytes{}, 0);
        auto receipt =
            co_await executor.executeTransaction(storage, blockHeader, *tx, 0, ledgerConfig, false);

        BOOST_REQUIRE(receipt);
        BOOST_CHECK_EQUAL(receipt->status(), 0);

        auto const expected = makeDelegationIndicatorCode(target);
        auto const codeEntry = co_await readAccountCode(storage, authorityEvmc, false);
        BOOST_REQUIRE(codeEntry);
        auto const view = codeEntry->get();
        BOOST_CHECK_EQUAL(view.size(), expected.size());
        BOOST_CHECK(executor::isEip7702DelegationIndicator(
            bytesConstRef(reinterpret_cast<byte const*>(view.data()), view.size())));

        auto const nonce = co_await authorityAccount.nonce();
        BOOST_CHECK_EQUAL(nonce.value(), "1");
    }());
}

BOOST_AUTO_TEST_CASE(type4_invalid_auth_skipped_smoke)
{
    task::syncWait([this]() -> task::Task<void> {
        auto const keyPair = testAuthorityKeyPair();
        auto const authority = authorityAddressFromKey(cryptoSuite->hashImpl(), keyPair);
        auto const authorityEvmc = executor::addressToEvmc(authority);

        EVMAccount<decltype(storage)> authorityAccount(storage, authorityEvmc, false);
        co_await authorityAccount.create();
        co_await authorityAccount.setNonce("5");

        evmc_address targetEvmc{};
        targetEvmc.bytes[19] = 0x43;
        Address target{};
        std::memcpy(target.data(), targetEvmc.bytes, sizeof(targetEvmc.bytes));
        auto auth = signAuthorizationTuple(cryptoSuite->hashImpl(), keyPair, 1, target, 0);

        evmc_address sender{};
        sender.bytes[19] = 0x98;
        co_await fundSender(sender);

        auto tx = makeWeb3Type4Transaction(*cryptoSuite, {auth}, sender, std::nullopt, bytes{}, 0);
        auto receipt =
            co_await executor.executeTransaction(storage, blockHeader, *tx, 0, ledgerConfig, false);

        BOOST_REQUIRE(receipt);
        BOOST_CHECK_EQUAL(receipt->status(), 0);

        auto const codeEntry = co_await readAccountCode(storage, authorityEvmc, false);
        BOOST_CHECK(!codeEntry || codeEntry->get().empty());
        auto const nonce = co_await authorityAccount.nonce();
        BOOST_CHECK_EQUAL(nonce.value(), "5");
    }());
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
