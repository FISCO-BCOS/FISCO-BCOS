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
    bcos::gateway::GatewayConfig::RateLimitConfig rateLimitConfig;
    auto rateLimiterManager = gatewayFactory->buildRateLimitManager(rateLimitConfig);
    auto rateLimiterFactory = rateLimiterManager->rateLimiterFactory();

    BOOST_CHECK(rateLimiterManager->getRateLimiter("192.108.0.0") == nullptr);

    BOOST_CHECK(rateLimiterManager->registerRateLimiter(
        "192.108.0.0", rateLimiterFactory->buildRateLimiter(10)));
    BOOST_CHECK(!rateLimiterManager->registerRateLimiter(
        "192.108.0.0", rateLimiterFactory->buildRateLimiter(10)));

    BOOST_CHECK(rateLimiterManager->getRateLimiter("192.108.0.0") != nullptr);
    BOOST_CHECK(rateLimiterManager->removeRateLimiter("192.108.0.0"));
    BOOST_CHECK(!rateLimiterManager->removeRateLimiter("192.108.0.0"));

    BOOST_CHECK(rateLimiterManager->getRateLimiter("192.108.0.0") == nullptr);

    BOOST_CHECK(rateLimiterManager->ensureRateLimiterExist("a", 111) != nullptr);
    BOOST_CHECK(rateLimiterManager->getRateLimiter("a") != nullptr);
    BOOST_CHECK(rateLimiterManager->ensureRateLimiterExist("a", 111) != nullptr);
}

BOOST_AUTO_TEST_CASE(test_rateLimiterManagerDefaultConfig)
{
    auto gatewayFactory = std::make_shared<GatewayFactory>("", "");

    bcos::gateway::GatewayConfig::RateLimitConfig rateLimitConfig;
    auto rateLimiterManager = gatewayFactory->buildRateLimitManager(rateLimitConfig);

    BOOST_CHECK(rateLimiterManager->getRateLimiter(
                    bcos::gateway::ratelimit::RateLimiterManager::TOTAL_OUTGOING_KEY) == nullptr);

    BOOST_CHECK(rateLimiterManager->getGroupRateLimiter("group0") == nullptr);
    BOOST_CHECK(!rateLimiterManager->removeGroupRateLimiter("group0"));

    BOOST_CHECK(rateLimiterManager->getConnRateLimiter("192.108.0.1") == nullptr);
    BOOST_CHECK(!rateLimiterManager->removeGroupRateLimiter("192.108.0.1"));
}

BOOST_AUTO_TEST_CASE(test_rateLimiterManagerConfigIPv4)
{
    std::string configIni("../../../bcos-gateway/test/unittests/data/config/config_ipv4.ini");

    boost::property_tree::ptree pt;
    boost::property_tree::ini_parser::read_ini(configIni, pt);

    auto config = std::make_shared<GatewayConfig>();
    config->initRatelimitConfig(pt);

    auto rateLimitConfig = config->rateLimitConfig();
    auto gatewayFactory = std::make_shared<GatewayFactory>("", "");
    auto rateLimiterManager = gatewayFactory->buildRateLimitManager(rateLimitConfig);

    auto rateLimiterFactory = rateLimiterManager->rateLimiterFactory();

    BOOST_CHECK(rateLimiterFactory != nullptr);

    {
        BOOST_CHECK(rateLimiterManager->getGroupRateLimiter("group0") != nullptr);
        BOOST_CHECK(rateLimiterManager->getGroupRateLimiter("group1") != nullptr);
        BOOST_CHECK(rateLimiterManager->getGroupRateLimiter("group2") != nullptr);
        BOOST_CHECK(rateLimiterManager->getGroupRateLimiter("group3") != nullptr);

        BOOST_CHECK(rateLimiterManager->removeGroupRateLimiter("group3"));
        BOOST_CHECK(!rateLimiterManager->removeGroupRateLimiter("group3"));
        BOOST_CHECK(rateLimiterManager->removeGroupRateLimiter("group2"));
        BOOST_CHECK(!rateLimiterManager->removeGroupRateLimiter("group2"));
        BOOST_CHECK(rateLimiterManager->getGroupRateLimiter("group2") != nullptr);
        BOOST_CHECK(!rateLimiterManager->registerGroupRateLimiter(
            "group2", rateLimiterFactory->buildRateLimiter(10)));
        BOOST_CHECK(rateLimiterManager->getGroupRateLimiter("group2") != nullptr);
    }

    {
        BOOST_CHECK(rateLimiterManager->getConnRateLimiter("192.108.0.1") != nullptr);
        BOOST_CHECK(rateLimiterManager->getConnRateLimiter("192.108.0.2") != nullptr);
        BOOST_CHECK(rateLimiterManager->getConnRateLimiter("192.108.0.3") != nullptr);
        BOOST_CHECK(rateLimiterManager->getConnRateLimiter("192.108.0.0") != nullptr);

        BOOST_CHECK(!rateLimiterManager->registerConnRateLimiter(
            "192.108.0.0", rateLimiterFactory->buildRateLimiter(10)));
        BOOST_CHECK(rateLimiterManager->getConnRateLimiter("192.108.0.0") != nullptr);

        BOOST_CHECK(!rateLimiterManager->registerConnRateLimiter(
            "192.108.0.0", rateLimiterFactory->buildRateLimiter(10)));

        BOOST_CHECK(rateLimiterManager->removeConnRateLimiter("192.108.0.2"));
        BOOST_CHECK(rateLimiterManager->getConnRateLimiter("192.108.0.2") != nullptr);
        BOOST_CHECK(rateLimiterManager->removeConnRateLimiter("192.108.0.2"));

        BOOST_CHECK(!rateLimiterManager->registerConnRateLimiter(
            "192.108.0.2", rateLimiterFactory->buildRateLimiter(10)));
        BOOST_CHECK(rateLimiterManager->getConnRateLimiter("192.108.0.2") != nullptr);
    }
}

BOOST_AUTO_TEST_CASE(test_rateLimiterManagerConfigIPv6)
{
    std::string configIni("../../../bcos-gateway/test/unittests/data/config/config_ipv6.ini");

    boost::property_tree::ptree pt;
    boost::property_tree::ini_parser::read_ini(configIni, pt);

    auto config = std::make_shared<GatewayConfig>();
    config->initRatelimitConfig(pt);

    auto rateLimitConfig = config->rateLimitConfig();
    auto gatewayFactory = std::make_shared<GatewayFactory>("", "");
    auto rateLimiterManager = gatewayFactory->buildRateLimitManager(rateLimitConfig);

    auto rateLimiterFactory = rateLimiterManager->rateLimiterFactory();

    BOOST_CHECK(rateLimiterFactory != nullptr);

    BOOST_CHECK(rateLimitConfig.totalOutgoingBwLimit > 0);
    BOOST_CHECK(rateLimitConfig.connOutgoingBwLimit > 0);
    BOOST_CHECK(rateLimitConfig.groupOutgoingBwLimit > 0);

    BOOST_CHECK(rateLimiterManager->getRateLimiter("group-group0") == nullptr);
    BOOST_CHECK(rateLimiterManager->ensureRateLimiterExist("group-group0", 10) != nullptr);
    BOOST_CHECK(rateLimiterManager->getRateLimiter("group-group0") != nullptr);
    BOOST_CHECK(rateLimiterManager->removeRateLimiter("group-group0"));
    BOOST_CHECK(!rateLimiterManager->removeRateLimiter("group-group0"));

    BOOST_CHECK(rateLimiterManager->getRateLimiter("ip-127.0.0.1") == nullptr);
    BOOST_CHECK(rateLimiterManager->ensureRateLimiterExist("ip-127.0.0.1", 10) != nullptr);
    BOOST_CHECK(rateLimiterManager->getRateLimiter("ip-127.0.0.1") != nullptr);
    BOOST_CHECK(rateLimiterManager->removeRateLimiter("ip-127.0.0.1"));
    BOOST_CHECK(!rateLimiterManager->removeRateLimiter("ip-127.0.0.1"));
}


BOOST_AUTO_TEST_SUITE_END()
