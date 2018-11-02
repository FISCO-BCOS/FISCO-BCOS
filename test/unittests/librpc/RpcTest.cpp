/**
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 *
 * @file RpcTest.cpp
 * @author: caryliao
 * @date 2018-10-27
 */
#include "FakeModule.h"
#include <libdevcrypto/Common.h>
#include <libethcore/CommonJS.h>
#include <librpc/Rpc.h>
#include <test/tools/libutils/Common.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <test/unittests/libp2p/FakeHost.h>
#include <boost/test/unit_test.hpp>


using namespace dev;
using namespace dev::rpc;
using namespace dev::ledger;

namespace dev
{
namespace test
{
class RpcTestFixure : public TestOutputHelperFixture
{
public:
    RpcTestFixure()
    {
        FakeHost* hostPtr = createFakeHostWithSession(clientVersion, listenIp, listenPort);
        m_host = std::shared_ptr<Host>(hostPtr);
        m_p2pHandler = std::make_shared<P2PMsgHandler>();
        m_service = std::make_shared<MockService>(m_host, m_p2pHandler);
        std::string configurationPath =
            getTestPath().string() + "/fisco-bcos-data/config.group10.ini";
        m_ledgerManager = std::make_shared<LedgerManager>(m_service, m_keyPair);
        m_ledgerManager->initSingleLedger<FakeLedger>(0, "", configurationPath);

        rpc = std::make_shared<Rpc>(m_ledgerManager, m_service);
    }

public:
    std::shared_ptr<Rpc> rpc;
    KeyPair m_keyPair = KeyPair::create();
    std::shared_ptr<MockService> m_service;
    std::shared_ptr<LedgerManager> m_ledgerManager;

