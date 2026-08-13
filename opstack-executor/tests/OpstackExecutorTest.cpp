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

    bcostars::protocol::TransactionImpl buildWeb3Tx(uint64_t chainId = 10)
    {
        // Construct a minimal EIP-2930 Web3 tx directly (nonce=7, gasPrice, gasLimit, to, value,
        // empty data/accessList, yParity=0, 32-byte r/s) — the v1 raw-RLP fixture no longer
        // decodes under the v2 Web3Transaction path, so build fields instead of RLP. chainId
        // defaults to 10 to match every executeTransaction call site below: Task 2 makes the
        // envelope's chain id binding (a mismatch is rejected in m_prepare before opValidate),
        // so the fixture envelope must carry the same chain id the call passes.
        bcos::rpc::Web3Transaction w3{};
        w3.type = bcos::rpc::TransactionType::EIP2930;
        w3.chainId = chainId;
        w3.nonce = 7;
        w3.maxFeePerGas = bcos::u256(30000000000);  // gasPrice (EIP-2930)
        w3.maxPriorityFeePerGas = w3.maxFeePerGas;
        w3.gasLimit = 5000000;
        w3.to = bcos::Address("0x811a752c8cd697e3cb27279c330ed1ada745a8d7");
        w3.value = bcos::u256(2000000000000000000);  // 2 ETH
        w3.signatureV = 0;                           // yParity
        w3.signatureR = bcos::bytes(32, 0x01);       // dummy r/s (unused: evmone skips sig verify)
        w3.signatureS = bcos::bytes(32, 0x02);
        auto tarsHolder = std::make_shared<bcostars::Transaction>(w3.takeToTarsTransaction());
        auto const txHash = w3.txHash();
        tarsHolder->extraTransactionHash.assign(txHash.begin(), txHash.end());
        // takeToTarsTransaction stores the signing preimage (Web3Transaction.cpp:220-223);
        // overwrite with the full EIP-2718 envelope (mirroring EngineServiceImpl buildOpBlock's
        // SEV-8 overwrite) so m_prepare's validateEnvelopeSignature sees canonical wire bytes.
        auto const envelope = w3.encode();
        tarsHolder->extraTransactionBytes.assign(envelope.begin(), envelope.end());
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
    auto rf =
        std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(makeCryptoSuite());
    OpstackExecutor executor(rf, makeCryptoSuite()->hashImpl());
    EXPECT_NE(&executor.vm(), nullptr);
}

TEST(OpstackExecutor, BuildOpBlockInfoMirrorsBlockPath)
{
    // buildBlockInfo must mirror toBlockInfo's field mapping (OpRlpDecode.h:106-121) so eth_call
    // sees the same block context as block execution: seconds timestamp, header baseFee (via
    // value_or(0), so optional-less test headers do not throw), and the full field set.
    auto h = std::make_shared<bcostars::protocol::BlockHeaderImpl>();
    h->setNumber(7);
    h->setTimestamp(1'234'567'890);  // ms; block path divides by 1000
    h->setCoinbase(bcos::Address{"0x00000000000000000000000000000000000000aa"});
    h->setBaseFee(bcos::u256(1'000'000'000));
    h->setPrevRandao(
        bcos::h256{"0xab00000000000000000000000000000000000000000000000000000000000000"});
    h->setParentBeaconBlockRoot(
        bcos::h256{"0xcd00000000000000000000000000000000000000000000000000000000000000"});
    h->setExtraData(bcos::bytes{0xde, 0xad});
    h->setBlobGasUsed(bcos::u256(0x1234));

    const auto blk = bcos::executor_v1::opstack::OpstackExecutor::buildBlockInfo(*h, 30'000'000);
    EXPECT_EQ(blk.number, 7);
    EXPECT_EQ(blk.timestamp, 1'234'567ULL);                 // ms -> s
    EXPECT_EQ(blk.gas_limit, 30'000'000);                   // injected gasLimit
    EXPECT_EQ(blk.base_fee, intx::uint256(1'000'000'000));  // header baseFee, NOT 0
    EXPECT_EQ(blk.prev_randao.bytes[0], 0xab);              // full field set
    EXPECT_EQ(blk.parent_beacon_block_root.bytes[0], 0xcd);
    EXPECT_EQ(blk.extra_data, (evmc::bytes{0xde, 0xad}));
    EXPECT_EQ(blk.blob_gas_used, 0x1234ULL);
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
        // EIP-3607: evmone validate_transaction rejects a sender whose code_hash !=
        // EMPTY_CODE_HASH. Seed the canonical empty-code hash so the sender is recognised as an
        // EOA.
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

TEST_F(Fixture, RejectsCrossChainEnvelopeUnlessCall)
{
    // Task 2: m_prepare runs validateEnvelopeSignature (chain-id binding, EIP-2 low-s, yParity<=1)
    // before opValidate for call=false. A cross-chain envelope (chainId 0x2106) vs the passed
    // chainId (0x2105) must be rejected as OpConsensusError. call=true (eth_call) skips the check
    // and proceeds to opValidate, so no OpConsensusError is thrown there.
    OpstackExecutor executor{receiptFactory, cryptoSuite->hashImpl(), fork};
    bcostars::protocol::BlockHeaderImpl blockHeader;
    blockHeader.setNumber(1);
    blockHeader.calculateHash(*cryptoSuite->hashImpl());

    auto tx = buildWeb3Tx(/*chainId=*/0x2106);  // cross-chain envelope
    constexpr auto sender = 0xe0e794ca86d198042b64285c5ce667aee747509b_address;
    tx.clearSenderAndHash();
    tx.forceSender(bcos::bytes(sender.bytes, sender.bytes + sizeof(sender.bytes)));
    task::syncWait([&]() -> task::Task<void> {
        ledger::account::EVMAccount<MutableStorage> acc(storage, sender, false);
        co_await acc.create();
        co_await acc.setBalance(u256("100000000000000000000"));
        // EIP-3607: seed the canonical empty-code hash so the sender is recognised as an EOA.
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
    constexpr uint64_t kChainId = 0x2105;

    // call=true (eth_call): envelope signature validation is skipped; execution proceeds, so no
    // OpConsensusError is thrown for the cross-chain envelope. Runs first so the account nonce is
    // pristine for both assertions (the real-execution path below persists the nonce bump).
    try
    {
        auto receipt = task::syncWait(executor.executeTransaction(storage, blockHeader, tx,
            /*contextID=*/0, ledgerConfig, /*call=*/true, fee, /*blockGasLeft=*/30000000,
            /*chainId=*/kChainId));
        EXPECT_NE(receipt, nullptr);
    }
    catch (bcos::evm::engine::OpConsensusError const&)
    {
        FAIL() << "call=true must skip validateEnvelopeSignature";
    }

    // call=false: chain-id mismatch rejected in m_prepare, before opValidate.
    EXPECT_THROW(task::syncWait(executor.executeTransaction(storage, blockHeader, tx,
                     /*contextID=*/0, ledgerConfig, /*call=*/false, fee, /*blockGasLeft=*/30000000,
                     /*chainId=*/kChainId)),
        bcos::evm::engine::OpConsensusError);
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

TEST_F(Fixture, BlockInfoGasLimitUsesHeaderGasLimit)
{
    // spec §8 (v2: gasLimit only): buildBlockInfo's gasLimit takes header.gasLimit (not
    // blockGasLeft). Behavior assertion: executing a minimal GASLIMIT-reading contract, slot0 must
    // store == header.gasLimit(). Before the fix: executeTransaction's BlockInfo.gasLimit =
    // blockGasLeft (injected value), so the contract stored blockGasLeft; injecting
    // blockGasLeft(250000) < header.gasLimit(1000000) went red. After the fix:
    // BlockInfo.gasLimit = header.gasLimit == 1000000 → green. Round-3 P0-1 correction:
    // blockGasLeft must be ≥ tx.gasLimit(200000) (else state.cpp:390 GAS_LIMIT_REACHED rejects at
    // validation and the test is permanently red) AND < header.gasLimit(1000000) to expose the
    // fork.
    constexpr auto kObserver = 0x00000000000000000000000000000000000000aa_address;
    constexpr auto kSender = 0xe0e794ca86d198042b64285c5ce667aee747509b_address;
    const bcos::bytes kObserverCode{0x45, 0x60, 0x00, 0x55, 0x00};  // GASLIMIT; PUSH1 0; SSTORE;
                                                                    // STOP

    OpstackExecutor executor{receiptFactory, cryptoSuite->hashImpl(), fork};
    bcostars::protocol::BlockHeaderImpl blockHeader;
    blockHeader.setNumber(1);
    blockHeader.setGasLimit(bcos::u256(1000000));  // exercises the new path (gasLimit()!=0)
    blockHeader.calculateHash(*cryptoSuite->hashImpl());
    ledgerConfig.setEVMCRevision(fork.rev);

    // Deploy the GASLIMIT observer contract + fund the sender (mirror the fundAccount pattern).
    task::syncWait([&]() -> task::Task<void> {
        ledger::account::EVMAccount<MutableStorage> obs(storage, kObserver, false);
        co_await obs.create();
        co_await obs.setCode(kObserverCode, "", cryptoSuite->hashImpl()->hash(kObserverCode));
        co_await obs.setBalance(bcos::u256(0));
        co_await obs.setNonce("0");
        ledger::account::EVMAccount<MutableStorage> snd(storage, kSender, false);
        co_await snd.create();
        co_await snd.setCode({}, "",
            bcos::crypto::HashType(
                "c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470"));
        co_await snd.setBalance(u256("100000000000000000000"));
        co_await snd.setNonce("0");
        co_return;
    }());

    // EIP-1559 tx calling the observer with empty data, value 0. chainId=10 matches the
    // executeTransaction call below (Task 2: envelope chain-id binding is enforced in m_prepare).
    bcos::rpc::Web3Transaction w3{};
    w3.type = bcos::rpc::TransactionType::EIP1559;
    w3.chainId = 10;
    w3.nonce = 0;
    w3.maxFeePerGas = bcos::u256(30000000000);
    w3.maxPriorityFeePerGas = bcos::u256(0);
    w3.gasLimit = 200000;
    w3.to = bcos::Address("0x00000000000000000000000000000000000000aa");
    w3.value = bcos::u256(0);
    w3.signatureV = 0;
    w3.signatureR = bcos::bytes(32, 0x01);
    w3.signatureS = bcos::bytes(32, 0x02);
    auto tarsHolder = std::make_shared<bcostars::Transaction>(w3.takeToTarsTransaction());
    auto const txHash = w3.txHash();
    tarsHolder->extraTransactionHash.assign(txHash.begin(), txHash.end());
    // Overwrite the signing preimage with the full EIP-2718 envelope (SEV-8 pattern).
    auto const envelope = w3.encode();
    tarsHolder->extraTransactionBytes.assign(envelope.begin(), envelope.end());
    bcostars::protocol::TransactionImpl tx([tarsHolder]() { return tarsHolder.get(); });
    tx.clearSenderAndHash();
    tx.forceSender(bcos::bytes(kSender.bytes, kSender.bytes + sizeof(kSender.bytes)));

    bcos::evm::opstack::OpFeeParams fee{};
    // Inject blockGasLeft=250000 < header.gasLimit=1000000 (round-3 P0-1: ≥ tx.gasLimit 200000
    // passes validation, < header exposes the GASLIMIT fork) — the key to exposing the fork.
    auto receipt = task::syncWait(executor.executeTransaction(storage, blockHeader, tx,
        /*contextID=*/0, ledgerConfig, /*call=*/false, fee, /*blockGasLeft=*/250000,
        /*chainId=*/10));
    ASSERT_NE(receipt, nullptr);
    EXPECT_EQ(receipt->status(), 0);

    // Read observer slot0: must == header.gasLimit (1000000), not the injected blockGasLeft.
    // Round-3 P0-2: the storage key is raw 32 bytes (evmc_bytes32{}=32 0x00 bytes); neither
    // storageEntry("0") nor a hex string reads it. EVMAccount::storage() returns an evmc_bytes32
    // value (all-zero when unset), not an optional — parse slot.bytes with intx::be::load
    // (32-byte big-endian), not storageEntry's has_value()/operator->.
    ledger::account::EVMAccount<MutableStorage> obs(storage, kObserver, false);
    auto slot = task::syncWait(obs.storage(evmc_bytes32{}));  // EVMAccount.h:210
    EXPECT_EQ(intx::be::load<intx::uint256>(slot.bytes), intx::uint256(1000000));
}
