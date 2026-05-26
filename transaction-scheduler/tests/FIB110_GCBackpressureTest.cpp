/*
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @brief Regression tests for FIB-110: GC backpressure
 * @file FIB110_GCBackpressureTest.cpp
 * @date 2026-04-07
 */

#include "bcos-transaction-scheduler/GC.h"
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <thread>

using namespace bcos::scheduler_v1;

namespace
{
// Resource with an observable destructor used to verify GC paths.
// Optionally blocks on a shared gate so a single instance can pin the
// single-threaded GC arena thread, forcing the queue to fill up.
struct Resource
{
    static inline std::atomic<int> destroyed{0};
    static inline std::atomic<bool> blockerStarted{false};
    static inline std::shared_future<void> gate;

    bool live = true;
    bool blocker = false;

    Resource() = default;
    explicit Resource(bool isBlocker) : blocker(isBlocker) {}
    Resource(Resource&& other) noexcept : live(other.live), blocker(other.blocker)
    {
        other.live = false;
    }
    Resource& operator=(Resource&& other) noexcept
    {
        live = other.live;
        blocker = other.blocker;
        other.live = false;
        return *this;
    }
    Resource(Resource const&) = delete;
    Resource& operator=(Resource const&) = delete;

    ~Resource()
    {
        if (!live)
        {
            return;  // moved-from
        }
        if (blocker)
        {
            blockerStarted.store(true, std::memory_order_release);
            if (gate.valid())
            {
                gate.wait();
            }
        }
        destroyed.fetch_add(1, std::memory_order_relaxed);
    }
};
}  // namespace

BOOST_AUTO_TEST_SUITE(FIB110_GCBackpressureTest)

BOOST_AUTO_TEST_CASE(MaxPendingGCConstant)
{
    BOOST_CHECK_EQUAL(GC::MAX_PENDING_GC, 64u);
}

BOOST_AUTO_TEST_CASE(NormalCollectWorks)
{
    auto ptr = std::make_shared<int>(42);
    BOOST_CHECK_EQUAL(ptr.use_count(), 1);
    GC::collect(std::move(ptr));
    BOOST_CHECK(!ptr);
}

BOOST_AUTO_TEST_CASE(CollectMultipleResources)
{
    auto ptr1 = std::make_shared<int>(1);
    auto ptr2 = std::make_shared<std::string>("hello");
    GC::collect(std::move(ptr1), std::move(ptr2));
    BOOST_CHECK(!ptr1);
    BOOST_CHECK(!ptr2);
}

// Core regression: when the deferred-destruction queue is at capacity,
// the next collect() MUST destroy synchronously instead of growing the
// backlog. We pin the GC arena's single thread with a Blocker resource
// whose destructor waits on a gate, then prove:
//   1. While the arena is blocked, pending climbs to MAX_PENDING_GC.
//   2. The (MAX_PENDING_GC + 1)-th call destructs in the caller's thread
//      (observable: destroyed counter ticks before we open the gate).
BOOST_AUTO_TEST_CASE(BackpressureForcesSynchronousDestruction)
{
    Resource::destroyed.store(0, std::memory_order_relaxed);
    Resource::blockerStarted.store(false, std::memory_order_relaxed);
    std::promise<void> gatePromise;
    Resource::gate = gatePromise.get_future().share();

    // 1. Pin the GC arena with a blocker. Its lambda body runs (decrementing
    //    pending), then its capture is destroyed, parking the arena thread
    //    inside gate.wait().
    GC::collect(Resource{true});

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!Resource::blockerStarted.load(std::memory_order_acquire))
    {
        if (std::chrono::steady_clock::now() > deadline)
        {
            gatePromise.set_value();  // unblock so process can exit
            BOOST_FAIL("GC arena did not start running blocker within 5s");
        }
        std::this_thread::yield();
    }
    // Arena is now stuck in the blocker's destructor. pending==0.

    // 2. Saturate the queue exactly to capacity. Nothing runs because the
    //    arena thread is parked.
    for (size_t i = 0; i < GC::MAX_PENDING_GC; ++i)
    {
        GC::collect(Resource{false});
    }
    BOOST_CHECK_EQUAL(Resource::destroyed.load(std::memory_order_relaxed), 0);

    // 3. The next call must hit the synchronous fallback in this thread,
    //    so destroyed ticks immediately (still no async work has run).
    GC::collect(Resource{false});
    BOOST_CHECK_EQUAL(Resource::destroyed.load(std::memory_order_relaxed), 1);

    // 4. Release the arena and wait for it to drain so we don't leave a
    //    blocked thread for the rest of the test binary to deal with.
    gatePromise.set_value();
    auto const expected = static_cast<int>(GC::MAX_PENDING_GC) + 2;
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (Resource::destroyed.load(std::memory_order_relaxed) < expected)
    {
        if (std::chrono::steady_clock::now() > deadline)
        {
            BOOST_FAIL("GC arena did not drain after gate opened within 5s");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    BOOST_CHECK_EQUAL(Resource::destroyed.load(std::memory_order_relaxed), expected);
}

BOOST_AUTO_TEST_SUITE_END()
