/*
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
 * @brief Unit tests for the symmetric-transfer AsioAwaitable: every completion fate (invoked,
 *        dropped uninvoked, initiate throwing) must settle the awaiting coroutine exactly once
 *        with the correct result.
 * @file AsioAwaitableTest.cpp
 * @date 2026-09-04
 */
#include "bcos-gateway/libnetwork/AsioAwaitable.h"
#include "bcos-task/Wait.h"
#include "bcos-utilities/testutils/TestPromptFixture.h"
#include <boost/asio/error.hpp>
#include <boost/test/unit_test.hpp>
#include <chrono>
#include <iostream>
#include <tuple>

using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::task;
using namespace bcos::test;

namespace ba = boost::asio;

// The awaitable completes with (error_code, bytes_transferred); helper type for the awaited result.
using ReadResult = std::tuple<boost::system::error_code, std::size_t>;

// Run one co_await to completion and return the delivered result.
static ReadResult awaitOnce(auto&& initiate)
{
    return syncWait(
        [initiate = std::forward<decltype(initiate)>(initiate)]() -> task::Task<ReadResult> {
            co_return co_await makeAsioAwaitable<boost::system::error_code, std::size_t>(
                std::move(initiate));
        }());
}

BOOST_FIXTURE_TEST_SUITE(AsioAwaitableTest, TestPromptFixture)

// Normal path: the initiate invokes the completion synchronously; the result must be delivered
// and the (now disarmed) completion must NOT re-resume on destruction.
BOOST_AUTO_TEST_CASE(NormalCompletionDeliversResult)
{
    auto result = awaitOnce([](auto handler) { handler(boost::system::error_code{}, 42); });

    BOOST_CHECK_EQUAL(std::get<0>(result), boost::system::error_code{});
    BOOST_CHECK_EQUAL(std::get<1>(result), static_cast<std::size_t>(42));
}

// Rescue path: the initiate drops the completion without invoking it (simulating io_context
// teardown / a fake dropping the handler). The destructor must report operation_aborted so the
// awaiting coroutine unwinds through its error path instead of hanging or being destroyed.
BOOST_AUTO_TEST_CASE(DroppedCompletionReportsAborted)
{
    auto result = awaitOnce([](auto handler) { (void)handler; /* dropped, never invoked */ });

    BOOST_CHECK_EQUAL(std::get<0>(result), ba::error::operation_aborted);
}

// Initiate-throw path: the initiate throws after the completion was armed. Stack unwinding
// destroys the armed completion, whose rescue reports operation_aborted; the bridge swallows the
// exception to avoid terminate.
BOOST_AUTO_TEST_CASE(InitiateThrowsReportsAborted)
{
    auto result = awaitOnce([](auto handler) {
        (void)handler;
        throw std::runtime_error("initiate failed");
    });

    BOOST_CHECK_EQUAL(std::get<0>(result), ba::error::operation_aborted);
}

// A completion that runs must settle exactly once: re-awaiting many iterations must never
// double-resume (which would crash as use-after-free / resuming a finished frame).
BOOST_AUTO_TEST_CASE(RepeatedCompletionsStayStable)
{
    for (int i = 0; i < 10000; ++i)
    {
        auto result = awaitOnce([](auto handler) {
            handler(boost::system::error_code{}, static_cast<std::size_t>(7));
        });
        BOOST_CHECK_EQUAL(std::get<0>(result), boost::system::error_code{});
        BOOST_CHECK_EQUAL(std::get<1>(result), static_cast<std::size_t>(7));
    }
}

BOOST_AUTO_TEST_SUITE_END()

// Micro-benchmark measuring the overhead of the bridge coroutine frame added by the symmetric-
// transfer awaitable: it co_awaits a normally-completing awaitable (bridge frame allocate +
// symmetric transfer + resume) and compares against a trivial co_return coroutine (baseline).
// The delta isolates the bridge-frame cost. Run standalone for stable numbers, e.g.
//   ./test-bcos-gateway --run_test=AsioAwaitableBenchmark/BridgeFrameOverhead
BOOST_FIXTURE_TEST_SUITE(AsioAwaitableBenchmark, TestPromptFixture)

BOOST_AUTO_TEST_CASE(BridgeFrameOverhead)
{
    constexpr int ITERATIONS = 100000;

    // baseline: a trivial coroutine co_returning a value (syncWait fixed cost + one coroutine
    // frame), no bridge frame.
    auto t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERATIONS; ++i)
    {
        auto value = syncWait([]() -> task::Task<int> { co_return 1; }());
        (void)value;
    }
    auto t1 = std::chrono::high_resolution_clock::now();

    // awaitable: co_await a normally-completing awaitable, paying one extra TaskPure bridge frame
    // (allocation) plus a symmetric transfer per operation.
    auto t2 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERATIONS; ++i)
    {
        auto result = syncWait([]() -> task::Task<ReadResult> {
            co_return co_await makeAsioAwaitable<boost::system::error_code, std::size_t>(
                [](auto handler) {
                    handler(boost::system::error_code{}, static_cast<std::size_t>(0));
                });
        }());
        (void)result;
    }
    auto t3 = std::chrono::high_resolution_clock::now();

    auto baselineNs =
        std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count() / ITERATIONS;
    auto awaitableNs =
        std::chrono::duration_cast<std::chrono::nanoseconds>(t3 - t2).count() / ITERATIONS;

    std::cout << "[AsioAwaitableBenchmark]"
              << " iterations=" << ITERATIONS << " baseline=" << baselineNs
              << "ns/op awaitable=" << awaitableNs << "ns/op bridgeOverhead="
              << (awaitableNs - baselineNs) << "ns/op" << std::endl;
}

BOOST_AUTO_TEST_SUITE_END()
