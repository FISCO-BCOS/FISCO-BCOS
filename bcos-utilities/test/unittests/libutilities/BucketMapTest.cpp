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
#include <chrono>
#include <thread>

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
        WriteAccessor::Ptr accessor;
        bucketMap.insert(accessor, {i, i});
    }
    BOOST_CHECK_EQUAL(100, bucketMap.size());

    std::cout << std::endl;
    // for each read
    bucketMap.forEach<ReadAccessor>([](ReadAccessor::Ptr accessor) {
        std::cout << accessor->key() << ":" << accessor->value() << std::endl;
        BOOST_CHECK_EQUAL(accessor->key(), accessor->value());
        return true;
    });

    // for each write
    bucketMap.forEach<WriteAccessor>([](WriteAccessor::Ptr accessor) {
        accessor->value()++;
        return true;
    });

    std::cout << std::endl;
    // for each read
    bucketMap.forEach<ReadAccessor>([](ReadAccessor::Ptr accessor) {
        std::cout << accessor->key() << ":" << accessor->value() << std::endl;
        BOOST_CHECK_EQUAL(accessor->key() + 1, accessor->value());
        return true;
    });
    BOOST_CHECK_EQUAL(100, bucketMap.size());

    // remove & find
    for (size_t i = 0; i < 10; i++)
    {
        {  // find
            ReadAccessor::Ptr accessor;
            bool has = bucketMap.find<ReadAccessor>(accessor, i);
            BOOST_CHECK_EQUAL(accessor->key(), i);
            BOOST_CHECK_EQUAL(accessor->value(), i + 1);
            BOOST_CHECK(has);
        }


        {  // constains
            bool has = bucketMap.contains(i);
            BOOST_CHECK(has);
        }

        // remove
        bucketMap.remove(i);

        {  // find again
            ReadAccessor::Ptr accessor;
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
        size_t cnt = 0;
        bucketMap.forEach<ReadAccessor>([&cnt](ReadAccessor::Ptr accessor) {
            std::cout << accessor->key() << ":" << accessor->value() << std::endl;
            cnt++;
            return true;
        });
        BOOST_CHECK_EQUAL(90, bucketMap.size());
    }

    // for each begin with certain startId
    {
        size_t cnt = 0;
        bucketMap.forEach<ReadAccessor>(666, [&cnt](ReadAccessor::Ptr accessor) {
            std::cout << accessor->key() << ":" << accessor->value() << std::endl;
            cnt++;
            return true;
        });
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
        WriteAccessor::Ptr accessor;
        bucketMap.insert(accessor, {i, i});
        keys.push_back(i);
    }
    BOOST_CHECK_EQUAL(100, bucketMap.size());

    size_t cnt = 0;
    bucketMap.batchFind<WriteAccessor>(keys, [&cnt](const int& key, WriteAccessor::Ptr accessor) {
        BOOST_CHECK(accessor);
        BOOST_CHECK_EQUAL(key, accessor->key());
        std::cout << accessor->key() << ":" << accessor->value() << std::endl;
        accessor->value()++;
        cnt++;
        return true;
    });
    BOOST_CHECK_EQUAL(cnt, 100);

    cnt = 0;
    bucketMap.batchFind<ReadAccessor>(keys, [&cnt](const int& key, ReadAccessor::Ptr accessor) {
        BOOST_CHECK(accessor);
        BOOST_CHECK_EQUAL(key, accessor->key());
        BOOST_CHECK_EQUAL(accessor->value(), accessor->key() + 1);
        std::cout << accessor->key() << ":" << accessor->value() << std::endl;
        cnt++;
        return true;
    });
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
                    bucketMap.forEach<ReadAccessor>([](ReadAccessor::Ptr accessor) {
                        std::cout << accessor->key() << ":" << accessor->value() << std::endl;
                        return true;
                    });
                    break;
                }
                case 1:
                {
                    bucketMap.forEach<WriteAccessor>([](WriteAccessor::Ptr accessor) {
                        accessor->value()++;
                        return true;
                    });
                    break;
                }
                case 2:
                {
                    // concurrent read and write
                    bucketMap.forEach<ReadAccessor>([](ReadAccessor::Ptr accessor) {
                        std::cout << accessor->key() << ":" << accessor->value() << std::endl;
                        return true;
                    });
                    bucketMap.forEach<WriteAccessor>([](WriteAccessor::Ptr accessor) {
                        accessor->value()++;
                        return true;
                    });
                    break;
                }
                case 3:
                {
                    WriteAccessor::Ptr accessor;
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
                    bucketMap.remove(magic);
                    break;
                }
                case 6:
                {
                    WriteAccessor::Ptr accessor;
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
                       RANGES::views::transform([](int i) { return std::make_pair(i, i); });

            bucketMap.batchInsert(kvs, [](bool, const int& key, WriteAccessor::Ptr accessor) {});
        });
    BOOST_CHECK_EQUAL(total, bucketMap.size());

    tbb::parallel_for(
        tbb::blocked_range<int>(0, total), [&bucketMap](const tbb::blocked_range<int>& range) {
            auto ks = RANGES::iota_view<int, int>(range.begin(), range.end());

            bucketMap.batchFind<WriteAccessor>(ks, [](const int& key, WriteAccessor::Ptr accessor) {
                accessor->value()++;
                return true;
            });
        });

    tbb::parallel_for(
        tbb::blocked_range<int>(0, total), [&bucketMap](const tbb::blocked_range<int>& range) {
            auto ks = RANGES::iota_view<int, int>(range.begin(), range.end());

            bucketMap.batchFind<ReadAccessor>(ks, [](const int& key, ReadAccessor::Ptr accessor) {
                // BOOST_CHECK_EQUAL(accessor->value(), accessor->key() + 1);
                return true;
            });
        });
    std::cout << bucketMap.size() << std::endl;

    tbb::parallel_for(
        tbb::blocked_range<int>(0, total), [&bucketMap](const tbb::blocked_range<int>& range) {
            auto ks = RANGES::iota_view<int, int>(range.begin(), range.end());

            bucketMap.batchRemove(ks, [](bool, const int& key, const int& value) {});
        });
    BOOST_CHECK_EQUAL(0, bucketMap.size());

    std::cout << bucketMap.size() << std::endl;
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos