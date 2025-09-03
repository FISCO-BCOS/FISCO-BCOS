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
 * @file Web3SubscribeTest.cpp
 * @author: octopus
 * @date 2025/01/06
 */

#include "../common/RPCFixture.h"
#include <bcos-boostssl/websocket/WsMessage.h>
#include <bcos-boostssl/websocket/WsService.h>
#include <bcos-boostssl/websocket/WsSession.h>
#include <bcos-framework/gateway/GatewayInterface.h>
#include <bcos-rpc/filter/FilterSystem.h>
#include <bcos-rpc/groupmgr/GroupManager.h>
#include <bcos-rpc/web3jsonrpc/Web3JsonRpcImpl.h>
#include <bcos-rpc/web3jsonrpc/Web3Subscribe.h>
#
using namespace bcos;
using namespace bcos::rpc;
using namespace bcos::crypto;
using namespace bcos::boostssl::ws;

namespace bcos::test
{

// Mock Web3JsonRpcImpl for testing
class MockWeb3JsonRpcImpl : public Web3JsonRpcImpl,
                            public std::enable_shared_from_this<MockWeb3JsonRpcImpl>
{
public:
    using Ptr = std::shared_ptr<MockWeb3JsonRpcImpl>;
    using WeakPtr = std::weak_ptr<MockWeb3JsonRpcImpl>;

    MockWeb3JsonRpcImpl()
      : Web3JsonRpcImpl("test-group", 1000,
            std::make_shared<GroupManager>("test-group", "1", nullptr, nullptr), nullptr, nullptr,
            nullptr, false)
    {}

    ~MockWeb3JsonRpcImpl() = default;

    // Mock methods that Web3Subscribe might call
    void mockMethod() {}
};

// Mock WsSession for testing
class MockWsSession : public WsSession
{
public:
    using Ptr = std::shared_ptr<MockWsSession>;

    MockWsSession(tbb::task_arena& taskArena, tbb::task_group& taskGroup)
      : WsSession(taskArena, taskGroup)
    {
        setEndPoint("127.0.0.1:8080");
    }

    ~MockWsSession() override = default;

    bool isConnected() override { return m_connected; }
    void setConnected(bool connected) { m_connected = connected; }

    std::string getEndpoint() const { return endPoint(); }

    void setEndPoint(const std::string& endpoint) { m_endPoint = endpoint; }

private:
    bool m_connected = true;
};

BOOST_FIXTURE_TEST_SUITE(testWeb3Subscribe, RPCFixture)

BOOST_AUTO_TEST_CASE(testIsSubscribeRequest)
{
    auto mockWeb3JsonRpcImpl = std::make_shared<MockWeb3JsonRpcImpl>();
    auto weakPtr = std::weak_ptr<MockWeb3JsonRpcImpl>(mockWeb3JsonRpcImpl);
    auto web3Subscribe = std::make_shared<Web3Subscribe>(weakPtr);

    // Test valid subscribe methods
    BOOST_CHECK(web3Subscribe->isSubscribeRequest("eth_subscribe"));
    BOOST_CHECK(web3Subscribe->isSubscribeRequest("eth_unsubscribe"));

    // Test invalid methods
    BOOST_CHECK(!web3Subscribe->isSubscribeRequest("eth_getBlockByNumber"));
    BOOST_CHECK(!web3Subscribe->isSubscribeRequest("eth_sendTransaction"));
    BOOST_CHECK(!web3Subscribe->isSubscribeRequest(""));
    BOOST_CHECK(!web3Subscribe->isSubscribeRequest("subscribe"));
    BOOST_CHECK(!web3Subscribe->isSubscribeRequest("unsubscribe"));
}

BOOST_AUTO_TEST_CASE(testGenerateSubscriptionId)
{
    auto mockWeb3JsonRpcImpl = std::make_shared<MockWeb3JsonRpcImpl>();
    auto weakPtr = std::weak_ptr<MockWeb3JsonRpcImpl>(mockWeb3JsonRpcImpl);
    auto web3Subscribe = std::make_shared<Web3Subscribe>(weakPtr);

    // Test that subscription IDs are generated and are unique
    std::string id1 = web3Subscribe->generateSubscriptionId();
    std::string id2 = web3Subscribe->generateSubscriptionId();
    std::string id3 = web3Subscribe->generateSubscriptionId();

    BOOST_CHECK(!id1.empty());
    BOOST_CHECK(!id2.empty());
    BOOST_CHECK(!id3.empty());

    BOOST_CHECK_NE(id1, id2);
    BOOST_CHECK_NE(id1, id3);
    BOOST_CHECK_NE(id2, id3);

    // Test that IDs are hexadecimal strings
    BOOST_CHECK(id1.substr(0, 2) == "0x");
    BOOST_CHECK(id2.substr(0, 2) == "0x");
    BOOST_CHECK(id3.substr(0, 2) == "0x");
    BOOST_CHECK(id1.substr(2).find_first_not_of("0123456789abcdef") == std::string::npos);
    BOOST_CHECK(id2.substr(2).find_first_not_of("0123456789abcdef") == std::string::npos);
    BOOST_CHECK(id3.substr(2).find_first_not_of("0123456789abcdef") == std::string::npos);
}


BOOST_AUTO_TEST_CASE(testOnHttpSubscribeRequest)
{
    auto mockWeb3JsonRpcImpl = std::make_shared<MockWeb3JsonRpcImpl>();
    auto weakPtr = std::weak_ptr<MockWeb3JsonRpcImpl>(mockWeb3JsonRpcImpl);
    auto web3Subscribe = std::make_shared<Web3Subscribe>(weakPtr);

    mockWeb3JsonRpcImpl->setWeb3Subscribe(web3Subscribe);

    // Create mock session
    tbb::task_arena taskArena(1);
    tbb::task_group taskGroup;
    auto session = std::make_shared<MockWsSession>(taskArena, taskGroup);

    auto mockSession = std::make_shared<MockWsSession>(taskArena, taskGroup);

    int id = 111;
    // Test newHeads subscription
    Json::Value request;
    request["jsonrpc"] = "2.0";
    request["id"] = id;
    request["method"] = "eth_subscribe";
    request["params"] = Json::Value(Json::arrayValue);
    request["params"].append("newHeads");

    std::string strRequest =
        "{\"jsonrpc\":  \"2.0\",  \"id\":  111,  \"method\":  \"eth_subscribe\",  \"params\":  "
        "[\"newHeads\"]}";

    Json::Value requestJson;
    Json::Reader reader;
    BOOST_CHECK(reader.parse(strRequest, requestJson));
    std::string strRequest1 = requestJson.toStyledString();
    BOOST_CHECK(strRequest1 == request.toStyledString());

    bcos::bytes respBytes1;
    auto sender = [&respBytes1](bcos::bytes bytes) { respBytes1 = bytes; };

    mockWeb3JsonRpcImpl->onRPCRequest(request.toStyledString(), nullptr, sender);

    Json::Value responseJson;
    // Json::Reader reader;
    BOOST_CHECK(reader.parse(std::string(respBytes1.begin(), respBytes1.end()), responseJson));

    std::cout << "responseJson: " << responseJson.toStyledString() << std::endl;

    BOOST_CHECK(responseJson.isMember("jsonrpc"));
    BOOST_CHECK(responseJson["jsonrpc"].asString() == "2.0");
    BOOST_CHECK(responseJson.isMember("id"));
    BOOST_CHECK(responseJson["id"].asInt() == id);
    BOOST_CHECK(responseJson.isMember("error"));
    BOOST_CHECK(responseJson["error"]["code"].asInt() == -32600);
    BOOST_CHECK(responseJson["error"]["message"].asString() ==
                "Subscribe request only support websocket protocol");
    // Test pending transactions subscription
}

BOOST_AUTO_TEST_CASE(testOnSubscribeRequest)
{
    auto mockWeb3JsonRpcImpl = std::make_shared<MockWeb3JsonRpcImpl>();
    auto weakPtr = std::weak_ptr<MockWeb3JsonRpcImpl>(mockWeb3JsonRpcImpl);
    auto web3Subscribe = std::make_shared<Web3Subscribe>(weakPtr);

    mockWeb3JsonRpcImpl->setWeb3Subscribe(web3Subscribe);

    // Create mock session
    tbb::task_arena taskArena(1);
    tbb::task_group taskGroup;
    auto session = std::make_shared<MockWsSession>(taskArena, taskGroup);

    auto mockSession = std::make_shared<MockWsSession>(taskArena, taskGroup);

    int id = 111;
    // Test newHeads subscription
    Json::Value request;
    request["jsonrpc"] = "2.0";
    request["id"] = id;
    request["method"] = "eth_subscribe";
    request["params"] = Json::Value(Json::arrayValue);
    request["params"].append("newHeads");

    std::string strRequest =
        "{\"jsonrpc\":  \"2.0\",  \"id\":  111,  \"method\":  \"eth_subscribe\",  \"params\":  "
        "[\"newHeads\"]}";

    Json::Value requestJson;
    Json::Reader reader;
    BOOST_CHECK(reader.parse(strRequest, requestJson));
    std::string strRequest1 = requestJson.toStyledString();
    BOOST_CHECK(strRequest1 == request.toStyledString());

    bcos::bytes respBytes1;
    auto sender = [&respBytes1](bcos::bytes bytes) { respBytes1 = bytes; };

    mockWeb3JsonRpcImpl->onRPCRequest(request.toStyledString(), mockSession, sender);

    Json::Value responseJson;
    // Json::Reader reader;
    BOOST_CHECK(reader.parse(std::string(respBytes1.begin(), respBytes1.end()), responseJson));

    BOOST_CHECK(responseJson.isMember("jsonrpc"));
    BOOST_CHECK(responseJson["jsonrpc"].asString() == "2.0");
    BOOST_CHECK(responseJson.isMember("id"));
    BOOST_CHECK(responseJson["id"].asInt() == id);
    BOOST_CHECK(responseJson.isMember("result"));
    BOOST_CHECK(!responseJson["result"].asString().empty());

    BOOST_CHECK(web3Subscribe->endpoint2SubscriptionIds().contains(mockSession->getEndpoint()));
    BOOST_CHECK(web3Subscribe->newHeads2Session().contains(responseJson["result"].asString()));
    BOOST_CHECK(web3Subscribe->isSubscriptionIdExists(responseJson["result"].asString()));

    // Test pending transactions subscription
    Json::Value request2;
    request2["jsonrpc"] = "2.0";
    request2["id"] = 2;
    request2["method"] = "eth_subscribe";
    request2["params"] = Json::Value(Json::arrayValue);
    request2["params"].append("newPendingTransactions");

    bcos::bytes respBytes2;
    auto sender2 = [&respBytes2](bcos::bytes bytes) { respBytes2 = bytes; };

    mockWeb3JsonRpcImpl->onRPCRequest(request2.toStyledString(), mockSession, sender2);

    Json::Value responseJson2;
    BOOST_CHECK(reader.parse(std::string(respBytes2.begin(), respBytes2.end()), responseJson2));
    BOOST_CHECK(responseJson2.isMember("error"));
    BOOST_CHECK(responseJson2["error"]["code"].asInt() == -32600);
    BOOST_CHECK(responseJson2["error"]["message"].asString() == "Unsupported eth_subscribe method");
}

BOOST_AUTO_TEST_CASE(testOnSubscribeNewHeads)
{
    auto mockWeb3JsonRpcImpl = std::make_shared<MockWeb3JsonRpcImpl>();
    auto weakPtr = std::weak_ptr<MockWeb3JsonRpcImpl>(mockWeb3JsonRpcImpl);
    auto web3Subscribe = std::make_shared<Web3Subscribe>(weakPtr);
    mockWeb3JsonRpcImpl->setWeb3Subscribe(web3Subscribe);

    // Create mock session
    tbb::task_arena taskArena(1);
    tbb::task_group taskGroup;
    auto session = std::make_shared<MockWsSession>(taskArena, taskGroup);

    int id = 123;
    Json::Value request;
    request["jsonrpc"] = "2.0";
    request["id"] = id;
    request["method"] = "eth_subscribe";
    request["params"] = Json::Value(Json::arrayValue);
    request["params"].append("newHeads");

    bcos::bytes respBytes;
    auto sender = [&respBytes](bcos::bytes bytes) { respBytes = bytes; };

    mockWeb3JsonRpcImpl->onRPCRequest(request.toStyledString(), session, sender);

    Json::Value responseJson;
    Json::Reader reader;
    BOOST_CHECK(reader.parse(std::string(respBytes.begin(), respBytes.end()), responseJson));

    BOOST_CHECK(responseJson.isMember("jsonrpc"));
    BOOST_CHECK(responseJson["jsonrpc"].asString() == "2.0");
    BOOST_CHECK(responseJson.isMember("id"));
    BOOST_CHECK(responseJson["id"].asInt() == id);
    BOOST_CHECK(responseJson.isMember("result"));
    BOOST_CHECK(!responseJson["result"].asString().empty());

    BOOST_CHECK(web3Subscribe->isSubscriptionIdExists(responseJson["result"].asString()));
}

BOOST_AUTO_TEST_CASE(testOnUnsubscribeRequest)
{
    auto mockWeb3JsonRpcImpl = std::make_shared<MockWeb3JsonRpcImpl>();
    auto weakPtr = std::weak_ptr<MockWeb3JsonRpcImpl>(mockWeb3JsonRpcImpl);
    auto web3Subscribe = std::make_shared<Web3Subscribe>(weakPtr);
    mockWeb3JsonRpcImpl->setWeb3Subscribe(web3Subscribe);


    std::string subscriptionId1;
    std::string subscriptionId2;
    std::string subscriptionId3;
    std::string subscriptionId4;

    // Create mock session
    tbb::task_arena taskArena(1);
    tbb::task_group taskGroup;
    // auto session = std::make_shared<MockWsSession>(taskArena, taskGroup);

    auto session1 = std::make_shared<MockWsSession>(taskArena, taskGroup);
    session1->setEndPoint("127.0.0.1:8080");
    auto session2 = std::make_shared<MockWsSession>(taskArena, taskGroup);
    session2->setEndPoint("127.0.0.1:8081");

    Json::Reader reader;

    const auto& newHeads2Session = web3Subscribe->newHeads2Session();
    const auto& endpoint2SubscriptionIds = web3Subscribe->endpoint2SubscriptionIds();
    BOOST_CHECK(newHeads2Session.empty());
    BOOST_CHECK(endpoint2SubscriptionIds.empty());

    {
        // Subscribe1 with session1
        Json::Value request1;
        request1["jsonrpc"] = "2.0";
        request1["id"] = 1;
        request1["method"] = "eth_subscribe";
        request1["params"] = Json::Value(Json::arrayValue);
        request1["params"].append("newHeads");

        bcos::bytes respBytes1;
        auto sender1 = [&respBytes1](bcos::bytes bytes) { respBytes1 = bytes; };
        mockWeb3JsonRpcImpl->onRPCRequest(request1.toStyledString(), session1, sender1);

        Json::Value responseJson1;

        BOOST_CHECK(reader.parse(std::string(respBytes1.begin(), respBytes1.end()), responseJson1));
        subscriptionId1 = responseJson1["result"].asString();

        BOOST_CHECK(web3Subscribe->isSubscriptionIdExists(subscriptionId1));
    }

    {
        // Subscribe2 with session2
        Json::Value request2;
        request2["jsonrpc"] = "2.0";
        request2["id"] = 2;
        request2["method"] = "eth_subscribe";
        request2["params"] = Json::Value(Json::arrayValue);
        request2["params"].append("newHeads");

        bcos::bytes respBytes2;
        auto sender2 = [&respBytes2](bcos::bytes bytes) { respBytes2 = bytes; };
        mockWeb3JsonRpcImpl->onRPCRequest(request2.toStyledString(), session2, sender2);

        Json::Value responseJson2;
        BOOST_CHECK(reader.parse(std::string(respBytes2.begin(), respBytes2.end()), responseJson2));
        subscriptionId2 = responseJson2["result"].asString();

        BOOST_CHECK(web3Subscribe->isSubscriptionIdExists(subscriptionId2));
    }

    {
        // Subscribe3 with session1
        Json::Value request3;
        request3["jsonrpc"] = "2.0";
        request3["id"] = 3;
        request3["method"] = "eth_subscribe";
        request3["params"] = Json::Value(Json::arrayValue);
        request3["params"].append("newHeads");

        bcos::bytes respBytes3;
        auto sender3 = [&respBytes3](bcos::bytes bytes) { respBytes3 = bytes; };
        mockWeb3JsonRpcImpl->onRPCRequest(request3.toStyledString(), session1, sender3);
        Json::Value responseJson3;
        BOOST_CHECK(reader.parse(std::string(respBytes3.begin(), respBytes3.end()), responseJson3));
        subscriptionId3 = responseJson3["result"].asString();

        BOOST_CHECK(web3Subscribe->isSubscriptionIdExists(subscriptionId3));
    }

    {
        // Subscribe4 with session2
        Json::Value request4;
        request4["jsonrpc"] = "2.0";
        request4["id"] = 4;
        request4["method"] = "eth_subscribe";
        request4["params"] = Json::Value(Json::arrayValue);
        request4["params"].append("newHeads");

        bcos::bytes respBytes4;
        auto sender4 = [&respBytes4](bcos::bytes bytes) { respBytes4 = bytes; };
        mockWeb3JsonRpcImpl->onRPCRequest(request4.toStyledString(), session2, sender4);
        Json::Value responseJson4;
        BOOST_CHECK(reader.parse(std::string(respBytes4.begin(), respBytes4.end()), responseJson4));
        subscriptionId4 = responseJson4["result"].asString();

        BOOST_CHECK(web3Subscribe->isSubscriptionIdExists(subscriptionId4));
    }

    BOOST_CHECK(web3Subscribe->isSubscriptionIdExists(subscriptionId1));
    BOOST_CHECK(web3Subscribe->isSubscriptionIdExists(subscriptionId2));
    BOOST_CHECK(web3Subscribe->isSubscriptionIdExists(subscriptionId3));
    BOOST_CHECK(web3Subscribe->isSubscriptionIdExists(subscriptionId4));

    BOOST_CHECK(newHeads2Session.size() == 4);
    BOOST_CHECK(newHeads2Session.contains(subscriptionId1));
    BOOST_CHECK(newHeads2Session.contains(subscriptionId2));
    BOOST_CHECK(newHeads2Session.contains(subscriptionId3));
    BOOST_CHECK(newHeads2Session.contains(subscriptionId4));

    BOOST_CHECK(endpoint2SubscriptionIds.size() == 2);
    BOOST_CHECK(endpoint2SubscriptionIds.contains(session1->getEndpoint()));
    BOOST_CHECK(endpoint2SubscriptionIds.contains(session2->getEndpoint()));

    {
        // unsubscribe subscription1
        Json::Value unsubscribeRequest;
        unsubscribeRequest["jsonrpc"] = "2.0";
        unsubscribeRequest["id"] = 2;
        unsubscribeRequest["method"] = "eth_unsubscribe";
        unsubscribeRequest["params"] = Json::Value(Json::arrayValue);
        unsubscribeRequest["params"].append(subscriptionId1);

        bcos::bytes respBytes;
        auto sender = [&respBytes](bcos::bytes bytes) { respBytes = bytes; };

        mockWeb3JsonRpcImpl->onRPCRequest(unsubscribeRequest.toStyledString(), session1, sender);

        Json::Value responseJson;
        BOOST_CHECK(reader.parse(std::string(respBytes.begin(), respBytes.end()), responseJson));
        BOOST_CHECK(!responseJson.isMember("error"));
        BOOST_CHECK(responseJson["result"].asBool());

        BOOST_CHECK(!web3Subscribe->isSubscriptionIdExists(subscriptionId1));

        BOOST_CHECK(newHeads2Session.size() == 3);
        BOOST_CHECK(!newHeads2Session.contains(subscriptionId1));
        BOOST_CHECK(newHeads2Session.contains(subscriptionId2));
        BOOST_CHECK(newHeads2Session.contains(subscriptionId3));
        BOOST_CHECK(newHeads2Session.contains(subscriptionId4));

        BOOST_CHECK(endpoint2SubscriptionIds.size() == 2);
        BOOST_CHECK(endpoint2SubscriptionIds.contains(session1->getEndpoint()));
        BOOST_CHECK(endpoint2SubscriptionIds.contains(session2->getEndpoint()));
    }

    {
        // unsubscribe subscription2
        Json::Value unsubscribeRequest;
        unsubscribeRequest["jsonrpc"] = "2.0";
        unsubscribeRequest["id"] = 2;
        unsubscribeRequest["method"] = "eth_unsubscribe";
        unsubscribeRequest["params"] = Json::Value(Json::arrayValue);
        unsubscribeRequest["params"].append(subscriptionId2);

        bcos::bytes respBytes;
        auto sender = [&respBytes](bcos::bytes bytes) { respBytes = bytes; };

        mockWeb3JsonRpcImpl->onRPCRequest(unsubscribeRequest.toStyledString(), session2, sender);

        Json::Value responseJson;
        BOOST_CHECK(reader.parse(std::string(respBytes.begin(), respBytes.end()), responseJson));
        BOOST_CHECK(!responseJson.isMember("error"));
        BOOST_CHECK(responseJson["result"].asBool());

        BOOST_CHECK(!web3Subscribe->isSubscriptionIdExists(subscriptionId2));

        BOOST_CHECK(newHeads2Session.size() == 2);
        BOOST_CHECK(!newHeads2Session.contains(subscriptionId1));
        BOOST_CHECK(!newHeads2Session.contains(subscriptionId2));
        BOOST_CHECK(newHeads2Session.contains(subscriptionId3));
        BOOST_CHECK(newHeads2Session.contains(subscriptionId4));

        BOOST_CHECK(endpoint2SubscriptionIds.size() == 2);
        BOOST_CHECK(endpoint2SubscriptionIds.contains(session1->getEndpoint()));
        BOOST_CHECK(endpoint2SubscriptionIds.contains(session2->getEndpoint()));
    }

    {
        // unsubscribe subscription3
        Json::Value unsubscribeRequest;
        unsubscribeRequest["jsonrpc"] = "2.0";
        unsubscribeRequest["id"] = 2;
        unsubscribeRequest["method"] = "eth_unsubscribe";
        unsubscribeRequest["params"] = Json::Value(Json::arrayValue);
        unsubscribeRequest["params"].append(subscriptionId3);

        bcos::bytes respBytes;
        auto sender = [&respBytes](bcos::bytes bytes) { respBytes = bytes; };

        mockWeb3JsonRpcImpl->onRPCRequest(unsubscribeRequest.toStyledString(), session1, sender);

        Json::Value responseJson;
        BOOST_CHECK(reader.parse(std::string(respBytes.begin(), respBytes.end()), responseJson));
        BOOST_CHECK(!responseJson.isMember("error"));
        BOOST_CHECK(responseJson["result"].asBool());

        BOOST_CHECK(!web3Subscribe->isSubscriptionIdExists(subscriptionId3));

        BOOST_CHECK(newHeads2Session.size() == 1);
        BOOST_CHECK(!newHeads2Session.contains(subscriptionId1));
        BOOST_CHECK(!newHeads2Session.contains(subscriptionId2));
        BOOST_CHECK(!newHeads2Session.contains(subscriptionId3));
        BOOST_CHECK(newHeads2Session.contains(subscriptionId4));

        BOOST_CHECK(endpoint2SubscriptionIds.size() == 1);
        BOOST_CHECK(!endpoint2SubscriptionIds.contains(session1->getEndpoint()));
        BOOST_CHECK(endpoint2SubscriptionIds.contains(session2->getEndpoint()));
    }

    {
        // unsubscribe subscription4
        Json::Value unsubscribeRequest;
        unsubscribeRequest["jsonrpc"] = "2.0";
        unsubscribeRequest["id"] = 2;
        unsubscribeRequest["method"] = "eth_unsubscribe";
        unsubscribeRequest["params"] = Json::Value(Json::arrayValue);
        unsubscribeRequest["params"].append(subscriptionId4);

        bcos::bytes respBytes;
        auto sender = [&respBytes](bcos::bytes bytes) { respBytes = bytes; };

        mockWeb3JsonRpcImpl->onRPCRequest(unsubscribeRequest.toStyledString(), session2, sender);

        Json::Value responseJson;
        BOOST_CHECK(reader.parse(std::string(respBytes.begin(), respBytes.end()), responseJson));
        BOOST_CHECK(!responseJson.isMember("error"));
        BOOST_CHECK(responseJson["result"].asBool());

        BOOST_CHECK(!web3Subscribe->isSubscriptionIdExists(subscriptionId4));

        BOOST_CHECK(newHeads2Session.size() == 0);
        BOOST_CHECK(!newHeads2Session.contains(subscriptionId1));
        BOOST_CHECK(!newHeads2Session.contains(subscriptionId2));
        BOOST_CHECK(!newHeads2Session.contains(subscriptionId3));
        BOOST_CHECK(!newHeads2Session.contains(subscriptionId4));

        BOOST_CHECK_EQUAL(endpoint2SubscriptionIds.size(), 0);
        BOOST_CHECK(!endpoint2SubscriptionIds.contains(session1->getEndpoint()));
        BOOST_CHECK(!endpoint2SubscriptionIds.contains(session2->getEndpoint()));
    }

    {
        // unsubscribe subscription1 with session1 again
        Json::Value unsubscribeRequest;
        unsubscribeRequest["jsonrpc"] = "2.0";
        unsubscribeRequest["id"] = 2;
        unsubscribeRequest["method"] = "eth_unsubscribe";
        unsubscribeRequest["params"] = Json::Value(Json::arrayValue);
        unsubscribeRequest["params"].append(subscriptionId1);

        bcos::bytes respBytes2;
        auto sender2 = [&respBytes2](bcos::bytes bytes) { respBytes2 = bytes; };

        mockWeb3JsonRpcImpl->onRPCRequest(unsubscribeRequest.toStyledString(), session1, sender2);

        Json::Value responseJson2;
        BOOST_CHECK(reader.parse(std::string(respBytes2.begin(), respBytes2.end()), responseJson2));
        BOOST_CHECK(!responseJson2.isMember("error"));
        BOOST_CHECK(!responseJson2["result"].asBool());

        BOOST_CHECK(!web3Subscribe->isSubscriptionIdExists(subscriptionId1));
    }
}

BOOST_AUTO_TEST_CASE(testOnRemoveSubscribeBySession)
{
    auto mockWeb3JsonRpcImpl = std::make_shared<MockWeb3JsonRpcImpl>();
    auto weakPtr = std::weak_ptr<MockWeb3JsonRpcImpl>(mockWeb3JsonRpcImpl);
    auto web3Subscribe = std::make_shared<Web3Subscribe>(weakPtr);
    mockWeb3JsonRpcImpl->setWeb3Subscribe(web3Subscribe);


    std::string subscriptionId1;
    std::string subscriptionId2;
    std::string subscriptionId3;
    std::string subscriptionId4;

    // Create mock session
    tbb::task_arena taskArena(1);
    tbb::task_group taskGroup;
    // auto session = std::make_shared<MockWsSession>(taskArena, taskGroup);

    auto session1 = std::make_shared<MockWsSession>(taskArena, taskGroup);
    session1->setEndPoint("127.0.0.1:8080");
    auto session2 = std::make_shared<MockWsSession>(taskArena, taskGroup);
    session2->setEndPoint("127.0.0.1:8081");

    Json::Reader reader;

    const auto& newHeads2Session = web3Subscribe->newHeads2Session();
    const auto& endpoint2SubscriptionIds = web3Subscribe->endpoint2SubscriptionIds();
    BOOST_CHECK(newHeads2Session.empty());
    BOOST_CHECK(endpoint2SubscriptionIds.empty());

    {
        // Subscribe1 with session1
        Json::Value request1;
        request1["jsonrpc"] = "2.0";
        request1["id"] = 1;
        request1["method"] = "eth_subscribe";
        request1["params"] = Json::Value(Json::arrayValue);
        request1["params"].append("newHeads");

        bcos::bytes respBytes1;
        auto sender1 = [&respBytes1](bcos::bytes bytes) { respBytes1 = bytes; };
        mockWeb3JsonRpcImpl->onRPCRequest(request1.toStyledString(), session1, sender1);

        Json::Value responseJson1;

        BOOST_CHECK(reader.parse(std::string(respBytes1.begin(), respBytes1.end()), responseJson1));
        subscriptionId1 = responseJson1["result"].asString();

        BOOST_CHECK(web3Subscribe->isSubscriptionIdExists(subscriptionId1));
    }

    {
        // Subscribe2 with session2
        Json::Value request2;
        request2["jsonrpc"] = "2.0";
        request2["id"] = 2;
        request2["method"] = "eth_subscribe";
        request2["params"] = Json::Value(Json::arrayValue);
        request2["params"].append("newHeads");

        bcos::bytes respBytes2;
        auto sender2 = [&respBytes2](bcos::bytes bytes) { respBytes2 = bytes; };
        mockWeb3JsonRpcImpl->onRPCRequest(request2.toStyledString(), session2, sender2);

        Json::Value responseJson2;
        BOOST_CHECK(reader.parse(std::string(respBytes2.begin(), respBytes2.end()), responseJson2));
        subscriptionId2 = responseJson2["result"].asString();

        BOOST_CHECK(web3Subscribe->isSubscriptionIdExists(subscriptionId2));
    }

    {
        // Subscribe3 with session1
        Json::Value request3;
        request3["jsonrpc"] = "2.0";
        request3["id"] = 3;
        request3["method"] = "eth_subscribe";
        request3["params"] = Json::Value(Json::arrayValue);
        request3["params"].append("newHeads");

        bcos::bytes respBytes3;
        auto sender3 = [&respBytes3](bcos::bytes bytes) { respBytes3 = bytes; };
        mockWeb3JsonRpcImpl->onRPCRequest(request3.toStyledString(), session1, sender3);
        Json::Value responseJson3;
        BOOST_CHECK(reader.parse(std::string(respBytes3.begin(), respBytes3.end()), responseJson3));
        subscriptionId3 = responseJson3["result"].asString();

        BOOST_CHECK(web3Subscribe->isSubscriptionIdExists(subscriptionId3));
    }

    {
        // Subscribe4 with session2
        Json::Value request4;
        request4["jsonrpc"] = "2.0";
        request4["id"] = 4;
        request4["method"] = "eth_subscribe";
        request4["params"] = Json::Value(Json::arrayValue);
        request4["params"].append("newHeads");

        bcos::bytes respBytes4;
        auto sender4 = [&respBytes4](bcos::bytes bytes) { respBytes4 = bytes; };
        mockWeb3JsonRpcImpl->onRPCRequest(request4.toStyledString(), session2, sender4);
        Json::Value responseJson4;
        BOOST_CHECK(reader.parse(std::string(respBytes4.begin(), respBytes4.end()), responseJson4));
        subscriptionId4 = responseJson4["result"].asString();

        BOOST_CHECK(web3Subscribe->isSubscriptionIdExists(subscriptionId4));
    }

    // Remove session1 subscriptions
    web3Subscribe->onRemoveSubscribeBySession(session1);
    BOOST_CHECK(!web3Subscribe->isSubscriptionIdExists(subscriptionId1));
    BOOST_CHECK(web3Subscribe->isSubscriptionIdExists(subscriptionId2));
    BOOST_CHECK(!web3Subscribe->isSubscriptionIdExists(subscriptionId3));
    BOOST_CHECK(web3Subscribe->isSubscriptionIdExists(subscriptionId4));

    BOOST_CHECK_EQUAL(newHeads2Session.size(), 2);
    BOOST_CHECK(newHeads2Session.contains(subscriptionId2));
    BOOST_CHECK(newHeads2Session.contains(subscriptionId4));
    BOOST_CHECK(!newHeads2Session.contains(subscriptionId3));
    BOOST_CHECK(!newHeads2Session.contains(subscriptionId1));
    BOOST_CHECK_EQUAL(endpoint2SubscriptionIds.size(), 1);
    BOOST_CHECK(endpoint2SubscriptionIds.contains(session2->getEndpoint()));

    // Remove session2 subscriptions
    web3Subscribe->onRemoveSubscribeBySession(session2);
    BOOST_CHECK(!web3Subscribe->isSubscriptionIdExists(subscriptionId1));
    BOOST_CHECK(!web3Subscribe->isSubscriptionIdExists(subscriptionId2));
    BOOST_CHECK(!web3Subscribe->isSubscriptionIdExists(subscriptionId3));
    BOOST_CHECK(!web3Subscribe->isSubscriptionIdExists(subscriptionId4));

    BOOST_CHECK_EQUAL(newHeads2Session.size(), 0);
    BOOST_CHECK_EQUAL(endpoint2SubscriptionIds.size(), 0);
}

BOOST_AUTO_TEST_CASE(testOnNewBlock)
{
    /*
    auto mockWeb3JsonRpcImpl = std::make_shared<MockWeb3JsonRpcImpl>();
    auto weakPtr = std::weak_ptr<MockWeb3JsonRpcImpl>(mockWeb3JsonRpcImpl);
    auto web3Subscribe = std::make_shared<Web3Subscribe>(weakPtr);
    mockWeb3JsonRpcImpl->setWeb3Subscribe(web3Subscribe);

    // Create mock session
    tbb::task_arena taskArena(1);
    tbb::task_group taskGroup;
    auto session = std::make_shared<MockWsSession>(taskArena, taskGroup);

    Json::Value request;
    request["jsonrpc"] = "2.0";
    request["id"] = 1;
    request["method"] = "eth_subscribe";
    request["params"] = Json::Value(Json::arrayValue);
    request["params"].append("newHeads");

    bcos::bytes respBytes;
    auto sender = [&respBytes](bcos::bytes bytes) { respBytes = bytes; };
    mockWeb3JsonRpcImpl->onRPCRequest(request.toStyledString(), session, sender);

    Json::Value responseJson;
    Json::Reader reader;
    BOOST_CHECK(reader.parse(std::string(respBytes.begin(), respBytes.end()), responseJson));
    std::string subscriptionId = responseJson["result"].asString();

    BOOST_CHECK(web3Subscribe->isSubscriptionIdExists(subscriptionId));

    // Test onNewBlock with different block numbers
    BOOST_CHECK_NO_THROW(web3Subscribe->onNewBlock(100));
    BOOST_CHECK_NO_THROW(web3Subscribe->onNewBlock(101));
    BOOST_CHECK_NO_THROW(web3Subscribe->onNewBlock(102));

    // Test with negative block numbers
    BOOST_CHECK_NO_THROW(web3Subscribe->onNewBlock(-1));
    BOOST_CHECK_NO_THROW(web3Subscribe->onNewBlock(0));
    */
}

BOOST_AUTO_TEST_CASE(testWeb3SubscribeInvalidRequests)
{
    auto mockWeb3JsonRpcImpl = std::make_shared<MockWeb3JsonRpcImpl>();
    auto weakPtr = std::weak_ptr<MockWeb3JsonRpcImpl>(mockWeb3JsonRpcImpl);
    auto web3Subscribe = std::make_shared<Web3Subscribe>(weakPtr);

    // Create mock session
    tbb::task_arena taskArena(1);
    tbb::task_group taskGroup;
    auto session = std::make_shared<MockWsSession>(taskArena, taskGroup);

    // Test with empty request
    Json::Value emptyRequest;
    BOOST_CHECK_THROW(web3Subscribe->onSubscribeRequest(emptyRequest, session), std::exception);

    // Test with invalid method
    Json::Value invalidRequest;
    invalidRequest["jsonrpc"] = "2.0";
    invalidRequest["id"] = 1;
    invalidRequest["method"] = "invalid_method";
    invalidRequest["params"] = Json::Value(Json::arrayValue);

    BOOST_CHECK_THROW(web3Subscribe->onSubscribeRequest(invalidRequest, session), std::exception);

    // Test with missing params
    Json::Value missingParamsRequest;
    missingParamsRequest["jsonrpc"] = "2.0";
    missingParamsRequest["id"] = 1;
    missingParamsRequest["method"] = "eth_subscribe";

    BOOST_CHECK_THROW(
        web3Subscribe->onSubscribeRequest(missingParamsRequest, session), std::exception);
}

BOOST_AUTO_TEST_CASE(testConcurrentAccess)
{
    auto mockWeb3JsonRpcImpl = std::make_shared<MockWeb3JsonRpcImpl>();
    auto weakPtr = std::weak_ptr<MockWeb3JsonRpcImpl>(mockWeb3JsonRpcImpl);
    auto web3Subscribe = std::make_shared<Web3Subscribe>(weakPtr);
    mockWeb3JsonRpcImpl->setWeb3Subscribe(web3Subscribe);

    // Create multiple mock sessions
    tbb::task_arena taskArena(1);
    tbb::task_group taskGroup;
    auto session1 = std::make_shared<MockWsSession>(taskArena, taskGroup);
    auto session2 = std::make_shared<MockWsSession>(taskArena, taskGroup);
    auto session3 = std::make_shared<MockWsSession>(taskArena, taskGroup);

    // Create subscription requests
    Json::Value request1, request2, request3;
    request1["jsonrpc"] = "2.0";
    request1["id"] = 1;
    request1["method"] = "eth_subscribe";
    request1["params"] = Json::Value(Json::arrayValue);
    request1["params"].append("newHeads");

    request2["jsonrpc"] = "2.0";
    request2["id"] = 2;
    request2["method"] = "eth_subscribe";
    request2["params"] = Json::Value(Json::arrayValue);
    request2["params"].append("newHeads");

    request3["jsonrpc"] = "2.0";
    request3["id"] = 3;
    request3["method"] = "eth_subscribe";
    request3["params"] = Json::Value(Json::arrayValue);
    request3["params"].append("newHeads");

    // Test concurrent subscriptions
    bcos::bytes respBytes1;
    auto sender1 = [&respBytes1](bcos::bytes bytes) { respBytes1 = bytes; };
    mockWeb3JsonRpcImpl->onRPCRequest(request1.toStyledString(), session1, sender1);
    bcos::bytes respBytes2;
    auto sender2 = [&respBytes2](bcos::bytes bytes) { respBytes2 = bytes; };
    mockWeb3JsonRpcImpl->onRPCRequest(request2.toStyledString(), session2, sender2);
    bcos::bytes respBytes3;
    auto sender3 = [&respBytes3](bcos::bytes bytes) { respBytes3 = bytes; };
    mockWeb3JsonRpcImpl->onRPCRequest(request3.toStyledString(), session3, sender3);

    Json::Value responseJson1, responseJson2, responseJson3;
    Json::Reader reader;
    BOOST_CHECK(reader.parse(std::string(respBytes1.begin(), respBytes1.end()), responseJson1));
    BOOST_CHECK(reader.parse(std::string(respBytes2.begin(), respBytes2.end()), responseJson2));
    BOOST_CHECK(reader.parse(std::string(respBytes3.begin(), respBytes3.end()), responseJson3));

    BOOST_CHECK(responseJson1.isMember("result"));
    BOOST_CHECK(responseJson2.isMember("result"));
    BOOST_CHECK(responseJson3.isMember("result"));
    BOOST_CHECK(!responseJson1.isMember("error"));
    BOOST_CHECK(!responseJson2.isMember("error"));
    BOOST_CHECK(!responseJson3.isMember("error"));

    BOOST_CHECK(responseJson1["id"].asInt() == 1);
    BOOST_CHECK(responseJson2["id"].asInt() == 2);
    BOOST_CHECK(responseJson3["id"].asInt() == 3);

    std::string id1 = responseJson1["result"].asString();
    std::string id2 = responseJson2["result"].asString();
    std::string id3 = responseJson3["result"].asString();

    // All IDs should be unique
    BOOST_CHECK_NE(id1, id2);
    BOOST_CHECK_NE(id1, id3);
    BOOST_CHECK_NE(id2, id3);
}

BOOST_AUTO_TEST_CASE(testMultiSubInOneRequest)
{
    auto mockWeb3JsonRpcImpl = std::make_shared<MockWeb3JsonRpcImpl>();
    auto weakPtr = std::weak_ptr<MockWeb3JsonRpcImpl>(mockWeb3JsonRpcImpl);
    auto web3Subscribe = std::make_shared<Web3Subscribe>(weakPtr);
    mockWeb3JsonRpcImpl->setWeb3Subscribe(web3Subscribe);

    // Create multiple mock sessions
    tbb::task_arena taskArena(1);
    tbb::task_group taskGroup;
    auto session = std::make_shared<MockWsSession>(taskArena, taskGroup);


    Json::Value request;

    // Create subscription requests
    Json::Value request1, request2, request3;
    request1["jsonrpc"] = "2.0";
    request1["id"] = 1;
    request1["method"] = "eth_subscribe";
    request1["params"] = Json::Value(Json::arrayValue);
    request1["params"].append("newHeads");

    request2["jsonrpc"] = "2.0";
    request2["id"] = 2;
    request2["method"] = "eth_subscribe";
    request2["params"] = Json::Value(Json::arrayValue);
    request2["params"].append("newHeads");

    request3["jsonrpc"] = "2.0";
    request3["id"] = 3;
    request3["method"] = "eth_subscribe";
    request3["params"] = Json::Value(Json::arrayValue);
    request3["params"].append("newHeads");

    request.append(request1);
    request.append(request2);
    request.append(request3);

    // Test concurrent subscriptions
    bcos::bytes respBytes;
    auto sender = [&respBytes](bcos::bytes bytes) { respBytes = bytes; };
    mockWeb3JsonRpcImpl->onRPCRequest(request.toStyledString(), session, sender);

    Json::Value responseJson;
    Json::Reader reader;
    BOOST_CHECK(reader.parse(std::string(respBytes.begin(), respBytes.end()), responseJson));

    std::cout << "responseJson: " << responseJson.toStyledString() << std::endl;

    BOOST_CHECK(responseJson.isArray());
    BOOST_CHECK(responseJson.size() == 3);
    BOOST_CHECK(responseJson[0].isMember("result"));
    BOOST_CHECK(responseJson[1].isMember("result"));
    BOOST_CHECK(responseJson[2].isMember("result"));
    BOOST_CHECK(!responseJson[0].isMember("error"));
    BOOST_CHECK(!responseJson[1].isMember("error"));
    BOOST_CHECK(!responseJson[2].isMember("error"));

    std::string id1 = responseJson[0]["result"].asString();
    std::string id2 = responseJson[1]["result"].asString();
    std::string id3 = responseJson[2]["result"].asString();

    // All IDs should be unique
    BOOST_CHECK_NE(id1, id2);
    BOOST_CHECK_NE(id1, id3);
    BOOST_CHECK_NE(id2, id3);
}

BOOST_AUTO_TEST_CASE(testMultiRequestInOneRequest)
{
    auto mockWeb3JsonRpcImpl = std::make_shared<MockWeb3JsonRpcImpl>();
    auto weakPtr = std::weak_ptr<MockWeb3JsonRpcImpl>(mockWeb3JsonRpcImpl);
    auto web3Subscribe = std::make_shared<Web3Subscribe>(weakPtr);
    mockWeb3JsonRpcImpl->setWeb3Subscribe(web3Subscribe);

    // Create multiple mock sessions
    tbb::task_arena taskArena(1);
    tbb::task_group taskGroup;
    auto session = std::make_shared<MockWsSession>(taskArena, taskGroup);


    Json::Value request;

    // Create subscription requests
    Json::Value request1, request2, request3;
    request1["jsonrpc"] = "2.0";
    request1["id"] = 1;
    request1["method"] = "eth_xxx";
    request1["params"] = Json::Value(Json::arrayValue);

    request2["jsonrpc"] = "2.0";
    request2["id"] = 2;
    request2["method"] = "eth_xxx";
    request2["params"] = Json::Value(Json::arrayValue);

    request3["jsonrpc"] = "2.0";
    request3["id"] = 3;
    request3["method"] = "eth_xxx";
    request3["params"] = Json::Value(Json::arrayValue);

    request.append(request1);
    request.append(request2);
    request.append(request3);

    // Test concurrent subscriptions
    bcos::bytes respBytes;
    auto sender = [&respBytes](bcos::bytes bytes) { respBytes = bytes; };
    mockWeb3JsonRpcImpl->onRPCRequest(request.toStyledString(), session, sender);

    Json::Value responseJson;
    Json::Reader reader;
    BOOST_CHECK(reader.parse(std::string(respBytes.begin(), respBytes.end()), responseJson));

    std::cout << "responseJson: " << responseJson.toStyledString() << std::endl;

    BOOST_CHECK(responseJson.isArray());
    BOOST_CHECK(responseJson.size() == 3);
    // BOOST_CHECK(responseJson[0].isMember("result"));
    // BOOST_CHECK(responseJson[1].isMember("result"));
    // BOOST_CHECK(responseJson[2].isMember("result"));
    BOOST_CHECK(responseJson[0].isMember("error"));
    BOOST_CHECK(responseJson[1].isMember("error"));
    BOOST_CHECK(responseJson[2].isMember("error"));
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test