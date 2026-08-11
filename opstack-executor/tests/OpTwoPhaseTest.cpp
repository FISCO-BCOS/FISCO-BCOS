// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// OpTwoPhaseTest — the two-phase surface of OpSchedulerImpl: executeBlock (phase 1, stages the
// result into m_pending) + commitBlock (phase 2, opstackRegisterBlock table writes + mergeView +
// notifier). Unlike the e2e suite (which drives the engine's old single-phase path), this file
// drives the scheduler directly so the pending-execution contract is pinned:
//   HappyExecuteCommit    execute -> pendingExecuteResult() has value -> commit -> tables written /
//                         m_pending cleared / notifier fired once
//   CommitEmptyPending    commit without execute -> OpExecutionInternalError
//   CommitNumberMismatch  execute(number 1) -> commit(number 2) -> OpExecutionInternalError
//   ChainedAfterCommit    execute+commit -> execute+commit (two full cycles over real chainA/B)
//   ChainedAfterReset     execute -> resetPending() -> execute -> commit (self-heal)
// The executable-block construction reuses the W6 golden-vector harness exactly as
// OpNewPayloadRpcE2eTest.cpp does (loadVectorSample / seedPreState / makeParamsJson /
// productionHeaderOf + registerVerifiedBlock).

#include "support/GoldenSample.h"
#include "support/SeedPreState.h"
#include <bcos-concepts/ByteBuffer.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-framework/engine/Errors.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <opstack-executor/OpSchedulerImpl.h>
// EngineHelper.h's parseNewPayloadRequest declaration references
// bcos::protocol::TransactionFactory&, but EngineHelper.h does not declare that type
// itself (production relies on bcos-rpc unity-build include order). A single-TU
// direct compile must include TransactionFactory.h first or the declaration fails.
#include <bcos-framework/protocol/TransactionFactory.h>
#include <bcos-rpc/web3jsonrpc/utils/EngineHelper.h>
#include <bcos-tars-protocol/protocol/BlockFactoryImpl.h>
#include <bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <engine/bcos-engine/EngineServiceImpl.h>
#include <boost/lexical_cast.hpp>
#include <boost/test/unit_test.hpp>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using bcos::executor_v1::StateKey;
using bcos::executor_v1::StateValue;
namespace memory_storage = bcos::storage2::memory_storage;

namespace
{
// ── storage fixture (alias family verbatim from OpNewPayloadRpcE2eTest.cpp) ──
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

constexpr uint64_t kChainId = 0x2105;

// All vectors used by this file (isthmus_deposit_only + the isthmus chainA/B pair) are Isthmus.
bcos::evm::opstack::OpForkTimestamps isthmusForkTimestamps()
{
    return bcos::evm::opstack::OpForkTimestamps{
        .isthmusTime = 0, .jovianTime = std::numeric_limits<uint64_t>::max()};
}

using OpScheduler = bcos::evm::engine::OpSchedulerImpl<ViewType, MLS>;

/// Envelope -> tars converter for the two-phase commit path. A file-scope function (not an inline
/// member lambda) so the fixture's member-init list can reference it by name — members initialize
/// in declaration order, and a converter declared after the scheduler would read an unconstructed
/// object. `bcos::engine::detail::opEnvelopeToTars`'s forward declaration is visible via
/// EngineServiceImpl.h (included above).
std::optional<bcostars::Transaction> realConverter(
    bcos::bytes const& env, bcos::crypto::HashType const& h)
{
    return bcos::engine::detail::opEnvelopeToTars(env, h);
}

/// Lean fixture: the two-phase tests drive the scheduler directly, so no EngineService member is
/// needed. blockFactory is declared before scheduler (declaration-order member init).
struct OpTwoPhaseFixture
{
    BackendMemStorage backendStorage{1};
    CheckpointBackend checkpointBackend{backendStorage};
    MLS multiLayerStorage{checkpointBackend};
    bcos::protocol::TransactionReceiptFactory::Ptr receiptFactory{makeReceiptFactory()};
    bcos::protocol::BlockFactory::Ptr blockFactory{makeBlockFactory()};
    StubExecutor executor;
    OpScheduler scheduler;

    OpTwoPhaseFixture()
      : scheduler(receiptFactory, kChainId, isthmusForkTimestamps(), blockFactory,
            multiLayerStorage, realConverter)
    {}
};

/// Produced header: production-mapping reconstruction (same as OpNewPayloadRpcE2eTest.cpp).
bcos::protocol::BlockHeader::Ptr productionHeaderOf(
    bcos::protocol::BlockFactory::Ptr const& blockFactory,
    bcos::engine::NewPayloadRequest const& request)
{
    auto const& payload = request.executionPayload;
    const auto transactionsRoot = OpScheduler::computeTxRoot(*payload.rawTransactions);
    return bcos::engine::detail::rebuildOpEthHeader(blockFactory->blockHeaderFactory(), payload,
        transactionsRoot, *request.parentBeaconBlockRoot);
}

/// Parent pre-registration (same as OpNewPayloadRpcE2eTest.cpp) — every isolated vector is block 1,
/// so the parent must be a trusted genesis (height 0) for the execution to see it.
void registerVerifiedBlock(MLS& multiLayerStorage, bcos::h256 const& blockHash, int64_t number)
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

/// Minimal OP header for the error-path tests (same field set as OpSchedulerImplSmokeTest.cpp).
std::shared_ptr<bcostars::protocol::BlockHeaderImpl> makeOpHeader(
    bcos::protocol::BlockNumber number)
{
    auto h = std::make_shared<bcostars::protocol::BlockHeaderImpl>();
    h->setNumber(number);
    h->setTimestamp(1700000000000);
    h->setParentInfo(
        bcos::protocol::ParentInfo{.blockNumber = number - 1, .blockHash = bcos::h256{}});
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
}  // namespace

BOOST_AUTO_TEST_SUITE(OpTwoPhaseSuite)

BOOST_AUTO_TEST_CASE(HappyExecuteCommit)
{
    auto sample = w6test::loadVectorSample("isthmus_deposit_only");
    OpTwoPhaseFixture fixture;
    w6test::seedPreState(fixture.multiLayerStorage, sample.vector["pre"]);
    const auto goldenHeader = w6test::decodeGoldenHeader(sample);
    registerVerifiedBlock(fixture.multiLayerStorage, goldenHeader->parentInfo().blockHash, 0);

    auto request = bcos::rpc::parseNewPayloadRequest(w6test::makeParamsJson(sample),
        *fixture.blockFactory->transactionFactory(), bcos::engine::ApiVersion::V4);
    auto header = productionHeaderOf(fixture.blockFactory, request);
    auto const& rawTxs = *request.executionPayload.rawTransactions;
    BOOST_REQUIRE_EQUAL(header->number(), 1);  // isolated vectors are block 1 (parent pre-registered at 0)

    auto view = fixture.multiLayerStorage.fork();
    view.newMutable();
    bcos::ledger::LedgerConfig ledgerConfig;
    int notified = 0;
    fixture.scheduler.setBlockNumberNotifier([&](bcos::protocol::BlockNumber) { ++notified; });

    // Phase 1: execute stages into m_pending.
    auto receipts = bcos::task::syncWait(
        fixture.scheduler.executeBlock(view, fixture.executor, *header, rawTxs, ledgerConfig));
    BOOST_REQUIRE(!receipts.empty());
    const auto& pending = fixture.scheduler.pendingExecuteResult();
    BOOST_REQUIRE_EQUAL(pending.receipts.size(), receipts.size());

    // Phase 2: commit persists + merges + notifies.
    const auto goldenBlockHash = bcos::h256(std::string(sample.golden["blockHash"].asString()));
    bcos::task::syncWait(fixture.scheduler.commitBlock(header, goldenBlockHash));

    // mergeView landed the block tables: SYS_NUMBER_2_HASH["1"] on a fresh fork.
    auto checkView = fixture.multiLayerStorage.fork();
    auto num2hash = bcos::task::syncWait(bcos::storage2::readOne(
        checkView, StateKey{bcos::ledger::SYS_NUMBER_2_HASH, "1"}));
    BOOST_REQUIRE(num2hash.has_value());
    BOOST_CHECK_EQUAL(num2hash->get(),
        std::string_view(reinterpret_cast<const char*>(goldenBlockHash.data()),
            goldenBlockHash.size()));

    // m_pending was cleared: pendingExecuteResult() now throws.
    BOOST_CHECK_THROW(
        fixture.scheduler.pendingExecuteResult(), bcos::engine::OpExecutionInternalError);
    // notifier fired exactly once (inside commitBlock, after the merge).
    BOOST_CHECK_EQUAL(notified, 1);
}

BOOST_AUTO_TEST_CASE(CommitEmptyPending)
{
    OpTwoPhaseFixture fixture;
    auto header = makeOpHeader(1);
    const bcos::h256 blockHash{
        std::string{"0x112233445566778899aabbccddeeff00112233445566778899aabbccddeeff0011"}};
    // No executeBlock ran -> commitBlock must refuse loudly.
    BOOST_CHECK_THROW(bcos::task::syncWait(fixture.scheduler.commitBlock(header, blockHash)),
        bcos::engine::OpExecutionInternalError);
}

BOOST_AUTO_TEST_CASE(CommitNumberMismatch)
{
    auto sample = w6test::loadVectorSample("isthmus_deposit_only");
    OpTwoPhaseFixture fixture;
    w6test::seedPreState(fixture.multiLayerStorage, sample.vector["pre"]);
    const auto goldenHeader = w6test::decodeGoldenHeader(sample);
    registerVerifiedBlock(fixture.multiLayerStorage, goldenHeader->parentInfo().blockHash, 0);

    auto request = bcos::rpc::parseNewPayloadRequest(w6test::makeParamsJson(sample),
        *fixture.blockFactory->transactionFactory(), bcos::engine::ApiVersion::V4);
    auto header = productionHeaderOf(fixture.blockFactory, request);  // number 1
    auto const& rawTxs = *request.executionPayload.rawTransactions;
    auto view = fixture.multiLayerStorage.fork();
    view.newMutable();
    bcos::ledger::LedgerConfig ledgerConfig;
    bcos::task::syncWait(
        fixture.scheduler.executeBlock(view, fixture.executor, *header, rawTxs, ledgerConfig));

    const auto goldenBlockHash = bcos::h256(std::string(sample.golden["blockHash"].asString()));
    auto mismatchedHeader = makeOpHeader(2);  // number 2 != the pending execution's number 1
    BOOST_CHECK_THROW(
        bcos::task::syncWait(fixture.scheduler.commitBlock(mismatchedHeader, goldenBlockHash)),
        bcos::engine::OpExecutionInternalError);
}

BOOST_AUTO_TEST_CASE(ChainedAfterCommit)
{
    auto sampleA = w6test::loadChainedSample("chainA");
    auto sampleB = w6test::loadChainedSample("chainB");
    BOOST_REQUIRE(sampleA.jovian == sampleB.jovian);  // chained pair shares one fork (isthmus)
    OpTwoPhaseFixture fixture;
    w6test::seedPreState(fixture.multiLayerStorage, sampleA.vector["pre"]);
    const auto goldenHeaderA = w6test::decodeGoldenHeader(sampleA);
    registerVerifiedBlock(fixture.multiLayerStorage, goldenHeaderA->parentInfo().blockHash, 0);

    auto requestA = bcos::rpc::parseNewPayloadRequest(w6test::makeParamsJson(sampleA),
        *fixture.blockFactory->transactionFactory(), bcos::engine::ApiVersion::V4);
    auto requestB = bcos::rpc::parseNewPayloadRequest(w6test::makeParamsJson(sampleB),
        *fixture.blockFactory->transactionFactory(), bcos::engine::ApiVersion::V4);

    // Cycle 1: execute + commit A.
    auto headerA = productionHeaderOf(fixture.blockFactory, requestA);
    auto const& rawTxsA = *requestA.executionPayload.rawTransactions;
    auto viewA = fixture.multiLayerStorage.fork();
    viewA.newMutable();
    bcos::ledger::LedgerConfig ledgerConfig;
    auto receiptsA = bcos::task::syncWait(
        fixture.scheduler.executeBlock(viewA, fixture.executor, *headerA, rawTxsA, ledgerConfig));
    BOOST_REQUIRE(!receiptsA.empty());
    bcos::task::syncWait(fixture.scheduler.commitBlock(
        headerA, bcos::h256(std::string(sampleA.golden["blockHash"].asString()))));

    // Cycle 2: execute + commit B (A's post is B's pre; the state already advanced via commit A).
    auto headerB = productionHeaderOf(fixture.blockFactory, requestB);
    auto const& rawTxsB = *requestB.executionPayload.rawTransactions;
    auto viewB = fixture.multiLayerStorage.fork();
    viewB.newMutable();
    auto receiptsB = bcos::task::syncWait(
        fixture.scheduler.executeBlock(viewB, fixture.executor, *headerB, rawTxsB, ledgerConfig));
    BOOST_REQUIRE(!receiptsB.empty());
    bcos::task::syncWait(fixture.scheduler.commitBlock(
        headerB, bcos::h256(std::string(sampleB.golden["blockHash"].asString()))));

    // Both blocks landed: SYS_HASH_2_NUMBER resolves both hashes (m_pending survived a full
    // execute->commit->execute->commit cycle).
    auto checkView = fixture.multiLayerStorage.fork();
    auto numA = bcos::task::syncWait(bcos::storage2::readOne(checkView,
        StateKey{bcos::ledger::SYS_HASH_2_NUMBER,
            bcos::concepts::bytebuffer::toView(
                bcos::h256(std::string(sampleA.golden["blockHash"].asString())))}));
    auto numB = bcos::task::syncWait(bcos::storage2::readOne(checkView,
        StateKey{bcos::ledger::SYS_HASH_2_NUMBER,
            bcos::concepts::bytebuffer::toView(
                bcos::h256(std::string(sampleB.golden["blockHash"].asString())))}));
    BOOST_REQUIRE(numA.has_value());
    BOOST_REQUIRE(numB.has_value());
}

BOOST_AUTO_TEST_CASE(ChainedAfterReset)
{
    auto sample = w6test::loadVectorSample("isthmus_deposit_only");
    OpTwoPhaseFixture fixture;
    w6test::seedPreState(fixture.multiLayerStorage, sample.vector["pre"]);
    const auto goldenHeader = w6test::decodeGoldenHeader(sample);
    registerVerifiedBlock(fixture.multiLayerStorage, goldenHeader->parentInfo().blockHash, 0);

    auto request = bcos::rpc::parseNewPayloadRequest(w6test::makeParamsJson(sample),
        *fixture.blockFactory->transactionFactory(), bcos::engine::ApiVersion::V4);
    auto header = productionHeaderOf(fixture.blockFactory, request);
    auto const& rawTxs = *request.executionPayload.rawTransactions;
    bcos::ledger::LedgerConfig ledgerConfig;

    // First execute stages a result...
    auto view1 = fixture.multiLayerStorage.fork();
    view1.newMutable();
    bcos::task::syncWait(
        fixture.scheduler.executeBlock(view1, fixture.executor, *header, rawTxs, ledgerConfig));
    BOOST_CHECK_NO_THROW(fixture.scheduler.pendingExecuteResult());

    // ...the compare-INVALID branch resets it (drops the mutable-layer view, no commit)...
    fixture.scheduler.resetPending();
    BOOST_CHECK_THROW(
        fixture.scheduler.pendingExecuteResult(), bcos::engine::OpExecutionInternalError);

    // ...and a fresh execute + commit self-heals.
    auto view2 = fixture.multiLayerStorage.fork();
    view2.newMutable();
    auto receipts = bcos::task::syncWait(
        fixture.scheduler.executeBlock(view2, fixture.executor, *header, rawTxs, ledgerConfig));
    BOOST_REQUIRE(!receipts.empty());
    const auto goldenBlockHash = bcos::h256(std::string(sample.golden["blockHash"].asString()));
    bcos::task::syncWait(fixture.scheduler.commitBlock(header, goldenBlockHash));

    auto checkView = fixture.multiLayerStorage.fork();
    auto num2hash = bcos::task::syncWait(
        bcos::storage2::readOne(checkView, StateKey{bcos::ledger::SYS_NUMBER_2_HASH, "1"}));
    BOOST_REQUIRE(num2hash.has_value());
}

BOOST_AUTO_TEST_SUITE_END()
