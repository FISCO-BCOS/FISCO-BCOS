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

#include "bcos-gateway/libratelimit/RateLimiterManager.h"
#include "bcos-framework/protocol/Protocol.h"
#include "bcos-utilities/ratelimiter/DistributedRateLimiter.h"
#include "bcos-gateway/libratelimit/RateLimiterFactory.h"
#include "bcos-utilities/ratelimiter/TimeWindowRateLimiter.h"
#include <bcos-gateway/GatewayConfig.h>
#include <bcos-gateway/GatewayFactory.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/filesystem.hpp>
#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>
#include <chrono>
#include <memory>
#include <thread>

using namespace bcos;
using namespace gateway;
using namespace bcos::test;

BOOST_FIXTURE_TEST_SUITE(RateLimiterManagerTest, TestPromptFixture)

BOOST_AUTO_TEST_CASE(test_timeWindowRateLimiter_allowExceedMaxPermitSize)
{
    {
        uint64_t maxPermitsSize = 10000;
        uint64_t timeWindowMS = 3000;
        auto allowExceedMaxPermitSize = false;
        auto rateLimiter = std::make_shared<bcos::ratelimiter::TimeWindowRateLimiter>(
            maxPermitsSize, timeWindowMS, allowExceedMaxPermitSize);

        BOOST_CHECK(rateLimiter->timeWindowMS() == timeWindowMS);
        BOOST_CHECK(rateLimiter->maxPermitsSize() == maxPermitsSize);
        BOOST_CHECK(rateLimiter->currentPermitsSize() == maxPermitsSize);
        BOOST_CHECK(rateLimiter->allowExceedMaxPermitSize() == allowExceedMaxPermitSize);

        uint64_t permitsSize = 2 * maxPermitsSize;
        BOOST_CHECK(permitsSize > maxPermitsSize);
        BOOST_CHECK(permitsSize > rateLimiter->maxPermitsSize());

        BOOST_CHECK(!rateLimiter->acquire(permitsSize));
        BOOST_CHECK(!rateLimiter->tryAcquire(permitsSize));
    }

    {
        uint64_t maxPermitsSize = 10000;
        uint64_t timeWindowMS = 3000;
        auto allowExceedMaxPermitSize = true;
        auto rateLimiter = std::make_shared<bcos::ratelimiter::TimeWindowRateLimiter>(
            maxPermitsSize, timeWindowMS, allowExceedMaxPermitSize);

        BOOST_CHECK(rateLimiter->timeWindowMS() == timeWindowMS);
        BOOST_CHECK(rateLimiter->maxPermitsSize() == maxPermitsSize);
        BOOST_CHECK(rateLimiter->currentPermitsSize() == maxPermitsSize);
        BOOST_CHECK(rateLimiter->allowExceedMaxPermitSize() == allowExceedMaxPermitSize);

        uint64_t permitsSize = 2 * maxPermitsSize;
        BOOST_CHECK(permitsSize > maxPermitsSize);
        BOOST_CHECK(permitsSize > rateLimiter->maxPermitsSize());

        BOOST_CHECK(rateLimiter->acquire(permitsSize));
        BOOST_CHECK(rateLimiter->tryAcquire(permitsSize));
    }
}

BOOST_AUTO_TEST_CASE(test_timeWindowRateLimiter)
{
    uint64_t maxPermitsSize = 2000;
    uint64_t timeWindowMS = 2000;
    auto allowExceedMaxPermitSize = false;
    auto rateLimiter = std::make_shared<bcos::ratelimiter::TimeWindowRateLimiter>(
        maxPermitsSize, timeWindowMS, allowExceedMaxPermitSize);

    BOOST_CHECK(rateLimiter->timeWindowMS() == timeWindowMS);
    BOOST_CHECK(rateLimiter->maxPermitsSize() == maxPermitsSize);
    BOOST_CHECK(rateLimiter->currentPermitsSize() == maxPermitsSize);
    BOOST_CHECK(rateLimiter->allowExceedMaxPermitSize() == allowExceedMaxPermitSize);

    {
        uint64_t permitsSize = 2001;
        BOOST_CHECK(permitsSize > rateLimiter->maxPermitsSize());
        BOOST_CHECK(!rateLimiter->tryAcquire(permitsSize));
        BOOST_CHECK(!rateLimiter->acquire(permitsSize));
    }

    {
        uint64_t permitsSize = 2000;
        BOOST_CHECK(rateLimiter->tryAcquire(permitsSize));
        BOOST_CHECK(rateLimiter->currentPermitsSize() == 0);
        BOOST_CHECK(!rateLimiter->tryAcquire(permitsSize));
        BOOST_CHECK(rateLimiter->acquire(permitsSize));
    }

    {
        int64_t permitsSize = 2000;
        std::this_thread::sleep_for(std::chrono::milliseconds(timeWindowMS / 2));
        BOOST_CHECK(!rateLimiter->tryAcquire(permitsSize));
        BOOST_CHECK(rateLimiter->currentPermitsSize() == 0);
        BOOST_CHECK(rateLimiter->timeWindowMS() == timeWindowMS);
        BOOST_CHECK(rateLimiter->maxPermitsSize() == maxPermitsSize);

        std::this_thread::sleep_for(std::chrono::milliseconds(timeWindowMS));
        BOOST_CHECK(rateLimiter->timeWindowMS() == timeWindowMS);
        BOOST_CHECK(rateLimiter->maxPermitsSize() == maxPermitsSize);
        BOOST_CHECK(rateLimiter->tryAcquire(permitsSize / 2));
        BOOST_CHECK(rateLimiter->currentPermitsSize() == (maxPermitsSize - permitsSize / 2));
        BOOST_CHECK(!rateLimiter->tryAcquire(permitsSize));

        rateLimiter->rollback(permitsSize);

        BOOST_CHECK(rateLimiter->timeWindowMS() == timeWindowMS);
        BOOST_CHECK(rateLimiter->maxPermitsSize() == maxPermitsSize);
        BOOST_CHECK(rateLimiter->currentPermitsSize() == (uint64_t)permitsSize);
        BOOST_CHECK(rateLimiter->tryAcquire(permitsSize));

        std::this_thread::sleep_for(std::chrono::milliseconds(timeWindowMS));
        BOOST_CHECK(rateLimiter->tryAcquire(200));
        BOOST_CHECK(rateLimiter->tryAcquire(400));
        BOOST_CHECK(rateLimiter->tryAcquire(600));
        BOOST_CHECK(rateLimiter->tryAcquire(800));
        BOOST_CHECK(!rateLimiter->tryAcquire(200));
        BOOST_CHECK(rateLimiter->acquire(2000));
    }
}

BOOST_AUTO_TEST_CASE(test_rateLimiterManager)
{
    auto gatewayFactory = std::make_shared<GatewayFactory>("", "");
    bcos::gateway::GatewayConfig::RateLimiterConfig rateLimiterConfig;
    bcos::gateway::GatewayConfig::RedisConfig redisConfig;
    auto rateLimiterManager = gatewayFactory->buildRateLimiterManager(rateLimiterConfig, nullptr);
    auto rateLimiterFactory = rateLimiterManager->rateLimiterFactory();

    BOOST_CHECK(!rateLimiterConfig.enableOutRateLimit());
    BOOST_CHECK(!rateLimiterConfig.enableDistributedRatelimit);
    BOOST_CHECK(rateLimiterConfig.enableDistributedRateLimitCache);
    BOOST_CHECK(!rateLimiterConfig.allowExceedMaxPermitSize);
    BOOST_CHECK(rateLimiterConfig.timeWindowSec == 1);
    BOOST_CHECK(rateLimiterConfig.totalOutgoingBwLimit < 0);
    BOOST_CHECK(rateLimiterConfig.connOutgoingBwLimit < 0);
    BOOST_CHECK(rateLimiterConfig.groupOutgoingBwLimit < 0);
    BOOST_CHECK(rateLimiterConfig.group2BwLimit.empty());

    BOOST_CHECK(!rateLimiterConfig.enableOutGroupRateLimit());
    BOOST_CHECK(!rateLimiterConfig.enableOutConnRateLimit());
    BOOST_CHECK(!rateLimiterConfig.enableDistributedRatelimit);

    BOOST_CHECK(!rateLimiterConfig.enableInRateLimit());
    BOOST_CHECK(!rateLimiterConfig.enableInP2pBasicMsgLimit());
    BOOST_CHECK(!rateLimiterConfig.enableInP2pModuleMsgLimit(1));
    BOOST_CHECK(rateLimiterConfig.p2pBasicMsgQPS < 0);
    BOOST_CHECK(rateLimiterConfig.p2pModuleMsgQPS < 0);
    BOOST_CHECK(rateLimiterConfig.moduleMsg2QPS.size() == 65535);
    BOOST_CHECK(rateLimiterConfig.moduleMsg2QPSSize == 0);

    BOOST_CHECK(rateLimiterManager->getRateLimiter("192.108.0.0") == nullptr);

    BOOST_CHECK(
        rateLimiterManager
            ->registerRateLimiter("192.108.0.0", rateLimiterFactory->buildTimeWindowRateLimiter(10))
            .first);
    BOOST_CHECK(!rateLimiterManager
                     ->registerRateLimiter(
                         "192.108.0.0", rateLimiterFactory->buildTimeWindowRateLimiter(10))
                     .first);

    BOOST_CHECK(rateLimiterManager->getRateLimiter("192.108.0.0") != nullptr);
    BOOST_CHECK(rateLimiterManager->removeRateLimiter("192.108.0.0"));
    BOOST_CHECK(!rateLimiterManager->removeRateLimiter("192.108.0.0"));

    BOOST_CHECK(rateLimiterManager->getRateLimiter("192.108.0.0") == nullptr);

    auto result = rateLimiterManager->registerRateLimiter(
        "192.108.0.0", rateLimiterFactory->buildTimeWindowRateLimiter(10));
    BOOST_CHECK(result.first);
    BOOST_CHECK(result.second == nullptr);
    BOOST_CHECK(rateLimiterManager->getRateLimiter("192.108.0.0") != nullptr);

    result = rateLimiterManager->registerRateLimiter(
        "192.108.0.0", rateLimiterFactory->buildTimeWindowRateLimiter(10));
    BOOST_CHECK(!result.first);
    BOOST_CHECK(result.second != nullptr);
}

BOOST_AUTO_TEST_CASE(test_rateLimiterManager_configIPv4)
{
    std::string configIni("data/config/config_ipv4.ini");

    boost::property_tree::ptree pt;
    boost::property_tree::ini_parser::read_ini(configIni, pt);

    auto config = std::make_shared<GatewayConfig>();
    config->initFlowControlConfig(pt);

    BOOST_CHECK(config->rateLimiterConfig().enableOutRateLimit());
    BOOST_CHECK(config->rateLimiterConfig().enableOutConnRateLimit());
    BOOST_CHECK(config->rateLimiterConfig().enableOutGroupRateLimit());
    BOOST_CHECK(config->rateLimiterConfig().enableDistributedRatelimit);
    BOOST_CHECK(config->rateLimiterConfig().enableDistributedRateLimitCache);
    BOOST_CHECK_EQUAL(config->rateLimiterConfig().distributedRateLimitCachePercent, 13);

    BOOST_CHECK(config->rateLimiterConfig().enableInRateLimit());
    BOOST_CHECK(config->rateLimiterConfig().enableInP2pBasicMsgLimit());
    BOOST_CHECK(config->rateLimiterConfig().enableInP2pModuleMsgLimit(1));
    BOOST_CHECK(config->rateLimiterConfig().enableInP2pModuleMsgLimit(9));

    BOOST_CHECK_EQUAL(config->rateLimiterConfig().p2pBasicMsgTypes.size(), 5);
    BOOST_CHECK(config->rateLimiterConfig().p2pBasicMsgTypes.contains(1));
    BOOST_CHECK(config->rateLimiterConfig().p2pBasicMsgTypes.contains(2));
    BOOST_CHECK(config->rateLimiterConfig().p2pBasicMsgTypes.contains(3));
    BOOST_CHECK(config->rateLimiterConfig().p2pBasicMsgTypes.contains(6));
    BOOST_CHECK(config->rateLimiterConfig().p2pBasicMsgTypes.contains(9));
    BOOST_CHECK(!config->rateLimiterConfig().p2pBasicMsgTypes.contains(10));

    BOOST_CHECK_EQUAL(config->rateLimiterConfig().p2pModuleMsgQPS, 555);
    BOOST_CHECK_EQUAL(config->rateLimiterConfig().p2pBasicMsgQPS, 123);
    BOOST_CHECK_EQUAL(config->rateLimiterConfig().moduleMsg2QPSSize, 3);
    BOOST_CHECK(config->rateLimiterConfig().moduleMsg2QPS.at(1));
    BOOST_CHECK(config->rateLimiterConfig().moduleMsg2QPS.at(5));
    BOOST_CHECK(config->rateLimiterConfig().moduleMsg2QPS.at(7));
    BOOST_CHECK(!config->rateLimiterConfig().moduleMsg2QPS.at(9));
    BOOST_CHECK(!config->rateLimiterConfig().moduleMsg2QPS.at(10));

    auto rateLimiterConfig = config->rateLimiterConfig();
    auto gatewayFactory = std::make_shared<GatewayFactory>("", "");
    auto rateLimiterManager = gatewayFactory->buildRateLimiterManager(rateLimiterConfig, nullptr);

    auto rateLimiterFactory = rateLimiterManager->rateLimiterFactory();

    auto timeWindowSec = rateLimiterConfig.timeWindowSec;

    BOOST_CHECK(rateLimiterFactory != nullptr);
    BOOST_CHECK(rateLimiterManager->isP2pBasicMsgType(1));
    BOOST_CHECK(rateLimiterManager->isP2pBasicMsgType(2));
    BOOST_CHECK(rateLimiterManager->isP2pBasicMsgType(3));
    BOOST_CHECK(rateLimiterManager->isP2pBasicMsgType(6));
    BOOST_CHECK(rateLimiterManager->isP2pBasicMsgType(9));
    BOOST_CHECK(!rateLimiterManager->isP2pBasicMsgType(10));
    BOOST_CHECK(!rateLimiterManager->isP2pBasicMsgType(0xff));

    {
        // modules_without_bw_limit=raft,pbft,txs_sync,amop
        BOOST_CHECK(rateLimiterManager->modulesWithoutLimit().at(bcos::protocol::ModuleID::PBFT));
        BOOST_CHECK(rateLimiterManager->modulesWithoutLimit().at(bcos::protocol::ModuleID::Raft));
        BOOST_CHECK(
            rateLimiterManager->modulesWithoutLimit().at(bcos::protocol::ModuleID::TxsSync));
        BOOST_CHECK(rateLimiterManager->modulesWithoutLimit().at(bcos::protocol::ModuleID::AMOP));
        BOOST_CHECK(!rateLimiterManager->modulesWithoutLimit().at(
            bcos::protocol::ModuleID::LIGHTNODE_GET_ABI));
        BOOST_CHECK(
            !rateLimiterManager->modulesWithoutLimit().at(bcos::protocol::ModuleID::BlockSync));
        BOOST_CHECK(!rateLimiterManager->modulesWithoutLimit().at(0xff));
    }

    {
        /*
        ; default bandwidth limit for the group
    group_outgoing_bw_limit=5
    ; specify group to limit bandwidth, group_groupName=n
       group_outgoing_bw_limit_group0=2.0
       group_outgoing_bw_limit_group1 = 2.0
       group_outgoing_bw_limit_group2= 2.0
       */

        BOOST_CHECK_EQUAL(
            config->rateLimiterConfig().groupOutgoingBwLimit, config->doubleMBToBit(5));
        BOOST_CHECK_EQUAL(config->rateLimiterConfig().group2BwLimit.find("group0")->second,
            config->doubleMBToBit(2));
        BOOST_CHECK_EQUAL(config->rateLimiterConfig().group2BwLimit.find("group1")->second,
            config->doubleMBToBit(2));
        BOOST_CHECK_EQUAL(config->rateLimiterConfig().group2BwLimit.find("group2")->second,
            config->doubleMBToBit(2));
        BOOST_CHECK(config->rateLimiterConfig().group2BwLimit.find("group3") ==
                    config->rateLimiterConfig().group2BwLimit.end());

        BOOST_CHECK(rateLimiterManager->getGroupRateLimiter("group0") != nullptr);
        BOOST_CHECK(rateLimiterManager->getGroupRateLimiter("group1") != nullptr);
        BOOST_CHECK(rateLimiterManager->getGroupRateLimiter("group2") != nullptr);
        BOOST_CHECK(rateLimiterManager->getGroupRateLimiter("group3") != nullptr);
        BOOST_CHECK(rateLimiterManager->getGroupRateLimiter("group4") != nullptr);

        BOOST_CHECK(rateLimiterManager->removeRateLimiter("group3"));
        BOOST_CHECK(!rateLimiterManager->removeRateLimiter("group3"));
        BOOST_CHECK(rateLimiterManager->removeRateLimiter("group2"));
        BOOST_CHECK(!rateLimiterManager->removeRateLimiter("group2"));
        BOOST_CHECK(rateLimiterManager->getGroupRateLimiter("group2") != nullptr);
        BOOST_CHECK(
            !rateLimiterManager
                 ->registerRateLimiter("group2", rateLimiterFactory->buildTimeWindowRateLimiter(10))
                 .first);
        BOOST_CHECK(rateLimiterManager->getGroupRateLimiter("group2") != nullptr);
        BOOST_CHECK(rateLimiterManager->removeRateLimiter("group2"));
        BOOST_CHECK(rateLimiterManager->getGroupRateLimiter("group2") != nullptr);

        {
            auto rateLimiterManager =
                gatewayFactory->buildRateLimiterManager(rateLimiterConfig, nullptr);

            BOOST_CHECK(rateLimiterConfig.enableOutConnRateLimit());
            BOOST_CHECK(rateLimiterConfig.enableOutGroupRateLimit());
            BOOST_CHECK(rateLimiterConfig.enableDistributedRatelimit);
            BOOST_CHECK(rateLimiterConfig.enableDistributedRateLimitCache);

            auto distributedRateLimiter0 =
                std::dynamic_pointer_cast<bcos::ratelimiter::DistributedRateLimiter>(
                    rateLimiterManager->getGroupRateLimiter("group0"));

            BOOST_CHECK_EQUAL(
                distributedRateLimiter0->rateLimitKey(), rateLimiterFactory->toTokenKey("group0"));
            BOOST_CHECK_EQUAL(distributedRateLimiter0->enableLocalCache(), true);
            BOOST_CHECK_EQUAL(distributedRateLimiter0->localCachePercent(), 13);
            BOOST_CHECK_EQUAL(distributedRateLimiter0->intervalSec(), 3);
            BOOST_CHECK_EQUAL(distributedRateLimiter0->maxPermitsSize(),
                timeWindowSec * config->doubleMBToBit(2));

            auto distributedRateLimiter1 =
                std::dynamic_pointer_cast<bcos::ratelimiter::DistributedRateLimiter>(
                    rateLimiterManager->getGroupRateLimiter("group1"));

            BOOST_CHECK_EQUAL(
                distributedRateLimiter1->rateLimitKey(), rateLimiterFactory->toTokenKey("group1"));
            BOOST_CHECK_EQUAL(distributedRateLimiter1->enableLocalCache(), true);
            BOOST_CHECK_EQUAL(distributedRateLimiter1->localCachePercent(), 13);
            BOOST_CHECK_EQUAL(distributedRateLimiter1->intervalSec(), 3);
            BOOST_CHECK_EQUAL(distributedRateLimiter1->maxPermitsSize(),
                timeWindowSec * config->doubleMBToBit(2));

            auto distributedRateLimiter2 =
                std::dynamic_pointer_cast<bcos::ratelimiter::DistributedRateLimiter>(
                    rateLimiterManager->getGroupRateLimiter("group3"));

            BOOST_CHECK_EQUAL(
                distributedRateLimiter2->rateLimitKey(), rateLimiterFactory->toTokenKey("group3"));
            BOOST_CHECK_EQUAL(distributedRateLimiter2->enableLocalCache(), true);
            BOOST_CHECK_EQUAL(distributedRateLimiter2->localCachePercent(), 13);
            BOOST_CHECK_EQUAL(distributedRateLimiter2->intervalSec(), 3);
            BOOST_CHECK_EQUAL(distributedRateLimiter2->maxPermitsSize(),
                timeWindowSec * config->doubleMBToBit(5));
        }
    }

    {
        /*
        conn_outgoing_bw_limit=2
    ; specify IP to limit bandwidth, format: conn_outgoing_bw_limit_x.x.x.x=n
       conn_outgoing_bw_limit_192.108.0.1=1.0
       conn_outgoing_bw_limit_192.108.0.2 =2.0
       conn_outgoing_bw_limit_192.108.0.3= 3.0
       */

        BOOST_CHECK_EQUAL(config->rateLimiterConfig().connOutgoingBwLimit, 2 * 1024 * 1024 / 8);
        BOOST_CHECK_EQUAL(config->rateLimiterConfig().ip2BwLimit.find("192.108.0.1")->second,
            config->doubleMBToBit(1));
        BOOST_CHECK_EQUAL(config->rateLimiterConfig().ip2BwLimit.find("192.108.0.2")->second,
            config->doubleMBToBit(2));
        BOOST_CHECK_EQUAL(config->rateLimiterConfig().ip2BwLimit.find("192.108.0.3")->second,
            config->doubleMBToBit(3));
        BOOST_CHECK(config->rateLimiterConfig().ip2BwLimit.find("192.108.0.0") ==
                    config->rateLimiterConfig().ip2BwLimit.end());

        BOOST_CHECK(rateLimiterManager->getConnRateLimiter("192.108.0.1") != nullptr);
        BOOST_CHECK(rateLimiterManager->getConnRateLimiter("192.108.0.2") != nullptr);
        BOOST_CHECK(rateLimiterManager->getConnRateLimiter("192.108.0.3") != nullptr);
        BOOST_CHECK(rateLimiterManager->getConnRateLimiter("192.108.0.0") != nullptr);

        BOOST_CHECK(!rateLimiterManager
                         ->registerRateLimiter(
                             "192.108.0.0", rateLimiterFactory->buildTimeWindowRateLimiter(10))
                         .first);
        BOOST_CHECK(rateLimiterManager->getConnRateLimiter("192.108.0.0") != nullptr);

        BOOST_CHECK(!rateLimiterManager
                         ->registerRateLimiter(
                             "192.108.0.0", rateLimiterFactory->buildTimeWindowRateLimiter(10))
                         .first);

        BOOST_CHECK(rateLimiterManager->removeRateLimiter("192.108.0.2"));
        BOOST_CHECK(rateLimiterManager->getConnRateLimiter("192.108.0.2") != nullptr);
        BOOST_CHECK(rateLimiterManager->removeRateLimiter("192.108.0.2"));

        BOOST_CHECK(rateLimiterManager
                        ->registerRateLimiter(
                            "192.108.0.2", rateLimiterFactory->buildTimeWindowRateLimiter(10))
                        .first);
        BOOST_CHECK(rateLimiterManager->getConnRateLimiter("192.108.0.2") != nullptr);

        {
            // rate limiter factory
            auto rateLimiterManager =
                gatewayFactory->buildRateLimiterManager(rateLimiterConfig, nullptr);

            BOOST_CHECK(rateLimiterConfig.enableOutGroupRateLimit());
            BOOST_CHECK(rateLimiterConfig.enableOutConnRateLimit());
            BOOST_CHECK(rateLimiterConfig.enableDistributedRatelimit);
            BOOST_CHECK(rateLimiterConfig.enableDistributedRateLimitCache);

            auto rateLimiter0 = std::dynamic_pointer_cast<bcos::ratelimiter::TimeWindowRateLimiter>(
                rateLimiterManager->getConnRateLimiter("192.108.0.1"));
            BOOST_CHECK_EQUAL(
                rateLimiter0->maxPermitsSize(), timeWindowSec * config->doubleMBToBit(1));

            auto rateLimiter1 = std::dynamic_pointer_cast<bcos::ratelimiter::TimeWindowRateLimiter>(
                rateLimiterManager->getConnRateLimiter("192.108.0.2"));

            BOOST_CHECK_EQUAL(
                rateLimiter1->maxPermitsSize(), timeWindowSec * config->doubleMBToBit(2));

            auto rateLimiter2 = std::dynamic_pointer_cast<bcos::ratelimiter::TimeWindowRateLimiter>(
                rateLimiterManager->getConnRateLimiter("192.108.0.3"));

            BOOST_CHECK_EQUAL(
                rateLimiter2->maxPermitsSize(), timeWindowSec * config->doubleMBToBit(3));

            auto rateLimiter3 = std::dynamic_pointer_cast<bcos::ratelimiter::TimeWindowRateLimiter>(
                rateLimiterManager->getConnRateLimiter("192.108.0.4"));

            BOOST_CHECK_EQUAL(
                rateLimiter3->maxPermitsSize(), timeWindowSec * config->doubleMBToBit(2));
        }
    }

    {
        /*
        incoming_p2p_basic_msg_type_list=1,2,3,6,9
        incoming_p2p_basic_msg_type_qps_limit=123
        incoming_module_msg_type_qps_limit=555
            incoming_module_qps_limit_1=123
            incoming_module_qps_limit_5=456
            incoming_module_qps_limit_7=789
        */

        BOOST_CHECK_EQUAL(config->rateLimiterConfig().p2pBasicMsgTypes.size(), 5);
        BOOST_CHECK_EQUAL(config->rateLimiterConfig().p2pBasicMsgQPS, 123);
        BOOST_CHECK_EQUAL(config->rateLimiterConfig().p2pModuleMsgQPS, 555);
        BOOST_CHECK_EQUAL(config->rateLimiterConfig().moduleMsg2QPSSize, 3);
        BOOST_CHECK(config->rateLimiterConfig().p2pBasicMsgTypes.contains(1));
        BOOST_CHECK(config->rateLimiterConfig().p2pBasicMsgTypes.contains(2));
        BOOST_CHECK(config->rateLimiterConfig().p2pBasicMsgTypes.contains(3));
        BOOST_CHECK(config->rateLimiterConfig().p2pBasicMsgTypes.contains(6));
        BOOST_CHECK(config->rateLimiterConfig().p2pBasicMsgTypes.contains(9));
        BOOST_CHECK(!config->rateLimiterConfig().p2pBasicMsgTypes.contains(10));
        BOOST_CHECK(config->rateLimiterConfig().moduleMsg2QPS.at(1));
        BOOST_CHECK(config->rateLimiterConfig().moduleMsg2QPS.at(5));
        BOOST_CHECK(config->rateLimiterConfig().moduleMsg2QPS.at(7));

        std::string groupID = "groupID";
        uint16_t packageType = 1;
        uint16_t moduleID = 1;
        std::string endpoint = "192.108.0.3";
        auto timeWindowSec = rateLimiterConfig.timeWindowSec;

        const std::string& inKey = endpoint + "_" + std::to_string(packageType);

        BOOST_CHECK(!rateLimiterManager->removeRateLimiter(inKey));
        BOOST_CHECK(rateLimiterManager->getInRateLimiter(endpoint, 1) != nullptr);
        BOOST_CHECK(rateLimiterManager->removeRateLimiter(inKey));

        BOOST_CHECK(rateLimiterManager->getInRateLimiter(endpoint, 2) != nullptr);
        BOOST_CHECK(rateLimiterManager->getInRateLimiter(endpoint, 3) != nullptr);
        BOOST_CHECK(rateLimiterManager->getInRateLimiter(endpoint, 6) != nullptr);
        BOOST_CHECK(rateLimiterManager->getInRateLimiter(endpoint, 9) != nullptr);
        BOOST_CHECK(rateLimiterManager->getInRateLimiter(endpoint, 1) != nullptr);
        BOOST_CHECK(rateLimiterManager->getInRateLimiter(endpoint, 10) == nullptr);

        const std::string& inKey1 = groupID + "_" + std::to_string(moduleID);
        BOOST_CHECK(rateLimiterManager->getInRateLimiter(groupID, 1, true) != nullptr);
        BOOST_CHECK(rateLimiterManager->removeRateLimiter(inKey1));
        BOOST_CHECK(rateLimiterManager->getInRateLimiter(groupID, 1, true) != nullptr);
        BOOST_CHECK(rateLimiterManager->removeRateLimiter(inKey1));
        BOOST_CHECK(rateLimiterManager->getInRateLimiter(groupID, 1, true) != nullptr);

        BOOST_CHECK(rateLimiterManager->getInRateLimiter(groupID, 5, true) != nullptr);
        BOOST_CHECK(rateLimiterManager->getInRateLimiter(groupID, 7, true) != nullptr);
        BOOST_CHECK(rateLimiterManager->getInRateLimiter(groupID, 9, true) != nullptr);

        {
            // rate limiter factory
            auto rateLimiterManager =
                gatewayFactory->buildRateLimiterManager(rateLimiterConfig, nullptr);

            BOOST_CHECK(rateLimiterConfig.allowExceedMaxPermitSize);
            BOOST_CHECK(rateLimiterConfig.enableOutGroupRateLimit());
            BOOST_CHECK(rateLimiterConfig.enableOutConnRateLimit());
            BOOST_CHECK(rateLimiterConfig.enableDistributedRatelimit);
            BOOST_CHECK(rateLimiterConfig.enableDistributedRateLimitCache);

            BOOST_CHECK(rateLimiterConfig.enableInRateLimit());
            BOOST_CHECK(rateLimiterConfig.enableInP2pBasicMsgLimit());
            // BOOST_CHECK(rateLimiterConfig.enableInP2pModuleMsgLimit(1));


            {
                uint16_t packageType = 10;
                std::string endpoint = "192.108.0.3";

                auto rateLimiter = rateLimiterManager->getInRateLimiter(endpoint, packageType);
                BOOST_CHECK(rateLimiter == nullptr);
            }

            {
                uint16_t packageType = 1;
                std::string endpoint = "192.108.0.3";

                const std::string& inKey = endpoint + "_" + std::to_string(packageType);
                auto rateLimiter = std::dynamic_pointer_cast<bcos::ratelimiter::TimeWindowRateLimiter>(
                    rateLimiterManager->getInRateLimiter(endpoint, packageType));
                BOOST_CHECK_EQUAL(rateLimiter->maxPermitsSize(),
                    timeWindowSec * rateLimiterConfig.p2pBasicMsgQPS);
                BOOST_CHECK_EQUAL(rateLimiter->maxPermitsSize(), timeWindowSec * 123);
            }

            {
                uint16_t module = 10;
                std::string group = "group0";

                const std::string& inKey = group + "_" + std::to_string(module);
                auto rateLimiter = std::dynamic_pointer_cast<bcos::ratelimiter::DistributedRateLimiter>(
                    rateLimiterManager->getInRateLimiter(endpoint, packageType, true));
                BOOST_CHECK(rateLimiter == nullptr);
            }

            {
                uint16_t module = 5;
                std::string group = "group0";

                BOOST_CHECK(rateLimiterConfig.moduleMsg2QPS.at(module));

                const std::string& inKey = group + "_" + std::to_string(module);
                auto rateLimiter = std::dynamic_pointer_cast<bcos::ratelimiter::DistributedRateLimiter>(
                    rateLimiterManager->getInRateLimiter(group, module, true));
                BOOST_CHECK_EQUAL(rateLimiter->maxPermitsSize(), timeWindowSec * 456);
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(test_rateLimiterManagerConfigIPv6)
{
    std::string configIni("data/config/config_ipv6.ini");

    boost::property_tree::ptree pt;
    boost::property_tree::ini_parser::read_ini(configIni, pt);

    auto config = std::make_shared<GatewayConfig>();
    config->initFlowControlConfig(pt);

    auto rateLimiterConfig = config->rateLimiterConfig();
    auto gatewayFactory = std::make_shared<GatewayFactory>("", "");
    auto rateLimiterManager = gatewayFactory->buildRateLimiterManager(rateLimiterConfig, nullptr);

    auto rateLimiterFactory = rateLimiterManager->rateLimiterFactory();

    BOOST_CHECK(rateLimiterFactory != nullptr);

    BOOST_CHECK(rateLimiterConfig.totalOutgoingBwLimit > 0);
    BOOST_CHECK(rateLimiterConfig.connOutgoingBwLimit > 0);
    BOOST_CHECK(rateLimiterConfig.groupOutgoingBwLimit > 0);

    BOOST_CHECK(rateLimiterConfig.enableOutRateLimit());
    BOOST_CHECK(rateLimiterConfig.enableOutConnRateLimit());
    BOOST_CHECK(rateLimiterConfig.enableOutGroupRateLimit());
    BOOST_CHECK(rateLimiterConfig.enableInRateLimit());
    BOOST_CHECK(rateLimiterConfig.enableInP2pBasicMsgLimit());
    BOOST_CHECK(rateLimiterConfig.enableInRateLimit());
    BOOST_CHECK(rateLimiterConfig.enableInP2pModuleMsgLimit(1));
    BOOST_CHECK(!rateLimiterConfig.enableDistributedRatelimit);
    BOOST_CHECK(!rateLimiterConfig.allowExceedMaxPermitSize);
    BOOST_CHECK_EQUAL(rateLimiterConfig.enableDistributedRateLimitCache, true);
    BOOST_CHECK_EQUAL(rateLimiterConfig.distributedRateLimitCachePercent, 20);
    BOOST_CHECK_EQUAL(rateLimiterConfig.p2pBasicMsgTypes.size(), 3);
    BOOST_CHECK_EQUAL(rateLimiterConfig.p2pBasicMsgTypes.contains(2), true);
    BOOST_CHECK_EQUAL(rateLimiterConfig.p2pBasicMsgQPS, 666);
    BOOST_CHECK_EQUAL(rateLimiterConfig.p2pModuleMsgQPS, 999);

    {
        auto tokenBucketRateLimiter0 =
            std::dynamic_pointer_cast<bcos::ratelimiter::TimeWindowRateLimiter>(
                rateLimiterManager->getGroupRateLimiter("group0"));

        BOOST_CHECK_EQUAL(tokenBucketRateLimiter0->maxPermitsSize(),
            config->rateLimiterConfig().timeWindowSec * config->doubleMBToBit(1));

        auto tokenBucketRateLimiter1 =
            std::dynamic_pointer_cast<bcos::ratelimiter::TimeWindowRateLimiter>(
                rateLimiterManager->getConnRateLimiter("127.0.0.1"));

        BOOST_CHECK_EQUAL(tokenBucketRateLimiter1->maxPermitsSize(),
            config->rateLimiterConfig().timeWindowSec * config->doubleMBToBit(2));

        auto tokenBucketRateLimiter2 =
            std::dynamic_pointer_cast<bcos::ratelimiter::TimeWindowRateLimiter>(
                rateLimiterManager->getRateLimiter(
                    bcos::gateway::ratelimiter::RateLimiterManager::TOTAL_OUTGOING_KEY));

        BOOST_CHECK_EQUAL(tokenBucketRateLimiter2->maxPermitsSize(),
            config->rateLimiterConfig().timeWindowSec * config->doubleMBToBit(3));
    }

    {
        std::string groupID = "groupID";
        uint16_t packageType = 2;
        uint16_t moduleID = 1;
        std::string endpoint = "192.108.0.3";
        auto timeWindowSec = rateLimiterConfig.timeWindowSec;

        auto rateLimiter1 = std::dynamic_pointer_cast<bcos::ratelimiter::TimeWindowRateLimiter>(
            rateLimiterManager->getInRateLimiter(endpoint, packageType));
        BOOST_CHECK_EQUAL(rateLimiter1->maxPermitsSize(), 666 * timeWindowSec);
        BOOST_CHECK(!rateLimiterManager->getInRateLimiter(endpoint, 1));

        auto ratelimiter2 = std::dynamic_pointer_cast<bcos::ratelimiter::TimeWindowRateLimiter>(
            rateLimiterManager->getInRateLimiter(groupID, 111, true));
        BOOST_CHECK_EQUAL(ratelimiter2->maxPermitsSize(), 999 * timeWindowSec);
    }
}

BOOST_AUTO_TEST_SUITE_END()
