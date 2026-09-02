/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#include "engine/bcos-engine/EngineTracker.h"
#include "engine/bcos-engine/PayloadCache.h"
#include "engine/bcos-engine/SplitEngineCommon.h"

#include <bcos-framework/engine/Errors.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/Exceptions.h>
#include <boost/test/unit_test.hpp>

#include <atomic>
#include <barrier>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

using namespace bcos;
using namespace bcos::engine;

namespace
{

ResolvedForkchoice resolved(
    h256 hash, bcos::protocol::BlockNumber number, bool canonical, bool attributes)
{
    return ResolvedForkchoice{
        .state = ForkchoiceState{hash, hash, hash},
        .headNumber = number,
        .safeNumber = number,
        .finalizedNumber = number,
        .headCanonical = canonical,
        .payloadAttributesPresent = attributes,
    };
}

CommonPayloadEntryPtr makePayload(std::uint32_t version, bool withWithdrawalsRoot = false)
{
    auto entry = std::make_shared<CommonPayloadEntry>();
    entry->version = version;
    entry->executionPayload.blockNumber = 1;
    if (withWithdrawalsRoot)
    {
        entry->executionPayload.withdrawalsRoot = h256(42);
    }
    return entry;
}

template <typename Exception>
void checkExceptionMessage(auto&& action, const char* expectedMessage)
{
    BOOST_CHECK_EXCEPTION(action(), Exception, [&](Exception const& e) {
        auto const* comment = boost::get_error_info<errinfo_comment>(e);
        return comment != nullptr && *comment == expectedMessage;
    });
}

}  // namespace

BOOST_AUTO_TEST_SUITE(EngineTrackerTest)

BOOST_AUTO_TEST_CASE(payload_cache_keeps_hash_index_and_fifo_consistent)
{
    PayloadCache cache;
    std::vector<PayloadID> ids;
    for (std::uint64_t i = 0; i < 65; ++i)
    {
        auto entry = std::make_shared<CommonPayloadEntry>();
        entry->version = 1;
        entry->executionPayload.blockNumber = i;
        auto id = bcos::toHexStringWithPrefix(
            bcos::bytesConstRef(reinterpret_cast<const byte*>(&i), sizeof(i)));
        ids.push_back(id);
        cache.put(id, h256(i + 1), std::static_pointer_cast<const CommonPayloadEntry>(entry));
    }

    BOOST_CHECK(!cache.find(ids.front()));
    BOOST_CHECK(!cache.payloadIdForHash(h256(1)).has_value());
    BOOST_REQUIRE(cache.find(ids.back()));
    BOOST_REQUIRE(cache.payloadIdForHash(h256(65)).has_value());
    BOOST_CHECK_EQUAL(*cache.payloadIdForHash(h256(65)), ids.back());
}

BOOST_AUTO_TEST_CASE(payload_cache_deduplicates_repeated_payload_id)
{
    PayloadCache cache;
    auto entry = std::make_shared<CommonPayloadEntry>();
    entry->version = 1;
    const PayloadID id = "0x0102030405060708";
    for (int i = 0; i < 70; ++i)
    {
        cache.put(id, h256(7), std::static_pointer_cast<const CommonPayloadEntry>(entry));
    }
    BOOST_REQUIRE(cache.find(id));
    BOOST_CHECK_EQUAL(*cache.payloadIdForHash(h256(7)), id);
}

BOOST_AUTO_TEST_CASE(engine_tracker_preserves_rebuild_on_parent)
{
    EngineTracker tracker;
    tracker.applyForkchoice(resolved(h256(10), 10, true, false));
    auto outcome = tracker.applyForkchoice(resolved(h256(9), 9, true, true));
    BOOST_CHECK(outcome == ForkchoiceApplyResult::Applied);
    BOOST_REQUIRE(tracker.trackedHead().has_value());
    BOOST_CHECK_EQUAL(tracker.trackedHead()->blockNumber, 9);
}

BOOST_AUTO_TEST_CASE(engine_tracker_swallows_old_head_without_attributes)
{
    EngineTracker tracker;
    tracker.applyForkchoice(resolved(h256(10), 10, true, false));
    auto outcome = tracker.applyForkchoice(resolved(h256(9), 9, true, false));
    BOOST_CHECK(outcome == ForkchoiceApplyResult::Swallowed);
    BOOST_CHECK_EQUAL(tracker.trackedHead()->blockNumber, 10);
}

