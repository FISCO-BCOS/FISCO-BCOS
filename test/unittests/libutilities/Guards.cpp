/**
 *  Copyright (C) 2020 FISCO BCOS.
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
 * @brief Construct a new boost auto test case object for Guards
 *
 * @file Guards.cpp
 * @author: tabsu
 * @date 2018-08-24
 */

#include <libutilities/Common.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
#include <thread>

using namespace bcos;
using namespace std;

namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(Guards, TestOutputHelperFixture)

BOOST_AUTO_TEST_CASE(testDevGuards)
{
    Mutex mutex;
    int count = 0;
    int max = 8;

    auto f = [&]() {
        Guard l(mutex);
        count++;
        usleep(1000);  // 1 ms
    };

    struct timeval begin;
    gettimeofday(&begin, NULL);

    thread* t = new thread[max];
    for (int i = 0; i < max; i++)
    {
        t[i] = thread(f);
    }

    for (int i = 0; i < max; i++)
    {
        t[i].join();
    }

    struct timeval end;
    gettimeofday(&end, NULL);

    BOOST_CHECK(count == max);

    uint64_t end_time = end.tv_sec * 1000000 + end.tv_usec;
    uint64_t begin_time = begin.tv_sec * 1000000 + begin.tv_usec;
    BOOST_CHECK((end_time - begin_time) >= (uint64_t(max) * 1000));
}

BOOST_AUTO_TEST_CASE(testWriteGuard)
{
    SharedMutex mutex;
    int count = 0;
    int max = 8;

    auto f = [&]() {
        WriteGuard l(mutex);
        count++;
        usleep(1000);  // 1 ms
    };

    struct timeval begin;
    gettimeofday(&begin, NULL);

    thread* t = new thread[max];
    for (int i = 0; i < max; i++)
    {
        t[i] = thread(f);
    }

    for (int i = 0; i < max; i++)
    {
        t[i].join();
    }

    struct timeval end;
    gettimeofday(&end, NULL);

    uint64_t end_time = end.tv_sec * 1000000 + end.tv_usec;
    uint64_t begin_time = begin.tv_sec * 1000000 + begin.tv_usec;

    BOOST_CHECK((end_time - begin_time) > (uint64_t(max) * 1000));
    BOOST_CHECK(count == max);
}

BOOST_AUTO_TEST_CASE(testRecursiveGuard)
{
    RecursiveMutex mutex;
    int count = 0;
    int max = 8;

    auto f0 = [&]() {
        RecursiveGuard l(mutex);
        count++;
    };

    auto f1 = [&]() {
        RecursiveGuard l(mutex);
        count++;
        usleep(1000);  // 1 ms
        f0();          // recursive
    };

    struct timeval begin;
    gettimeofday(&begin, NULL);

    thread* t = new thread[max];
    for (int i = 0; i < max; i++)
    {
        t[i] = thread(f1);
    }

    for (int i = 0; i < max; i++)
    {
        t[i].join();
    }

    struct timeval end;
    gettimeofday(&end, NULL);

    uint64_t end_time = end.tv_sec * 1000000 + end.tv_usec;
    uint64_t begin_time = begin.tv_sec * 1000000 + begin.tv_usec;
    BOOST_CHECK((end_time - begin_time) >= (uint64_t(max) * 1000));
    BOOST_CHECK(count == 2 * max);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
