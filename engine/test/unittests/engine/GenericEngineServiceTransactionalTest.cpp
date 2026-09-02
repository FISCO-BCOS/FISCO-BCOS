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
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

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
    std::shared_ptr<std::atomic<bool>> throwOnMerge = std::make_shared<std::atomic<bool>>(false);

    ViewType fork() { return inner.fork(); }
    void pushView(ViewType view) { inner.pushView(std::move(view)); }

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

}  // namespace generic_tx_test

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

    ForkchoiceState forkchoice{
        h256("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
        h256("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"),
        h256("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc")};

    auto writeBlock = [&](GateMergeStorage& storageFixture, h256 const& hash) {
        storage::Entry entry;
        entry.set("5");
        task::syncWait(bcos::storage2::writeOne(storageFixture.backendStorage,
            bcos::executor_v1::StateKey{
                ledger::SYS_HASH_2_NUMBER, bcos::concepts::bytebuffer::toView(hash)},
            std::move(entry)));
    };
    writeBlock(storage, forkchoice.headBlockHash);
    writeBlock(storage, forkchoice.safeBlockHash);
    writeBlock(storage, forkchoice.finalizedBlockHash);

    PayloadAttributes attrs;
    attrs.timestamp = 123;
    attrs.prevRandao = h256(std::string(64, '1'));
    attrs.suggestedFeeRecipient = Address(std::string(40, '2'));
    attrs.withdrawals = std::vector<WithdrawalV1>{};
    attrs.parentBeaconBlockRoot = h256(std::string(64, '3'));

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

    ForkchoiceState forkchoice{
        h256("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
        h256("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"),
        h256("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc")};

    auto seedStorage = [&](GateMergeStorage& storageFixture) {
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
    };
    seedStorage(legacyStorage);
    seedStorage(newStorage);

    PayloadAttributes attrs;
    attrs.timestamp = 456;
    attrs.prevRandao = h256(std::string(64, '4'));
    attrs.suggestedFeeRecipient = Address(std::string(40, '5'));
    attrs.withdrawals = std::vector<WithdrawalV1>{};
    attrs.parentBeaconBlockRoot = h256(std::string(64, '6'));

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
    std::thread legacyThread([&] {
        try
        {
            task::syncWait(legacy.newPayload(legacyRequest, 3));
        }
        catch (...)
        {}
    });
    std::thread newThread([&] {
        try
        {
            task::syncWait(fresh.newPayload(newRequest, 3));
        }
        catch (...)
        {}
    });

    auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (legacyStorage.mergeStarted->load() && newStorage.mergeStarted->load())
        {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    BOOST_REQUIRE(legacyStorage.mergeStarted->load());
    BOOST_REQUIRE(newStorage.mergeStarted->load());

    std::thread legacyProbe([&] {
        task::syncWait(legacy.getPayload(*legacyBuild.payloadId, 3));
        legacyProbeDone.store(true);
    });
    std::thread newProbe([&] {
        task::syncWait(fresh.getPayload(*newBuild.payloadId, 3));
        newProbeDone.store(true);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    BOOST_CHECK(!legacyProbeDone.load());
    BOOST_CHECK(!newProbeDone.load());

    legacyStorage.mergeGate->store(true);
    newStorage.mergeGate->store(true);

    legacyThread.join();
    newThread.join();
    legacyProbe.join();
    newProbe.join();

    BOOST_CHECK(legacyProbeDone.load());
    BOOST_CHECK(newProbeDone.load());
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
