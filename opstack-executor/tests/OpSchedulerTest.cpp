// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// OpSchedulerTest — minimal OP-block + three-way-classification unit tests for wired Task 4
// (the new OpScheduler class).
//
// 1. ExecutesMinimalOpBlockEqualToDirectRouteB: the minimal OP block (deposit + 1 normal with
//    envelope, SEV-8: extraTransactionBytes = full envelope, precedent
//    OpDualPathEquivalenceTest.cpp:566-568) driven via OpScheduler.executeBlock == direct route B
//    (runOpBlockInjection) result (receipts/status/gasUsed + six-field commitment). Corpus anchor:
//    deposit + eip1559 envelope of isthmus_transfer_basic.json (real op-geth signatures); direct
//    anchor = independent route B on the same MLS (runOpBlockInjection; route A executeOpBlock is
//    retired — the dual-path harness proved route B semantically equivalent to it).
// 2. ConsensusRejectionClassifiedAsOpConsensusRejected: the execute hook throws OpConsensusError
//    (unsupported tx type byte 0x03, decodeOneRawTx throws deterministically) → the skeleton's
//    coExecuteBlock classifies via classifyException → Error code == OpConsensusRejected.
// 3. classifyException direct three-way mapping: OpConsensusError→OpConsensusRejected /
//    OpStorageError→OpStorageFault / other→UnknownError.
// 4. CommitPersistsSevenLedgerTables (Task 5c slot-3 E2E): after executeBlock + commitBlock, 7 SYS
//    tables are asserted persisted (SEV-10, replacing the deleted OpBlockScheduler's RefuseStubs).
//
// Golden constraint: minimal OP block receipts/status/gasUsed == direct route B
// (runOpBlockInjection).
#include <opstack-executor/OpScheduler.h>
#include <opstack-executor/OpSchedulerImpl.h>
#include <opstack-executor/OpTxDecode.h>  // detail::canonicalEnvelopeBytes (deposit envelope reconstruction)

#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-rpc/web3jsonrpc/model/Web3Transaction.h>  // for migrated call cases (Task 5c fix round 1)
#include <bcos-tars-protocol/protocol/BlockFactoryImpl.h>
#include <bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/Error.h>
#include <engine/bcos-engine/EngineServiceImpl.h>  // detail::opEnvelopeToTars (tests link engine)
#include <boost/test/unit_test.hpp>
#include <evmc/evmc.hpp>
#include <evmc/hex.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using bcos::executor_v1::StateKey;
using bcos::executor_v1::StateValue;
using evmc::literals::operator""_address;
using evmc::literals::operator""_bytes32;
namespace memory_storage = bcos::storage2::memory_storage;

namespace
{
namespace detail = bcos::evm::engine::detail;

constexpr uint64_t kChainId = 0x2105;  // 8453 — the FISCO OP chain id (vector eip1559 chainId)
const bcos::Address kSender{"0x7e5f4552091a69125d5dfcb7b8c2659029395bdf"};  // eip1559 recovered
                                                                            // sender

// Corpus isthmus_transfer_basic.json: block.transactions[1]._op_raw (op-geth-signed eip1559
// envelope).
constexpr const char* kEip1559EnvelopeHex =
    "0x02f874822105808405f5e100847735940082520894b0b0000000000000000000000000000000000001880de"
    "0b6b3a764000080c001a0e37533ddb9f696c0b21788f1b00c78adc4a81b1d811d84e70fad672096fc924ea00ae"
    "693f4d68955a4c01ee8bab26f5be740ee416dd2556822f68b747d5aab7714";

// Minimal CheckpointStorage stub (same as the source-branch fixture: do not cross-include other
// modules' test-private headers).
template <class Key, class Value, bcos::storage2::ReadWriteStorage<Key, Value> Storage>
struct TrivialCheckpointStorage
{
    using CheckpointName = bcos::h256;

    Storage& m_storage;
    explicit TrivialCheckpointStorage(Storage& storage) noexcept : m_storage(storage) {}
    Storage& open() & { return m_storage; }
    [[noreturn]] Storage& open(CheckpointName const& /*unused*/) &
    {
        std::abort();  // this fixture never needs a historical checkpoint.
    }
    void createCheckpoint(Storage& /*unused*/, CheckpointName const& /*unused*/) {}
    void deleteCheckpoint(CheckpointName const& /*unused*/) {}
    [[nodiscard]] std::optional<CheckpointName> latestCheckpointName() const
    {
        return std::nullopt;
    }
    [[nodiscard]] std::optional<CheckpointName> oldestCheckpointName() const
    {
        return std::nullopt;
    }
};

using MutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;
using BackendMemStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::CONCURRENT),
    std::hash<StateKey>>;
using CheckpointBackend = TrivialCheckpointStorage<StateKey, StateValue, BackendMemStorage>;
using MLS = bcos::storage2::MultiLayerStorage<MutableStorage, void, CheckpointBackend>;
using ViewType = typename MLS::ViewType;

bcos::crypto::CryptoSuite::Ptr makeCryptoSuite()
{
    return std::make_shared<bcos::crypto::CryptoSuite>(
        std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr);
}

bcos::protocol::BlockFactory::Ptr makeBlockFactory()
{
    auto cryptoSuite = makeCryptoSuite();
    auto blockHeaderFactory =
        std::make_shared<bcostars::protocol::BlockHeaderFactoryImpl>(cryptoSuite);
    auto transactionFactory =
        std::make_shared<bcostars::protocol::TransactionFactoryImpl>(cryptoSuite);
    auto receiptFactory =
        std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(cryptoSuite);
    return std::make_shared<bcostars::protocol::BlockFactoryImpl>(
        cryptoSuite, blockHeaderFactory, transactionFactory, receiptFactory);
}

bcos::protocol::TransactionReceiptFactory::Ptr makeReceiptFactory()
{
    return std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(makeCryptoSuite());
}

/// EnvelopeToTarsConverter injected at the composition root: wraps engine's opEnvelopeToTars
/// (tests link engine; production injects a same-shape lambda via Initializer — OpScheduler does
/// not build it in).
bcos::evm::engine::EnvelopeToTarsConverter makeConverter()
{
    return [](bcos::bytes const& env, bcos::crypto::HashType const& txHash) {
        return bcos::engine::detail::opEnvelopeToTars(env, txHash);
    };
}

/// L1 attributes deposit (the isthmus_transfer_basic corpus deposit shape): to==OP_L1_BLOCK &&
/// from==OP_DEPOSITOR satisfies isL1AttributesTx (OpBlockExecute.h:97-99). data is empty — under
/// Isthmus (pre-Jovian) validateJovianBlockShape is a no-op (OpBlockExecute.h:76); processOpBlock
/// classifies isL1AttributesTx purely by content and never validates calldata (precedent: the
/// empty-data deposit in OpBlockInjectorTest.cpp:88-101 passes likewise).
bcos::evm::opstack::DepositTx makeDeposit()
{
    bcos::evm::opstack::DepositTx dep;
    dep.source_hash = 0x6ab967dfdd3aa359031bef6965cca32ed9a21ea969f7aeee2e58817142a645d7_bytes32;
    dep.from = 0xdeaddeaddeaddeaddeaddeaddeaddeaddead0001_address;
    dep.to = 0x4200000000000000000000000000000000000015_address;
    dep.mint = std::nullopt;
    dep.value = intx::uint256{0};
    dep.gas_limit = 0xf4240;  // 1000000 (corpus gas: "0xf4240")
    dep.is_system_tx = false;
    dep.data = {};
    return dep;
}

/// OP header from the corpus environment (isthmus_transfer_basic env). timestamp is stored in
/// milliseconds (FISCO convention; /1000 gives OP seconds). Commitment fields (stateRoot/txsRoot/
/// receiptsRoot/gasUsed/withdrawalsRoot/logsBloom/requestsHash) are back-filled by the caller from
/// the direct route B (runOpBlockInjection) result — the announced header carries the true
/// commitments, so the six-field comparison verifies.
std::shared_ptr<bcostars::protocol::BlockHeaderImpl> makeHeader()
{
    auto h = std::make_shared<bcostars::protocol::BlockHeaderImpl>();
    h->setNumber(1);
    h->setTimestamp(0x3f2 * 1000);  // 0x3f2 = 1010 s (OP seconds) → 1_010_000 ms
    h->setParentInfo(bcos::protocol::ParentInfo{.blockNumber = 0,
        .blockHash =
            bcos::h256{"0x45daac1c62119a8624509cd80f0b2543f6c78fd21457213af891d8a6d8b14f74"}});
    h->setCoinbase(bcos::Address{"0x4200000000000000000000000000000000000011"});
    h->setStateRoot(bcos::h256{});
    h->setTxsRoot(bcos::h256{});
    h->setReceiptsRoot(bcos::h256{});
    h->setGasLimit(bcos::u256(0x989680));  // 10000000 (corpus currentGasLimit)
    h->setGasUsed(bcos::u256(0));
    h->setExtraData(bcos::bytes{});
    h->setPrevRandao(bcos::h256{});
    h->setBaseFee(bcos::u256(0x3a699d00));  // 981000000 (corpus currentBaseFee)
    h->setWithdrawalsRoot(bcos::h256{});
    h->setBlobGasUsed(bcos::u256(0));
    h->setExcessBlobGas(bcos::u256(0));
    h->setParentBeaconBlockRoot(
        bcos::h256{"0x0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b"});
    h->setRequestsHash(bcos::h256{});
    return h;
}

/// Back-fill the announced header's commitment fields from the route B (runOpBlockInjection) result
/// (finishExecute writes the same batch of fields, so verify compares equal).
void fillAnnouncedHeader(bcos::protocol::BlockHeader::Ptr const& header,
    bcos::evm::engine::OpExecuteBlockResult const& result)
{
    header->setStateRoot(result.stateRoot);
    header->setTxsRoot(result.txRoot);
    header->setReceiptsRoot(detail::toBcosH256(result.seal.receiptsRoot));
    header->setGasUsed(bcos::u256(result.gasUsed));
    header->setLogsBloom(bcos::bytesConstRef(result.seal.logsBloom.bytes, 256));
    header->setWithdrawalsRoot(detail::toBcosH256(result.seal.withdrawalsRoot));
    if (result.seal.requestsHash.has_value())
        header->setRequestsHash(detail::toBcosH256(*result.seal.requestsHash));
    if (result.seal.blobGasUsed.has_value())
        header->setBlobGasUsed(bcos::u256(*result.seal.blobGasUsed));
}

/// opEnvelopeToTars + SEV-8 full-envelope override (precedent
/// OpDualPathEquivalenceTest.cpp:566-568): takeToTarsTransaction stores the signing preimage;
/// executeTransaction reads extraTransactionBytes as the full envelope (OpstackExecutor.h:280-281).
bcos::protocol::Transaction::Ptr buildFiscoTx(
    bcos::bytes const& env, bcos::crypto::Hash::Ptr const& hashImpl)
{
    auto txHash = hashImpl->hash(env);
    auto tarsTx = bcos::engine::detail::opEnvelopeToTars(env, txHash);
    if (!tarsTx)
    {
        return nullptr;
    }
    tarsTx->extraTransactionBytes.assign(env.begin(), env.end());
    auto tx = std::make_shared<bcostars::protocol::TransactionImpl>(
        [tars = std::move(*tarsTx)]() mutable { return &tars; });
    return tx;
}

/// A one-byte 0x03 envelope — decodeOneRawTx deterministically throws OpConsensusError
/// ("unsupported tx type byte", OpTxDecode.h:405). Used to reliably drive the execute hook into the
/// consensus-rejected classification (independent of RTTI-boundary runtime behavior).
bcos::protocol::Transaction::Ptr buildUnsupportedTypeTx()
{
    bcostars::Transaction tars;
    tars.extraTransactionBytes.push_back(0x03);
    auto tx = std::make_shared<bcostars::protocol::TransactionImpl>(
        [tars = std::move(tars)]() mutable { return &tars; });
    return tx;
}

/// Seed the eip1559 sender account into the MLS backend (StorageStateView::exists() needs a
/// non-zero codeHash — create() + setCode(empty); a bare setBalance would be deemed non-existent).
void seedSender(MLS& mls, bcos::Address const& addr, bcos::crypto::Hash::Ptr const& hashImpl)
{
    auto view = mls.fork();
    view.newMutable();
    bcos::ledger::account::EVMAccount account(view, addr, /*rawAddress=*/false);
    bcos::task::syncWait(account.create());
    bcos::task::syncWait(account.setCode({}, {}, hashImpl->emptyHash()));
    bcos::task::syncWait(account.setNonce("0"));
    bcos::task::syncWait(account.setBalance(bcos::u256(1) << 200));
    bcos::task::syncWait(mls.mergeView(std::move(view)));
}

/// Test subclass: exposes the receipts of the latest pending execution result (the skeleton's
/// m_results is protected; a derived class can read it).
class TestOpScheduler : public bcos::executor_v1::opstack::OpScheduler<MLS>
{
public:
    using bcos::executor_v1::opstack::OpScheduler<MLS>::OpScheduler;

    std::vector<bcos::protocol::TransactionReceipt::Ptr> lastExecutedReceipts()
    {
        std::unique_lock<std::mutex> lock(this->m_resultsMutex);
        if (this->m_results.empty())
            return {};
        return this->m_results.front()->receipts;
    }
};

struct Fixture
{
    BackendMemStorage backendStorage{1};
    CheckpointBackend checkpointBackend{backendStorage};
    MLS multiLayerStorage{checkpointBackend};
    bcos::protocol::TransactionReceiptFactory::Ptr receiptFactory{makeReceiptFactory()};
    bcos::crypto::Hash::Ptr hashImpl{makeCryptoSuite()->hashImpl()};
    bcos::protocol::BlockFactory::Ptr blockFactory{makeBlockFactory()};
    bcos::evm::opstack::OpForkTimestamps forkTimestamps{.isthmusTime = 0,
        .jovianTime = std::numeric_limits<uint64_t>::max()};
    std::shared_ptr<TestOpScheduler> scheduler;

    Fixture()
      : scheduler(std::make_shared<TestOpScheduler>(receiptFactory, hashImpl, kChainId,
            forkTimestamps, blockFactory, multiLayerStorage, makeConverter()))
    {
        seedSender(multiLayerStorage, kSender, hashImpl);
    }
};

// ── call/getCode case migration (Task 5c fix round 1, verbatim from the deleted
//    OpBlockSchedulerTest) ──
// Once OpScheduler absorbed OpBlockScheduler's call/getCode pure-virtual implementations, the
// original OpBlockSchedulerTest RPC-face cases (StatusAndResetNoOp / GetCodeEmpty /
// CallInvalidReturnsError / CallHappyPathInjectsRealBaseFee) moved into this suite, with the driven
// object changed to OpScheduler (f.scheduler).
const bcos::Address kCallSender{"0x1000000000000000000000000000000000000000"};

/// A genesis header carrying every field coCallLatest's buildOpBlockInfo/toBlockInfo reads
/// (baseFee=1e9 is the target value of CallHappyPath's injection assertion).
std::shared_ptr<bcostars::protocol::BlockHeaderImpl> makeCallGenesisHeader()
{
    auto h = std::make_shared<bcostars::protocol::BlockHeaderImpl>();
    h->setNumber(0);
    h->setTimestamp(1000000);
    h->setParentInfo(bcos::protocol::ParentInfo{.blockNumber = 0, .blockHash = bcos::h256{}});
    h->setCoinbase(bcos::Address{});
    h->setStateRoot(bcos::h256{});
    h->setTxsRoot(bcos::h256{});
    h->setReceiptsRoot(bcos::h256{});
    h->setGasLimit(bcos::u256(30000000));
    h->setGasUsed(bcos::u256(0));
    h->setExtraData(bcos::bytes{});
    h->setPrevRandao(bcos::h256{});
    h->setBaseFee(bcos::u256(1000000000));
    h->setWithdrawalsRoot(bcos::h256{});
    h->setBlobGasUsed(bcos::u256(0));
    h->setExcessBlobGas(bcos::u256(0));
    h->setParentBeaconBlockRoot(bcos::h256{});
    h->setRequestsHash(bcos::h256{});
    return h;
}

/// Seed the minimal OP ledger the RPC call()/read paths need: current head (SYS_CURRENT_STATE)
/// and the block-0 header (getBlockData(HEADER) reads SYS_NUMBER_2_BLOCK_HEADER by number).
void seedCallGenesis(MLS& mls, bcos::protocol::BlockHeader::Ptr const& genesisHeader)
{
    auto view = mls.fork();
    view.newMutable();
    {
        bcos::storage::Entry e;
        e.set(boost::lexical_cast<std::string>(genesisHeader->number()));
        bcos::task::syncWait(bcos::storage2::writeOne(view,
            StateKey{bcos::ledger::SYS_CURRENT_STATE, bcos::ledger::SYS_KEY_CURRENT_NUMBER},
            std::move(e)));
    }
    {
        bcos::bytes buf;
        genesisHeader->encode(buf);
        bcos::storage::Entry e;
        e.set(std::move(buf));
        bcos::task::syncWait(bcos::storage2::writeOne(view,
            StateKey{bcos::ledger::SYS_NUMBER_2_BLOCK_HEADER,
                boost::lexical_cast<std::string>(genesisHeader->number())},
            std::move(e)));
    }
    bcos::task::syncWait(mls.mergeView(std::move(view)));
}

/// Build an EIP-1559 (type 2) web3 tx wrapped as a tars Transaction::Ptr (lambda-holder form).
/// EIP-1559 is deliberate (round-3 C2): an EIP-2930/legacy tx with maxPriorityFeePerGas=0 would
/// trigger BCOS2Evmone's access_list override (max_priority=max_gas), making effectiveGasPrice =
/// maxFeePerGas instead of baseFee — a false positive; EIP-1559 keeps max_priority=0 so
/// effectiveGasPrice == baseFee exactly (injection-sensitive).
bcos::protocol::Transaction::Ptr buildWeb3Tx(
    bcos::u256 maxFeePerGas, bcos::u256 maxPriorityFeePerGas)
{
    bcos::rpc::Web3Transaction w3{};
    w3.type = bcos::rpc::TransactionType::EIP1559;
    w3.chainId = 5;
    w3.nonce = 0;
    w3.maxFeePerGas = maxFeePerGas;
    w3.maxPriorityFeePerGas = maxPriorityFeePerGas;
    w3.gasLimit = 5000000;
    w3.to = bcos::Address("0x811a752c8cd697e3cb27279c330ed1ada745a8d7");
    w3.value = bcos::u256(0);
    w3.signatureV = 0;
    w3.signatureR = bcos::bytes(32, 0x01);
    w3.signatureS = bcos::bytes(32, 0x02);
    auto tarsHolder = std::make_shared<bcostars::Transaction>(w3.takeToTarsTransaction());
    auto const txHash = w3.txHash();
    tarsHolder->extraTransactionHash.assign(txHash.begin(), txHash.end());
    auto tx = std::make_shared<bcostars::protocol::TransactionImpl>(
        [tarsHolder]() { return tarsHolder.get(); });
    // The dummy r/s cannot recover a sender; force it so opValidate sees the funded account.
    tx->clearSenderAndHash();
    tx->forceSender(kCallSender.asBytes());
    return tx;
}

/// Fund an EOA so opValidate passes (same create()+setCode(empty) existence pattern as seedSender).
void fundCallAccount(MLS& mls, bcos::Address const& addr, bcos::crypto::Hash::Ptr const& hashImpl,
    bcos::u256 const& balance)
{
    auto view = mls.fork();
    view.newMutable();
    bcos::ledger::account::EVMAccount account(view, addr, /*rawAddress=*/false);
    bcos::task::syncWait(account.create());
    bcos::task::syncWait(account.setCode({}, {}, hashImpl->emptyHash()));
    bcos::task::syncWait(account.setNonce("0"));
    bcos::task::syncWait(account.setBalance(balance));
    bcos::task::syncWait(mls.mergeView(std::move(view)));
}

/// route B direct-call anchor (after route A executeOpBlock retired, the independent execution
/// comparison moved to runOpBlockInjection): assemble txs/normalTxs + OpstackExecutor then call
/// directly, returning OpExecuteBlockResult. Assembly matches OpScheduler's execute hook
/// (OpScheduler.h:127-179) / OpDualPathEquivalenceTest route B (:1015-1034) (no copy drift).
bcos::evm::engine::OpExecuteBlockResult runRouteBDirect(Fixture& f, ViewType& view,
    bcos::protocol::BlockHeader const& header, std::vector<bcos::bytes> const& rawTxBytes)
{
    namespace op = bcos::evm::opstack;
    const auto& cfg =
        op::configAt(static_cast<uint64_t>(header.timestamp()) / 1000, f.forkTimestamps);
    std::vector<op::OpBlockTx> txs;
    txs.reserve(rawTxBytes.size());
    for (auto const& raw : rawTxBytes)
        txs.push_back(detail::decodeOneRawTx(raw, kChainId));
    std::vector<bcos::protocol::Transaction::Ptr> normalTxs;
    normalTxs.reserve(rawTxBytes.size());
    for (std::size_t i = 0; i < rawTxBytes.size(); ++i)
    {
        if (std::holds_alternative<op::DepositTx>(txs[i].tx))
            continue;
        const auto txHash = f.hashImpl->hash(rawTxBytes[i]);
        auto tarsTx = makeConverter()(rawTxBytes[i], txHash);
        if (!tarsTx)
            throw std::runtime_error("route B direct: envelope failed opEnvelopeToTars");
        tarsTx->extraTransactionBytes.assign(rawTxBytes[i].begin(), rawTxBytes[i].end());
        normalTxs.push_back(std::make_shared<bcostars::protocol::TransactionImpl>(
            [tarsTx = std::move(*tarsTx)]() mutable { return &tarsTx; }));
    }
    bcos::ledger::LedgerConfig ledgerConfig;
    ledgerConfig.setEVMCRevision(cfg.rev);
    bcos::executor_v1::opstack::OpstackExecutor executor{f.receiptFactory, f.hashImpl, cfg};
    return bcos::evm::engine::runOpBlockInjection(executor, view, header, txs, normalTxs, cfg,
        kChainId, ledgerConfig, rawTxBytes, f.hashImpl);
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OpSchedulerSuite)

/// Golden constraint: minimal OP block (deposit + 1 normal with envelope) via OpScheduler == direct
/// route B.
BOOST_AUTO_TEST_CASE(ExecutesMinimalOpBlockEqualToDirectRouteB)
{
    Fixture f;

    // Envelopes: deposit rebuilt via canonicalEnvelopeBytes (0x7e || rlp([...8 fields]),
    // OpTxDecode.h:307); normal uses the corpus's real op-geth-signed envelope. Both pass through
    // decodeOneRawTx's canonical round-trip.
    auto depTx = makeDeposit();
    bcos::bytes depEnv = detail::canonicalEnvelopeBytes(bcos::evm::opstack::OpBlockTx{depTx, {}});
    auto eipEvmcBytes = evmc::from_hex(kEip1559EnvelopeHex).value();
    bcos::bytes eipEnvBytes(eipEvmcBytes.begin(), eipEvmcBytes.end());
    std::vector<bcos::bytes> rawTxBytes{depEnv, eipEnvBytes};

    auto header = makeHeader();

    // Direct anchor: independent route B (runOpBlockInjection) on the same MLS, same header
    // environment. (Route A executeOpBlock is retired — the dual-path harness proved route B
    // semantically equivalent to it.)
    auto viewA = f.multiLayerStorage.fork();
    viewA.newMutable();
    bcos::evm::engine::OpExecuteBlockResult resultA;
    try
    {
        resultA = runRouteBDirect(f, viewA, *header, rawTxBytes);
    }
    catch (const std::exception& e)
    {
        BOOST_ERROR("direct runOpBlockInjection threw: " << e.what());
        return;
    }
    catch (...)
    {
        BOOST_ERROR("direct runOpBlockInjection threw an unknown (RTTI-bypassed) exception");
        return;
    }

    BOOST_REQUIRE_EQUAL(resultA.receipts.size(), rawTxBytes.size());
    BOOST_CHECK_GT(resultA.gasUsed, 0);

    // Back-fill the announced header with the direct-call commitments (SYS_NUMBER_2_BLOCK_HEADER
    // is not written — the execute path doesn't write the header table, only compares).
    fillAnnouncedHeader(header, resultA);

    // Block assembly: extraTransactionBytes = full envelope (SEV-8).
    auto block = f.blockFactory->createBlock();
    block->setBlockHeader(header);
    auto depFiscoTx = buildFiscoTx(depEnv, f.hashImpl);
    auto eipFiscoTx = buildFiscoTx(eipEnvBytes, f.hashImpl);
    BOOST_REQUIRE(depFiscoTx != nullptr);
    BOOST_REQUIRE(eipFiscoTx != nullptr);
    block->appendTransaction(depFiscoTx);
    block->appendTransaction(eipFiscoTx);

    // Drive via OpScheduler.
    bcos::Error::Ptr err;
    bcos::protocol::BlockHeader::Ptr executedHeader;
    bool sysBlock = false;
    bool called = false;
    f.scheduler->executeBlock(block, /*verify=*/true,
        [&](bcos::Error::Ptr e, bcos::protocol::BlockHeader::Ptr h, bool s) {
            called = true;
            err = std::move(e);
            executedHeader = std::move(h);
            sysBlock = s;
        });
    BOOST_REQUIRE(called);
    BOOST_REQUIRE_MESSAGE(
        err == nullptr, "executeBlock failed: " << (err ? err->errorMessage() : ""));
    BOOST_REQUIRE(executedHeader != nullptr);
    BOOST_CHECK(!sysBlock);

    // Six-field commitments == direct call.
    BOOST_CHECK_EQUAL(executedHeader->stateRoot(), resultA.stateRoot);
    BOOST_CHECK_EQUAL(executedHeader->txsRoot(), resultA.txRoot);
    BOOST_CHECK_EQUAL(
        executedHeader->receiptsRoot(), detail::toBcosH256(resultA.seal.receiptsRoot));
    BOOST_CHECK_EQUAL(executedHeader->gasUsed(), bcos::u256(resultA.gasUsed));
    BOOST_CHECK_EQUAL(executedHeader->withdrawalsRoot().value_or(bcos::h256{}),
        detail::toBcosH256(resultA.seal.withdrawalsRoot));
    {
        auto gotBloom = executedHeader->logsBloom();
        BOOST_REQUIRE_EQUAL(gotBloom.size(), 256u);
        BOOST_CHECK(
            std::equal(gotBloom.begin(), gotBloom.end(), std::begin(resultA.seal.logsBloom.bytes)));
    }
    if (resultA.seal.requestsHash.has_value())
    {
        BOOST_REQUIRE(executedHeader->requestsHash().has_value());
        BOOST_CHECK_EQUAL(
            executedHeader->requestsHash().value(), detail::toBcosH256(*resultA.seal.requestsHash));
    }

    // Per-tx receipts/status/gasUsed == direct call.
    auto receipts = f.scheduler->lastExecutedReceipts();
    BOOST_REQUIRE_EQUAL(receipts.size(), resultA.receipts.size());
    for (std::size_t i = 0; i < receipts.size(); ++i)
    {
        BOOST_CHECK_EQUAL(receipts[i]->status(), resultA.receipts[i]->status());
        BOOST_CHECK_EQUAL(receipts[i]->gasUsed(), resultA.receipts[i]->gasUsed());
        BOOST_CHECK_EQUAL(std::string(receipts[i]->cumulativeGasUsed()),
            std::string(resultA.receipts[i]->cumulativeGasUsed()));
        BOOST_CHECK_EQUAL(std::string(receipts[i]->effectiveGasPrice()),
            std::string(resultA.receipts[i]->effectiveGasPrice()));
    }

    // Task 6 (P4 M3) golden constraint: executeBlock's (skeleton-driven, execute hook =
    // runOpBlockInjection route B) full result == direct runOpBlockInjection (route B) —
    // peekExecuteResult exposes the raw result stashed in the skeleton's m_results.
    auto routeB = f.scheduler->peekExecuteResult();
    BOOST_REQUIRE_MESSAGE(routeB.has_value(), "executeBlock must stash an OpExecuteBlockResult");
    BOOST_CHECK_EQUAL(routeB->stateRoot, resultA.stateRoot);
    BOOST_CHECK_EQUAL(routeB->txRoot, resultA.txRoot);
    BOOST_CHECK_EQUAL(routeB->gasUsed, resultA.gasUsed);
    // evmc::bytes32 / BloomFilter have no operator<<, so compare with == / byte-wise
    // (BOOST_CHECK_EQUAL is unusable).
    BOOST_CHECK(routeB->seal.receiptsRoot == resultA.seal.receiptsRoot);
    BOOST_CHECK(std::equal(std::begin(routeB->seal.logsBloom.bytes),
        std::end(routeB->seal.logsBloom.bytes), std::begin(resultA.seal.logsBloom.bytes)));
    BOOST_CHECK(routeB->seal.withdrawalsRoot == resultA.seal.withdrawalsRoot);
    BOOST_CHECK_EQUAL(routeB->seal.requestsHash.has_value(), resultA.seal.requestsHash.has_value());
    if (routeB->seal.requestsHash.has_value() && resultA.seal.requestsHash.has_value())
        BOOST_CHECK(*routeB->seal.requestsHash == *resultA.seal.requestsHash);  // evmc::bytes32 has
                                                                                // no
                                                                                // <<
    BOOST_CHECK_EQUAL(routeB->seal.blobGasUsed.has_value(), resultA.seal.blobGasUsed.has_value());
    if (routeB->seal.blobGasUsed.has_value() && resultA.seal.blobGasUsed.has_value())
        BOOST_CHECK_EQUAL(*routeB->seal.blobGasUsed, *resultA.seal.blobGasUsed);
}

/// Wires Task 5c slot-3 E2E (SEV-10): after OpScheduler executeBlock + commitBlock, 7 SYS tables
/// are persisted (SYS_NUMBER_2_HASH / SYS_HASH_2_NUMBER / SYS_NUMBER_2_BLOCK_HEADER /
/// SYS_CURRENT_STATE / SYS_NUMBER_2_TXS / SYS_HASH_2_RECEIPT / SYS_HASH_2_TX). Replaces the
/// deleted OpBlockScheduler's RefuseStubs (OP block execution/commit is no longer a refused stub
/// but is genuinely persisted via OpScheduler).
BOOST_AUTO_TEST_CASE(CommitPersistsSevenLedgerTables)
{
    Fixture f;

    // Corpus envelopes (same as ExecutesMinimalOpBlockEqualToDirectRouteB): deposit + eip1559.
    auto depTx = makeDeposit();
    bcos::bytes depEnv = detail::canonicalEnvelopeBytes(bcos::evm::opstack::OpBlockTx{depTx, {}});
    auto eipEvmcBytes = evmc::from_hex(kEip1559EnvelopeHex).value();
    bcos::bytes eipEnvBytes(eipEvmcBytes.begin(), eipEvmcBytes.end());
    std::vector<bcos::bytes> rawTxBytes{depEnv, eipEnvBytes};

    auto header = makeHeader();

    // Direct route B yields the real commitments; back-fill the announced header (so verify's
    // six-field comparison passes). (Route A executeOpBlock is retired — the dual-path harness
    // proved route B semantically equivalent to it.)
    auto viewA = f.multiLayerStorage.fork();
    viewA.newMutable();
    bcos::evm::engine::OpExecuteBlockResult resultA =
        runRouteBDirect(f, viewA, *header, rawTxBytes);
    BOOST_REQUIRE_EQUAL(resultA.receipts.size(), rawTxBytes.size());
    fillAnnouncedHeader(header, resultA);

    // Block assembly (SEV-8 full envelope).
    auto block = f.blockFactory->createBlock();
    block->setBlockHeader(header);
    auto depFiscoTx = buildFiscoTx(depEnv, f.hashImpl);
    auto eipFiscoTx = buildFiscoTx(eipEnvBytes, f.hashImpl);
    BOOST_REQUIRE(depFiscoTx != nullptr);
    BOOST_REQUIRE(eipFiscoTx != nullptr);
    block->appendTransaction(depFiscoTx);
    block->appendTransaction(eipFiscoTx);

    // executeBlock → commitBlock (slot-3 driving).
    bcos::Error::Ptr execErr;
    bcos::protocol::BlockHeader::Ptr executedHeader;
    bool called = false;
    f.scheduler->executeBlock(
        block, /*verify=*/true, [&](bcos::Error::Ptr e, bcos::protocol::BlockHeader::Ptr h, bool) {
            called = true;
            execErr = std::move(e);
            executedHeader = std::move(h);
        });
    BOOST_REQUIRE(called);
    BOOST_REQUIRE_MESSAGE(
        execErr == nullptr, "executeBlock failed: " << (execErr ? execErr->errorMessage() : ""));
    BOOST_REQUIRE(executedHeader != nullptr);

    bcos::Error::Ptr commitErr;
    called = false;
    f.scheduler->commitBlock(
        executedHeader, [&](bcos::Error::Ptr e, bcos::ledger::LedgerConfig::Ptr) {
            called = true;
            commitErr = std::move(e);
        });
    BOOST_REQUIRE(called);
    BOOST_REQUIRE_MESSAGE(commitErr == nullptr,
        "commitBlock failed: " << (commitErr ? commitErr->errorMessage() : ""));

    // ── 7-table persistence assertions ──
    auto const blockNumberStr = boost::lexical_cast<std::string>(header->number());
    auto& hashImpl = *f.blockFactory->cryptoSuite()->hashImpl();
    auto view = f.multiLayerStorage.fork();
    const auto expectedBlockHash = header->opHeaderHash(bcos::engine::detail::opHeaderConst());

    // 1. SYS_NUMBER_2_HASH[number] = blockHash (announced header opHeaderHash, commit hook key).
    auto number2Hash = bcos::task::syncWait(
        bcos::storage2::readOne(view, StateKey{bcos::ledger::SYS_NUMBER_2_HASH, blockNumberStr}));
    BOOST_REQUIRE_MESSAGE(number2Hash.has_value(), "SYS_NUMBER_2_HASH must be written");
    {
        auto const& stored = number2Hash->get();
        BOOST_REQUIRE_EQUAL(stored.size(), size_t(32));
        // Compare as hex: the entry stores raw bytes as char (signed on AppleClang), so a
        // byte-wise std::equal against h256's unsigned bytes fails for high bytes (>= 0x80).
        BOOST_CHECK_EQUAL(bcos::toHex(stored), expectedBlockHash.hex());
    }

    // 2. SYS_HASH_2_NUMBER[blockHash] = number.
    auto hash2Number = bcos::task::syncWait(
        bcos::storage2::readOne(view, StateKey{bcos::ledger::SYS_HASH_2_NUMBER,
                                          bcos::concepts::bytebuffer::toView(expectedBlockHash)}));
    BOOST_REQUIRE_MESSAGE(hash2Number.has_value(), "SYS_HASH_2_NUMBER must be written");
    BOOST_CHECK_EQUAL(std::string(hash2Number->get()), blockNumberStr);

    // 3. SYS_NUMBER_2_BLOCK_HEADER[number] = tars header (non-empty).
    auto headerEntry = bcos::task::syncWait(bcos::storage2::readOne(
        view, StateKey{bcos::ledger::SYS_NUMBER_2_BLOCK_HEADER, blockNumberStr}));
    BOOST_REQUIRE_MESSAGE(headerEntry.has_value(), "SYS_NUMBER_2_BLOCK_HEADER must be written");
    BOOST_CHECK(!headerEntry->get().empty());

    // 4. SYS_CURRENT_STATE[SYS_KEY_CURRENT_NUMBER] = number (head advanced).
    auto currentState = bcos::task::syncWait(bcos::storage2::readOne(
        view, StateKey{bcos::ledger::SYS_CURRENT_STATE, bcos::ledger::SYS_KEY_CURRENT_NUMBER}));
    BOOST_REQUIRE_MESSAGE(currentState.has_value(), "SYS_CURRENT_STATE head must advance");
    BOOST_CHECK_EQUAL(std::string(currentState->get()), blockNumberStr);

    // 5. SYS_NUMBER_2_TXS[number] = tx metadata (SEV-10's 7th table).
    auto number2Txs = bcos::task::syncWait(
        bcos::storage2::readOne(view, StateKey{bcos::ledger::SYS_NUMBER_2_TXS, blockNumberStr}));
    BOOST_REQUIRE_MESSAGE(number2Txs.has_value(), "SYS_NUMBER_2_TXS (SEV-10) must be written");
    BOOST_CHECK(!number2Txs->get().empty());

    // 6/7. SYS_HASH_2_RECEIPT + SYS_HASH_2_TX per tx.
    for (auto const& env : rawTxBytes)
    {
        const auto txHash = hashImpl.hash(env);
        auto receiptEntry = bcos::task::syncWait(
            bcos::storage2::readOne(view, StateKey{bcos::ledger::SYS_HASH_2_RECEIPT,
                                              bcos::concepts::bytebuffer::toView(txHash)}));
        BOOST_REQUIRE_MESSAGE(
            receiptEntry.has_value(), "SYS_HASH_2_RECEIPT must be written for tx " << txHash.hex());
        auto txEntry = bcos::task::syncWait(bcos::storage2::readOne(view,
            StateKey{bcos::ledger::SYS_HASH_2_TX, bcos::concepts::bytebuffer::toView(txHash)}));
        BOOST_REQUIRE_MESSAGE(
            txEntry.has_value(), "SYS_HASH_2_TX must be written for tx " << txHash.hex());
    }
}

// ── RPC-face case migration (Task 5c fix round 1, verbatim from the deleted OpBlockSchedulerTest;
//    driven object changed to OpScheduler — f.scheduler is TestOpScheduler<OpScheduler>;
//    call/getCode/status/reset inherited) ──

/// Skeleton defaults to no-op status/reset (same semantics as OpBlockScheduler).
BOOST_AUTO_TEST_CASE(StatusAndResetNoOp)
{
    Fixture f;
    f.scheduler->status([&](bcos::Error::Ptr err, bcos::protocol::Session::ConstPtr) {
        BOOST_REQUIRE(err == nullptr);
    });
    f.scheduler->reset([&](bcos::Error::Ptr err) { BOOST_REQUIRE(err == nullptr); });
}

/// Unknown address → empty code, no error (getCode only reads features, never calls
/// getLedgerConfig; the OP header's empty dataHash doesn't touch BlockHeader::hash()).
BOOST_AUTO_TEST_CASE(GetCodeEmpty)
{
    Fixture f;
    seedCallGenesis(f.multiLayerStorage, makeCallGenesisHeader());
    bool called = false;
    f.scheduler->getCode(
        "0x0000000000000000000000000000000000000001", [&](bcos::Error::Ptr err, bcos::bytes code) {
            called = true;
            BOOST_REQUIRE(err == nullptr);
            BOOST_REQUIRE(code.empty());
        });
    BOOST_REQUIRE(called);
}

/// Invalid call (maxFeePerGas=1 < baseFee(1e9) trips evmone validate FEE_CAP_LESS_THAN_BLOCKS; an
/// unfunded sender also trips the balance check) → JSON-RPC Error, never a status-0 receipt.
BOOST_AUTO_TEST_CASE(CallInvalidReturnsError)
{
    Fixture f;
    seedCallGenesis(f.multiLayerStorage, makeCallGenesisHeader());
    auto tx = buildWeb3Tx(/*maxFeePerGas=*/1, /*maxPriorityFeePerGas=*/0);
    bool called = false;
    f.scheduler->call(
        std::move(tx), [&](bcos::Error::Ptr err, bcos::protocol::TransactionReceipt::Ptr) {
            called = true;
            BOOST_REQUIRE(err != nullptr);  // Error (JSON-RPC), never a status-0 receipt
        });
    BOOST_REQUIRE(called);
}

/// Scheduler-level call-chain (OpScheduler::call → coCallLatest → buildOpBlockInfo) baseFee
/// injection cross-check (Task 5/dual-path #34): maxPriorityFeePerGas=0 (EIP-1559, BCOS2Evmone
/// access_list override not triggered) → effectiveGasPrice == base_fee + min(0, maxFee-base_fee) ==
/// base_fee exactly. Pre-fix buildOpBlockInfo injected base_fee=0 → egp "0x0"; post-fix the header
/// baseFee(1e9) shines through — proving Task 4's buildOpBlockInfo baseFee fix takes effect on the
/// scheduler-level call chain.
BOOST_AUTO_TEST_CASE(CallHappyPathInjectsRealBaseFee)
{
    Fixture f;
    seedCallGenesis(f.multiLayerStorage, makeCallGenesisHeader());
    fundCallAccount(f.multiLayerStorage, kCallSender, f.hashImpl, bcos::u256(1) << 200);

    auto tx = buildWeb3Tx(
        /*maxFeePerGas=*/bcos::u256(30'000'000'000ULL), /*maxPriorityFeePerGas=*/0);

    bcos::protocol::TransactionReceipt::Ptr got;
    bcos::Error::Ptr err;
    f.scheduler->call(
        std::move(tx), [&](bcos::Error::Ptr e, bcos::protocol::TransactionReceipt::Ptr r) {
            err = std::move(e);
            got = std::move(r);
        });
    BOOST_REQUIRE_MESSAGE(
        err == nullptr, "eth_call must succeed, got error: " << (err ? err->errorMessage() : ""));
    BOOST_REQUIRE(got != nullptr);
    // effectiveGasPrice is a hex string ("0x...", TransactionReceipt.h:75). Parse to u256 and
    // assert
    // == header baseFee(1e9) — exact (EIP-1559 + maxPriority=0 → effectiveGasPrice == baseFee).
    const auto egp = bcos::u256(std::string(got->effectiveGasPrice()));
    const auto baseFee = bcos::u256(1'000'000'000);
    BOOST_CHECK_MESSAGE(
        egp == baseFee, "effectiveGasPrice " << egp << " must equal header baseFee " << baseFee);
}

/// execute hook throws OpConsensusError (the 0x03 envelope is deterministically thrown by
/// decodeOneRawTx) → skeleton classify → Error code == OpConsensusRejected.
BOOST_AUTO_TEST_CASE(ConsensusRejectionClassifiedAsOpConsensusRejected)
{
    Fixture f;

    auto block = f.blockFactory->createBlock();
    block->setBlockHeader(makeHeader());
    auto badTx = buildUnsupportedTypeTx();
    BOOST_REQUIRE(badTx != nullptr);
    block->appendTransaction(badTx);

    bcos::Error::Ptr err;
    bcos::protocol::BlockHeader::Ptr executedHeader;
    bool called = false;
    f.scheduler->executeBlock(
        block, /*verify=*/true, [&](bcos::Error::Ptr e, bcos::protocol::BlockHeader::Ptr h, bool) {
            called = true;
            err = std::move(e);
            executedHeader = std::move(h);
        });
    BOOST_REQUIRE(called);
    BOOST_REQUIRE(err != nullptr);
    BOOST_CHECK_EQUAL(err->errorCode(), (int)bcos::scheduler::SchedulerError::OpConsensusRejected);
    BOOST_CHECK(executedHeader == nullptr);
}

/// Three-way classification direct call: OpConsensusError→OpConsensusRejected /
/// OpStorageError→OpStorageFault / other→Unknown.
BOOST_AUTO_TEST_CASE(ClassifyExceptionThreeWayMapping)
{
    Fixture f;

    auto consensus = f.scheduler->classifyException(
        std::make_exception_ptr(bcos::evm::engine::OpConsensusError{"block-level consensus"}));
    BOOST_CHECK_EQUAL(consensus, bcos::scheduler::SchedulerError::OpConsensusRejected);

    auto storage = f.scheduler->classifyException(
        std::make_exception_ptr(bcos::evm::engine::OpStorageError{"ledger bridge poison"}));
    BOOST_CHECK_EQUAL(storage, bcos::scheduler::SchedulerError::OpStorageFault);

    auto unknown = f.scheduler->classifyException(
        std::make_exception_ptr(std::runtime_error{"generic ethereum-mode fault"}));
    BOOST_CHECK_EQUAL(unknown, bcos::scheduler::SchedulerError::UnknownError);
}

BOOST_AUTO_TEST_SUITE_END()
