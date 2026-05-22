/*
 * End-to-end transaction-executor tests for EIP-7623 TE gas settlement (spec §6.2).
 */
#include "../bcos-transaction-executor/TransactionExecutorImpl.h"
#include "Eip7702TestHelpers.h"
#include "TestMemoryStorage.h"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/protocol/Protocol.h"
#include "bcos-rpc/web3jsonrpc/model/Web3Transaction.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include "bcos-tars-protocol/tars/Transaction.h"
#include "bcos-transaction-executor/gas/EthTxGasSettlement.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-task/Wait.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::executor_v1;
using namespace bcos::test::eip7702;
using bcos::ledger::account::EVMAccount;

namespace bcos::test
{

class EthTxGasSettlementExecutorFixture
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
    int contextId = 0;

    EthTxGasSettlementExecutorFixture()
    {
        executor::GlobalHashImpl::g_hashImpl = cryptoSuite->hashImpl();
        setPragueFeatures(ledgerConfig);
        setLedgerChainId(ledgerConfig, 1);
        ledgerConfig.setGasPrice({"1", 0});
        ledgerConfig.setBalanceTransfer(true);
        blockHeader.setVersion(static_cast<uint32_t>(protocol::BlockVersion::MAX_VERSION));
        blockHeader.calculateHash(*cryptoSuite->hashImpl());
    }

    task::Task<void> deployStopAt(evmc_address const& addr)
    {
        EVMAccount<decltype(storage)> account(storage, addr, false);
        if (!co_await account.exists())
        {
            co_await account.create();
        }
        bytes const stopCode{0x00};
        auto const codeHash = cryptoSuite->hashImpl()->hash(ref(stopCode));
        co_await account.setCode(stopCode, "", codeHash);
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

BOOST_FIXTURE_TEST_SUITE(EthTxGasSettlementExecutor, EthTxGasSettlementExecutorFixture)

BOOST_AUTO_TEST_CASE(type4_emptyCall_receiptGasUsed_is21000)
{
    task::syncWait([this]() -> task::Task<void> {
        evmc_address sender{};
        sender.bytes[19] = 0xe1;
        evmc_address target{};
        target.bytes[19] = 0xe2;
        co_await fundSender(sender);
        co_await deployStopAt(target);

        Address toAddr{};
        std::memcpy(toAddr.data(), target.bytes, sizeof(target.bytes));
        auto tx = makeWeb3Type4Transaction(*cryptoSuite, {}, sender, toAddr, bytes{}, 0, 500'000);
        auto receipt = co_await executor.executeTransaction(
            storage, blockHeader, *tx, contextId++, ledgerConfig, false);

        BOOST_REQUIRE(receipt);
        BOOST_CHECK_EQUAL(receipt->status(), 0);
        BOOST_CHECK_EQUAL(receipt->gasUsed(), u256(gas::TX_BASE_GAS));
    }());
}

BOOST_AUTO_TEST_CASE(type4_intrinsicOOG_receiptGasUsed_cappedAtGasLimit)
{
    task::syncWait([this]() -> task::Task<void> {
        auto features = ledgerConfig.features();
        features.set(ledger::Features::Flag::bugfix_v1_exec_error_gas_used);
        ledgerConfig.setFeatures(features);

        evmc_address sender{};
        sender.bytes[19] = 0xe3;
        evmc_address target{};
        target.bytes[19] = 0xe4;
        co_await fundSender(sender);
        co_await deployStopAt(target);

        Address toAddr{};
        std::memcpy(toAddr.data(), target.bytes, sizeof(target.bytes));

        constexpr uint64_t lowGas = 20'000;
        ledgerConfig.setGasLimit({lowGas, 0});
        auto tx = makeWeb3Type4Transaction(*cryptoSuite, {}, sender, toAddr, bytes{}, 0, lowGas);
        auto receipt = co_await executor.executeTransaction(
            storage, blockHeader, *tx, contextId++, ledgerConfig, false);

        BOOST_REQUIRE(receipt);
        BOOST_CHECK_EQUAL(
            receipt->status(), static_cast<int32_t>(protocol::TransactionStatus::OutOfGas));
        BOOST_CHECK_EQUAL(receipt->gasUsed(), u256(lowGas));
    }());
}

BOOST_AUTO_TEST_CASE(type4_estimateCall_matchesExecute_gasUsed)
{
    task::syncWait([this]() -> task::Task<void> {
        evmc_address sender{};
        sender.bytes[19] = 0xe5;
        evmc_address target{};
        target.bytes[19] = 0xe6;
        co_await fundSender(sender);
        co_await deployStopAt(target);

        Address toAddr{};
        std::memcpy(toAddr.data(), target.bytes, sizeof(target.bytes));
        auto tx = makeWeb3Type4Transaction(*cryptoSuite, {}, sender, toAddr, bytes{}, 0, 500'000);

        auto execReceipt = co_await executor.executeTransaction(
            storage, blockHeader, *tx, contextId++, ledgerConfig, false);
        auto callReceipt = co_await executor.executeTransaction(
            storage, blockHeader, *tx, contextId++, ledgerConfig, true);

        BOOST_REQUIRE(execReceipt);
        BOOST_REQUIRE(callReceipt);
        BOOST_CHECK_EQUAL(execReceipt->status(), 0);
        BOOST_CHECK_EQUAL(callReceipt->status(), 0);
        BOOST_CHECK_EQUAL(execReceipt->gasUsed(), callReceipt->gasUsed());
        BOOST_CHECK_EQUAL(execReceipt->gasUsed(), u256(gas::TX_BASE_GAS));
    }());
}

BOOST_AUTO_TEST_CASE(type4_valueTransfer_receiptGasUsed_not42000)
{
    task::syncWait([this]() -> task::Task<void> {
        evmc_address sender{};
        sender.bytes[19] = 0xe7;
        evmc_address target{};
        target.bytes[19] = 0xe8;
        co_await fundSender(sender);
        co_await deployStopAt(target);

        Address toAddr{};
        std::memcpy(toAddr.data(), target.bytes, sizeof(target.bytes));

        auto tx = makeWeb3Type4Transaction(*cryptoSuite, {}, sender, toAddr, bytes{}, 0, 500'000);
        // Web3Transaction value is set via tars - use makeWeb3 with value by rebuilding
        bcos::rpc::Web3Transaction w3;
        w3.type = bcos::rpc::TransactionType::EIP7702;
        w3.chainId = 1;
        w3.nonce = 0;
        w3.maxPriorityFeePerGas = 1;
        w3.maxFeePerGas = 2;
        w3.gasLimit = 500'000;
        w3.to = toAddr;
        w3.value = 1;
        w3.data = {};
        w3.signatureR = bytes(32, 0x11);
        w3.signatureS = bytes(32, 0x22);
        w3.signatureV = 27;
        auto tarsHolder = std::make_shared<bcostars::Transaction>(w3.takeToTarsTransaction());
        auto const signBytes = w3.encodeForSign();
        tarsHolder->extraTransactionBytes.assign(signBytes.begin(), signBytes.end());
        auto const txHash = w3.hashForSign();
        tarsHolder->extraTransactionHash.assign(txHash.begin(), txHash.end());
        tarsHolder->sender.assign(sender.bytes, sender.bytes + sizeof(sender.bytes));
        auto valueTx = std::make_shared<bcostars::protocol::TransactionImpl>(
            [tarsHolder]() { return tarsHolder.get(); });

        auto receipt = co_await executor.executeTransaction(
            storage, blockHeader, *valueTx, contextId++, ledgerConfig, false);

        BOOST_REQUIRE(receipt);
        BOOST_CHECK_EQUAL(receipt->status(), 0);
        BOOST_CHECK_EQUAL(receipt->gasUsed(), u256(gas::TX_BASE_GAS));
        BOOST_CHECK_LT(receipt->gasUsed(), u256(2 * gas::TX_BASE_GAS));
    }());
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
