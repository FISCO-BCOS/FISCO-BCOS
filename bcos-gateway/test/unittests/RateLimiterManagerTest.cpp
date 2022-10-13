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
#include <bcos-gateway/GatewayFactory.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace gateway;
using namespace bcos::test;

BOOST_FIXTURE_TEST_SUITE(RateLimiterManagerTest, TestPromptFixture)

BOOST_AUTO_TEST_CASE(test_rateLimiterManager)
{
    auto gatewayFactory = std::make_shared<GatewayFactory>("", "");
    bcos::gateway::GatewayConfig::RateLimiterConfig rateLimiterConfig;
    bcos::gateway::GatewayConfig::RedisConfig redisConfig;
    auto rateLimiterManager =
        gatewayFactory->buildRateLimiterManager(rateLimiterConfig, redisConfig);
    auto rateLimiterFactory = rateLimiterManager->rateLimiterFactory();

    BOOST_CHECK(!rateLimiterConfig.hasRateLimiterConfigEffect());
    BOOST_CHECK(!rateLimiterConfig.conRateLimitOn);
    BOOST_CHECK(!rateLimiterConfig.groupRateLimitOn);
    BOOST_CHECK(!rateLimiterConfig.distributedRateLimitOn);

    BOOST_CHECK(rateLimiterManager->getRateLimiter("192.108.0.0") == nullptr);

    BOOST_CHECK(rateLimiterManager->registerRateLimiter(
        "192.108.0.0", rateLimiterFactory->buildTokenBucketRateLimiter(10)));
    BOOST_CHECK(!rateLimiterManager->registerRateLimiter(
        "192.108.0.0", rateLimiterFactory->buildTokenBucketRateLimiter(10)));

    BOOST_CHECK(rateLimiterManager->getRateLimiter("192.108.0.0") != nullptr);
    BOOST_CHECK(rateLimiterManager->removeRateLimiter("192.108.0.0"));
    BOOST_CHECK(!rateLimiterManager->removeRateLimiter("192.108.0.0"));

    BOOST_CHECK(rateLimiterManager->getRateLimiter("192.108.0.0") == nullptr);
}

BOOST_AUTO_TEST_CASE(test_rateLimiterManagerDefaultConfig)
{
    auto gatewayFactory = std::make_shared<GatewayFactory>("", "");

    bcos::gateway::GatewayConfig::RateLimiterConfig rateLimiterConfig;
    bcos::gateway::GatewayConfig::RedisConfig redisConfig;
    auto rateLimiterManager =
        gatewayFactory->buildRateLimiterManager(rateLimiterConfig, redisConfig);

    BOOST_CHECK(!rateLimiterConfig.hasRateLimiterConfigEffect());
    BOOST_CHECK(!rateLimiterConfig.conRateLimitOn);
    BOOST_CHECK(!rateLimiterConfig.groupRateLimitOn);
    BOOST_CHECK(!rateLimiterConfig.distributedRateLimitOn);

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

    BOOST_CHECK(config->rateLimiterConfig().hasRateLimiterConfigEffect());
    BOOST_CHECK(config->rateLimiterConfig().conRateLimitOn);
    BOOST_CHECK(config->rateLimiterConfig().groupRateLimitOn);
    BOOST_CHECK(!config->rateLimiterConfig().distributedRateLimitOn);

    auto rateLimiterConfig = config->rateLimiterConfig();
    auto gatewayFactory = std::make_shared<GatewayFactory>("", "");
    auto rateLimiterManager =
        gatewayFactory->buildRateLimiterManager(rateLimiterConfig, config->redisConfig());

    auto rateLimiterFactory = rateLimiterManager->rateLimiterFactory();

    BOOST_CHECK(rateLimiterFactory != nullptr);

    {
        /*
        group_outgoing_bw_limit=5
    ; specify group to limit bandwidth, group_groupName=n
       group_group0=2.0
       group_group1 = 2.0
       group_group2= 2.0
       */
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
    }

    {
        /*
        conn_outgoing_bw_limit=2
    ; specify IP to limit bandwidth, format: ip_x.x.x.x=n
       ip_192.108.0.1=1.0
       ip_192.108.0.2 =2.0
       ip_192.108.0.3= 3.0
       */
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
    auto rateLimiterManager =
        gatewayFactory->buildRateLimiterManager(rateLimiterConfig, config->redisConfig());

    auto rateLimiterFactory = rateLimiterManager->rateLimiterFactory();

    BOOST_CHECK(rateLimiterFactory != nullptr);

    BOOST_CHECK(rateLimiterConfig.totalOutgoingBwLimit > 0);
    BOOST_CHECK(rateLimiterConfig.connOutgoingBwLimit > 0);
    BOOST_CHECK(rateLimiterConfig.groupOutgoingBwLimit > 0);

    BOOST_CHECK(rateLimiterConfig.hasRateLimiterConfigEffect());
    BOOST_CHECK(rateLimiterConfig.conRateLimitOn);
    BOOST_CHECK(rateLimiterConfig.groupRateLimitOn);
    BOOST_CHECK(!rateLimiterConfig.distributedRateLimitOn);
}


BOOST_AUTO_TEST_SUITE_END()
