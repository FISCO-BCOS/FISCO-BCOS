// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// OpBlockVerifierTest — the devp2p sync lane's verify+commit path (opstack-executor/
// OpBlockVerifier.h). Every case assembles a devp2p::sync::Block whose announced header
// commitments come from a probe that runs the verifier's OWN shared stages
// (preBlockOpSteps → SchedulerSerialImpl(serial=true) → finalizeOpBlockResult +
// ledger::mpt::computeMptStateDelta) on a forkCommitted view, so a valid block is equal by
// construction and the mismatch cases flip exactly one announced field.
//
//  1. IsthmusBlockVerifiesAndCommits — deposit + eip1559; six-way + seal-output commitments
//     match; ledger head/SYS rows advance; the p2p block hash keys SYS_NUMBER_2_HASH.
//  2. BedrockBlockVerifiesAndCommits — m_isthmusTime=2000 with the block at second 1010
//     resolves bedrockConfig(); the pre-Ecotone header carries NO fork-gated optionals and
//     the lenient toBlockInfo path (OpBlockExecute.h) executes it.
//  3. StateRootMismatchRejected / GasUsedMismatchRejected — one flipped announced field →
//     OpBlockVerificationFailed (an OpConsensusError) naming the field and both values.
//  4. OutOfOrderAndStaleHeightsRejected — a gap (number = head+2) and a replay of the
//     committed tip both throw OpStaleOrOutOfOrderBlock before any execution.
//  5. BlobAnd0x7dTypeBytesRejected — 0x03 / 0x7d envelope type bytes are deterministic
//     OpConsensusError rejections naming the byte (op-geth admits neither on OP chains).

#include <opstack-executor/OpBlockVerifier.h>
#include <opstack-executor/OpCommitments.h>    // detail::toBcosH256
#include <opstack-executor/OpDepositEncode.h>  // encodeDepositEnvelope

#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-devp2p/sync/Block.h>
#include <bcos-evm/adapter/Storage2State.h>
#include <bcos-evm/eth/state/hash_utils.hpp>
#include <bcos-framework/ledger/FeaturesStorage.h>
#include <bcos-framework/ledger/GenesisConfig.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-ledger/Ledger.h>
#include <bcos-ledger/mpt/HashBuilder.h>
#include <bcos-ledger/mpt/ViewNodeStorage.h>
#include <bcos-tars-protocol/protocol/BlockFactoryImpl.h>
#include <bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/lexical_cast.hpp>
#include <boost/test/unit_test.hpp>
#include <evmc/evmc.hpp>
#include <evmc/hex.hpp>

#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

using bcos::executor_v1::StateKey;
using bcos::executor_v1::StateValue;
using evmc::literals::operator""_address;
using evmc::literals::operator""_bytes32;
namespace memory_storage = bcos::storage2::memory_storage;
namespace op = bcos::evm::opstack;
namespace engine = bcos::evm::engine;
namespace vdetail = bcos::executor_v1::opstack::detail;  // the verifier's inline helpers

namespace
{

constexpr uint64_t kChainId = 0x2105;  // 8453 — matches the eip1559 envelope's chainId
const bcos::Address kSender{"0x7e5f4552091a69125d5dfcb7b8c2659029395bdf"};  // envelope sender

// Corpus isthmus_transfer_basic.json: block.transactions[1]._op_raw (op-geth-signed eip1559
// envelope).
constexpr const char* kEip1559EnvelopeHex =
    "0x02f874822105808405f5e100847735940082520894b0b0000000000000000000000000000000000001880de"
    "0b6b3a764000080c001a0e37533ddb9f696c0b21788f1b00c78adc4a81b1d811d84e70fad672096fc924ea00ae"
    "693f4d68955a4c01ee8bab26f5be740ee416dd2556822f68b747d5aab7714";

// Same minimal CheckpointStorage stub as OpSchedulerTest (anonymous-namespace fixture pieces are
// deliberately not shared across test TUs).
template <class Key, class Value, bcos::storage2::ReadWriteStorage<Key, Value> Storage>
struct TrivialCheckpointStorage
{
    using CheckpointName = bcos::h256;

    Storage& m_storage;
    explicit TrivialCheckpointStorage(Storage& storage) noexcept : m_storage(storage) {}
    Storage& open() & { return m_storage; }
    [[noreturn]] Storage& open(CheckpointName const& /*unused*/) & { std::abort(); }
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
using Verifier = bcos::executor_v1::opstack::OpBlockVerifier<MLS>;

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

/// L1 attributes deposit (isthmus_transfer_basic corpus shape): to==OP_L1_BLOCK &&
/// from==OP_DEPOSITOR, empty data (pre-Jovian forks run no DA-footprint shape checks).
op::DepositTx makeDeposit()
{
    op::DepositTx dep;
    dep.source_hash = 0x6ab967dfdd3aa359031bef6965cca32ed9a21ea969f7aeee2e58817142a645d7_bytes32;
    dep.from = 0xdeaddeaddeaddeaddeaddeaddeaddeaddead0001_address;
    dep.to = 0x4200000000000000000000000000000000000015_address;
    dep.mint = std::nullopt;
    dep.value = intx::uint256{0};
    dep.gas_limit = 0xf4240;
    dep.is_system_tx = false;
    dep.data = {};
    return dep;
}

std::vector<bcos::bytes> corpusTxs()
{
    auto const eipEvmc = evmc::from_hex(kEip1559EnvelopeHex).value();
    return {op::encodeDepositEnvelope(makeDeposit()),
        bcos::bytes(eipEvmc.begin(), eipEvmc.end())};
}

void seedSender(MLS& mls, bcos::Address const& addr, bcos::crypto::Hash::Ptr const& hashImpl)
{
    auto view = mls.fork();
    view.newMutable();
    bcos::ledger::account::EVMAccount account(
        view, addr, bcos::ledger::account::AddressTableMode::Hex);
    bcos::task::syncWait(account.create());
    bcos::task::syncWait(account.setCode({}, {}, hashImpl->emptyHash()));
    bcos::task::syncWait(account.setNonce("0"));
    bcos::task::syncWait(account.setBalance(bcos::u256(1) << 200));
    bcos::task::syncWait(mls.mergeView(std::move(view)));
}

void seedSysTables(MLS& mls)
{
    auto view = mls.fork();
    view.newMutable();
    constexpr std::string_view sysTables[] = {bcos::ledger::SYS_CURRENT_STATE,
        bcos::ledger::SYS_HASH_2_TX, bcos::ledger::SYS_HASH_2_NUMBER,
        bcos::ledger::SYS_NUMBER_2_HASH, bcos::ledger::SYS_NUMBER_2_BLOCK_HEADER,
        bcos::ledger::SYS_NUMBER_2_TXS, bcos::ledger::SYS_HASH_2_RECEIPT,
        bcos::ledger::SYS_BLOCK_NUMBER_2_NONCES};
    for (auto const& table : sysTables)
    {
        bcos::storage::Entry e;
        e.set(std::string(bcos::ledger::SYS_VALUE));
        bcos::task::syncWait(bcos::storage2::writeOne(
            view, StateKey{bcos::ledger::SYS_TABLES, std::string(table)}, std::move(e)));
    }
    bcos::task::syncWait(mls.mergeView(std::move(view)));
}

std::shared_ptr<bcostars::protocol::BlockHeaderImpl> makeGenesisHeader(bcos::h256 stateRoot)
{
    auto h = std::make_shared<bcostars::protocol::BlockHeaderImpl>();
    h->setNumber(0);
    h->setTimestamp(1000000);  // second 1000, internal milliseconds
    h->setParentInfo(bcos::protocol::ParentInfo{.blockNumber = 0, .blockHash = bcos::h256{}});
    h->setCoinbase(bcos::Address{});
    h->setStateRoot(stateRoot);
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

void seedHeadAndGenesisHeader(MLS& mls, bcos::protocol::BlockHeader::Ptr const& genesisHeader)
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

// ── genesis MPT trie (the scenario-B import, verbatim from OpSchedulerTest) ──

bcos::ledger::mpt::TrieBuildResult collectAccountStorageTrie(
    const std::map<evmc::bytes32, evmc::bytes32>& storage)
{
    std::map<bcos::h256, bcos::bytes> entries;
    for (auto const& [key, value] : storage)
    {
        if (evmc::is_zero(value))
            continue;
        bcos::bytes leaf;
        bcos::codec::rlp::encode(leaf,
            bcos::evm::trimmedBigEndian(bcos::bytesConstRef{value.bytes, sizeof(value.bytes)}));
        entries[bcos::h256{evmone::keccak256(key).bytes, 32}] = std::move(leaf);
    }
    return bcos::ledger::mpt::computeTrieRoot(entries);
}

struct CollectedStateRoot
{
    evmone::hash256 root{};
    std::unordered_map<bcos::h256, bcos::bytes> newNodes;
};

template <class Ledger>
CollectedStateRoot collectStateRoot(const Ledger& ledger)
{
    std::map<bcos::h256, bcos::bytes> entries;
    CollectedStateRoot out;
    if (!ledger.visitAccounts([&](const auto& account) {
            auto storageTrie = collectAccountStorageTrie(account.storage);
            out.newNodes.merge(std::move(storageTrie.newNodes));
            evmone::hash256 storageRoot{};
            std::memcpy(storageRoot.bytes, storageTrie.root.data(), sizeof(storageRoot.bytes));
            auto const balanceBe = intx::be::store<evmc::uint256be>(account.balance);
            bcos::bytes leaf;
            bcos::codec::rlp::encode(leaf, account.nonce,
                bcos::evm::trimmedBigEndian(
                    bcos::bytesConstRef{balanceBe.bytes, sizeof(balanceBe.bytes)}),
                bcos::bytesConstRef{storageRoot.bytes, sizeof(storageRoot)},
                bcos::bytesConstRef{account.codeHash.bytes, sizeof(evmc::bytes32)});
            entries[bcos::h256{evmone::keccak256(account.addr).bytes, 32}] = std::move(leaf);
            return true;
        }))
    {
        throw std::runtime_error("collectStateRoot: account traversal incomplete");
    }
    auto result = bcos::ledger::mpt::computeTrieRoot(entries);
    out.newNodes.merge(std::move(result.newNodes));
    std::memcpy(out.root.bytes, result.root.data(), sizeof(out.root.bytes));
    return out;
}

bcos::h256 computeAndPersistGenesisTrie(MLS& mls)
{
    auto view = mls.fork();
    view.newMutable();
    bcos::evm::evmstate::Storage2State<ViewType> bridge(view);
    auto result = collectStateRoot(bridge);
    BOOST_REQUIRE_MESSAGE(
        !bridge.poisoned(), "genesis trie build poisoned: " << std::string(bridge.firstError()));
    bcos::ledger::mpt::ViewNodeStorage<ViewType> nodeStorage(view);
    bcos::task::syncWait(bcos::ledger::mpt::flushTrieNodes(nodeStorage, result.newNodes));
    bcos::task::syncWait(mls.mergeView(std::move(view)));
    return engine::detail::toBcosH256(result.root);
}

struct VerifierFixture
{
    BackendMemStorage backendStorage{1};
    CheckpointBackend checkpointBackend{backendStorage};
    MLS multiLayerStorage{checkpointBackend};
    bcos::protocol::TransactionReceiptFactory::Ptr receiptFactory{
        std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(makeCryptoSuite())};
    bcos::crypto::Hash::Ptr hashImpl{makeCryptoSuite()->hashImpl()};
    bcos::protocol::BlockFactory::Ptr blockFactory{makeBlockFactory()};
    bcos::ledger::OpForkSchedule forkSchedule{};
    std::shared_ptr<bcos::storage::LegacyStorageWrapper<BackendMemStorage>> legacyLedgerStorage;
    std::shared_ptr<bcos::ledger::Ledger> ledger;
    bcos::IOServicePool::Ptr ioServicePool{std::make_shared<bcos::IOServicePool>(1)};
    std::shared_ptr<Verifier> verifier;

    explicit VerifierFixture(bcos::ledger::OpForkSchedule schedule = {})
      : forkSchedule(schedule),
        legacyLedgerStorage(
            std::make_shared<bcos::storage::LegacyStorageWrapper<BackendMemStorage>>(
                backendStorage)),
        ledger(std::make_shared<bcos::ledger::Ledger>(blockFactory, legacyLedgerStorage, 1000)),
        verifier(std::make_shared<Verifier>(receiptFactory, hashImpl, kChainId, forkSchedule,
            blockFactory, multiLayerStorage, ledger, ioServicePool))
    {
        seedSender(multiLayerStorage, kSender, hashImpl);
        seedSysTables(multiLayerStorage);
    }

    /// Ledger head 0 with the real genesis trie root — the incremental MPT build resolves the
    /// genesis nodes persisted here. The OP lane is scenario B by construction
    /// (executor_version >= OPSTACK_EXECUTOR_VERSION), so no feature row is seeded.
    void prepareGenesis()
    {
        auto const genesisRoot = computeAndPersistGenesisTrie(multiLayerStorage);
        seedHeadAndGenesisHeader(multiLayerStorage, makeGenesisHeader(genesisRoot));
    }
};

// ── p2p block assembly ──

/// Non-commitment header fields (corpus isthmus_transfer_basic env). @p isthmusShape selects the
/// fork-gated field presence: Isthmus headers carry withdrawalsHash/blobGasUsed/excessBlobGas/
/// parentBeaconRoot/requestsHash; Bedrock headers carry none of them (baseFee only).
bcos::protocol::EthBlockHeaderData makeEthHeaderBase(
    int64_t number, int64_t timestampSec, bool isthmusShape)
{
    bcos::protocol::EthBlockHeaderData d;
    d.number = number;
    d.timestamp = timestampSec;
    d.parentInfo = bcos::protocol::ParentInfo{.blockNumber = number - 1,
        .blockHash =
            bcos::h256{"0x45daac1c62119a8624509cd80f0b2543f6c78fd21457213af891d8a6d8b14f74"}};
    d.coinbase = bcos::Address{"0x4200000000000000000000000000000000000011"};
    d.prevRandao = bcos::h256{};
    d.gasLimit = bcos::u256(0x989680);  // 10000000 (corpus currentGasLimit)
    d.uncleHash = bcos::protocol::c_emptyOmmersHash;
    d.difficulty = bcos::u256(0);
    d.nonce = bcos::h64{};
    d.baseFee = bcos::u256(0x3a699d00);  // 981000000 (corpus currentBaseFee)
    if (isthmusShape)
    {
        // Header-shape fields (Ecotone+): presence, not value, is decided here;
        // fillCommitments overwrites blobGasUsed with the probed seal value.
        d.blobGasUsed = bcos::u256(0);
        d.excessBlobGas = bcos::u256(0);
        d.parentBeaconRoot =
            bcos::h256{"0x0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b"};
    }
    return d;
}

/// Fill the announced commitment fields from a probe result, preserving the fork's field
/// PRESENCE (an optional stays unset when the probe reports it absent).
void fillCommitments(
    bcos::protocol::EthBlockHeaderData& d, engine::OpBlockCommitments const& c)
{
    d.stateRoot = c.stateRoot;
    d.txsRoot = c.txRoot;
    d.receiptsRoot = c.receiptsRoot;
    std::memcpy(d.logsBloom.data(), c.logsBloom.data(), d.logsBloom.size());
    d.gasUsed = c.gasUsed;
    if (c.withdrawalsRoot)
    {
        d.withdrawalsHash = *c.withdrawalsRoot;
    }
    if (c.blobGasUsed)
    {
        d.blobGasUsed = bcos::u256(*c.blobGasUsed);
    }
    if (c.requestsHash)
    {
        d.requestsHash = *c.requestsHash;
    }
}

/// Drive the verifier's OWN shared stages on a discarded forkCommitted view and return the
/// executed commitments. A valid block's announced header is back-filled from this probe, so the
/// verifier's comparison is equal by construction; the mismatch cases flip one field afterwards.
engine::OpBlockCommitments probeCommitments(VerifierFixture& f,
    bcos::protocol::EthBlockHeaderData const& ethHeader, std::vector<bcos::bytes> const& rawTxBytes)
{
    auto view = f.multiLayerStorage.forkCommitted();
    view.newMutable();
    auto const number = ethHeader.number;
    const auto& cfg = op::configAt(f.forkSchedule, static_cast<uint64_t>(ethHeader.timestamp));
    auto header = vdetail::projectOpP2pHeader(ethHeader, *f.blockFactory);

    bcos::ledger::Features features;
    bcos::task::syncWait(bcos::ledger::readFromStorage(features, view, number));
    bcos::ledger::LedgerConfig execLedgerConfig;
    execLedgerConfig.setBlockNumber(number);
    // Mirror OpBlockVerifier::verifyAndCommit: the executor_version is pinned to the OP lane
    // (computeMptStateDelta's l2Mode and the parent-root rule branch on it).
    execLedgerConfig.setExecutorVersion(bcos::ledger::OPSTACK_EXECUTOR_VERSION);
    execLedgerConfig.setEVMCRevision(cfg.rev);
    execLedgerConfig.setFeatures(features);

    std::vector<bcos::protocol::Transaction::Ptr> transactions;
    std::vector<bcos::bytesConstRef> rawRefs;
    std::vector<op::DepositTx> deposits;
    for (auto const& raw : rawTxBytes)
    {
        auto tx = vdetail::wrapOpP2pEnvelope(raw, *f.hashImpl);
        if (tx->isDepositTx())
        {
            deposits.push_back(
                bcos::executor_v1::opstack::OpstackExecutor::depositFromTransaction(*tx));
        }
        rawRefs.emplace_back(raw.data(), raw.size());
        transactions.push_back(std::move(tx));
    }

    auto sharedError = std::make_shared<bcos::evm::evmstate::SharedErrorSlot>();
    bcos::executor_v1::opstack::OpstackExecutor executor(f.receiptFactory, f.hashImpl, cfg,
        sharedError);
    std::optional<std::string> hashErr;
    std::optional<uint16_t> daFootprintGasScalar;
    std::optional<engine::detail::RecentBlockHashes<ViewType>> hashes;
    engine::preBlockOpSteps(
        view, *header, cfg, rawRefs, deposits, executor, hashes, hashErr, daFootprintGasScalar);

    bcos::executor_v1::opstack::OpBlockExecutionContext ctx{.fee = {},
        .blockGasLeft = engine::detail::narrowU256ToI64(header->gasLimit(), "probe blockGasLeft"),
        .blockHashes = &*hashes,
        .chainId = kChainId,
        .daFootprintGasScalar = daFootprintGasScalar};
    bcos::scheduler_v1::SchedulerSerialImpl serialScheduler(
        f.ioServicePool, /*chunkSize=*/1, /*serial=*/true);
    auto transactionsRefs =
        transactions | ::ranges::views::transform([](bcos::protocol::Transaction::Ptr const& ptr)
                           -> bcos::protocol::Transaction const& { return *ptr; });
    auto receipts = bcos::task::syncWait(serialScheduler.executeBlock(
        view, executor, *header, transactionsRefs, execLedgerConfig, ctx));
    auto opResult = engine::finalizeOpBlockResult(executor, view, *header, execLedgerConfig, cfg,
        receipts, rawRefs, ctx.cumulativeGasUsed, hashErr, /*skipStateRootBuild=*/true);

    auto const parentRoot = bcos::task::syncWait(bcos::ledger::mpt::parentStateRootFor(
        view, bcos::ledger::OPSTACK_EXECUTOR_VERSION, features, number, *f.blockFactory));
    auto delta = bcos::task::syncWait(
        bcos::ledger::mpt::computeMptStateDelta(view, parentRoot, execLedgerConfig, false));
    return engine::commitmentsOf(opResult.seal, delta.stateRoot, opResult.gasUsed, opResult.txRoot);
}

/// Assemble the devp2p block: header commitments filled, hash = keccak256(rlp(header)).
bcos::devp2p::sync::Block makeSyncBlock(
    bcos::protocol::EthBlockHeaderData data, std::vector<bcos::bytes> rawTxBytes)
{
    bcos::devp2p::sync::Block block;
    bcos::codec::rlp::encode(block.headerRlp, data);
    block.header = std::move(data);
    block.hash = bcos::protocol::ethHeaderHash(block.header);
    block.transactions = std::move(rawTxBytes);
    return block;
}

/// Probe → announced header → verifyAndCommit success path. Returns the verifier result.
bcos::executor_v1::opstack::OpBlockVerificationResult verifyOk(VerifierFixture& f,
    std::vector<bcos::bytes> const& rawTxs, int64_t number, int64_t timestampSec,
    bool isthmusShape)
{
    auto data = makeEthHeaderBase(number, timestampSec, isthmusShape);
    auto commitments = probeCommitments(f, data, rawTxs);
    fillCommitments(data, commitments);
    auto block = makeSyncBlock(data, rawTxs);
    auto result = bcos::task::syncWait(f.verifier->verifyAndCommit(block));
    BOOST_REQUIRE_EQUAL(result.blockHash, block.hash);
    BOOST_REQUIRE_EQUAL(result.transactions.size(), rawTxs.size());
    BOOST_REQUIRE_EQUAL(result.receipts.size(), rawTxs.size());
    return result;
}

/// Post-commit ledger assertions: head advanced and the hash rows key on the p2p block hash.
void checkCommitted(VerifierFixture& f, bcos::protocol::BlockNumber number,
    bcos::h256 const& blockHash, std::vector<bcos::bytes> const& rawTxs)
{
    auto const numberStr = boost::lexical_cast<std::string>(number);
    auto view = f.multiLayerStorage.fork();

    auto currentState = bcos::task::syncWait(bcos::storage2::readOne(
        view, StateKey{bcos::ledger::SYS_CURRENT_STATE, bcos::ledger::SYS_KEY_CURRENT_NUMBER}));
    BOOST_REQUIRE_MESSAGE(currentState.has_value(), "SYS_CURRENT_STATE head must advance");
    BOOST_CHECK_EQUAL(std::string(currentState->get()), numberStr);

    auto number2Hash = bcos::task::syncWait(
        bcos::storage2::readOne(view, StateKey{bcos::ledger::SYS_NUMBER_2_HASH, numberStr}));
    BOOST_REQUIRE_MESSAGE(number2Hash.has_value(), "SYS_NUMBER_2_HASH must be written");
    BOOST_CHECK_EQUAL(bcos::toHex(number2Hash->get()), blockHash.hex());

    auto hash2Number = bcos::task::syncWait(bcos::storage2::readOne(view,
        StateKey{bcos::ledger::SYS_HASH_2_NUMBER, bcos::concepts::bytebuffer::toView(blockHash)}));
    BOOST_REQUIRE_MESSAGE(hash2Number.has_value(), "SYS_HASH_2_NUMBER must be written");
    BOOST_CHECK_EQUAL(std::string(hash2Number->get()), numberStr);

    for (auto const& raw : rawTxs)
    {
        auto const txHash = f.hashImpl->hash(bcos::bytesConstRef(raw.data(), raw.size()));
        auto txEntry = bcos::task::syncWait(bcos::storage2::readOne(view,
            StateKey{bcos::ledger::SYS_HASH_2_TX, bcos::concepts::bytebuffer::toView(txHash)}));
        BOOST_REQUIRE_MESSAGE(
            txEntry.has_value(), "SYS_HASH_2_TX must be written for tx " << txHash.hex());
        auto receiptEntry = bcos::task::syncWait(bcos::storage2::readOne(view,
            StateKey{bcos::ledger::SYS_HASH_2_RECEIPT,
                bcos::concepts::bytebuffer::toView(txHash)}));
        BOOST_REQUIRE_MESSAGE(
            receiptEntry.has_value(), "SYS_HASH_2_RECEIPT must be written for tx " << txHash.hex());
    }
}

}  // namespace

BOOST_AUTO_TEST_SUITE(OpBlockVerifierSuite)

BOOST_AUTO_TEST_CASE(IsthmusBlockVerifiesAndCommits)
{
    VerifierFixture f;  // default schedule: Isthmus zero-start baseline
    f.prepareGenesis();

    auto const rawTxs = corpusTxs();
    auto const result = verifyOk(f, rawTxs, /*number=*/1, /*timestampSec=*/1010,
        /*isthmusShape=*/true);

    // Isthmus seal surface: MessagePasser withdrawalsRoot, blobGasUsed 0, sha256("") requestsHash.
    BOOST_REQUIRE(result.commitments.withdrawalsRoot.has_value());
    BOOST_REQUIRE(result.commitments.blobGasUsed.has_value());
    BOOST_CHECK_EQUAL(*result.commitments.blobGasUsed, 0U);
    BOOST_REQUIRE(result.commitments.requestsHash.has_value());
    BOOST_CHECK_EQUAL(result.header->number(), 1);

    checkCommitted(f, 1, result.blockHash, rawTxs);
}

BOOST_AUTO_TEST_CASE(BedrockBlockVerifiesAndCommits)
{
    // isthmus_time=2000 activates the full ladder; a block at second 1010 falls back to Bedrock.
    bcos::ledger::OpForkSchedule schedule{};
    schedule.m_isthmusTime = 2000;
    VerifierFixture f(schedule);
    f.prepareGenesis();

    auto const rawTxs = corpusTxs();
    auto const result = verifyOk(f, rawTxs, /*number=*/1, /*timestampSec=*/1010,
        /*isthmusShape=*/false);

    // Pre-Canyon seal: no withdrawalsRoot / blobGasUsed / requestsHash on either side — presence
    // asymmetry would itself be a mismatch, so equality here proves both sides stayed absent.
    BOOST_CHECK(!result.commitments.withdrawalsRoot.has_value());
    BOOST_CHECK(!result.commitments.blobGasUsed.has_value());
    BOOST_CHECK(!result.commitments.requestsHash.has_value());

    checkCommitted(f, 1, result.blockHash, rawTxs);
}

BOOST_AUTO_TEST_CASE(StateRootMismatchRejected)
{
    VerifierFixture f;
    f.prepareGenesis();

    auto const rawTxs = corpusTxs();
    auto data = makeEthHeaderBase(1, 1010, /*isthmusShape=*/true);
    auto commitments = probeCommitments(f, data, rawTxs);
    fillCommitments(data, commitments);
    // Flip exactly one announced commitment.
    data.stateRoot = bcos::h256{"0x1234"};
    auto const block = makeSyncBlock(data, rawTxs);

    using bcos::executor_v1::opstack::OpBlockVerificationFailed;
    BOOST_CHECK_EXCEPTION(
        bcos::task::syncWait(f.verifier->verifyAndCommit(block)), OpBlockVerificationFailed,
        [](OpBlockVerificationFailed const& e) {
            return e.field == "stateRoot" && !e.computedValue.empty() &&
                   !e.announcedValue.empty();
        });
    // A mismatch is a consensus-level rejection (INVALID), catchable as OpConsensusError.
    BOOST_CHECK_EXCEPTION(bcos::task::syncWait(f.verifier->verifyAndCommit(block)),
        bcos::evm::OpConsensusError, [](bcos::evm::OpConsensusError const&) { return true; });
    // Nothing committed: the head stays at genesis.
    auto view = f.multiLayerStorage.fork();
    auto currentState = bcos::task::syncWait(bcos::storage2::readOne(
        view, StateKey{bcos::ledger::SYS_CURRENT_STATE, bcos::ledger::SYS_KEY_CURRENT_NUMBER}));
    BOOST_REQUIRE(currentState.has_value());
    BOOST_CHECK_EQUAL(std::string(currentState->get()), "0");
}

BOOST_AUTO_TEST_CASE(GasUsedMismatchRejected)
{
    VerifierFixture f;
    f.prepareGenesis();

    auto const rawTxs = corpusTxs();
    auto data = makeEthHeaderBase(1, 1010, /*isthmusShape=*/true);
    auto commitments = probeCommitments(f, data, rawTxs);
    fillCommitments(data, commitments);
    data.gasUsed += 1;
    auto const block = makeSyncBlock(data, rawTxs);

    using bcos::executor_v1::opstack::OpBlockVerificationFailed;
    BOOST_CHECK_EXCEPTION(
        bcos::task::syncWait(f.verifier->verifyAndCommit(block)), OpBlockVerificationFailed,
        [](OpBlockVerificationFailed const& e) { return e.field == "gasUsed"; });
}

BOOST_AUTO_TEST_CASE(OutOfOrderAndStaleHeightsRejected)
{
    VerifierFixture f;
    f.prepareGenesis();

    auto const rawTxs = corpusTxs();

    // Gap: head is 0, a block numbered 2 must be refused before any execution (dummy commitments
    // are never reached — the height guard fires first).
    auto gapBlock = makeSyncBlock(makeEthHeaderBase(2, 1012, /*isthmusShape=*/true), rawTxs);
    BOOST_CHECK_EXCEPTION(bcos::task::syncWait(f.verifier->verifyAndCommit(gapBlock)),
        bcos::executor_v1::opstack::OpStaleOrOutOfOrderBlock,
        [](bcos::executor_v1::opstack::OpStaleOrOutOfOrderBlock const&) { return true; });

    // Commit block 1 for real, then replay it: number == head is stale.
    auto const result = verifyOk(f, rawTxs, 1, 1010, /*isthmusShape=*/true);
    auto data = makeEthHeaderBase(1, 1010, /*isthmusShape=*/true);
    fillCommitments(data, result.commitments);
    auto replayBlock = makeSyncBlock(data, rawTxs);
    BOOST_REQUIRE_EQUAL(replayBlock.hash, result.blockHash);
    BOOST_CHECK_EXCEPTION(bcos::task::syncWait(f.verifier->verifyAndCommit(replayBlock)),
        bcos::executor_v1::opstack::OpStaleOrOutOfOrderBlock,
        [](bcos::executor_v1::opstack::OpStaleOrOutOfOrderBlock const&) { return true; });
}

BOOST_AUTO_TEST_CASE(BlobAnd0x7dTypeBytesRejected)
{
    VerifierFixture f;
    f.prepareGenesis();

    auto const depEnv = op::encodeDepositEnvelope(makeDeposit());
    // Commitment fields are never reached — the envelope type gate throws first — so a base
    // header with a self-consistent hash suffices.
    auto const data = makeEthHeaderBase(1, 1010, /*isthmusShape=*/true);

    auto const blobBlock = makeSyncBlock(data, {depEnv, bcos::bytes{0x03}});
    BOOST_CHECK_EXCEPTION(bcos::task::syncWait(f.verifier->verifyAndCommit(blobBlock)),
        bcos::evm::OpConsensusError,
        [](bcos::evm::OpConsensusError const& e) {
            return std::string(e.what()).find("0x03") != std::string::npos;
        });

    auto const depositRevertBlock = makeSyncBlock(data, {depEnv, bcos::bytes{0x7d}});
    BOOST_CHECK_EXCEPTION(bcos::task::syncWait(f.verifier->verifyAndCommit(depositRevertBlock)),
        bcos::evm::OpConsensusError,
        [](bcos::evm::OpConsensusError const& e) {
            return std::string(e.what()).find("0x7d") != std::string::npos;
        });
}

BOOST_AUTO_TEST_SUITE_END()
