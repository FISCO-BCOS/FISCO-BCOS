// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// OpstackExecutorTest.cpp — drives OpstackExecutor over BCOS storage. Ported from the v1
// (feat-op-validator-loop) test and adapted to the v2 OP API: opTransition/runDeposit now produce
// the final FISCO receipt directly (OP metadata + effective gas price already projected), and the
// state diff is returned via an out-param. Storage is a plain MutableStorage.

#include "opstack-executor/OpstackExecutor.h"
#include "bcos-evm/opstack/OpForkSchedule.h"
#include "bcos-evm/opstack/OpPredeploys.h"
#include <bcos-codec/rlp/Common.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-framework/ledger/EVMAccount.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-rpc/web3jsonrpc/model/Web3Transaction.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <evmc/evmc.h>
#include <gtest/gtest.h>
#include <evmc/evmc.hpp>
#include <memory>

using namespace bcos;
using namespace bcos::executor_v1::opstack;
using namespace evmc::literals;  // _address / _bytes32

namespace
{
using bcos::executor_v1::StateKey;
using bcos::executor_v1::StateValue;
namespace memory_storage = bcos::storage2::memory_storage;

using MutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;

bcos::crypto::CryptoSuite::Ptr makeCryptoSuite()
{
    return std::make_shared<bcos::crypto::CryptoSuite>(
        std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr);
}

struct Fixture : public ::testing::Test
{
    std::shared_ptr<crypto::CryptoSuite> cryptoSuite = std::make_shared<crypto::CryptoSuite>(
        std::make_shared<crypto::Keccak256>(), nullptr, nullptr);
    bcos::protocol::TransactionReceiptFactory::Ptr receiptFactory{
        std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(cryptoSuite)};
    MutableStorage storage;
    ledger::LedgerConfig ledgerConfig;

    const bcos::evm::opstack::OpForkConfig& fork = bcos::evm::opstack::jovianConfig();

    Fixture()
    {
        ledgerConfig.setEVMCRevision(fork.rev);
        ledgerConfig.setGasLimit({30000000, 0});
        ledgerConfig.setGasPrice({"0x0", 0});
    }

    bcostars::protocol::TransactionImpl buildWeb3Tx()
    {
        // Construct a minimal EIP-2930 Web3 tx directly (chainId=5, nonce=7, gasPrice, gasLimit,
        // to, value, empty data/accessList, yParity=0, 32-byte r/s) — the v1 raw-RLP fixture no
        // longer decodes under the v2 Web3Transaction path, so build fields instead of RLP.
        bcos::rpc::Web3Transaction w3{};
        w3.type = bcos::rpc::TransactionType::EIP2930;
        w3.chainId = 5;
        w3.nonce = 7;
        w3.maxFeePerGas = bcos::u256(30000000000);  // gasPrice (EIP-2930)
        w3.maxPriorityFeePerGas = w3.maxFeePerGas;
        w3.gasLimit = 5000000;
        w3.to = bcos::Address("0x811a752c8cd697e3cb27279c330ed1ada745a8d7");
        w3.value = bcos::u256(2000000000000000000);  // 2 ETH
        w3.signatureV = 0;                            // yParity
        w3.signatureR = bcos::bytes(32, 0x01);        // dummy r/s (unused: evmone skips sig verify)
        w3.signatureS = bcos::bytes(32, 0x02);
        auto tarsHolder = std::make_shared<bcostars::Transaction>(w3.takeToTarsTransaction());
        auto const txHash = w3.txHash();
        tarsHolder->extraTransactionHash.assign(txHash.begin(), txHash.end());
        return bcostars::protocol::TransactionImpl([tarsHolder]() { return tarsHolder.get(); });
    }

    template <class T>
    static T sync(task::Task<T> t)
    {
        return task::syncWait(std::move(t));
    }
};
}  // namespace

TEST(OpstackExecutor, ConstructsWithJovianFork)
{
    auto rf = std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(makeCryptoSuite());
    OpstackExecutor executor(rf, makeCryptoSuite()->hashImpl());
    EXPECT_NE(&executor.vm(), nullptr);
}

