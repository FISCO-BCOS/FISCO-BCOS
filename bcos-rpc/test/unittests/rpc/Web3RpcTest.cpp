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
        std::promise<bcos::bytes> promise;
        web3JsonRpc->onRPCRequest(
            request, [&promise](bcos::bytes resp) { promise.set_value(std::move(resp)); });
        Json::Reader reader;
        auto jsonBytes = promise.get_future().get();
        std::string_view json((char*)jsonBytes.data(), (char*)jsonBytes.data() + jsonBytes.size());
        Json::Value value;
        reader.parse(json.begin(), json.end(), value);
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
    // method
    {
        const auto request = R"({"jsonrpc":"2.0","id":1, "method":"eth_blockNumber","params":[]})";
        auto response = onRPCRequestWrapper(request);
        BOOST_CHECK(!response.isMember("error"));
    }
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test