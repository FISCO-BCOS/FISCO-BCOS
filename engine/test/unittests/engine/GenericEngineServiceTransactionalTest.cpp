/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#include "engine/bcos-engine/EngineServiceImpl.h"
#include "engine/bcos-engine/EngineTracker.h"
#include "engine/bcos-engine/GenericEngineService.h"
#include "engine/bcos-engine/PayloadCache.h"

#include <bcos-concepts/ByteBuffer.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/testutils/faker/FakeBlock.h>
#include <bcos-framework/testutils/faker/FakeLedger.h>
#include <bcos-mempool/MemPoolImpl.h>
#include <bcos-task/Wait.h>
#include <boost/test/unit_test.hpp>

#include <atomic>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

using namespace bcos;
using namespace bcos::engine;
using namespace bcos::txpool;

namespace generic_tx_test
{

using RealGlobalStateMutableStorage = bcos::storage2::memory_storage::MemoryStorage<
    bcos::executor_v1::StateKey, bcos::executor_v1::StateValue,
    bcos::storage2::memory_storage::Attribute(bcos::storage2::memory_storage::ORDERED |
                                              bcos::storage2::memory_storage::LOGICAL_DELETION)>;
using RealGlobalStateBackendStorage =
    bcos::storage2::memory_storage::MemoryStorage<bcos::executor_v1::StateKey,
        bcos::executor_v1::StateValue,
        bcos::storage2::memory_storage::Attribute(
            bcos::storage2::memory_storage::ORDERED | bcos::storage2::memory_storage::CONCURRENT),
        std::hash<bcos::executor_v1::StateKey>>;

template <class Key, class Value, bcos::storage2::ReadWriteStorage<Key, Value> Storage>
struct TrivialCheckpointStorage
{
    using CheckpointName = bcos::h256;
    Storage& m_storage;
    explicit TrivialCheckpointStorage(Storage& s) : m_storage(s) {}
    Storage& open() { return m_storage; }
    [[noreturn]] Storage& open(CheckpointName const&) { std::abort(); }
    void createCheckpoint(Storage&, CheckpointName const&) {}
    void deleteCheckpoint(CheckpointName const&) {}
    std::optional<CheckpointName> latestCheckpointName() const { return std::nullopt; }
    std::optional<CheckpointName> oldestCheckpointName() const { return std::nullopt; }
};

using RealGlobalCheckpointBackend = TrivialCheckpointStorage<bcos::executor_v1::StateKey,
    bcos::executor_v1::StateValue, RealGlobalStateBackendStorage>;
using RealGlobalStateStorage = bcos::storage2::MultiLayerStorage<RealGlobalStateMutableStorage,
    void, RealGlobalCheckpointBackend>;

struct GateMergeStorage
{
    using ViewType = RealGlobalStateStorage::ViewType;
    using MutableStorage = RealGlobalStateStorage::MutableStorage;

    RealGlobalStateBackendStorage backendStorage;
    RealGlobalCheckpointBackend checkpointBackend{backendStorage};
    RealGlobalStateStorage inner{checkpointBackend};
    std::shared_ptr<std::atomic<bool>> mergeGate = std::make_shared<std::atomic<bool>>(false);
    std::shared_ptr<std::atomic<bool>> mergeStarted = std::make_shared<std::atomic<bool>>(false);
    std::shared_ptr<std::atomic<bool>> pushViewGate = std::make_shared<std::atomic<bool>>(true);
    std::shared_ptr<std::atomic<bool>> pushViewStarted = std::make_shared<std::atomic<bool>>(false);
    std::shared_ptr<std::atomic<bool>> throwOnMerge = std::make_shared<std::atomic<bool>>(false);

    ViewType fork() { return inner.fork(); }
    void pushView(ViewType view)
    {
        pushViewStarted->store(true, std::memory_order_release);
        while (!pushViewGate->load(std::memory_order_acquire))
        {
            std::this_thread::yield();
        }
        inner.pushView(std::move(view));
    }

    task::Task<std::shared_ptr<MutableStorage>> mergeBackStorage()
    {
        mergeStarted->store(true, std::memory_order_release);
        while (!mergeGate->load(std::memory_order_acquire))
        {
            std::this_thread::yield();
        }
        if (throwOnMerge->load())
        {
            BOOST_THROW_EXCEPTION(std::runtime_error{"merge failed"});
        }
        co_return co_await inner.mergeBackStorage();
    }

    task::Task<std::shared_ptr<MutableStorage>> mergeBackStorage(MutableStorage& extra)
    {
        mergeStarted->store(true, std::memory_order_release);
        while (!mergeGate->load(std::memory_order_acquire))
        {
            std::this_thread::yield();
        }
        if (throwOnMerge->load())
        {
            BOOST_THROW_EXCEPTION(std::runtime_error{"merge failed"});
        }
        co_return co_await inner.mergeBackStorage(extra);
    }
};

struct StubExecutor
{
    template <class Storage>
    struct ExecuteContext
    {
        task::Task<void> prepare() { co_return; }
        task::Task<void> execute() { co_return; }
        task::Task<protocol::TransactionReceipt::Ptr> finish() { co_return nullptr; }
    };

    template <class Storage>
    task::Task<protocol::TransactionReceipt::Ptr> executeTransaction(Storage&,
        const protocol::BlockHeader&, const protocol::Transaction&, int,
        const ledger::LedgerConfig&, bool)
    {
        co_return nullptr;
    }

    template <class Storage>
    task::Task<ExecuteContext<Storage>> createExecuteContext(Storage&, const protocol::BlockHeader&,
        const protocol::Transaction&, int, const ledger::LedgerConfig&, bool)
    {
        co_return ExecuteContext<Storage>{};
    }
};

struct StubScheduler
{
    template <class Storage, class Executor>
    task::Task<std::vector<protocol::TransactionReceipt::Ptr>> executeBlock(Storage&, Executor&,
        const protocol::BlockHeader&, ::ranges::input_range auto&&, const ledger::LedgerConfig&)
    {
        co_return {};
    }
};

struct ArtifactNode
{
    int value = 0;
};

struct ThrowingArtifactsMap
{
    std::unordered_map<PayloadID, ArtifactNode> inner;
    bool throwOnAssign = false;

    struct AssignProxy
    {
        ArtifactNode& slot;
        bool& throwFlag;
        AssignProxy(ArtifactNode& s, bool& f) : slot(s), throwFlag(f) {}
        AssignProxy& operator=(ArtifactNode&&)
        {
            if (throwFlag)
            {
                throw std::runtime_error{"artifact assign failed"};
            }
            slot.value = 1;
            return *this;
        }
    };

    AssignProxy operator[](PayloadID const& id) { return AssignProxy(inner[id], throwOnAssign); }

    void erase(PayloadID const& id) { inner.erase(id); }
};

CommonPayloadEntryPtr makeEntry(PayloadID const& id)
{
    auto entry = std::make_shared<CommonPayloadEntry>();
    entry->version = 3;
    entry->executionPayload.blockNumber = static_cast<bcos::protocol::BlockNumber>(id.size());
    return entry;
}

struct GateRelease
{
    std::vector<std::jthread*> threads;
    std::vector<std::shared_ptr<std::atomic<bool>>> gates;

    ~GateRelease()
    {
        for (auto const& gate : gates)
        {
            if (gate)
            {
                gate->store(true, std::memory_order_release);
            }
        }
        for (auto* thread : threads)
        {
            if (thread != nullptr && thread->joinable())
            {
                thread->join();
            }
        }
    }
};

class GatedFakeLedger : public bcos::test::FakeLedger
{
public:
    using FakeLedger::FakeLedger;
};

template <class ViewType>
struct ThrowingPayloadArtifactsMap
{
    std::unordered_map<PayloadID, GenericPayloadArtifacts<ViewType>> inner;
    bool throwOnAssign = false;

    struct AssignProxy
    {
        GenericPayloadArtifacts<ViewType>& slot;
        bool& throwFlag;
        AssignProxy(GenericPayloadArtifacts<ViewType>& s, bool& f) : slot(s), throwFlag(f) {}
        AssignProxy& operator=(GenericPayloadArtifacts<ViewType>&& other)
        {
            if (throwFlag)
            {
                throw std::runtime_error{"artifact assign failed"};
            }
            slot = std::move(other);
            return *this;
        }
    };

    AssignProxy operator[](PayloadID const& id) { return AssignProxy(inner[id], throwOnAssign); }

    void erase(PayloadID const& id) { inner.erase(id); }

    ThrowingPayloadArtifactsMap duplicate() const { return *this; }
};

template <class ViewType>
struct ThrowingClearArtifactStore
{
    std::unordered_map<PayloadID, GenericPayloadArtifacts<ViewType>> inner;
    bool throwOnClear = false;

    void clear()
    {
        if (throwOnClear)
        {
            throw std::runtime_error{"artifacts clear failed"};
        }
        inner.clear();
    }
};

ForkchoiceState makeForkchoiceState()
{
    return {h256("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
        h256("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"),
        h256("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc")};
}

void seedForkchoiceStorage(GateMergeStorage& storageFixture, ForkchoiceState const& forkchoice)
{
    auto writeBlock = [&](h256 const& hash) {
        storage::Entry entry;
        entry.set("5");
        task::syncWait(bcos::storage2::writeOne(storageFixture.backendStorage,
            bcos::executor_v1::StateKey{
                ledger::SYS_HASH_2_NUMBER, bcos::concepts::bytebuffer::toView(hash)},
            std::move(entry)));
    };
    writeBlock(forkchoice.headBlockHash);
    writeBlock(forkchoice.safeBlockHash);
    writeBlock(forkchoice.finalizedBlockHash);
}

PayloadAttributes makeAttrs(std::uint64_t timestamp)
{
    PayloadAttributes attrs;
    attrs.timestamp = timestamp;
    attrs.prevRandao = h256(std::string(64, '1'));
    attrs.suggestedFeeRecipient = Address(std::string(40, '2'));
    attrs.withdrawals = std::vector<WithdrawalV1>{};
    attrs.parentBeaconBlockRoot = h256(std::string(64, '3'));
    return attrs;
}

}  // namespace generic_tx_test

namespace generic_tx_test
{

BOOST_AUTO_TEST_SUITE(GenericEngineServiceTransactionalTest)

using namespace generic_tx_test;

BOOST_AUTO_TEST_CASE(payload_cache_put_and_retain_only_is_atomic)
{
    PayloadCache cache;
    for (std::uint64_t i = 0; i < 3; ++i)
    {
        auto entry = makeEntry("0x0" + std::to_string(i));
        cache.put("0x0" + std::to_string(i), h256(i + 1), entry);
    }
    BOOST_REQUIRE(cache.find("0x00"));
    BOOST_REQUIRE(cache.find("0x01"));

    auto retained = makeEntry("0xretained");
    cache.putAndRetainOnly("0xretained", h256(99), retained);

    BOOST_CHECK(!cache.find("0x00"));
    BOOST_CHECK(!cache.find("0x01"));
    BOOST_REQUIRE(cache.find("0xretained"));
    BOOST_CHECK_EQUAL(*cache.payloadIdForHash(h256(99)), "0xretained");
}

BOOST_AUTO_TEST_CASE(payload_cache_publish_from_restores_snapshot)
{
    PayloadCache cache;
    cache.put("0x01", h256(1), makeEntry("0x01"));
    auto snapshot = cache.duplicate();
    cache.put("0x02", h256(2), makeEntry("0x02"));
    BOOST_REQUIRE(cache.find("0x02"));

    cache.publishFrom(std::move(snapshot));
    BOOST_REQUIRE(cache.find("0x01"));
    BOOST_CHECK(!cache.find("0x02"));
}

BOOST_AUTO_TEST_CASE(publish_built_payload_rolls_back_cache_and_artifacts_on_throw)
{
    EngineTracker tracker;
    auto guard = tracker.lockExclusive();
    guard.putPayload("0xexisting", h256(1), makeEntry("0xexisting"));

    ThrowingArtifactsMap artifacts;
    artifacts.inner.emplace("0xexisting", ArtifactNode{.value = 42});
    artifacts.throwOnAssign = true;

    PayloadID newId = "0xnew";
    BOOST_CHECK_THROW(generic_detail::publishBuiltPayload(guard, artifacts, newId, h256(2),
                          makeEntry(newId), ArtifactNode{.value = 7}),
        std::runtime_error);

    BOOST_REQUIRE(guard.findPayload("0xexisting"));
    BOOST_CHECK(!guard.findPayload(newId));
    BOOST_CHECK_EQUAL(artifacts.inner.at("0xexisting").value, 42);
    BOOST_CHECK(!artifacts.inner.contains(newId));
}

BOOST_AUTO_TEST_CASE(publish_built_payload_preserves_prior_generic_artifact)
{
    EngineTracker tracker;
    ThrowingPayloadArtifactsMap<RealGlobalStateStorage::ViewType> artifacts;
    GateMergeStorage storage;
    auto blockFactory = bcos::test::createBlockFactory(bcos::test::createNormalCryptoSuite());
    auto guard = tracker.lockExclusive();

    PayloadID keepId = "0xkeep";
    auto header = blockFactory->blockHeaderFactory()->createBlockHeader();
    header->setTimestamp(424242);
    GenericPayloadArtifacts<RealGlobalStateStorage::ViewType> staged{
        .view = std::make_shared<RealGlobalStateStorage::ViewType>(storage.fork()),
        .header = std::move(header),
        .receipts = {},
    };
    generic_detail::publishBuiltPayload(
        guard, artifacts, keepId, h256(1), makeEntry(keepId), std::move(staged));

    artifacts.throwOnAssign = true;
    PayloadID failingId = "0xfailing";
    BOOST_CHECK_THROW(
        generic_detail::publishBuiltPayload(guard, artifacts, failingId, h256(99),
            makeEntry(failingId), GenericPayloadArtifacts<RealGlobalStateStorage::ViewType>{}),
        std::runtime_error);

    BOOST_REQUIRE(guard.findPayload(keepId));
    BOOST_CHECK(!guard.findPayload(failingId));
    BOOST_REQUIRE(artifacts.inner.contains(keepId));
    BOOST_CHECK_EQUAL(artifacts.inner.at(keepId).header->timestamp(), 424242);
}

BOOST_AUTO_TEST_CASE(commit_retained_payload_rolls_back_on_artifacts_clear_throw)
{
    EngineTracker tracker;
    ThrowingClearArtifactStore<RealGlobalStateStorage::ViewType> artifacts;
    auto guard = tracker.lockExclusive();
    guard.putPayload("0xkeep", h256(1), makeEntry("0xkeep"));
    GateMergeStorage storage;
    auto header = bcos::test::createBlockFactory(bcos::test::createNormalCryptoSuite())
                      ->blockHeaderFactory()
                      ->createBlockHeader();
    header->setTimestamp(515151);
    artifacts.inner.emplace(
        "0xkeep", GenericPayloadArtifacts<RealGlobalStateStorage::ViewType>{
                      .view = std::make_shared<RealGlobalStateStorage::ViewType>(storage.fork()),
                      .header = std::move(header),
                      .receipts = {},
                  });

    artifacts.throwOnClear = true;
    BOOST_CHECK_THROW(generic_detail::commitRetainedPayload(
                          guard, artifacts, "0xcommit", h256(2), makeEntry("0xcommit")),
        std::runtime_error);

    BOOST_REQUIRE(guard.findPayload("0xkeep"));
    BOOST_CHECK(!guard.findPayload("0xcommit"));
    BOOST_REQUIRE(artifacts.inner.contains("0xkeep"));
    BOOST_CHECK_EQUAL(artifacts.inner.at("0xkeep").header->timestamp(), 515151);
}

BOOST_AUTO_TEST_CASE(commit_merge_failure_leaves_cache_and_artifacts)
{
    GateMergeStorage storage;
    MemPoolImpl memPool;
    StubExecutor executor;
    StubScheduler scheduler;
    auto blockFactory = bcos::test::createBlockFactory(bcos::test::createNormalCryptoSuite());
    auto ledger = std::make_shared<bcos::test::FakeLedger>(blockFactory, 20, 10, 10);

    using Service =
        GenericEngineService<MemPoolImpl, GateMergeStorage, StubExecutor, StubScheduler>;
    Service service(memPool, storage, executor, scheduler, blockFactory, ledger);

    auto forkchoice = makeForkchoiceState();
    seedForkchoiceStorage(storage, forkchoice);

    PayloadAttributes attrs = makeAttrs(123);
    auto build = task::syncWait(service.updateForkchoice(forkchoice, &attrs, 3));
    BOOST_REQUIRE(build.payloadId.has_value());
    auto payload = task::syncWait(service.getPayload(*build.payloadId, 3));

    storage.throwOnMerge->store(true);
    storage.mergeGate->store(true);

    NewPayloadRequest request;
    request.executionPayload = payload->executionPayload;
    request.parentBeaconBlockRoot = attrs.parentBeaconBlockRoot;

    BOOST_CHECK_THROW(task::syncWait(service.newPayload(request, 3)), std::runtime_error);
    BOOST_CHECK_NO_THROW(task::syncWait(service.getPayload(*build.payloadId, 3)));

    storage.throwOnMerge->store(false);
    auto retryStatus = task::syncWait(service.newPayload(request, 3));
    BOOST_CHECK_EQUAL(
        static_cast<int>(retryStatus.status), static_cast<int>(PayloadValidationStatus::Valid));
}

BOOST_AUTO_TEST_CASE(commit_holds_exclusive_guard_during_merge)
{
    GateMergeStorage legacyStorage;
    GateMergeStorage newStorage;
    MemPoolImpl legacyMemPool;
    MemPoolImpl newMemPool;
    StubExecutor legacyExecutor;
    StubExecutor newExecutor;
    StubScheduler legacyScheduler;
    StubScheduler newScheduler;
    auto blockFactory = bcos::test::createBlockFactory(bcos::test::createNormalCryptoSuite());

    using LegacyService =
        EngineServiceImpl<MemPoolImpl, GateMergeStorage, StubExecutor, StubScheduler>;
    using NewService =
        GenericEngineService<MemPoolImpl, GateMergeStorage, StubExecutor, StubScheduler>;

    LegacyService legacy(
        legacyMemPool, legacyStorage, legacyExecutor, legacyScheduler, blockFactory);
    NewService fresh(newMemPool, newStorage, newExecutor, newScheduler, blockFactory);

    auto forkchoice = makeForkchoiceState();
    seedForkchoiceStorage(legacyStorage, forkchoice);
    seedForkchoiceStorage(newStorage, forkchoice);

    PayloadAttributes attrs = makeAttrs(456);
    auto legacyBuild = task::syncWait(legacy.updateForkchoice(forkchoice, &attrs, 3));
    auto newBuild = task::syncWait(fresh.updateForkchoice(forkchoice, &attrs, 3));
    BOOST_REQUIRE(legacyBuild.payloadId.has_value());
    BOOST_REQUIRE(newBuild.payloadId.has_value());

    auto legacyPayload = task::syncWait(legacy.getPayload(*legacyBuild.payloadId, 3));
    auto newPayload = task::syncWait(fresh.getPayload(*newBuild.payloadId, 3));

    NewPayloadRequest legacyRequest;
    legacyRequest.executionPayload = legacyPayload->executionPayload;
    legacyRequest.parentBeaconBlockRoot = attrs.parentBeaconBlockRoot;
    NewPayloadRequest newRequest;
    newRequest.executionPayload = newPayload->executionPayload;
    newRequest.parentBeaconBlockRoot = attrs.parentBeaconBlockRoot;

    std::atomic<bool> legacyProbeDone{false};
    std::atomic<bool> newProbeDone{false};
    bool mergeStarted = false;
    bool probeBlockedDuringMerge = false;
    {
        std::jthread legacyThread([&](std::stop_token) {
            try
            {
                task::syncWait(legacy.newPayload(legacyRequest, 3));
            }
            catch (...)
            {}
        });
        std::jthread newThread([&](std::stop_token) {
            try
            {
                task::syncWait(fresh.newPayload(newRequest, 3));
            }
            catch (...)
            {}
        });
        std::jthread legacyProbe([&](std::stop_token) {
            task::syncWait(legacy.getPayload(*legacyBuild.payloadId, 3));
            legacyProbeDone.store(true, std::memory_order_release);
        });
        std::jthread newProbe([&](std::stop_token) {
            task::syncWait(fresh.getPayload(*newBuild.payloadId, 3));
            newProbeDone.store(true, std::memory_order_release);
        });
        GateRelease cleanup{
            .threads = {&legacyThread, &newThread, &legacyProbe, &newProbe},
            .gates = {legacyStorage.mergeGate, newStorage.mergeGate},
        };

        auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (legacyStorage.mergeStarted->load(std::memory_order_acquire) &&
                newStorage.mergeStarted->load(std::memory_order_acquire))
            {
                mergeStarted = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        if (mergeStarted)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            probeBlockedDuringMerge = !legacyProbeDone.load(std::memory_order_acquire) &&
                                      !newProbeDone.load(std::memory_order_acquire);
        }
    }

    BOOST_CHECK(mergeStarted);
    BOOST_CHECK(probeBlockedDuringMerge);
    BOOST_CHECK(legacyProbeDone.load(std::memory_order_acquire));
    BOOST_CHECK(newProbeDone.load(std::memory_order_acquire));
}

BOOST_AUTO_TEST_CASE(commit_holds_exclusive_guard_during_prewrite_path)
{
    GateMergeStorage storage;
    MemPoolImpl memPool;
    StubExecutor executor;
    StubScheduler scheduler;
    auto blockFactory = bcos::test::createBlockFactory(bcos::test::createNormalCryptoSuite());
    auto ledger = std::make_shared<GatedFakeLedger>(blockFactory, 20, 10, 10);

    using Service =
        GenericEngineService<MemPoolImpl, GateMergeStorage, StubExecutor, StubScheduler>;
    Service service(memPool, storage, executor, scheduler, blockFactory, ledger);

    auto forkchoice = makeForkchoiceState();
    seedForkchoiceStorage(storage, forkchoice);

    PayloadAttributes attrs = makeAttrs(789);
    auto build = task::syncWait(service.updateForkchoice(forkchoice, &attrs, 3));
    BOOST_REQUIRE(build.payloadId.has_value());
    auto payload = task::syncWait(service.getPayload(*build.payloadId, 3));

    NewPayloadRequest request;
    request.executionPayload = payload->executionPayload;
    request.parentBeaconBlockRoot = attrs.parentBeaconBlockRoot;

    std::atomic<bool> probeDone{false};
    bool pushViewStarted = false;
    bool probeBlockedDuringCommit = false;
    storage.pushViewGate->store(false, std::memory_order_release);
    {
        std::jthread commitThread([&](std::stop_token) {
            try
            {
                task::syncWait(service.newPayload(request, 3));
            }
            catch (...)
            {}
        });
        std::jthread probeThread([&](std::stop_token) {
            task::syncWait(service.getPayload(*build.payloadId, 3));
            probeDone.store(true, std::memory_order_release);
        });
        GateRelease cleanup{
            .threads = {&commitThread, &probeThread},
            .gates = {storage.pushViewGate, storage.mergeGate},
        };

        auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (storage.pushViewStarted->load(std::memory_order_acquire))
            {
                pushViewStarted = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        if (pushViewStarted)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            probeBlockedDuringCommit = !probeDone.load(std::memory_order_acquire);
        }
    }

    BOOST_CHECK(pushViewStarted);
    BOOST_CHECK(probeBlockedDuringCommit);
    BOOST_CHECK(probeDone.load(std::memory_order_acquire));
}

BOOST_AUTO_TEST_CASE(engine_tracker_bounded_put_fifo_evicts_oldest)
{
    EngineTracker tracker;
    auto guard = tracker.lockExclusive();
    std::vector<PayloadID> ids;
    for (std::uint64_t i = 0; i < 65; ++i)
    {
        PayloadID id = "0xbounded" + std::to_string(i);
        ids.push_back(id);
        guard.putPayload(id, h256(i + 1), makeEntry(id));
    }
    BOOST_CHECK(!guard.findPayload(ids.front()));
    BOOST_REQUIRE(guard.findPayload(ids[1]));
    BOOST_REQUIRE(guard.findPayload(ids.back()));
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace generic_tx_test
