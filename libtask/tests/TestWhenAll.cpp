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
 * @brief Unit tests for whenAll: concurrent fan-out, total completion under failures,
 *        deterministic first-in-child-order errors and ordered results.
 * @file TestWhenAll.cpp
 */
#include "bcos-task/OneShot.h"
#include "bcos-task/Task.h"
#include "bcos-task/Wait.h"
#include "bcos-task/WhenAll.h"
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <chrono>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <type_traits>
#include <vector>

using namespace bcos::task;

BOOST_AUTO_TEST_SUITE(WhenAllTest)

namespace
{
// Parks on the gate, then returns the value. The gate arrives as a by-value coroutine parameter,
// so the parked frame owns its copy outright (a capturing lambda temporary would dangle).
Task<int> gatedValue(OneShot<>::Ptr gate, int value)
{
    co_await *gate;
    co_return value;
}

Task<void> gatedMark(OneShot<>::Ptr gate, std::shared_ptr<std::atomic<int>> counter)
{
    co_await *gate;
    counter->fetch_add(1);
    co_return;
}

Task<int> gatedThrow(
    OneShot<>::Ptr gate, std::string message, std::shared_ptr<std::atomic<int>> counter)
{
    co_await *gate;
    counter->fetch_add(1);
    throw std::runtime_error(std::move(message));
}

// A throwing child MUST still contain a co_* keyword — a body without one is not a coroutine
// and the throw would fire synchronously at task construction instead of inside the fan-out.
Task<void> failInline(std::shared_ptr<std::atomic<int>> counter, std::string message)
{
    counter->fetch_add(1);
    throw std::runtime_error(std::move(message));
    co_return;
}
}  // namespace

BOOST_AUTO_TEST_CASE(EmptyVectorCompletesImmediately)
{
    auto values = syncWait(whenAll(std::vector<Task<int>>{}));
    BOOST_CHECK(values.empty());
    syncWait(whenAll(std::vector<Task<void>>{}));
}

// Children that never suspend complete inline during startup; the awaiting coroutine must take
// the no-suspend fast path and still observe every result in child order.
BOOST_AUTO_TEST_CASE(InlineChildrenKeepOrder)
{
    std::vector<Task<int>> tasks;
    for (int i = 0; i < 8; ++i)
    {
        tasks.emplace_back([](int value) -> Task<int> { co_return value; }(i));
    }
    auto values = syncWait(whenAll(std::move(tasks)));
    BOOST_REQUIRE_EQUAL(values.size(), 8U);
    for (int i = 0; i < 8; ++i)
    {
        BOOST_CHECK_EQUAL(values[i], i);
    }
}

// Genuinely suspending children: completions land from another thread, one gate at a time, and
// the fan-out finishes only after the LAST child settles. Assertions run on the main thread; the
// worker only records outcomes (boost.test's framework is not thread-safe).
BOOST_AUTO_TEST_CASE(SuspendingChildrenCompleteAfterTheLastSettle)
{
    constexpr int kCount = 3;
    std::vector<std::shared_ptr<OneShot<>>> gates;
    std::vector<Task<int>> tasks;
    for (int i = 0; i < kCount; ++i)
    {
        gates.push_back(std::make_shared<OneShot<>>());
        tasks.emplace_back(gatedValue(gates.back(), i * 10));
    }

    std::atomic<bool> finished{false};
    std::vector<int> results;
    std::exception_ptr error;
    std::thread worker([&]() {
        try
        {
            results = syncWait(whenAll(std::move(tasks)));
        }
        catch (...)
        {
            error = std::current_exception();
        }
        finished.store(true);
    });

    for (int i = 0; i < kCount - 1; ++i)
    {
        gates[i]->complete();
        // The fan-out must still be parked: one child remains unsettled.
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        BOOST_CHECK(!finished.load());
    }
    gates.back()->complete();
    worker.join();
    BOOST_CHECK(finished.load());
    BOOST_CHECK(!error);
    BOOST_REQUIRE_EQUAL(results.size(), 3U);
    for (int i = 0; i < kCount; ++i)
    {
        BOOST_CHECK_EQUAL(results[i], i * 10);
    }
}