TEST_F(Fixture, ExecutesNormalTransferEndToEnd)
{
    OpstackExecutor executor{receiptFactory, cryptoSuite->hashImpl(), fork};

    bcostars::protocol::BlockHeaderImpl blockHeader;
    blockHeader.setNumber(1);
    blockHeader.calculateHash(*cryptoSuite->hashImpl());

    auto tx = buildWeb3Tx();
    constexpr auto sender = 0xe0e794ca86d198042b64285c5ce667aee747509b_address;
    tx.clearSenderAndHash();
    tx.forceSender(bcos::bytes(sender.bytes, sender.bytes + sizeof(sender.bytes)));

    task::syncWait([&]() -> task::Task<void> {
        ledger::account::EVMAccount<MutableStorage> acc(storage, sender, false);
        co_await acc.create();
        co_await acc.setBalance(u256("100000000000000000000"));
        // EIP-3607: evmone validate_transaction rejects a sender whose code_hash != EMPTY_CODE_HASH.
        // Seed the canonical empty-code hash so the sender is recognised as an EOA.
        co_await acc.setCode({}, "",
            bcos::crypto::HashType(
                "c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470"));
        std::string nonceStr(tx.nonce());
        if (nonceStr.empty())
            nonceStr = "0";
        co_await acc.setNonce(nonceStr);
        co_return;
    }());

    bcos::evm::opstack::OpFeeParams fee{};  // zero fee
    auto receipt = task::syncWait(executor.executeTransaction(storage, blockHeader, tx,
        /*contextID=*/0, ledgerConfig, /*call=*/false, fee, /*blockGasLeft=*/30000000,
        /*chainId=*/10));
    ASSERT_NE(receipt, nullptr);
    // FISCO internal convention: 0 = success (the Ethereum RPC 0<->1 flip happens later).
    EXPECT_EQ(receipt->status(), 0);
    EXPECT_GE(receipt->gasUsed(), 21000u);
    EXPECT_EQ(receipt->blockNumber(), 1);
    EXPECT_TRUE(receipt->opStackMeta().has_value());
}

TEST_F(Fixture, RejectsForkRevisionMismatch)
{
    OpstackExecutor executor{receiptFactory, cryptoSuite->hashImpl(), fork};
    ledgerConfig.setEVMCRevision(EVMC_FRONTIER);  // deliberately != fork.rev
    bcostars::protocol::BlockHeaderImpl blockHeader;
    blockHeader.setNumber(1);
    auto tx = buildWeb3Tx();
    bcos::evm::opstack::OpFeeParams fee{};
    EXPECT_THROW(task::syncWait(executor.executeTransaction(
                     storage, blockHeader, tx, 0, ledgerConfig, false, fee, 30000000, 10)),
        bcos::executor_v1::opstack::OpForkRevisionMismatch);
}

TEST_F(Fixture, RejectsInvalidTx)
{
    // Balance 0 sender + value transfer -> insufficient balance -> OpTxValidationFailed.
    OpstackExecutor executor{receiptFactory, cryptoSuite->hashImpl(), fork};
    bcostars::protocol::BlockHeaderImpl blockHeader;
    blockHeader.setNumber(1);
    auto tx = buildWeb3Tx();
    constexpr auto sender = 0xe0e794ca86d198042b64285c5ce667aee747509b_address;
    tx.clearSenderAndHash();
    tx.forceSender(bcos::bytes(sender.bytes, sender.bytes + sizeof(sender.bytes)));
    // No account created -> balance 0 -> validation fails.
    bcos::evm::opstack::OpFeeParams fee{};
    EXPECT_THROW(task::syncWait(executor.executeTransaction(
                     storage, blockHeader, tx, 0, ledgerConfig, false, fee, 30000000, 10)),
        bcos::executor_v1::opstack::OpTxValidationFailed);
}

TEST_F(Fixture, ChargesL1AndOperatorFees)
{
    // Non-zero L1 + operator fee params route through opValidate (pre-charge) -> opTransition
    // (deriveOpReceiptMeta) and surface in the receipt's opStackMeta.
    OpstackExecutor executor{receiptFactory, cryptoSuite->hashImpl(), fork};
    bcostars::protocol::BlockHeaderImpl blockHeader;
    blockHeader.setNumber(1);
    blockHeader.calculateHash(*cryptoSuite->hashImpl());

    auto tx = buildWeb3Tx();
    constexpr auto sender = 0xe0e794ca86d198042b64285c5ce667aee747509b_address;
    tx.clearSenderAndHash();
    tx.forceSender(bcos::bytes(sender.bytes, sender.bytes + sizeof(sender.bytes)));
    task::syncWait([&]() -> task::Task<void> {
        ledger::account::EVMAccount<MutableStorage> acc(storage, sender, false);
        co_await acc.create();
        co_await acc.setBalance(u256("100000000000000000000"));
        co_await acc.setCode({}, "",
            bcos::crypto::HashType(
                "c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470"));
        std::string nonceStr(tx.nonce());
        if (nonceStr.empty())
            nonceStr = "0";
        co_await acc.setNonce(nonceStr);
        co_return;
    }());

    bcos::evm::opstack::OpFeeParams fee{};
    fee.l1_base_fee = 1000;
    fee.base_fee_scalar = 7;
    fee.blob_base_fee = 2000;
    fee.blob_base_fee_scalar = 9;
    fee.operator_fee_scalar = 11;
    fee.operator_fee_constant = 13;

    auto receipt = task::syncWait(executor.executeTransaction(storage, blockHeader, tx,
        /*contextID=*/0, ledgerConfig, /*call=*/false, fee, /*blockGasLeft=*/30000000,
        /*chainId=*/10));
    ASSERT_NE(receipt, nullptr);
    EXPECT_EQ(receipt->status(), 0);

    auto meta = receipt->opStackMeta();
    ASSERT_TRUE(meta.has_value());
    // Jovian derives these unconditionally.
    EXPECT_TRUE(meta->l1_fee.has_value());
    EXPECT_TRUE(meta->l1_gas_price.has_value());
    // operator_fee_scalar filled when has_operator_fee && (scalar != 0 || constant != 0).
    EXPECT_TRUE(meta->operator_fee_scalar.has_value());
    EXPECT_EQ(*meta->operator_fee_scalar, 11u);
}

TEST_F(Fixture, ReceiptMetaSurvives)
{
    OpstackExecutor executor{receiptFactory, cryptoSuite->hashImpl(), fork};
    bcostars::protocol::BlockHeaderImpl blockHeader;
    blockHeader.setNumber(1);

    auto tx = buildWeb3Tx();
    constexpr auto sender = 0xe0e794ca86d198042b64285c5ce667aee747509b_address;
    tx.clearSenderAndHash();
    tx.forceSender(bcos::bytes(sender.bytes, sender.bytes + sizeof(sender.bytes)));
    task::syncWait([&]() -> task::Task<void> {
        ledger::account::EVMAccount<MutableStorage> acc(storage, sender, false);
        co_await acc.create();
        co_await acc.setBalance(u256("100000000000000000000"));
        co_await acc.setCode({}, "",
            bcos::crypto::HashType(
                "c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470"));
        std::string nonceStr(tx.nonce());
        if (nonceStr.empty())
            nonceStr = "0";
        co_await acc.setNonce(nonceStr);
        co_return;
    }());

    bcos::evm::opstack::OpFeeParams fee{};
    fee.l1_base_fee = 1000;
    fee.base_fee_scalar = 7;

    auto receipt = task::syncWait(executor.executeTransaction(storage, blockHeader, tx,
        /*contextID=*/0, ledgerConfig, /*call=*/false, fee, /*blockGasLeft=*/30000000,
        /*chainId=*/10));
    ASSERT_NE(receipt, nullptr);
    auto meta = receipt->opStackMeta();
    ASSERT_TRUE(meta.has_value());
    ASSERT_TRUE(meta->l1_gas_price.has_value());
    EXPECT_EQ(*meta->l1_gas_price, bcos::u256(1000));
    // v2 deriveOpReceiptMeta derives l1_base_fee_scalar from props.fee (not l1_gas_used, which the
    // v2 implementation no longer fills); the fee below set base_fee_scalar=7.
    ASSERT_TRUE(meta->l1_base_fee_scalar.has_value());
    EXPECT_EQ(*meta->l1_base_fee_scalar, 7u);
    // effective_gas_price = base_fee(0) + min(maxPriority, maxFee-0); for this EIP-2930 tx
    // BCOS2Evmone.h maps max_gas_price = max_priority_gas_price = gasPrice, so effective =
    // gasPrice = 0x6fc23ac00.
    EXPECT_EQ(receipt->effectiveGasPrice(), "0x6fc23ac00");
}

// ---- executeDeposit + finalizeBlock (reuse bcos-evm/opstack runDeposit / finalizeOpBlock) ----

TEST_F(Fixture, ExecutesDepositMint)
{
    OpstackExecutor executor{receiptFactory, cryptoSuite->hashImpl(), fork};
    bcostars::protocol::BlockHeaderImpl blockHeader;
    blockHeader.setNumber(1);
    blockHeader.calculateHash(*cryptoSuite->hashImpl());

    constexpr auto kFrom = 0x000000000000000000000000000000000000dead_address;
    bcos::evm::opstack::DepositTx dep{
        .source_hash = 0x01_bytes32,
        .from = kFrom,
        .to = std::nullopt,  // contract creation
        .mint = intx::uint256{5},
        .value = intx::uint256{0},
        .gas_limit = 100000,
        .is_system_tx = false,
        .data = {},
    };
    task::syncWait([&]() -> task::Task<void> {
        ledger::account::EVMAccount<MutableStorage> acc(storage, kFrom, false);
        co_await acc.create();
        co_await acc.setBalance(u256(0));
        co_await acc.setNonce("0");
        co_return;
    }());

    auto receipt = task::syncWait(executor.executeDeposit(
        storage, blockHeader, dep, /*chainId=*/10, /*blockGasLeft=*/30000000, ledgerConfig));
    ASSERT_NE(receipt, nullptr);
    EXPECT_EQ(receipt->status(), 0);
    // mint(5) added to the from balance.
    ledger::account::EVMAccount<MutableStorage> acc(storage, kFrom, false);
    auto bal = task::syncWait(acc.balance());
    EXPECT_EQ(bal, 5u);
    // deposit_nonce == 0, version == 1 (Canyon+).
    auto meta = receipt->opStackMeta();
    ASSERT_TRUE(meta.has_value());
    ASSERT_TRUE(meta->deposit_nonce.has_value());
    EXPECT_EQ(*meta->deposit_nonce, 0u);
    ASSERT_TRUE(meta->deposit_receipt_version.has_value());
    EXPECT_EQ(*meta->deposit_receipt_version, 1u);
}

TEST_F(Fixture, DepositGasLimitReachedIsBlockError)
{
    OpstackExecutor executor{receiptFactory, cryptoSuite->hashImpl(), fork};
    bcostars::protocol::BlockHeaderImpl blockHeader;
    blockHeader.setNumber(1);
    bcos::evm::opstack::DepositTx dep{
        .source_hash = 0x02_bytes32,
        .from = 0x000000000000000000000000000000000000dead_address,
        .to = 0x0000000000000000000000000000000000000001_address,
        .mint = std::nullopt,
        .value = intx::uint256{0},
        .gas_limit = 100000,
        .is_system_tx = false,
        .data = {},
    };
    // gas_limit(100000) > blockGasLeft(100) -> runDeposit throws std::runtime_error.
    EXPECT_THROW(task::syncWait(executor.executeDeposit(storage, blockHeader, dep, /*chainId=*/10,
                     /*blockGasLeft=*/100, ledgerConfig)),
        std::runtime_error);
}

TEST_F(Fixture, FinalizeOpBlockNoReward)
{
    OpstackExecutor executor{receiptFactory, cryptoSuite->hashImpl(), fork};
    bcostars::protocol::BlockHeaderImpl blockHeader;
    blockHeader.setNumber(1);
    blockHeader.setCoinbase(bcos::Address(bcos::bytes(20, 0x11)));
    blockHeader.calculateHash(*cryptoSuite->hashImpl());

    constexpr auto kCoinbase = 0x1111111111111111111111111111111111111111_address;
    task::syncWait([&]() -> task::Task<void> {
        ledger::account::EVMAccount<MutableStorage> acc(storage, kCoinbase, false);
        co_await acc.create();
        co_await acc.setBalance(u256(100));
        co_return;
    }());

    task::syncWait(executor.finalizeBlock(storage, blockHeader, ledgerConfig));

    // OP has no block reward: coinbase balance unchanged.
    ledger::account::EVMAccount<MutableStorage> acc(storage, kCoinbase, false);
    auto bal = task::syncWait(acc.balance());
    EXPECT_EQ(bal, 100u);
}
