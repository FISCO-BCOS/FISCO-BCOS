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
#include "bcos-gateway/libratelimit/DistributedRateLimiter.h"
#include "bcos-gateway/libratelimit/RateLimiterFactory.h"
#include "bcos-gateway/libratelimit/TokenBucketRateLimiter.h"
#include <bcos-gateway/GatewayConfig.h>
#include <bcos-gateway/GatewayFactory.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/filesystem.hpp>
#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>
#include <memory>

using namespace bcos;
using namespace gateway;
using namespace bcos::test;

BOOST_FIXTURE_TEST_SUITE(RateLimiterManagerTest, TestPromptFixture)

BOOST_AUTO_TEST_CASE(test_rateLimiterManager)
{
    auto gatewayFactory = std::make_shared<GatewayFactory>("", "");
    bcos::gateway::GatewayConfig::RateLimiterConfig rateLimiterConfig;
    bcos::gateway::GatewayConfig::RedisConfig redisConfig;
    auto rateLimiterManager = gatewayFactory->buildRateLimiterManager(rateLimiterConfig, nullptr);
    auto rateLimiterFactory = rateLimiterManager->rateLimiterFactory();

    BOOST_CHECK(!rateLimiterConfig.enableRateLimit());
    BOOST_CHECK(!rateLimiterConfig.enableDistributedRatelimit);
    BOOST_CHECK(rateLimiterConfig.enableDistributedRateLimitCache);

    BOOST_CHECK(!rateLimiterConfig.enableConRateLimit);
    BOOST_CHECK(!rateLimiterConfig.enableGroupRateLimit);
    BOOST_CHECK(!rateLimiterConfig.enableDistributedRatelimit);

    BOOST_CHECK(rateLimiterManager->getRateLimiter("192.108.0.0") == nullptr);

    BOOST_CHECK(rateLimiterManager->registerRateLimiter(
        "192.108.0.0", rateLimiterFactory->buildTokenBucketRateLimiter(10)));
    BOOST_CHECK(!rateLimiterManager->registerRateLimiter(
        "192.108.0.0", rateLimiterFactory->buildTokenBucketRateLimiter(10)));

    BOOST_CHECK(rateLimiterManager->getRateLimiter("192.108.0.0") != nullptr);
    BOOST_CHECK(rateLimiterManager->removeRateLimiter("192.108.0.0"));
    BOOST_CHECK(!rateLimiterManager->removeRateLimiter("192.108.0.0"));

    BOOST_CHECK(rateLimiterManager->getRateLimiter("192.108.0.0") == nullptr);

    BOOST_CHECK(rateLimiterManager->registerRateLimiter(
        "192.108.0.0", rateLimiterFactory->buildTokenBucketRateLimiter(10)));
    BOOST_CHECK(rateLimiterManager->getRateLimiter("192.108.0.0") != nullptr);
}

BOOST_AUTO_TEST_CASE(test_rateLimiterManagerDefaultConfig)
{
    auto gatewayFactory = std::make_shared<GatewayFactory>("", "");

    bcos::gateway::GatewayConfig::RateLimiterConfig rateLimiterConfig;
    auto rateLimiterManager = gatewayFactory->buildRateLimiterManager(rateLimiterConfig, nullptr);

    BOOST_CHECK(!rateLimiterConfig.enableRateLimit());
    BOOST_CHECK(!rateLimiterConfig.enableConRateLimit);
    BOOST_CHECK(!rateLimiterConfig.enableGroupRateLimit);
    BOOST_CHECK(!rateLimiterConfig.enableDistributedRatelimit);
    BOOST_CHECK(rateLimiterConfig.enableDistributedRateLimitCache);

    BOOST_CHECK(rateLimiterManager->getRateLimiter(
                    bcos::gateway::ratelimiter::RateLimiterManager::TOTAL_OUTGOING_KEY) == nullptr);

    BOOST_CHECK(rateLimiterManager->getGroupRateLimiter("group0") == nullptr);
    BOOST_CHECK(!rateLimiterManager->removeRateLimiter(("group0")));

    BOOST_CHECK(rateLimiterManager->getConnRateLimiter("192.108.0.1") == nullptr);
    BOOST_CHECK(!rateLimiterManager->removeRateLimiter("192.108.0.1"));
}

BOOST_AUTO_TEST_CASE(test_rateLimiterManagerConfigIPv4)
{
    std::string configIni("data/config/config_ipv4.ini");

    boost::property_tree::ptree pt;
    boost::property_tree::ini_parser::read_ini(configIni, pt);

    auto config = std::make_shared<GatewayConfig>();
    config->initRateLimitConfig(pt);

    BOOST_CHECK(config->rateLimiterConfig().enableRateLimit());
    BOOST_CHECK(config->rateLimiterConfig().enableConRateLimit);
    BOOST_CHECK(config->rateLimiterConfig().enableGroupRateLimit);
    BOOST_CHECK(config->rateLimiterConfig().enableDistributedRatelimit);
    BOOST_CHECK(config->rateLimiterConfig().enableDistributedRateLimitCache);
    BOOST_CHECK_EQUAL(config->rateLimiterConfig().distributedRateLimitCachePercent, 13);

    auto rateLimiterConfig = config->rateLimiterConfig();
    auto gatewayFactory = std::make_shared<GatewayFactory>("", "");
    auto rateLimiterManager = gatewayFactory->buildRateLimiterManager(rateLimiterConfig, nullptr);

    auto rateLimiterFactory = rateLimiterManager->rateLimiterFactory();

    BOOST_CHECK(rateLimiterFactory != nullptr);

    {
        /*
        ; default bandwidth limit for the group
    group_outgoing_bw_limit=5
    ; specify group to limit bandwidth, group_groupName=n
       group_outgoing_bw_limit_group0=2.0
       group_outgoing_bw_limit_group1 = 2.0
       group_outgoing_bw_limit_group2= 2.0
       */

        BOOST_CHECK_EQUAL(config->rateLimiterConfig().groupOutgoingBwLimit, 5 * 1024 * 1024 / 8);
        BOOST_CHECK_EQUAL(
            config->rateLimiterConfig().group2BwLimit.find("group0")->second, 2 * 1024 * 1024 / 8);
        BOOST_CHECK_EQUAL(
            config->rateLimiterConfig().group2BwLimit.find("group1")->second, 2 * 1024 * 1024 / 8);
        BOOST_CHECK_EQUAL(
            config->rateLimiterConfig().group2BwLimit.find("group2")->second, 2 * 1024 * 1024 / 8);
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
        BOOST_CHECK(!rateLimiterManager->registerRateLimiter(
            "group2", rateLimiterFactory->buildTokenBucketRateLimiter(10)));
        BOOST_CHECK(rateLimiterManager->getGroupRateLimiter("group2") != nullptr);
        BOOST_CHECK(rateLimiterManager->removeRateLimiter("group2"));
        BOOST_CHECK(rateLimiterManager->getGroupRateLimiter("group2") != nullptr);

        {
            auto rateLimiterManager =
                gatewayFactory->buildRateLimiterManager(rateLimiterConfig, nullptr);

            BOOST_CHECK(rateLimiterConfig.enableGroupRateLimit);
            BOOST_CHECK(rateLimiterConfig.enableConRateLimit);
            BOOST_CHECK(rateLimiterConfig.enableDistributedRatelimit);
            BOOST_CHECK(rateLimiterConfig.enableDistributedRateLimitCache);

            auto distributedRateLimiter0 =
                std::dynamic_pointer_cast<ratelimiter::DistributedRateLimiter>(
                    rateLimiterManager->getGroupRateLimiter("group0"));

            BOOST_CHECK_EQUAL(
                distributedRateLimiter0->rateLimitKey(), rateLimiterFactory->toTokenKey("group0"));
            BOOST_CHECK_EQUAL(distributedRateLimiter0->enableLocalCache(), true);
            BOOST_CHECK_EQUAL(distributedRateLimiter0->localCachePercent(), 13);
            BOOST_CHECK_EQUAL(distributedRateLimiter0->interval(), 1);
            BOOST_CHECK_EQUAL(distributedRateLimiter0->maxPermits(), 2 * 1024 * 1024 / 8);

            auto distributedRateLimiter1 =
                std::dynamic_pointer_cast<ratelimiter::DistributedRateLimiter>(
                    rateLimiterManager->getGroupRateLimiter("group1"));

            BOOST_CHECK_EQUAL(
                distributedRateLimiter1->rateLimitKey(), rateLimiterFactory->toTokenKey("group1"));
            BOOST_CHECK_EQUAL(distributedRateLimiter1->enableLocalCache(), true);
            BOOST_CHECK_EQUAL(distributedRateLimiter1->localCachePercent(), 13);
            BOOST_CHECK_EQUAL(distributedRateLimiter1->interval(), 1);
            BOOST_CHECK_EQUAL(distributedRateLimiter1->maxPermits(), 2 * 1024 * 1024 / 8);

            auto distributedRateLimiter2 =
                std::dynamic_pointer_cast<ratelimiter::DistributedRateLimiter>(
                    rateLimiterManager->getGroupRateLimiter("group3"));

            BOOST_CHECK_EQUAL(
                distributedRateLimiter2->rateLimitKey(), rateLimiterFactory->toTokenKey("group3"));
            BOOST_CHECK_EQUAL(distributedRateLimiter2->enableLocalCache(), true);
            BOOST_CHECK_EQUAL(distributedRateLimiter2->localCachePercent(), 13);
            BOOST_CHECK_EQUAL(distributedRateLimiter2->interval(), 1);
            BOOST_CHECK_EQUAL(distributedRateLimiter2->maxPermits(), 5 * 1024 * 1024 / 8);
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
            1 * 1024 * 1024 / 8);
        BOOST_CHECK_EQUAL(config->rateLimiterConfig().ip2BwLimit.find("192.108.0.2")->second,
            2 * 1024 * 1024 / 8);
        BOOST_CHECK_EQUAL(config->rateLimiterConfig().ip2BwLimit.find("192.108.0.3")->second,
            3 * 1024 * 1024 / 8);
        BOOST_CHECK(config->rateLimiterConfig().ip2BwLimit.find("192.108.0.0") ==
                    config->rateLimiterConfig().ip2BwLimit.end());

        BOOST_CHECK(rateLimiterManager->getConnRateLimiter("192.108.0.1") != nullptr);
        BOOST_CHECK(rateLimiterManager->getConnRateLimiter("192.108.0.2") != nullptr);
        BOOST_CHECK(rateLimiterManager->getConnRateLimiter("192.108.0.3") != nullptr);
        BOOST_CHECK(rateLimiterManager->getConnRateLimiter("192.108.0.0") != nullptr);

        BOOST_CHECK(!rateLimiterManager->registerRateLimiter(
            "192.108.0.0", rateLimiterFactory->buildTokenBucketRateLimiter(10)));
        BOOST_CHECK(rateLimiterManager->getConnRateLimiter("192.108.0.0") != nullptr);

        BOOST_CHECK(!rateLimiterManager->registerRateLimiter(
            "192.108.0.0", rateLimiterFactory->buildTokenBucketRateLimiter(10)));

        BOOST_CHECK(rateLimiterManager->removeRateLimiter("192.108.0.2"));
        BOOST_CHECK(rateLimiterManager->getConnRateLimiter("192.108.0.2") != nullptr);
        BOOST_CHECK(rateLimiterManager->removeRateLimiter("192.108.0.2"));

        BOOST_CHECK(rateLimiterManager->registerRateLimiter(
            "192.108.0.2", rateLimiterFactory->buildTokenBucketRateLimiter(10)));
        BOOST_CHECK(rateLimiterManager->getConnRateLimiter("192.108.0.2") != nullptr);

        {
            // rate limiter factory
            auto rateLimiterManager =
                gatewayFactory->buildRateLimiterManager(rateLimiterConfig, nullptr);

            BOOST_CHECK(rateLimiterConfig.enableGroupRateLimit);
            BOOST_CHECK(rateLimiterConfig.enableConRateLimit);
            BOOST_CHECK(rateLimiterConfig.enableDistributedRatelimit);
            BOOST_CHECK(rateLimiterConfig.enableDistributedRateLimitCache);

            auto rateLimiter0 = std::dynamic_pointer_cast<ratelimiter::TokenBucketRateLimiter>(
                rateLimiterManager->getConnRateLimiter("192.108.0.1"));

            BOOST_CHECK_EQUAL(rateLimiter0->maxQPS(), 1 * 1024 * 1024 / 8);

            auto rateLimiter1 = std::dynamic_pointer_cast<ratelimiter::TokenBucketRateLimiter>(
                rateLimiterManager->getConnRateLimiter("192.108.0.2"));

            BOOST_CHECK_EQUAL(rateLimiter1->maxQPS(), 2 * 1024 * 1024 / 8);

            auto rateLimiter2 = std::dynamic_pointer_cast<ratelimiter::TokenBucketRateLimiter>(
                rateLimiterManager->getConnRateLimiter("192.108.0.3"));

            BOOST_CHECK_EQUAL(rateLimiter2->maxQPS(), 3 * 1024 * 1024 / 8);

            auto rateLimiter3 = std::dynamic_pointer_cast<ratelimiter::TokenBucketRateLimiter>(
                rateLimiterManager->getConnRateLimiter("192.108.0.4"));

            BOOST_CHECK_EQUAL(rateLimiter3->maxQPS(), 2 * 1024 * 1024 / 8);
        }
    }
}

BOOST_AUTO_TEST_CASE(test_rateLimiterManagerConfigIPv6)
{
    std::string configIni("data/config/config_ipv6.ini");

    boost::property_tree::ptree pt;
    boost::property_tree::ini_parser::read_ini(configIni, pt);

    auto config = std::make_shared<GatewayConfig>();
    config->initRateLimitConfig(pt);

    auto rateLimiterConfig = config->rateLimiterConfig();
    auto gatewayFactory = std::make_shared<GatewayFactory>("", "");
    auto rateLimiterManager = gatewayFactory->buildRateLimiterManager(rateLimiterConfig, nullptr);

    auto rateLimiterFactory = rateLimiterManager->rateLimiterFactory();

    BOOST_CHECK(rateLimiterFactory != nullptr);

    BOOST_CHECK(rateLimiterConfig.totalOutgoingBwLimit > 0);
    BOOST_CHECK(rateLimiterConfig.connOutgoingBwLimit > 0);
    BOOST_CHECK(rateLimiterConfig.groupOutgoingBwLimit > 0);

    BOOST_CHECK(rateLimiterConfig.enableRateLimit());
    BOOST_CHECK(rateLimiterConfig.enableConRateLimit);
    BOOST_CHECK(rateLimiterConfig.enableGroupRateLimit);
    BOOST_CHECK(!rateLimiterConfig.enableDistributedRatelimit);
    BOOST_CHECK_EQUAL(rateLimiterConfig.enableDistributedRateLimitCache, true);
    BOOST_CHECK_EQUAL(rateLimiterConfig.distributedRateLimitCachePercent, 20);

    {
        // rate limiter manager
        auto rateLimiterManager =
            gatewayFactory->buildRateLimiterManager(rateLimiterConfig, nullptr);

        auto tokenBucketRateLimiter0 =
            std::dynamic_pointer_cast<ratelimiter::TokenBucketRateLimiter>(
                rateLimiterManager->getGroupRateLimiter("group0"));

        BOOST_CHECK_EQUAL(tokenBucketRateLimiter0->maxQPS(), 1024 * 1024 / 8);

        auto tokenBucketRateLimiter1 =
            std::dynamic_pointer_cast<ratelimiter::TokenBucketRateLimiter>(
                rateLimiterManager->getConnRateLimiter("127.0.0.1"));

        BOOST_CHECK_EQUAL(tokenBucketRateLimiter1->maxQPS(), 2 * 1024 * 1024 / 8);

        auto tokenBucketRateLimiter2 =
            std::dynamic_pointer_cast<ratelimiter::TokenBucketRateLimiter>(
                rateLimiterManager->getRateLimiter(
                    bcos::gateway::ratelimiter::RateLimiterManager::TOTAL_OUTGOING_KEY));

        BOOST_CHECK_EQUAL(tokenBucketRateLimiter2->maxQPS(), 3 * 1024 * 1024 / 8);
    }
}

BOOST_AUTO_TEST_SUITE_END()
