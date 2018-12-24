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

#include <jsonrpccpp/common/exception.h>
#include <libdevcrypto/Common.h>
#include <libethcore/CommonJS.h>
#include <librpc/Rpc.h>
#include <test/tools/libutils/Common.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>

using namespace jsonrpc;
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
        m_service = std::make_shared<FakesService>();
        std::string configurationPath =
            getTestPath().string() + "/fisco-bcos-data/config.group10.ini";
        m_ledgerManager = std::make_shared<LedgerManager>(m_service, m_keyPair);
        m_ledgerManager->initSingleLedger<FakeLedger>(groupId, "", configurationPath);

        rpc = std::make_shared<Rpc>(m_ledgerManager, m_service);
    }

public:
    std::shared_ptr<Rpc> rpc;
    KeyPair m_keyPair = KeyPair::create();
    std::shared_ptr<FakesService> m_service;
    std::shared_ptr<LedgerManager> m_ledgerManager;

    std::string clientVersion = "2.0";
    std::string listenIp = "127.0.0.1";
    uint16_t listenPort = 30304;
    std::shared_ptr<dev::network::Host> m_host;
    dev::GROUP_ID groupId = 1;
    dev::GROUP_ID invalidGroup = 2;
};

BOOST_FIXTURE_TEST_SUITE(RpcTest, RpcTestFixure)

BOOST_AUTO_TEST_CASE(testConsensusPart)
{
    std::string blockNumber = rpc->getBlockNumber(groupId);
    BOOST_CHECK(blockNumber == "0x0");
    BOOST_CHECK_THROW(rpc->getBlockNumber(invalidGroup), JsonRpcException);

    std::string pbftView = rpc->getPbftView(groupId);
    BOOST_CHECK(pbftView == "0x0");
    BOOST_CHECK_THROW(rpc->getPbftView(invalidGroup), JsonRpcException);

    Json::Value status = rpc->getConsensusStatus(groupId);
    BOOST_CHECK(status.size() == 8);
    BOOST_CHECK_THROW(rpc->getConsensusStatus(invalidGroup), JsonRpcException);

    Json::Value minerList = rpc->getMinerList(groupId);
    BOOST_CHECK(minerList.size() == 1);
    BOOST_CHECK_THROW(rpc->getMinerList(invalidGroup), JsonRpcException);

    Json::Value observerList = rpc->getObserverList(groupId);
    BOOST_CHECK(observerList.size() == 0);
    BOOST_CHECK_THROW(rpc->getObserverList(invalidGroup), JsonRpcException);
}

BOOST_AUTO_TEST_CASE(testSyncPart)
{
    Json::Value status = rpc->getSyncStatus(groupId);
    BOOST_CHECK(status.size() == 9);
    BOOST_CHECK_THROW(rpc->getSyncStatus(invalidGroup), JsonRpcException);
}

BOOST_AUTO_TEST_CASE(testP2pPart)
{
    std::string s = rpc->getClientVersion();
    BOOST_CHECK(s == "FISCO BCOS:2.0");

    Json::Value response = rpc->getPeers();
    BOOST_CHECK(response[0]["NodeID"].asString() != "");
    BOOST_CHECK(response[0]["IPAndPort"].asString() == "127.0.0.1:30310");
    BOOST_CHECK(response[0]["Topic"][0].asString() == "Topic1");

    response = rpc->getGroupPeers(groupId);
    BOOST_CHECK(response.size() == 0);

    response = rpc->getGroupList();
    BOOST_CHECK(response.size() == 1);
}

