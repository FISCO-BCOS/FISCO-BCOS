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
 * @brief: unit test for Common.* of bcos-utilities
 *
 * @file CommonTest.cpp
 * @author: yujiechen
 * @date 2021-02-24
 */


#include "bcos-utilities/Common.h"
#include "bcos-utilities/Error.h"
#include "bcos-utilities/Exceptions.h"
#include "bcos-utilities/Ranges.h"
#include "bcos-utilities/testutils/TestPromptFixture.h"
#include <boost/test/unit_test.hpp>
#include <chrono>
#include <thread>


using namespace bcos;
using namespace std;
namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(DevcoreCommonTest, TestPromptFixture)
/// test Arith Calculations
BOOST_AUTO_TEST_CASE(testArithCal)
{
    ///=========test u2s==================
    u256 u_bigint("343894723987432");
    bigint c_end = bigint(1) << 256;
    BOOST_CHECK(u2s(u_bigint) == u_bigint);
    u_bigint = Invalid256;
    BOOST_CHECK(u2s(u_bigint) < s256(0));
    u_bigint = u256("0xa170d8e0ae1b57d7ecc121f6fe5ceb03c1267801ff720edd2f8463e7effac6c6");
    BOOST_CHECK(u2s(u_bigint) < s256(0));
    BOOST_CHECK(u2s(u_bigint) == s256(-(c_end - u_bigint)));
    u_bigint = u256("0x7170d8e0ae1b57d7ecc121f6fe5ceb03c1267801ff720edd2f8463e7effac6c6");
    BOOST_CHECK(u2s(u_bigint) == u_bigint);
    ///=========test s2u==================
    s256 s_bigint("0x7170d8e0ae1b57d7ecc121f6fe5ceb03c1267801ff720edd2f8463e7effac6c6");
    BOOST_CHECK(s2u(s_bigint) == s_bigint);
    s_bigint = s256("0xf170d8e0ae1b57d7ecc121f6fe5ceb03c1267801ff720edd2f8463e7effac6c6");
    BOOST_CHECK(s2u(s_bigint) == u256(c_end + s_bigint));
    ///=========test exp10==================
    BOOST_CHECK(exp10<1>() == u256(10));
    BOOST_CHECK(exp10<9>() == u256(1000000000));
    BOOST_CHECK(exp10<0>() == u256(1));
}
/// test utcTime
BOOST_AUTO_TEST_CASE(testUtcTime)
{
    uint64_t old_time = utcTime();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    BOOST_CHECK(old_time < utcTime());
}

BOOST_AUTO_TEST_CASE(testGuards)
{
    Mutex mutex;
    int count = 0;
    int max = 8;

    auto f = [&]() {
        Guard l(mutex);
        count++;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    };

    // auto begin = std::chrono::high_resolution_clock::now();

    thread* t = new thread[max];
    for (int i = 0; i < max; i++)
    {
        t[i] = thread(f);
    }

    for (int i = 0; i < max; i++)
    {
        t[i].join();
    }

    // auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
    //     std::chrono::high_resolution_clock::now() - begin);

    BOOST_CHECK(count == max);

    // uint64_t end_time = end.tv_sec * 1000000 + end.tv_usec;
    // uint64_t begin_time = begin.tv_sec * 1000000 + begin.tv_usec;
    // BOOST_CHECK((end_time - begin_time) >= (uint64_t(max) * 1000));
}

BOOST_AUTO_TEST_CASE(testWriteGuard)
{
    SharedMutex mutex;
    int count = 0;
    int max = 8;

    auto f = [&]() {
        WriteGuard l(mutex);
        count++;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    };

    // struct timeval begin;
    // gettimeofday(&begin, NULL);

    thread* t = new thread[max];
    for (int i = 0; i < max; i++)
    {
        t[i] = thread(f);
    }

    for (int i = 0; i < max; i++)
    {
        t[i].join();
    }

    // struct timeval end;
    // gettimeofday(&end, NULL);

    // uint64_t end_time = end.tv_sec * 1000000 + end.tv_usec;
    // uint64_t begin_time = begin.tv_sec * 1000000 + begin.tv_usec;

    // BOOST_CHECK((end_time - begin_time) > (uint64_t(max) * 1000));
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
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        f0();  // recursive
    };

    // struct timeval begin;
    // gettimeofday(&begin, NULL);

    thread* t = new thread[max];
    for (int i = 0; i < max; i++)
    {
        t[i] = thread(f1);
    }

    for (int i = 0; i < max; i++)
    {
        t[i].join();
    }

    // struct timeval end;
    // gettimeofday(&end, NULL);

    // uint64_t end_time = end.tv_sec * 1000000 + end.tv_usec;
    // uint64_t begin_time = begin.tv_sec * 1000000 + begin.tv_usec;
    // BOOST_CHECK((end_time - begin_time) >= (uint64_t(max) * 1000));
    BOOST_CHECK(count == 2 * max);
}
BOOST_AUTO_TEST_CASE(testError)
{
    std::string errorMessage = " test error";
    int64_t errorCode = -100042;
    Error::Ptr error = std::make_unique<Error>(errorCode, errorMessage);
    BOOST_CHECK(error->errorCode() == errorCode);
    BOOST_CHECK(error->errorMessage() == errorMessage);

    errorMessage = " test error234";
    errorCode = 100044;
    error->setErrorCode(errorCode);
    error->setErrorMessage(errorMessage);
    BOOST_CHECK(error->errorCode() == errorCode);
    BOOST_CHECK(error->errorMessage() == errorMessage);
}

void testRange(RANGES::range auto range)
{
    BOOST_CHECK(RANGES::size(range) > 0);
}

BOOST_AUTO_TEST_CASE(range)
{
    std::vector<int> list = {1, 2, 3, 4, 5};

    testRange(list);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
