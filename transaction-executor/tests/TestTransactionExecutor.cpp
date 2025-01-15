#include "../bcos-transaction-executor/TransactionExecutorImpl.h"
#include "TestBytecode.h"
#include "TestMemoryStorage.h"
#include "bcos-codec/bcos-codec/abi/ContractABICodec.h"
#include "bcos-executor/src/Common.h"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/protocol/Protocol.h"
#include "bcos-framework/transaction-executor/TransactionExecutor.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <evmc/evmc.h>
#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>
#include <memory>

using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::transaction_executor;

class TestTransactionExecutorImplFixture
{
public:
    MutableStorage storage;
    ledger::LedgerConfig ledgerConfig;
    std::shared_ptr<bcos::crypto::CryptoSuite> cryptoSuite =
        std::make_shared<bcos::crypto::CryptoSuite>(
            std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr);
    bcostars::protocol::TransactionFactoryImpl transactionFactory{cryptoSuite};
    bcostars::protocol::TransactionReceiptFactoryImpl receiptFactory{cryptoSuite};
    PrecompiledManager precompiledManager{cryptoSuite->hashImpl()};
    bcos::transaction_executor::TransactionExecutorImpl executor{
        receiptFactory, cryptoSuite->hashImpl(), precompiledManager};
};

BOOST_FIXTURE_TEST_SUITE(TransactionExecutorImpl, TestTransactionExecutorImplFixture)

