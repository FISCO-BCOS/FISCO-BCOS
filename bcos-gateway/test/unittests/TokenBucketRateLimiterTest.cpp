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
 * @brief test for TokenBucketRateLimiterTest
 * @file TokenBucketRateLimiterTest.cpp
 * @author: jimmyshi
 * @date 2023-07-23
 */

#include <bcos-utilities/ratelimiter/TokenBucketRateLimiter.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>
#include <thread>

using namespace bcos;
using namespace bcos::test;

BOOST_FIXTURE_TEST_SUITE(TokenBucketRateLimiterTest, TestPromptFixture)


BOOST_AUTO_TEST_CASE(acquireTest)
{
    int64_t maxPermit = 100;
    bcos::ratelimiter::TokenBucketRateLimiter rateLimiter(maxPermit);
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // check acquire failed
    BOOST_CHECK(!rateLimiter.acquire(maxPermit + 1));

    // check acquire bound
    BOOST_CHECK(rateLimiter.acquire(maxPermit));

    // check acquire waiting and must get
    BOOST_CHECK(rateLimiter.acquire(1));
}

BOOST_AUTO_TEST_CASE(tryAcquireTest)
{
    int64_t maxPermit = 100;
    bcos::ratelimiter::TokenBucketRateLimiter rateLimiter(maxPermit);
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // check acquire failed
    BOOST_CHECK(!rateLimiter.tryAcquire(maxPermit + 1));

    // check acquire bound
    BOOST_CHECK(rateLimiter.tryAcquire(maxPermit));

    // check acquire waiting and must get
    BOOST_CHECK(!rateLimiter.tryAcquire(maxPermit));

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // check acquire waiting and must get
    BOOST_CHECK(rateLimiter.tryAcquire(maxPermit));
}

BOOST_AUTO_TEST_CASE(rollbackTest)
{
    int64_t maxPermit = 100;
    bcos::ratelimiter::TokenBucketRateLimiter rateLimiter(maxPermit);
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    BOOST_CHECK(rateLimiter.acquire(maxPermit));

    // check acquire waiting and must get
    BOOST_CHECK(!rateLimiter.tryAcquire(maxPermit));

    rateLimiter.rollback(maxPermit / 10);

    BOOST_CHECK(rateLimiter.tryAcquire(maxPermit / 10));
}

BOOST_AUTO_TEST_SUITE_END()
