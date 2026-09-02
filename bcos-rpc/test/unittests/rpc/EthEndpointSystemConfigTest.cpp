/**
 *  Copyright (C) 2024 FISCO BCOS.
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
 * @file EthEndpointSystemConfigTest.cpp
 * @brief EthEndpoint::chainId() must source from LedgerConfig (A6.9 / PR-6).
 *
 * In L2 mode the SystemConfigPrecompiled (0x1000) is disabled and L2ConfigLoader
 * (PR-4) fills LedgerConfig::chainId per block from SystemConfig.sol's
 * getChainConfig(). EthEndpoint must therefore read chainId from LedgerConfig
 * (getLedgerConfig -> fetchAllSystemConfigs) rather than parsing it itself, so
 * what dApps observe via eth_chainId is L2 governance.
 */

#include "../common/RPCFixture.h"
#include "bcos-utilities/DataConvertUtility.h"
#include <bcos-framework/ledger/LedgerTypeDef.h>

#include <boost/test/unit_test.hpp>
using namespace bcos;
using namespace bcos::rpc;
using namespace bcos::crypto;

namespace bcos::test
{

class EthSystemConfigFixture : public RPCFixture
{
public:
    EthSystemConfigFixture()
    {
        rpc = factory->buildLocalRpc(groupInfo, nodeService);
        web3JsonRpc = rpc->web3JsonRpc();
        BOOST_TEST(web3JsonRpc != nullptr);
    }

    Json::Value request(std::string_view req)
    {
        Json::Value value;
        Json::Reader reader;
        std::promise<bcos::bytes> promise;
        web3JsonRpc->onRPCRequest(req, [&promise](bcos::bytes resp, boost::beast::http::status) {
            promise.set_value(std::move(resp));
        });
        auto jsonBytes = promise.get_future().get();
        std::string_view json((char*)jsonBytes.data(), (char*)jsonBytes.data() + jsonBytes.size());
        reader.parse(json.begin(), json.end(), value);
        return value;
    }

    static void validRespCheck(Json::Value const& resp)
    {
        BOOST_TEST(!resp.isMember("error"));
        BOOST_TEST(resp.isMember("result"));
        BOOST_TEST(resp["jsonrpc"].asString() == "2.0");
    }