BOOST_AUTO_TEST_CASE(testGetBlockByHash)
{
    std::string blockHash = "0x067150c07dab4facb7160e075548007e067150c07dab4facb7160e075548007e";
    Json::Value response = rpc->getBlockByHash(groupId, blockHash, true);

    BOOST_CHECK(response["number"].asString() == "0x0");
    BOOST_CHECK(response["hash"].asString() ==
                "0x067150c07dab4facb7160e075548007e067150c07dab4facb7160e075548007e");
    BOOST_CHECK(response["sealer"].asString() == "0x1");
    BOOST_CHECK(response["extraData"][0].asString() == "0x0a");
    BOOST_CHECK(response["gasLimit"].asString() == "0x9");
    BOOST_CHECK(response["gasUsed"].asString() == "0x8");
    BOOST_CHECK(response["timestamp"].asString() == "0x9");

    BOOST_CHECK(response["transactions"][0]["hash"].asString() ==
                "0x7536cf1286b5ce6c110cd4fea5c891467884240c9af366d678eb4191e1c31c6f");
    BOOST_CHECK(response["transactions"][0]["to"].asString() ==
                "0xd6f1a71052366dbae2f7ab2d5d5845e77965cf0d");
    BOOST_CHECK(response["transactions"][0]["from"].asString() ==
                "0x6bc952a2e4db9c0c86a368d83e9df0c6ab481102");
    BOOST_CHECK(response["transactions"][0]["gas"].asString() == "0x9184e729fff");
    BOOST_CHECK(response["transactions"][0]["gasPrice"].asString() == "0x174876e7ff");
    BOOST_CHECK(response["transactions"][0]["nonce"].asString() ==
                "0x65f0d06e39dc3c08e32ac10a5070858962bc6c0f5760baca823f2d5582d03f");
    BOOST_CHECK(response["transactions"][0]["value"].asString() == "0x0");
    BOOST_CHECK(response["transactions"][0]["blockHash"].asString() ==
                "0x067150c07dab4facb7160e075548007e067150c07dab4facb7160e075548007e");
    BOOST_CHECK(response["transactions"][0]["transactionIndex"].asString() == "0x0");
    BOOST_CHECK(response["transactions"][0]["blockNumber"].asString() == "0x0");

    response = rpc->getBlockByHash(groupId, blockHash, false);
    BOOST_CHECK(response["transactions"][0].asString() ==
                "0x7536cf1286b5ce6c110cd4fea5c891467884240c9af366d678eb4191e1c31c6f");

    BOOST_CHECK_THROW(rpc->getBlockByHash(invalidGroup, blockHash, false), JsonRpcException);
    blockHash = "0x067150c07dab4facb7160e075548007e067150c07dab4facb7160e0755480070";
    BOOST_CHECK_THROW(rpc->getBlockByHash(groupId, blockHash, false), JsonRpcException);
}
BOOST_AUTO_TEST_CASE(getBlockByNumber)
{
    Json::Value response = rpc->getBlockByNumber(groupId, "0x0", true);

    BOOST_CHECK(response["number"].asString() == "0x0");
    BOOST_CHECK(response["hash"].asString() ==
                "0x2d6d365ccaa099b44a85edac6e0f40666f707e1324db375eee52ed3227640a03");
    BOOST_CHECK(response["sealer"].asString() == "0x1");
    BOOST_CHECK(response["extraData"][0].asString() == "0x0a");
    BOOST_CHECK(response["gasLimit"].asString() == "0x9");
    BOOST_CHECK(response["gasUsed"].asString() == "0x8");
    BOOST_CHECK(response["timestamp"].asString() == "0x9");

    BOOST_CHECK(response["transactions"][0]["hash"].asString() ==
                "0x7536cf1286b5ce6c110cd4fea5c891467884240c9af366d678eb4191e1c31c6f");
    BOOST_CHECK(response["transactions"][0]["to"].asString() ==
                "0xd6f1a71052366dbae2f7ab2d5d5845e77965cf0d");
    BOOST_CHECK(response["transactions"][0]["from"].asString() ==
                "0x6bc952a2e4db9c0c86a368d83e9df0c6ab481102");
    BOOST_CHECK(response["transactions"][0]["gas"].asString() == "0x9184e729fff");
    BOOST_CHECK(response["transactions"][0]["gasPrice"].asString() == "0x174876e7ff");
    BOOST_CHECK(response["transactions"][0]["nonce"].asString() ==
                "0x65f0d06e39dc3c08e32ac10a5070858962bc6c0f5760baca823f2d5582d03f");
    BOOST_CHECK(response["transactions"][0]["value"].asString() == "0x0");
    BOOST_CHECK(response["transactions"][0]["blockHash"].asString() ==
                "0x2d6d365ccaa099b44a85edac6e0f40666f707e1324db375eee52ed3227640a03");
    BOOST_CHECK(response["transactions"][0]["transactionIndex"].asString() == "0x0");
    BOOST_CHECK(response["transactions"][0]["blockNumber"].asString() == "0x0");

    response = rpc->getBlockByNumber(groupId, "0x0", false);
    BOOST_CHECK(response["transactions"][0].asString() ==
                "0x7536cf1286b5ce6c110cd4fea5c891467884240c9af366d678eb4191e1c31c6f");

    BOOST_CHECK_THROW(rpc->getBlockByNumber(invalidGroup, "0x0", false), JsonRpcException);
}

BOOST_AUTO_TEST_CASE(testGetBlockHashByNumber)
{
    std::string blockNumber = "0x0";
    std::string response = rpc->getBlockHashByNumber(groupId, blockNumber);
    BOOST_CHECK(response == "0x067150c07dab4facb7160e075548007e067150c07dab4facb7160e075548007e");

    BOOST_CHECK_THROW(rpc->getBlockHashByNumber(invalidGroup, blockNumber), JsonRpcException);
}

BOOST_AUTO_TEST_CASE(testGetTransactionByHash)
{
    std::string txHash = "0x7536cf1286b5ce6c110cd4fea5c891467884240c9af366d678eb4191e1c31c6f";
    Json::Value response = rpc->getTransactionByHash(groupId, txHash);

    BOOST_CHECK(response["blockNumber"].asString() == "0x0");
    BOOST_CHECK(response["from"].asString() == "0x6bc952a2e4db9c0c86a368d83e9df0c6ab481102");
    BOOST_CHECK(response["gas"].asString() == "0x9184e729fff");
    BOOST_CHECK(response["gasPrice"].asString() == "0x174876e7ff");
    BOOST_CHECK(response["hash"].asString() ==
                "0x7536cf1286b5ce6c110cd4fea5c891467884240c9af366d678eb4191e1c31c6f");
    BOOST_CHECK(response["nonce"].asString() ==
                "0x65f0d06e39dc3c08e32ac10a5070858962bc6c0f5760baca823f2d5582d03f");
    BOOST_CHECK(response["to"].asString() == "0xd6f1a71052366dbae2f7ab2d5d5845e77965cf0d");
    BOOST_CHECK(response["transactionIndex"].asString() == "0x0");
    BOOST_CHECK(response["value"].asString() == "0x0");

    BOOST_CHECK_THROW(rpc->getTransactionByHash(invalidGroup, txHash), JsonRpcException);
}

BOOST_AUTO_TEST_CASE(testGetTransactionByBlockHashAndIndex)
{
    std::string blockHash = "0x067150c07dab4facb7160e075548007e067150c07dab4facb7160e075548007e";
    std::string index = "0x0";
    Json::Value response = rpc->getTransactionByBlockHashAndIndex(groupId, blockHash, index);

    BOOST_CHECK(response["blockHash"].asString() ==
                "0x067150c07dab4facb7160e075548007e067150c07dab4facb7160e075548007e");
    BOOST_CHECK(response["blockNumber"].asString() == "0x0");
    BOOST_CHECK(response["from"].asString() == "0x6bc952a2e4db9c0c86a368d83e9df0c6ab481102");
    BOOST_CHECK(response["gas"].asString() == "0x9184e729fff");
    BOOST_CHECK(response["gasPrice"].asString() == "0x174876e7ff");
    BOOST_CHECK(response["hash"].asString() ==
                "0x7536cf1286b5ce6c110cd4fea5c891467884240c9af366d678eb4191e1c31c6f");
    BOOST_CHECK(response["nonce"].asString() ==
                "0x65f0d06e39dc3c08e32ac10a5070858962bc6c0f5760baca823f2d5582d03f");
    BOOST_CHECK(response["to"].asString() == "0xd6f1a71052366dbae2f7ab2d5d5845e77965cf0d");
    BOOST_CHECK(response["transactionIndex"].asString() == "0x0");
    BOOST_CHECK(response["value"].asString() == "0x0");

    BOOST_CHECK_THROW(
        rpc->getTransactionByBlockHashAndIndex(invalidGroup, blockHash, index), JsonRpcException);
    blockHash = "0x067150c07dab4facb7160e075548007e067150c07dab4facb7160e0755480070";
    BOOST_CHECK_THROW(
        rpc->getTransactionByBlockHashAndIndex(groupId, blockHash, index), JsonRpcException);
    blockHash = "0x067150c07dab4facb7160e075548007e067150c07dab4facb7160e075548007e";
    index = "0x1";
    BOOST_CHECK_THROW(
        rpc->getTransactionByBlockHashAndIndex(invalidGroup, blockHash, index), JsonRpcException);
}

