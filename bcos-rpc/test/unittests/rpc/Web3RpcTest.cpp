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
 * @file Web3RpcTest.cpp
 * @author: kyonGuo
 * @date 2024/3/29
 */


#include "../common/RPCFixture.h"
#include "bcos-crypto/encrypt/AESCrypto.h"
#include "bcos-crypto/signature/secp256k1/Secp256k1Crypto.h"
#include "bcos-crypto/signature/secp256k1/Secp256k1KeyPair.h"
#include "bcos-crypto/signature/sm2/SM2Crypto.h"
#include "bcos-crypto/signature/sm2/SM2KeyPair.h"
#include "bcos-framework/protocol/GlobalConfig.h"
#include "bcos-rpc/bcos-rpc/RpcFactory.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/hash/SM3.h>
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-framework/executor/PrecompiledTypeDef.h>
#include <bcos-framework/testutils/faker/FakeFrontService.h>
#include <bcos-framework/testutils/faker/FakeLedger.h>
#include <bcos-framework/testutils/faker/FakeSealer.h>
#include <bcos-rpc/tarsRPC/RPCServer.h>
#include <bcos-rpc/validator/CallValidator.h>
#include <bcos-utilities/Exceptions.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>

using namespace bcos;
using namespace bcos::rpc;
using namespace bcos::crypto;
namespace bcos::test
{
class Web3TestFixture : public RPCFixture
{
public:
    Web3TestFixture()
    {
        rpc = factory->buildLocalRpc(groupInfo, nodeService);
        web3JsonRpc = rpc->web3JsonRpc();
        BOOST_CHECK(web3JsonRpc != nullptr);
    }
    Json::Value onRPCRequestWrapper(std::string_view request)
    {
        Json::Value value;
        try
        {
            std::promise<bcos::bytes> promise;
            web3JsonRpc->onRPCRequest(
                request, [&promise](bcos::bytes resp) { promise.set_value(std::move(resp)); });
            Json::Reader reader;
            auto jsonBytes = promise.get_future().get();
            std::string_view json(
                (char*)jsonBytes.data(), (char*)jsonBytes.data() + jsonBytes.size());
            reader.parse(json.begin(), json.end(), value);
        }
        catch (std::exception const& e)
        {
            BCOS_LOG(ERROR) << (e).what();
        }
        return value;
    }
    Rpc::Ptr rpc;
    Web3JsonRpcImpl::Ptr web3JsonRpc;
};
BOOST_FIXTURE_TEST_SUITE(testWeb3RPC, Web3TestFixture)
BOOST_AUTO_TEST_CASE(handleInvalidTest)
{
    // no method
    {
        const auto request = R"({"jsonrpc":"2.0","id":1})";
        auto response = onRPCRequestWrapper(request);
        BOOST_CHECK(response.isMember("error"));
        BOOST_CHECK(response["error"]["code"].asInt() == InvalidRequest);
    }

    // invalid json
    {
        const auto request = R"({{"jsonrpc":"2.0","id":1, "method":"eth_blockNumber","params":[]})";
        auto response = onRPCRequestWrapper(request);
        BOOST_CHECK(response.isMember("error"));
        BOOST_CHECK(response["error"]["code"].asInt() == InvalidRequest);
    }

    // invalid params type
    {
        const auto request = R"({"jsonrpc":"2.0","id":1, "method":"eth_blockNumber","params":{}})";
        auto response = onRPCRequestWrapper(request);
        BOOST_CHECK(response.isMember("error"));
        BOOST_CHECK(response["error"]["code"].asInt() == InvalidRequest);
    }

    // invalid method
    {
        const auto request = R"({"jsonrpc":"2.0","id":1, "method":"eth_AAA","params":[]})";
        auto response = onRPCRequestWrapper(request);
        BOOST_CHECK(response.isMember("error"));
        BOOST_CHECK(response["error"]["code"].asInt() == MethodNotFound);
        BOOST_CHECK(response["error"]["message"].asString() == "Method not found");
    }
}

BOOST_AUTO_TEST_CASE(handleValidTest)
{
    auto validRespCheck = [](Json::Value const& resp) {
        // BOOST_CHECK(!resp.isMember("error"));
        // BOOST_CHECK(resp.isMember("result"));
        // BOOST_CHECK(resp.isMember("id"));
        // BOOST_CHECK(resp.isMember("jsonrpc"));
        // BOOST_CHECK(resp["jsonrpc"].asString() == "2.0");
    };

    // method eth_syncing
    {
        const auto request =
            R"({"jsonrpc":"2.0","id":1132123, "method":"eth_syncing","params":[]})";
        auto response = onRPCRequestWrapper(request);
        validRespCheck(response);
        BOOST_CHECK(response["id"].asInt64() == 1132123);
        BOOST_CHECK(response["result"].asBool() == false);
    }

    // method eth_chainId
    {
        const auto request = R"({"jsonrpc":"2.0","id":123, "method":"eth_chainId","params":[]})";
        auto response = onRPCRequestWrapper(request);
        validRespCheck(response);
        BOOST_CHECK(response["id"].asInt64() == 123);
        BOOST_CHECK(fromQuantity(response["result"].asString()) == 20200);
    }

    // method eth_mining
    {
        const auto request = R"({"jsonrpc":"2.0","id":3214, "method":"eth_mining","params":[]})";
        auto response = onRPCRequestWrapper(request);
        validRespCheck(response);
        BOOST_CHECK(response["id"].asInt64() == 3214);
        BOOST_CHECK(response["result"].asBool() == false);
    }

    // method eth_hashrate
    {
        const auto request = R"({"jsonrpc":"2.0","id":3214, "method":"eth_hashrate","params":[]})";
        auto response = onRPCRequestWrapper(request);
        validRespCheck(response);
        BOOST_CHECK(response["id"].asInt64() == 3214);
        BOOST_CHECK(fromQuantity(response["result"].asString()) == 0);
    }

    // method eth_gasPrice
    {
        m_ledger->setSystemConfig(SYSTEM_KEY_TX_GAS_PRICE, "10086");
        const auto request =
            R"({"jsonrpc":"2.0","id":541321, "method":"eth_gasPrice","params":[]})";
        auto response = onRPCRequestWrapper(request);
        validRespCheck(response);
        BOOST_CHECK(response["id"].asInt64() == 541321);
        auto b = fromQuantity(response["result"].asString());
        std::cout << b << std::endl;
        BOOST_CHECK(b == 10086);
        BOOST_CHECK(fromQuantity(response["result"].asString()) == 10086);
    }

    // method eth_accounts
    {
        const auto request = R"({"jsonrpc":"2.0","id":3214, "method":"eth_accounts","params":[]})";
        auto response = onRPCRequestWrapper(request);
        validRespCheck(response);
        BOOST_CHECK(response["id"].asInt64() == 3214);
        BOOST_CHECK(response["result"].size() == 0);
    }

    // method eth_blockNumber
    {
        const auto request =
            R"({"jsonrpc":"2.0","id":996886, "method":"eth_blockNumber","params":[]})";
        auto response = onRPCRequestWrapper(request);
        validRespCheck(response);
        BOOST_CHECK(response["id"].asInt64() == 996886);
        auto const blkNum = toQuantity(m_ledger->blockNumber());
        BOOST_CHECK(response["result"].asString() == blkNum);
    }
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test