BOOST_AUTO_TEST_CASE(engine_tracker_swallows_noncanonical_parent_even_with_attributes)
{
    EngineTracker tracker;
    tracker.applyForkchoice(resolved(h256(10), 10, true, false));
    auto outcome = tracker.applyForkchoice(resolved(h256(9), 9, false, true));
    BOOST_CHECK(outcome == ForkchoiceApplyResult::Swallowed);
    BOOST_CHECK_EQUAL(tracker.trackedHead()->blockNumber, 10);
    BOOST_CHECK_EQUAL(tracker.trackedHead()->hash, h256(10));
}

BOOST_AUTO_TEST_CASE(engine_tracker_swallows_older_than_parent_with_attributes)
{
    EngineTracker tracker;
    tracker.applyForkchoice(resolved(h256(10), 10, true, false));
    auto outcome = tracker.applyForkchoice(resolved(h256(8), 8, true, true));
    BOOST_CHECK(outcome == ForkchoiceApplyResult::Swallowed);
    BOOST_CHECK_EQUAL(tracker.trackedHead()->blockNumber, 10);
    BOOST_CHECK_EQUAL(tracker.trackedHead()->hash, h256(10));
}

BOOST_AUTO_TEST_CASE(engine_tracker_allows_canonical_same_height_reorg_sibling)
{
    EngineTracker tracker;
    tracker.applyForkchoice(resolved(h256(10), 10, true, false));
    auto outcome = tracker.applyForkchoice(resolved(h256(1010), 10, true, false));
    BOOST_CHECK(outcome == ForkchoiceApplyResult::Applied);
    BOOST_REQUIRE(tracker.trackedHead().has_value());
    BOOST_CHECK_EQUAL(tracker.trackedHead()->blockNumber, 10);
    BOOST_CHECK_EQUAL(tracker.trackedHead()->hash, h256(1010));
}

BOOST_AUTO_TEST_CASE(engine_tracker_rejects_noncanonical_same_height)
{
    EngineTracker tracker;
    tracker.applyForkchoice(resolved(h256(10), 10, true, false));
    checkExceptionMessage<InvalidForkchoiceState>(
        [&]() { tracker.applyForkchoice(resolved(h256(1010), 10, false, false)); },
        "Forkchoice head block hash conflicts with tracked block number");
}

BOOST_AUTO_TEST_CASE(engine_tracker_rejects_safe_above_head)
{
    EngineTracker tracker;
    ResolvedForkchoice bad{
        .state = ForkchoiceState{h256(1), h256(2), h256(3)},
        .headNumber = 5,
        .safeNumber = 6,
        .finalizedNumber = 4,
        .headCanonical = true,
        .payloadAttributesPresent = false,
    };
    checkExceptionMessage<InvalidForkchoiceState>([&]() { tracker.applyForkchoice(bad); },
        "Forkchoice safe block number must not exceed head block number");
}

BOOST_AUTO_TEST_CASE(engine_tracker_rejects_finalized_above_head)
{
    EngineTracker tracker;
    ResolvedForkchoice bad{
        .state = ForkchoiceState{h256(1), h256(2), h256(3)},
        .headNumber = 5,
        .safeNumber = 4,
        .finalizedNumber = 6,
        .headCanonical = true,
        .payloadAttributesPresent = false,
    };
    checkExceptionMessage<InvalidForkchoiceState>([&]() { tracker.applyForkchoice(bad); },
        "Forkchoice finalized block number must not exceed head block number");
}

BOOST_AUTO_TEST_CASE(engine_tracker_rejects_finalized_above_safe)
{
    EngineTracker tracker;
    ResolvedForkchoice bad{
        .state = ForkchoiceState{h256(1), h256(2), h256(3)},
        .headNumber = 10,
        .safeNumber = 5,
        .finalizedNumber = 6,
        .headCanonical = true,
        .payloadAttributesPresent = false,
    };
    checkExceptionMessage<InvalidForkchoiceState>([&]() { tracker.applyForkchoice(bad); },
        "Forkchoice finalized block number must not exceed safe block number");
}

BOOST_AUTO_TEST_CASE(engine_tracker_rejects_head_jump)
{
    EngineTracker tracker;
    tracker.applyForkchoice(resolved(h256(10), 10, true, false));
    checkExceptionMessage<InvalidForkchoiceState>(
        [&]() { tracker.applyForkchoice(resolved(h256(12), 12, true, false)); },
        "Forkchoice head block number must increase by exactly 1");
}

BOOST_AUTO_TEST_CASE(engine_tracker_updates_safe_and_finalized_on_apply)
{
    EngineTracker tracker;
    ResolvedForkchoice first{
        .state = ForkchoiceState{h256(10), h256(11), h256(12)},
        .headNumber = 10,
        .safeNumber = 8,
        .finalizedNumber = 7,
        .headCanonical = true,
        .payloadAttributesPresent = false,
    };
    tracker.applyForkchoice(first);
    BOOST_CHECK_EQUAL(*tracker.safeBlockNumber(), 8);
    BOOST_CHECK_EQUAL(*tracker.finalizedBlockNumber(), 7);
}

BOOST_AUTO_TEST_CASE(engine_tracker_get_payload_unknown)
{
    EngineTracker tracker;
    BOOST_CHECK_THROW(tracker.getPayload("0xdeadbeef", 1), UnknownPayload);
}

BOOST_AUTO_TEST_CASE(engine_tracker_get_payload_unsupported_version)
{
    EngineTracker tracker;
    BOOST_CHECK_THROW(tracker.getPayload("0xdeadbeef", 0), UnsupportedEngineApiVersion);
    BOOST_CHECK_THROW(tracker.getPayload("0xdeadbeef", 6), UnsupportedEngineApiVersion);
}

BOOST_AUTO_TEST_CASE(engine_tracker_get_payload_incompatible_version)
{
    EngineTracker tracker;
    const PayloadID id = "0x0102030405060708";
    auto guard = tracker.lockExclusive();
    guard.putPayload(id, h256(7), makePayload(4));
    guard = EngineTracker::ExclusiveAccess{};

    BOOST_CHECK_THROW(tracker.getPayload(id, 1), IncompatiblePayloadVersion);
}

BOOST_AUTO_TEST_CASE(engine_tracker_get_payload_v4_requires_withdrawals_root)
{
    EngineTracker tracker;
    const PayloadID id = "0x0102030405060708";
    {
        auto guard = tracker.lockExclusive();
        guard.putPayload(id, h256(7), makePayload(3));
    }
    BOOST_CHECK_THROW(tracker.getPayload(id, 4), IncompatiblePayloadVersion);
}

BOOST_AUTO_TEST_CASE(engine_tracker_get_payload_v4_includes_execution_requests)
{
    EngineTracker tracker;
    const PayloadID id = "0x0102030405060708";
    {
        auto guard = tracker.lockExclusive();
        guard.putPayload(id, h256(7), makePayload(3, true));
    }
    auto result = tracker.getPayload(id, 4);
    BOOST_REQUIRE(result);
    BOOST_REQUIRE(result->executionRequests.has_value());
    BOOST_CHECK(result->executionRequests->empty());
}

BOOST_AUTO_TEST_CASE(engine_tracker_get_payload_v3_omits_execution_requests)
{
    EngineTracker tracker;
    const PayloadID id = "0x0102030405060708";
    {
        auto guard = tracker.lockExclusive();
        guard.putPayload(id, h256(7), makePayload(3, true));
    }
    auto result = tracker.getPayload(id, 3);
    BOOST_REQUIRE(result);
    BOOST_CHECK(!result->executionRequests.has_value());
}

BOOST_AUTO_TEST_CASE(engine_tracker_exclusive_guard_exposes_payload_cache)
{
    EngineTracker tracker;
    tracker.applyForkchoice(resolved(h256(10), 10, true, false));
    const PayloadID id = "0x0102030405060708";
    auto guard = tracker.lockExclusive();
    guard.putPayload(id, h256(10), makePayload(1));
    BOOST_REQUIRE(guard.findPayload(id));
    BOOST_REQUIRE(guard.payloadIdForHash(h256(10)).has_value());
    BOOST_CHECK_EQUAL(guard.forkchoiceState().headBlockHash, h256(10));
}

BOOST_AUTO_TEST_CASE(engine_tracker_shared_guard_allows_concurrent_readers)
{
    EngineTracker tracker;
    tracker.applyForkchoice(resolved(h256(10), 10, true, false));
    const PayloadID id = "0x0102030405060708";
    {
        auto guard = tracker.lockExclusive();
        guard.putPayload(id, h256(10), makePayload(1));
    }

    std::atomic<int> activeReaders{0};
    std::atomic<int> peakReaders{0};
    std::atomic<bool> readerDataOk{true};
    std::barrier sync(3);

    auto read = [&]() {
        bool localOk = true;
        auto guard = tracker.lockShared();
        if (!guard.findPayload(id) || guard.forkchoiceState().headBlockHash != h256(10))
        {
            localOk = false;
        }
        else
        {
            int current = ++activeReaders;
            int observed = peakReaders.load();
            while (observed < current && !peakReaders.compare_exchange_weak(observed, current))
            {
            }
        }

        sync.arrive_and_wait();
        sync.arrive_and_wait();

        if (localOk)
        {
            --activeReaders;
        }
        else
        {
            readerDataOk = false;
        }
    };

    std::jthread t1(read);
    std::jthread t2(read);
    sync.arrive_and_wait();
    sync.arrive_and_wait();
    t1 = std::jthread{};
    t2 = std::jthread{};

    BOOST_CHECK(readerDataOk.load());
    BOOST_CHECK_GE(peakReaders.load(), 2);
    BOOST_CHECK_EQUAL(activeReaders.load(), 0);
}

BOOST_AUTO_TEST_CASE(engine_tracker_exclusive_guard_blocks_shared_readers)
{
    // Observability boundary: std::shared_mutex exposes no API to observe a thread
    // blocked inside lock_shared(). This test establishes happens-before with explicit
    // permission to attempt lockShared():
    //   permissionGranted -> permissionConsumed (worker consumed the permit and proceeds
    //   to lockShared()) -> lockAcquired only after exclusive release.
    // permissionConsumed + acquired==false while exclusive is held excludes "thread never
    // ran"; acquired==true after release excludes "no blocking occurred".

    EngineTracker tracker;
    tracker.applyForkchoice(resolved(h256(10), 10, true, false));

    std::mutex syncMutex;
    std::condition_variable workerReadyCv;
    std::condition_variable permissionCv;
    std::condition_variable acquiredCv;

    bool workerReady = false;
    bool permissionGranted = false;
    bool permissionConsumed = false;
    bool lockAcquired = false;

    struct Outcome
    {
        bool workerReadySeen = false;
        bool permissionConsumedWhileExclusive = false;
        bool notAcquiredWhileExclusive = false;
        bool acquiredAfterRelease = false;
    } outcome;
    constexpr auto kTimeout = std::chrono::seconds(2);

    auto guard = tracker.lockExclusive();

    std::jthread worker([&](std::stop_token) {
        {
            std::lock_guard lock(syncMutex);
            workerReady = true;
        }
        workerReadyCv.notify_one();

        {
            std::unique_lock lock(syncMutex);
            permissionCv.wait(lock, [&]() { return permissionGranted; });
            permissionConsumed = true;
        }
        permissionCv.notify_one();

        (void)tracker.lockShared();

        {
            std::lock_guard lock(syncMutex);
            lockAcquired = true;
        }
        acquiredCv.notify_one();
    });

    {
        std::unique_lock lock(syncMutex);
        if (workerReadyCv.wait_for(lock, kTimeout, [&]() { return workerReady; }))
        {
            outcome.workerReadySeen = true;
            permissionGranted = true;
            permissionCv.notify_one();
            if (permissionCv.wait_for(lock, kTimeout, [&]() { return permissionConsumed; }))
            {
                outcome.permissionConsumedWhileExclusive = true;
                outcome.notAcquiredWhileExclusive = !lockAcquired;
            }
        }
    }

    {
        std::lock_guard lock(syncMutex);
        permissionGranted = true;
    }
    permissionCv.notify_all();
    guard = EngineTracker::ExclusiveAccess{};

    {
        std::unique_lock lock(syncMutex);
        if (acquiredCv.wait_for(lock, kTimeout, [&]() { return lockAcquired; }))
        {
            outcome.acquiredAfterRelease = lockAcquired;
        }
    }

    worker = std::jthread{};

    BOOST_CHECK(outcome.workerReadySeen);
    BOOST_CHECK(outcome.permissionConsumedWhileExclusive);
    BOOST_CHECK(outcome.notAcquiredWhileExclusive);
    BOOST_CHECK(outcome.acquiredAfterRelease);
}

BOOST_AUTO_TEST_CASE(engine_tracker_retain_only_through_guard)
{
    EngineTracker tracker;
    const PayloadID keep = "0xkeep";
    const PayloadID drop = "0xdrop";
    auto guard = tracker.lockExclusive();
    guard.putPayload(keep, h256(1), makePayload(1));
    guard.putPayload(drop, h256(2), makePayload(1));
    guard.retainOnly(keep, h256(1));
    BOOST_REQUIRE(guard.findPayload(keep));
    BOOST_CHECK(!guard.findPayload(drop));
    BOOST_CHECK(!guard.payloadIdForHash(h256(2)).has_value());
}

BOOST_AUTO_TEST_SUITE_END()
