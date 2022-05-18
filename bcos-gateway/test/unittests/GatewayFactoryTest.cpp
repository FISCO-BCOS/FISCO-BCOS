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
 * @brief test for GatewayFactory
 * @file GatewayFactoryTest.cpp
 * @author: octopus
 * @date 2021-05-17
 */

#include <bcos-gateway/GatewayConfig.h>
#include <bcos-gateway/GatewayFactory.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace gateway;
using namespace bcos::test;

BOOST_FIXTURE_TEST_SUITE(GatewayFactoryTest, TestPromptFixture)

BOOST_AUTO_TEST_CASE(test_certPubHexHandler)
{
    auto factory = std::make_shared<GatewayFactory>("", "");
    {
        // sm cert
        std::string cert = "../../../bcos-gateway/test/unittests/data/sm_ca/sm_node.crt";
        std::string pubHex;
        auto r = factory->certPubHexHandler()(cert, pubHex);
        BOOST_CHECK(r);
        BOOST_CHECK_EQUAL(pubHex,
            R"(045a0d065954bbc96dba0e9eea163d970a9187c3e5f1a6329daf2898acb888ac2d668f4e3b34b538dcd1be7839d86a0869ca6478913cfd4e46c1517586f9c0b3c0)");
    }

    {
        // RSA cert
        std::string cert("../../../bcos-gateway/test/unittests/data/ca/node.crt");
        std::string pubHex;
        auto r = factory->certPubHexHandler()(cert, pubHex);
        BOOST_CHECK(r);
    }
}

BOOST_AUTO_TEST_CASE(test_buildSSLContext)
{
    auto factory = std::make_shared<GatewayFactory>("", "");

    {
        // SM SSLContext
        std::string configIni("../../../bcos-gateway/test/unittests/data/config/config_ipv6.ini");
        auto config = std::make_shared<GatewayConfig>();
        config->initConfig(configIni);
        auto context = factory->buildSSLContext(config->smCertConfig(), config->storageSecurityConfig());
        BOOST_CHECK(context);
    }

    {
        // SSLContext
        std::string configIni("../../../bcos-gateway/test/unittests/data/config/config_ipv4.ini");
        auto config = std::make_shared<GatewayConfig>();
        config->initConfig(configIni);
        auto context = factory->buildSSLContext(config->certConfig(), config->storageSecurityConfig());
        BOOST_CHECK(context);
    }
}

BOOST_AUTO_TEST_SUITE_END()