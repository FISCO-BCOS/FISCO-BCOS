/*
 * End-to-end transaction-executor tests for EIP-7623 TE gas settlement (spec §6.2).
 */
#include "../bcos-transaction-executor/TransactionExecutorImpl.h"
#include "TestMemoryStorage.h"
#include "bcos-executor/src/CallParameters.h"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/ledger/LedgerConfig.h"
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
#include <algorithm>
#include <intx/intx.hpp>

using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::executor_v1;
using bcos::ledger::account::EVMAccount;

namespace bcos::test
{

namespace
{
bytes mixedCalldata100()
{
    bytes mixed(100);
    std::fill(mixed.begin(), mixed.begin() + 50, 0x00);
    std::fill(mixed.begin() + 50, mixed.end(), 0x42);
    return mixed;
}

void setPragueFeatures(ledger::LedgerConfig& ledgerConfig)
{
    ledger::Features features;
    features.setGenesisFeatures(protocol::BlockVersion::MAX_VERSION);
    features.set(ledger::Features::Flag::feature_evm_cancun);
    features.set(ledger::Features::Flag::feature_evm_prague);
    features.set(ledger::Features::Flag::feature_evm_eip2929);
    features.set(ledger::Features::Flag::feature_balance);
    features.set(ledger::Features::Flag::feature_balance_policy1);
    ledgerConfig.setFeatures(features);
}

void setPragueFeaturesWithoutBalancePrecheck(ledger::LedgerConfig& ledgerConfig)
{
    ledger::Features features;
    features.set(ledger::Features::Flag::feature_evm_cancun);
    features.set(ledger::Features::Flag::feature_evm_prague);
    features.set(ledger::Features::Flag::feature_evm_eip2929);
    features.set(ledger::Features::Flag::feature_balance);
    features.set(ledger::Features::Flag::feature_balance_policy1);
    features.set(ledger::Features::Flag::bugfix_evm_exception_gas_used);
    ledgerConfig.setFeatures(features);
}

void setLedgerChainId(ledger::LedgerConfig& ledgerConfig, uint64_t chainId = 1)
{
    evmc_uint256be evmcChain{};
    intx::be::store(evmcChain.bytes, intx::uint256{chainId});
    ledgerConfig.setChainId(evmcChain);
}

void setPrePragueFeatures(ledger::LedgerConfig& ledgerConfig)
{
    ledger::Features features;
    features.setGenesisFeatures(protocol::BlockVersion::MAX_VERSION);
    features.set(ledger::Features::Flag::feature_evm_cancun);
    features.set(ledger::Features::Flag::feature_balance);
    features.set(ledger::Features::Flag::feature_balance_policy1);
    ledgerConfig.setFeatures(features);
}

std::shared_ptr<bcostars::protocol::TransactionImpl> makeWeb3Type2Transaction(
    evmc_address const& sender, bcos::Address const& to, bcos::bytes const& data, uint64_t gasLimit,
    u256 value = 0)
{
    bcos::rpc::Web3Transaction w3;
    w3.type = bcos::rpc::TransactionType::EIP1559;
    w3.chainId = 1;
    w3.nonce = 0;
    w3.maxPriorityFeePerGas = 1;
    w3.maxFeePerGas = 2;
    w3.gasLimit = gasLimit;
    w3.to = to;
    w3.value = value;
    w3.data = data;
    w3.signatureR = bcos::bytes(32, 0x11);
    w3.signatureS = bcos::bytes(32, 0x22);
    w3.signatureV = 27;

    auto tarsHolder = std::make_shared<bcostars::Transaction>(w3.takeToTarsTransaction());
    auto const signBytes = w3.encodeForSign();
    tarsHolder->extraTransactionBytes.assign(signBytes.begin(), signBytes.end());
    auto const txHash = w3.hashForSign();
    tarsHolder->extraTransactionHash.assign(txHash.begin(), txHash.end());
    tarsHolder->sender.assign(sender.bytes, sender.bytes + sizeof(sender.bytes));

    return std::make_shared<bcostars::protocol::TransactionImpl>(
        [tarsHolder]() { return tarsHolder.get(); });
}

std::shared_ptr<bcostars::protocol::TransactionImpl> makeWeb3Type2930Transaction(
    evmc_address const& sender, bcos::Address const& to, bcos::bytes const& data, uint64_t gasLimit,
    bcos::Address const& accessAddr, std::vector<h256> storageKeys)
{
    bcos::rpc::Web3Transaction w3;
    w3.type = bcos::rpc::TransactionType::EIP2930;
    w3.chainId = 1;
    w3.nonce = 0;
    w3.maxPriorityFeePerGas = 1;
    w3.maxFeePerGas = 1;
    w3.gasLimit = gasLimit;
    w3.to = to;
    w3.data = data;
    w3.signatureR = bcos::bytes(32, 0x11);
    w3.signatureS = bcos::bytes(32, 0x22);
    w3.signatureV = 27;
    bcos::rpc::AccessListEntry entry;
    entry.account = accessAddr;
    entry.storageKeys = std::move(storageKeys);
    w3.accessList.emplace_back(std::move(entry));

    auto tarsHolder = std::make_shared<bcostars::Transaction>(w3.takeToTarsTransaction());
    auto const signBytes = w3.encodeForSign();
    tarsHolder->extraTransactionBytes.assign(signBytes.begin(), signBytes.end());
    auto const txHash = w3.hashForSign();
    tarsHolder->extraTransactionHash.assign(txHash.begin(), txHash.end());
    tarsHolder->sender.assign(sender.bytes, sender.bytes + sizeof(sender.bytes));

    return std::make_shared<bcostars::protocol::TransactionImpl>(
        [tarsHolder]() { return tarsHolder.get(); });
}

std::shared_ptr<bcostars::protocol::TransactionImpl> makeWeb3Type2CreateTransaction(
    evmc_address const& sender, bcos::bytes const& initcode, uint64_t gasLimit, u256 value = 0)
{
    bcos::rpc::Web3Transaction w3;
    w3.type = bcos::rpc::TransactionType::EIP1559;
    w3.chainId = 1;
    w3.nonce = 0;
    w3.maxPriorityFeePerGas = 1;
    w3.maxFeePerGas = 2;
    w3.gasLimit = gasLimit;
    w3.to = std::nullopt;
    w3.value = value;
    w3.data = initcode;
    w3.signatureR = bcos::bytes(32, 0x11);
    w3.signatureS = bcos::bytes(32, 0x22);
    w3.signatureV = 27;

    auto tarsHolder = std::make_shared<bcostars::Transaction>(w3.takeToTarsTransaction());
    auto const signBytes = w3.encodeForSign();
    tarsHolder->extraTransactionBytes.assign(signBytes.begin(), signBytes.end());
    auto const txHash = w3.hashForSign();
    tarsHolder->extraTransactionHash.assign(txHash.begin(), txHash.end());
    tarsHolder->sender.assign(sender.bytes, sender.bytes + sizeof(sender.bytes));

    return std::make_shared<bcostars::protocol::TransactionImpl>(
        [tarsHolder]() { return tarsHolder.get(); });
}
}  // namespace

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