    Rpc::Ptr rpc;
    Web3JsonRpcImpl::Ptr web3JsonRpc;
};

BOOST_FIXTURE_TEST_SUITE(EthEndpointSystemConfigTest, EthSystemConfigFixture)

// Contract 1: eth_chainId returns the value carried by LedgerConfig.
// The mock's getLedgerConfig path reads web3_chain_id through fetchAllSystemConfigs
// (the same row SystemConfig.sol governs in L2), so setting it here and reading it
// back over the wire proves the endpoint consumes LedgerConfig::chainId().
BOOST_AUTO_TEST_CASE(chainIdFromLedgerConfig)
{
    constexpr uint64_t kChainId = 11155111ULL;  // sepolia
    m_ledger->setSystemConfig(ledger::SYSTEM_KEY_WEB3_CHAIN_ID, std::to_string(kChainId));

    auto resp = request(R"({"jsonrpc":"2.0","id":1,"method":"eth_chainId","params":[]})");
    validRespCheck(resp);
    BOOST_TEST(resp["id"].asInt64() == 1);
    BOOST_TEST(fromQuantity(resp["result"].asString()) == kChainId);
}

// Contract 2: the value flows through LedgerConfig's 256-bit chainId, NOT a raw
// scalar read. A chainId larger than uint64::max round-trips correctly only via
// LedgerConfig::chainId() (an evmc_uint256be parsed by boost::lexical_cast<u256>).
// The previous raw path (std::stoull(web3_chain_id)) would have thrown out_of_range
// and degraded the response to "0x0". A correct big value is therefore positive
// evidence that LedgerConfig is the source.
BOOST_AUTO_TEST_CASE(chainIdRoutesThroughLedgerConfigNotRawScalar)
{
    // 2^64 + 1 = 18446744073709551617, unrepresentable as uint64.
    const std::string bigChainId = "18446744073709551617";
    m_ledger->setSystemConfig(ledger::SYSTEM_KEY_WEB3_CHAIN_ID, bigChainId);

    auto resp = request(R"({"jsonrpc":"2.0","id":2,"method":"eth_chainId","params":[]})");
    validRespCheck(resp);
    BOOST_TEST(resp["id"].asInt64() == 2);
    // Expected hex of 2^64 + 1.
    BOOST_TEST(resp["result"].asString() == "0x10000000000000001");
    // Guard against the old degraded behaviour.
    BOOST_TEST(resp["result"].asString() != "0x0");
}

// net_version must share parseWeb3ChainId with eth_chainId. The previous
// std::stoull path treated "0x539" as 0 (stops at 'x') and forged 20200 on
// any exception or a missing row.
BOOST_AUTO_TEST_CASE(netVersionHexQuantityMatchesEthChainId)
{
    m_ledger->setSystemConfig(ledger::SYSTEM_KEY_WEB3_CHAIN_ID, "0x539");

    auto net = request(R"({"jsonrpc":"2.0","id":3,"method":"net_version","params":[]})");
    auto eth = request(R"({"jsonrpc":"2.0","id":4,"method":"eth_chainId","params":[]})");
    validRespCheck(net);
    validRespCheck(eth);
    BOOST_TEST(fromQuantity(net["result"].asString()) == 1337ULL);
    BOOST_TEST(net["result"].asString() != "0x0");
    BOOST_TEST(net["result"].asString() != "0x4ee8");
    BOOST_TEST(fromQuantity(net["result"].asString()) == fromQuantity(eth["result"].asString()));
}

BOOST_AUTO_TEST_CASE(netVersionDecimalAgreesWithEthChainId)
{
    m_ledger->setSystemConfig(ledger::SYSTEM_KEY_WEB3_CHAIN_ID, "1337");

    auto net = request(R"({"jsonrpc":"2.0","id":5,"method":"net_version","params":[]})");
    auto eth = request(R"({"jsonrpc":"2.0","id":6,"method":"eth_chainId","params":[]})");
    validRespCheck(net);
    validRespCheck(eth);
    BOOST_TEST(fromQuantity(net["result"].asString()) == 1337ULL);
    BOOST_TEST(fromQuantity(net["result"].asString()) == fromQuantity(eth["result"].asString()));
}

BOOST_AUTO_TEST_CASE(netVersionMalformedDoesNotForge20200)
{
    m_ledger->setSystemConfig(ledger::SYSTEM_KEY_WEB3_CHAIN_ID, "not-a-number");

    auto resp = request(R"({"jsonrpc":"2.0","id":7,"method":"net_version","params":[]})");
    BOOST_TEST(resp.isMember("error"));
    if (resp.isMember("result"))
    {
        BOOST_TEST(resp["result"].asString() != "0x4ee8");
    }
}

BOOST_AUTO_TEST_CASE(netVersionNegativeDoesNotWrap)
{
    m_ledger->setSystemConfig(ledger::SYSTEM_KEY_WEB3_CHAIN_ID, "-5");

    auto resp = request(R"({"jsonrpc":"2.0","id":8,"method":"net_version","params":[]})");
    BOOST_TEST(resp.isMember("error"));
    if (resp.isMember("result"))
    {
        BOOST_TEST(resp["result"].asString() != "0x4ee8");
    }
}

BOOST_AUTO_TEST_CASE(netVersionMissingIsZeroNot20200)
{
    auto resp = request(R"({"jsonrpc":"2.0","id":9,"method":"net_version","params":[]})");
    validRespCheck(resp);
    BOOST_TEST(resp["result"].asString() == "0x0");
    BOOST_TEST(resp["result"].asString() != "0x4ee8");
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