BOOST_AUTO_TEST_CASE(testGetTransactionByBlockNumberAndIndex)
{
    std::string blockNumber = "1";
    std::string index = "0x0";
    Json::Value response = rpc->getTransactionByBlockNumberAndIndex(groupId, blockNumber, index);

    BOOST_CHECK(response["blockHash"].asString() ==
                "0x2d6d365ccaa099b44a85edac6e0f40666f707e1324db375eee52ed3227640a03");
    BOOST_CHECK(response["blockNumber"].asString() == "0x0");
    BOOST_CHECK(response["from"].asString() == "0x6bc952a2e4db9c0c86a368d83e9df0c6ab481102");
    BOOST_CHECK(response["gas"].asString() == "0x9184e729fff");
    BOOST_CHECK(response["gasPrice"].asString() == "0x174876e7ff");
    BOOST_CHECK(response["hash"].asString() ==
                "0x7536cf1286b5ce6c110cd4fea5c891467884240c9af366d678eb4191e1c31c6f");
    BOOST_CHECK(response["nonce"].asString() ==
                "0x65f0d06e39dc3c08e32ac10a5070858962bc6c0f5760baca823f2d5582d03f");
    BOOST_CHECK(response["to"].asString() == "0xd6f1a71052366dbae2f7ab2d5d5845e77965cf0d");
    BOOST_CHECK(response["transactionIndex"].asString() == "0x0");
    BOOST_CHECK(response["value"].asString() == "0x0");

    BOOST_CHECK_THROW(rpc->getTransactionByBlockNumberAndIndex(invalidGroup, blockNumber, index),
        JsonRpcException);
    index = "0x1";
    BOOST_CHECK_THROW(
        rpc->getTransactionByBlockHashAndIndex(groupId, blockNumber, index), JsonRpcException);
}

BOOST_AUTO_TEST_CASE(testGetTransactionReceipt)
{
    std::string txHash = "0x7536cf1286b5ce6c110cd4fea5c891467884240c9af366d678eb4191e1c31c6f";
    Json::Value response = rpc->getTransactionReceipt(groupId, txHash);

    BOOST_CHECK(response["transactionHash"].asString() ==
                "0x7536cf1286b5ce6c110cd4fea5c891467884240c9af366d678eb4191e1c31c6f");
    BOOST_CHECK(response["transactionIndex"].asString() == "0x0");
    BOOST_CHECK(response["blockNumber"].asString() == "0x0");
    BOOST_CHECK(response["blockHash"].asString() ==
                "0x067150c07dab4facb7160e075548007e067150c07dab4facb7160e075548007e");
    BOOST_CHECK(response["from"].asString() == "0x6bc952a2e4db9c0c86a368d83e9df0c6ab481102");
    BOOST_CHECK(response["to"].asString() == "0xd6f1a71052366dbae2f7ab2d5d5845e77965cf0d");
    BOOST_CHECK(response["gasUsed"].asString() == "0x8");
    BOOST_CHECK(
        response["contractAddress"].asString() == "0x0000000000000000000000000000000000001000");
    BOOST_CHECK(
        response["logs"][0]["address"].asString() == "0x0000000000000000000000000000000000002000");
    BOOST_CHECK(response["logs"][0]["data"].asString() == "0x");
    BOOST_CHECK(response["logs"][0]["topics"].size() == 0);
    BOOST_CHECK(response["status"].asString() == "0x0");

    BOOST_CHECK_THROW(rpc->getTransactionReceipt(invalidGroup, txHash), JsonRpcException);
}
BOOST_AUTO_TEST_CASE(testGetpendingTransactions)
{
    Json::Value response = rpc->getPendingTransactions(groupId);

    BOOST_CHECK(response[0]["from"].asString() == "0x6bc952a2e4db9c0c86a368d83e9df0c6ab481102");
    BOOST_CHECK(response[0]["gas"].asString() == "0x9184e729fff");
    BOOST_CHECK(response[0]["gasPrice"].asString() == "0x174876e7ff");
    BOOST_CHECK(response[0]["nonce"].asString() ==
                "0x65f0d06e39dc3c08e32ac10a5070858962bc6c0f5760baca823f2d5582d03f");
    BOOST_CHECK(response[0]["to"].asString() == "0xd6f1a71052366dbae2f7ab2d5d5845e77965cf0d");
    BOOST_CHECK(response[0]["value"].asString() == "0x0");

    BOOST_CHECK_THROW(rpc->getPendingTransactions(invalidGroup), JsonRpcException);
}

BOOST_AUTO_TEST_CASE(testGetCode)
{
    std::string address = "0xa94f5374fce5edbc8e2a8697c15331677e6ebf0b";
    std::string response = rpc->getCode(groupId, address);
    BOOST_CHECK(response == "0x");

    BOOST_CHECK_THROW(rpc->getCode(invalidGroup, address), JsonRpcException);
}

BOOST_AUTO_TEST_CASE(testGetTotalTransactionCount)
{
    Json::Value response = rpc->getTotalTransactionCount(groupId);
    BOOST_CHECK(response["txSum"].asString() == "0x0");
    BOOST_CHECK(response["blockNumber"].asString() == "0x0");

    BOOST_CHECK_THROW(rpc->getTotalTransactionCount(invalidGroup), JsonRpcException);
}

BOOST_AUTO_TEST_CASE(testCall)
{
    Json::Value request;
    request["from"] = "0x" + toHex(toAddress(KeyPair::create().pub()));
    request["to"] = "0x" + toHex(toAddress(KeyPair::create().pub()));
    request["value"] = "0x1";
    request["gas"] = "0x12";
    request["gasPrice"] = "0x1";
    request["data"] = "0x3";
    request["code"] = "0x3";
    request["randomid"] = "0x4";
    request["blockLimit"] = "0x5";
    Json::Value response = rpc->call(groupId, request);

    BOOST_CHECK(response["currentBlockNumber"].asString() == "0x0");
    BOOST_CHECK(response["output"].asString() == "0x");

    BOOST_CHECK_THROW(rpc->call(invalidGroup, request), JsonRpcException);
}

BOOST_AUTO_TEST_CASE(testSendRawTransaction)
{
    std::string rlpStr =
        "f8ef9f65f0d06e39dc3c08e32ac10a5070858962bc6c0f5760baca823f2d5582d03f85174876e7ff"
        "8609184e729fff82020394d6f1a71052366dbae2f7ab2d5d5845e77965cf0d80b86448f85bce000000"
        "000000000000000000000000000000000000000000000000000000001bf5bd8a9e7ba8b936ea704292"
        "ff4aaa5797bf671fdc8526dcd159f23c1f5a05f44e9fa862834dc7cb4541558f2b4961dc39eaaf0af7"
        "f7395028658d0e01b86a371ca00b2b3fabd8598fefdda4efdb54f626367fc68e1735a8047f0f1c4f84"
        "0255ca1ea0512500bc29f4cfe18ee1c88683006d73e56c934100b8abf4d2334560e1d2f75e";
    std::string response = rpc->sendRawTransaction(groupId, rlpStr);

    BOOST_CHECK(response == "0x7536cf1286b5ce6c110cd4fea5c891467884240c9af366d678eb4191e1c31c6f");

    BOOST_CHECK_THROW(rpc->sendRawTransaction(invalidGroup, rlpStr), JsonRpcException);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev
