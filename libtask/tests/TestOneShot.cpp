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
 * @brief Unit tests for OneShot: one-shot exactly-once delivery, the no-lost-wakeup fast path,
 *        cancel() as the teardown settle path, and the racing producers it exists to replace.
 * @file TestOneShot.cpp
 */
#include "bcos-task/Task.h"
#include "bcos-task/OneShot.h"
#include "bcos-task/Wait.h"
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>

using namespace bcos::task;

BOOST_AUTO_TEST_SUITE(OneShotTest)

// Fast path: a completion that arrives BEFORE the coroutine parks must not be lost, and must not
// suspend at all (the ordering "complete() then co_await" is the pattern the docs promise).
BOOST_AUTO_TEST_CASE(CompleteBeforeAwaitWinsTheFastPath)
{
    auto value = syncWait([]() -> Task<int> {
        OneShot<int> once;
        once.complete(42);
        auto [delivered] = co_await once;
        co_return delivered;
    }());
    BOOST_CHECK_EQUAL(value, 42);
}

// Slow path: the coroutine parks inside syncWait, so syncWait can only return once an EXTERNAL
// producer settles the once — a completion that never woke the parked frame would hang here.
BOOST_AUTO_TEST_CASE(CompleteAfterSuspendResumesTheAwaiter)
{
    auto once = std::make_shared<OneShot<int>>();
    auto delivered = std::make_shared<std::atomic<int>>(-1);

    std::thread producer([once] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        BOOST_CHECK(once->complete(7));
    });

    syncWait([once, delivered]() -> Task<void> {
        auto [value] = co_await *once;
        delivered->store(value);
        co_return;
    }());

    producer.join();
    BOOST_CHECK(once->settled());
    BOOST_CHECK_EQUAL(delivered->load(), 7);
}

// Exactly-once: after the first completion every further completion is a no-op that reports it lost.
BOOST_AUTO_TEST_CASE(SecondCompletionIsANoOp)
{
    auto value = syncWait([]() -> Task<int> {
        OneShot<int> once;
        const bool first = once.complete(42);
        const bool second = once.complete(99);
        auto [delivered] = co_await once;
        co_return first && !second ? delivered : -1;
    }());
    BOOST_CHECK_EQUAL(value, 42);
}

// The two racing producers OneShot exists for (ack vs timeout): one wins, the loser observes it,
// and the awaiting coroutine is resumed exactly once.
BOOST_AUTO_TEST_CASE(ConcurrentProducersSettleExactlyOnce)
{
    for (int iteration = 0; iteration < 200; ++iteration)
    {
        auto once = std::make_shared<OneShot<int>>();
        auto winners = std::make_shared<std::atomic<int>>(0);
        auto delivered = std::make_shared<std::atomic<int>>(-1);

        std::thread first([once, winners] {
            if (once->complete(42))
            {
                ++*winners;
            }
        });
        std::thread second([once, winners] {
            if (once->complete(99))
            {
                ++*winners;
            }
        });

        syncWait([once, delivered]() -> Task<void> {
            auto [value] = co_await *once;
            delivered->store(value);
            co_return;
        }());

        first.join();
        second.join();

        BOOST_CHECK_EQUAL(winners->load(), 1);
        const auto value = delivered->load();
        BOOST_CHECK(value == 42 || value == 99);
    }
}

// Teardown path: cancel() ends a PARKED wait without a value, and await_resume() rethrows the reason
// instead of returning a default-constructed value that could be read as a real result.
BOOST_AUTO_TEST_CASE(CancelResumesParkedAwaiterWithTheError)
{
    auto once = std::make_shared<OneShot<int>>();
    auto cancelled = std::make_shared<std::atomic<bool>>(false);

    std::thread teardown([once] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        BOOST_CHECK(once->cancel(std::make_exception_ptr(std::runtime_error("torn down"))));
    });

    syncWait([once, cancelled]() -> Task<void> {
        try
        {
            auto [value] = co_await *once;
            (void)value;
        }
        catch (const std::runtime_error&)
        {
            cancelled->store(true);
        }
        co_return;
    }());

    teardown.join();

    BOOST_CHECK(cancelled->load());
    BOOST_CHECK(once->settled());
}

// cancel() also settles a wait that has not parked yet: the later co_await takes the fast path and
// throws.
BOOST_AUTO_TEST_CASE(CancelBeforeAwaitThrowsOnFastPath)
{
    auto cancelled = syncWait([]() -> Task<bool> {
        OneShot<int> once;
        once.cancel();
        try
        {
            auto [value] = co_await once;
            (void)value;
        }
        catch (const std::runtime_error&)
        {
            co_return true;
        }
        co_return false;
    }());
    BOOST_CHECK(cancelled);
}

// A value that did arrive is never lost to a later teardown: cancel() after complete() is a no-op.
BOOST_AUTO_TEST_CASE(CancelAfterCompleteDoesNotOverrideTheValue)
{
    auto value = syncWait([]() -> Task<int> {
        OneShot<int> once;
        once.complete(42);
        const bool cancelled = once.cancel(std::make_exception_ptr(std::runtime_error("late")));
        auto [delivered] = co_await once;
        co_return cancelled ? -1 : delivered;
    }());
    BOOST_CHECK_EQUAL(value, 42);
}

// Whatever wins the race between complete() and cancel(), the wait ends exactly once and the two
// outcomes stay consistent: a value, or the cancellation — never a default value from a cancel.
BOOST_AUTO_TEST_CASE(CompleteAndCancelRaceStaysConsistent)
{
    for (int iteration = 0; iteration < 200; ++iteration)
    {
        auto once = std::make_shared<OneShot<int>>();
        auto winners = std::make_shared<std::atomic<int>>(0);
        auto delivered = std::make_shared<std::atomic<int>>(-1);

        std::thread completer([once, winners] {
            if (once->complete(42))
            {
                ++*winners;
            }
        });
        std::thread canceller([once, winners] {
            if (once->cancel())
            {
                ++*winners;
            }
        });

        syncWait([once, delivered]() -> Task<void> {
            try
            {
                auto [value] = co_await *once;
                delivered->store(value);
            }
            catch (const std::runtime_error&)
            {
                delivered->store(-2);  // the cancellation outcome, distinct from any value above
            }
            co_return;
        }());

        completer.join();
        canceller.join();

        BOOST_CHECK_EQUAL(winners->load(), 1);
        const auto value = delivered->load();
        BOOST_CHECK(value == 42 || value == -2);
    }
}

// Multi-value signals carry a heterogeneous tuple: this is the gateway's
// (error, optional<FrameMeta>) shape and the front's SendResult shape.
BOOST_AUTO_TEST_CASE(CarriesMultipleHeterogeneousValues)
{
    auto size = syncWait([]() -> Task<int> {
        OneShot<int, std::string, double> once;
        once.complete(1, "payload", 2.5);
        auto [number, text, ratio] = co_await once;
        co_return static_cast<int>(text.size() + number + static_cast<int>(ratio));
    }());
    BOOST_CHECK_EQUAL(size, 10);
}

BOOST_AUTO_TEST_SUITE_END()
