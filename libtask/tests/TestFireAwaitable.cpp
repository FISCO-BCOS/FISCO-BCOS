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
 * @brief Unit tests for the generic FireAwaitable: every completion fate (invoked, dropped
 *        uninvoked, initiate throwing) must settle the awaiting coroutine exactly once with the
 *        correct result.
 * @file TestFireAwaitable.cpp
 */
#include "bcos-task/FireAwaitable.h"
#include "bcos-task/Task.h"
#include "bcos-task/Wait.h"
#include "bcos-utilities/Error.h"
#include <boost/asio/error.hpp>
#include <boost/system/error_code.hpp>
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <memory>
#include <stdexcept>
#include <tuple>

using namespace bcos::task;

namespace
{
// The failure error used to initialize the awaitable: a non-empty error_code.
constexpr auto failureError = boost::asio::error::operation_aborted;

// Run one co_await to completion and return the delivered result. Success delivers (empty ec,
// value); rescue/initiate-throw delivers (operation_aborted, 0).
static int runOnce(auto&& initiate)
{
    return syncWait([initiate = std::forward<decltype(initiate)>(initiate)]() mutable -> Task<int> {
        FireAwaitable<boost::system::error_code, std::decay_t<decltype(initiate)>, int> awaitable(
            std::move(initiate), failureError);
        auto [ec, value] = co_await awaitable;
        co_return ec ? -1 : value;
    }());
}
}  // namespace

BOOST_AUTO_TEST_SUITE(FireAwaitableTest)

// Normal path: initiate invokes the completion synchronously with an empty error (success); the
// result is delivered and the (now disarmed) completion must NOT re-resume on destruction.
BOOST_AUTO_TEST_CASE(NormalCompletionDeliversResult)
{
    auto result =
        runOnce([](auto completion) { completion(boost::system::error_code{}, 42); });
    BOOST_CHECK_EQUAL(result, 42);
}

// Rescue path: initiate drops the completion without invoking it (simulating executor teardown /
// a handler being dropped). The destructor must deliver the initial failure error instead of
// hanging or leaking.
BOOST_AUTO_TEST_CASE(DroppedCompletionDeliversErrorResult)
{
    auto result = runOnce([](auto completion) { (void)completion; /* dropped, never invoked */ });
    BOOST_CHECK_EQUAL(result, -1);
}

// Initiate-throw path: the initiate throws after the completion was armed. Stack unwinding
// destroys the armed completion, whose rescue delivers the failure error.
BOOST_AUTO_TEST_CASE(InitiateThrowsDeliversErrorResult)
{
    auto result = runOnce([](auto completion) {
        (void)completion;
        throw std::runtime_error("initiate failed");
    });
    BOOST_CHECK_EQUAL(result, -1);
}

// A completion that runs must settle exactly once: re-awaiting many iterations must never
// double-resume (which would crash as use-after-free / resuming a finished frame).
BOOST_AUTO_TEST_CASE(RepeatedCompletionsStayStable)
{
    for (int i = 0; i < 10000; ++i)
    {
        auto result =
            runOnce([](auto completion) { completion(boost::system::error_code{}, 7); });
        BOOST_CHECK_EQUAL(result, 7);
    }
}

// The Error type is a template parameter: verify the same awaitable works with bcos::Error::Ptr
// (nullptr = success) rather than boost::system::error_code.
BOOST_AUTO_TEST_CASE(WorksWithBcosErrorPtr)
{
    auto errorResult = BCOS_ERROR_PTR(-1, "aborted");
    auto initiate = [](auto completion) { completion(bcos::Error::Ptr{}, 42); };
    auto result = syncWait([errorResult, initiate]() -> Task<int> {
        FireAwaitable<bcos::Error::Ptr, std::decay_t<decltype(initiate)>, int> awaitable(
            initiate, errorResult);
        auto [error, value] = co_await awaitable;
        co_return error ? -1 : value;
    }());
    BOOST_CHECK_EQUAL(result, 42);
}

// Regression for the [this]-capture UAF: the bridge must own the initiate callable (moved into
// its frame), never reach back into the awaitable once it is gone. syncWait keeps the inner Task
// alive through the test parameter and so cannot reach that destruction ordering — drive it
// through task::wait instead, whose detached AsyncTask chain is the shape that produced the bug.
BOOST_AUTO_TEST_CASE(SynchronousCompletionUnderTaskWait)
{
    auto calls = std::make_shared<std::atomic<int>>(0);
    auto delivered = std::make_shared<std::atomic<int>>(-1);

    bcos::task::wait([](std::shared_ptr<std::atomic<int>> calls,
                         std::shared_ptr<std::atomic<int>> delivered) -> Task<void> {
        auto [ec, value] = co_await makeFireAwaitable<boost::system::error_code, int>(
            [calls](auto completion) {
                ++*calls;
                completion(boost::system::error_code{}, 42);
            },
            failureError);
        delivered->store(ec ? -1 : value);
        co_return;
    }(calls, delivered));

    // Synchronous completion settles the whole chain inside the symmetric transfer above, so both
    // are observable here. A stale [this] capture would re-enter the destroyed awaitable (counted
    // as a second call), and under ASan would fault outright.
    BOOST_CHECK_EQUAL(calls->load(), 1);
    BOOST_CHECK_EQUAL(delivered->load(), 42);
}

BOOST_AUTO_TEST_SUITE_END()
