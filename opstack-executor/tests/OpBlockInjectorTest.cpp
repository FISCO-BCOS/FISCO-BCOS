// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// OpBlockInjectorTest — drives the shared block-execution path (preBlockOpSteps →
// SchedulerSerialImpl(serial=true) → finalizeOpBlockResult — runOpBlockInjection's successor, Task
// 5) over a plain MutableStorage fixture (spec §7(a); the path is Storage templates, so no MLS is
// needed). A minimal "L1 attributes deposit + eip1559" block verifies:
//   (1) the system-call BlockInfo's gas_limit == header.gasLimit (toBlockInfo, trivially true);
//   (2) receipt count == tx count;
//   (3) the block-level gasUsed == manual Σ per-receipt gasUsed.
// Plus: preBlockOpSteps rejects an empty block with OpConsensusError (the retired injector's
// empty-block guard now lives there).
// Per-tx BlockInfo gasLimit==header is deliberately NOT asserted here — that belongs to
// OpstackExecutorTest::BlockInfoGasLimitUsesHeaderGasLimit.

#include "support/OpDepositEncode.h"  // encodeDepositEnvelope (deposit envelope reconstruction)
#include <opstack-executor/OpBlockExecute.h>

#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpPredeploys.h>
#include <bcos-framework/ledger/EVMAccount.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-rpc/web3jsonrpc/model/Web3Transaction.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-task/Wait.h>
#include <bcos-transaction-scheduler/SchedulerSerialImpl.h>  // per-tx loop (Task 5)
#include <bcos-utilities/IOServicePool.h>
#include <engine/bcos-engine/EngineServiceImpl.h>  // detail::opEnvelopeToTars (tests link engine)
#include <boost/test/unit_test.hpp>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using bcos::executor_v1::StateKey;
using bcos::executor_v1::StateValue;
namespace memory_storage = bcos::storage2::memory_storage;
using evmc::literals::operator""_address;

namespace
{
using MutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;

constexpr uint64_t kChainId = 0x2105;
constexpr int64_t kHeaderGasLimit = 30'000'000;
const bcos::Address kSender{"0x1000000000000000000000000000000000000000"};

bcos::crypto::CryptoSuite::Ptr makeCryptoSuite()
{
    return std::make_shared<bcos::crypto::CryptoSuite>(
        std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr);
}

bcos::protocol::TransactionReceiptFactory::Ptr makeReceiptFactory()
{
    return std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(makeCryptoSuite());
}

/// A header carrying every optional field toBlockInfo reads via `.value()` (OpCommon.h:106-121):
/// baseFee / parentBeaconBlockRoot / blobGasUsed must be set or toBlockInfo throws.
std::shared_ptr<bcostars::protocol::BlockHeaderImpl> makeHeader(int64_t timestampMillis)
{
    auto h = std::make_shared<bcostars::protocol::BlockHeaderImpl>();
    h->setNumber(1);
    h->setTimestamp(timestampMillis);
    h->setParentInfo(bcos::protocol::ParentInfo{.blockNumber = 0, .blockHash = bcos::h256{}});
    h->setCoinbase(bcos::Address{});
    h->setStateRoot(bcos::h256{});
    h->setTxsRoot(bcos::h256{});
    h->setReceiptsRoot(bcos::h256{});
    h->setGasLimit(bcos::u256(kHeaderGasLimit));
    h->setGasUsed(bcos::u256(0));
    h->setExtraData(bcos::bytes{});
    h->setPrevRandao(bcos::h256{});
    h->setBaseFee(bcos::u256(1'000'000'000));
    h->setWithdrawalsRoot(bcos::h256{});
    h->setBlobGasUsed(bcos::u256(0));
    h->setExcessBlobGas(bcos::u256(0));
    h->setParentBeaconBlockRoot(bcos::h256{});
    h->setRequestsHash(bcos::h256{});
    return h;
}

/// The block's L1 attributes deposit: to==OP_L1_BLOCK && from==OP_DEPOSITOR satisfies the
/// stricter-than-spec content check (OpBlockExecute.h isL1AttributesTx). Isthmus config means
/// validateJovianBlockShape is a no-op, so the calldata can be minimal.
bcos::evm::opstack::OpBlockTx makeAttributesDeposit()
{
    bcos::evm::opstack::DepositTx dep{
        .source_hash = evmc::bytes32{},
        .from = bcos::evm::opstack::OP_DEPOSITOR,
        .to = bcos::evm::opstack::OP_L1_BLOCK,
        .mint = std::nullopt,
        .value = intx::uint256{0},
        .gas_limit = 100000,
        .is_system_tx = false,
        .data = {},
    };
    return bcos::evm::opstack::OpBlockTx{dep, {}};
}

/// The block's normal EIP-1559 tx shell. Only `signedEnvelope` is consumed (the raw envelope the
/// injector classifies by first byte); the evmone fields are filled for realism.
bcos::evm::opstack::OpBlockTx makeEip1559OpBlockTx()
{
    namespace detail = bcos::evm::engine::detail;
    evmone::state::Transaction evmTx;
    evmTx.type = evmone::state::Transaction::Type::eip1559;
    evmTx.sender = detail::toEvmcAddress(kSender);
    evmTx.to = 0x811a752c8cd697e3cb27279c330ed1ada745a8d7_address;
    evmTx.gas_limit = 5'000'000;
    evmTx.max_gas_price = intx::uint256{30'000'000'000ULL};
    evmTx.max_priority_gas_price = 0;
    evmTx.nonce = 0;
    evmTx.value = 0;
    evmTx.data = {};
    evmc::bytes envelope(64, 0x02);  // non-empty stand-in for the raw 0x02-prefixed envelope
    return bcos::evm::opstack::OpBlockTx{evmTx, envelope};
}

/// Caller-prebuilt FISCO Transaction for the eip1559 tx (buildFiscoTx is the caller's
/// responsibility — the injector only consumes Transaction::Ptr). EIP-1559 with
/// maxPriorityFeePerGas=0 keeps effectiveGasPrice == baseFee (BCOS2Evmone's access-list override
/// never fires), and the dummy r/s is neutralized by forceSender.
bcos::protocol::Transaction::Ptr buildEip1559FiscoTx()
{
    // chainId must match the block chainId (kChainId, 0x2105): a mismatch is rejected in m_prepare
    // before opValidate.
    bcos::rpc::Web3Transaction w3{};
    w3.type = bcos::rpc::TransactionType::EIP1559;
    w3.chainId = kChainId;
    w3.nonce = 0;
    w3.maxFeePerGas = bcos::u256(30'000'000'000ULL);
    w3.maxPriorityFeePerGas = 0;
    w3.gasLimit = 5'000'000;
    w3.to = bcos::Address("0x811a752c8cd697e3cb27279c330ed1ada745a8d7");
    w3.value = bcos::u256(0);
    w3.signatureV = 0;
    w3.signatureR = bcos::bytes(32, 0x01);
    w3.signatureS = bcos::bytes(32, 0x02);
    auto tarsHolder = std::make_shared<bcostars::Transaction>(w3.takeToTarsTransaction());
    auto const txHash = w3.txHash();
    tarsHolder->extraTransactionHash.assign(txHash.begin(), txHash.end());
    // takeToTarsTransaction stores the signing preimage (which a full-envelope consumer would
    // reject as a truncated typed envelope); overwrite with the full EIP-2718 envelope.
    auto const envelope = w3.encode();
    tarsHolder->extraTransactionBytes.assign(envelope.begin(), envelope.end());
    auto tx = std::make_shared<bcostars::protocol::TransactionImpl>(
        [tarsHolder]() { return tarsHolder.get(); });
    tx->clearSenderAndHash();
    tx->forceSender(kSender.asBytes());
    return tx;
}

/// Fund the sender EOA in the plain MutableStorage so opValidate's balance + EIP-3607 checks pass
/// (StorageStateView::exists() needs a non-zero codeHash — create + setCode(empty) makes it an
/// existing account with empty code; a bare setBalance would leave it nonexistent).
void fundSender(MutableStorage& storage, bcos::crypto::Hash::Ptr const& hashImpl)
{
    bcos::ledger::account::EVMAccount<MutableStorage> account(storage, kSender, false);
    bcos::task::syncWait(account.create());
    bcos::task::syncWait(account.setCode({}, {}, hashImpl->emptyHash()));
    bcos::task::syncWait(account.setNonce("0"));
    bcos::task::syncWait(account.setBalance(bcos::u256(1) << 200));
}

/// FISCO Transaction from a raw envelope (opEnvelopeToTars + full-envelope override, precedent
/// OpDualPathEquivalenceTest.cpp buildBlockTx): the shared path's per-tx loop re-derives deposits
/// from the Transaction objects, so index 0 must carry a REAL deposit tx (not a placeholder).
bcos::protocol::Transaction::Ptr buildFiscoTxFromEnvelope(
    bcos::bytes const& env, bcos::crypto::Hash::Ptr const& hashImpl)
{
    auto txHash = hashImpl->hash(env);
    auto tarsTx = bcos::engine::detail::opEnvelopeToTars(env, txHash);
    if (!tarsTx)
        return nullptr;
    tarsTx->extraTransactionBytes.assign(env.begin(), env.end());
    return std::make_shared<bcostars::protocol::TransactionImpl>(
        [tars = std::move(*tarsTx)]() mutable { return &tars; });
}

/// Drive the shared block-execution path (preBlockOpSteps → SchedulerSerialImpl(serial=true) →
/// finalizeOpBlockResult) — runOpBlockInjection's successor (Task 5). Mirrors OpScheduler::execute
/// over a plain MutableStorage (no MLS needed: preBlockOpSteps / SchedulerSerialImpl / finalize are
/// Storage templates). preBlockOpSteps throws OpConsensusError on block-level shape faults (the
/// empty-block guard).
bcos::evm::engine::OpExecuteBlockResult runSharedPath(MutableStorage& storage,
    bcos::protocol::BlockHeader const& header, std::vector<bcos::bytes> const& rawTxBytes,
    std::vector<bcos::protocol::Transaction::ConstPtr> const& transactions,
    std::vector<bcos::evm::opstack::DepositTx> const& deposits,
    bcos::evm::opstack::OpForkConfig const& cfg,
    bcos::executor_v1::opstack::OpstackExecutor& executor, bcos::crypto::Hash::Ptr const& hashImpl,
    bcos::IOServicePool::Ptr const& ioServicePool)
{
    namespace detail = bcos::evm::engine::detail;
    bcos::ledger::LedgerConfig execLedgerConfig;
    execLedgerConfig.setEVMCRevision(cfg.rev);

    std::optional<std::string> hashErr;
    std::optional<uint16_t> daFootprintGasScalar;
    std::optional<detail::RecentBlockHashes<MutableStorage>> hashes;
    bcos::evm::engine::preBlockOpSteps(storage, header, cfg, rawTxBytes, deposits, executor,
        hashImpl, hashes, hashErr, daFootprintGasScalar);
    bcos::executor_v1::opstack::OpBlockExecutionContext ctx{.fee = {},
        .blockGasLeft = static_cast<int64_t>(header.gasLimit()),
        .blockHashes = &*hashes,
        .chainId = kChainId,
        .daFootprintGasScalar = daFootprintGasScalar};
    bcos::scheduler_v1::SchedulerSerialImpl serialScheduler(
        ioServicePool, /*chunkSize=*/1, /*serial=*/true);
    auto transactionsRefs =
        transactions |
        ::ranges::views::transform([](bcos::protocol::Transaction::ConstPtr const& ptr)
                                       -> bcos::protocol::Transaction const& { return *ptr; });
    auto receipts = bcos::task::syncWait(serialScheduler.executeBlock(
        storage, executor, header, transactionsRefs, execLedgerConfig, ctx));
    return bcos::evm::engine::finalizeOpBlockResult(executor, storage, header, execLedgerConfig,
        cfg, receipts, rawTxBytes, ctx.cumulativeGasUsed, hashErr);
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OpBlockInjector)

BOOST_AUTO_TEST_CASE(InjectsDepositAndEip1559Block)
{
    namespace op = bcos::evm::opstack;
    namespace engine = bcos::evm::engine;
    namespace detail = bcos::evm::engine::detail;

    // Isthmus-active fork config.
    // Isthmus-active fork config (feature_op_jovian OFF).
    // Named-lvalue first: configAt takes const OpForkFlags&, and GCC-14's -Wdangling-reference
    // flags passing a prvalue `op::OpForkFlags{}` here even though the returned reference
    // aliases the static config, never the flags (false positive).
    const auto forkFlags = op::OpForkFlags{};
    const auto& cfg = op::configAt(forkFlags);

    MutableStorage storage;
    auto cryptoSuite = makeCryptoSuite();
    auto hashImpl = cryptoSuite->hashImpl();
    auto receiptFactory = makeReceiptFactory();
    bcos::executor_v1::opstack::OpstackExecutor executor{receiptFactory, hashImpl, cfg};
    auto ioServicePool = std::make_shared<bcos::IOServicePool>(1);

    auto header = makeHeader(1'000'000);  // 1000 s

    fundSender(storage, hashImpl);

    auto depTx = makeAttributesDeposit();  // OpBlockTx
    auto normTx = makeEip1559OpBlockTx();  // OpBlockTx
    std::vector<op::DepositTx> deposits{std::get<op::DepositTx>(depTx.tx)};
    // makeAttributesDeposit leaves signedEnvelope empty; the type-byte classification reads
    // rawTxBytes[0][0] -> rebuild the real 0x7e envelope with encodeDepositEnvelope.
    bcos::bytes depEnv = encodeDepositEnvelope(std::get<op::DepositTx>(depTx.tx));
    bcos::bytes normEnv(normTx.signedEnvelope.begin(), normTx.signedEnvelope.end());
    std::vector<bcos::bytes> rawTxBytes{depEnv, normEnv};

    // Block-order transactions: index 0 is a REAL deposit tx (the shared per-tx loop re-derives
    // deposits from the Transaction objects), index 1 is normal.
    auto depFiscoTx = buildFiscoTxFromEnvelope(depEnv, hashImpl);
    BOOST_REQUIRE(depFiscoTx != nullptr);
    std::vector<bcos::protocol::Transaction::ConstPtr> transactions{
        depFiscoTx, buildEip1559FiscoTx()};

    auto result = runSharedPath(storage, *header, rawTxBytes, transactions, deposits, cfg, executor,
        hashImpl, ioServicePool);

    // System-call BlockInfo gas_limit == header.gasLimit (toBlockInfo, trivially true here).
    const auto sysBlk = detail::toBlockInfo(*header);
    BOOST_CHECK_EQUAL(sysBlk.gas_limit, kHeaderGasLimit);
    BOOST_CHECK_EQUAL(sysBlk.gas_limit,
        static_cast<int64_t>(detail::narrowU256ToU64(header->gasLimit(), "test")));

    // Receipt count == tx count (both txs execute).
    BOOST_CHECK_EQUAL(result.receipts.size(), rawTxBytes.size());

    // gasUsed == manual Σ per-receipt gasUsed (the block-level cumulative accumulator).
    int64_t manual = 0;
    for (auto const& r : result.receipts)
        manual += op::narrowGasUsed(r->gasUsed());
    BOOST_CHECK_EQUAL(result.gasUsed, static_cast<uint64_t>(manual));
    BOOST_CHECK_GT(manual, 0);  // both txs actually consumed gas
}

/// Empty-block rejection: preBlockOpSteps with empty rawTxBytes → OpConsensusError (a
/// std::runtime_error subclass). The retired runOpBlockInjection's empty-block guard lives here
/// now.
BOOST_AUTO_TEST_CASE(EmptyBlockRejectedByBlockPreSteps)
{
    namespace op = bcos::evm::opstack;
    namespace engine = bcos::evm::engine;
    namespace detail = bcos::evm::engine::detail;

    // Isthmus-active fork config (feature_op_jovian OFF).
    // Named-lvalue first: configAt takes const OpForkFlags&, and GCC-14's -Wdangling-reference
    // flags passing a prvalue `op::OpForkFlags{}` here even though the returned reference
    // aliases the static config, never the flags (false positive).
    const auto forkFlags = op::OpForkFlags{};
    const auto& cfg = op::configAt(forkFlags);

    MutableStorage storage;
    auto cryptoSuite = makeCryptoSuite();
    auto hashImpl = cryptoSuite->hashImpl();
    auto receiptFactory = makeReceiptFactory();
    bcos::executor_v1::opstack::OpstackExecutor executor{receiptFactory, hashImpl, cfg};

    auto header = makeHeader(1'000'000);

    // Empty deposits/rawTxBytes → "op block: missing L1 attributes deposit (empty block)" →
    // OpConsensusError.
    std::vector<op::DepositTx> deposits;
    std::vector<bcos::bytes> rawTxBytes;
    std::optional<std::string> hashErr;
    std::optional<uint16_t> daFootprintGasScalar;
    std::optional<detail::RecentBlockHashes<MutableStorage>> hashes;
    BOOST_CHECK_THROW(engine::preBlockOpSteps(storage, *header, cfg, rawTxBytes, deposits, executor,
                          hashImpl, hashes, hashErr, daFootprintGasScalar),
        std::runtime_error);
}

BOOST_AUTO_TEST_SUITE_END()
