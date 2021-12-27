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
 * @brief test for GatewayNodeManager
 * @file GatewayNodeManagerTest.cpp
 * @author: octopus
 * @date 2021-05-14
 */

#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-framework/testutils/TestPromptFixture.h>
#include <bcos-front/FrontServiceFactory.h>
#include <bcos-gateway/Gateway.h>
#include <bcos-gateway/GatewayNodeManager.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::test;

BOOST_FIXTURE_TEST_SUITE(GatewayNodeManagerTest, TestPromptFixture)

class FakeGatewayNodeManager : public GatewayNodeManager
{
public:
    FakeGatewayNodeManager(P2pID const& _nodeID,
        std::shared_ptr<bcos::crypto::KeyFactory> _keyFactory, P2PInterface::Ptr _p2pInterface)
      : GatewayNodeManager(_nodeID, _keyFactory, _p2pInterface)
    {}
    ~FakeGatewayNodeManager() override {}
    // public for ut
    bool statusChanged(std::string const& _p2pNodeID, uint32_t _seq)
    {
        return GatewayNodeManager::statusChanged(_p2pNodeID, _seq);
    }
    // public for ut
    uint32_t statusSeq() { return GatewayNodeManager::statusSeq(); }
    // public for ut
    bool generateNodeInfo(std::string& _nodeInfo) override
    {
        return GatewayNodeManager::generateNodeInfo(_nodeInfo);
    }
    // public for ut
    bool parseReceivedJson(const std::string& _json, uint32_t& statusSeq,
        std::unordered_map<std::string, std::set<std::string>>& nodeIDsMap)
    {
        return GatewayNodeManager::parseReceivedJson(_json, statusSeq, nodeIDsMap);
    }
    void updateNodeInfo(const P2pID& _p2pID, const std::string& _nodeIDsJson) override
    {
        return GatewayNodeManager::updateNodeInfo(_p2pID, _nodeIDsJson);
    }
};

BOOST_AUTO_TEST_CASE(test_P2PMessage_statusSeqChanged)
{
    auto gatewayNodeManager = std::make_shared<FakeGatewayNodeManager>("", nullptr, nullptr);
    std::string p2pID = "1";
    bool changed = false;
    changed = gatewayNodeManager->statusChanged(p2pID, 1);
    BOOST_CHECK(changed);
}

BOOST_AUTO_TEST_CASE(test_GatewayNodeManager_registerFrontService)
{
    auto gatewayNodeManager = std::make_shared<FakeGatewayNodeManager>("", nullptr, nullptr);
    std::string groupID = "group";
    std::string strNodeID = "nodeID";
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();

    auto nodeID =
        keyFactory->createKey(bytesConstRef((bcos::byte*)strNodeID.data(), strNodeID.size()));

    auto frontServiceFactory = std::make_shared<bcos::front::FrontServiceFactory>();
    frontServiceFactory->setGatewayInterface(
        std::make_shared<bcos::gateway::Gateway>("", nullptr, nullptr, nullptr));

    auto frontService = frontServiceFactory->buildFrontService(groupID, nodeID);

    bool r = false;
    auto seq = gatewayNodeManager->statusSeq();
    r = gatewayNodeManager->registerFrontService(groupID, nodeID, frontService);
    BOOST_CHECK_EQUAL(r, true);
    BOOST_CHECK_EQUAL(seq + 1, gatewayNodeManager->statusSeq());

    auto s = gatewayNodeManager->queryFrontServiceInterfaceByGroupID(groupID);
    BOOST_CHECK(!s.empty());

    seq = gatewayNodeManager->statusSeq();
    r = gatewayNodeManager->registerFrontService(groupID, nodeID, nullptr);
    BOOST_CHECK_EQUAL(r, false);
    BOOST_CHECK_EQUAL(seq, gatewayNodeManager->statusSeq());

    seq = gatewayNodeManager->statusSeq();
    r = gatewayNodeManager->unregisterFrontService(groupID, nodeID);
    BOOST_CHECK_EQUAL(r, true);
    BOOST_CHECK_EQUAL(seq + 1, gatewayNodeManager->statusSeq());

    s = gatewayNodeManager->queryFrontServiceInterfaceByGroupID(groupID);
    BOOST_CHECK(s.empty());

    seq = gatewayNodeManager->statusSeq();
    r = gatewayNodeManager->registerFrontService(groupID, nodeID, nullptr);
    BOOST_CHECK_EQUAL(r, true);
    BOOST_CHECK_EQUAL(seq + 1, gatewayNodeManager->statusSeq());

    s = gatewayNodeManager->queryFrontServiceInterfaceByGroupID(groupID);
    BOOST_CHECK(!s.empty());

    seq = gatewayNodeManager->statusSeq();
    r = gatewayNodeManager->registerFrontService(groupID, nodeID, nullptr);
    BOOST_CHECK_EQUAL(r, false);
    BOOST_CHECK_EQUAL(seq, gatewayNodeManager->statusSeq());
    s = gatewayNodeManager->queryFrontServiceInterfaceByGroupID(groupID);
    BOOST_CHECK(!s.empty());
}

