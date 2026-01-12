#include "../bcos-transaction-executor/TransactionExecutorImpl.h"
#include "TestBytecode.h"
#include "TestMemoryStorage.h"
#include "bcos-codec/bcos-codec/abi/ContractABICodec.h"
#include "bcos-crypto/ChecksumAddress.h"
#include "bcos-executor/src/Common.h"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/protocol/Protocol.h"
#include "bcos-framework/transaction-executor/TransactionExecutor.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include "bcos-tars-protocol/tars/Transaction.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <evmc/evmc.h>
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

    static_assert(bcos::executor_v1::TransactionExecutor<bcos::executor_v1::TransactionExecutorImpl,
        MutableStorage>);

    TestTransactionExecutorImplFixture()
    {
        bcos::executor::GlobalHashImpl::g_hashImpl = std::make_shared<bcos::crypto::Keccak256>();
    }
};

BOOST_FIXTURE_TEST_SUITE(TransactionExecutorImpl, TestTransactionExecutorImplFixture)

BOOST_AUTO_TEST_CASE(execute)
{
    task::syncWait([this]() mutable -> task::Task<void> {
        bcostars::protocol::BlockHeaderImpl blockHeader;
        blockHeader.setVersion((uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION);
        blockHeader.calculateHash(*cryptoSuite->hashImpl());

        bcos::bytes helloworldBytecodeBinary;
        boost::algorithm::unhex(helloworldBytecode, std::back_inserter(helloworldBytecodeBinary));
        // First deploy
        auto transaction =
            transactionFactory.createTransaction(0, "", helloworldBytecodeBinary, {}, 0, "", "", 0);
        auto receipt = co_await executor.executeTransaction(
            storage, blockHeader, *transaction, 0, ledgerConfig, false);
        BOOST_CHECK_EQUAL(receipt->status(), 0);
        BOOST_CHECK_EQUAL(receipt->contractAddress(), "e0e794ca86d198042b64285c5ce667aee747509b");

        // Set the value
        bcos::codec::abi::ContractABICodec abiCodec(*cryptoSuite->hashImpl());
        auto input = abiCodec.abiIn("setInt(int256)", bcos::s256(10099));
        auto transaction2 = transactionFactory.createTransaction(
            0, std::string(receipt->contractAddress()), input, {}, 0, "", "", 0);
        auto receipt2 = co_await executor.executeTransaction(
            storage, blockHeader, *transaction2, 1, ledgerConfig, false);
        BOOST_CHECK_EQUAL(receipt2->status(), 0);

        // Get the value
        auto input2 = abiCodec.abiIn("getInt()");
        auto transaction3 = transactionFactory.createTransaction(
            0, std::string(receipt->contractAddress()), input2, {}, 0, "", "", 0);
        auto receipt3 = co_await executor.executeTransaction(
            storage, blockHeader, *transaction3, 2, ledgerConfig, false);
        BOOST_CHECK_EQUAL(receipt3->status(), 0);
        bcos::s256 getIntResult = -1;
        abiCodec.abiOut(receipt3->output(), getIntResult);
        BOOST_CHECK_EQUAL(getIntResult, 10099);
    }());
}

BOOST_AUTO_TEST_CASE(transientStorageTest)
{
    task::syncWait([this]() mutable -> task::Task<void> {
        bcostars::protocol::BlockHeaderImpl blockHeader;
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
        auto receipt = co_await executor.executeTransaction(
            storage, blockHeader, *transaction, 3, ledgerConfig, false);
        BOOST_CHECK_EQUAL(receipt->status(), 0);

        // test read and write transient storage
        bcos::codec::abi::ContractABICodec abiCodec(*cryptoSuite->hashImpl());
        auto input = abiCodec.abiIn("storeIntTest(int256)", bcos::s256(10000));
        auto transaction2 = transactionFactory.createTransaction(
            0, std::string(receipt->contractAddress()), input, {}, 0, "", "", 0);
        auto receipt2 = co_await executor.executeTransaction(
            storage, blockHeader, *transaction2, 4, ledgerConfig, false);
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
        auto receipt = co_await executor.executeTransaction(
            storage, blockHeader, *transaction, 5, ledgerConfig, false);
        BOOST_CHECK_EQUAL(receipt->status(), 0);

        // test read and write transient storage
        bcos::codec::abi::ContractABICodec abiCodec(*cryptoSuite->hashImpl());
        auto input = abiCodec.abiIn("checkAndVerifyIntValue(int256)", bcos::h256(12345));
        auto transaction2 = transactionFactory.createTransaction(
            0, std::string(receipt->contractAddress()), input, {}, 0, "", "", 0);
        auto receipt2 = co_await executor.executeTransaction(
            storage, blockHeader, *transaction2, 6, ledgerConfig, false);
        BOOST_CHECK_EQUAL(receipt2->status(), 0);
        bool checkResult = false;
        abiCodec.abiOut(receipt2->output(), checkResult);
        BOOST_CHECK_EQUAL(checkResult, true);
    }());
}


BOOST_AUTO_TEST_CASE(costBalance)
{
    task::syncWait([this]() mutable -> task::Task<void> {
        bcostars::protocol::BlockHeaderImpl blockHeader;
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
        auto receipt = co_await executor.executeTransaction(
            storage, blockHeader, *transaction, 0, ledgerConfig, false);
        BOOST_CHECK_EQUAL(
            receipt->status(), static_cast<int32_t>(protocol::TransactionStatus::NotEnoughCash));

        using namespace std::string_view_literals;
        auto sender = "e0e794ca86d198042b64285c5ce667aee747509b"sv;
        evmc_address senderAddress = unhexAddress(sender);
        transaction->forceSender(
            bytes(senderAddress.bytes, senderAddress.bytes + sizeof(senderAddress.bytes)));

        ledger::account::EVMAccount senderAccount(storage, senderAddress, false);

        constexpr static int64_t initBalance = 149586;
        co_await senderAccount.setBalance(initBalance);

        receipt = co_await executor.executeTransaction(
            storage, blockHeader, *transaction, 0, ledgerConfig, false);
        BOOST_CHECK_EQUAL(receipt->status(), 0);
        BOOST_CHECK_EQUAL(receipt->contractAddress(), "e0e794ca86d198042b64285c5ce667aee747509b");
        BOOST_CHECK_EQUAL(co_await senderAccount.balance(), initBalance - receipt->gasUsed());
    }());
}

BOOST_AUTO_TEST_CASE(insufficientBalanceNoLogs)
{
    using namespace std::string_view_literals;
    task::syncWait([this]() mutable -> task::Task<void> {
        // Prepare block header and enable balance features
        bcostars::protocol::BlockHeaderImpl blockHeader;
        blockHeader.setVersion((uint32_t)bcos::protocol::BlockVersion::V3_13_0_VERSION);
        blockHeader.calculateHash(*cryptoSuite->hashImpl());

        auto features = ledgerConfig.features();
        features.setGenesisFeatures(protocol::BlockVersion::V3_13_0_VERSION);
        features.set(bcos::ledger::Features::Flag::feature_balance);
        features.set(bcos::ledger::Features::Flag::feature_balance_policy1);
        features.set(bcos::ledger::Features::Flag::bugfix_revert_logs);
        ledgerConfig.setFeatures(features);
        // Set gas price high to trigger insufficient balance quickly
        ledgerConfig.setGasPrice({"1000", 0});

        // Deploy HelloWorld contract with a funded deployer
        bcos::bytes helloworldBytecodeBinary;
        boost::algorithm::unhex(helloworldBytecode, std::back_inserter(helloworldBytecodeBinary));
        auto deployTx = transactionFactory.createTransaction(
            0, "", helloworldBytecodeBinary, {}, 0, "", "", 0, std::string{}, {}, {}, 200000);

        // Use a deployer with enough balance for deployment
        auto deployer = unhexAddress("e0e794ca86d198042b64285c5ce667aee747509b"sv);
        deployTx->forceSender(bytes(deployer.bytes, deployer.bytes + sizeof(deployer.bytes)));
        ledger::account::EVMAccount deployerAccount(storage, deployer, false);
        co_await deployerAccount.setBalance(100000000000);

        auto deployReceipt = co_await executor.executeTransaction(
            storage, blockHeader, *deployTx, 10, ledgerConfig, false);
        BOOST_CHECK_EQUAL(deployReceipt->status(), 0);

        // Prepare a call that would emit logs if executed successfully
        bcos::codec::abi::ContractABICodec abiCodec(*cryptoSuite->hashImpl());
        auto callInput = abiCodec.abiIn("logOut()");
        auto callTx =
            transactionFactory.createTransaction(0, std::string(deployReceipt->contractAddress()),
                callInput, {}, 0, "", "", 0, std::string{}, {}, {}, 50000);

        // Set sender balance to 1 to ensure insufficient balance
        auto lowBalanceSender = unhexAddress("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"sv);
        callTx->forceSender(
            bytes(lowBalanceSender.bytes, lowBalanceSender.bytes + sizeof(lowBalanceSender.bytes)));
        ledger::account::EVMAccount lowAccount(storage, lowBalanceSender, false);
        co_await lowAccount.setBalance(0);

        auto callReceipt = co_await executor.executeTransaction(
            storage, blockHeader, *callTx, 11, ledgerConfig, false);
        BOOST_CHECK_EQUAL(callReceipt->status(),
            static_cast<int32_t>(protocol::TransactionStatus::NotEnoughCash));
        // On failure due to insufficient balance, receipt should have no logs
        BOOST_CHECK(callReceipt->logEntries().empty());
    }());
}

BOOST_AUTO_TEST_CASE(revertLogsClearedWithFeature)
{
    // Ensure revert logs are cleared when bugfix_revert_logs is enabled
    task::syncWait([this]() mutable -> task::Task<void> {
        // Block header
        bcostars::protocol::BlockHeaderImpl blockHeader;
        blockHeader.setVersion((uint32_t)bcos::protocol::BlockVersion::MAX_VERSION);
        blockHeader.calculateHash(*cryptoSuite->hashImpl());

        // Enable the bugfix flag
        auto features = ledgerConfig.features();
        features.setGenesisFeatures(bcos::protocol::BlockVersion::MAX_VERSION);
        features.set(bcos::ledger::Features::Flag::bugfix_revert_logs);
        ledgerConfig.setFeatures(features);

        // Create a simple transaction (will not be executed)
        bcos::bytes helloworldBytecodeBinary;
        boost::algorithm::unhex(helloworldBytecode, std::back_inserter(helloworldBytecodeBinary));
        auto tx =
            transactionFactory.createTransaction(0, "", helloworldBytecodeBinary, {}, 0, "", "", 0);

        // Build ExecuteContext directly
        auto execCtx = co_await executor.createExecuteContext(
            storage, blockHeader, *tx, 100, ledgerConfig, false);

        // Seed a fake log entry
        std::vector<uint8_t> addr(20, 0);  // 20-byte address
        bcos::h256s topics{bcos::h256{}};
        bcos::bytes data{'a', 'b', 'c'};
        execCtx.m_data->m_hostContext.logs().emplace_back(
            std::move(addr), std::move(topics), std::move(data));

        // Craft a revert result
        evmc_result base{};
        base.status_code = EVMC_REVERT;
        base.gas_left = 0;
        base.output_data = nullptr;
        base.output_size = 0;
        base.release = nullptr;
        execCtx.m_data->m_evmcResult.emplace(base);
        execCtx.m_data->m_evmcResult->status = bcos::protocol::TransactionStatus::RevertInstruction;

        // Finish and verify logs cleared
        auto receipt = co_await execCtx.template executeStep<2>();
        BOOST_CHECK_NE(receipt->status(), 0);
        BOOST_CHECK(receipt->logEntries().empty());
    }());
}

BOOST_AUTO_TEST_CASE(revertLogsRemainWithoutFeature)
{
    // Verify logs remain when bugfix_revert_logs is disabled
    task::syncWait([this]() mutable -> task::Task<void> {
        // Block header
        bcostars::protocol::BlockHeaderImpl blockHeader;
        blockHeader.setVersion((uint32_t)bcos::protocol::BlockVersion::MAX_VERSION);
        blockHeader.calculateHash(*cryptoSuite->hashImpl());

        // Use a local ledgerConfig with a version prior to enabling bugfix_revert_logs
        bcos::ledger::LedgerConfig localLedgerConfig;
        auto localFeatures = localLedgerConfig.features();
        localFeatures.setGenesisFeatures(bcos::protocol::BlockVersion::V3_16_0_VERSION);
        localLedgerConfig.setFeatures(localFeatures);

        // Create a simple transaction (will not be executed)
        bcos::bytes helloworldBytecodeBinary;
        boost::algorithm::unhex(helloworldBytecode, std::back_inserter(helloworldBytecodeBinary));
        auto tx =
            transactionFactory.createTransaction(0, "", helloworldBytecodeBinary, {}, 0, "", "", 0);

        // Build ExecuteContext directly
        auto execCtx = co_await executor.createExecuteContext(
            storage, blockHeader, *tx, 101, localLedgerConfig, false);

        // Seed a fake log entry
        std::vector<uint8_t> addr(20, 1);  // non-zero 20-byte address
        bcos::h256s topics{bcos::h256{}};
        bcos::bytes data{'x', 'y', 'z'};
        execCtx.m_data->m_hostContext.logs().emplace_back(
            std::move(addr), std::move(topics), std::move(data));

        // Craft a revert result
        evmc_result base{};
        base.status_code = EVMC_REVERT;
        base.gas_left = 0;
        base.output_data = nullptr;
        base.output_size = 0;
        base.release = nullptr;
        execCtx.m_data->m_evmcResult.emplace(base);
        execCtx.m_data->m_evmcResult->status = bcos::protocol::TransactionStatus::RevertInstruction;

        // Finish and verify logs remain (bugfix is off)
        auto receipt = co_await execCtx.template executeStep<2>();
        BOOST_CHECK_NE(receipt->status(), 0);
        BOOST_CHECK(!receipt->logEntries().empty());
    }());
}

BOOST_AUTO_TEST_CASE(web3Nonce)
{
    using namespace std::string_view_literals;
    task::syncWait([this]() mutable -> task::Task<void> {
        bcostars::protocol::BlockHeaderImpl blockHeader;
        blockHeader.setVersion((uint32_t)bcos::protocol::BlockVersion::MAX_VERSION);
        blockHeader.calculateHash(*cryptoSuite->hashImpl());

        auto features = ledgerConfig.features();
        features.setGenesisFeatures(bcos::protocol::BlockVersion::MAX_VERSION);
        ledgerConfig.setFeatures(features);

        bcos::bytes helloworldBytecodeBinary;
        boost::algorithm::unhex(helloworldBytecode, std::back_inserter(helloworldBytecodeBinary));
        // First deploy
        auto transaction = transactionFactory.createTransaction(
            0, "", helloworldBytecodeBinary, "0x5", 0, "", "", 0, std::string{}, {}, {}, 1000);

        evmc_address senderAddress = unhexAddress("e0e794ca86d198042b64285c5ce667aee747509b"sv);
        transaction->forceSender(
            bytes(senderAddress.bytes, senderAddress.bytes + sizeof(senderAddress.bytes)));
        dynamic_cast<bcostars::protocol::TransactionImpl&>(*transaction).mutableInner().type = 1;
        auto receipt = co_await executor.executeTransaction(
            storage, blockHeader, *transaction, 0, ledgerConfig, false);
        BOOST_CHECK_EQUAL(
            receipt->status(), static_cast<int32_t>(protocol::TransactionStatus::None));

        ledger::account::EVMAccount senderAccount(storage, senderAddress, false);
        auto nonce = co_await senderAccount.nonce();
        BOOST_TEST(nonce.value() == "6");

        ledger::account::EVMAccount helloworldAccount(storage, receipt->contractAddress(), false);
        auto contractNonce = co_await helloworldAccount.nonce();
        BOOST_CHECK_EQUAL(contractNonce.value(), "1");

        bcos::codec::abi::ContractABICodec abiCodec(*cryptoSuite->hashImpl());

        std::string hexHelloworldAddress(receipt->contractAddress());
        auto helloworldAddress = unhexAddress(hexHelloworldAddress);
        auto expectAddress = newLegacyEVMAddress(bytesConstRef{helloworldAddress.bytes}, 1);
        ledger::account::EVMAccount expectAccount(storage, expectAddress, false);

        auto input = abiCodec.abiIn("deployAndCall(int256)", bcos::s256(90));
        auto deployCallTx = transactionFactory.createTransaction(0,
            std::string(receipt->contractAddress()), input, "0x6", 0, "", "", 0, {}, {}, {}, 1001);
        deployCallTx->forceSender(
            bytes(senderAddress.bytes, senderAddress.bytes + sizeof(senderAddress.bytes)));
        dynamic_cast<bcostars::protocol::TransactionImpl&>(*deployCallTx).mutableInner().type = 1;
        receipt = co_await executor.executeTransaction(
            storage, blockHeader, *deployCallTx, 0, ledgerConfig, false);

        BOOST_CHECK_EQUAL(receipt->status(), 0);
        BOOST_CHECK_EQUAL((co_await senderAccount.nonce()).value(), "7");
        BOOST_CHECK_EQUAL((co_await helloworldAccount.nonce()).value(), "2");

        BOOST_REQUIRE(co_await expectAccount.exists());
        auto expectNonce = co_await expectAccount.nonce();
        BOOST_CHECK_EQUAL(expectNonce.value(), "1");

        input = abiCodec.abiIn("returnRevert()");
        auto revertTx = transactionFactory.createTransaction(0,
            std::string(receipt->contractAddress()), input, "0x10", 0, "", "", 0, {}, {}, {}, 1002);
        revertTx->forceSender(
            bytes(senderAddress.bytes, senderAddress.bytes + sizeof(senderAddress.bytes)));
        dynamic_cast<bcostars::protocol::TransactionImpl&>(*revertTx).mutableInner().type = 1;
        receipt = co_await executor.executeTransaction(
            storage, blockHeader, *revertTx, 0, ledgerConfig, false);

        BOOST_CHECK_NE(receipt->status(), 0);
        BOOST_CHECK_EQUAL((co_await senderAccount.nonce()).value(), "17");
        BOOST_CHECK_EQUAL((co_await helloworldAccount.nonce()).value(), "2");
        BOOST_CHECK_EQUAL((co_await expectAccount.nonce()).value(), "1");

        // Contract deploy 10 contracts
        input = abiCodec.abiIn("deployWithDeploy()");
        auto deployDeployTx = transactionFactory.createTransaction(
            0, hexHelloworldAddress, input, "0x1a", 0, "", "", 0, {}, {}, {}, 1001);
        deployDeployTx->forceSender(
            bytes(senderAddress.bytes, senderAddress.bytes + sizeof(senderAddress.bytes)));
        dynamic_cast<bcostars::protocol::TransactionImpl&>(*deployDeployTx).mutableInner().type = 1;

        receipt = co_await executor.executeTransaction(
            storage, blockHeader, *deployDeployTx, 0, ledgerConfig, false);

        BOOST_TEST(receipt->status() == 0);
        bcos::Address address1{};
        abiCodec.abiOut(receipt->output(), address1);
        bcos::ledger::account::EVMAccount deployAccount(storage, address1, false);
        BOOST_CHECK_EQUAL((co_await deployAccount.nonce()).value(), "11");

        for (auto i : ::ranges::views::iota(1, 11))
        {
            auto expectAddress =
                newLegacyEVMAddress(bytesConstRef{address1.data(), address1.size()}, i);
            ledger::account::EVMAccount account(storage, expectAddress, false);
            BOOST_TEST(co_await account.exists());
            BOOST_TEST((co_await account.nonce()).value() == "1");
        }
    }());
}

BOOST_AUTO_TEST_CASE(callGas)
{
    task::syncWait([this]() mutable -> task::Task<void> {
        bcostars::protocol::BlockHeaderImpl blockHeader;
        blockHeader.setVersion((uint32_t)bcos::protocol::BlockVersion::MAX_VERSION);
        blockHeader.calculateHash(*cryptoSuite->hashImpl());

        ledgerConfig.setGasPrice({"21000", 0});
        ledgerConfig.setBalanceTransfer(true);
        bcos::bytes helloworldBytecodeBinary;
        boost::algorithm::unhex(helloworldBytecode, std::back_inserter(helloworldBytecodeBinary));
        // First deploy
        auto transaction =
            transactionFactory.createTransaction(0, "", helloworldBytecodeBinary, {}, 0, "", "", 0);
        auto receipt = co_await executor.executeTransaction(
            storage, blockHeader, *transaction, 0, ledgerConfig, false);
        BOOST_CHECK_EQUAL(receipt->status(), 7);
        BOOST_CHECK_EQUAL(receipt->contractAddress(), "");

        auto receipt2 = co_await executor.executeTransaction(
            storage, blockHeader, *transaction, 0, ledgerConfig, true);
        BOOST_CHECK_EQUAL(receipt2->status(), 0);
        BOOST_CHECK_EQUAL(receipt2->contractAddress(), "e0e794ca86d198042b64285c5ce667aee747509b");
    }());
}

BOOST_AUTO_TEST_CASE(cashRevert)
{
    task::syncWait([this]() mutable -> task::Task<void> {
        bcostars::protocol::BlockHeaderImpl blockHeader;
        blockHeader.setVersion((uint32_t)bcos::protocol::BlockVersion::MAX_VERSION);
        blockHeader.calculateHash(*cryptoSuite->hashImpl());

        ledgerConfig.setGasPrice({"10000", 0});
        ledgerConfig.setBalanceTransfer(true);
        bcos::bytes helloworldBytecodeBinary;
        boost::algorithm::unhex(helloworldBytecode, std::back_inserter(helloworldBytecodeBinary));
        auto transaction =
            transactionFactory.createTransaction(0, "", helloworldBytecodeBinary, {}, 0, "", "", 0);
        using namespace std::string_view_literals;
        evmc_address senderAddress = unhexAddress("e0e794ca86d198042b64285c5ce667aee747509b"sv);
        transaction->forceSender(bytesConstRef{senderAddress.bytes}.toBytes());
        dynamic_cast<bcostars::protocol::TransactionImpl&>(*transaction).mutableInner().type = 1;

        auto receipt = co_await executor.executeTransaction(
            storage, blockHeader, *transaction, 0, ledgerConfig, false);
        BOOST_CHECK_EQUAL(receipt->status(), 7);
        BOOST_CHECK_EQUAL(receipt->contractAddress(), "");

        ledger::account::EVMAccount senderAccount(storage, senderAddress, false);
        auto nonce = co_await senderAccount.nonce();
        BOOST_CHECK_EQUAL(nonce.value(), "1");
    }());
}

BOOST_AUTO_TEST_CASE(proxyReceive)
{
    task::syncWait([this]() mutable -> task::Task<void> {
        bcostars::protocol::BlockHeaderImpl blockHeader;
        blockHeader.setVersion((uint32_t)bcos::protocol::BlockVersion::MAX_VERSION);
        blockHeader.calculateHash(*cryptoSuite->hashImpl());

        ledger::Features features = ledgerConfig.features();
        features.set(bcos::ledger::Features::Flag::bugfix_delegatecall_transfer);
        features.set(bcos::ledger::Features::Flag::bugfix_nonce_initialize);
        ledgerConfig.setBalanceTransfer(true);
        ledgerConfig.setFeatures(features);

        using namespace std::string_literals;
        using namespace std::string_view_literals;
        auto sender = "e0e794ca86d198042b64285c5ce667aee747509b"sv;
        evmc_address senderAddress = unhexAddress(sender);
        ledger::account::EVMAccount senderAccount(storage, senderAddress, false);
        co_await senderAccount.setBalance(500);

        bcos::bytes input;
        boost::algorithm::unhex(ETHReceiverV1ByteCode, std::back_inserter(input));
        auto transaction = transactionFactory.createTransaction(
            1, "", input, "0", 0, "", "", 0, ""s, "0x32", {}, 0, "0x0", "0x0");
        transaction->forceSender(bytesConstRef{senderAddress.bytes}.toBytes());
        dynamic_cast<bcostars::protocol::TransactionImpl&>(*transaction).mutableInner().type = 1;

        auto receipt = co_await executor.executeTransaction(
            storage, blockHeader, *transaction, 0, ledgerConfig, false);
        BOOST_CHECK_EQUAL(receipt->status(), 0);
        BOOST_CHECK_GT(receipt->contractAddress().size(), 0);
        BOOST_CHECK_EQUAL(co_await senderAccount.balance(), 450);

        ledger::account::EVMAccount implAccount(storage, receipt->contractAddress(), false);
        BOOST_CHECK_EQUAL(co_await implAccount.balance(), 50);

        bcos::bytes input2;
        boost::algorithm::unhex(TRANSPARENT_UPGRADEABLE_PROXY_BYTECODE, std::back_inserter(input2));
        bcos::codec::abi::ContractABICodec abiCodec(*cryptoSuite->hashImpl());
        auto data = abiCodec.abiIn("", bcos::toAddress(std::string(receipt->contractAddress())),
            bcos::toAddress(std::string(sender)), bcos::bytes{});
        input2.insert(input2.end(), data.begin(), data.end());

        auto transaction2 = transactionFactory.createTransaction(0, "", input2, "1", 0, "", "", 0);
        transaction2->forceSender(bytesConstRef{senderAddress.bytes}.toBytes());
        dynamic_cast<bcostars::protocol::TransactionImpl&>(*transaction2).mutableInner().type = 1;

        auto receipt2 = co_await executor.executeTransaction(
            storage, blockHeader, *transaction2, 1, ledgerConfig, false);
        BOOST_CHECK_EQUAL(receipt2->status(), 0);
        BOOST_CHECK_GT(receipt2->contractAddress().size(), 0);
        BOOST_CHECK_NE(receipt->contractAddress(), receipt2->contractAddress());

        auto transferInput = abiCodec.abiIn("transfer()");
        auto transaction3 =
            transactionFactory.createTransaction(1, std::string{receipt2->contractAddress()},
                transferInput, "2", 0, "", "", 0, ""s, "0x64", {}, 0, "0x0", "0x0");
        transaction3->forceSender(bytesConstRef{senderAddress.bytes}.toBytes());
        dynamic_cast<bcostars::protocol::TransactionImpl&>(*transaction3).mutableInner().type = 1;
        auto receipt3 = co_await executor.executeTransaction(
            storage, blockHeader, *transaction3, 2, ledgerConfig, false);
        BOOST_CHECK_EQUAL(receipt3->status(), 0);
        BOOST_CHECK_EQUAL(co_await senderAccount.balance(), 350);

        ledger::account::EVMAccount proxyAccount(storage, receipt2->contractAddress(), false);
        BOOST_CHECK_EQUAL(co_await proxyAccount.balance(), 100);
        BOOST_CHECK_EQUAL(co_await implAccount.balance(), 50);

        auto queryInput = abiCodec.abiIn("totalETH()");
        auto transaction4 =
            transactionFactory.createTransaction(1, std::string{receipt->contractAddress()},
                queryInput, {}, 0, "", "", 0, ""s, "0x0", {}, 0, "0x0", "0x0");
        transaction4->forceSender(bytesConstRef{senderAddress.bytes}.toBytes());
        dynamic_cast<bcostars::protocol::TransactionImpl&>(*transaction4).mutableInner().type = 1;
        auto receipt4 = co_await executor.executeTransaction(
            storage, blockHeader, *transaction4, 3, ledgerConfig, true);
        BOOST_CHECK_EQUAL(receipt4->status(), 0);
        u256 totalETH = 0;
        abiCodec.abiOut(receipt4->output(), totalETH);
        BOOST_CHECK_EQUAL(totalETH, 50);

        auto transaction5 =
            transactionFactory.createTransaction(1, std::string{receipt2->contractAddress()},
                queryInput, {}, 0, "", "", 0, ""s, "0x0", {}, 0, "0x0", "0x0");
        transaction5->forceSender(bytesConstRef{senderAddress.bytes}.toBytes());
        dynamic_cast<bcostars::protocol::TransactionImpl&>(*transaction5).mutableInner().type = 1;
        auto receipt5 = co_await executor.executeTransaction(
            storage, blockHeader, *transaction5, 4, ledgerConfig, true);
        BOOST_CHECK_EQUAL(receipt5->status(), 0);
        totalETH = 0;
        abiCodec.abiOut(receipt5->output(), totalETH);
        BOOST_CHECK_EQUAL(totalETH, 100);
    }());
}

BOOST_AUTO_TEST_SUITE_END()
