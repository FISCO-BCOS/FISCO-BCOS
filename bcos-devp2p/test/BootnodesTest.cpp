/**
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @file BootnodesTest.cpp
 * @brief Tests for geth-style enode:// parsing and bootnodes.json loading
 *        (Ethereum L1 EL-mode bootnode configuration).
 * @date 2026/8/18
 */
#include <bcos-devp2p/sync/Bootnodes.h>
#include <boost/test/unit_test.hpp>
#include <cstdio>
#include <fstream>
#include <string>

using namespace bcos;
using namespace bcos::devp2p;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(BootnodesTest)

BOOST_AUTO_TEST_CASE(parseEnodeValid)
{
    // 128 hex chars = 64-byte uncompressed secp256k1 pubkey.
    std::string pubkey(128, 'a');
    auto config = sync::parseEnode("enode://" + pubkey + "@1.2.3.4:30303");
    BOOST_CHECK_EQUAL(config.host, "1.2.3.4");
    BOOST_CHECK_EQUAL(config.port, 30303);
    BOOST_CHECK_EQUAL(config.peerPublicKey.size(), 64u);
    BOOST_CHECK_EQUAL(bcos::toHex(config.peerPublicKey), pubkey);
}

BOOST_AUTO_TEST_CASE(parseEnodeWithDiscPort)
{
    std::string pubkey(128, 'b');
    auto config = sync::parseEnode("enode://" + pubkey + "@10.0.0.1:30303?discport=30301");
    BOOST_CHECK_EQUAL(config.host, "10.0.0.1");
    BOOST_CHECK_EQUAL(config.port, 30303);
    BOOST_CHECK_EQUAL(config.peerPublicKey.size(), 64u);
}

BOOST_AUTO_TEST_CASE(parseEnodeBadPrefix)
{
    std::string pubkey(128, 'c');
    BOOST_CHECK_THROW(sync::parseEnode(pubkey + "@1.2.3.4:30303"), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(parseEnodeMissingAt)
{
    std::string pubkey(128, 'd');
    BOOST_CHECK_THROW(sync::parseEnode("enode://" + pubkey + "1.2.3.4:30303"),
        std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(parseEnodeBadPort)
{
    std::string pubkey(128, 'e');
    BOOST_CHECK_THROW(
        sync::parseEnode("enode://" + pubkey + "@1.2.3.4:notaport"), std::invalid_argument);
    BOOST_CHECK_THROW(
        sync::parseEnode("enode://" + pubkey + "@1.2.3.4:99999"), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(loadBootnodesArray)
{
    std::string pub1(128, '1');
    std::string pub2(128, '2');
    std::string tmp = "/tmp/bootnodes_array_test.json";
    {
        std::ofstream out(tmp);
        out << "[\"enode://" << pub1 << "@1.1.1.1:30303\",\"enode://" << pub2
            << "@2.2.2.2:30304\"]";
    }
    auto nodes = sync::loadBootnodes(tmp);
    BOOST_REQUIRE_EQUAL(nodes.size(), 2u);
    BOOST_CHECK_EQUAL(nodes[0].host, "1.1.1.1");
    BOOST_CHECK_EQUAL(nodes[0].port, 30303);
    BOOST_CHECK_EQUAL(nodes[1].host, "2.2.2.2");
    BOOST_CHECK_EQUAL(nodes[1].port, 30304);
    std::remove(tmp.c_str());
}

BOOST_AUTO_TEST_CASE(loadBootnodesObject)
{
    std::string pub(128, 'f');
    std::string tmp = "/tmp/bootnodes_object_test.json";
    {
        std::ofstream out(tmp);
        out << "{\"bootnodes\":[\"enode://" << pub << "@3.3.3.3:30303\"]}";
    }
    auto nodes = sync::loadBootnodes(tmp);
    BOOST_REQUIRE_EQUAL(nodes.size(), 1u);
    BOOST_CHECK_EQUAL(nodes[0].host, "3.3.3.3");
    std::remove(tmp.c_str());
}

BOOST_AUTO_TEST_CASE(loadBootnodesMissingFile)
{
    BOOST_CHECK_THROW(sync::loadBootnodes("/nonexistent/bootnodes.json"), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(loadBootnodesInvalidJson)
{
    std::string tmp = "/tmp/bootnodes_bad_test.json";
    {
        std::ofstream out(tmp);
        out << "not json {";
    }
    BOOST_CHECK_THROW(sync::loadBootnodes(tmp), std::invalid_argument);
    std::remove(tmp.c_str());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