BOOST_AUTO_TEST_CASE(execute)
{
    task::syncWait([this]() mutable -> task::Task<void> {
        bcostars::protocol::BlockHeaderImpl blockHeader(
            [inner = bcostars::BlockHeader()]() mutable { return std::addressof(inner); });
        blockHeader.setVersion((uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION);
        blockHeader.calculateHash(*cryptoSuite->hashImpl());

        bcos::bytes helloworldBytecodeBinary;
        boost::algorithm::unhex(helloworldBytecode, std::back_inserter(helloworldBytecodeBinary));
        // First deploy
        auto transaction =
            transactionFactory.createTransaction(0, "", helloworldBytecodeBinary, {}, 0, "", "", 0);
        auto receipt = co_await bcos::transaction_executor::executeTransaction(
            executor, storage, blockHeader, *transaction, 0, ledgerConfig, task::syncWait);
        BOOST_CHECK_EQUAL(receipt->status(), 0);
        BOOST_CHECK_EQUAL(receipt->contractAddress(), "e0e794ca86d198042b64285c5ce667aee747509b");

        // Set the value
        bcos::codec::abi::ContractABICodec abiCodec(*cryptoSuite->hashImpl());
        auto input = abiCodec.abiIn("setInt(int256)", bcos::s256(10099));
        auto transaction2 = transactionFactory.createTransaction(
            0, std::string(receipt->contractAddress()), input, {}, 0, "", "", 0);
        auto receipt2 = co_await bcos::transaction_executor::executeTransaction(
            executor, storage, blockHeader, *transaction2, 1, ledgerConfig, task::syncWait);
        BOOST_CHECK_EQUAL(receipt2->status(), 0);

        // Get the value
        auto input2 = abiCodec.abiIn("getInt()");
        auto transaction3 = transactionFactory.createTransaction(
            0, std::string(receipt->contractAddress()), input2, {}, 0, "", "", 0);
        auto receipt3 = co_await bcos::transaction_executor::executeTransaction(
            executor, storage, blockHeader, *transaction3, 2, ledgerConfig, task::syncWait);
        BOOST_CHECK_EQUAL(receipt3->status(), 0);
        bcos::s256 getIntResult = -1;
        abiCodec.abiOut(receipt3->output(), getIntResult);
        BOOST_CHECK_EQUAL(getIntResult, 10099);
    }());
}

BOOST_AUTO_TEST_CASE(transientStorageTest)
{
    task::syncWait([this]() mutable -> task::Task<void> {
        bcostars::protocol::BlockHeaderImpl blockHeader(
            [inner = bcostars::BlockHeader()]() mutable { return std::addressof(inner); });
        blockHeader.setVersion((uint32_t)bcos::protocol::BlockVersion::V3_7_0_VERSION);
        blockHeader.calculateHash(*cryptoSuite->hashImpl());

        // turn on evm_cancun feature
        ledger::Features features;
        features.set(ledger::Features::Flag::feature_evm_cancun);
        ledgerConfig.setFeatures(features);

        bcos::bytes transientStorageBinary;
        boost::algorithm::unhex(
            transientStorageBytecode, std::back_inserter(transientStorageBinary));
        // First deploy
        auto transaction =
            transactionFactory.createTransaction(0, "", transientStorageBinary, {}, 0, "", "", 0);
        auto receipt = co_await bcos::transaction_executor::executeTransaction(
            executor, storage, blockHeader, *transaction, 3, ledgerConfig, task::syncWait);
        BOOST_CHECK_EQUAL(receipt->status(), 0);

        // test read and write transient storage
        bcos::codec::abi::ContractABICodec abiCodec(*cryptoSuite->hashImpl());
        auto input = abiCodec.abiIn("storeIntTest(int256)", bcos::s256(10000));
        auto transaction2 = transactionFactory.createTransaction(
            0, std::string(receipt->contractAddress()), input, {}, 0, "", "", 0);
        auto receipt2 = co_await bcos::transaction_executor::executeTransaction(
            executor, storage, blockHeader, *transaction2, 4, ledgerConfig, task::syncWait);
        BOOST_CHECK_EQUAL(receipt2->status(), 0);
        bcos::s256 getIntResult = -1;
        abiCodec.abiOut(receipt2->output(), getIntResult);
        BOOST_CHECK_EQUAL(getIntResult, 10000);
    }());
}

BOOST_AUTO_TEST_CASE(transientStorageContractTest)
{
    task::syncWait([this]() mutable -> task::Task<void> {
        PrecompiledManager precompiledManager(cryptoSuite->hashImpl());
        bcos::transaction_executor::TransactionExecutorImpl executor(
            receiptFactory, cryptoSuite->hashImpl(), precompiledManager);
        bcostars::protocol::BlockHeaderImpl blockHeader(
            [inner = bcostars::BlockHeader()]() mutable { return std::addressof(inner); });
        blockHeader.setVersion((uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION);
        blockHeader.calculateHash(*cryptoSuite->hashImpl());

        // turn on evm_cancun feature
        ledger::Features features;
        features.set(ledger::Features::Flag::feature_evm_cancun);
        ledgerConfig.setFeatures(features);

        bcos::bytes transientStorageBinary;
        boost::algorithm::unhex(
            transientStorageContractTestByteCode, std::back_inserter(transientStorageBinary));
        // First deploy
        auto transaction =
            transactionFactory.createTransaction(0, "", transientStorageBinary, {}, 0, "", "", 0);
        auto receipt = co_await bcos::transaction_executor::executeTransaction(
            executor, storage, blockHeader, *transaction, 5, ledgerConfig, task::syncWait);
        BOOST_CHECK_EQUAL(receipt->status(), 0);

        // test read and write transient storage
        bcos::codec::abi::ContractABICodec abiCodec(*cryptoSuite->hashImpl());
        auto input = abiCodec.abiIn("checkAndVerifyIntValue(int256)", bcos::h256(12345));
        auto transaction2 = transactionFactory.createTransaction(
            0, std::string(receipt->contractAddress()), input, {}, 0, "", "", 0);
        auto receipt2 = co_await bcos::transaction_executor::executeTransaction(
            executor, storage, blockHeader, *transaction2, 6, ledgerConfig, task::syncWait);
        BOOST_CHECK_EQUAL(receipt2->status(), 0);
        bool checkResult = false;
        abiCodec.abiOut(receipt2->output(), checkResult);
        BOOST_CHECK_EQUAL(checkResult, true);
    }());
}


BOOST_AUTO_TEST_CASE(costBalance)
{
    task::syncWait([this]() mutable -> task::Task<void> {
        bcostars::protocol::BlockHeaderImpl blockHeader(
            [inner = bcostars::BlockHeader()]() mutable { return std::addressof(inner); });
        blockHeader.setVersion((uint32_t)bcos::protocol::BlockVersion::V3_13_0_VERSION);
        blockHeader.calculateHash(*cryptoSuite->hashImpl());

        auto features = ledgerConfig.features();
        features.setGenesisFeatures(protocol::BlockVersion::V3_13_0_VERSION);
        features.set(bcos::ledger::Features::Flag::feature_balance);
        features.set(bcos::ledger::Features::Flag::feature_balance_policy1);
        ledgerConfig.setFeatures(features);
        ledgerConfig.setGasPrice({"1", 0});

        bcos::bytes helloworldBytecodeBinary;
        boost::algorithm::unhex(helloworldBytecode, std::back_inserter(helloworldBytecodeBinary));
        // First deploy
        auto transaction = transactionFactory.createTransaction(
            0, "", helloworldBytecodeBinary, {}, 0, "", "", 0, std::string{}, {}, {}, 1000);
        auto receipt = co_await bcos::transaction_executor::executeTransaction(
            executor, storage, blockHeader, *transaction, 0, ledgerConfig, task::syncWait);
        BOOST_CHECK_EQUAL(
            receipt->status(), static_cast<int32_t>(protocol::TransactionStatus::NotEnoughCash));

        using namespace std::string_view_literals;
        auto sender = "e0e794ca86d198042b64285c5ce667aee747509b"sv;
        evmc_address senderAddress = unhexAddress(sender);
        transaction->forceSender(
            bytes(senderAddress.bytes, senderAddress.bytes + sizeof(senderAddress.bytes)));

        ledger::account::EVMAccount senderAccount(storage, senderAddress, false);

        constexpr static int64_t initBalance = 90000 + 21000;
        co_await ledger::account::setBalance(senderAccount, initBalance);

        receipt = co_await bcos::transaction_executor::executeTransaction(
            executor, storage, blockHeader, *transaction, 0, ledgerConfig, task::syncWait);
        BOOST_CHECK_EQUAL(receipt->status(), 0);
        BOOST_CHECK_EQUAL(receipt->contractAddress(), "e0e794ca86d198042b64285c5ce667aee747509b");
        BOOST_CHECK_EQUAL(
            co_await ledger::account::balance(senderAccount), initBalance - receipt->gasUsed());
    }());
}

// BOOST_AUTO_TEST_CASE(bugfixPrecompiled)
// {
//     task::syncWait([this]() mutable -> task::Task<void> {
//         bcostars::protocol::BlockHeaderImpl blockHeader(
//             [inner = bcostars::BlockHeader()]() mutable { return std::addressof(inner); });
//         blockHeader.setVersion((uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION);
//         blockHeader.calculateHash(*cryptoSuite->hashImpl());

//         auto features = ledgerConfig.features();
//         features.setGenesisFeatures(protocol::BlockVersion::V3_13_0_VERSION);
//         features.set(bcos::ledger::Features::Flag::feature_balance);
//         features.set(bcos::ledger::Features::Flag::feature_balance_policy1);
//         ledgerConfig.setFeatures(features);
//         ledgerConfig.setGasPrice({"1", 0});

//         bcos::bytes helloworldBytecodeBinary;
//         boost::algorithm::unhex(helloworldBytecode,
//         std::back_inserter(helloworldBytecodeBinary));
//         // First deploy
//         auto transaction = transactionFactory.createTransaction(
//             0, "", helloworldBytecodeBinary, {}, 0, "", "", 0, std::string{}, {}, {}, 1000);
//         auto receipt = co_await bcos::transaction_executor::executeTransaction(
//             executor, storage, blockHeader, *transaction, 0, ledgerConfig, task::syncWait);
//         BOOST_CHECK_EQUAL(
//             receipt->status(), static_cast<int32_t>(protocol::TransactionStatus::NotEnoughCash));

//         using namespace std::string_view_literals;
//         auto sender = "e0e794ca86d198042b64285c5ce667aee747509b"sv;
//         evmc_address senderAddress = unhexAddress(sender);
//         transaction->forceSender(
//             bytes(senderAddress.bytes, senderAddress.bytes + sizeof(senderAddress.bytes)));

//         ledger::account::EVMAccount senderAccount(storage, senderAddress, false);
//         co_await ledger::account::setBalance(senderAccount, 90000);

//         receipt = co_await bcos::transaction_executor::executeTransaction(
//             executor, storage, blockHeader, *transaction, 0, ledgerConfig, task::syncWait);
//         BOOST_CHECK_EQUAL(receipt->status(), 0);
//         BOOST_CHECK_EQUAL(receipt->contractAddress(),
//         "e0e794ca86d198042b64285c5ce667aee747509b"); BOOST_CHECK_EQUAL(
//             co_await ledger::account::balance(senderAccount), 90000 - receipt->gasUsed());
//     }());
// }

BOOST_AUTO_TEST_SUITE_END()