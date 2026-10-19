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
 * @brief Unit tests for Immediate: a value that is already there. `co_await` must never suspend, so
 *        the synchronous end of the library's awaitables stays synchronous.
 * @file TestImmediate.cpp
 */
#include "bcos-task/Immediate.h"
#include "bcos-task/OneShot.h"
#include "bcos-task/Task.h"
#include "bcos-task/Trait.h"
#include "bcos-task/Wait.h"
#include <boost/test/unit_test.hpp>
#include <string>
#include <type_traits>

using namespace bcos::task;

BOOST_AUTO_TEST_SUITE(ImmediateTest)

// The defining property, asserted at compile time: the value is ready before anyone awaits it.
// Everything that treats Immediate as "the awaitable that never blocks" (MPTPruner's parallel-sweep
// safety check is the load-bearing one) rests on these two facts.
static_assert(Immediate<int>::await_ready());
static_assert(Immediate<void>::await_ready());
static_assert(std::is_same_v<decltype(Immediate<int>(1).await_resume()), int>);
static_assert(std::is_void_v<decltype(Immediate<void>().await_resume())>);

// A value is delivered by value, with no suspension in between.
BOOST_AUTO_TEST_CASE(DeliversTheValueInline)
{
    auto value = syncWait([]() -> Task<int> { co_return co_await Immediate<int>(42); }());
    BOOST_CHECK_EQUAL(value, 42);
}

// The void specialisation must not require a default-constructible payload and must be awaitable on
// its own (this is the shape every storage backend returns).
BOOST_AUTO_TEST_CASE(VoidSpecialisationIsAwaitable)
{
    auto awaited = syncWait([]() -> Task<bool> {
        co_await Immediate<void>();
        co_return true;
    }());
    BOOST_CHECK(awaited);
}

// Immediate is what the trait machinery sees: an awaitable whose await_resume yields the value.
static_assert(IsAwaitable<Immediate<int>>);
static_assert(std::is_same_v<AwaitableReturnType<Immediate<int>>, int>);
static_assert(std::is_same_v<AwaitableReturnType<Immediate<void>>, void>);

// Both ends of the library's awaitables are the same concept, and differ only in WHEN the value is
// there: Immediate answers at once, OneShot answers later (or not at all). This is the pairing the
// names encode, so it is worth pinning down.
static_assert(IsAwaitable<OneShot<int>>);
static_assert(std::is_same_v<AwaitableReturnType<OneShot<int>>, std::tuple<int>>);

// Moving a non-trivial payload through the inline path must not copy it.
BOOST_AUTO_TEST_CASE(MovesNonTrivialValues)
{
    auto size = syncWait([]() -> Task<std::size_t> {
        auto payload = std::string(64, 'x');
        auto delivered = co_await Immediate<std::string>(std::move(payload));
        co_return delivered.size();
    }());
    BOOST_CHECK_EQUAL(size, 64);
}

BOOST_AUTO_TEST_SUITE_END()
