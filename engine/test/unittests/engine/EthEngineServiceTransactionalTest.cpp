/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file EthEngineServiceTransactionalTest.cpp
 * @brief Transactional (retry/commit) tests for EthEngineService
 */

#include "engine/bcos-engine/EngineServiceImpl.h"
#include "engine/bcos-engine/EngineTracker.h"
#include "engine/bcos-engine/EthEngineService.h"
#include "engine/bcos-engine/PayloadCache.h"
#include "engine/test/unittests/engine/EthServiceStubs.h"

#include <bcos-concepts/ByteBuffer.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage/Serialize.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/testutils/faker/FakeBlock.h>
#include <bcos-framework/testutils/faker/FakeLedger.h>
#include <bcos-mempool/MemPoolImpl.h>
#include <bcos-task/Wait.h>
#include <evmc/evmc.h>
#include <boost/test/unit_test.hpp>
#include <magic_enum/magic_enum.hpp>

#include <atomic>
#include <functional>
#include <thread>
#include <vector>

using namespace bcos;
using namespace bcos::engine;
using namespace bcos::txpool;

namespace eth_tx_test
{

using namespace bcos::engine::eth_test;

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

    task::Task<void> mergeToBackends(MutableStorage& extra)
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
        co_await inner.mergeToBackends(extra);
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

BuiltPayloadPtr makeEntry(PayloadID const& id)
{
    auto entry = std::make_shared<BuiltPayload>();
    entry->version = 3;
    entry->executionPayload.blockNumber = static_cast<bcos::protocol::BlockNumber>(id.size());
    return entry;
}

struct CommitCleanup
{
    std::vector<std::thread*> threads;
    std::vector<std::shared_ptr<std::atomic<bool>>> gates;

    ~CommitCleanup()
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

bool waitUntil(std::chrono::steady_clock::duration timeout, auto&& predicate)
{
    auto const deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (predicate())
        {
            return true;
        }
        std::this_thread::yield();
    }
    return predicate();
}

class GatedFakeLedger : public bcos::test::FakeLedger
{
public:
    using FakeLedger::FakeLedger;

    std::atomic<bool> prewriteStarted{false};
    std::shared_ptr<std::atomic<bool>> prewriteGate = std::make_shared<std::atomic<bool>>(false);

    void asyncPrewriteBlock(bcos::storage::StorageInterface::Ptr storage,
        bcos::protocol::ConstTransactionsPtr blockTxs, bcos::protocol::Block::ConstPtr block,
        std::function<void(std::string, Error::Ptr&&)> callback, bool writeTxsAndReceipts,
        std::optional<bcos::ledger::Features> features,
        std::optional<bcos::crypto::HashType> blockHashOverride, bool writeNonces) override
    {
        (void)storage;
        (void)blockTxs;
        (void)block;
        (void)writeTxsAndReceipts;
        (void)features;
        (void)blockHashOverride;
        (void)writeNonces;
        prewriteStarted.store(true, std::memory_order_release);
        // prewriteBlockToBuffer suspends until this callback runs; keep it on the
        // commit thread (spin/yield) so task::syncWait can resume without a
        // cross-thread handle.resume() deadlock.
        while (!prewriteGate->load(std::memory_order_acquire))
        {
            std::this_thread::yield();
        }
        callback("", nullptr);
    }
};

template <class ViewType>
struct ThrowingPayloadArtifactsMap
{
    std::unordered_map<PayloadID, EthPayloadArtifacts<ViewType>> inner;
    bool throwOnAssign = false;

    struct AssignProxy
    {
        EthPayloadArtifacts<ViewType>& slot;
        bool& throwFlag;
        AssignProxy(EthPayloadArtifacts<ViewType>& s, bool& f) : slot(s), throwFlag(f) {}
        AssignProxy& operator=(EthPayloadArtifacts<ViewType>&& other)
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
    std::unordered_map<PayloadID, EthPayloadArtifacts<ViewType>> inner;
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
    auto writeSysConfig = [&](std::string_view key, std::string value) {
        storage::Entry entry;
        entry.set(bcos::storage::serialize::encode(ledger::SystemConfigEntry{std::move(value), 0}));
        task::syncWait(bcos::storage2::writeOne(storageFixture.backendStorage,
            bcos::executor_v1::StateKey{ledger::SYS_CONFIG, key}, std::move(entry)));
    };
    writeSysConfig(magic_enum::enum_name(ledger::SystemConfig::executor_version),
        std::to_string(ledger::ETHEREUM_EXECUTOR_VERSION));
    writeSysConfig(
        ledger::SYSTEM_KEY_EVMC_REVISION, ledger::encodeEVMCRevisionConfig(EVMC_CANCUN, {}));