    std::string clientVersion = "2.0";
    std::string listenIp = "127.0.0.1";
    uint16_t listenPort = 30304;
    std::shared_ptr<P2PMsgHandler> m_p2pHandler;
    std::shared_ptr<Host> m_host;
};

BOOST_FIXTURE_TEST_SUITE(RpcTest, RpcTestFixure)

BOOST_AUTO_TEST_CASE(testConsensusPart)
{
    Json::Value requestJson;
    requestJson["id"] = "1";
    requestJson["jsonrpc"] = "2.0";
    requestJson["groupId"] = 0;
    requestJson["method"] = "blockNumber";

    Json::Value responseJson = rpc->blockNumber(requestJson);
    BOOST_CHECK(responseJson["id"].asString() == "1");
    BOOST_CHECK(responseJson["jsonrpc"].asString() == "2.0");
    BOOST_CHECK(responseJson["result"].asString() == "0x1");

    requestJson["method"] = "pbftView";
    responseJson = rpc->pbftView(requestJson);
    BOOST_CHECK(responseJson["id"].asString() == "1");
    BOOST_CHECK(responseJson["jsonrpc"].asString() == "2.0");
    BOOST_CHECK(responseJson["result"].asString() == "0x0");

    requestJson["groupId"] = "abcd";
    responseJson = rpc->blockNumber(requestJson);
    BOOST_CHECK(responseJson["error"]["code"].asInt() == -1);
    responseJson = rpc->pbftView(requestJson);
    BOOST_CHECK(responseJson["error"]["code"].asInt() == -1);
}

BOOST_AUTO_TEST_CASE(testP2pPart)
{
    Json::Value requestJson;
    requestJson["id"] = "1";
    requestJson["jsonrpc"] = "2.0";
    requestJson["groupId"] = 0;
    requestJson["method"] = "peers";

    Json::Value responseJson = rpc->peers(requestJson);
    BOOST_CHECK(responseJson["id"].asString() == "1");
    BOOST_CHECK(responseJson["jsonrpc"].asString() == "2.0");
    BOOST_CHECK(responseJson["result"][0]["NodeID"].asString() == h512(100).hex());
    BOOST_CHECK(responseJson["result"][0]["IP"].asString() == "127.0.0.1");
    BOOST_CHECK(responseJson["result"][0]["UdpPort"].asInt() == 30303);
    BOOST_CHECK(responseJson["result"][0]["TcpPort"].asInt() == 30310);
    BOOST_CHECK(responseJson["result"][0]["Topic"][0].asString() == "Topic1");

    requestJson["groupId"] = "abcd";
    responseJson = rpc->peers(requestJson);
    BOOST_CHECK(responseJson["error"]["code"].asInt() == -1);
}

BOOST_AUTO_TEST_CASE(testGetBlockByHash)
{
    Json::Value requestJson;
    requestJson["id"] = "1";
    requestJson["jsonrpc"] = "2.0";
    requestJson["groupId"] = 0;
    requestJson["method"] = "getBlockByHash";
    std::string blockHash = "0x067150c07dab4facb7160e075548007e067150c07dab4facb7160e075548007e";

    requestJson["params"] = Json::Value(Json::arrayValue);
    requestJson["params"].append(blockHash);
    requestJson["params"].append(true);

    Json::Value responseJson = rpc->getBlockByHash(requestJson);
    BOOST_CHECK(responseJson["id"].asString() == "1");
    BOOST_CHECK(responseJson["jsonrpc"].asString() == "2.0");
    BOOST_CHECK(responseJson["result"]["number"].asString() == "0x1");
    BOOST_CHECK(responseJson["result"]["hash"].asString() ==
                "0x067150c07dab4facb7160e075548007e067150c07dab4facb7160e075548007e");
    BOOST_CHECK(responseJson["result"]["sealer"].asString() == "0x1");
    BOOST_CHECK(responseJson["result"]["extraData"][0].asString() == "0x0a");
    BOOST_CHECK(responseJson["result"]["gasLimit"].asString() == "0x9");
    BOOST_CHECK(responseJson["result"]["gasUsed"].asString() == "0x8");
    BOOST_CHECK(responseJson["result"]["timestamp"].asString() == "0x9");

    BOOST_CHECK(responseJson["result"]["transactions"][0]["hash"].asString() ==
                "0x7536cf1286b5ce6c110cd4fea5c891467884240c9af366d678eb4191e1c31c6f");
    BOOST_CHECK(responseJson["result"]["transactions"][0]["to"].asString() ==
                "0xd6f1a71052366dbae2f7ab2d5d5845e77965cf0d");
    BOOST_CHECK(responseJson["result"]["transactions"][0]["from"].asString() ==
                "0x6bc952a2e4db9c0c86a368d83e9df0c6ab481102");
    BOOST_CHECK(responseJson["result"]["transactions"][0]["gas"].asString() == "0x9184e729fff");
    BOOST_CHECK(responseJson["result"]["transactions"][0]["gasPrice"].asString() == "0x174876e7ff");
    BOOST_CHECK(responseJson["result"]["transactions"][0]["nonce"].asString() ==
                "0x65f0d06e39dc3c08e32ac10a5070858962bc6c0f5760baca823f2d5582d03f");
    BOOST_CHECK(responseJson["result"]["transactions"][0]["value"].asString() == "0x0");
    BOOST_CHECK(responseJson["result"]["transactions"][0]["blockHash"].asString() ==
                "0x067150c07dab4facb7160e075548007e067150c07dab4facb7160e075548007e");
    BOOST_CHECK(responseJson["result"]["transactions"][0]["transactionIndex"].asString() == "0x0");
    BOOST_CHECK(responseJson["result"]["transactions"][0]["blockNumber"].asString() == "0x1");

    requestJson["params"][1] = false;
    responseJson = rpc->getBlockByHash(requestJson);
    BOOST_CHECK(responseJson["result"]["transactions"][0].asString() ==
                "0x7536cf1286b5ce6c110cd4fea5c891467884240c9af366d678eb4191e1c31c6f");

    requestJson["groupId"] = "abcd";
    responseJson = rpc->getBlockByHash(requestJson);
    BOOST_CHECK(responseJson["error"]["code"].asInt() == -1);

    Json::Value requestJson2;
    requestJson2["id"] = "1";
    requestJson2["jsonrpc"] = "2.0";
    requestJson2["groupId"] = 0;
    requestJson2["method"] = "getBlockByHash";
    requestJson2["params"] = Json::Value(Json::arrayValue);
    requestJson2["params"].append(blockHash);
    responseJson = rpc->getBlockByHash(requestJson2);
    BOOST_CHECK(responseJson["error"]["code"].asInt() == -2);
}
BOOST_AUTO_TEST_CASE(getBlockByNumber)
{
    Json::Value requestJson;
    requestJson["id"] = "1";
    requestJson["jsonrpc"] = "2.0";
    requestJson["groupId"] = 0;
    requestJson["method"] = "getBlockByNumber";

    requestJson["params"] = Json::Value(Json::arrayValue);
    requestJson["params"].append(1);
    requestJson["params"].append(true);

    Json::Value responseJson = rpc->getBlockByNumber(requestJson);
    BOOST_CHECK(responseJson["id"].asString() == "1");
    BOOST_CHECK(responseJson["jsonrpc"].asString() == "2.0");
    BOOST_CHECK(responseJson["result"]["number"].asString() == "0x1");
    BOOST_CHECK(responseJson["result"]["hash"].asString() ==
                "0xb079297e640eb6c6062bb6126af716592e06ce460adee73889cb43e5b5bfbc74");
    BOOST_CHECK(responseJson["result"]["sealer"].asString() == "0x1");
    BOOST_CHECK(responseJson["result"]["extraData"][0].asString() == "0x0a");
    BOOST_CHECK(responseJson["result"]["gasLimit"].asString() == "0x9");
    BOOST_CHECK(responseJson["result"]["gasUsed"].asString() == "0x8");
    BOOST_CHECK(responseJson["result"]["timestamp"].asString() == "0x9");


    BOOST_CHECK(responseJson["result"]["transactions"][0]["hash"].asString() ==
                "0x7536cf1286b5ce6c110cd4fea5c891467884240c9af366d678eb4191e1c31c6f");
    BOOST_CHECK(responseJson["result"]["transactions"][0]["to"].asString() ==
                "0xd6f1a71052366dbae2f7ab2d5d5845e77965cf0d");
    BOOST_CHECK(responseJson["result"]["transactions"][0]["from"].asString() ==
                "0x6bc952a2e4db9c0c86a368d83e9df0c6ab481102");
    BOOST_CHECK(responseJson["result"]["transactions"][0]["gas"].asString() == "0x9184e729fff");
    BOOST_CHECK(responseJson["result"]["transactions"][0]["gasPrice"].asString() == "0x174876e7ff");
    BOOST_CHECK(responseJson["result"]["transactions"][0]["nonce"].asString() ==
                "0x65f0d06e39dc3c08e32ac10a5070858962bc6c0f5760baca823f2d5582d03f");
    BOOST_CHECK(responseJson["result"]["transactions"][0]["value"].asString() == "0x0");
    BOOST_CHECK(responseJson["result"]["transactions"][0]["blockHash"].asString() ==
                "0xb079297e640eb6c6062bb6126af716592e06ce460adee73889cb43e5b5bfbc74");
    BOOST_CHECK(responseJson["result"]["transactions"][0]["transactionIndex"].asString() == "0x0");
    BOOST_CHECK(responseJson["result"]["transactions"][0]["blockNumber"].asString() == "0x1");

    requestJson["params"][1] = false;
    responseJson = rpc->getBlockByNumber(requestJson);
    BOOST_CHECK(responseJson["result"]["transactions"][0].asString() ==
                "0x7536cf1286b5ce6c110cd4fea5c891467884240c9af366d678eb4191e1c31c6f");

    requestJson["groupId"] = "abcd";
    responseJson = rpc->getBlockByNumber(requestJson);
    BOOST_CHECK(responseJson["error"]["code"].asInt() == -1);

    Json::Value requestJson2;
    requestJson2["id"] = "1";
    requestJson2["jsonrpc"] = "2.0";
    requestJson2["groupId"] = 0;
    requestJson2["method"] = "getBlockByNumber";
    requestJson2["params"] = Json::Value(Json::arrayValue);
    requestJson2["params"].append(1);
    responseJson = rpc->getBlockByNumber(requestJson2);
    BOOST_CHECK(responseJson["error"]["code"].asInt() == -2);
}

BOOST_AUTO_TEST_CASE(testGetTransactionByHash)
{
    Json::Value requestJson;
    requestJson["id"] = "1";
    requestJson["jsonrpc"] = "2.0";
    requestJson["groupId"] = 0;
    requestJson["method"] = "getTransactionByHash";
    std::string txHash = "0x7536cf1286b5ce6c110cd4fea5c891467884240c9af366d678eb4191e1c31c6f";

    requestJson["params"] = Json::Value(Json::arrayValue);
    requestJson["params"].append(txHash);

    Json::Value responseJson = rpc->getTransactionByHash(requestJson);

    BOOST_CHECK(responseJson["id"].asString() == "1");
    BOOST_CHECK(responseJson["jsonrpc"].asString() == "2.0");
    BOOST_CHECK(responseJson["result"]["blockNumber"].asString() == "0x1");
    BOOST_CHECK(
        responseJson["result"]["from"].asString() == "0x6bc952a2e4db9c0c86a368d83e9df0c6ab481102");
    BOOST_CHECK(responseJson["result"]["gas"].asString() == "0x9184e729fff");
    BOOST_CHECK(responseJson["result"]["gasPrice"].asString() == "0x174876e7ff");
    BOOST_CHECK(responseJson["result"]["hash"].asString() ==
                "0x7536cf1286b5ce6c110cd4fea5c891467884240c9af366d678eb4191e1c31c6f");
    BOOST_CHECK(responseJson["result"]["nonce"].asString() ==
                "0x65f0d06e39dc3c08e32ac10a5070858962bc6c0f5760baca823f2d5582d03f");
    BOOST_CHECK(
        responseJson["result"]["to"].asString() == "0xd6f1a71052366dbae2f7ab2d5d5845e77965cf0d");
    BOOST_CHECK(responseJson["result"]["transactionIndex"].asString() == "0x0");
    BOOST_CHECK(responseJson["result"]["value"].asString() == "0x0");

    requestJson["groupId"] = "abcd";
    responseJson = rpc->getTransactionByHash(requestJson);
    BOOST_CHECK(responseJson["error"]["code"].asInt() == -1);

    Json::Value requestJson2;
    requestJson2["id"] = "1";
    requestJson2["jsonrpc"] = "2.0";
    requestJson2["groupId"] = 0;
    requestJson2["method"] = "getTransactionByHash";
    requestJson2["params"] = Json::Value(Json::arrayValue);
    responseJson = rpc->getTransactionByHash(requestJson2);
    BOOST_CHECK(responseJson["error"]["code"].asInt() == -2);
}

BOOST_AUTO_TEST_CASE(testGetTransactionByBlockHashAndIndex)
{
    Json::Value requestJson;
    requestJson["id"] = "1";
    requestJson["jsonrpc"] = "2.0";
    requestJson["groupId"] = 0;
    requestJson["method"] = "getTransactionByHash";
    std::string blockHash = "0x067150c07dab4facb7160e075548007e067150c07dab4facb7160e075548007e";
    std::string index = "0";

    requestJson["params"] = Json::Value(Json::arrayValue);
    requestJson["params"].append(blockHash);
    requestJson["params"].append(index);

    Json::Value responseJson = rpc->getTransactionByBlockHashAndIndex(requestJson);

    BOOST_CHECK(responseJson["id"].asString() == "1");
    BOOST_CHECK(responseJson["jsonrpc"].asString() == "2.0");
    BOOST_CHECK(responseJson["result"]["blockHash"].asString() ==
                "0x067150c07dab4facb7160e075548007e067150c07dab4facb7160e075548007e");
    BOOST_CHECK(responseJson["result"]["blockNumber"].asString() == "0x1");
    BOOST_CHECK(
        responseJson["result"]["from"].asString() == "0x6bc952a2e4db9c0c86a368d83e9df0c6ab481102");
    BOOST_CHECK(responseJson["result"]["gas"].asString() == "0x9184e729fff");
    BOOST_CHECK(responseJson["result"]["gasPrice"].asString() == "0x174876e7ff");
    BOOST_CHECK(responseJson["result"]["hash"].asString() ==
                "0x7536cf1286b5ce6c110cd4fea5c891467884240c9af366d678eb4191e1c31c6f");
    BOOST_CHECK(responseJson["result"]["nonce"].asString() ==
                "0x65f0d06e39dc3c08e32ac10a5070858962bc6c0f5760baca823f2d5582d03f");
    BOOST_CHECK(
        responseJson["result"]["to"].asString() == "0xd6f1a71052366dbae2f7ab2d5d5845e77965cf0d");
    BOOST_CHECK(responseJson["result"]["transactionIndex"].asString() == "0x0");
    BOOST_CHECK(responseJson["result"]["value"].asString() == "0x0");

    requestJson["groupId"] = "abcd";
    responseJson = rpc->getTransactionByBlockHashAndIndex(requestJson);
    BOOST_CHECK(responseJson["error"]["code"].asInt() == -1);

    Json::Value requestJson2;
    requestJson2["id"] = "1";
    requestJson2["jsonrpc"] = "2.0";
    requestJson2["groupId"] = 0;
    requestJson2["method"] = "getTransactionByBlockHashAndIndex";
    requestJson2["params"] = Json::Value(Json::arrayValue);
    responseJson = rpc->getTransactionByBlockHashAndIndex(requestJson2);
    BOOST_CHECK(responseJson["error"]["code"].asInt() == -2);
}

BOOST_AUTO_TEST_CASE(testGetTransactionByBlockNumberAndIndex)
{
    Json::Value requestJson;
    requestJson["id"] = "1";
    requestJson["jsonrpc"] = "2.0";
    requestJson["groupId"] = 0;
    requestJson["method"] = "getTransactionByBlockNumberAndIndex";
    std::string blockNumber = "1";
    std::string index = "0";

    requestJson["params"] = Json::Value(Json::arrayValue);
    requestJson["params"].append(blockNumber);
    requestJson["params"].append(index);

    Json::Value responseJson = rpc->getTransactionByBlockNumberAndIndex(requestJson);

    BOOST_CHECK(responseJson["id"].asString() == "1");
    BOOST_CHECK(responseJson["jsonrpc"].asString() == "2.0");
    BOOST_CHECK(responseJson["result"]["blockHash"].asString() ==
                "0xb079297e640eb6c6062bb6126af716592e06ce460adee73889cb43e5b5bfbc74");
    BOOST_CHECK(responseJson["result"]["blockNumber"].asString() == "0x1");
    BOOST_CHECK(
        responseJson["result"]["from"].asString() == "0x6bc952a2e4db9c0c86a368d83e9df0c6ab481102");
    BOOST_CHECK(responseJson["result"]["gas"].asString() == "0x9184e729fff");
    BOOST_CHECK(responseJson["result"]["gasPrice"].asString() == "0x174876e7ff");
    BOOST_CHECK(responseJson["result"]["hash"].asString() ==
                "0x7536cf1286b5ce6c110cd4fea5c891467884240c9af366d678eb4191e1c31c6f");
    BOOST_CHECK(responseJson["result"]["nonce"].asString() ==
                "0x65f0d06e39dc3c08e32ac10a5070858962bc6c0f5760baca823f2d5582d03f");
    BOOST_CHECK(
        responseJson["result"]["to"].asString() == "0xd6f1a71052366dbae2f7ab2d5d5845e77965cf0d");
    BOOST_CHECK(responseJson["result"]["transactionIndex"].asString() == "0x0");
    BOOST_CHECK(responseJson["result"]["value"].asString() == "0x0");

    requestJson["groupId"] = "abcd";
    responseJson = rpc->getTransactionByBlockNumberAndIndex(requestJson);
    BOOST_CHECK(responseJson["error"]["code"].asInt() == -1);

    Json::Value requestJson2;
    requestJson2["id"] = "1";
    requestJson2["jsonrpc"] = "2.0";
    requestJson2["groupId"] = 0;
    requestJson2["method"] = "getTransactionByBlockNumberAndIndex";
    requestJson2["params"] = Json::Value(Json::arrayValue);
    responseJson = rpc->getTransactionByBlockNumberAndIndex(requestJson2);
    BOOST_CHECK(responseJson["error"]["code"].asInt() == -2);

    Json::Value requestJson3;
    requestJson3["id"] = "1";
    requestJson3["jsonrpc"] = "2.0";
    requestJson3["groupId"] = 0;
    requestJson3["method"] = "getTransactionByBlockNumberAndIndex";
    requestJson3["params"] = Json::Value(Json::arrayValue);
    requestJson3["params"].append("1");
    requestJson3["params"].append("2");

    responseJson = rpc->getTransactionByBlockNumberAndIndex(requestJson3);
    BOOST_CHECK(responseJson["error"]["code"].asInt() == -2);
}

BOOST_AUTO_TEST_CASE(testGetTransactionReceipt)
{
    Json::Value requestJson;
    requestJson["id"] = "1";
    requestJson["jsonrpc"] = "2.0";
    requestJson["groupId"] = 0;
    requestJson["method"] = "getTransactionReceipt";
    std::string txHash = "0x7536cf1286b5ce6c110cd4fea5c891467884240c9af366d678eb4191e1c31c6f";

    requestJson["params"] = Json::Value(Json::arrayValue);
    requestJson["params"].append(txHash);

    Json::Value responseJson = rpc->getTransactionReceipt(requestJson);

    BOOST_CHECK(responseJson["id"].asString() == "1");
    BOOST_CHECK(responseJson["jsonrpc"].asString() == "2.0");
    BOOST_CHECK(responseJson["result"]["transactionHash"].asString() ==
                "0x7536cf1286b5ce6c110cd4fea5c891467884240c9af366d678eb4191e1c31c6f");
    BOOST_CHECK(responseJson["result"]["transactionIndex"].asString() == "0x0");
    BOOST_CHECK(responseJson["result"]["blockNumber"].asString() == "0x1");
    BOOST_CHECK(responseJson["result"]["blockHash"].asString() ==
                "0x067150c07dab4facb7160e075548007e067150c07dab4facb7160e075548007e");
    BOOST_CHECK(
        responseJson["result"]["from"].asString() == "0x6bc952a2e4db9c0c86a368d83e9df0c6ab481102");
    BOOST_CHECK(
        responseJson["result"]["to"].asString() == "0xd6f1a71052366dbae2f7ab2d5d5845e77965cf0d");
    BOOST_CHECK(responseJson["result"]["gasUsed"].asString() == "0x8");
    BOOST_CHECK(responseJson["result"]["contractAddress"].asString() ==
                "0x0000000000000000000000000000000000001000");
    BOOST_CHECK(responseJson["result"]["logs"][0]["address"].asString() ==
                "0x0000000000000000000000000000000000002000");
    BOOST_CHECK(responseJson["result"]["logs"][0]["data"].asString() == "0x");
    BOOST_CHECK(responseJson["result"]["logs"][0]["topics"].asString() == "0x[]");
    BOOST_CHECK(responseJson["result"]["status"].asString() == "0x0");

    requestJson["groupId"] = "abcd";
    responseJson = rpc->getTransactionReceipt(requestJson);
    BOOST_CHECK(responseJson["error"]["code"].asInt() == -1);

    Json::Value requestJson2;
    requestJson2["id"] = "1";
    requestJson2["jsonrpc"] = "2.0";
    requestJson2["groupId"] = 0;
    requestJson2["method"] = "getTransactionReceipt";
    requestJson2["params"] = Json::Value(Json::arrayValue);
    responseJson = rpc->getTransactionReceipt(requestJson2);
    BOOST_CHECK(responseJson["error"]["code"].asInt() == -2);
}
BOOST_AUTO_TEST_CASE(testPendingTransactions)
{
    Json::Value requestJson;
    requestJson["id"] = "1";
    requestJson["jsonrpc"] = "2.0";
    requestJson["groupId"] = 0;
    requestJson["method"] = "pendingTransactions";

    Json::Value responseJson = rpc->pendingTransactions(requestJson);

    BOOST_CHECK(responseJson["id"].asString() == "1");
    BOOST_CHECK(responseJson["jsonrpc"].asString() == "2.0");
    BOOST_CHECK(responseJson["result"]["pending"][0]["from"].asString() ==
                "0x6bc952a2e4db9c0c86a368d83e9df0c6ab481102");
    BOOST_CHECK(responseJson["result"]["pending"][0]["gas"].asString() == "0x9184e729fff");
    BOOST_CHECK(responseJson["result"]["pending"][0]["gasPrice"].asString() == "0x174876e7ff");
    BOOST_CHECK(responseJson["result"]["pending"][0]["nonce"].asString() ==
                "0x65f0d06e39dc3c08e32ac10a5070858962bc6c0f5760baca823f2d5582d03f");
    BOOST_CHECK(responseJson["result"]["pending"][0]["to"].asString() ==
                "0xd6f1a71052366dbae2f7ab2d5d5845e77965cf0d");
    BOOST_CHECK(responseJson["result"]["pending"][0]["value"].asString() == "0x0");

    requestJson["groupId"] = "abcd";
    responseJson = rpc->pendingTransactions(requestJson);
    BOOST_CHECK(responseJson["error"]["code"].asInt() == -1);
}

BOOST_AUTO_TEST_CASE(testCall)
{
    Json::Value requestJson;
    requestJson["id"] = "1";
    requestJson["jsonrpc"] = "2.0";
    requestJson["groupId"] = 0;
    requestJson["method"] = "call";
    requestJson["params"]["from"] = "0x" + toHex(toAddress(KeyPair::create().pub()));
    requestJson["params"]["to"] = "0x" + toHex(toAddress(KeyPair::create().pub()));
    requestJson["params"]["value"] = "0x1";
    requestJson["params"]["gas"] = "0x12";
    requestJson["params"]["gasPrice"] = "0x1";
    requestJson["params"]["data"] = "0x3";
    requestJson["params"]["code"] = "0x3";
    requestJson["params"]["randomid"] = "0x4";
    requestJson["params"]["blockLimit"] = "0x5";

    Json::Value responseJson = rpc->call(requestJson);

    BOOST_CHECK(responseJson["id"].asString() == "1");
    BOOST_CHECK(responseJson["jsonrpc"].asString() == "2.0");
    BOOST_CHECK(responseJson["result"].asString() == "0x");

    requestJson["groupId"] = "abcd";
    responseJson = rpc->call(requestJson);
    BOOST_CHECK(responseJson["error"]["code"].asInt() == -1);
}

BOOST_AUTO_TEST_CASE(testSendRawTransaction)
{
    Json::Value requestJson;
    requestJson["id"] = "1";
    requestJson["jsonrpc"] = "2.0";
    requestJson["groupId"] = 0;
    requestJson["method"] = "sendRawTransaction";
    requestJson["params"] = Json::Value(Json::arrayValue);
    std::string rlpStr =
        "f8ef9f65f0d06e39dc3c08e32ac10a5070858962bc6c0f5760baca823f2d5582d03f85174876e7ff"
        "8609184e729fff82020394d6f1a71052366dbae2f7ab2d5d5845e77965cf0d80b86448f85bce000000"
        "000000000000000000000000000000000000000000000000000000001bf5bd8a9e7ba8b936ea704292"
        "ff4aaa5797bf671fdc8526dcd159f23c1f5a05f44e9fa862834dc7cb4541558f2b4961dc39eaaf0af7"
        "f7395028658d0e01b86a371ca00b2b3fabd8598fefdda4efdb54f626367fc68e1735a8047f0f1c4f84"
        "0255ca1ea0512500bc29f4cfe18ee1c88683006d73e56c934100b8abf4d2334560e1d2f75e";

    requestJson["params"].append(rlpStr);

    Json::Value responseJson = rpc->sendRawTransaction(requestJson);

    BOOST_CHECK(responseJson["id"].asString() == "1");
    BOOST_CHECK(responseJson["jsonrpc"].asString() == "2.0");
    BOOST_CHECK(responseJson["result"].asString() ==
                "0x7536cf1286b5ce6c110cd4fea5c891467884240c9af366d678eb4191e1c31c6f");

    requestJson["groupId"] = "abcd";
    responseJson = rpc->sendRawTransaction(requestJson);
    BOOST_CHECK(responseJson["error"]["code"].asInt() == -1);

    Json::Value requestJson2;
    requestJson2["id"] = "1";
    requestJson2["jsonrpc"] = "2.0";
    requestJson2["groupId"] = 0;
    requestJson2["method"] = "sendRawTransaction";
    requestJson2["params"] = Json::Value(Json::arrayValue);
    responseJson = rpc->sendRawTransaction(requestJson2);
    BOOST_CHECK(responseJson["error"]["code"].asInt() == -2);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev
