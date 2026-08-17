// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// OpstackExecutorTest.cpp — drives OpstackExecutor over BCOS storage. opTransition/runDeposit
// produce the final FISCO receipt directly (OP metadata + effective gas price already projected),
// and the state diff is returned via an out-param. Storage is a plain MutableStorage.

#define BOOST_TEST_MODULE BcosOpstackExecutorTests
#include <boost/test/unit_test.hpp>

#include "bcos-evm/opstack/OpForkSchedule.h"
#include "bcos-evm/opstack/OpPredeploys.h"
#include "opstack-executor/OpstackExecutor.h"
#include <bcos-codec/rlp/Common.h>
#include <bcos-codec/rlp/RLPEncode.h>  // construct a 33-byte-mint envelope for the over-wide test
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

struct Fixture
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
        // defaults to 10 to match every executeTransaction call site below, so the fixture
        // envelope must carry the same chain id the call passes.
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
        // overwrite with the full EIP-2718 envelope (mirroring EngineServiceImpl buildOpBlock) so
        // the envelope is canonical wire bytes.
        auto const envelope = w3.encode();
        tarsHolder->extraTransactionBytes.assign(envelope.begin(), envelope.end());
        return bcostars::protocol::TransactionImpl([tarsHolder]() { return tarsHolder.get(); });
    }

    // Deposit tx mirror for single-tx deposit dispatch. isSystemTx mirrors the
    // deposit RLP field (tars field 15), NOT Transaction::systemTx().
    bcostars::protocol::TransactionImpl buildDepositTx(bool isSystemTx = false)
    {
        bcos::rpc::Web3Transaction w3{};
        w3.type = bcos::rpc::TransactionType::Deposit;
        w3.sourceHash =
            bcos::h256("0x6ab967dfdd3aa359031bef6965cca32ed9a21ea969f7aeee2e58817142a645d7");
        w3.from = bcos::Address("0xdeaddeaddeaddeaddeaddeaddeaddeaddead0001");
        w3.to = bcos::Address("0x4200000000000000000000000000000000000015");
        w3.mint = bcos::u256(5);
        w3.value = bcos::u256(0);
        w3.gasLimit = 100000;
        w3.isSystemTx = isSystemTx;
        auto tarsHolder = std::make_shared<bcostars::Transaction>(w3.takeToTarsTransaction());
        return bcostars::protocol::TransactionImpl([tarsHolder]() { return tarsHolder.get(); });
    }

    template <class T>
    static T sync(task::Task<T> t)
    {
        return task::syncWait(std::move(t));
    }
};
}  // namespace

BOOST_AUTO_TEST_SUITE(OpstackExecutorTestSuite)

BOOST_AUTO_TEST_CASE(ConstructsWithJovianFork)
{
    auto rf =
        std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(makeCryptoSuite());
    OpstackExecutor executor(rf, makeCryptoSuite()->hashImpl());
    BOOST_CHECK_NE(&executor.vm(), nullptr);
}

