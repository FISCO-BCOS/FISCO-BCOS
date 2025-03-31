#include "../bcos-transaction-executor/TransactionExecutorImpl.h"
#include "TestBytecode.h"
#include "TestMemoryStorage.h"
#include "bcos-codec/bcos-codec/abi/ContractABICodec.h"
#include "bcos-crypto/ChecksumAddress.h"
#include "bcos-executor/src/Common.h"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/protocol/Protocol.h"
#include "bcos-framework/transaction-executor/TransactionExecutor.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include "bcos-tars-protocol/tars/Transaction.h"
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
using namespace bcos::executor_v1;

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
    bcos::executor_v1::TransactionExecutorImpl executor{
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
        auto receipt = co_await bcos::executor_v1::executeTransaction(
            executor, storage, blockHeader, *transaction, 0, ledgerConfig, false, task::syncWait);
        BOOST_CHECK_EQUAL(receipt->status(), 0);
        BOOST_CHECK_EQUAL(receipt->contractAddress(), "e0e794ca86d198042b64285c5ce667aee747509b");

        // Set the value
        bcos::codec::abi::ContractABICodec abiCodec(*cryptoSuite->hashImpl());
        auto input = abiCodec.abiIn("setInt(int256)", bcos::s256(10099));
        auto transaction2 = transactionFactory.createTransaction(
            0, std::string(receipt->contractAddress()), input, {}, 0, "", "", 0);
        auto receipt2 = co_await bcos::executor_v1::executeTransaction(
            executor, storage, blockHeader, *transaction2, 1, ledgerConfig, false, task::syncWait);
        BOOST_CHECK_EQUAL(receipt2->status(), 0);

        // Get the value
        auto input2 = abiCodec.abiIn("getInt()");
        auto transaction3 = transactionFactory.createTransaction(
            0, std::string(receipt->contractAddress()), input2, {}, 0, "", "", 0);
        auto receipt3 = co_await bcos::executor_v1::executeTransaction(
            executor, storage, blockHeader, *transaction3, 2, ledgerConfig, false, task::syncWait);
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
        auto receipt = co_await bcos::executor_v1::executeTransaction(
            executor, storage, blockHeader, *transaction, 3, ledgerConfig, false, task::syncWait);
        BOOST_CHECK_EQUAL(receipt->status(), 0);

        // test read and write transient storage
        bcos::codec::abi::ContractABICodec abiCodec(*cryptoSuite->hashImpl());
        auto input = abiCodec.abiIn("storeIntTest(int256)", bcos::s256(10000));
        auto transaction2 = transactionFactory.createTransaction(
            0, std::string(receipt->contractAddress()), input, {}, 0, "", "", 0);
        auto receipt2 = co_await bcos::executor_v1::executeTransaction(
            executor, storage, blockHeader, *transaction2, 4, ledgerConfig, false, task::syncWait);
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
        bcos::executor_v1::TransactionExecutorImpl executor(
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
        auto receipt = co_await bcos::executor_v1::executeTransaction(
            executor, storage, blockHeader, *transaction, 5, ledgerConfig, false, task::syncWait);
        BOOST_CHECK_EQUAL(receipt->status(), 0);

        // test read and write transient storage
        bcos::codec::abi::ContractABICodec abiCodec(*cryptoSuite->hashImpl());
        auto input = abiCodec.abiIn("checkAndVerifyIntValue(int256)", bcos::h256(12345));
        auto transaction2 = transactionFactory.createTransaction(
            0, std::string(receipt->contractAddress()), input, {}, 0, "", "", 0);
        auto receipt2 = co_await bcos::executor_v1::executeTransaction(
            executor, storage, blockHeader, *transaction2, 6, ledgerConfig, false, task::syncWait);
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
        auto receipt = co_await bcos::executor_v1::executeTransaction(
            executor, storage, blockHeader, *transaction, 0, ledgerConfig, false, task::syncWait);
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

        receipt = co_await bcos::executor_v1::executeTransaction(
            executor, storage, blockHeader, *transaction, 0, ledgerConfig, false, task::syncWait);
        BOOST_CHECK_EQUAL(receipt->status(), 0);
        BOOST_CHECK_EQUAL(receipt->contractAddress(), "e0e794ca86d198042b64285c5ce667aee747509b");
        BOOST_CHECK_EQUAL(
            co_await ledger::account::balance(senderAccount), initBalance - receipt->gasUsed());
    }());
}

BOOST_AUTO_TEST_CASE(nonce)
{
    task::syncWait([this]() mutable -> task::Task<void> {
        bcostars::protocol::BlockHeaderImpl blockHeader(
            [inner = bcostars::BlockHeader()]() mutable { return std::addressof(inner); });
        blockHeader.setVersion((uint32_t)bcos::protocol::BlockVersion::V3_15_0_VERSION);
        blockHeader.calculateHash(*cryptoSuite->hashImpl());

        bcos::bytes helloworldBytecodeBinary;
        boost::algorithm::unhex(helloworldBytecode, std::back_inserter(helloworldBytecodeBinary));
        // First deploy
        auto transaction = transactionFactory.createTransaction(
            0, "", helloworldBytecodeBinary, "0x5", 0, "", "", 0, std::string{}, {}, {}, 1000);
        using namespace std::string_view_literals;
        evmc_address senderAddress = unhexAddress("e0e794ca86d198042b64285c5ce667aee747509b"sv);
        transaction->forceSender(
            bytes(senderAddress.bytes, senderAddress.bytes + sizeof(senderAddress.bytes)));
        auto& tarsTransaction = dynamic_cast<bcostars::protocol::TransactionImpl&>(*transaction);
        tarsTransaction.mutableInner().type = 1;
        auto receipt = co_await bcos::executor_v1::executeTransaction(
            executor, storage, blockHeader, *transaction, 0, ledgerConfig, false, task::syncWait);
        BOOST_CHECK_EQUAL(
            receipt->status(), static_cast<int32_t>(protocol::TransactionStatus::None));

        ledger::account::EVMAccount senderAccount(storage, senderAddress, false);
        auto nonce = co_await ledger::account::nonce(senderAccount);
        BOOST_CHECK_EQUAL(nonce.value(), "6");

        ledger::account::EVMAccount contractAccount(storage, receipt->contractAddress(), false);
        auto contractNonce = co_await ledger::account::nonce(contractAccount);
        BOOST_CHECK_EQUAL(contractNonce.value(), "1");

        bcos::codec::abi::ContractABICodec abiCodec(*cryptoSuite->hashImpl());

        auto newAddress = unhexAddress(receipt->contractAddress());
        auto expectAddress = newLegacyEVMAddress(bytesConstRef{newAddress.bytes}, 1);
        ledger::account::EVMAccount expectAccount(storage, expectAddress, false);

        auto input = abiCodec.abiIn("deployAndCall(int256)", bcos::s256(90));
        auto deployCallTx = transactionFactory.createTransaction(0,
            std::string(receipt->contractAddress()), input, "0x5", 0, "", "", 0, {}, {}, {}, 1001);
        deployCallTx->forceSender(
            bytes(senderAddress.bytes, senderAddress.bytes + sizeof(senderAddress.bytes)));
        dynamic_cast<bcostars::protocol::TransactionImpl&>(*deployCallTx).mutableInner().type = 1;
        receipt = co_await bcos::executor_v1::executeTransaction(
            executor, storage, blockHeader, *deployCallTx, 0, ledgerConfig, false, task::syncWait);

        BOOST_CHECK_EQUAL(receipt->status(), 0);
        nonce = co_await ledger::account::nonce(senderAccount);
        BOOST_CHECK_EQUAL(nonce.value(), "7");

        contractNonce = co_await ledger::account::nonce(contractAccount);
        BOOST_REQUIRE(contractNonce);
        BOOST_CHECK_EQUAL(*contractNonce, "2");

        BOOST_REQUIRE(co_await ledger::account::exists(expectAccount));
        auto expectNonce = co_await ledger::account::nonce(expectAccount);
        BOOST_CHECK_EQUAL(expectNonce.value(), "1");

        input = abiCodec.abiIn("returnRevert()");
        auto revertTx = transactionFactory.createTransaction(0,
            std::string(receipt->contractAddress()), input, "0x10", 0, "", "", 0, {}, {}, {}, 1002);
        revertTx->forceSender(
            bytes(senderAddress.bytes, senderAddress.bytes + sizeof(senderAddress.bytes)));
        dynamic_cast<bcostars::protocol::TransactionImpl&>(*revertTx).mutableInner().type = 1;
        receipt = co_await bcos::executor_v1::executeTransaction(
            executor, storage, blockHeader, *revertTx, 0, ledgerConfig, false, task::syncWait);

        BOOST_CHECK_NE(receipt->status(), 0);
        BOOST_CHECK_EQUAL((co_await ledger::account::nonce(senderAccount)).value(), "17");
        BOOST_CHECK_EQUAL((co_await ledger::account::nonce(contractAccount)).value(), "2");
        BOOST_CHECK_EQUAL((co_await ledger::account::nonce(expectAccount)).value(), "1");
    }());
}

BOOST_AUTO_TEST_CASE(bugfixPrecompiled)
{
    // task::syncWait([this]() mutable -> task::Task<void> {
    //     bcostars::protocol::BlockHeaderImpl blockHeader(
    //         [inner = bcostars::BlockHeader()]() mutable { return std::addressof(inner); });
    //     blockHeader.setVersion((uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION);
    //     blockHeader.calculateHash(*cryptoSuite->hashImpl());

    //     auto features = ledgerConfig.features();
    //     features.setGenesisFeatures(protocol::BlockVersion::V3_13_0_VERSION);
    //     features.set(bcos::ledger::Features::Flag::feature_balance);
    //     features.set(bcos::ledger::Features::Flag::feature_balance_policy1);
    //     ledgerConfig.setFeatures(features);
    //     ledgerConfig.setGasPrice({"1", 0});

    //     bcos::bytes helloworldBytecodeBinary;
    //     boost::algorithm::unhex(helloworldBytecode,
    //     std::back_inserter(helloworldBytecodeBinary));
    //     // First deploy
    //     auto transaction = transactionFactory.createTransaction(
    //         0, "", helloworldBytecodeBinary, {}, 0, "", "", 0, std::string{}, {}, {}, 1000);
    //     auto receipt = co_await bcos::executor_v1::executeTransaction(
    //         executor, storage, blockHeader, *transaction, 0, ledgerConfig, task::syncWait);
    //     BOOST_CHECK_EQUAL(
    //         receipt->status(), static_cast<int32_t>(protocol::TransactionStatus::NotEnoughCash));

    //     using namespace std::string_view_literals;
    //     auto sender = "e0e794ca86d198042b64285c5ce667aee747509b"sv;
    //     evmc_address senderAddress = unhexAddress(sender);
    //     transaction->forceSender(
    //         bytes(senderAddress.bytes, senderAddress.bytes + sizeof(senderAddress.bytes)));

    //     ledger::account::EVMAccount senderAccount(storage, senderAddress, false);
    //     co_await ledger::account::setBalance(senderAccount, 90000);

    //     receipt = co_await bcos::executor_v1::executeTransaction(
    //         executor, storage, blockHeader, *transaction, 0, ledgerConfig, task::syncWait);
    //     BOOST_CHECK_EQUAL(receipt->status(), 0);
    //     BOOST_CHECK_EQUAL(receipt->contractAddress(),
    //     "e0e794ca86d198042b64285c5ce667aee747509b"); BOOST_CHECK_EQUAL(
    //         co_await ledger::account::balance(senderAccount), 90000 - receipt->gasUsed());
    // }());
}

BOOST_AUTO_TEST_SUITE_END()
