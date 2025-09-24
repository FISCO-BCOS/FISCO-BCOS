/**
 *  Copyright (C) 2023 FISCO BCOS.
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
 * @file BucketMapTest.cpp
 * @author: Jimmy Shi
 * @date 2023/3/3
 */

#include "bcos-utilities/BucketMap.h"
#include "bcos-utilities/testutils/TestPromptFixture.h"
#include <tbb/parallel_for.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace std;

namespace bcos
{
namespace test
{


BOOST_FIXTURE_TEST_SUITE(BucketMap, TestPromptFixture)

BOOST_AUTO_TEST_CASE(serialTest)
{
    using BKMap = bcos::BucketMap<int, int, std::hash<int>>;
    using ReadAccessor = BKMap::ReadAccessor;
    using WriteAccessor = BKMap::WriteAccessor;

    BKMap bucketMap(10);
    std::cout << bucketMap.size() << std::endl;
    BOOST_CHECK(bucketMap.empty());

    // insert
    for (int i = 0; i < 100; i++)
    {
        WriteAccessor accessor;
        bucketMap.insert(accessor, {i, i});
    }
    BOOST_CHECK_EQUAL(100, bucketMap.size());

    std::cout << std::endl;
    // for each read
    for (auto& accessor : bucketMap.range<ReadAccessor>())
    {
        std::cout << accessor.key() << ":" << accessor.value() << std::endl;
        BOOST_CHECK_EQUAL(accessor.key(), accessor.value());
    }

    // for each write
    for (auto& accessor : bucketMap.range<WriteAccessor>())
    {
        accessor.value()++;
    }

    std::cout << std::endl;
    // for each read
    for (auto& accessor : bucketMap.range<ReadAccessor>())
    {
        std::cout << accessor.key() << ":" << accessor.value() << std::endl;
        BOOST_CHECK_EQUAL(accessor.key() + 1, accessor.value());
    }
    BOOST_CHECK_EQUAL(100, bucketMap.size());

    // remove & find
    for (size_t i = 0; i < 10; i++)
    {
        {  // find
            ReadAccessor accessor;
            bool has = bucketMap.find<ReadAccessor>(accessor, i);
            BOOST_CHECK_EQUAL(accessor.key(), i);
            BOOST_CHECK_EQUAL(accessor.value(), i + 1);
            BOOST_CHECK(has);
        }


        {  // constains
            bool has = bucketMap.contains(i);
            BOOST_CHECK(has);
        }

        // remove
        if (WriteAccessor accessor; bucketMap.find<WriteAccessor>(accessor, i))
        {
            bucketMap.remove(accessor);
        }


        {  // find again
            ReadAccessor accessor;
            bool has = bucketMap.find<ReadAccessor>(accessor, i);
            BOOST_CHECK(!has);
        }

        {  // constains ?
            bool has = bucketMap.contains(i);
            BOOST_CHECK(!has);
        }
    }
    BOOST_CHECK_EQUAL(90, bucketMap.size());

    std::cout << std::endl;
    // for each size check
    {
        for (auto& accessor : bucketMap.range<ReadAccessor>())
        {
            std::cout << accessor.key() << ":" << accessor.value() << std::endl;
        }
        BOOST_CHECK_EQUAL(90, bucketMap.size());
    }

    // for each begin with certain startId
    {
        for (auto& accessor : bucketMap.rangeByKey<ReadAccessor>(666))
        {
            std::cout << accessor.key() << ":" << accessor.value() << std::endl;
        }
        BOOST_CHECK_EQUAL(90, bucketMap.size());
    }

    // clear
    bucketMap.clear();
    BOOST_CHECK(!bucketMap.contains(6));
    BOOST_CHECK(bucketMap.empty());
}

BOOST_AUTO_TEST_CASE(batchFindTest)
{
    using BKMap = bcos::BucketMap<int, int, std::hash<int>>;
    using ReadAccessor = BKMap::ReadAccessor;
    using WriteAccessor = BKMap::WriteAccessor;

    BKMap bucketMap(10);
    std::cout << bucketMap.size() << std::endl;
    BOOST_CHECK(bucketMap.empty());
    std::vector<int> keys;

    // insert
    for (int i = 0; i < 100; i++)
    {
        WriteAccessor accessor;
        bucketMap.insert(accessor, {i, i});
        keys.push_back(i);
    }
    BOOST_CHECK_EQUAL(100, bucketMap.size());

    std::atomic_size_t cnt = 0;
    bucketMap.traverse<WriteAccessor, true>(
        keys, [&](WriteAccessor& accessor, const auto& range, auto& bucket) {
            for (auto index : range)
            {
                if (bucket.find(accessor, keys[index]))
                {
                    accessor.value()++;
                    cnt++;
                    std::cout << accessor.key() << ":" << accessor.value() << std::endl;
                }
            }
        });
    BOOST_CHECK_EQUAL(cnt, 100);

    cnt = 0;
    for (auto& accessor : bucketMap.range<ReadAccessor>())
    {
        BOOST_CHECK_EQUAL(accessor.key() + 1, accessor.value());
        cnt++;
    }
    BOOST_CHECK_EQUAL(cnt, 100);
}

BOOST_AUTO_TEST_CASE(parallelTest)
{
    using BKMap = bcos::BucketMap<int, int, std::hash<int>>;
    using ReadAccessor = BKMap::ReadAccessor;
    using WriteAccessor = BKMap::WriteAccessor;

    BKMap bucketMap(10);
    std::cout << bucketMap.size() << std::endl;

    // insert
    tbb::parallel_for(
        tbb::blocked_range<int>(0, 5000), [&bucketMap](const tbb::blocked_range<int>& range) {
            for (auto i = range.begin(); i < range.end(); ++i)
            {
                size_t magic = std::rand() % 500;
                size_t op = magic % 7;
                switch (op)
                {
                case 0:
                {
                    for (auto& accessor : bucketMap.range<ReadAccessor>())
                    {
                        std::cout << accessor.key() << ":" << accessor.value() << std::endl;
                    }
                    break;
                }
                case 1:
                {
                    for (auto& accessor : bucketMap.range<WriteAccessor>())
                    {
                        accessor.value()++;
                    }
                    break;
                }
                case 2:
                {
                    // concurrent read and write
                    for (auto& accessor : bucketMap.range<ReadAccessor>())
                    {
                        std::cout << accessor.key() << ":" << accessor.value() << std::endl;
                    }
                    for (auto& accessor : bucketMap.range<WriteAccessor>())
                    {
                        accessor.value()++;
                    }
                    break;
                }
                case 3:
                {
                    WriteAccessor accessor;
                    bucketMap.insert(accessor, {magic, magic});
                    break;
                }
                case 4:
                {
                    bucketMap.contains(magic);
                    break;
                }
                case 5:
                {
                    if (WriteAccessor accessor; bucketMap.find<WriteAccessor>(accessor, magic))
                    {
                        bucketMap.remove(accessor);
                    }

                    break;
                }
                case 6:
                {
                    WriteAccessor accessor;
                    bucketMap.find<WriteAccessor>(accessor, magic);
                    break;
                }
                default:
                {
                    bucketMap.size();
                    break;
                }
                }
            }
        });
}

BOOST_AUTO_TEST_CASE(parallelTest2)
{
    using BKMap = bcos::BucketMap<int, int, std::hash<int>>;
    using ReadAccessor = BKMap::ReadAccessor;
    using WriteAccessor = BKMap::WriteAccessor;

    BKMap bucketMap(10);
    std::cout << bucketMap.size() << std::endl;

    int total = 500;

    tbb::parallel_for(
        tbb::blocked_range<int>(0, total), [&bucketMap](const tbb::blocked_range<int>& range) {
            auto kvs = RANGES::iota_view<int, int>(range.begin(), range.end()) |
                       RANGES::views::transform([](int i) { return std::make_pair(i, i); }) |
                       ::ranges::to<std::vector>();

            bucketMap.batchInsert(kvs);
        });
    BOOST_CHECK_EQUAL(total, bucketMap.size());

    tbb::parallel_for(
        tbb::blocked_range<int>(0, total), [&bucketMap](const tbb::blocked_range<int>& range) {
            auto ks = RANGES::iota_view<int, int>(range.begin(), range.end()) |
                      ::ranges::to<std::vector>();

            bucketMap.traverse<WriteAccessor, true>(
                ks, [&](WriteAccessor& accessor, const auto& range, auto& bucket) {
                    for (auto index : range)
                    {
                        if (bucket.find(accessor, ks[index]))
                        {
                            accessor.value()++;
                        }
                    }
                });
        });

    for (auto& accessor : bucketMap.range<ReadAccessor>())
    {
        BOOST_CHECK_EQUAL(accessor.value(), accessor.key() + 1);
    }
    std::cout << bucketMap.size() << std::endl;

    tbb::parallel_for(
        tbb::blocked_range<int>(0, total), [&bucketMap](const tbb::blocked_range<int>& range) {
            auto ks = RANGES::iota_view<int, int>(range.begin(), range.end()) |
                      ::ranges::to<std::vector>();

            bucketMap.batchRemove(ks);
        });
    BOOST_CHECK_EQUAL(0, bucketMap.size());

    std::cout << bucketMap.size() << std::endl;
}

BOOST_AUTO_TEST_CASE(stringKeyTransparentContains)
{
    using BKMap = bcos::BucketMap<std::string, int, bcos::StringHash>;
    using WriteAccessor = BKMap::WriteAccessor;

    constexpr std::size_t kBucketCount = 8;
    constexpr int kInsertCount = 10;

    BKMap mapUnderTest(kBucketCount);
    // Insert with std::string keys
    for (int i = 0; i < kInsertCount; ++i)
    {
        std::string keyStr = "key_" + std::to_string(i);
        WriteAccessor writeAcc;
        mapUnderTest.insert(writeAcc, {keyStr, i});
    }
    // Use std::string_view transparent contains
    for (int i = 0; i < kInsertCount; ++i)
    {
        std::string str = "key_" + std::to_string(i);
        std::string_view strView{str};
        BOOST_CHECK(mapUnderTest.contains(strView));
    }
    // Miss case
    BOOST_CHECK(!mapUnderTest.contains(std::string_view{"not-exist"}));
}

BOOST_AUTO_TEST_CASE(batchFindCovers)
{
    using BKMap = bcos::BucketMap<int, int, std::hash<int>>;
    using ReadAccessor = BKMap::ReadAccessor;

    constexpr std::size_t kBucketCount = 7;
    constexpr int kHitCount = 64;
    constexpr int kMissStart = 100;
    constexpr int kMissEndExclusive = 132;  // 32 misses
    constexpr int kMultiplier = 10;

    BKMap mapUnderTest(kBucketCount);

    std::vector<std::pair<int, int>> kvs;
    kvs.reserve(kHitCount);
    for (int i = 0; i < kHitCount; ++i)
    {
        kvs.emplace_back(i, i * kMultiplier);
    }
    mapUnderTest.batchInsert(kvs);

    // Keys including hits and misses
    std::vector<int> keys;
    keys.reserve(static_cast<std::size_t>(kHitCount) +
                 static_cast<std::size_t>(kMissEndExclusive - kMissStart));
    for (int i = 0; i < kHitCount; ++i)
    {
        keys.push_back(i);
    }
    for (int i = kMissStart; i < kMissEndExclusive; ++i)
    {
        keys.push_back(i);
    }

    auto vals = mapUnderTest.batchFind<ReadAccessor>(keys);
    BOOST_CHECK_EQUAL(vals.size(), keys.size());

    std::size_t found = 0;
    std::size_t miss = 0;
    for (std::size_t i = 0; i < keys.size(); ++i)
    {
        if (keys[i] < kHitCount)
        {
            BOOST_CHECK(vals[i].has_value());
            BOOST_CHECK_EQUAL(vals[i].value(), keys[i] * kMultiplier);
            ++found;
        }
        else
        {
            BOOST_CHECK(!vals[i].has_value());
            ++miss;
        }
    }
    BOOST_CHECK_EQUAL(found, static_cast<std::size_t>(kHitCount));
    BOOST_CHECK_EQUAL(miss, static_cast<std::size_t>(kMissEndExclusive - kMissStart));

    // Verify deletion again via batchRemove
    std::vector<int> toRemove{0, 1, 2, 3, 4, 5, 6};
    mapUnderTest.batchRemove(toRemove);
    for (int key : toRemove)
    {
        BOOST_CHECK(!mapUnderTest.contains(key));
        ReadAccessor ra;
        bool ok = mapUnderTest.find<ReadAccessor>(ra, key);
        BOOST_CHECK(!ok);
    }
}

BOOST_AUTO_TEST_CASE(rangeByKeyAndWrap)
{
    using BKMap = bcos::BucketMap<int, int, std::hash<int>>;
    using ReadAccessor = BKMap::ReadAccessor;
    using WriteAccessor = BKMap::WriteAccessor;

    const std::size_t bucketSize = 11;  // Prime number to cover hash modulo distribution

    BKMap mapUnderTest(bucketSize);

    // Sparse inserts to alternate empty and non-empty buckets
    for (int i = 0; i < 110; i += 5)
    {
        WriteAccessor writer;
        mapUnderTest.insert(writer, {i, i});
    }

    // Start from a non-existing key to ensure wrap-around across all buckets
    std::size_t visited = 0;
    for (auto& acc : mapUnderTest.rangeByKey<ReadAccessor>(666))
    {
        BOOST_CHECK_EQUAL(acc.key(), acc.value());
        ++visited;
    }
    // A total of 22 elements inserted
    BOOST_CHECK_EQUAL(visited, 22);

    // Range iteration on an empty map
    BKMap emptyMap(bucketSize);
    std::size_t visitedEmpty = 0;
    for (auto& acc : emptyMap.range<ReadAccessor>())
    {
        (void)acc;
        ++visitedEmpty;
    }
    BOOST_CHECK_EQUAL(visitedEmpty, 0);
}

BOOST_AUTO_TEST_CASE(bucketSetBatchInsertReturn)
{
    using Set = bcos::BucketSet<int, std::hash<int>>;

    constexpr std::size_t kBucketCount = 8;

    Set setUnderTest(kBucketCount);

    // First batchInsert should return all true
    std::vector<int> keys{1, 2, 3, 4, 5};
    auto result1 = setUnderTest.batchInsert<true>(keys);
    BOOST_CHECK_EQUAL(result1.size(), keys.size());
    for (auto inserted : result1)
    {
        BOOST_CHECK(inserted);
    }

    // Second insert should return all false
    auto result2 = setUnderTest.batchInsert<true>(keys);
    for (auto inserted : result2)
    {
        BOOST_CHECK(!inserted);
    }

    // Validate with contains
    for (int key : keys)
    {
        BOOST_CHECK(setUnderTest.contains(key));
    }

    // Clear and validate
    setUnderTest.clear();
    for (int key : keys)
    {
        BOOST_CHECK(!setUnderTest.contains(key));
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos