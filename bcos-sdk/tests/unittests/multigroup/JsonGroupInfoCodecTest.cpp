/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include <bcos-cpp-sdk/multigroup/JsonGroupInfoCodec.h>
#include <bcos-framework/multigroup/ChainNodeInfo.h>
#include <bcos-framework/multigroup/GroupInfoFactory.h>
#include <bcos-framework/protocol/ProtocolInfo.h>
#include <bcos-framework/protocol/ServiceDesc.h>
#include <json/json.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::group;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(JsonGroupInfoCodecTest)

namespace
{
ChainNodeInfo::Ptr makeNode(std::string const& _name, NodeCryptoType _cryptoType)
{
    auto node = std::make_shared<ChainNodeInfo>();
    node->setNodeName(_name);
    node->setNodeCryptoType(_cryptoType);
    // a node's iniConfig is itself a json string carrying isWasm + smCryptoType
    node->setIniConfig(R"({"isWasm":false,"smCryptoType":true})");
    node->appendServiceInfo(bcos::protocol::ServiceType::RPC, "chain0.group0.rpc");
    node->appendServiceInfo(bcos::protocol::ServiceType::GATEWAY, "chain0.group0.gateway");
    bcos::protocol::ProtocolInfo protocol;
    protocol.setMinVersion(1);
    protocol.setMaxVersion(3);
    node->setNodeProtocol(std::move(protocol));
    node->setCompatibilityVersion(2);
    node->setNodeID("node-id-abc");
    node->setMicroService(true);
    return node;
}
}  // namespace

BOOST_AUTO_TEST_CASE(chainNodeRoundTrip)
{
    JsonChainNodeInfoCodec codec;
    auto node = makeNode("node0", SM_NODE);

    Json::FastWriter writer;
    auto decoded = codec.deserialize(writer.write(codec.serialize(node)));

    BOOST_CHECK_EQUAL(decoded->nodeName(), "node0");
    BOOST_CHECK_EQUAL(
        static_cast<uint32_t>(decoded->nodeCryptoType()), static_cast<uint32_t>(SM_NODE));
    BOOST_CHECK_EQUAL(decoded->serviceInfo().size(), 2U);
    BOOST_CHECK_EQUAL(decoded->serviceName(bcos::protocol::ServiceType::RPC), "chain0.group0.rpc");
    BOOST_CHECK_EQUAL(decoded->nodeProtocol()->minVersion(), 1U);
    BOOST_CHECK_EQUAL(decoded->nodeProtocol()->maxVersion(), 3U);
    BOOST_CHECK_EQUAL(decoded->compatibilityVersion(), 2U);
}

BOOST_AUTO_TEST_CASE(groupInfoRoundTripString)
{
    auto groupFactory = std::make_shared<GroupInfoFactory>();
    auto group = groupFactory->createGroupInfo("chain0", "group0");
    group->setGenesisConfig("[genesis]");
    group->setIniConfig("[ini]");
    group->appendNodeInfo(makeNode("nodeA", NON_SM_NODE));
    group->appendNodeInfo(makeNode("nodeB", SM_NODE));

    JsonGroupInfoCodec codec;
    std::string encoded;
    codec.serialize(encoded, group);
    BOOST_CHECK(!encoded.empty());

    auto decoded = codec.deserialize(encoded);
    BOOST_CHECK_EQUAL(decoded->chainID(), "chain0");
    BOOST_CHECK_EQUAL(decoded->groupID(), "group0");
    BOOST_CHECK_EQUAL(decoded->genesisConfig(), "[genesis]");
    BOOST_CHECK_EQUAL(decoded->iniConfig(), "[ini]");
    BOOST_CHECK_EQUAL(decoded->nodeInfos().size(), 2U);

    auto nodeA = decoded->nodeInfo("nodeA");
    BOOST_REQUIRE(nodeA);
    BOOST_CHECK_EQUAL(nodeA->serviceInfo().size(), 2U);
}

BOOST_AUTO_TEST_CASE(groupInfoSerializeJsonValue)
{
    auto group = std::make_shared<GroupInfo>("chainX", "groupX");
    group->setIniConfig("[ini]");
    group->appendNodeInfo(makeNode("n0", NON_SM_NODE));

    JsonGroupInfoCodec codec;
    auto json = codec.serialize(group);
    BOOST_CHECK_EQUAL(json["chainID"].asString(), "chainX");
    BOOST_CHECK_EQUAL(json["groupID"].asString(), "groupX");
    BOOST_REQUIRE(json["nodeList"].isArray());
    BOOST_CHECK_EQUAL(json["nodeList"].size(), 1U);
}

BOOST_AUTO_TEST_CASE(groupInfoDeserializeErrors)
{
    JsonGroupInfoCodec codec;
    BOOST_CHECK_THROW(codec.deserialize("not-json"), bcos::InvalidParameter);
    BOOST_CHECK_THROW(codec.deserialize(R"({"groupID":"g"})"), bcos::InvalidParameter);
    BOOST_CHECK_THROW(codec.deserialize(R"({"chainID":"c"})"), bcos::InvalidParameter);
    BOOST_CHECK_THROW(
        codec.deserialize(R"({"chainID":"c","groupID":"g"})"), bcos::InvalidParameter);
    BOOST_CHECK_THROW(codec.deserialize(R"({"chainID":"c","groupID":"g","iniConfig":"i"})"),
        bcos::InvalidParameter);
}

BOOST_AUTO_TEST_CASE(chainNodeDeserializeErrors)
{
    JsonChainNodeInfoCodec codec;
    BOOST_CHECK_THROW(codec.deserialize("not-json"), bcos::InvalidParameter);
    BOOST_CHECK_THROW(codec.deserialize(R"({"type":0})"), bcos::InvalidParameter);
    BOOST_CHECK_THROW(codec.deserialize(R"({"name":"n","type":0})"), bcos::InvalidParameter);
    // iniConfig present but not carrying isWasm
    BOOST_CHECK_THROW(
        codec.deserialize(R"({"name":"n","type":0,"iniConfig":"{}","serviceInfo":[]})"),
        bcos::InvalidParameter);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