    auto writeBlock = [&](h256 const& hash, char const* number) {
        storage::Entry entry;
        entry.set(number);
        task::syncWait(bcos::storage2::writeOne(storageFixture.backendStorage,
            bcos::executor_v1::StateKey{
                ledger::SYS_HASH_2_NUMBER, bcos::concepts::bytebuffer::toView(hash)},
            std::move(entry)));
        storage::Entry hashEntry;
        hashEntry.set(hash.asBytes());
        task::syncWait(bcos::storage2::writeOne(storageFixture.backendStorage,
            bcos::executor_v1::StateKey{ledger::SYS_NUMBER_2_HASH, number}, std::move(hashEntry)));
    };
    writeBlock(forkchoice.finalizedBlockHash, "3");
    writeBlock(forkchoice.safeBlockHash, "4");
    writeBlock(forkchoice.headBlockHash, "5");
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

}  // namespace eth_tx_test

namespace eth_tx_test
{

BOOST_AUTO_TEST_SUITE(EthEngineServiceTransactionalTest)

using namespace eth_tx_test;

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
    BOOST_CHECK_THROW(publishBuiltPayload(guard, artifacts, newId, h256(2), makeEntry(newId),
                          ArtifactNode{.value = 7}),
        std::runtime_error);

    BOOST_REQUIRE(guard.findPayload("0xexisting"));
    BOOST_CHECK(!guard.findPayload(newId));
    BOOST_CHECK_EQUAL(artifacts.inner.at("0xexisting").value, 42);
    BOOST_CHECK(!artifacts.inner.contains(newId));
}

BOOST_AUTO_TEST_CASE(publish_built_payload_restores_fifo_evicted_on_artifacts_throw)
{
    EngineTracker tracker;
    auto guard = tracker.lockExclusive();

    // PayloadCache::put FIFO cap is 64. Fill it, then a 65th put evicts the oldest.
    constexpr std::size_t c_maxEntries = 64;
    for (std::size_t i = 0; i < c_maxEntries; ++i)
    {
        auto id = "0x" + std::to_string(i);
        guard.putPayload(id, h256(i + 1), makeEntry(id));
    }
    BOOST_REQUIRE(guard.findPayload("0x0"));

    ThrowingArtifactsMap artifacts;
    artifacts.throwOnAssign = true;
    PayloadID newId = "0xnew";
    BOOST_CHECK_THROW(publishBuiltPayload(guard, artifacts, newId, h256(100), makeEntry(newId),
                          ArtifactNode{.value = 7}),
        std::runtime_error);

    BOOST_CHECK(!guard.findPayload(newId));
    BOOST_CHECK(!artifacts.inner.contains(newId));
    // Failed publish restores the pre-put snapshot, including the FIFO victim.
    BOOST_REQUIRE(guard.findPayload("0x0"));
    BOOST_REQUIRE(guard.findPayload("0x1"));
}

BOOST_AUTO_TEST_CASE(payload_cache_erase_removes_id_hash_and_order)
{
    PayloadCache cache;
    cache.put("0xkeep", h256(1), makeEntry("0xkeep"));
    cache.put("0xdrop", h256(2), makeEntry("0xdrop"));
    cache.erase("0xdrop");

    BOOST_REQUIRE(cache.find("0xkeep"));
    BOOST_CHECK(!cache.find("0xdrop"));
    BOOST_CHECK(!cache.payloadIdForHash(h256(2)).has_value());
    BOOST_CHECK_EQUAL(*cache.payloadIdForHash(h256(1)), "0xkeep");

    // A later put must not evict `keep` as if `drop` were still in FIFO order.
    for (std::size_t i = 0; i < 63; ++i)
    {
        auto id = "0xf" + std::to_string(i);
        cache.put(id, h256(10 + i), makeEntry(id));
    }
    BOOST_REQUIRE(cache.find("0xkeep"));
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
    EthPayloadArtifacts<RealGlobalStateStorage::ViewType> staged{
        .view = std::make_shared<RealGlobalStateStorage::ViewType>(storage.fork()),
        .header = std::move(header),
        .receipts = {},
    };
    publishBuiltPayload(guard, artifacts, keepId, h256(1), makeEntry(keepId), std::move(staged));

    artifacts.throwOnAssign = true;
    PayloadID failingId = "0xfailing";
    BOOST_CHECK_THROW(
        publishBuiltPayload(guard, artifacts, failingId, h256(99), makeEntry(failingId),
            EthPayloadArtifacts<RealGlobalStateStorage::ViewType>{}),
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
        "0xkeep", EthPayloadArtifacts<RealGlobalStateStorage::ViewType>{
                      .view = std::make_shared<RealGlobalStateStorage::ViewType>(storage.fork()),
                      .header = std::move(header),
                      .receipts = {},
                  });

    artifacts.throwOnClear = true;
    BOOST_CHECK_THROW(detail::commitRetainedPayload(
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

    using Service = EthEngineService<MemPoolImpl, GateMergeStorage, StubExecutor, StubScheduler>;
    Service service(memPool, storage, executor, scheduler, blockFactory, ledger);

    auto forkchoice = makeForkchoiceState();
    seedForkchoiceStorage(storage, forkchoice);

    PayloadAttributes attrs = makeAttrs(1'700'000'000'000ULL);
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
        static_cast<int>(retryStatus.status), static_cast<int>(PayloadValidationStatus::Syncing));
}

BOOST_AUTO_TEST_CASE(commit_retry_valid_when_ledger_row_exists)
{
    GateMergeStorage storage;
    MemPoolImpl memPool;
    StubExecutor executor;
    StubScheduler scheduler;
    auto blockFactory = bcos::test::createBlockFactory(bcos::test::createNormalCryptoSuite());
    auto ledger = std::make_shared<bcos::test::FakeLedger>(blockFactory, 20, 10, 10);

    using Service = EthEngineService<MemPoolImpl, GateMergeStorage, StubExecutor, StubScheduler>;
    Service service(memPool, storage, executor, scheduler, blockFactory, ledger);

    auto forkchoice = makeForkchoiceState();
    seedForkchoiceStorage(storage, forkchoice);

    PayloadAttributes attrs = makeAttrs(1'700'000'001'000ULL);
    auto build = task::syncWait(service.updateForkchoice(forkchoice, &attrs, 3));
    BOOST_REQUIRE(build.payloadId.has_value());
    auto payload = task::syncWait(service.getPayload(*build.payloadId, 3));

    storage.mergeGate->store(true);

    NewPayloadRequest request;
    request.executionPayload = payload->executionPayload;
    request.parentBeaconBlockRoot = attrs.parentBeaconBlockRoot;

    auto firstStatus = task::syncWait(service.newPayload(request, 3));
    BOOST_CHECK_EQUAL(
        static_cast<int>(firstStatus.status), static_cast<int>(PayloadValidationStatus::Valid));

    storage::Entry persisted;
    persisted.set(std::to_string(static_cast<int64_t>(request.executionPayload.blockNumber)));
    task::syncWait(bcos::storage2::writeOne(storage.backendStorage,
        bcos::executor_v1::StateKey{ledger::SYS_HASH_2_NUMBER,
            bcos::concepts::bytebuffer::toView(request.executionPayload.blockHash)},
        std::move(persisted)));

    auto retryStatus = task::syncWait(service.newPayload(request, 3));
    BOOST_CHECK_EQUAL(
        static_cast<int>(retryStatus.status), static_cast<int>(PayloadValidationStatus::Valid));
}

BOOST_AUTO_TEST_CASE(commit_releases_exclusive_guard_during_merge)
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
    using NewService = EthEngineService<MemPoolImpl, GateMergeStorage, StubExecutor, StubScheduler>;

    LegacyService legacy(
        legacyMemPool, legacyStorage, legacyExecutor, legacyScheduler, blockFactory);
    NewService fresh(newMemPool, newStorage, newExecutor, newScheduler, blockFactory);

    auto forkchoice = makeForkchoiceState();
    seedForkchoiceStorage(legacyStorage, forkchoice);
    seedForkchoiceStorage(newStorage, forkchoice);

    PayloadAttributes attrs = makeAttrs(1'700'000'001'000ULL);
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

    legacyStorage.mergeGate->store(false, std::memory_order_release);
    newStorage.mergeGate->store(false, std::memory_order_release);

    std::atomic<bool> legacyProbeFinished{false};
    std::atomic<bool> newProbeFinished{false};
    bool mergeStarted = false;
    bool probesFinishedDuringMerge = false;

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

    mergeStarted = waitUntil(std::chrono::seconds(3), [&] {
        return legacyStorage.mergeStarted->load(std::memory_order_acquire) &&
               newStorage.mergeStarted->load(std::memory_order_acquire);
    });

    std::thread legacyProbe;
    std::thread newProbe;
    if (mergeStarted)
    {
        legacyProbe = std::thread([&] {
            task::syncWait(legacy.getPayload(*legacyBuild.payloadId, 3));
            legacyProbeFinished.store(true, std::memory_order_release);
        });
        newProbe = std::thread([&] {
            task::syncWait(fresh.getPayload(*newBuild.payloadId, 3));
            newProbeFinished.store(true, std::memory_order_release);
        });

        CommitCleanup cleanup{
            .threads = {&legacyThread, &newThread, &legacyProbe, &newProbe},
            .gates = {legacyStorage.mergeGate, newStorage.mergeGate},
        };

        // Both paths release the tracker lock before gated merge, so getPayload proceeds.
        probesFinishedDuringMerge = waitUntil(std::chrono::seconds(3), [&] {
            return legacyProbeFinished.load(std::memory_order_acquire) &&
                   newProbeFinished.load(std::memory_order_acquire);
        });
    }
    else
    {
        CommitCleanup cleanup{
            .threads = {&legacyThread, &newThread},
            .gates = {legacyStorage.mergeGate, newStorage.mergeGate},
        };
    }

    BOOST_CHECK(mergeStarted);
    BOOST_CHECK(probesFinishedDuringMerge);
}

BOOST_AUTO_TEST_CASE(commit_releases_exclusive_guard_during_prewrite)
{
    GateMergeStorage storage;
    MemPoolImpl memPool;
    StubExecutor executor;
    StubScheduler scheduler;
    auto blockFactory = bcos::test::createBlockFactory(bcos::test::createNormalCryptoSuite());
    auto ledger = std::make_shared<GatedFakeLedger>(blockFactory, 20, 10, 10);

    using Service = EthEngineService<MemPoolImpl, GateMergeStorage, StubExecutor, StubScheduler>;
    Service service(memPool, storage, executor, scheduler, blockFactory, ledger);

    auto forkchoice = makeForkchoiceState();
    seedForkchoiceStorage(storage, forkchoice);

    PayloadAttributes attrs = makeAttrs(1'700'000'002'000ULL);
    auto build = task::syncWait(service.updateForkchoice(forkchoice, &attrs, 3));
    BOOST_REQUIRE(build.payloadId.has_value());
    auto payload = task::syncWait(service.getPayload(*build.payloadId, 3));

    NewPayloadRequest request;
    request.executionPayload = payload->executionPayload;
    request.parentBeaconBlockRoot = attrs.parentBeaconBlockRoot;

    storage.mergeGate->store(false, std::memory_order_release);

    std::atomic<bool> probeFinished{false};
    bool prewriteStarted = false;
    bool probeFinishedDuringPrewrite = false;

    std::thread commitThread([&] {
        try
        {
            task::syncWait(service.newPayload(request, 3));
        }
        catch (...)
        {}
    });

    prewriteStarted = waitUntil(std::chrono::seconds(3),
        [&] { return ledger->prewriteStarted.load(std::memory_order_acquire); });

    std::thread probeThread;
    if (prewriteStarted)
    {
        probeThread = std::thread([&] {
            task::syncWait(service.getPayload(*build.payloadId, 3));
            probeFinished.store(true, std::memory_order_release);
        });

        CommitCleanup cleanup{
            .threads = {&commitThread, &probeThread},
            .gates = {ledger->prewriteGate, storage.mergeGate},
        };

        // newPayload no longer holds exclusive across prewrite; getPayload must proceed.
        probeFinishedDuringPrewrite = waitUntil(
            std::chrono::seconds(3), [&] { return probeFinished.load(std::memory_order_acquire); });
    }
    else
    {
        CommitCleanup cleanup{
            .threads = {&commitThread},
            .gates = {ledger->prewriteGate, storage.mergeGate},
        };
    }

    BOOST_CHECK(prewriteStarted);
    BOOST_CHECK(probeFinishedDuringPrewrite);
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

}  // namespace eth_tx_test
