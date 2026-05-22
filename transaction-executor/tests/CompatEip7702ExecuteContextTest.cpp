/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief EIP-7702 ExecuteContext apply / skip / revert / refund tests (design §9.4).
 */

#include "../bcos-transaction-executor/TransactionExecutorImpl.h"
#include "Eip7702TestHelpers.h"
#include "TestMemoryStorage.h"
#include "bcos-executor/src/Web3Eip7702Apply.h"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/protocol/Protocol.h"
#include "bcos-task/Wait.h"
#include "bcos-transaction-executor/Eip7702Common.h"

using bcos::task::syncWait;
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <boost/test/unit_test.hpp>
#include <cstring>

using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::executor_v1;
using namespace bcos::test::eip7702;
using bcos::ledger::account::EVMAccount;

namespace bcos::test
{

namespace
{
evmc_address evmcAddr19(uint8_t suffix)
{
    evmc_address a{};
    a.bytes[19] = suffix;
    return a;
}

Address address19(uint8_t suffix)
{
    evmc_address a = evmcAddr19(suffix);
    Address out;
    std::memcpy(out.data(), a.bytes, sizeof(a.bytes));
    return out;
}
}  // namespace

class CompatEip7702ExecFixture
{
public:
    MutableStorage storage;
    ledger::LedgerConfig ledgerConfig;
    std::shared_ptr<crypto::CryptoSuite> cryptoSuite = std::make_shared<crypto::CryptoSuite>(
        std::make_shared<crypto::Keccak256>(), nullptr, nullptr);
    bcostars::protocol::TransactionFactoryImpl transactionFactory{cryptoSuite};
    bcostars::protocol::TransactionReceiptFactoryImpl receiptFactory{cryptoSuite};
    PrecompiledManager precompiledManager{cryptoSuite->hashImpl()};
    TransactionExecutorImpl executor{receiptFactory, cryptoSuite->hashImpl(), precompiledManager};
    bcostars::protocol::BlockHeaderImpl blockHeader;

    CompatEip7702ExecFixture()
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

    task::Task<TransactionExecutorImpl::ExecuteContext<MutableStorage>> runSteps01(
        protocol::Transaction const& tx, int contextId = 0)
    {
        auto execCtx = co_await executor.createExecuteContext(
            storage, blockHeader, tx, contextId, ledgerConfig, false);
        co_await execCtx.template executeStep<0>();
        co_await execCtx.template executeStep<1>();
        co_return std::move(execCtx);
    }
};

BOOST_FIXTURE_TEST_SUITE(CompatEip7702ExecuteContext, CompatEip7702ExecFixture)

BOOST_AUTO_TEST_CASE(SkipsInvalidChainId)
{
    syncWait([this]() -> task::Task<void> {
        auto const keyPair = testAuthorityKeyPair();
        auto const authority = authorityAddressFromKey(cryptoSuite->hashImpl(), keyPair);
        auto const target = address19(0x22);
        auto auth = signAuthorizationTuple(cryptoSuite->hashImpl(), keyPair, 999, target, 0);

        evmc_address sender = evmcAddr19(0x01);
        co_await fundSender(sender);

        auto tx = makeWeb3Type4Transaction(*cryptoSuite, {auth}, sender, Address{}, bytes{}, 0);
        auto execCtx = co_await runSteps01(*tx);

        BOOST_CHECK_EQUAL(execCtx.m_data->m_eip7702Refund, 0);
        auto const codeEntry =
            co_await readAccountCode(storage, executor::addressToEvmc(authority), false);
        BOOST_CHECK(!codeEntry || codeEntry->get().empty());
    }());
}

BOOST_AUTO_TEST_CASE(SkipsInvalidNonce)
{
    syncWait([this]() -> task::Task<void> {
        auto const keyPair = testAuthorityKeyPair();
        auto const authority = authorityAddressFromKey(cryptoSuite->hashImpl(), keyPair);
        auto const authorityEvmc = executor::addressToEvmc(authority);
        auto const target = address19(0x23);

        EVMAccount<decltype(storage)> authorityAccount(storage, authorityEvmc, false);
        co_await authorityAccount.create();
        co_await authorityAccount.setNonce("7");

        auto auth = signAuthorizationTuple(cryptoSuite->hashImpl(), keyPair, 1, target, 0);

        evmc_address sender = evmcAddr19(0x02);
        co_await fundSender(sender);

        auto tx = makeWeb3Type4Transaction(*cryptoSuite, {auth}, sender, Address{}, bytes{}, 0);
        auto execCtx = co_await runSteps01(*tx);

        BOOST_CHECK_EQUAL(execCtx.m_data->m_eip7702Refund, 0);
        auto const codeEntry = co_await readAccountCode(storage, authorityEvmc, false);
        BOOST_CHECK(!codeEntry || codeEntry->get().empty());
        auto const nonce = co_await authorityAccount.nonce();
        BOOST_CHECK_EQUAL(nonce.value(), "7");
    }());
}

BOOST_AUTO_TEST_CASE(SkipsNonDelegationCode)
{
    syncWait([this]() -> task::Task<void> {
        auto const keyPair = testAuthorityKeyPair();
        auto const authority = authorityAddressFromKey(cryptoSuite->hashImpl(), keyPair);
        auto const authorityEvmc = executor::addressToEvmc(authority);
        auto const target = address19(0x24);

        EVMAccount<decltype(storage)> authorityAccount(storage, authorityEvmc, false);
        co_await authorityAccount.create();
        co_await authorityAccount.setNonce("0");
        bcos::bytes const regularCode{0x60, 0x00, 0x52, 0x00};
        auto const codeHash =
            cryptoSuite->hashImpl()->hash(bytesConstRef(regularCode.data(), regularCode.size()));
        co_await authorityAccount.setCode(regularCode, "", codeHash);

        auto auth = signAuthorizationTuple(cryptoSuite->hashImpl(), keyPair, 1, target, 0);

        evmc_address sender = evmcAddr19(0x03);
        co_await fundSender(sender);

        auto tx = makeWeb3Type4Transaction(*cryptoSuite, {auth}, sender, Address{}, bytes{}, 0);
        co_await runSteps01(*tx);

        auto const codeEntry = co_await readAccountCode(storage, authorityEvmc, false);
        BOOST_REQUIRE(codeEntry);
        BOOST_CHECK_EQUAL(codeEntry->get().size(), regularCode.size());
        BOOST_CHECK(!executor::isEip7702DelegationIndicator(bytesConstRef(
            reinterpret_cast<byte const*>(codeEntry->get().data()), codeEntry->get().size())));
    }());
}

BOOST_AUTO_TEST_CASE(AppliesIndicator)
{
    syncWait([this]() -> task::Task<void> {
        auto const keyPair = testAuthorityKeyPair();
        auto const authority = authorityAddressFromKey(cryptoSuite->hashImpl(), keyPair);
        auto const authorityEvmc = executor::addressToEvmc(authority);
        auto const target = address19(0x25);

        EVMAccount<decltype(storage)> authorityAccount(storage, authorityEvmc, false);
        co_await authorityAccount.create();
        co_await authorityAccount.setNonce("0");

        auto auth = signAuthorizationTuple(cryptoSuite->hashImpl(), keyPair, 1, target, 0);

        evmc_address sender = evmcAddr19(0x04);
        co_await fundSender(sender);

        auto tx = makeWeb3Type4Transaction(*cryptoSuite, {auth}, sender, Address{}, bytes{}, 0);
        co_await runSteps01(*tx);

        auto const expected = makeDelegationIndicatorCode(target);
        auto const codeEntry = co_await readAccountCode(storage, authorityEvmc, false);
        BOOST_REQUIRE(codeEntry);
        auto const view = codeEntry->get();
        BOOST_CHECK_EQUAL(view.size(), expected.size());
        BOOST_CHECK(executor::isEip7702DelegationIndicator(
            bytesConstRef(reinterpret_cast<byte const*>(view.data()), view.size())));

        auto const codeHash = co_await authorityAccount.codeHash();
        auto const expectedHash =
            cryptoSuite->hashImpl()->hash(bytesConstRef(expected.data(), expected.size()));
        BOOST_CHECK_EQUAL(codeHash, expectedHash);
    }());
}

BOOST_AUTO_TEST_CASE(ClearsOnZeroTarget)
{
    syncWait([this]() -> task::Task<void> {
        auto const keyPair = testAuthorityKeyPair();
        auto const authority = authorityAddressFromKey(cryptoSuite->hashImpl(), keyPair);
        auto const authorityEvmc = executor::addressToEvmc(authority);
        auto const target = address19(0x26);

        co_await setDelegationIndicator(storage, cryptoSuite->hashImpl(), authorityEvmc, target);

        EVMAccount<decltype(storage)> authorityAccount(storage, authorityEvmc, false);
        co_await authorityAccount.setNonce("0");

        Address const zeroAddress;
        auto auth = signAuthorizationTuple(cryptoSuite->hashImpl(), keyPair, 1, zeroAddress, 0);

        evmc_address sender = evmcAddr19(0x05);
        co_await fundSender(sender);

        auto tx = makeWeb3Type4Transaction(*cryptoSuite, {auth}, sender, Address{}, bytes{}, 0);
        co_await runSteps01(*tx);

        auto const codeEntry = co_await readAccountCode(storage, authorityEvmc, false);
        BOOST_CHECK(!codeEntry || codeEntry->get().empty());
        auto const codeHash = co_await authorityAccount.codeHash();
        BOOST_CHECK_EQUAL(codeHash, cryptoSuite->hashImpl()->hash(std::string_view{}));
    }());
}

BOOST_AUTO_TEST_CASE(RefundForExistingAuthority)
{
    syncWait([this]() -> task::Task<void> {
        auto const keyPair = testAuthorityKeyPair();
        auto const authority = authorityAddressFromKey(cryptoSuite->hashImpl(), keyPair);
        auto const authorityEvmc = executor::addressToEvmc(authority);
        auto const target = address19(0x27);

        EVMAccount<decltype(storage)> authorityAccount(storage, authorityEvmc, false);
        co_await authorityAccount.create();
        co_await authorityAccount.setNonce("0");

        auto auth = signAuthorizationTuple(cryptoSuite->hashImpl(), keyPair, 1, target, 0);

        evmc_address sender = evmcAddr19(0x06);
        co_await fundSender(sender);

        auto tx = makeWeb3Type4Transaction(*cryptoSuite, {auth}, sender, Address{}, bytes{}, 0);
        auto execCtx = co_await runSteps01(*tx);

        BOOST_CHECK_EQUAL(
            execCtx.m_data->m_eip7702Refund, executor_v1::EIP_7702_REFUND_PER_EXISTING_AUTHORITY);
    }());
}

BOOST_AUTO_TEST_CASE(IncrementsAuthorityNonce)
{
    syncWait([this]() -> task::Task<void> {
        auto const keyPair = testAuthorityKeyPair();
        auto const authority = authorityAddressFromKey(cryptoSuite->hashImpl(), keyPair);
        auto const authorityEvmc = executor::addressToEvmc(authority);
        auto const target = address19(0x28);

        EVMAccount<decltype(storage)> authorityAccount(storage, authorityEvmc, false);
        co_await authorityAccount.create();
        co_await authorityAccount.setNonce("3");

        auto auth = signAuthorizationTuple(cryptoSuite->hashImpl(), keyPair, 1, target, 3);

        evmc_address sender = evmcAddr19(0x07);
        co_await fundSender(sender);

        auto tx = makeWeb3Type4Transaction(*cryptoSuite, {auth}, sender, Address{}, bytes{}, 0);
        co_await runSteps01(*tx);

        auto const nonce = co_await authorityAccount.nonce();
        BOOST_CHECK_EQUAL(nonce.value(), "4");
    }());
}

BOOST_AUTO_TEST_CASE(RefundAccumulatedOnGasRefund)
{
    syncWait([this]() -> task::Task<void> {
        auto const keyPair = testAuthorityKeyPair();
        auto const authority = authorityAddressFromKey(cryptoSuite->hashImpl(), keyPair);
        auto const authorityEvmc = executor::addressToEvmc(authority);
        auto const target = address19(0x29);

        EVMAccount<decltype(storage)> authorityAccount(storage, authorityEvmc, false);
        co_await authorityAccount.create();
        co_await authorityAccount.setNonce("0");

        auto auth = signAuthorizationTuple(cryptoSuite->hashImpl(), keyPair, 1, target, 0);

        evmc_address sender = evmcAddr19(0x08);
        co_await fundSender(sender);

        evmc_address recipient = evmcAddr19(0x30);
        EVMAccount<decltype(storage)> recipientAccount(storage, recipient, false);
        co_await recipientAccount.create();
        bcos::bytes const stopCode{0x00};
        auto const stopHash =
            cryptoSuite->hashImpl()->hash(bytesConstRef(stopCode.data(), stopCode.size()));
        co_await recipientAccount.setCode(stopCode, "", stopHash);

        auto tx = makeWeb3Type4Transaction(
            *cryptoSuite, {auth}, sender, address19(0x30), bytes{}, 0, 500'000);
        auto execCtx = co_await runSteps01(*tx);

        BOOST_REQUIRE(execCtx.m_data->m_evmcResult);
        BOOST_CHECK_GE(execCtx.m_data->m_evmcResult->gas_refund,
            executor_v1::EIP_7702_REFUND_PER_EXISTING_AUTHORITY);
    }());
}

BOOST_AUTO_TEST_CASE(PreservesOrder)
{
    syncWait([this]() -> task::Task<void> {
        auto const keyPair = testAuthorityKeyPair();
        auto const authority = authorityAddressFromKey(cryptoSuite->hashImpl(), keyPair);
        auto const authorityEvmc = executor::addressToEvmc(authority);
        auto const targetA = address19(0x31);
        auto const targetB = address19(0x32);

        EVMAccount<decltype(storage)> authorityAccount(storage, authorityEvmc, false);
        co_await authorityAccount.create();
        co_await authorityAccount.setNonce("0");

        auto auth0 = signAuthorizationTuple(cryptoSuite->hashImpl(), keyPair, 1, targetA, 0);
        auto auth1 = signAuthorizationTuple(cryptoSuite->hashImpl(), keyPair, 1, targetB, 1);

        evmc_address sender = evmcAddr19(0x09);
        co_await fundSender(sender);

        auto tx =
            makeWeb3Type4Transaction(*cryptoSuite, {auth0, auth1}, sender, Address{}, bytes{}, 0);
        co_await runSteps01(*tx);

        auto const expected = makeDelegationIndicatorCode(targetB);
        auto const codeEntry = co_await readAccountCode(storage, authorityEvmc, false);
        BOOST_REQUIRE(codeEntry);
        auto const view = codeEntry->get();
        BOOST_REQUIRE_EQUAL(view.size(), expected.size());
        for (size_t i = 0; i < expected.size(); ++i)
        {
            BOOST_CHECK_EQUAL(static_cast<uint8_t>(view[i]), expected[i]);
        }
        auto const nonce = co_await authorityAccount.nonce();
        BOOST_CHECK_EQUAL(nonce.value(), "2");
    }());
}

BOOST_AUTO_TEST_CASE(RevertDoesNotUndoDelegations)
{
    syncWait([this]() -> task::Task<void> {
        auto const keyPair = testAuthorityKeyPair();
        auto const authority = authorityAddressFromKey(cryptoSuite->hashImpl(), keyPair);
        auto const authorityEvmc = executor::addressToEvmc(authority);
        auto const target = address19(0x33);

        EVMAccount<decltype(storage)> authorityAccount(storage, authorityEvmc, false);
        co_await authorityAccount.create();
        co_await authorityAccount.setNonce("0");

        auto auth = signAuthorizationTuple(cryptoSuite->hashImpl(), keyPair, 1, target, 0);

        evmc_address sender = evmcAddr19(0x0a);
        co_await fundSender(sender);

        evmc_address recipient = evmcAddr19(0x34);
        EVMAccount<decltype(storage)> recipientAccount(storage, recipient, false);
        co_await recipientAccount.create();
        auto const revHash = cryptoSuite->hashImpl()->hash(
            bytesConstRef(revertBytecode().data(), revertBytecode().size()));
        co_await recipientAccount.setCode(revertBytecode(), "", revHash);

        auto tx =
            makeWeb3Type4Transaction(*cryptoSuite, {auth}, sender, address19(0x34), bytes{}, 0);
        auto receipt =
            co_await executor.executeTransaction(storage, blockHeader, *tx, 0, ledgerConfig, false);
        BOOST_CHECK_NE(receipt->status(), 0);

        auto const codeEntry = co_await readAccountCode(storage, authorityEvmc, false);
        BOOST_REQUIRE(codeEntry);
        BOOST_CHECK(executor::isEip7702DelegationIndicator(bytesConstRef(
            reinterpret_cast<byte const*>(codeEntry->get().data()), codeEntry->get().size())));
    }());
}

BOOST_AUTO_TEST_CASE(InsufficientBalanceDoesNotUndoDelegations)
{
    syncWait([this]() -> task::Task<void> {
        ledger::LedgerConfig localConfig;
        setPragueFeatures(localConfig);
        setLedgerChainId(localConfig, 1);
        localConfig.setGasPrice({"1000000000000000000000000", 0});

        auto const keyPair = testAuthorityKeyPair();
        auto const authority = authorityAddressFromKey(cryptoSuite->hashImpl(), keyPair);
        auto const authorityEvmc = executor::addressToEvmc(authority);
        auto const target = address19(0x35);

        EVMAccount<decltype(storage)> authorityAccount(storage, authorityEvmc, false);
        co_await authorityAccount.create();
        co_await authorityAccount.setNonce("0");

        auto auth = signAuthorizationTuple(cryptoSuite->hashImpl(), keyPair, 1, target, 0);

        evmc_address sender = evmcAddr19(0x0b);
        co_await fundSender(sender, 1);

        evmc_address recipient = evmcAddr19(0x36);
        EVMAccount<decltype(storage)> recipientAccount(storage, recipient, false);
        co_await recipientAccount.create();
        bcos::bytes const stopCode{0x00};
        auto const stopHash =
            cryptoSuite->hashImpl()->hash(bytesConstRef(stopCode.data(), stopCode.size()));
        co_await recipientAccount.setCode(stopCode, "", stopHash);

        auto tx = makeWeb3Type4Transaction(
            *cryptoSuite, {auth}, sender, address19(0x36), bytes{}, 0, 10'000'000);
        auto receipt =
            co_await executor.executeTransaction(storage, blockHeader, *tx, 1, localConfig, false);
        BOOST_CHECK_EQUAL(
            receipt->status(), static_cast<int32_t>(protocol::TransactionStatus::NotEnoughCash));

        auto const codeEntry = co_await readAccountCode(storage, authorityEvmc, false);
        BOOST_REQUIRE(codeEntry);
        BOOST_CHECK(executor::isEip7702DelegationIndicator(bytesConstRef(
            reinterpret_cast<byte const*>(codeEntry->get().data()), codeEntry->get().size())));
    }());
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
