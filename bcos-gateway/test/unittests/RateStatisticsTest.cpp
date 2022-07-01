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
 * @brief test for RateLimiterManager
 * @file RateLimiterManagerTest.cpp
 * @author: octopus
 * @date 2021-05-17
 */

#include <bcos-gateway/GatewayConfig.h>
#include <bcos-gateway/libratelimit/BWRateStatistics.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/filesystem.hpp>
#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>
#include <memory>

using namespace bcos;
using namespace gateway;
using namespace bcos::test;

BOOST_FIXTURE_TEST_SUITE(RateStatisticsTest, TestPromptFixture)

BOOST_AUTO_TEST_CASE(test_rateStatisticsTest_statistics)
{
    bcos::gateway::ratelimit::Statistics s;

    int64_t dataSize = 1024;

    s.update(dataSize);

    BOOST_CHECK_EQUAL(s.lastCount, 1);
    BOOST_CHECK_EQUAL(s.totalCount, 1);
    BOOST_CHECK_EQUAL(s.totalDataSize, dataSize);
    BOOST_CHECK_EQUAL(s.lastDataSize, dataSize);

    s.update(dataSize);

    BOOST_CHECK_EQUAL(s.lastCount, 2);
    BOOST_CHECK_EQUAL(s.totalCount, 2);
    BOOST_CHECK_EQUAL(s.totalDataSize, 2 * dataSize);
    BOOST_CHECK_EQUAL(s.lastDataSize, 2 * dataSize);

    BOOST_CHECK_EQUAL(s.toString("prefix", 1).value(),
        " 	[prefix]  	 |total data: 2048 |last data: 2048 |total count: 2 |last count: 2 |avg "
        "rate(Mb/s): 15.62");

    s.resetLast();

    BOOST_CHECK_EQUAL(s.toString("prefix", 1).value(),
        " 	[prefix]  	 |total data: 2048 |last data: 0 |total count: 2 |last count: 0 |avg "
        "rate(Mb/s): 0.00");

    BOOST_CHECK_EQUAL(s.lastCount, 0);
    BOOST_CHECK_EQUAL(s.totalCount, 2);
    BOOST_CHECK_EQUAL(s.lastDataSize, 0);
    BOOST_CHECK_EQUAL(s.totalDataSize, 2 * dataSize);
}


BOOST_AUTO_TEST_CASE(test_rateStatisticsTest_outStat)
{
    auto rateStatistics = std::make_shared<ratelimit::BWRateStatistics>();

    std::string ep0 = "127.0.0.1";
    std::string ep1 = "127.0.0.2";
    int64_t dataSize = 1024;

    std::string group0 = "group0";
    std::string group1 = "group1";
    std::string group2 = "group2";

    uint16_t moduleID0 = 1000;
    uint16_t moduleID1 = 1001;

    int times = 1000;
    for (int i = 0; i < times; i++)
    {
        rateStatistics->updateOutGoing(ep0, dataSize);
        rateStatistics->updateOutGoing(ep1, dataSize);

        rateStatistics->updateOutGoing(group0, moduleID0, dataSize);
        rateStatistics->updateOutGoing(group1, moduleID1, dataSize);
        rateStatistics->updateOutGoing(group2, moduleID1, dataSize);
    }

    auto& outStat = rateStatistics->outStat();

    BOOST_CHECK_EQUAL(outStat.size(), 6);

    for (auto& [k, v] : outStat)
    {
        if (k == bcos::gateway::ratelimit::BWRateStatistics::TOTAL_INCOMING)
        {
            BOOST_CHECK_EQUAL(v.lastCount, times * 2);
            BOOST_CHECK_EQUAL(v.totalCount, times * 2);

            BOOST_CHECK_EQUAL(v.lastDataSize, times * dataSize * 2);
            BOOST_CHECK_EQUAL(v.totalDataSize, times * dataSize * 2);
        }
        else
        {
            BOOST_CHECK_EQUAL(v.lastCount, times);
            BOOST_CHECK_EQUAL(v.totalCount, times);

            BOOST_CHECK_EQUAL(v.lastDataSize, times * dataSize);
            BOOST_CHECK_EQUAL(v.totalDataSize, times * dataSize);
        }
    }

    rateStatistics->flushStat();

    BOOST_CHECK_EQUAL(outStat.size(), 6);

    for (auto& [k, v] : outStat)
    {
        if (k == bcos::gateway::ratelimit::BWRateStatistics::TOTAL_INCOMING)
        {
            BOOST_CHECK_EQUAL(v.lastCount, 0);
            BOOST_CHECK_EQUAL(v.totalCount, times * 2);

            BOOST_CHECK_EQUAL(v.lastDataSize, 0);
            BOOST_CHECK_EQUAL(v.totalDataSize, times * dataSize * 2);
        }
        else
        {
            BOOST_CHECK_EQUAL(v.lastCount, 0);
            BOOST_CHECK_EQUAL(v.totalCount, times);

            BOOST_CHECK_EQUAL(v.lastDataSize, 0);
            BOOST_CHECK_EQUAL(v.totalDataSize, times * dataSize);
        }
    }

    rateStatistics->flushStat();
}


BOOST_AUTO_TEST_CASE(test_rateStatisticsTest_inStat)
{
    auto rateStatistics = std::make_shared<ratelimit::BWRateStatistics>();

    std::string ep0 = "127.0.0.1";
    std::string ep1 = "127.0.0.2";
    int64_t dataSize = 10240;

    std::string group0 = "group0";
    std::string group1 = "group1";
    std::string group2 = "group2";

    uint16_t moduleID0 = 1000;
    uint16_t moduleID1 = 1001;

    int times = 1000;
    for (int i = 0; i < times; i++)
    {
        rateStatistics->updateInComing(ep0, dataSize);
        rateStatistics->updateInComing(ep1, dataSize);

        rateStatistics->updateInComing(group0, moduleID0, dataSize);
        rateStatistics->updateInComing(group1, moduleID1, dataSize);
        rateStatistics->updateInComing(group2, moduleID1, dataSize);
    }

    auto& inStat = rateStatistics->inStat();

    BOOST_CHECK_EQUAL(inStat.size(), 6);

    for (auto& [k, v] : inStat)
    {
        if (k == bcos::gateway::ratelimit::BWRateStatistics::TOTAL_INCOMING)
        {
            BOOST_CHECK_EQUAL(v.lastCount, times * 2);
            BOOST_CHECK_EQUAL(v.totalCount, times * 2);

            BOOST_CHECK_EQUAL(v.lastDataSize, times * dataSize * 2);
            BOOST_CHECK_EQUAL(v.totalDataSize, times * dataSize * 2);
        }
        else
        {
            BOOST_CHECK_EQUAL(v.lastCount, times);
            BOOST_CHECK_EQUAL(v.totalCount, times);

            BOOST_CHECK_EQUAL(v.lastDataSize, times * dataSize);
            BOOST_CHECK_EQUAL(v.totalDataSize, times * dataSize);
        }
    }

    rateStatistics->flushStat();

    BOOST_CHECK_EQUAL(inStat.size(), 6);

    for (auto& [k, v] : inStat)
    {
        if (k == bcos::gateway::ratelimit::BWRateStatistics::TOTAL_INCOMING)
        {
            BOOST_CHECK_EQUAL(v.lastCount, 0);
            BOOST_CHECK_EQUAL(v.totalCount, times * 2);

            BOOST_CHECK_EQUAL(v.lastDataSize, 0);
            BOOST_CHECK_EQUAL(v.totalDataSize, times * dataSize * 2);
        }
        else
        {
            BOOST_CHECK_EQUAL(v.lastCount, 0);
            BOOST_CHECK_EQUAL(v.totalCount, times);

            BOOST_CHECK_EQUAL(v.lastDataSize, 0);
            BOOST_CHECK_EQUAL(v.totalDataSize, times * dataSize);
        }
    }

    rateStatistics->flushStat();
}

BOOST_AUTO_TEST_SUITE_END()