// A failing child neither aborts its siblings nor abandons them: every side effect is observed
// and the error surfaces only after all children settled. Child 1 fails inline during startup;
// children 0 and 2 park until settled from this thread.
BOOST_AUTO_TEST_CASE(VoidVectorFailureIsTotal)
{
    auto counter = std::make_shared<std::atomic<int>>(0);
    auto gate0 = std::make_shared<OneShot<>>();
    auto gate2 = std::make_shared<OneShot<>>();
    std::vector<Task<void>> tasks;
    tasks.emplace_back(gatedMark(gate0, counter));
    tasks.emplace_back(failInline(counter, "void child failed"));
    tasks.emplace_back(gatedMark(gate2, counter));

    std::string message;
    std::thread worker([&]() {
        try
        {
            syncWait(whenAll(std::move(tasks)));
        }
        catch (std::runtime_error& e)
        {
            message = e.what();
        }
    });
    gate0->complete();
    gate2->complete();
    worker.join();
    BOOST_CHECK_EQUAL(message, "void child failed");
    BOOST_CHECK_EQUAL(counter->load(), 3);
}

// Deterministic errors: several children fail, settling in REVERSE child order; the rethrown
// exception is still the first one in child order.
BOOST_AUTO_TEST_CASE(FirstErrorInChildOrderWins)
{
    constexpr int kCount = 3;
    auto counter = std::make_shared<std::atomic<int>>(0);
    std::vector<std::shared_ptr<OneShot<>>> gates;
    std::vector<Task<int>> tasks;
    for (int i = 0; i < kCount; ++i)
    {
        gates.push_back(std::make_shared<OneShot<>>());
        tasks.emplace_back(gatedThrow(gates.back(), "error-" + std::to_string(i), counter));
    }

    std::string message;
    std::thread worker([&]() {
        try
        {
            (void)syncWait(whenAll(std::move(tasks)));
        }
        catch (std::runtime_error& e)
        {
            message = e.what();
        }
    });

    // Settle in reverse: error-2 lands first chronologically, error-0 must still win.
    for (int i = kCount - 1; i >= 0; --i)
    {
        gates[i]->complete();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    worker.join();
    BOOST_CHECK_EQUAL(message, "error-0");
    BOOST_CHECK_EQUAL(counter->load(), kCount);
}

// Heterogeneous variadic fan-out: one tuple element per child, void as std::monostate.
BOOST_AUTO_TEST_CASE(VariadicMixedVoidAndValues)
{
    auto counter = std::make_shared<std::atomic<int>>(0);
    auto result = syncWait(whenAll([]() -> Task<int> { co_return 7; }(),
        [](std::shared_ptr<std::atomic<int>> c) -> Task<void> {
            c->fetch_add(1);
            co_return;
        }(counter),
        []() -> Task<std::string> { co_return "ok"; }()));

    static_assert(std::tuple_size_v<decltype(result)> == 3);
    static_assert(std::is_same_v<std::tuple_element_t<1, decltype(result)>, std::monostate>);
    BOOST_CHECK_EQUAL(std::get<0>(result), 7);
    BOOST_CHECK_EQUAL(std::get<2>(result), "ok");
    BOOST_CHECK_EQUAL(counter->load(), 1);
}

// The variadic flavor applies the same deterministic error rule: the first child throws inline,
// the second settles later from this thread; the first child's error wins.
BOOST_AUTO_TEST_CASE(VariadicRethrowsFirstErrorInChildOrder)
{
    auto gate = std::make_shared<OneShot<>>();
    std::string message;
    std::thread worker([&]() {
        try
        {
            (void)syncWait(
                whenAll(failInline(std::make_shared<std::atomic<int>>(0), "variadic-first"),
                    gatedValue(gate, 1)));
        }
        catch (std::runtime_error& e)
        {
            message = e.what();
        }
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    gate->complete();
    worker.join();
    BOOST_CHECK_EQUAL(message, "variadic-first");
}

BOOST_AUTO_TEST_CASE(VariadicOwnsTemporaryChildrenAcrossStatements)
{
    auto all = whenAll(
        []() -> Task<int> { co_return 11; }(), []() -> Task<std::string> { co_return "owned"; }());
    auto result = syncWait(std::move(all));
    BOOST_CHECK_EQUAL(std::get<0>(result), 11);
    BOOST_CHECK_EQUAL(std::get<1>(result), "owned");
}

BOOST_AUTO_TEST_CASE(StartedFanOutCompletesAfterAwaiterDestroyed)
{
    auto gate0 = std::make_shared<OneShot<>>();
    auto gate1 = std::make_shared<OneShot<>>();
    auto counter = std::make_shared<std::atomic<int>>(0);
    std::vector<Task<void>> tasks;
    tasks.emplace_back(gatedMark(gate0, counter));
    tasks.emplace_back(gatedMark(gate1, counter));
    {
        auto all = whenAll(std::move(tasks));
        all.start();
    }
    BOOST_CHECK_EQUAL(counter->load(), 0);
    gate0->complete();
    BOOST_CHECK_EQUAL(counter->load(), 1);
    gate1->complete();
    BOOST_CHECK_EQUAL(counter->load(), 2);
}

BOOST_AUTO_TEST_SUITE_END()