BOOST_AUTO_TEST_CASE(BuildOpBlockInfoMirrorsBlockPath)
{
    // buildBlockInfo must mirror toBlockInfo's field mapping (OpCommon.h:106-121) so eth_call
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
    BOOST_CHECK_EQUAL(blk.number, 7);
    BOOST_CHECK_EQUAL(blk.timestamp, 1'234'567ULL);  // ms -> s
    BOOST_CHECK_EQUAL(blk.gas_limit, 30'000'000);    // injected gasLimit
    // intx::uint256 / evmc::bytes / optional<uint64_t> have no operator<< — use plain BOOST_CHECK
    // (same predicate as EXPECT_EQ, just without value printing on failure).
    BOOST_CHECK(blk.base_fee == intx::uint256(1'000'000'000));  // header baseFee, NOT 0
    BOOST_CHECK_EQUAL(blk.prev_randao.bytes[0], 0xab);          // full field set
    BOOST_CHECK_EQUAL(blk.parent_beacon_block_root.bytes[0], 0xcd);
    BOOST_CHECK(blk.extra_data == (evmc::bytes{0xde, 0xad}));
    BOOST_CHECK(blk.blob_gas_used == 0x1234ULL);
}

BOOST_FIXTURE_TEST_CASE(ExecutesNormalTransferEndToEnd, Fixture)
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
    BOOST_REQUIRE_NE(receipt, nullptr);
    // FISCO internal convention: 0 = success (the Ethereum RPC 0<->1 flip happens later).
    BOOST_CHECK_EQUAL(receipt->status(), 0);
    BOOST_CHECK_GE(receipt->gasUsed(), 21000u);
    BOOST_CHECK_EQUAL(receipt->blockNumber(), 1);
    BOOST_CHECK(receipt->opStackMeta().has_value());
}

BOOST_FIXTURE_TEST_CASE(RejectsForkRevisionMismatch, Fixture)
{
    OpstackExecutor executor{receiptFactory, cryptoSuite->hashImpl(), fork};
    ledgerConfig.setEVMCRevision(EVMC_FRONTIER);  // deliberately != fork.rev
    bcostars::protocol::BlockHeaderImpl blockHeader;
    blockHeader.setNumber(1);
    auto tx = buildWeb3Tx();
    bcos::evm::opstack::OpFeeParams fee{};
    BOOST_CHECK_THROW(task::syncWait(executor.executeTransaction(
                          storage, blockHeader, tx, 0, ledgerConfig, false, fee, 30000000, 10)),
        bcos::executor_v1::opstack::OpForkRevisionMismatch);
}

BOOST_FIXTURE_TEST_CASE(RejectsInvalidTx, Fixture)
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
    BOOST_CHECK_THROW(task::syncWait(executor.executeTransaction(
                          storage, blockHeader, tx, 0, ledgerConfig, false, fee, 30000000, 10)),
        bcos::executor_v1::opstack::OpTxValidationFailed);
}

BOOST_FIXTURE_TEST_CASE(ChargesL1AndOperatorFees, Fixture)
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
    BOOST_REQUIRE_NE(receipt, nullptr);
    BOOST_CHECK_EQUAL(receipt->status(), 0);

    auto meta = receipt->opStackMeta();
    BOOST_REQUIRE(meta.has_value());
    // Jovian derives these unconditionally.
    BOOST_CHECK(meta->l1_fee.has_value());
    BOOST_CHECK(meta->l1_gas_price.has_value());
    // operator_fee_scalar filled when has_operator_fee && (scalar != 0 || constant != 0).
    BOOST_CHECK(meta->operator_fee_scalar.has_value());
    BOOST_CHECK_EQUAL(*meta->operator_fee_scalar, 11u);
}

BOOST_FIXTURE_TEST_CASE(ReceiptMetaSurvives, Fixture)
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
    BOOST_REQUIRE_NE(receipt, nullptr);
    auto meta = receipt->opStackMeta();
    BOOST_REQUIRE(meta.has_value());
    BOOST_REQUIRE(meta->l1_gas_price.has_value());
    BOOST_CHECK(*meta->l1_gas_price == bcos::u256(1000));
    // v2 deriveOpReceiptMeta derives l1_base_fee_scalar from props.fee (not l1_gas_used, which the
    // v2 implementation no longer fills); the fee below set base_fee_scalar=7.
    BOOST_REQUIRE(meta->l1_base_fee_scalar.has_value());
    BOOST_CHECK_EQUAL(*meta->l1_base_fee_scalar, 7u);
    // effective_gas_price = base_fee(0) + min(maxPriority, maxFee-0); for this EIP-2930 tx
    // BCOS2Evmone.h maps max_gas_price = max_priority_gas_price = gasPrice, so effective =
    // gasPrice = 0x6fc23ac00.
    BOOST_CHECK_EQUAL(receipt->effectiveGasPrice(), "0x6fc23ac00");
}

// ---- executeDeposit + finalizeBlock (reuse bcos-evm/opstack runDeposit / finalizeOpBlock) ----

BOOST_FIXTURE_TEST_CASE(ExecutesDepositMint, Fixture)
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
    BOOST_REQUIRE_NE(receipt, nullptr);
    BOOST_CHECK_EQUAL(receipt->status(), 0);
    // mint(5) added to the from balance.
    ledger::account::EVMAccount<MutableStorage> acc(storage, kFrom, false);
    auto bal = task::syncWait(acc.balance());
    BOOST_CHECK(bal == 5u);
    // deposit_nonce == 0, version == 1 (Canyon+).
    auto meta = receipt->opStackMeta();
    BOOST_REQUIRE(meta.has_value());
    BOOST_REQUIRE(meta->deposit_nonce.has_value());
    BOOST_CHECK_EQUAL(*meta->deposit_nonce, 0u);
    BOOST_REQUIRE(meta->deposit_receipt_version.has_value());
    BOOST_CHECK_EQUAL(*meta->deposit_receipt_version, 1u);
}

BOOST_FIXTURE_TEST_CASE(ExecutesDepositThroughExecuteTransaction, Fixture)
{
    OpstackExecutor executor{receiptFactory, cryptoSuite->hashImpl(), fork};
    bcostars::protocol::BlockHeaderImpl blockHeader;
    blockHeader.setNumber(1);
    blockHeader.calculateHash(*cryptoSuite->hashImpl());

    auto tx = buildDepositTx();
    constexpr auto from = 0xdeaddeaddeaddeaddeaddeaddeaddeaddead0001_address;
    task::syncWait([&]() -> task::Task<void> {
        ledger::account::EVMAccount<MutableStorage> acc(storage, from, false);
        co_await acc.create();
        co_await acc.setBalance(u256(0));
        co_await acc.setNonce("0");
        co_return;
    }());

    // fee default {} — deposit must ignore it (no L1/operator fee).
    auto receipt = task::syncWait(executor.executeTransaction(storage, blockHeader, tx,
        /*contextID=*/0, ledgerConfig, /*call=*/false, /*fee=*/{}, /*blockGasLeft=*/30000000,
        /*chainId=*/10));
    BOOST_REQUIRE_NE(receipt, nullptr);
    BOOST_CHECK_EQUAL(receipt->status(), 0);

    // mint(5) added to from balance (distinguishes deposit from a normal-path run, which mints
    // nothing).
    ledger::account::EVMAccount<MutableStorage> acc(storage, from, false);
    auto bal = task::syncWait(acc.balance());
    BOOST_CHECK(bal == 5u);

    // deposit_nonce == 0, version == 1 (Canyon+).
    auto meta = receipt->opStackMeta();
    BOOST_REQUIRE(meta.has_value());
    BOOST_REQUIRE(meta->deposit_nonce.has_value());
    BOOST_CHECK_EQUAL(*meta->deposit_nonce, 0u);
    BOOST_REQUIRE(meta->deposit_receipt_version.has_value());
    BOOST_CHECK_EQUAL(*meta->deposit_receipt_version, 1u);
}

BOOST_FIXTURE_TEST_CASE(DepositLifecycleThroughExecuteContext, Fixture)
{
    // Concept lifecycle (createExecuteContext -> prepare -> execute -> finish) on a deposit tx:
    // the deposit branch must short-circuit opValidate and run executeDeposit, and finish() must
    // decrement ctx.blockGasLeft + backfill the receipt's cumulativeGasUsed.
    OpstackExecutor executor{receiptFactory, cryptoSuite->hashImpl(), fork};
    bcostars::protocol::BlockHeaderImpl blockHeader;
    blockHeader.setNumber(1);
    blockHeader.calculateHash(*cryptoSuite->hashImpl());

    auto tx = buildDepositTx();
    constexpr auto from = 0xdeaddeaddeaddeaddeaddeaddeaddeaddead0001_address;
    task::syncWait([&]() -> task::Task<void> {
        ledger::account::EVMAccount<MutableStorage> acc(storage, from, false);
        co_await acc.create();
        co_await acc.setBalance(u256(0));
        co_await acc.setNonce("0");
        co_return;
    }());

    constexpr int64_t kInitialGasLeft = 30000000;
    OpBlockExecutionContext ctx{};
    ctx.blockGasLeft = kInitialGasLeft;
    ctx.chainId = 10;  // blockHashes left null -> executeDeposit uses ZeroBlockHashes

    auto context = task::syncWait(executor.createExecuteContext(
        storage, blockHeader, tx, /*contextID=*/0, ledgerConfig, /*call=*/false, ctx));
    task::syncWait(context.prepare());
    task::syncWait(context.execute());
    auto receipt = task::syncWait(context.finish());

    BOOST_REQUIRE_NE(receipt, nullptr);
    BOOST_CHECK_EQUAL(receipt->status(), 0);
    // mint(5) applied to the from balance (distinguishes the deposit path from a no-op).
    ledger::account::EVMAccount<MutableStorage> acc(storage, from, false);
    auto bal = task::syncWait(acc.balance());
    BOOST_CHECK(bal == 5u);
    // blockGasLeft decremented by the deposit's gasUsed; cumulative gas backfilled on the receipt.
    auto const gasUsed = bcos::evm::opstack::narrowGasUsed(receipt->gasUsed());
    BOOST_CHECK_LT(ctx.blockGasLeft, kInitialGasLeft);
    BOOST_CHECK_EQUAL(ctx.blockGasLeft, kInitialGasLeft - gasUsed);
    BOOST_CHECK(ctx.cumulativeGasUsed == gasUsed);
    BOOST_CHECK_EQUAL(
        std::string(receipt->cumulativeGasUsed()), bcos::evm::opstack::hexCumulative(gasUsed));
}

// 6-arg createExecuteContext (generic scheduler / concept form) has no BlockContext: the old
// default-arg footgun left m_ctx pointing at a destroyed temporary. It must now build a null-ctx
// context whose prepare() throws a clear OpConsensusError instead of UB.
BOOST_FIXTURE_TEST_CASE(NullContextSixArgFormThrows, Fixture)
{
    OpstackExecutor executor{receiptFactory, cryptoSuite->hashImpl(), fork};
    bcostars::protocol::BlockHeaderImpl blockHeader;
    blockHeader.setNumber(1);
    blockHeader.calculateHash(*cryptoSuite->hashImpl());

    auto tx = buildDepositTx();
    auto context = task::syncWait(executor.createExecuteContext(
        storage, blockHeader, tx, /*contextID=*/0, ledgerConfig, /*call=*/false));
    BOOST_CHECK_THROW(task::syncWait(context.prepare()), bcos::evm::engine::OpConsensusError);
    BOOST_CHECK_THROW(task::syncWait(context.execute()), bcos::evm::engine::OpConsensusError);
    BOOST_CHECK_THROW(task::syncWait(context.finish()), bcos::evm::engine::OpConsensusError);
}

BOOST_FIXTURE_TEST_CASE(DepositGasLimitReachedIsBlockError, Fixture)
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
    BOOST_CHECK_THROW(task::syncWait(executor.executeDeposit(storage, blockHeader, dep,
                          /*chainId=*/10, /*blockGasLeft=*/100, ledgerConfig)),
        std::runtime_error);
}

BOOST_FIXTURE_TEST_CASE(FinalizeOpBlockNoReward, Fixture)
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
    BOOST_CHECK(bal == 100u);
}

BOOST_FIXTURE_TEST_CASE(BlockInfoGasLimitUsesHeaderGasLimit, Fixture)
{
    // buildBlockInfo's gasLimit takes header.gasLimit (not blockGasLeft). Behavior assertion:
    // executing a minimal GASLIMIT-reading contract, slot0 must store == header.gasLimit().
    // Before the fix: executeTransaction's BlockInfo.gasLimit = blockGasLeft (injected value), so
    // the contract stored blockGasLeft; injecting blockGasLeft(250000) < header.gasLimit(1000000)
    // went red. After the fix: BlockInfo.gasLimit = header.gasLimit == 1000000 → green.
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
    blockHeader.setGasLimit(bcos::u256(1000000));  // exercises the header-gasLimit path
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
    // executeTransaction call below.
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
    // Overwrite the signing preimage with the full EIP-2718 envelope.
    auto const envelope = w3.encode();
    tarsHolder->extraTransactionBytes.assign(envelope.begin(), envelope.end());
    bcostars::protocol::TransactionImpl tx([tarsHolder]() { return tarsHolder.get(); });
    tx.clearSenderAndHash();
    tx.forceSender(bcos::bytes(kSender.bytes, kSender.bytes + sizeof(kSender.bytes)));

    bcos::evm::opstack::OpFeeParams fee{};
    // Inject blockGasLeft=250000 < header.gasLimit=1000000 (≥ tx.gasLimit 200000 passes
    // validation, < header exposes the GASLIMIT fork) — the key to exposing the fork.
    auto receipt = task::syncWait(executor.executeTransaction(storage, blockHeader, tx,
        /*contextID=*/0, ledgerConfig, /*call=*/false, fee, /*blockGasLeft=*/250000,
        /*chainId=*/10));
    BOOST_REQUIRE_NE(receipt, nullptr);
    BOOST_CHECK_EQUAL(receipt->status(), 0);

    // Read observer slot0: must == header.gasLimit (1000000), not the injected blockGasLeft.
    // The storage key is raw 32 bytes (evmc_bytes32{}=32 0x00 bytes); neither
    // storageEntry("0") nor a hex string reads it. EVMAccount::storage() returns an evmc_bytes32
    // value (all-zero when unset), not an optional — parse slot.bytes with intx::be::load
    // (32-byte big-endian), not storageEntry's has_value()/operator->.
    ledger::account::EVMAccount<MutableStorage> obs(storage, kObserver, false);
    auto slot = task::syncWait(obs.storage(evmc_bytes32{}));  // EVMAccount.h:210
    BOOST_CHECK(intx::be::load<intx::uint256>(slot.bytes) == intx::uint256(1000000));
}

BOOST_FIXTURE_TEST_CASE(DepositIsSystemTransactionGetter, Fixture)
{
    auto tx = buildDepositTx(/*isSystemTx=*/true);
    BOOST_CHECK(tx.isDepositTx());                 // web3TypedTxKind == 0x7e
    BOOST_CHECK(tx.depositIsSystemTransaction());  // tars field 15 == 1

    auto tx2 = buildDepositTx(/*isSystemTx=*/false);
    BOOST_CHECK(tx2.isDepositTx());
    BOOST_CHECK(!tx2.depositIsSystemTransaction());  // tars field 15 == 0
}

/// kyonRay review #5429 K3: consensus deposit fields MUST come from the signed 0x7E envelope
/// (decodeDepositEnvelope), never from the unauthenticated tars mirrors. Happy path decodes a
/// legit buildDepositTx envelope; rejection paths cover a non-0x7e type byte and trailing bytes.
BOOST_AUTO_TEST_CASE(DecodeDepositEnvelopeFromSignedEnvelope)
{
    Fixture f;
    auto tx = f.buildDepositTx(/*isSystemTx=*/false);
    auto env = tx.extraTransactionBytes();
    BOOST_REQUIRE(!env.empty());
    BOOST_CHECK_EQUAL(env[0], 0x7e);

    auto dep = decodeDepositEnvelope(env);
    // sourceHash matches the builder's fixed h256.
    bcos::crypto::HashType sh("0x6ab967dfdd3aa359031bef6965cca32ed9a21ea969f7aeee2e58817142a645d7");
    BOOST_CHECK(std::equal(
        dep.source_hash.bytes, dep.source_hash.bytes + sizeof(dep.source_hash.bytes), sh.begin()));
    // from == the builder's dead...0001 address.
    BOOST_CHECK_EQUAL(bcos::toHexStringWithPrefix(
                          bcos::bytes(dep.from.bytes, dep.from.bytes + sizeof(dep.from.bytes))),
        "0xdeaddeaddeaddeaddeaddeaddeaddeaddead0001");
    BOOST_REQUIRE(dep.to.has_value());
    BOOST_CHECK_EQUAL(bcos::toHexStringWithPrefix(
                          bcos::bytes(dep.to->bytes, dep.to->bytes + sizeof(dep.to->bytes))),
        "0x4200000000000000000000000000000000000015");
    BOOST_REQUIRE(dep.mint.has_value());
    BOOST_CHECK(dep.mint.value() == intx::uint256{5});  // intx::uint256 has no ostream<<
    BOOST_CHECK(dep.value == intx::uint256{0});
    BOOST_CHECK_EQUAL(dep.gas_limit, 100000);
    BOOST_CHECK(!dep.is_system_tx);
    BOOST_CHECK(dep.data.empty());

    // Rejection: a non-0x7e type byte (the 0x02-envelope + 0x7e-mirror attack) must fail loud.
    bcos::bytes bad(env.begin(), env.end());
    bad[0] = 0x02;
    BOOST_CHECK_THROW(
        [&]() { (void)decodeDepositEnvelope(bcos::ref(bad)); }(), OpTxValidationFailed);

    // Rejection: trailing bytes after the RLP list must be refused (strict, consensus-grade).
    bcos::bytes trailing(env.begin(), env.end());
    trailing.push_back(0x00);
    BOOST_CHECK_THROW(
        [&]() { (void)decodeDepositEnvelope(bcos::ref(trailing)); }(), OpTxValidationFailed);

    // Rejection (#1, kyonRay): an over-wide integer (33-byte mint) must fail loud —
    // rlp::decode(UnsignedIntegral) alone would truncate to the low 32 bytes via fromBigEndian.
    {
        bcos::bytes body;
        bcos::codec::rlp::encode(
            body, bcos::h256("0x6ab967dfdd3aa359031bef6965cca32ed9a21ea969f7aeee2e58817142a645d7"));
        bcos::codec::rlp::encode(body, bcos::Address("0xdeaddeaddeaddeaddeaddeaddeaddeaddead0001"));
        bcos::codec::rlp::encode(body, bcos::Address("0x4200000000000000000000000000000000000015"));
        bcos::codec::rlp::encode(body, bcos::bytes(33, 0xff));  // 33-byte mint -> over-wide
        bcos::codec::rlp::encode(body, bcos::bytes{});          // value = 0 (empty RLP item)
        bcos::codec::rlp::encode(body, bcos::bytes{});          // gas = 0
        bcos::codec::rlp::encode(body, bcos::bytes{});          // isSystemTx = 0
        bcos::codec::rlp::encode(body, bcos::bytes{});          // data = empty
        bcos::bytes list;
        bcos::codec::rlp::encodeHeader(list, bcos::codec::rlp::Header{true, body.size()});
        list.insert(list.end(), body.begin(), body.end());
        bcos::bytes overWideEnv{0x7e};
        overWideEnv.insert(overWideEnv.end(), list.begin(), list.end());
        BOOST_CHECK_THROW(
            [&]() { (void)decodeDepositEnvelope(bcos::ref(overWideEnv)); }(), OpTxValidationFailed);
    }

    // Rejection (#2 round 3, kyonRay): non-canonical integers / int64 range / bool — the width
    // gate does not cover these, and rlp::decode only rejects one non-canonical form (single-byte
    // payload < 0x80 via decodeHeader). Build an envelope from raw per-field items so we can feed
    // encodings RLPEncode would never emit. Field order: sourceHash, from, to, mint, value, gas,
    // isSystemTx, data.
    auto canonicalItems = []() {
        bcos::bytes sh, from, to, mint, value, gas, isSystem, data;
        bcos::codec::rlp::encode(
            sh, bcos::h256("0x6ab967dfdd3aa359031bef6965cca32ed9a21ea969f7aeee2e58817142a645d7"));
        bcos::codec::rlp::encode(from, bcos::Address("0xdeaddeaddeaddeaddeaddeaddeaddeaddead0001"));
        bcos::codec::rlp::encode(to, bcos::Address("0x4200000000000000000000000000000000000015"));
        bcos::codec::rlp::encode(mint, bcos::bytes{5});                // mint = 5 (bare Byte)
        bcos::codec::rlp::encode(value, bcos::bytes{});                // value = 0 (empty item)
        bcos::codec::rlp::encode(gas, bcos::bytes{0x01, 0x86, 0xa0});  // gas = 100000
        bcos::codec::rlp::encode(isSystem, bcos::bytes{});             // isSystemTx = false
        bcos::codec::rlp::encode(data, bcos::bytes{});                 // data = empty
        return std::vector<bcos::bytes>{sh, from, to, mint, value, gas, isSystem, data};
    };
    auto envelopeFromItems = [](std::vector<bcos::bytes> const& items) {
        bcos::bytes body;
        for (auto const& it : items)
            body.insert(body.end(), it.begin(), it.end());
        bcos::bytes list;
        bcos::codec::rlp::encodeHeader(list, bcos::codec::rlp::Header{true, body.size()});
        bcos::bytes env{0x7e};
        env.insert(env.end(), list.begin(), list.end());
        return env;
    };

    // (a) leading-zero multi-byte integer (value = 0x82 0x00 0x05): would fold to 5, but op-geth
    // rejects it as ErrCanonInt.
    {
        auto items = canonicalItems();
        items[4] = bcos::bytes{0x82, 0x00, 0x05};  // value
        BOOST_CHECK_THROW(
            [&]() { (void)decodeDepositEnvelope(bcos::ref(envelopeFromItems(items))); }(),
            OpTxValidationFailed);
    }
    // (b) single Byte 0x00 (value): integer zero must be the empty item 0x80 — op-geth ErrCanonInt.
    {
        auto items = canonicalItems();
        items[4] = bcos::bytes{0x00};  // value
        BOOST_CHECK_THROW(
            [&]() { (void)decodeDepositEnvelope(bcos::ref(envelopeFromItems(items))); }(),
            OpTxValidationFailed);
    }
    // (c) gas = uint64 max (0x88 FF..FF): static_cast<int64_t>(gas) would wrap to -1; the decoder
    // rejects values that exceed int64 range.
    {
        auto items = canonicalItems();
        items[5] = bcos::bytes{0x88, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};  // gas
        BOOST_CHECK_THROW(
            [&]() { (void)decodeDepositEnvelope(bcos::ref(envelopeFromItems(items))); }(),
            OpTxValidationFailed);
    }
    // (d) isSystemTx = bare Byte 2: != 0 would read true, but op-geth decodeBool accepts only the
    // empty item (false) and 0x01 (true).
    {
        auto items = canonicalItems();
        items[6] = bcos::bytes{0x02};  // isSystemTx
        BOOST_CHECK_THROW(
            [&]() { (void)decodeDepositEnvelope(bcos::ref(envelopeFromItems(items))); }(),
            OpTxValidationFailed);
    }
}

BOOST_AUTO_TEST_SUITE_END()
