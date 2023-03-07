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

    // insert
    for (int i = 0; i < 100; i++)
    {
        WriteAccessor::Ptr accessor;
        bucketMap.insert(accessor, {i, i});
    }

    std::cout << std::endl;
    // for each read
    bucketMap.forEachRead([](ReadAccessor::Ptr accessor) {
        std::cout << accessor->key() << ":" << accessor->value() << std::endl;
        return true;
    });

    // for each write
    bucketMap.forEachWrite([](WriteAccessor::Ptr accessor) {
        accessor->value()++;
        return true;
    });

    std::cout << std::endl;
    // for each read
    bucketMap.forEachRead([](ReadAccessor::Ptr accessor) {
        std::cout << accessor->key() << ":" << accessor->value() << std::endl;
        return true;
    });

    // remove
    for (size_t i = 0; i < 10; i++)
    {
        bucketMap.remove(i);
    }

    std::cout << std::endl;
    // for each read
    bucketMap.forEachRead([](ReadAccessor::Ptr accessor) {
        std::cout << accessor->key() << ":" << accessor->value() << std::endl;
        return true;
    });
}

BOOST_AUTO_TEST_CASE(parallelTest)
{
    using BKMap = bcos::BucketMap<int, int, std::hash<int>>;
    using ReadAccessor = BKMap::ReadAccessor;
    using WriteAccessor = BKMap::WriteAccessor;

    BKMap bucketMap(10);
    std::cout << bucketMap.size() << std::endl;

    // insert
    tbb::parallel_for(tbb::blocked_range<size_t>(0, 10000),
        [&bucketMap](const tbb::blocked_range<size_t>& range) {
            for (auto i = range.begin(); i < range.end(); ++i)
            {
                size_t op = i % 5;
                switch (op)
                {
                case 1:
                {
                    bucketMap.forEachRead([](ReadAccessor::Ptr accessor) {
                        std::cout << accessor->key() << ":" << accessor->value() << std::endl;
                        return true;
                    });
                }
                case 2:
                {
                    bucketMap.forEachWrite([](WriteAccessor::Ptr accessor) {
                        accessor->value()++;
                        return true;
                    });
                }
                default:
                {
                    WriteAccessor::Ptr accessor;
                    bucketMap.insert(accessor, {i, i});
                    break;
                    bucketMap.remove(i);
                }
                }
            }
        });
}


BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos