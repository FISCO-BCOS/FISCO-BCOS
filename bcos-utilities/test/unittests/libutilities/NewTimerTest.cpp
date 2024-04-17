/**
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
 * @brief Unit tests for the NewTimer.cpp
 * @file NewTimerTest.cpp
 */
#include "bcos-utilities/NewTimer.h"
#include "bcos-utilities/testutils/TestPromptFixture.h"
#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>
#include <chrono>
#include <thread>

namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(TimerTest, TestPromptFixture)

BOOST_AUTO_TEST_CASE(testTimer)
{
    timer::TimerFactory timerFactory;

    {
        auto timer = timerFactory.createTimer([] {}, 0, 0);
        BOOST_CHECK_EQUAL(timer->delayMS(), 0);
        BOOST_CHECK_EQUAL(timer->periodMS(), 0);
    }

    {
        auto timer = timerFactory.createTimer([] {}, 10, 10);
        BOOST_CHECK_EQUAL(timer->delayMS(), 10);
        BOOST_CHECK_EQUAL(timer->periodMS(), 10);
    }

    {
        int count = 0;
        auto timer = timerFactory.createTimer([&count] { count++; }, 0, 0);
        timer->start();
        BOOST_CHECK_EQUAL(timer->delayMS(), 0);
        BOOST_CHECK_EQUAL(timer->periodMS(), 0);
        BOOST_CHECK_EQUAL(count, 1);
        timer->stop();
    }

    {
        int count = 0;
        auto timer = timerFactory.createTimer([&count] { count++; }, 1000, 0);
        timer->start();

        std::this_thread::sleep_for(std::chrono::seconds(3));

        BOOST_CHECK_EQUAL(timer->delayMS(), 0);
        BOOST_CHECK_EQUAL(timer->periodMS(), 1000);
        BOOST_CHECK_GE(count, 2);
        timer->stop();
    }

    {
        int count = 0;
        auto timer = timerFactory.createTimer([&count] { count++; }, 1000, 10000);
        timer->start();

        std::this_thread::sleep_for(std::chrono::milliseconds(11100));

        BOOST_CHECK_EQUAL(timer->delayMS(), 10000);
        BOOST_CHECK_EQUAL(timer->periodMS(), 1000);
        BOOST_CHECK(count >= 1);
        timer->stop();
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
