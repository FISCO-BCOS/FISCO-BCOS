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
#include <librpc/Rpc.cpp>


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
        m_preCompile =
            std::make_shared<std::unordered_map<dev::Address, dev::eth::PrecompiledContract>>();
        std::string configurationPath =
            getTestPath().string() + "/fisco-bcos-data/config.group10.ini";
        m_ledgerManager = std::make_shared<LedgerManager>(m_service, m_keyPair, m_preCompile);
        m_ledgerManager->initSingleLedger<FakeLedger>(0, "", configurationPath);

        rpc = std::make_shared<Rpc>(m_ledgerManager, m_service);
    }

public:
    std::shared_ptr<Rpc> rpc;
    std::shared_ptr<std::unordered_map<dev::Address, dev::eth::PrecompiledContract>> m_preCompile;
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
    BOOST_CHECK(responseJson["result"]["extraData"][0][0].asString() == "");
    BOOST_CHECK(responseJson["result"]["gasLimit"].asString() == "0x9");
    BOOST_CHECK(responseJson["result"]["gasUsed"].asString() == "0x8");
    BOOST_CHECK(responseJson["result"]["timestamp"].asString() == "0x9");

    BOOST_CHECK(responseJson["result"]["transactions"][0]["hash"].asString() ==
                "0x43bd55476954e71447de981e8a2bbc01c6b3d585cd4e5a46321c44ba177140f8");
    BOOST_CHECK(responseJson["result"]["transactions"][0]["to"].asString() ==
                "0x1dc8def0867ea7e3626e03acee3eb40ee17251c8");
    BOOST_CHECK(responseJson["result"]["transactions"][0]["from"].asString() ==
                "0x7667b41c569604a64956d985bb2a4f8d5f2dae87");
    BOOST_CHECK(responseJson["result"]["transactions"][0]["gas"].asString() == "0xf4240");
    BOOST_CHECK(responseJson["result"]["transactions"][0]["gasPrice"].asString() == "0x0");
    BOOST_CHECK(responseJson["result"]["transactions"][0]["nonce"].asString() == "0x1be1a7d");
    BOOST_CHECK(responseJson["result"]["transactions"][0]["value"].asString() == "0x0");
    BOOST_CHECK(responseJson["result"]["transactions"][0]["blockHash"].asString() ==
                "0x067150c07dab4facb7160e075548007e067150c07dab4facb7160e075548007e");
    BOOST_CHECK(responseJson["result"]["transactions"][0]["transactionIndex"].asString() == "0x0");
    BOOST_CHECK(responseJson["result"]["transactions"][0]["blockNumber"].asString() == "0x1");

    requestJson["params"][1] = false;
    responseJson = rpc->getBlockByHash(requestJson);
    BOOST_CHECK(responseJson["result"]["transactions"][0].asString() ==
                "0x43bd55476954e71447de981e8a2bbc01c6b3d585cd4e5a46321c44ba177140f8");

    requestJson["groupId"] = "abcd";
    responseJson = rpc->getBlockByHash(requestJson);
    BOOST_CHECK(responseJson["error"]["code"].asInt() == -1);
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
                "0x7c2d5f5510f856a6c3f93de2b45c0a0ef32862bafb8328e9467f33464dbf938a");
    BOOST_CHECK(responseJson["result"]["sealer"].asString() == "0x1");
    BOOST_CHECK(responseJson["result"]["extraData"][0][0].asString() == "");
    BOOST_CHECK(responseJson["result"]["gasLimit"].asString() == "0x9");
    BOOST_CHECK(responseJson["result"]["gasUsed"].asString() == "0x8");
    BOOST_CHECK(responseJson["result"]["timestamp"].asString() == "0x9");


    BOOST_CHECK(responseJson["result"]["transactions"][0]["hash"].asString() ==
                "0x43bd55476954e71447de981e8a2bbc01c6b3d585cd4e5a46321c44ba177140f8");
    BOOST_CHECK(responseJson["result"]["transactions"][0]["to"].asString() ==
                "0x1dc8def0867ea7e3626e03acee3eb40ee17251c8");
    BOOST_CHECK(responseJson["result"]["transactions"][0]["from"].asString() ==
                "0x7667b41c569604a64956d985bb2a4f8d5f2dae87");
    BOOST_CHECK(responseJson["result"]["transactions"][0]["gas"].asString() == "0xf4240");
    BOOST_CHECK(responseJson["result"]["transactions"][0]["gasPrice"].asString() == "0x0");
    BOOST_CHECK(responseJson["result"]["transactions"][0]["nonce"].asString() == "0x1be1a7d");
    BOOST_CHECK(responseJson["result"]["transactions"][0]["value"].asString() == "0x0");
    BOOST_CHECK(responseJson["result"]["transactions"][0]["blockHash"].asString() ==
                "0x7c2d5f5510f856a6c3f93de2b45c0a0ef32862bafb8328e9467f33464dbf938a");
    BOOST_CHECK(responseJson["result"]["transactions"][0]["transactionIndex"].asString() == "0x0");
    BOOST_CHECK(responseJson["result"]["transactions"][0]["blockNumber"].asString() == "0x1");

    requestJson["params"][1] = false;
    responseJson = rpc->getBlockByNumber(requestJson);
    BOOST_CHECK(responseJson["result"]["transactions"][0].asString() ==
                "0x43bd55476954e71447de981e8a2bbc01c6b3d585cd4e5a46321c44ba177140f8");

    requestJson["groupId"] = "abcd";
    responseJson = rpc->getBlockByNumber(requestJson);
    BOOST_CHECK(responseJson["error"]["code"].asInt() == -1);
}

BOOST_AUTO_TEST_CASE(testGetTransactionByHash)
{
    Json::Value requestJson;
    requestJson["id"] = "1";
    requestJson["jsonrpc"] = "2.0";
    requestJson["groupId"] = 0;
    requestJson["method"] = "getTransactionByHash";
    std::string txHash = "0x43bd55476954e71447de981e8a2bbc01c6b3d585cd4e5a46321c44ba177140f8";

    requestJson["params"] = Json::Value(Json::arrayValue);
    requestJson["params"].append(txHash);

    Json::Value responseJson = rpc->getTransactionByHash(requestJson);

    BOOST_CHECK(responseJson["id"].asString() == "1");
    BOOST_CHECK(responseJson["jsonrpc"].asString() == "2.0");
    BOOST_CHECK(responseJson["result"]["blockNumber"].asString() == "0x1");
    BOOST_CHECK(
        responseJson["result"]["from"].asString() == "0x7667b41c569604a64956d985bb2a4f8d5f2dae87");
    BOOST_CHECK(responseJson["result"]["gas"].asString() == "0xf4240");
    BOOST_CHECK(responseJson["result"]["gasPrice"].asString() == "0x0");
    BOOST_CHECK(responseJson["result"]["hash"].asString() ==
                "0x43bd55476954e71447de981e8a2bbc01c6b3d585cd4e5a46321c44ba177140f8");
    BOOST_CHECK(responseJson["result"]["nonce"].asString() == "0x1be1a7d");
    BOOST_CHECK(
        responseJson["result"]["to"].asString() == "0x1dc8def0867ea7e3626e03acee3eb40ee17251c8");
    BOOST_CHECK(responseJson["result"]["transactionIndex"].asString() == "0x0");
    BOOST_CHECK(responseJson["result"]["value"].asString() == "0x0");

    requestJson["groupId"] = "abcd";
    responseJson = rpc->getTransactionByHash(requestJson);
    BOOST_CHECK(responseJson["error"]["code"].asInt() == -1);
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
        responseJson["result"]["from"].asString() == "0x7667b41c569604a64956d985bb2a4f8d5f2dae87");
    BOOST_CHECK(responseJson["result"]["gas"].asString() == "0xf4240");
    BOOST_CHECK(responseJson["result"]["gasPrice"].asString() == "0x0");
    BOOST_CHECK(responseJson["result"]["hash"].asString() ==
                "0x43bd55476954e71447de981e8a2bbc01c6b3d585cd4e5a46321c44ba177140f8");
    BOOST_CHECK(responseJson["result"]["nonce"].asString() == "0x1be1a7d");
    BOOST_CHECK(
        responseJson["result"]["to"].asString() == "0x1dc8def0867ea7e3626e03acee3eb40ee17251c8");
    BOOST_CHECK(responseJson["result"]["transactionIndex"].asString() == "0x0");
    BOOST_CHECK(responseJson["result"]["value"].asString() == "0x0");

    requestJson["groupId"] = "abcd";
    responseJson = rpc->getTransactionByBlockHashAndIndex(requestJson);
    BOOST_CHECK(responseJson["error"]["code"].asInt() == -1);
}

BOOST_AUTO_TEST_CASE(testGetTransactionByBlockNumberAndIndex)
{
    Json::Value requestJson;
    requestJson["id"] = "1";
    requestJson["jsonrpc"] = "2.0";
    requestJson["groupId"] = 0;
    requestJson["method"] = "getTransactionByHash";
    std::string blockNumber = "1";
    std::string index = "0";

    requestJson["params"] = Json::Value(Json::arrayValue);
    requestJson["params"].append(blockNumber);
    requestJson["params"].append(index);

    Json::Value responseJson = rpc->getTransactionByBlockNumberAndIndex(requestJson);

    BOOST_CHECK(responseJson["id"].asString() == "1");
    BOOST_CHECK(responseJson["jsonrpc"].asString() == "2.0");
    BOOST_CHECK(responseJson["result"]["blockHash"].asString() ==
                "0x7c2d5f5510f856a6c3f93de2b45c0a0ef32862bafb8328e9467f33464dbf938a");
    BOOST_CHECK(responseJson["result"]["blockNumber"].asString() == "0x1");
    BOOST_CHECK(
        responseJson["result"]["from"].asString() == "0x7667b41c569604a64956d985bb2a4f8d5f2dae87");
    BOOST_CHECK(responseJson["result"]["gas"].asString() == "0xf4240");
    BOOST_CHECK(responseJson["result"]["gasPrice"].asString() == "0x0");
    BOOST_CHECK(responseJson["result"]["hash"].asString() ==
                "0x43bd55476954e71447de981e8a2bbc01c6b3d585cd4e5a46321c44ba177140f8");
    BOOST_CHECK(responseJson["result"]["nonce"].asString() == "0x1be1a7d");
    BOOST_CHECK(
        responseJson["result"]["to"].asString() == "0x1dc8def0867ea7e3626e03acee3eb40ee17251c8");
    BOOST_CHECK(responseJson["result"]["transactionIndex"].asString() == "0x0");
    BOOST_CHECK(responseJson["result"]["value"].asString() == "0x0");

    requestJson["groupId"] = "abcd";
    responseJson = rpc->getTransactionByBlockNumberAndIndex(requestJson);
    BOOST_CHECK(responseJson["error"]["code"].asInt() == -1);
}

BOOST_AUTO_TEST_CASE(testGetTransactionReceipt)
{
    Json::Value requestJson;
    requestJson["id"] = "1";
    requestJson["jsonrpc"] = "2.0";
    requestJson["groupId"] = 0;
    requestJson["method"] = "getTransactionReceipt";
    std::string txHash = "0x43bd55476954e71447de981e8a2bbc01c6b3d585cd4e5a46321c44ba177140f8";

    requestJson["params"] = Json::Value(Json::arrayValue);
    requestJson["params"].append(txHash);

    Json::Value responseJson = rpc->getTransactionReceipt(requestJson);

    BOOST_CHECK(responseJson["id"].asString() == "1");
    BOOST_CHECK(responseJson["jsonrpc"].asString() == "2.0");
    BOOST_CHECK(responseJson["result"]["transactionHash"].asString() ==
                "0x43bd55476954e71447de981e8a2bbc01c6b3d585cd4e5a46321c44ba177140f8");
    BOOST_CHECK(responseJson["result"]["transactionIndex"].asString() == "0x0");
    BOOST_CHECK(responseJson["result"]["blockNumber"].asString() == "0x1");
    BOOST_CHECK(responseJson["result"]["blockHash"].asString() ==
                "0x067150c07dab4facb7160e075548007e067150c07dab4facb7160e075548007e");
    BOOST_CHECK(
        responseJson["result"]["from"].asString() == "0x7667b41c569604a64956d985bb2a4f8d5f2dae87");
    BOOST_CHECK(
        responseJson["result"]["to"].asString() == "0x1dc8def0867ea7e3626e03acee3eb40ee17251c8");
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
    BOOST_CHECK(responseJson["result"]["pending"][0]["blockHash"].asString() ==
                "0x067150c07dab4facb7160e075548007e067150c07dab4facb7160e075548007e");
    BOOST_CHECK(responseJson["result"]["pending"][0]["blockNumber"].asString() == "0x1");
    BOOST_CHECK(responseJson["result"]["pending"][0]["from"].asString() ==
                "0x7667b41c569604a64956d985bb2a4f8d5f2dae87");
    BOOST_CHECK(responseJson["result"]["pending"][0]["gas"].asString() == "0xf4240");
    BOOST_CHECK(responseJson["result"]["pending"][0]["gasPrice"].asString() == "0x0");
    BOOST_CHECK(responseJson["result"]["pending"][0]["nonce"].asString() == "0x1be1a7d");
    BOOST_CHECK(responseJson["result"]["pending"][0]["to"].asString() ==
                "0x1dc8def0867ea7e3626e03acee3eb40ee17251c8");
    BOOST_CHECK(responseJson["result"]["pending"][0]["transactionIndex"].asString() == "0x0");
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
        "f8ac8401be1a7d80830f4240941dc8def0867ea7e3626e03acee3eb40ee17251c880b84494e78a10000000"
        "0000"
        "000000000000003ca576d469d7aa0244071d27eb33c5629753593e00000000000000000000000000000000"
        "0000"
        "00000000000000000000000013881ba0f44a5ce4a1d1d6c2e4385a7985cdf804cb10a7fb892e9c08ff6d62"
        "657c"
        "4da01ea01d4c2af5ce505f574a320563ea9ea55003903ca5d22140155b3c2c968df050948203ea";

    requestJson["params"].append(rlpStr);

    Json::Value responseJson = rpc->sendRawTransaction(requestJson);

    BOOST_CHECK(responseJson["id"].asString() == "1");
    BOOST_CHECK(responseJson["jsonrpc"].asString() == "2.0");
    BOOST_CHECK(responseJson["result"].asString() ==
                "0x43bd55476954e71447de981e8a2bbc01c6b3d585cd4e5a46321c44ba177140f8");

    requestJson["groupId"] = "abcd";
    responseJson = rpc->sendRawTransaction(requestJson);
    BOOST_CHECK(responseJson["error"]["code"].asInt() == -1);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev
