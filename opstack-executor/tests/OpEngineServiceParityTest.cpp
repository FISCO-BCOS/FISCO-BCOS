// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

#include "engine/bcos-engine/EngineServiceImpl.h"
#include "engine/bcos-engine/OpEngineService.h"
#include "support/GoldenSample.h"
#include "support/SeedPreState.h"

#include <bcos-framework/engine/EngineService.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/protocol/TransactionFactory.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-ledger/Ledger.h>
#include <bcos-rpc/web3jsonrpc/utils/EngineHelper.h>
#include <bcos-table/src/LegacyStorageWrapper.h>
#include <bcos-tars-protocol/protocol/BlockFactoryImpl.h>
#include <bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/IOServicePool.h>
#include <opstack-executor/OpScheduler.h>
#include <opstack-executor/OpSchedulerSeam.h>
#include <boost/lexical_cast.hpp>
#include <boost/test/unit_test.hpp>

using bcos::executor_v1::StateKey;
using bcos::executor_v1::StateValue;
namespace memory_storage = bcos::storage2::memory_storage;

namespace op_engine_parity_test
{

template <class Key, class Value, bcos::storage2::ReadWriteStorage<Key, Value> Storage>
struct TrivialCheckpointStorage
{
    using CheckpointName = bcos::h256;
    Storage& m_storage;
    explicit TrivialCheckpointStorage(Storage& storage) noexcept : m_storage(storage) {}
    Storage& open() & { return m_storage; }
    [[noreturn]] Storage& open(CheckpointName const&) & { std::abort(); }
    void createCheckpoint(Storage&, CheckpointName const&) {}
    void deleteCheckpoint(CheckpointName const&) {}
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

struct StubMemPool
{
    void removeByHash(std::span<bcos::crypto::HashType const>) {}
    template <class View>
    void remove(View&)
    {}
    template <class View, class OutputIt>
    void seal(int64_t, View&, OutputIt)
    {}
};

struct StubExecutor
{
    template <class Storage>
    struct ExecuteContext
    {
        bcos::task::Task<void> prepare() { co_return; }
        bcos::task::Task<void> execute() { co_return; }
        bcos::task::Task<bcos::protocol::TransactionReceipt::Ptr> finish() { co_return {}; }
    };
    template <class Storage>
    bcos::task::Task<bcos::protocol::TransactionReceipt::Ptr> executeTransaction(Storage&,
        const bcos::protocol::BlockHeader&, const bcos::protocol::Transaction&, int,
        const bcos::ledger::LedgerConfig&, bool)
    {
        co_return nullptr;
    }
    template <class Storage>
    bcos::task::Task<ExecuteContext<Storage>> createExecuteContext(Storage&,
        const bcos::protocol::BlockHeader&, const bcos::protocol::Transaction&, int,
        const bcos::ledger::LedgerConfig&, bool)
    {
        co_return ExecuteContext<Storage>{};
    }
};

using EngineOpScheduler = bcos::evm::engine::OpSchedulerSeam<ViewType>;
using LegacyOpEngine =
    bcos::engine::EngineServiceImpl<StubMemPool, MLS, StubExecutor, EngineOpScheduler>;
using NewOpEngine =
    bcos::engine::OpEngineService<StubMemPool, MLS, StubExecutor, EngineOpScheduler>;

static_assert(bcos::engine::EngineServiceConcept<LegacyOpEngine>);
static_assert(bcos::engine::EngineServiceConcept<NewOpEngine>);

constexpr uint64_t kChainId = 0x2105;
constexpr bcos::protocol::BlockNumber c_headOrderingBlockNumber = 40;
constexpr bcos::protocol::BlockNumber c_safeOrderingBlockNumber = 41;

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

bcos::evm::opstack::OpForkFlags forkFlagsFor(bool jovian)
{
    return bcos::evm::opstack::OpForkFlags{.jovianActive = jovian};
}

void seedSysTables(MLS& multiLayerStorage)
{
    auto view = multiLayerStorage.fork();
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
    bcos::task::syncWait(multiLayerStorage.mergeView(std::move(view)));
}

void registerVerifiedBlock(MLS& multiLayerStorage, bcos::h256 const& blockHash, int64_t number)
{
    auto view = multiLayerStorage.fork();
    view.newMutable();
    bcos::storage::Entry entry;
    entry.set(boost::lexical_cast<std::string>(number));
    bcos::task::syncWait(bcos::storage2::writeOne(view,
        StateKey{bcos::ledger::SYS_HASH_2_NUMBER, bcos::concepts::bytebuffer::toView(blockHash)},
        std::move(entry)));
    bcos::storage::Entry hashEntry;
    hashEntry.set(blockHash.asBytes());
    bcos::task::syncWait(bcos::storage2::writeOne(view,
        StateKey{bcos::ledger::SYS_NUMBER_2_HASH, std::to_string(number)}, std::move(hashEntry)));
    bcos::task::syncWait(multiLayerStorage.mergeView(std::move(view)));
}

bcos::protocol::BlockHeader::Ptr productionHeaderOf(
    bcos::protocol::BlockFactory::Ptr const& blockFactory,
    bcos::engine::NewPayloadRequest const& request)
{
    auto const& payload = request.executionPayload;
    const auto transactionsRoot = EngineOpScheduler::computeTxRoot(*payload.rawTransactions);
    return bcos::engine::detail::rebuildOpEthHeader(blockFactory->blockHeaderFactory(), payload,
        transactionsRoot, *request.parentBeaconBlockRoot);
}

void checkStatusParity(
    bcos::engine::PayloadStatus const& legacy, bcos::engine::PayloadStatus const& fresh)
{
    BOOST_CHECK_EQUAL(static_cast<int>(legacy.status), static_cast<int>(fresh.status));
    BOOST_CHECK(legacy.latestValidHash == fresh.latestValidHash);
    BOOST_CHECK(legacy.validationError == fresh.validationError);
}

void checkForkchoiceParity(bcos::engine::ForkchoiceUpdatedResult const& legacy,
    bcos::engine::ForkchoiceUpdatedResult const& fresh)
{
    checkStatusParity(legacy.payloadStatus, fresh.payloadStatus);
    BOOST_CHECK(legacy.payloadId == fresh.payloadId);
}

template <typename Exception>
void checkBothThrow(auto&& legacyAction, auto&& newAction)
{
    BOOST_CHECK_THROW(legacyAction(), Exception);
    BOOST_CHECK_THROW(newAction(), Exception);
}

struct OpServicePair
{
    BackendMemStorage legacyBackend{1};
    BackendMemStorage newBackend{1};
    CheckpointBackend legacyCheckpoint{legacyBackend};
    CheckpointBackend newCheckpoint{newBackend};
    MLS legacyStorage{legacyCheckpoint};
    MLS newStorage{newCheckpoint};
    StubMemPool legacyMemPool;
    StubMemPool newMemPool;
    StubExecutor legacyExecutor;
    StubExecutor newExecutor;
    bcos::protocol::BlockFactory::Ptr blockFactory{makeBlockFactory()};
    EngineOpScheduler legacyScheduler{bcos::evm::opstack::OpForkFlags{}};
    EngineOpScheduler newScheduler{bcos::evm::opstack::OpForkFlags{}};
    LegacyOpEngine legacy;
    NewOpEngine fresh;

    OpServicePair()
      : legacy(legacyMemPool, legacyStorage, legacyExecutor, legacyScheduler, blockFactory, nullptr,
            bcos::engine::c_defaultBlockTxCountLimit,
            static_cast<std::uint32_t>(bcos::engine::ApiVersion::V4), nullptr),
        fresh(newMemPool, newStorage, newExecutor, newScheduler, blockFactory, nullptr,
            bcos::engine::c_defaultBlockTxCountLimit,
            static_cast<std::uint32_t>(bcos::engine::ApiVersion::V4), nullptr)
    {}
};

struct OpE2ePair
{
    BackendMemStorage legacyBackend{1};
    BackendMemStorage newBackend{1};
    CheckpointBackend legacyCheckpoint{legacyBackend};
    CheckpointBackend newCheckpoint{newBackend};
    MLS legacyStorage{legacyCheckpoint};
    MLS newStorage{newCheckpoint};
    StubMemPool legacyMemPool;
    StubMemPool newMemPool;
    StubExecutor legacyExecutor;
    StubExecutor newExecutor;
    bcos::protocol::BlockFactory::Ptr blockFactory{makeBlockFactory()};
    bcos::protocol::TransactionReceiptFactory::Ptr receiptFactory{makeReceiptFactory()};
    bcos::crypto::Hash::Ptr hashImpl{makeCryptoSuite()->hashImpl()};
    bcos::IOServicePool::Ptr ioServicePool{std::make_shared<bcos::IOServicePool>(1)};
    std::shared_ptr<bcos::storage::LegacyStorageWrapper<BackendMemStorage>> legacyLedgerStorage;
    std::shared_ptr<bcos::storage::LegacyStorageWrapper<BackendMemStorage>> newLedgerStorage;
    std::shared_ptr<bcos::ledger::Ledger> legacyLedger;
    std::shared_ptr<bcos::ledger::Ledger> newLedger;
    EngineOpScheduler legacyScheduler;
    EngineOpScheduler newScheduler;
    std::shared_ptr<bcos::executor_v1::opstack::OpScheduler<MLS>> legacyDelegate;
    std::shared_ptr<bcos::executor_v1::opstack::OpScheduler<MLS>> newDelegate;
    LegacyOpEngine legacy;
    NewOpEngine fresh;

    explicit OpE2ePair(bcos::evm::opstack::OpForkFlags forkFlags)
      : legacyLedgerStorage(
            std::make_shared<bcos::storage::LegacyStorageWrapper<BackendMemStorage>>(
                legacyBackend)),
        newLedgerStorage(
            std::make_shared<bcos::storage::LegacyStorageWrapper<BackendMemStorage>>(newBackend)),
        legacyLedger(
            std::make_shared<bcos::ledger::Ledger>(blockFactory, legacyLedgerStorage, 1000)),
        newLedger(std::make_shared<bcos::ledger::Ledger>(blockFactory, newLedgerStorage, 1000)),
        legacyScheduler(forkFlags),
        newScheduler(forkFlags),
        legacyDelegate(
            std::make_shared<bcos::executor_v1::opstack::OpScheduler<MLS>>(receiptFactory, hashImpl,
                kChainId, forkFlags, blockFactory, legacyStorage, legacyLedger, ioServicePool)),
        newDelegate(std::make_shared<bcos::executor_v1::opstack::OpScheduler<MLS>>(receiptFactory,
            hashImpl, kChainId, forkFlags, blockFactory, newStorage, newLedger, ioServicePool)),
        legacy(legacyMemPool, legacyStorage, legacyExecutor, legacyScheduler, blockFactory, nullptr,
            bcos::engine::c_defaultBlockTxCountLimit,
            static_cast<std::uint32_t>(bcos::engine::ApiVersion::V4), legacyDelegate),
        fresh(newMemPool, newStorage, newExecutor, newScheduler, blockFactory, nullptr,
            bcos::engine::c_defaultBlockTxCountLimit,
            static_cast<std::uint32_t>(bcos::engine::ApiVersion::V4), newDelegate)
    {
        seedSysTables(legacyStorage);
        seedSysTables(newStorage);
    }
};

bcos::engine::PayloadAttributes makeOpPayloadAttributes()
{
    bcos::engine::PayloadAttributes attrs;
    attrs.timestamp = 123456;
    attrs.prevRandao =
        bcos::h256("1111111111111111111111111111111111111111111111111111111111111111");
    attrs.suggestedFeeRecipient = bcos::Address("1234567890abcdef1234567890abcdef12345678");
    attrs.withdrawals = std::vector<bcos::engine::WithdrawalV1>{};
    attrs.parentBeaconBlockRoot =
        bcos::h256("2222222222222222222222222222222222222222222222222222222222222222");
    attrs.gasLimit = 30'000'000;
    attrs.eip1559Params = bcos::bytes{0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x08};
    return attrs;
}

void runE2eVectorParity(std::string const& vectorId)
{
    auto sample = w6test::loadVectorSample(vectorId);
    OpE2ePair pair(forkFlagsFor(sample.jovian));
    opstack_test::seedPreState(pair.legacyStorage, sample.vector["pre"]);
    opstack_test::seedPreState(pair.newStorage, sample.vector["pre"]);
    const auto goldenHeader = w6test::decodeGoldenHeader(sample);
    registerVerifiedBlock(pair.legacyStorage, goldenHeader->parentInfo().blockHash, 0);
    registerVerifiedBlock(pair.newStorage, goldenHeader->parentInfo().blockHash, 0);

    auto params = w6test::makeParamsJson(sample);
    auto request = bcos::rpc::parseNewPayloadRequest(params, bcos::engine::ApiVersion::V4);

    auto legacyStatus = bcos::task::syncWait(pair.legacy.newPayload(request, 4));
    auto newStatus = bcos::task::syncWait(pair.fresh.newPayload(request, 4));
    checkStatusParity(legacyStatus, newStatus);
    BOOST_REQUIRE_EQUAL(static_cast<int>(legacyStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));

    auto legacyFcu = bcos::task::syncWait(pair.legacy.updateForkchoice(
        bcos::engine::ForkchoiceState{request.executionPayload.blockHash,
            request.executionPayload.blockHash, request.executionPayload.blockHash},
        nullptr, 3));
    auto newFcu = bcos::task::syncWait(pair.fresh.updateForkchoice(
        bcos::engine::ForkchoiceState{request.executionPayload.blockHash,
            request.executionPayload.blockHash, request.executionPayload.blockHash},
        nullptr, 3));
    checkForkchoiceParity(legacyFcu, newFcu);

    auto legacyRepeat = bcos::task::syncWait(pair.legacy.newPayload(request, 4));
    auto newRepeat = bcos::task::syncWait(pair.fresh.newPayload(request, 4));
    checkStatusParity(legacyRepeat, newRepeat);

    auto legacyProduced = productionHeaderOf(pair.blockFactory, request);
    auto legacyView = pair.legacyStorage.fork();
    auto legacyCanonical = bcos::task::syncWait(bcos::ledger::getBlockHash(
        legacyView, request.executionPayload.blockNumber, bcos::ledger::fromStorage));
    auto newView = pair.newStorage.fork();
    auto newCanonical = bcos::task::syncWait(bcos::ledger::getBlockHash(
        newView, request.executionPayload.blockNumber, bcos::ledger::fromStorage));
    BOOST_CHECK(legacyCanonical.has_value());
    BOOST_CHECK(newCanonical.has_value());
    BOOST_CHECK_EQUAL(*legacyCanonical, request.executionPayload.blockHash);
    BOOST_CHECK_EQUAL(*newCanonical, request.executionPayload.blockHash);
    BOOST_CHECK_EQUAL(legacyProduced->stateRoot(), legacyProduced->stateRoot());
    BOOST_CHECK_EQUAL(legacyProduced->receiptsRoot(), legacyProduced->receiptsRoot());
}

}  // namespace op_engine_parity_test

BOOST_AUTO_TEST_SUITE(OpEngineServiceParityTest)

using namespace op_engine_parity_test;

BOOST_AUTO_TEST_CASE(op_capabilities_match)
{
    OpServicePair pair;
    auto legacyCaps = bcos::task::syncWait(pair.legacy.exchangeCapabilities({}));
    auto newCaps = bcos::task::syncWait(pair.fresh.exchangeCapabilities({}));
    BOOST_CHECK_EQUAL_COLLECTIONS(
        legacyCaps.begin(), legacyCaps.end(), newCaps.begin(), newCaps.end());
}

BOOST_AUTO_TEST_CASE(op_v3_new_payload_throws_same_unsupported_fork)
{
    OpServicePair pair;
    bcos::engine::NewPayloadRequest request;
    request.executionPayload.timestamp = 1000;
    request.executionPayload.blockNumber = 1;
    request.executionPayload.rawTransactions = std::vector<bcos::bytes>{};
    request.executionPayload.withdrawals = std::vector<bcos::engine::WithdrawalV1>{};
    request.executionPayload.withdrawalsRoot = bcos::h256{};
    request.executionPayload.excessBlobGas = bcos::u256(0);
    request.executionPayload.blobGasUsed = bcos::u256(0);
    request.parentBeaconBlockRoot = bcos::h256{};
    checkBothThrow<bcos::engine::UnsupportedFork>(
        [&] { return bcos::task::syncWait(pair.legacy.newPayload(request, 3)); },
        [&] { return bcos::task::syncWait(pair.fresh.newPayload(request, 3)); });
}

BOOST_AUTO_TEST_CASE(op_missing_gas_limit_returns_same_invalid)
{
    OpServicePair pair;
    auto attrs = makeOpPayloadAttributes();
    attrs.gasLimit = std::nullopt;
    bcos::engine::ForkchoiceState forkchoice{
        bcos::h256("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
        bcos::h256("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"),
        bcos::h256("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc")};
    registerVerifiedBlock(pair.legacyStorage, forkchoice.headBlockHash, 0);
    registerVerifiedBlock(pair.newStorage, forkchoice.headBlockHash, 0);
    auto legacyResult = bcos::task::syncWait(pair.legacy.updateForkchoice(forkchoice, &attrs, 3));
    auto newResult = bcos::task::syncWait(pair.fresh.updateForkchoice(forkchoice, &attrs, 3));
    checkForkchoiceParity(legacyResult, newResult);
    BOOST_REQUIRE(legacyResult.payloadStatus.validationError.has_value());
    BOOST_CHECK(legacyResult.payloadStatus.validationError->find("gasLimit") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(op_unknown_parent_returns_same_syncing)
{
    OpE2ePair pair(bcos::evm::opstack::OpForkFlags{.jovianActive = true});
    auto sample = w6test::loadVectorSample("jovian_deposit_only");
    opstack_test::seedPreState(pair.legacyStorage, sample.vector["pre"]);
    opstack_test::seedPreState(pair.newStorage, sample.vector["pre"]);
    auto params = w6test::makeParamsJson(sample);
    params[0u]["parentHash"] = "0x0000000000000000000000000000000000000000000000000000000000000001";
    auto request = bcos::rpc::parseNewPayloadRequest(params, bcos::engine::ApiVersion::V4);
    auto header = productionHeaderOf(pair.blockFactory, request);
    params[0u]["blockHash"] =
        w6test::hexPrefixedH256(bcos::protocol::EthBlockHeader::computeHash(*header));
    request = bcos::rpc::parseNewPayloadRequest(params, bcos::engine::ApiVersion::V4);
    auto legacyStatus = bcos::task::syncWait(pair.legacy.newPayload(request, 4));
    auto newStatus = bcos::task::syncWait(pair.fresh.newPayload(request, 4));
    checkStatusParity(legacyStatus, newStatus);
    BOOST_CHECK_EQUAL(static_cast<int>(legacyStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Syncing));
}

BOOST_AUTO_TEST_CASE(op_safe_finalized_validation_matches)
{
    OpServicePair pair;
    bcos::engine::ForkchoiceState forkchoiceState{
        bcos::h256("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"),
        bcos::h256("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"),
        bcos::h256("0000000000000000000000000000000000000000000000000000000000000011")};
    registerVerifiedBlock(
        pair.legacyStorage, forkchoiceState.headBlockHash, c_headOrderingBlockNumber);
    registerVerifiedBlock(
        pair.legacyStorage, forkchoiceState.safeBlockHash, c_safeOrderingBlockNumber);
    registerVerifiedBlock(
        pair.legacyStorage, forkchoiceState.finalizedBlockHash, c_headOrderingBlockNumber);
    registerVerifiedBlock(
        pair.newStorage, forkchoiceState.headBlockHash, c_headOrderingBlockNumber);
    registerVerifiedBlock(
        pair.newStorage, forkchoiceState.safeBlockHash, c_safeOrderingBlockNumber);
    registerVerifiedBlock(
        pair.newStorage, forkchoiceState.finalizedBlockHash, c_headOrderingBlockNumber);
    checkBothThrow<bcos::engine::InvalidForkchoiceState>(
        [&] {
            return bcos::task::syncWait(pair.legacy.updateForkchoice(forkchoiceState, nullptr, 3));
        },
        [&] {
            return bcos::task::syncWait(pair.fresh.updateForkchoice(forkchoiceState, nullptr, 3));
        });
}

BOOST_AUTO_TEST_CASE(op_e2e_isthmus_deposit_only_parity)
{
    runE2eVectorParity("isthmus_deposit_only");
}

BOOST_AUTO_TEST_CASE(op_e2e_jovian_transfer_multi_parity)
{
    runE2eVectorParity("jovian_transfer_multi");
}

BOOST_AUTO_TEST_SUITE_END()