    task::Task<void> deployBytecode(evmc_address const& addr, bcos::bytes const& code)
    {
        EVMAccount<decltype(storage)> account(storage, addr, false);
        if (!co_await account.exists())
        {
            co_await account.create();
        }
        auto const codeHash = cryptoSuite->hashImpl()->hash(ref(code));
        co_await account.setCode(code, "", codeHash);
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

BOOST_AUTO_TEST_CASE(type2_emptyCall_receiptGasUsed_is21000)
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
        auto tx = makeWeb3Type2Transaction(sender, toAddr, bytes{}, 500'000);
        auto receipt = co_await executor.executeTransaction(
            storage, blockHeader, *tx, contextId++, ledgerConfig, false);

        BOOST_REQUIRE(receipt);
        BOOST_CHECK_EQUAL(receipt->status(), 0);
        BOOST_CHECK_EQUAL(receipt->gasUsed(), u256(gas::TX_BASE_GAS));
    }());
}

BOOST_AUTO_TEST_CASE(type2_intrinsicOOG_receiptGasUsed_cappedAtGasLimit)
{
    task::syncWait([this]() -> task::Task<void> {
        auto features = ledgerConfig.features();
        features.set(ledger::Features::Flag::bugfix_evm_exception_gas_used);
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
        auto tx = makeWeb3Type2Transaction(sender, toAddr, bytes{}, lowGas);
        auto receipt = co_await executor.executeTransaction(
            storage, blockHeader, *tx, contextId++, ledgerConfig, false);

        BOOST_REQUIRE(receipt);
        BOOST_CHECK_EQUAL(
            receipt->status(), static_cast<int32_t>(protocol::TransactionStatus::OutOfGas));
        BOOST_CHECK_EQUAL(receipt->gasUsed(), u256(lowGas));
    }());
}

BOOST_AUTO_TEST_CASE(type2_estimateCall_matchesExecute_gasUsed)
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
        auto tx = makeWeb3Type2Transaction(sender, toAddr, bytes{}, 500'000);

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

BOOST_AUTO_TEST_CASE(type2_mixedCalldata_estimateCall_matchesExecute_floorGasUsed)
{
    task::syncWait([this]() -> task::Task<void> {
        evmc_address sender{};
        sender.bytes[19] = 0xef;
        evmc_address target{};
        target.bytes[19] = 0xf0;
        co_await fundSender(sender);
        co_await deployStopAt(target);

        Address toAddr{};
        std::memcpy(toAddr.data(), target.bytes, sizeof(target.bytes));
        auto const data = mixedCalldata100();
        constexpr int64_t expectedGasUsed = gas::TX_BASE_GAS + 2500;

        auto tx =
            makeWeb3Type2Transaction(sender, toAddr, data, static_cast<uint64_t>(expectedGasUsed));

        auto execReceipt = co_await executor.executeTransaction(
            storage, blockHeader, *tx, contextId++, ledgerConfig, false);
        auto callReceipt = co_await executor.executeTransaction(
            storage, blockHeader, *tx, contextId++, ledgerConfig, true);

        BOOST_REQUIRE(execReceipt);
        BOOST_REQUIRE(callReceipt);
        BOOST_CHECK_EQUAL(execReceipt->status(), 0);
        BOOST_CHECK_EQUAL(callReceipt->status(), 0);
        BOOST_CHECK_EQUAL(execReceipt->gasUsed(), u256(expectedGasUsed));
        BOOST_CHECK_EQUAL(callReceipt->gasUsed(), execReceipt->gasUsed());
    }());
}

BOOST_AUTO_TEST_CASE(type2_mixedCalldata_floorDominatesReceiptGasUsed)
{
    task::syncWait([this]() -> task::Task<void> {
        evmc_address sender{};
        sender.bytes[19] = 0xe9;
        evmc_address target{};
        target.bytes[19] = 0xea;
        co_await fundSender(sender);
        co_await deployStopAt(target);

        bcos::Address toAddr{};
        std::memcpy(toAddr.data(), target.bytes, sizeof(target.bytes));
        auto const data = mixedCalldata100();

        evmc_message msg{};
        msg.kind = EVMC_CALL;
        msg.input_data = data.data();
        msg.input_size = data.size();
        auto const intrinsic = gas::computeTxIntrinsicGas(msg, nullptr, 2);
        constexpr int64_t expectedGasUsed = gas::TX_BASE_GAS + 2500;
        BOOST_CHECK_EQUAL(intrinsic.gasLimitMinimum(), expectedGasUsed);
        BOOST_CHECK_EQUAL(intrinsic.preExecutionDebit(), gas::TX_BASE_GAS + 1000);

        auto tx =
            makeWeb3Type2Transaction(sender, toAddr, data, static_cast<uint64_t>(expectedGasUsed));
        auto receipt = co_await executor.executeTransaction(
            storage, blockHeader, *tx, contextId++, ledgerConfig, false);

        BOOST_REQUIRE(receipt);
        BOOST_CHECK_EQUAL(receipt->status(), 0);
        BOOST_CHECK_EQUAL(receipt->gasUsed(), u256(expectedGasUsed));
    }());
}

BOOST_AUTO_TEST_CASE(type2_mixedCalldata_gasLimitAtFloor_executesSuccessfully)
{
    task::syncWait([this]() -> task::Task<void> {
        evmc_address sender{};
        sender.bytes[19] = 0xeb;
        evmc_address target{};
        target.bytes[19] = 0xec;
        co_await fundSender(sender);
        co_await deployStopAt(target);

        Address toAddr{};
        std::memcpy(toAddr.data(), target.bytes, sizeof(target.bytes));
        auto const data = mixedCalldata100();

        evmc_message msg{};
        msg.kind = EVMC_CALL;
        msg.input_data = data.data();
        msg.input_size = data.size();
        auto const intrinsic = gas::computeTxIntrinsicGas(msg, nullptr, 2);
        constexpr int64_t expectedGasUsed = gas::TX_BASE_GAS + 2500;
        BOOST_REQUIRE_EQUAL(intrinsic.gasLimitMinimum(), expectedGasUsed);

        auto tx =
            makeWeb3Type2Transaction(sender, toAddr, data, static_cast<uint64_t>(expectedGasUsed));
        auto receipt = co_await executor.executeTransaction(
            storage, blockHeader, *tx, contextId++, ledgerConfig, false);

        BOOST_REQUIRE(receipt);
        BOOST_CHECK_EQUAL(receipt->status(), 0);
        BOOST_CHECK_EQUAL(receipt->gasUsed(), u256(expectedGasUsed));
    }());
}

BOOST_AUTO_TEST_CASE(type2_mixedCalldata_precheckOff_floorDominatesReceiptGasUsed)
{
    task::syncWait([this]() -> task::Task<void> {
        evmc_address sender{};
        sender.bytes[19] = 0xed;
        evmc_address target{};
        target.bytes[19] = 0xee;
        co_await fundSender(sender);
        co_await deployStopAt(target);

        ledger::LedgerConfig legacyConfig;
        setPragueFeaturesWithoutBalancePrecheck(legacyConfig);
        setLedgerChainId(legacyConfig, 1);
        legacyConfig.setGasPrice({"1", 0});
        legacyConfig.setBalanceTransfer(true);

        Address toAddr{};
        std::memcpy(toAddr.data(), target.bytes, sizeof(target.bytes));
        auto const data = mixedCalldata100();

        evmc_message msg{};
        msg.kind = EVMC_CALL;
        msg.input_data = data.data();
        msg.input_size = data.size();
        auto const intrinsic = gas::computeTxIntrinsicGas(msg, nullptr, 2);
        constexpr int64_t expectedGasUsed = gas::TX_BASE_GAS + 2500;
        BOOST_CHECK_EQUAL(intrinsic.gasLimitMinimum(), expectedGasUsed);

        auto tx =
            makeWeb3Type2Transaction(sender, toAddr, data, static_cast<uint64_t>(expectedGasUsed));
        auto receipt = co_await executor.executeTransaction(
            storage, blockHeader, *tx, contextId++, legacyConfig, false);

        BOOST_REQUIRE(receipt);
        BOOST_CHECK_EQUAL(receipt->status(), 0);
        BOOST_CHECK_EQUAL(receipt->gasUsed(), u256(expectedGasUsed));
    }());
}

BOOST_AUTO_TEST_CASE(type2_valueTransfer_receiptGasUsed_not42000)
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

        auto valueTx = makeWeb3Type2Transaction(sender, toAddr, bytes{}, 500'000, 1);

        auto receipt = co_await executor.executeTransaction(
            storage, blockHeader, *valueTx, contextId++, ledgerConfig, false);

        BOOST_REQUIRE(receipt);
        BOOST_CHECK_EQUAL(receipt->status(), 0);
        BOOST_CHECK_EQUAL(receipt->gasUsed(), u256(gas::TX_BASE_GAS));
        BOOST_CHECK_LT(receipt->gasUsed(), u256(2 * gas::TX_BASE_GAS));
    }());
}

BOOST_AUTO_TEST_CASE(type2_accessList_floorDominatesReceiptGasUsed)
{
    task::syncWait([this]() -> task::Task<void> {
        evmc_address sender{};
        sender.bytes[19] = 0xf1;
        evmc_address target{};
        target.bytes[19] = 0xf2;
        co_await fundSender(sender);
        co_await deployStopAt(target);

        Address toAddr{};
        std::memcpy(toAddr.data(), target.bytes, sizeof(target.bytes));
        Address accessAddr{};
        accessAddr[19] = 0xaa;

        bcos::bytes const data = bcos::asBytes("x");
        executor::Eip2930AccessList list{{accessAddr, {h256(0x01), h256(0x02)}}};
        evmc_message msg{};
        msg.kind = EVMC_CALL;
        msg.input_data = data.data();
        msg.input_size = data.size();
        auto const intrinsic = gas::computeTxIntrinsicGas(msg, std::addressof(list), 1);
        constexpr int64_t accessListCost =
            gas::ACCESS_LIST_ADDRESS_COST + 2 * gas::ACCESS_LIST_STORAGE_KEY_COST;
        constexpr int64_t gethMinGasLimit = gas::TX_BASE_GAS + accessListCost + 16;
        BOOST_CHECK_EQUAL(intrinsic.gasLimitMinimum(), gethMinGasLimit);
        // geth receipt matches admission min for light calldata + access list (no floor top-up).
        constexpr int64_t expectedReceiptGasUsed = gethMinGasLimit;

        auto tx = makeWeb3Type2930Transaction(sender, toAddr, data,
            static_cast<uint64_t>(expectedReceiptGasUsed), accessAddr, {h256(0x01), h256(0x02)});
        auto receipt = co_await executor.executeTransaction(
            storage, blockHeader, *tx, contextId++, ledgerConfig, false);

        BOOST_REQUIRE(receipt);
        BOOST_CHECK_EQUAL(receipt->status(), 0);
        BOOST_CHECK_EQUAL(receipt->gasUsed(), u256(expectedReceiptGasUsed));
    }());
}

BOOST_AUTO_TEST_CASE(type2_contractCreate_floorDominatesReceiptGasUsed)
{
    task::syncWait([this]() -> task::Task<void> {
        evmc_address sender{};
        sender.bytes[19] = 0xf3;
        co_await fundSender(sender);

        // Minimal initcode: deploy runtime bytecode 0x00 (STOP).
        bcos::bytes const initcode{0x60, 0x00, 0x60, 0x00, 0x52, 0x60, 0x01, 0x60, 0x00, 0xf3};
        evmc_message msg{};
        msg.kind = EVMC_CREATE;
        msg.input_data = initcode.data();
        msg.input_size = initcode.size();
        auto const intrinsic = gas::computeTxIntrinsicGas(msg, nullptr, 2);
        auto const expectedGasUsed = intrinsic.gasLimitMinimum();
        BOOST_REQUIRE_GT(expectedGasUsed, gas::TX_BASE_GAS + gas::CREATE_BASE_GAS);

        auto tx = makeWeb3Type2CreateTransaction(
            sender, initcode, static_cast<uint64_t>(expectedGasUsed));
        auto receipt = co_await executor.executeTransaction(
            storage, blockHeader, *tx, contextId++, ledgerConfig, false);

        BOOST_REQUIRE(receipt);
        BOOST_CHECK_EQUAL(receipt->status(), 0);
        BOOST_CHECK_EQUAL(receipt->gasUsed(), u256(expectedGasUsed));
        BOOST_CHECK(!receipt->contractAddress().empty());
    }());
}

BOOST_AUTO_TEST_CASE(type2_mixedCalldata_postEvmOOG_receiptGasUsed_isGasLimit)
{
    task::syncWait([this]() -> task::Task<void> {
        evmc_address sender{};
        sender.bytes[19] = 0xf7;
        evmc_address target{};
        target.bytes[19] = 0xf8;
        co_await fundSender(sender);
        // JUMPDEST PUSH0 PUSH0 JUMP — burns remaining EVM gas until OOG.
        bcos::bytes const loopCode{0x5b, 0x60, 0x00, 0x60, 0x00, 0x56};
        co_await deployBytecode(target, loopCode);

        Address toAddr{};
        std::memcpy(toAddr.data(), target.bytes, sizeof(target.bytes));
        auto const data = mixedCalldata100();
        constexpr int64_t expectedGasUsed = gas::TX_BASE_GAS + 2500;

        auto tx =
            makeWeb3Type2Transaction(sender, toAddr, data, static_cast<uint64_t>(expectedGasUsed));
        auto receipt = co_await executor.executeTransaction(
            storage, blockHeader, *tx, contextId++, ledgerConfig, false);

        BOOST_REQUIRE(receipt);
        BOOST_CHECK_EQUAL(
            receipt->status(), static_cast<int32_t>(protocol::TransactionStatus::OutOfGas));
        BOOST_CHECK_EQUAL(receipt->gasUsed(), u256(expectedGasUsed));
    }());
}

BOOST_AUTO_TEST_CASE(prePrague_mixedCalldata_skips7623Floor)
{
    task::syncWait([this]() -> task::Task<void> {
        evmc_address sender{};
        sender.bytes[19] = 0xf5;
        evmc_address target{};
        target.bytes[19] = 0xf6;
        co_await fundSender(sender);
        co_await deployStopAt(target);

        ledger::LedgerConfig prePragueConfig;
        setPrePragueFeatures(prePragueConfig);
        setLedgerChainId(prePragueConfig, 1);
        prePragueConfig.setGasPrice({"1", 0});
        prePragueConfig.setBalanceTransfer(true);

        Address toAddr{};
        std::memcpy(toAddr.data(), target.bytes, sizeof(target.bytes));
        auto const data = mixedCalldata100();

        evmc_message msg{};
        msg.kind = EVMC_CALL;
        msg.input_data = data.data();
        msg.input_size = data.size();
        auto const intrinsic = gas::computeTxIntrinsicGas(msg, nullptr, 2);

        auto tx = makeWeb3Type2Transaction(sender, toAddr, data, 500'000);
        auto receipt = co_await executor.executeTransaction(
            storage, blockHeader, *tx, contextId++, prePragueConfig, false);

        BOOST_REQUIRE(receipt);
        BOOST_CHECK_EQUAL(receipt->status(), 0);
        // Prague off: EIP-7623 floor is not applied; receipt stays at legacy base gas.
        BOOST_CHECK_EQUAL(receipt->gasUsed(), u256(gas::TX_BASE_GAS));
        BOOST_CHECK_LT(receipt->gasUsed(), u256(intrinsic.gasLimitMinimum()));
    }());
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
