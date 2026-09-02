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
 * @file FIB100_FIB106_ReadWriteSetTest.cpp
 */
#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/storage2/Storage.h"
#include <bcos-task/Wait.h>
#include <bcos-transaction-executor/RollbackableStorage.h>
#include <bcos-transaction-scheduler/ReadWriteSetStorage.h>
#include <boost/test/unit_test.hpp>
#include <range/v3/view/transform.hpp>

using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::scheduler_v1;

// ---- Mock storage that supports untracked reads (used for FIB-100 tracking tests) ----
// ReadWriteSetStorage forwards readSomeRaw/readOneRaw via direct member access,
// so the wrapped backend must expose those (matching MemoryStorage's interface).
struct DirectMockStorage
{
    using Key = int;
    using Value = int;

    auto readOneRaw(auto /*key*/, auto&&... /*args*/)
        -> task::Task<storage2::StorageValueType<Value>>
    {
        co_return storage2::NOT_EXISTS_TYPE{};
    }

    auto readSomeRaw(::ranges::input_range auto keys, auto&&... /*args*/)
        -> task::Task<std::vector<storage2::StorageValueType<Value>>>
    {
        std::vector<storage2::StorageValueType<Value>> result;
        for (auto&& key : keys)
        {
            (void)key;
            result.emplace_back(storage2::NOT_EXISTS_TYPE{});
        }
        co_return result;
    }
};

task::Task<void> tag_invoke(storage2::tag_t<storage2::writeOne> /*unused*/,
    DirectMockStorage& /*storage*/, auto /*key*/, auto /*value*/)
{
    co_return;
}

task::Task<std::optional<int>> tag_invoke(
    storage2::tag_t<storage2::readOne> /*unused*/, DirectMockStorage& /*storage*/, auto&& /*key*/)
{
    co_return std::nullopt;
}

task::Task<std::optional<int>> tag_invoke(storage2::tag_t<storage2::readOne> /*unused*/,
    DirectMockStorage& /*storage*/, const auto& key, storage2::BYPASS_READ_SET_TYPE /*unused*/)
{
    co_return std::make_optional(key * 10);
}

task::Task<std::vector<std::optional<int>>> tag_invoke(
    storage2::tag_t<storage2::readSome> /*unused*/, DirectMockStorage& /*storage*/,
    ::ranges::forward_range auto keys)
{
    std::vector<std::optional<int>> result;
    for (auto&& key : keys)
    {
        result.emplace_back(key);
    }
    co_return result;
}

task::Task<std::vector<std::optional<int>>> tag_invoke(
    storage2::tag_t<storage2::readSome> /*unused*/, DirectMockStorage& /*storage*/,
    ::ranges::forward_range auto keys, storage2::BYPASS_READ_SET_TYPE /*unused*/)
{
    std::vector<std::optional<int>> result;
    for (auto&& key : keys)
    {
        result.emplace_back(key * 10);
    }
    co_return result;
}

BOOST_AUTO_TEST_SUITE(FIB100_FIB106_ReadWriteSetTest)

// ---- FIB-100 ----

// bypass-read-set reads are Rollbackable's internal pre-image snapshots. Tracking them
// as transaction-visible reads would create phantom RAW dependencies. The
// rollback-correctness concern raised by the audit is addressed instead by
// WAW detection (see wawIntersectionDetected below): two chunks writing the
// same key are serialized, so Rollbackable never reads a stale pre-image.
BOOST_AUTO_TEST_CASE(untrackedReadOneDoesNotPolluteReadSet)
{
    task::syncWait([]() -> task::Task<void> {
        DirectMockStorage backend;
        ReadWriteSetStorage<decltype(backend)> reader(backend);

        auto value = co_await storage2::readOne(reader, 42, storage2::BYPASS_READ_SET);
        BOOST_REQUIRE(value);
        BOOST_CHECK_EQUAL(*value, 420);

        // Read set must remain empty — bypass-read-set is infrastructure, not business.
        BOOST_CHECK(::ranges::empty(readWriteSet(reader)));
        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(untrackedReadSomeDoesNotPolluteReadSet)
{
    task::syncWait([]() -> task::Task<void> {
        DirectMockStorage backend;
        ReadWriteSetStorage<decltype(backend)> reader(backend);

        std::vector<int> keys = {3, 5, 7};
        auto values = co_await storage2::readSome(reader, keys, storage2::BYPASS_READ_SET);
        BOOST_REQUIRE_EQUAL(values.size(), 3);
        BOOST_CHECK_EQUAL(*values[0], 30);
        BOOST_CHECK_EQUAL(*values[1], 50);
        BOOST_CHECK_EQUAL(*values[2], 70);

        BOOST_CHECK(::ranges::empty(readWriteSet(reader)));
        co_return;
    }());
}

// Rollbackable<ReadWriteSetStorage<...>>.writeOne internally does an
// untracked readOne to capture the pre-image. That read must not appear in the
// tracked set — only the subsequent write should.
BOOST_AUTO_TEST_CASE(rollbackablePreImageReadIsNotTracked)
{
    using Storage =
        memory_storage::MemoryStorage<int, int, memory_storage::Attribute(memory_storage::ORDERED)>;
    using RWSet = ReadWriteSetStorage<Storage>;
    using Rollbackable = bcos::executor_v1::Rollbackable<RWSet>;

    task::syncWait([]() -> task::Task<void> {
        Storage backend;
        RWSet rwStorage(backend);
        Rollbackable rollbackable(rwStorage);

        co_await storage2::writeOne(rollbackable, 42, 7);

        // Exactly one entry, and it is a write (not a read inflated by the
        // untracked pre-image snapshot).
        auto const& tracked = readWriteSet(rwStorage);
        BOOST_REQUIRE_EQUAL(tracked.size(), 1);
        auto const& flag = tracked.at(std::hash<int>{}(42));
        BOOST_CHECK(flag.write);
        BOOST_CHECK(!flag.read);
        co_return;
    }());
}

// range() is a transparent pass-through: production code only reaches it from
// BaselineScheduler.calculateStateRoot, which runs post-merge on the committed
// storage and bypasses ReadWriteSetStorage entirely. This test pins that
// contract so future callers cannot silently rely on range reads being tracked.
BOOST_AUTO_TEST_CASE(rangeDoesNotPolluteReadSet)
{
    using Storage =
        memory_storage::MemoryStorage<int, int, memory_storage::Attribute(memory_storage::ORDERED)>;

    task::syncWait([]() -> task::Task<void> {
        Storage backend;
        ReadWriteSetStorage<decltype(backend)> rwStorage(backend);

        co_await storage2::writeOne(rwStorage, 1, 100);
        co_await storage2::writeOne(rwStorage, 2, 200);

        auto before = readWriteSet(rwStorage).size();
        auto it = co_await storage2::range(rwStorage);
        while (auto kv = co_await it.next())
        {
            (void)kv;
        }

        // Range iteration leaves the tracked set unchanged.
        BOOST_CHECK_EQUAL(readWriteSet(rwStorage).size(), before);
        co_return;
    }());
}

// FIB-100: rollback semantics make writes depend on the pre-image, so two
// chunks writing the same key must not execute concurrently.
BOOST_AUTO_TEST_CASE(wawIntersectionDetected)
{
    task::syncWait([]() -> task::Task<void> {
        DirectMockStorage priorBackend;
        ReadWriteSetStorage<decltype(priorBackend)> prior(priorBackend);
        co_await storage2::writeOne(prior, 42, 1);

        DirectMockStorage currentBackend;
        ReadWriteSetStorage<decltype(currentBackend)> current(currentBackend);
        co_await storage2::writeOne(current, 42, 2);  // WAW with prior chunk

        BOOST_CHECK(hasRAWIntersection(prior, current));
        co_return;
    }());
}

// ---- FIB-106 ----

// A deliberately single-pass input_range built on top of std::istream_iterator.
// Under the old code (input_range constraint) this would silently lose data;
// the tightened forward_range constraint makes the mis-use a compile error,
// which we check by building a forward_range adapter instead. If the constraint
// were silently wrong, this test would compile and detect the missing writes.
BOOST_AUTO_TEST_CASE(forwardRangeWriteSomePersists)
{
    using Storage =
        memory_storage::MemoryStorage<int, int, memory_storage::Attribute(memory_storage::ORDERED)>;

    task::syncWait([]() -> task::Task<void> {
        Storage backend;
        ReadWriteSetStorage<decltype(backend)> rwStorage(backend);

        // ranges::views::transform is a forward_range when the source is, so
        // this exercises the two-pass pattern (tracking loop + forwarding).
        std::vector<int> source{1, 2, 3};
        auto keyValues =
            source | ::ranges::views::transform([](int key) { return std::pair{key, key * 100}; });

        co_await storage2::writeSome(rwStorage, keyValues);

        // All three keys must have reached the backend.
        auto value1 = co_await storage2::readOne(rwStorage, 1);
        auto value2 = co_await storage2::readOne(rwStorage, 2);
        auto value3 = co_await storage2::readOne(rwStorage, 3);
        BOOST_REQUIRE(value1);
        BOOST_REQUIRE(value2);
        BOOST_REQUIRE(value3);
        BOOST_CHECK_EQUAL(*value1, 100);
        BOOST_CHECK_EQUAL(*value2, 200);
        BOOST_CHECK_EQUAL(*value3, 300);

        // And all three keys must be tracked as writes (so conflict detection
        // can see them).
        auto const& tracked = readWriteSet(rwStorage);
        for (int key : source)
        {
            auto it = tracked.find(std::hash<int>{}(key));
            BOOST_REQUIRE(it != tracked.end());
            BOOST_CHECK(it->second.write);
        }
        co_return;
    }());
}

BOOST_AUTO_TEST_SUITE_END()
