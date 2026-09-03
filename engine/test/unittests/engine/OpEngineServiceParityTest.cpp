// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
//
// Matrix: S5 — OpEngineService wired on release-3.18.0 (single transactions[i].raw carrier).
// Full dual parity vs EngineServiceImpl OP mode / GoldenSample e2e is deferred (no Impl
// opMode and no t8n fixtures on this branch). This suite covers:
//   - OpEngineService API gates (capabilities, V3 newPayload, gasLimit)
//   - Shared FCU ordering exceptions vs EngineServiceImpl (safe/finalized)
//   - EngineTracker exclusive/shared publish concurrency (op_fast_path)

#include "engine/bcos-engine/EngineServiceImpl.h"
#include "engine/bcos-engine/EngineTracker.h"
#include "engine/bcos-engine/OpEngineService.h"

#include <bcos-concepts/ByteBuffer.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-framework/engine/EngineService.h>
#include <bcos-framework/engine/Errors.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-tars-protocol/protocol/BlockFactoryImpl.h>
#include <bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/Error.h>
#include <bcos-utilities/Exceptions.h>
#include <opstack-executor/OpSchedulerSeam.h>
#include <opstack-executor/tests/OpSchedulerSeamTestHelpers.h>
#include <boost/lexical_cast.hpp>
#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <exception>
#include <latch>
#include <optional>
#include <span>
#include <thread>
#include <unordered_map>
#include <vector>

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
    std::vector<bcos::crypto::HashType> removed;
    void removeByHash(std::span<bcos::crypto::HashType const> hashes)
    {
        removed.insert(removed.end(), hashes.begin(), hashes.end());
    }
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
        bcos::task::Task<bcos::protocol::TransactionReceipt::Ptr> finish() { co_return nullptr; }
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

using EngineOpSchedulerBase = bcos::evm::engine::OpSchedulerSeam<ViewType>;
/// Production seam synthesizes from L1BlockInfo. Fixtures keep the zero envelope.
struct EngineOpScheduler : EngineOpSchedulerBase
{
    using EngineOpSchedulerBase::EngineOpSchedulerBase;
    [[nodiscard]] bcos::bytes synthesizeL1AttributesEnvelope() const
    {
        return bcos::evm::engine::testutil::synthesizeL1AttributesEnvelope(isJovianActive());
    }
};
using EthLegacyEngine =
    bcos::engine::EngineServiceImpl<StubMemPool, MLS, StubExecutor, EngineOpScheduler>;
using OpEngine = bcos::engine::OpEngineService<StubMemPool, MLS, StubExecutor, EngineOpScheduler>;

static_assert(bcos::engine::EngineServiceConcept<EthLegacyEngine>);
static_assert(bcos::engine::EngineServiceConcept<OpEngine>);

constexpr bcos::protocol::BlockNumber c_headOrderingBlockNumber = 40;
constexpr bcos::protocol::BlockNumber c_safeOrderingBlockNumber = 41;
constexpr bcos::protocol::BlockNumber c_finalizedOrderingBlockNumber = 42;

constexpr char const* c_opV4UnsupportedForkMessage =
    "Isthmus+ payloads require engine_newPayloadV4 (JSON-RPC -38005)";
constexpr char const* c_safeAboveHeadMessage =
    "Forkchoice safe block number must not exceed head block number";
constexpr char const* c_finalizedAboveHeadMessage =
    "Forkchoice finalized block number must not exceed head block number";
constexpr char const* c_finalizedAboveSafeMessage =
    "Forkchoice finalized block number must not exceed safe block number";

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

void registerHashToNumberOnly(MLS& multiLayerStorage, bcos::h256 const& blockHash, int64_t number)
{
    auto view = multiLayerStorage.fork();
    view.newMutable();
    bcos::storage::Entry entry;
    entry.set(boost::lexical_cast<std::string>(number));
    bcos::task::syncWait(bcos::storage2::writeOne(view,
        StateKey{bcos::ledger::SYS_HASH_2_NUMBER, bcos::concepts::bytebuffer::toView(blockHash)},
        std::move(entry)));
    bcos::task::syncWait(multiLayerStorage.mergeView(std::move(view)));
}

void checkStatusParity(
    bcos::engine::PayloadStatus const& left, bcos::engine::PayloadStatus const& right)
{
    BOOST_CHECK_EQUAL(static_cast<int>(left.status), static_cast<int>(right.status));
    BOOST_CHECK(left.latestValidHash == right.latestValidHash);
    BOOST_CHECK(left.validationError == right.validationError);
}

template <typename Exception>
void checkBothExceptionMessages(auto&& leftAction, auto&& rightAction, char const* expectedMessage)
{
    BOOST_CHECK_EXCEPTION(leftAction(), Exception, [&](Exception const& e) {
        auto const* comment = boost::get_error_info<bcos::errinfo_comment>(e);
        return comment != nullptr && *comment == expectedMessage;
    });
    BOOST_CHECK_EXCEPTION(rightAction(), Exception, [&](Exception const& e) {
        auto const* comment = boost::get_error_info<bcos::errinfo_comment>(e);
        return comment != nullptr && *comment == expectedMessage;
    });
}

bcos::engine::PayloadAttributes makeOpPayloadAttributes()
{
    bcos::engine::PayloadAttributes attrs;
    // Whole-second milliseconds for Eth RLP timestamp validation.
    attrs.timestamp = 1'700'000'000'000ULL;
    attrs.prevRandao = bcos::h256(std::string(64, '2'));
    attrs.suggestedFeeRecipient = bcos::Address(std::string(40, '3'));
    attrs.withdrawals = std::vector<bcos::engine::WithdrawalV1>{};
    attrs.parentBeaconBlockRoot = bcos::h256(std::string(64, '4'));
    attrs.gasLimit = 30'000'000;
    attrs.eip1559Params = bcos::bytes(8, 0);
    attrs.minBaseFee = 0;
    attrs.noTxPool = true;
    return attrs;
}

struct OpServicePair
{
    BackendMemStorage backend{1};
    CheckpointBackend checkpoint{backend};
    MLS storage{checkpoint};
    StubMemPool memPool;
    StubExecutor executor;
    bcos::protocol::BlockFactory::Ptr blockFactory{makeBlockFactory()};
    EngineOpScheduler scheduler{bcos::evm::opstack::OpForkFlags{}, {}};
    OpEngine service;

    explicit OpServicePair(bool allowSynthesizedL1Attributes = true)
      : service(memPool, storage, scheduler, blockFactory, nullptr,
            bcos::engine::c_defaultBlockTxCountLimit,
            static_cast<std::uint32_t>(bcos::engine::ApiVersion::V3), nullptr, nullptr,
            allowSynthesizedL1Attributes)
    {}
};

struct SharedForkchoicePair
{
    BackendMemStorage legacyBackend{1};
    BackendMemStorage opBackend{1};
    CheckpointBackend legacyCheckpoint{legacyBackend};
    CheckpointBackend opCheckpoint{opBackend};
    MLS legacyStorage{legacyCheckpoint};
    MLS opStorage{opCheckpoint};
    StubMemPool legacyMemPool;
    StubMemPool opMemPool;
    StubExecutor legacyExecutor;
    StubExecutor opExecutor;
    bcos::protocol::BlockFactory::Ptr blockFactory{makeBlockFactory()};
    EngineOpScheduler legacyScheduler{bcos::evm::opstack::OpForkFlags{}, {}};
    EngineOpScheduler opScheduler{bcos::evm::opstack::OpForkFlags{}, {}};
    EthLegacyEngine legacy;
    OpEngine op;

    SharedForkchoicePair()
      : legacy(legacyMemPool, legacyStorage, legacyExecutor, legacyScheduler, blockFactory),
        op(opMemPool, opStorage, opScheduler, blockFactory, nullptr,
            bcos::engine::c_defaultBlockTxCountLimit,
            static_cast<std::uint32_t>(bcos::engine::ApiVersion::V3), nullptr)
    {}
};

}  // namespace op_engine_parity_test

BOOST_AUTO_TEST_SUITE(OpEngineServiceParityTest)

using namespace op_engine_parity_test;

BOOST_AUTO_TEST_CASE(op_capabilities_include_op_methods)
{
    // Matrix: S5
    OpServicePair pair;
    auto caps = bcos::task::syncWait(pair.service.exchangeCapabilities({}));
    BOOST_CHECK(std::find(caps.begin(), caps.end(), "engine_newPayloadV4") != caps.end());
    BOOST_CHECK(std::find(caps.begin(), caps.end(), "engine_getPayloadV5") != caps.end());
    BOOST_CHECK(std::find(caps.begin(), caps.end(), "engine_forkchoiceUpdatedV3") != caps.end());
}

BOOST_AUTO_TEST_CASE(op_v3_new_payload_throws_unsupported_fork)
{
    // Matrix: S5 — release carrier is transactions[i].raw (empty list here).
    OpServicePair pair;
    bcos::engine::NewPayloadRequest request;
    request.executionPayload.timestamp = 1000;
    request.executionPayload.blockNumber = 1;
    request.executionPayload.transactions = {};
    request.executionPayload.withdrawals = std::vector<bcos::engine::WithdrawalV1>{};
    request.executionPayload.withdrawalsRoot = bcos::h256{};
    request.executionPayload.excessBlobGas = bcos::u256(0);
    request.executionPayload.blobGasUsed = bcos::u256(0);
    request.parentBeaconBlockRoot = bcos::h256{};
    BOOST_CHECK_EXCEPTION(bcos::task::syncWait(pair.service.newPayload(request, 3)),
        bcos::engine::UnsupportedFork, [&](bcos::engine::UnsupportedFork const& e) {
            auto const* comment = boost::get_error_info<bcos::errinfo_comment>(e);
            return comment != nullptr && *comment == c_opV4UnsupportedForkMessage;
        });
}

BOOST_AUTO_TEST_CASE(op_missing_gas_limit_returns_invalid)
{
    // Matrix: S5
    OpServicePair pair;
    auto attrs = makeOpPayloadAttributes();
    attrs.gasLimit = std::nullopt;
    bcos::engine::ForkchoiceState forkchoice{
        bcos::h256("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
        bcos::h256("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"),
        bcos::h256("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc")};
    registerVerifiedBlock(pair.storage, forkchoice.headBlockHash, 0);
    auto result = bcos::task::syncWait(pair.service.updateForkchoice(forkchoice, &attrs, 3));
    BOOST_CHECK_EQUAL(static_cast<int>(result.payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Invalid));
    BOOST_REQUIRE(result.payloadStatus.validationError.has_value());
    BOOST_CHECK(result.payloadStatus.validationError->find("gasLimit") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(op_safe_above_head_matches_eth_legacy)
{
    // Matrix: S5 — shared forkchoice ordering vs EngineServiceImpl.
    SharedForkchoicePair pair;
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
    registerVerifiedBlock(pair.opStorage, forkchoiceState.headBlockHash, c_headOrderingBlockNumber);
    registerVerifiedBlock(pair.opStorage, forkchoiceState.safeBlockHash, c_safeOrderingBlockNumber);
    registerVerifiedBlock(
        pair.opStorage, forkchoiceState.finalizedBlockHash, c_headOrderingBlockNumber);
    checkBothExceptionMessages<bcos::engine::InvalidForkchoiceState>(
        [&] {
            return bcos::task::syncWait(pair.legacy.updateForkchoice(forkchoiceState, nullptr, 3));
        },
        [&] { return bcos::task::syncWait(pair.op.updateForkchoice(forkchoiceState, nullptr, 3)); },
        c_safeAboveHeadMessage);
}

BOOST_AUTO_TEST_CASE(op_finalized_above_head_matches_eth_legacy)
{
    SharedForkchoicePair pair;
    bcos::engine::ForkchoiceState forkchoiceState{
        bcos::h256("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"),
        bcos::h256("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"),
        bcos::h256("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff")};
    registerVerifiedBlock(
        pair.legacyStorage, forkchoiceState.headBlockHash, c_headOrderingBlockNumber);
    registerVerifiedBlock(
        pair.legacyStorage, forkchoiceState.safeBlockHash, c_headOrderingBlockNumber);
    registerVerifiedBlock(
        pair.legacyStorage, forkchoiceState.finalizedBlockHash, c_safeOrderingBlockNumber);
    registerVerifiedBlock(pair.opStorage, forkchoiceState.headBlockHash, c_headOrderingBlockNumber);
    registerVerifiedBlock(pair.opStorage, forkchoiceState.safeBlockHash, c_headOrderingBlockNumber);
    registerVerifiedBlock(
        pair.opStorage, forkchoiceState.finalizedBlockHash, c_safeOrderingBlockNumber);
    checkBothExceptionMessages<bcos::engine::InvalidForkchoiceState>(
        [&] {
            return bcos::task::syncWait(pair.legacy.updateForkchoice(forkchoiceState, nullptr, 3));
        },
        [&] { return bcos::task::syncWait(pair.op.updateForkchoice(forkchoiceState, nullptr, 3)); },
        c_finalizedAboveHeadMessage);
}

BOOST_AUTO_TEST_CASE(op_finalized_above_safe_matches_eth_legacy)
{
    SharedForkchoicePair pair;
    bcos::engine::ForkchoiceState forkchoiceState{
        bcos::h256("1212121212121212121212121212121212121212121212121212121212121212"),
        bcos::h256("1313131313131313131313131313131313131313131313131313131313131313"),
        bcos::h256("1414141414141414141414141414141414141414141414141414141414141414")};
    registerVerifiedBlock(
        pair.legacyStorage, forkchoiceState.headBlockHash, c_finalizedOrderingBlockNumber);
    registerVerifiedBlock(
        pair.legacyStorage, forkchoiceState.safeBlockHash, c_headOrderingBlockNumber);
    registerVerifiedBlock(
        pair.legacyStorage, forkchoiceState.finalizedBlockHash, c_safeOrderingBlockNumber);
    registerVerifiedBlock(
        pair.opStorage, forkchoiceState.headBlockHash, c_finalizedOrderingBlockNumber);
    registerVerifiedBlock(pair.opStorage, forkchoiceState.safeBlockHash, c_headOrderingBlockNumber);
    registerVerifiedBlock(
        pair.opStorage, forkchoiceState.finalizedBlockHash, c_safeOrderingBlockNumber);
    checkBothExceptionMessages<bcos::engine::InvalidForkchoiceState>(
        [&] {
            return bcos::task::syncWait(pair.legacy.updateForkchoice(forkchoiceState, nullptr, 3));
        },
        [&] { return bcos::task::syncWait(pair.op.updateForkchoice(forkchoiceState, nullptr, 3)); },
        c_finalizedAboveSafeMessage);
}

BOOST_AUTO_TEST_CASE(op_fast_path_concurrent_with_build_publish)
{
    // Matrix: S5 / S7-adjacent — shared guard blocks exclusive publish.
    bcos::engine::EngineTracker tracker;
    std::unordered_map<bcos::engine::PayloadID, bcos::engine::OpPayloadArtifacts> artifacts;
    auto blockFactory = makeBlockFactory();

    bcos::h256 const targetHash(0x42);
    bcos::engine::PayloadID const targetPayloadId = "0xdeadbeef";
    constexpr bcos::protocol::BlockNumber kTargetNumber = 7;

    {
        auto guard = tracker.lockExclusive();
        auto entry = std::make_shared<bcos::engine::BuiltPayload>();
        entry->executionPayload.blockHash = targetHash;
        auto header = blockFactory->blockHeaderFactory()->createBlockHeader();
        header->setNumber(kTargetNumber);
        (void)bcos::engine::op_detail::publishBuiltPayload(guard, artifacts, targetPayloadId,
            targetHash, entry, bcos::engine::OpPayloadArtifacts{.canonicalHeader = header});
    }

    bcos::protocol::BlockHeader::Ptr initialHeader;
    std::atomic<bool> writerFinished{false};
    std::exception_ptr writerError;

    std::latch writerReady{1};
    std::latch permission{1};
    std::latch committed{1};

    std::optional<std::thread> writer;
    {
        auto shared = tracker.lockShared();
        initialHeader = bcos::engine::op_detail::findBuiltHeader(shared, artifacts, targetHash);
        BOOST_REQUIRE(initialHeader);
        BOOST_CHECK_EQUAL(initialHeader->number(), kTargetNumber);

        writer.emplace([&] {
            try
            {
                writerReady.count_down();
                permission.wait();
                committed.count_down();
                auto guard = tracker.lockExclusive();
                bcos::h256 writerHash(0x99);
                bcos::engine::PayloadID writerPayloadId = "0xcafebabe";
                auto entry = std::make_shared<bcos::engine::BuiltPayload>();
                entry->executionPayload.blockHash = writerHash;
                auto header = blockFactory->blockHeaderFactory()->createBlockHeader();
                header->setNumber(99);
                (void)bcos::engine::op_detail::publishBuiltPayload(guard, artifacts,
                    writerPayloadId, writerHash, entry,
                    bcos::engine::OpPayloadArtifacts{.canonicalHeader = header});
                writerFinished.store(true, std::memory_order_release);
            }
            catch (...)
            {
                writerError = std::current_exception();
            }
        });

        writerReady.wait();
        permission.count_down();
        committed.wait();
        BOOST_CHECK(!writerFinished.load(std::memory_order_acquire));
    }

    writer->join();
    if (writerError)
    {
        std::rethrow_exception(writerError);
    }
    BOOST_CHECK(writerFinished.load(std::memory_order_acquire));

    {
        auto shared = tracker.lockShared();
        auto stableHeader = bcos::engine::op_detail::findBuiltHeader(shared, artifacts, targetHash);
        BOOST_REQUIRE(stableHeader);
        BOOST_CHECK_EQUAL(stableHeader->number(), kTargetNumber);
        BOOST_CHECK_EQUAL(stableHeader.get(), initialHeader.get());
    }
}

BOOST_AUTO_TEST_CASE(op_fcu_rejects_non_canonical_safe)
{
    // op-geth: HASH_2_NUMBER finds the block, but ReadCanonicalHash(number) differs.
    OpServicePair pair;
    bcos::engine::ForkchoiceState forkchoice{
        bcos::h256("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
        bcos::h256("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"),
        bcos::h256("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc")};
    auto const canonicalSafe =
        bcos::h256("dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd");
    registerVerifiedBlock(pair.storage, forkchoice.headBlockHash, 10);
    registerVerifiedBlock(pair.storage, canonicalSafe, 8);
    registerHashToNumberOnly(pair.storage, forkchoice.safeBlockHash, 8);
    registerVerifiedBlock(pair.storage, forkchoice.finalizedBlockHash, 7);
    BOOST_CHECK_EXCEPTION(
        bcos::task::syncWait(pair.service.updateForkchoice(forkchoice, nullptr, 3)),
        bcos::engine::InvalidForkchoiceState, [&](bcos::engine::InvalidForkchoiceState const& e) {
            auto const* comment = boost::get_error_info<bcos::errinfo_comment>(e);
            return comment != nullptr && *comment == "Forkchoice safe block not in canonical chain";
        });
}

BOOST_AUTO_TEST_CASE(op_fcu_rejects_empty_txs_when_synthesis_disabled)
{
    // op_engine_rpc / op-geth: do not invent an L1-attributes deposit.
    OpServicePair pair(/*allowSynthesizedL1Attributes=*/false);
    auto attrs = makeOpPayloadAttributes();
    attrs.minBaseFee = std::nullopt;
    attrs.transactions = std::nullopt;
    auto const hash =
        bcos::h256("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    bcos::engine::ForkchoiceState forkchoice{hash, hash, hash};
    registerVerifiedBlock(pair.storage, hash, 0);
    auto result = bcos::task::syncWait(pair.service.updateForkchoice(forkchoice, &attrs, 3));
    BOOST_CHECK_EQUAL(static_cast<int>(result.payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Invalid));
    BOOST_REQUIRE(result.payloadStatus.validationError.has_value());
    BOOST_CHECK(
        result.payloadStatus.validationError->find("L1 attributes deposit") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(op_fcu_v4_is_unsupported)
{
    // Matrix: T05 — maxEngineVersion is V3, so FCU V4 is UnsupportedEngineApiVersion.
    OpServicePair pair;
    bcos::engine::ForkchoiceState state;
    BOOST_CHECK_THROW(bcos::task::syncWait(pair.service.updateForkchoice(state, nullptr, 4)),
        bcos::engine::UnsupportedEngineApiVersion);
}

BOOST_AUTO_TEST_CASE(op_culprit_hash_is_structured_not_message_text)
{
    // Matrix: T02 T03 — engine reads OpCulpritTxHash; a [tx=0x...] message is ignored.
    bcos::h256 const hash(std::string(64, 'a'));
    auto error = BCOS_ERROR_PTR(-1, "Execute block failed! invalid tx");
    *error << bcos::engine::OpCulpritTxHash(hash);
    auto got = bcos::engine::culpritTxHashFromError(*error);
    BOOST_REQUIRE(got.has_value());
    BOOST_CHECK_EQUAL(got->hex(), hash.hex());

    auto plain = BCOS_ERROR_PTR(-1, "Execute block failed! [tx=0x" + hash.hex() + "]");
    BOOST_CHECK(!bcos::engine::culpritTxHashFromError(*plain).has_value());

    bcos::evm::OpConsensusError thrown("op block: invalid non-deposit tx");
    thrown.txHash = hash;
    auto attached = BCOS_ERROR_UNIQUE_PTR(1, thrown.what());
    *attached << bcos::engine::OpCulpritTxHash(*thrown.txHash);
    auto recovered = bcos::engine::culpritTxHashFromError(*attached);
    BOOST_REQUIRE(recovered.has_value());
    BOOST_CHECK_EQUAL(recovered->hex(), hash.hex());

    StubMemPool pool;
    std::array<bcos::crypto::HashType, 1> hashes{*recovered};
    pool.removeByHash(std::span<bcos::crypto::HashType const>(hashes));
    BOOST_REQUIRE_EQUAL(pool.removed.size(), 1);
    BOOST_CHECK_EQUAL(pool.removed.front().hex(), hash.hex());
}

BOOST_AUTO_TEST_SUITE_END()
