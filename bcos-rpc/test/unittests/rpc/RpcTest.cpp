/**
 *  Copyright (C) 2022 FISCO BCOS.
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
 * @file RpcTest.cpp
 * @author: kyonGuo
 * @date 2023/7/20
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
BOOST_FIXTURE_TEST_SUITE(testValidator, RPCFixture)
BOOST_AUTO_TEST_CASE(buildTest)
{
    auto rpc = factory->buildRpc("gateway", "rpc", nullptr);
    auto groupManager = rpc->groupManager();
    BOOST_CHECK(groupManager != nullptr);

    nodeConfig = std::make_shared<bcos::tool::NodeConfig>();
    nodeConfig->loadConfigFromString(wrongConfigini);
    factory->setNodeConfig(nodeConfig);
    BOOST_CHECK_THROW(factory->buildRpc("gateway", "rpc", nullptr), InvalidParameter);

    rpc::RPCApplication rpcApplication(nullptr);
    rpcApplication.destroyApp();
}

BOOST_AUTO_TEST_CASE(jsonRpcTest)
{
    auto rpc = factory->buildLocalRpc(groupInfo, nodeService);

    rpc->groupManager()->updateGroupInfo(groupInfo);
    rpc->jsonRpcImpl()->getBlockNumber(groupId, "", [](auto&& error, Json::Value& value) {
        BOOST_CHECK(!error);
        BOOST_CHECK(!value.empty());
        auto intValue = value.asInt();
        BOOST_CHECK(intValue > 0);
    });
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test