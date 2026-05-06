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
 * @file FIB100_FIB106_FIB109_ReadWriteSetTest.cpp
 */
#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/storage2/Storage.h"
#include <bcos-task/Wait.h>
#include <bcos-transaction-scheduler/ReadWriteSetStorage.h>
#include <boost/test/unit_test.hpp>
#include <range/v3/view/single.hpp>

using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::scheduler_v1;

// Mock storage that supports DIRECT reads (readOne and readSome)
struct DirectMockStorage
{
    using Key = int;
    using Value = int;
};

// writeOne: no-op store for tracking tests
task::Task<void> tag_invoke(storage2::tag_t<storage2::writeOne> /*unused*/,
    DirectMockStorage& /*storage*/, auto /*key*/, auto /*value*/)
{
    co_return;
}

// readOne without DIRECT: should not be called in DIRECT tests
task::Task<std::optional<int>> tag_invoke(
    storage2::tag_t<storage2::readOne> /*unused*/, DirectMockStorage& /*storage*/, auto&& /*key*/)
{
    co_return std::nullopt;
}

// readOne with DIRECT: returns key * 10
task::Task<std::optional<int>> tag_invoke(storage2::tag_t<storage2::readOne> /*unused*/,
    DirectMockStorage& /*storage*/, const auto& key, storage2::DIRECT_TYPE /*unused*/)
{
    co_return std::make_optional(key * 10);
}

// readSome without DIRECT
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

// readSome with DIRECT: returns key * 10 for each key
task::Task<std::vector<std::optional<int>>> tag_invoke(
    storage2::tag_t<storage2::readSome> /*unused*/, DirectMockStorage& /*storage*/,
    ::ranges::forward_range auto keys, storage2::DIRECT_TYPE /*unused*/)
{
    std::vector<std::optional<int>> result;
    for (auto&& key : keys)
    {
        result.emplace_back(key * 10);
    }
    co_return result;
}

BOOST_AUTO_TEST_SUITE(FIB100_FIB106_FIB109_ReadWriteSetTest)

// FIB-100: DIRECT readOne now tracks the key in the read set
BOOST_AUTO_TEST_CASE(directReadOneTracksReadSet)
{
    task::syncWait([]() -> task::Task<void> {
        DirectMockStorage mockStorage;
        ReadWriteSetStorage<decltype(mockStorage)> rwStorage(mockStorage);

        // Write key 42 via a second storage (to detect RAW)
        DirectMockStorage writerBackend;
        ReadWriteSetStorage<decltype(writerBackend)> writerStorage(writerBackend);
        co_await storage2::writeOne(writerStorage, 42, 1);

        // DIRECT readOne key 42 should now track it
        auto value = co_await storage2::readOne(rwStorage, 42, storage2::DIRECT);
        BOOST_CHECK(value);
        BOOST_CHECK_EQUAL(*value, 420);

        // The read of key 42 should create a RAW intersection with the writer
        BOOST_CHECK(hasRAWIntersection(writerStorage, rwStorage));

        co_return;
    }());
}

// FIB-100: DIRECT readSome now tracks keys in the read set
BOOST_AUTO_TEST_CASE(directReadSomeTracksReadSet)
{
    task::syncWait([]() -> task::Task<void> {
        DirectMockStorage mockStorage;
        ReadWriteSetStorage<decltype(mockStorage)> rwStorage(mockStorage);

        // Write key 5 via a separate storage
        DirectMockStorage writerBackend;
        ReadWriteSetStorage<decltype(writerBackend)> writerStorage(writerBackend);
        co_await storage2::writeOne(writerStorage, 5, 1);

        // DIRECT readSome keys {3, 5, 7}
        std::vector<int> keys = {3, 5, 7};
        auto values = co_await storage2::readSome(rwStorage, keys, storage2::DIRECT);
        BOOST_CHECK_EQUAL(values.size(), 3);
        BOOST_CHECK_EQUAL(*values[0], 30);
        BOOST_CHECK_EQUAL(*values[1], 50);
        BOOST_CHECK_EQUAL(*values[2], 70);

        // Key 5 was read and written, so RAW intersection should exist
        BOOST_CHECK(hasRAWIntersection(writerStorage, rwStorage));

        co_return;
    }());
}

// FIB-106: readSome with forward_range compiles and works correctly
BOOST_AUTO_TEST_CASE(readSomeForwardRangeWorks)
{
    using Storage =
        memory_storage::MemoryStorage<int, int, memory_storage::Attribute(memory_storage::ORDERED)>;

    task::syncWait([]() -> task::Task<void> {
        Storage backend;
        ReadWriteSetStorage<decltype(backend)> rwStorage(backend);

        // Write some data first
        co_await storage2::writeOne(rwStorage, 1, 100);
        co_await storage2::writeOne(rwStorage, 2, 200);
        co_await storage2::writeOne(rwStorage, 3, 300);

        // readSome with a vector (forward_range)
        std::vector<int> keys = {1, 2, 3};
        auto values = co_await storage2::readSome(rwStorage, keys);
        BOOST_CHECK_EQUAL(values.size(), 3);
        BOOST_CHECK_EQUAL(*values[0], 100);
        BOOST_CHECK_EQUAL(*values[1], 200);
        BOOST_CHECK_EQUAL(*values[2], 300);

        co_return;
    }());
}

// FIB-109: writeOne tracks key in write set only after co_await completes
BOOST_AUTO_TEST_CASE(writeOneTracksAfterCompletion)
{
    using Storage =
        memory_storage::MemoryStorage<int, int, memory_storage::Attribute(memory_storage::ORDERED)>;

    task::syncWait([]() -> task::Task<void> {
        Storage backend;
        ReadWriteSetStorage<decltype(backend)> rwStorage(backend);

        // After writeOne, the key should be in the write set
        co_await storage2::writeOne(rwStorage, 10, 42);

        auto const& rwSet = readWriteSet(rwStorage);
        auto hash = std::hash<int>{}(10);
        auto it = rwSet.find(hash);
        BOOST_REQUIRE(it != rwSet.end());
        BOOST_CHECK(it->second.write);

        co_return;
    }());
}

// FIB-100 + RAW: DIRECT reads create proper RAW intersections with writes
BOOST_AUTO_TEST_CASE(directReadsCreateRAWIntersections)
{
    task::syncWait([]() -> task::Task<void> {
        DirectMockStorage backend1;
        ReadWriteSetStorage<decltype(backend1)> writer(backend1);

        DirectMockStorage backend2;
        ReadWriteSetStorage<decltype(backend2)> reader(backend2);

        // Writer writes keys 10, 20, 30
        co_await storage2::writeOne(writer, 10, 1);
        co_await storage2::writeOne(writer, 20, 2);
        co_await storage2::writeOne(writer, 30, 3);

        // Reader does a DIRECT read of key 99 (not written)
        co_await storage2::readOne(reader, 99, storage2::DIRECT);
        BOOST_CHECK(!hasRAWIntersection(writer, reader));

        // Reader does a DIRECT read of key 20 (was written)
        co_await storage2::readOne(reader, 20, storage2::DIRECT);
        BOOST_CHECK(hasRAWIntersection(writer, reader));

        co_return;
    }());
}

BOOST_AUTO_TEST_SUITE_END()
