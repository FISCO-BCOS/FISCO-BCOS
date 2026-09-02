/**
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
 * @brief unit tests for IOServicePool strand()
 *
 * @file IOServicePoolTest.cpp
 */

#include "bcos-utilities/IOServicePool.h"
#include "bcos-utilities/testutils/TestPromptFixture.h"
#include <chrono>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

using namespace bcos;
using namespace std;

namespace bcos::test
{
BOOST_FIXTURE_TEST_SUITE(IOServicePoolTest, TestPromptFixture)

BOOST_AUTO_TEST_CASE(strandRunsSerially)
{
    auto pool = std::make_shared<IOServicePool>(4, "strandSerial");
    auto strand = std::make_shared<Strand>(pool);

    std::vector<int> order;
    std::atomic<int> inFlight{0};
    std::atomic<bool> concurrent{false};

    constexpr int N = 100;
    for (int i = 0; i < N; ++i)
    {
        strand->post([&order, &inFlight, &concurrent, i]() {
            // If another strand task is already running, we have a concurrency bug.
            if (inFlight.fetch_add(1) > 0)
            {
                concurrent.store(true);
            }
            order.push_back(i);
            inFlight.fetch_sub(1);
        });
    }

    // Let the strand drain.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    pool.reset();  // stop and join

    BOOST_CHECK(!concurrent.load());
    BOOST_CHECK_EQUAL(order.size(), (size_t)N);
    // Verify FIFO order.
    for (int i = 0; i < N; ++i)
    {
        BOOST_CHECK_EQUAL(order[(size_t)i], i);
    }
}

BOOST_AUTO_TEST_CASE(strandSpreadsAcrossThreads)
{
    constexpr size_t kThreads = 4;
    auto pool = std::make_shared<IOServicePool>(kThreads, "strandSpread");
    auto strand = std::make_shared<Strand>(pool);

    std::vector<std::thread::id> threadIds;
    std::mutex mutex;
    std::atomic<int> done{0};

    constexpr int N = 20;
    for (int i = 0; i < N; ++i)
    {
        strand->post([&threadIds, &mutex, &done]() {
            {
                std::lock_guard<std::mutex> lock(mutex);
                threadIds.push_back(std::this_thread::get_id());
            }
            // Simulate some work so tasks spread across scheduling quanta.
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            done.fetch_add(1);
        });
    }

    // Wait for all strand tasks.
    for (int timeout = 0; timeout < 100 && done.load() < N; ++timeout)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    pool.reset();
    BOOST_CHECK_EQUAL(done.load(), N);
    // Count unique thread IDs to verify round-robin distribution.
    std::sort(threadIds.begin(), threadIds.end());
    auto uniqueCount =
        (size_t)std::distance(threadIds.begin(), std::unique(threadIds.begin(), threadIds.end()));
    // With >1 io_context and round-robin, we should have used more than 1 thread.
    BOOST_CHECK_GT(uniqueCount, 1U);
}

BOOST_AUTO_TEST_CASE(strandAndPostAreIndependent)
{
    auto pool = std::make_shared<IOServicePool>(4, "strandPost");
    auto strand = std::make_shared<Strand>(pool);

    std::atomic<int> strandInFlight{0};
    std::atomic<bool> strandConcurrent{false};
    std::atomic<int> strandOrder{0};
    std::atomic<int> strandDone{0};

    std::atomic<int> postInFlight{0};
    std::atomic<int> postMaxConcurrency{0};
    std::atomic<int> postDone{0};

    constexpr int N = 30;
    for (int i = 0; i < N; ++i)
    {
        // Strand tasks must be serial.
        strand->post([&strandInFlight, &strandConcurrent, &strandOrder, &strandDone]() {
            if (strandInFlight.fetch_add(1) > 0)
            {
                strandConcurrent.store(true);
            }
            // Busy-wait a tiny bit to increase chance of catching concurrency bugs.
            auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1);
            while (std::chrono::steady_clock::now() < deadline)
                ;
            strandOrder.store(strandOrder.load() + 1);
            strandInFlight.fetch_sub(1);
            strandDone.fetch_add(1);
        });

        // Post tasks may run concurrently with strand and with each other.
        pool->post([&postInFlight, &postMaxConcurrency, &postDone]() {
            int current = postInFlight.fetch_add(1) + 1;
            // Track the maximum observed concurrency among post tasks.
            int prev;
            do
            {
                prev = postMaxConcurrency.load();
                if (current <= prev)
                    break;
            } while (!postMaxConcurrency.compare_exchange_weak(prev, current));

            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            postInFlight.fetch_sub(1);
            postDone.fetch_add(1);
        });
    }

    // Wait for completion.
    for (int timeout = 0; timeout < 100 && (strandDone.load() < N || postDone.load() < N);
        ++timeout)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    pool.reset();

    BOOST_CHECK(!strandConcurrent.load());
    BOOST_CHECK_EQUAL(strandDone.load(), N);
    BOOST_CHECK_EQUAL(postDone.load(), N);
    // Post tasks should have observed some concurrency (with 4 threads).
    // It's theoretically possible they all serialized, but extremely unlikely.
    BOOST_WARN_GT(postMaxConcurrency.load(), 1);
}

BOOST_AUTO_TEST_CASE(strandOrderWithMixedSubmission)
{
    auto pool = std::make_shared<IOServicePool>(2, "strandMixed");
    auto strand = std::make_shared<Strand>(pool);

    std::vector<int> strandOrder;
    std::mutex orderMutex;

    // Submit strand tasks interleaved with post tasks.
    strand->post([&strandOrder, &orderMutex]() {
        std::lock_guard lock(orderMutex);
        strandOrder.push_back(0);
    });
    pool->post([]() {
        // no-op, just to interleave
    });
    strand->post([&strandOrder, &orderMutex]() {
        std::lock_guard lock(orderMutex);
        strandOrder.push_back(1);
    });
    strand->post([&strandOrder, &orderMutex]() {
        std::lock_guard lock(orderMutex);
        strandOrder.push_back(2);
    });
    pool->post([]() {
        // no-op
    });
    strand->post([&strandOrder, &orderMutex]() {
        std::lock_guard lock(orderMutex);
        strandOrder.push_back(3);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    pool.reset();

    BOOST_CHECK_EQUAL(strandOrder.size(), 4U);
    // Strand tasks must execute in submission order regardless of interleaved posts.
    for (size_t i = 0; i < strandOrder.size(); ++i)
    {
        BOOST_CHECK_EQUAL(strandOrder[i], (int)i);
    }
}

// getIOService() hands the pool's io_contexts out round-robin, wrapping past the last one.
// Carried over from release-3.17.0's startRoundRobinStop: this file was an add/add conflict and
// the 3.18.0 side (all Strand semantics) does not cover the pool's own dispatch. The explicit
// start()/stop() calls are gone -- the pool now spins its threads in the constructor and stops and
// joins them in the destructor -- so only the round-robin half of the original case survives.
BOOST_AUTO_TEST_CASE(getIOServiceRoundRobinWraps)
{
    bcos::IOServicePool pool(2, "roundRobinTest");
    auto first = pool.getIOService();
    auto second = pool.getIOService();
    auto wrapped = pool.getIOService();  // index wraps back to the first service
    BOOST_CHECK(first);
    BOOST_CHECK(second);
    BOOST_CHECK(wrapped);
    BOOST_CHECK(first != second);
    BOOST_CHECK(first == wrapped);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