BOOST_AUTO_TEST_CASE(test_GatewayNodeManager_registerFrontService_loop)
{
    auto gatewayNodeManager = std::make_shared<FakeGatewayNodeManager>("", nullptr, nullptr);
    size_t loopCount = 100;
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();

    for (size_t i = 0; i < loopCount; i++)
    {
        std::string strNodeID = "nodeID" + std::to_string(i);
        std::string groupID = "group" + std::to_string(i);

        auto nodeID =
            keyFactory->createKey(bytesConstRef((bcos::byte*)strNodeID.data(), strNodeID.size()));

        auto seq = gatewayNodeManager->statusSeq();
        bool r = gatewayNodeManager->registerFrontService(groupID, nodeID, nullptr);
        BOOST_CHECK_EQUAL(r, true);
        BOOST_CHECK_EQUAL(seq + 1, gatewayNodeManager->statusSeq());

        seq = gatewayNodeManager->statusSeq();
        r = gatewayNodeManager->registerFrontService(groupID, nodeID, nullptr);
        BOOST_CHECK_EQUAL(r, false);
        BOOST_CHECK_EQUAL(seq, gatewayNodeManager->statusSeq());

        std::string jsonValue;
        gatewayNodeManager->generateNodeInfo(jsonValue);
        BOOST_CHECK(!jsonValue.empty());

        seq = gatewayNodeManager->statusSeq();
        r = gatewayNodeManager->unregisterFrontService(groupID, nodeID);
        BOOST_CHECK_EQUAL(r, true);
        BOOST_CHECK_EQUAL(seq + 1, gatewayNodeManager->statusSeq());

        seq = gatewayNodeManager->statusSeq();
        r = gatewayNodeManager->unregisterFrontService(groupID, nodeID);
        BOOST_CHECK_EQUAL(r, false);
        BOOST_CHECK_EQUAL(seq, gatewayNodeManager->statusSeq());
    }
}

BOOST_AUTO_TEST_CASE(test_GatewayNodeManager_onRequestNodeIDs)
{
    auto gatewayNodeManager = std::make_shared<FakeGatewayNodeManager>("", nullptr, nullptr);
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();

    for (size_t i = 0; i < 100; i++)
    {
        std::string groupID = "group" + std::to_string(i);
        std::string strNodeID = "nodeID" + std::to_string(i);

        auto nodeID =
            keyFactory->createKey(bytesConstRef((bcos::byte*)strNodeID.data(), strNodeID.size()));

        bool r = false;
        auto seq = gatewayNodeManager->statusSeq();
        r = gatewayNodeManager->registerFrontService(groupID, nodeID, nullptr);
        BOOST_CHECK(r);
        BOOST_CHECK_EQUAL(seq + 1, gatewayNodeManager->statusSeq());

        std::string jsonValue;
        gatewayNodeManager->generateNodeInfo(jsonValue);
        BOOST_CHECK(!jsonValue.empty());

        uint32_t statusSeq;
        std::unordered_map<std::string, std::set<std::string>> nodeIDsMap;
        r = gatewayNodeManager->parseReceivedJson(jsonValue, statusSeq, nodeIDsMap);
        BOOST_CHECK(r);
        BOOST_CHECK_EQUAL(seq + 1, statusSeq);
    }
}

BOOST_AUTO_TEST_CASE(test_GatewayNodeManager_jsonJarser)
{
    auto gatewayNodeManager = std::make_shared<FakeGatewayNodeManager>("", nullptr, nullptr);

    uint32_t statusSeq;
    std::unordered_map<std::string, std::set<std::string>> nodeIDsMap;
    const std::string json =
        "{\"statusSeq\":110,\"nodeInfoList\":[{\"groupID\":\"group1\","
        "\"nodeIDs\":["
        "\"a0\",\"b0\",\"c0\"]},{\"groupID\":\"group2\",\"nodeIDs\":[\"a1\","
        "\"b1\",\"c1\"]},{\"groupID\":\"group3\",\"nodeIDs\":[\"a2\",\"b2\","
        "\"c2\"]}]}";

    auto r = gatewayNodeManager->parseReceivedJson(json, statusSeq, nodeIDsMap);
    BOOST_CHECK_EQUAL(r, true);
    BOOST_CHECK_EQUAL(statusSeq, 110);
    BOOST_CHECK_EQUAL(nodeIDsMap.size(), 3);

    BOOST_CHECK(nodeIDsMap.find("group1") != nodeIDsMap.end());
    BOOST_CHECK(nodeIDsMap.find("group2") != nodeIDsMap.end());
    BOOST_CHECK(nodeIDsMap.find("group3") != nodeIDsMap.end());

    const auto& group1 = nodeIDsMap["group1"];
    BOOST_CHECK_EQUAL(group1.size(), 3);
    BOOST_CHECK(group1.find("a0") != group1.end());
    BOOST_CHECK(group1.find("b0") != group1.end());
    BOOST_CHECK(group1.find("c0") != group1.end());

    const auto& group2 = nodeIDsMap["group2"];
    BOOST_CHECK_EQUAL(group2.size(), 3);
    BOOST_CHECK(group2.find("a1") != group2.end());
    BOOST_CHECK(group2.find("b1") != group2.end());
    BOOST_CHECK(group2.find("c1") != group2.end());

    const auto& group3 = nodeIDsMap["group3"];
    BOOST_CHECK_EQUAL(group3.size(), 3);
    BOOST_CHECK(group3.find("a2") != group3.end());
    BOOST_CHECK(group3.find("b2") != group3.end());
    BOOST_CHECK(group3.find("c2") != group3.end());
}

BOOST_AUTO_TEST_CASE(test_GatewayNodeManager_onReceiveNodeIDs)
{
    auto gatewayNodeManager = std::make_shared<FakeGatewayNodeManager>("", nullptr, nullptr);

    const std::string json =
        "{\"statusSeq\":110,\"nodeInfoList\":[{\"groupID\":\"group1\","
        "\"nodeIDs\":["
        "\"a0\",\"b0\",\"c0\"]},{\"groupID\":\"group2\",\"nodeIDs\":[\"a1\","
        "\"b1\",\"c1\"]},{\"groupID\":\"group3\",\"nodeIDs\":[\"a2\",\"b2\","
        "\"c2\"]}]}";
    std::string p2pID = "xxxxxxxxxxxxxxxxxxxxx";
    gatewayNodeManager->updateNodeInfo(p2pID, json);
    bool changed = false;

    changed = gatewayNodeManager->statusChanged(p2pID, 110);
    BOOST_CHECK(!changed);

    changed = gatewayNodeManager->statusChanged(p2pID, 1);
    BOOST_CHECK(changed);
}

BOOST_AUTO_TEST_CASE(test_GatewayNodeManager_query)
{
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    auto gatewayNodeManager = std::make_shared<FakeGatewayNodeManager>("", keyFactory, nullptr);

    const std::string json =
        "{\"statusSeq\":110,\"nodeInfoList\":[{\"groupID\":\"group1\","
        "\"nodeIDs\":["
        "\"a0\",\"b0\",\"c0\"]},{\"groupID\":\"group2\",\"nodeIDs\":[\"a1\","
        "\"b1\",\"c1\"]},{\"groupID\":\"group3\",\"nodeIDs\":[\"a2\",\"b2\","
        "\"c2\"]}]}";
    /*
    {
    "statusSeq": 1,
    "nodeInfoList": [
    {
    "groupID": "group1",
    "nodeIDs": [
      "a0",
      "b0",
      "c0"
    ]
    },
    {
    "groupID": "group2",
    "nodeIDs": [
      "a1",
      "b1",
      "c1"
    ]
    },
    {
    "groupID": "group3",
    "nodeIDs": [
      "a2",
      "b2",
      "c2"
    ]
    }
    ]
    }*/

    std::string group1 = "group1";
    std::string group2 = "group2";
    std::string group3 = "group3";

    std::string p2pID1 = "xxxxx";
    std::string p2pID2 = "yyyyy";
    std::string p2pID3 = "zzzzz";

    gatewayNodeManager->updateNodeInfo(p2pID1, json);

    std::set<P2pID> p2pIDs1;
    auto r = gatewayNodeManager->queryP2pIDsByGroupID(group1, p2pIDs1);
    BOOST_CHECK(r);
    BOOST_CHECK_EQUAL(p2pIDs1.size(), 1);
    BOOST_CHECK_EQUAL(*p2pIDs1.begin(), p2pID1);

    bcos::crypto::NodeIDs nodeIDs;
    gatewayNodeManager->queryNodeIDsByGroupID(group1, nodeIDs);
    BOOST_CHECK(r);
    BOOST_CHECK_EQUAL(nodeIDs.size(), 3);

    std::set<P2pID> p2pIDs2;
    r = gatewayNodeManager->queryP2pIDs(group1, "a0", p2pIDs2);
    BOOST_CHECK(r);
    BOOST_CHECK_EQUAL(p2pIDs2.size(), 1);
    BOOST_CHECK_EQUAL(*p2pIDs2.begin(), p2pID1);

    gatewayNodeManager->updateNodeInfo(p2pID2, json);

    std::set<P2pID> p2pIDs3;
    r = gatewayNodeManager->queryP2pIDsByGroupID(group2, p2pIDs3);
    BOOST_CHECK(r);
    BOOST_CHECK_EQUAL(p2pIDs3.size(), 2);

    std::set<P2pID> p2pIDs4;
    r = gatewayNodeManager->queryP2pIDs(group2, "a1", p2pIDs4);
    BOOST_CHECK(r);
    BOOST_CHECK_EQUAL(p2pIDs4.size(), 2);

    gatewayNodeManager->updateNodeInfo(p2pID3, json);

    std::set<P2pID> p2pIDs5;
    r = gatewayNodeManager->queryP2pIDsByGroupID(group3, p2pIDs5);
    BOOST_CHECK(r);
    BOOST_CHECK_EQUAL(p2pIDs5.size(), 3);

    std::set<P2pID> p2pIDs6;
    r = gatewayNodeManager->queryP2pIDs(group3, "a2", p2pIDs6);
    BOOST_CHECK(r);
    BOOST_CHECK_EQUAL(p2pIDs6.size(), 3);
}

BOOST_AUTO_TEST_CASE(test_GatewayNodeManager_remove)
{
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    auto gatewayNodeManager = std::make_shared<FakeGatewayNodeManager>("", keyFactory, nullptr);

    const std::string json =
        "{\"statusSeq\":110,\"nodeInfoList\":[{\"groupID\":\"group1\","
        "\"nodeIDs\":["
        "\"a0\",\"b0\",\"c0\"]},{\"groupID\":\"group2\",\"nodeIDs\":[\"a1\","
        "\"b1\",\"c1\"]},{\"groupID\":\"group3\",\"nodeIDs\":[\"a2\",\"b2\","
        "\"c2\"]}]}";

    std::string group1 = "group1";
    std::string group2 = "group2";
    std::string group3 = "group3";

    std::string p2pID1 = "xxxxx";
    std::string p2pID2 = "yyyyy";
    std::string p2pID3 = "zzzzz";

    gatewayNodeManager->updateNodeInfo(p2pID1, json);
    gatewayNodeManager->updateNodeInfo(p2pID2, json);
    gatewayNodeManager->updateNodeInfo(p2pID3, json);

    {
        std::set<P2pID> p2pIDs1;
        auto r = gatewayNodeManager->queryP2pIDsByGroupID(group1, p2pIDs1);
        BOOST_CHECK(r);
        BOOST_CHECK_EQUAL(p2pIDs1.size(), 3);
        BOOST_CHECK(p2pIDs1.find(p2pID2) != p2pIDs1.end());
        BOOST_CHECK(p2pIDs1.find(p2pID3) != p2pIDs1.end());
        BOOST_CHECK(p2pIDs1.find(p2pID1) != p2pIDs1.end());

        std::set<P2pID> p2pIDs2;
        r = gatewayNodeManager->queryP2pIDs(group1, "a0", p2pIDs2);
        BOOST_CHECK(r);
        BOOST_CHECK_EQUAL(p2pIDs2.size(), 3);
        BOOST_CHECK(p2pIDs2.find(p2pID2) != p2pIDs2.end());
        BOOST_CHECK(p2pIDs2.find(p2pID3) != p2pIDs2.end());
        BOOST_CHECK(p2pIDs2.find(p2pID1) != p2pIDs2.end());
    }

    gatewayNodeManager->onRemoveNodeIDs(p2pID1);
    {
        std::set<P2pID> p2pIDs1;
        auto r = gatewayNodeManager->queryP2pIDsByGroupID(group1, p2pIDs1);
        BOOST_CHECK(r);
        BOOST_CHECK_EQUAL(p2pIDs1.size(), 2);
        BOOST_CHECK(p2pIDs1.find(p2pID2) != p2pIDs1.end());
        BOOST_CHECK(p2pIDs1.find(p2pID3) != p2pIDs1.end());

        std::set<P2pID> p2pIDs2;
        r = gatewayNodeManager->queryP2pIDs(group1, "a0", p2pIDs2);
        BOOST_CHECK(r);
        BOOST_CHECK_EQUAL(p2pIDs2.size(), 2);
        BOOST_CHECK(p2pIDs2.find(p2pID2) != p2pIDs2.end());
        BOOST_CHECK(p2pIDs2.find(p2pID3) != p2pIDs2.end());
    }

    gatewayNodeManager->onRemoveNodeIDs(p2pID2);
    {
        std::set<P2pID> p2pIDs1;
        auto r = gatewayNodeManager->queryP2pIDsByGroupID(group1, p2pIDs1);
        BOOST_CHECK(r);
        BOOST_CHECK_EQUAL(p2pIDs1.size(), 1);
        BOOST_CHECK(p2pIDs1.find(p2pID3) != p2pIDs1.end());

        std::set<P2pID> p2pIDs2;
        r = gatewayNodeManager->queryP2pIDs(group1, "a0", p2pIDs2);
        BOOST_CHECK(r);
        BOOST_CHECK_EQUAL(p2pIDs2.size(), 1);
        BOOST_CHECK(p2pIDs2.find(p2pID3) != p2pIDs2.end());
    }

    gatewayNodeManager->onRemoveNodeIDs(p2pID3);
    {
        std::set<P2pID> p2pIDs1;
        auto r = gatewayNodeManager->queryP2pIDsByGroupID(group1, p2pIDs1);
        BOOST_CHECK(!r);

        std::set<P2pID> p2pIDs2;
        r = gatewayNodeManager->queryP2pIDs(group1, "a0", p2pIDs2);
        BOOST_CHECK(!r);
    }
}

BOOST_AUTO_TEST_SUITE_END()