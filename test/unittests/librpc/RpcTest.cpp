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
#include <libinitializer/LedgerInitializer.h>
#include <librpc/Rpc.h>
#include <test/tools/libutils/Common.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>

using namespace jsonrpc;
using namespace dev;
using namespace dev::rpc;
using namespace dev::ledger;
using namespace dev::initializer;
using namespace dev::test;

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
            getTestPath().string() + "/fisco-bcos-data/group.10.genesis";

        m_ledgerManager = std::make_shared<LedgerManager>();
        m_ledgerInitializer = std::make_shared<LedgerInitializer>();
        m_ledgerInitializer->setLedgerManager(m_ledgerManager);
        std::shared_ptr<LedgerInterface> ledger =
            std::make_shared<FakeLedger>(m_service, groupId, m_keyPair, "", configurationPath);
        auto ledgerParams = std::make_shared<LedgerParam>();
        ledgerParams->init(configurationPath);
        ledger->initLedger(ledgerParams);
        m_ledgerManager->insertLedger(groupId, ledger);
        rpc = std::make_shared<Rpc>(m_ledgerInitializer, m_service);
    }

public:
    std::shared_ptr<Rpc> rpc;
    KeyPair m_keyPair = KeyPair::create();
    std::shared_ptr<FakesService> m_service;
    std::shared_ptr<LedgerManager> m_ledgerManager;
    LedgerInitializer::Ptr m_ledgerInitializer;

    std::string clientVersion = "2.0";
    std::string listenIp = "127.0.0.1";
    uint16_t listenPort = 30304;
    std::shared_ptr<dev::network::Host> m_host;
    dev::GROUP_ID groupId = 1;
    dev::GROUP_ID invalidGroup = 2;
};

class SM_RpcTestFixure : public SM_CryptoTestFixture, public RpcTestFixure
{
public:
    SM_RpcTestFixure() : SM_CryptoTestFixture(), RpcTestFixure() {}
};

BOOST_FIXTURE_TEST_SUITE(SM_RpcTest, SM_RpcTestFixure)
BOOST_AUTO_TEST_CASE(SM_testConsensusPart)
{
    std::string blockNumber = rpc->getBlockNumber(groupId);
    BOOST_CHECK(blockNumber == "0x0");
    BOOST_CHECK_THROW(rpc->getBlockNumber(invalidGroup), JsonRpcException);

    std::string pbftView = rpc->getPbftView(groupId);
    BOOST_CHECK(pbftView == "0x0");
    BOOST_CHECK_THROW(rpc->getPbftView(invalidGroup), JsonRpcException);

    Json::Value status = rpc->getConsensusStatus(groupId);
    BOOST_CHECK_THROW(rpc->getConsensusStatus(invalidGroup), JsonRpcException);
}

BOOST_AUTO_TEST_CASE(SM_testSyncPart)
{
    Json::Value status = rpc->getSyncStatus(groupId);
    BOOST_CHECK(status.size() == 9);
    BOOST_CHECK_THROW(rpc->getSyncStatus(invalidGroup), JsonRpcException);
}

BOOST_AUTO_TEST_CASE(SM_testP2pPart)
{
    Json::Value version = rpc->getClientVersion();
    BOOST_CHECK(version.size() != 0);

    Json::Value response = rpc->getPeers(groupId);
    BOOST_CHECK(response[0]["NodeID"].asString() != "");
    BOOST_CHECK(response[0]["IPAndPort"].asString() == "127.0.0.1:30310");
    BOOST_CHECK(response[0]["Topic"][0].asString() == "Topic1");

    response = rpc->getGroupPeers(groupId);
    BOOST_CHECK(response.size() == 1);

    response = rpc->getGroupList();
    BOOST_CHECK(response.size() == 0);
}

BOOST_AUTO_TEST_CASE(SM_testGetBlockByHash)
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

    if (g_BCOSConfig.version() == RC1_VERSION)
    {
        BOOST_CHECK_EQUAL(response["transactions"][0]["hash"].asString(),
            "0x9319b663d2982b6d3894b455757843b5b68ca84a94356eebccdfa6d1eb34d680");
        BOOST_CHECK(response["transactions"][0]["to"].asString() ==
                    "0xd6f1a71052366dbae2f7ab2d5d5845e77965cf0d");
        BOOST_CHECK_EQUAL(response["transactions"][0]["from"].asString(),
            "0xb5e55058e234c6a58cde2d0f128546d31ea80c1d");
        BOOST_CHECK(response["transactions"][0]["gas"].asString() == "0x9184e729fff");
        BOOST_CHECK(response["transactions"][0]["gasPrice"].asString() == "0x174876e7ff");
        BOOST_CHECK_EQUAL(response["transactions"][0]["nonce"].asString(),
            "0x65f0d06e39dc3c08e32ac10a5070858962bc6c0f5760baca823f2d5582d144");
    }
    else
    {
        BOOST_CHECK_EQUAL(response["transactions"][0]["hash"].asString(),
            "0xe1054f38907ab3bef251caf8cbd8b25e779a40445b8dd4fcf15fce917d3e4ebe");
        BOOST_CHECK_EQUAL(response["transactions"][0]["to"].asString(),
            "0xbab78cea98af2320ad4ee81bba8a7473e0c8c48d");
        BOOST_CHECK_EQUAL(response["transactions"][0]["from"].asString(),
            "0x1a3fc157bd47c3fc2e260b34abbf481730d0f80f");
        BOOST_CHECK_EQUAL(response["transactions"][0]["gas"].asString(), "0xb2d05e00");
        BOOST_CHECK_EQUAL(response["transactions"][0]["gasPrice"].asString(), "0x11e1a300");
        BOOST_CHECK_EQUAL(response["transactions"][0]["nonce"].asString(),
            "0x3eebc46c9c0e3b84799097c5a6ccd6657a9295c11270407707366d0750fcd59");
    }

    BOOST_CHECK(response["transactions"][0]["value"].asString() == "0x0");
    BOOST_CHECK(response["transactions"][0]["blockHash"].asString() ==
                "0x067150c07dab4facb7160e075548007e067150c07dab4facb7160e075548007e");
    BOOST_CHECK(response["transactions"][0]["transactionIndex"].asString() == "0x0");
    BOOST_CHECK(response["transactions"][0]["blockNumber"].asString() == "0x0");

    response = rpc->getBlockByHash(groupId, blockHash, false);
    if (g_BCOSConfig.version() == RC1_VERSION)
    {
        BOOST_CHECK_EQUAL(response["transactions"][0].asString(),
            "0x9319b663d2982b6d3894b455757843b5b68ca84a94356eebccdfa6d1eb34d680");
    }
    else
    {
        BOOST_CHECK_EQUAL(response["transactions"][0].asString(),
            "0xe1054f38907ab3bef251caf8cbd8b25e779a40445b8dd4fcf15fce917d3e4ebe");
    }

    BOOST_CHECK_THROW(rpc->getBlockByHash(invalidGroup, blockHash, false), JsonRpcException);
    blockHash = "0x067150c07dab4facb7160e075548007e067150c07dab4facb7160e0755480070";
    BOOST_CHECK_THROW(rpc->getBlockByHash(groupId, blockHash, false), JsonRpcException);
}
BOOST_AUTO_TEST_CASE(SM_getBlockByNumber)
{
    Json::Value response = rpc->getBlockByNumber(groupId, "0x0", true);

    BOOST_CHECK(response["number"].asString() == "0x0");
    BOOST_CHECK_EQUAL(response["hash"].asString(),
        "0xa5529e6035e28f69977bb893888d338b9f76f023124216484e4056219d4e7e68");
    BOOST_CHECK(response["sealer"].asString() == "0x1");
    BOOST_CHECK(response["extraData"][0].asString() == "0x0a");
    BOOST_CHECK(response["gasLimit"].asString() == "0x9");
    BOOST_CHECK(response["gasUsed"].asString() == "0x8");
    BOOST_CHECK(response["timestamp"].asString() == "0x9");

    if (g_BCOSConfig.version() == RC1_VERSION)
    {
        BOOST_CHECK_EQUAL(response["transactions"][0]["hash"].asString(),
            "0x9319b663d2982b6d3894b455757843b5b68ca84a94356eebccdfa6d1eb34d680");
        BOOST_CHECK(response["transactions"][0]["to"].asString() ==
                    "0xd6f1a71052366dbae2f7ab2d5d5845e77965cf0d");
        BOOST_CHECK_EQUAL(response["transactions"][0]["from"].asString(),
            "0xb5e55058e234c6a58cde2d0f128546d31ea80c1d");
        BOOST_CHECK(response["transactions"][0]["gas"].asString() == "0x9184e729fff");
        BOOST_CHECK(response["transactions"][0]["gasPrice"].asString() == "0x174876e7ff");
        BOOST_CHECK_EQUAL(response["transactions"][0]["nonce"].asString(),
            "0x65f0d06e39dc3c08e32ac10a5070858962bc6c0f5760baca823f2d5582d144");
    }
    else
    {
        BOOST_CHECK_EQUAL(response["transactions"][0]["hash"].asString(),
            "0xe1054f38907ab3bef251caf8cbd8b25e779a40445b8dd4fcf15fce917d3e4ebe");
        BOOST_CHECK_EQUAL(response["transactions"][0]["to"].asString(),
            "0xbab78cea98af2320ad4ee81bba8a7473e0c8c48d");
        BOOST_CHECK_EQUAL(response["transactions"][0]["from"].asString(),
            "0x1a3fc157bd47c3fc2e260b34abbf481730d0f80f");
        BOOST_CHECK_EQUAL(response["transactions"][0]["gas"].asString(), "0xb2d05e00");
        BOOST_CHECK_EQUAL(response["transactions"][0]["gasPrice"].asString(), "0x11e1a300");
        BOOST_CHECK_EQUAL(response["transactions"][0]["nonce"].asString(),
            "0x3eebc46c9c0e3b84799097c5a6ccd6657a9295c11270407707366d0750fcd59");
    }
    BOOST_CHECK(response["transactions"][0]["value"].asString() == "0x0");
    BOOST_CHECK_EQUAL(response["transactions"][0]["blockHash"].asString(),
        "0xa5529e6035e28f69977bb893888d338b9f76f023124216484e4056219d4e7e68");
    BOOST_CHECK(response["transactions"][0]["transactionIndex"].asString() == "0x0");
    BOOST_CHECK(response["transactions"][0]["blockNumber"].asString() == "0x0");

    response = rpc->getBlockByNumber(groupId, "0x0", false);
    if (g_BCOSConfig.version() == RC1_VERSION)
    {
        BOOST_CHECK_EQUAL(response["transactions"][0].asString(),
            "0x9319b663d2982b6d3894b455757843b5b68ca84a94356eebccdfa6d1eb34d680");
    }
    else
    {
        BOOST_CHECK_EQUAL(response["transactions"][0].asString(),
            "0xe1054f38907ab3bef251caf8cbd8b25e779a40445b8dd4fcf15fce917d3e4ebe");
    }

    BOOST_CHECK_THROW(rpc->getBlockByNumber(invalidGroup, "0x0", false), JsonRpcException);
}

BOOST_AUTO_TEST_CASE(SM_testGetBlockHashByNumber)
{
    std::string blockNumber = "0x0";
    std::string response = rpc->getBlockHashByNumber(groupId, blockNumber);
    BOOST_CHECK(response == "0x067150c07dab4facb7160e075548007e067150c07dab4facb7160e075548007e");

    BOOST_CHECK_THROW(rpc->getBlockHashByNumber(invalidGroup, blockNumber), JsonRpcException);
}

BOOST_AUTO_TEST_CASE(SM_testGetTransactionByHash)
{
    std::string txHash = "0x7536cf1286b5ce6c110cd4fea5c891467884240c9af366d678eb4191e1c31c6f";
    Json::Value response = rpc->getTransactionByHash(groupId, txHash);
    BOOST_CHECK(response["hash"].asString() ==
                "0x7536cf1286b5ce6c110cd4fea5c891467884240c9af366d678eb4191e1c31c6f");
    BOOST_CHECK(response["blockNumber"].asString() == "0x0");
    if (g_BCOSConfig.version() == RC1_VERSION)
    {
        BOOST_CHECK_EQUAL(
            response["from"].asString(), "0xb5e55058e234c6a58cde2d0f128546d31ea80c1d");
        BOOST_CHECK(response["gas"].asString() == "0x9184e729fff");
        BOOST_CHECK(response["gasPrice"].asString() == "0x174876e7ff");
        BOOST_CHECK_EQUAL(response["nonce"].asString(),
            "0x65f0d06e39dc3c08e32ac10a5070858962bc6c0f5760baca823f2d5582d144");
        BOOST_CHECK(response["to"].asString() == "0xd6f1a71052366dbae2f7ab2d5d5845e77965cf0d");
    }
    else
    {
        BOOST_CHECK_EQUAL(
            response["from"].asString(), "0x1a3fc157bd47c3fc2e260b34abbf481730d0f80f");
        BOOST_CHECK_EQUAL(response["gas"].asString(), "0xb2d05e00");
        BOOST_CHECK_EQUAL(response["gasPrice"].asString(), "0x11e1a300");
        BOOST_CHECK_EQUAL(response["nonce"].asString(),
            "0x3eebc46c9c0e3b84799097c5a6ccd6657a9295c11270407707366d0750fcd59");
        BOOST_CHECK_EQUAL(response["to"].asString(), "0xbab78cea98af2320ad4ee81bba8a7473e0c8c48d");
    }
    BOOST_CHECK(response["transactionIndex"].asString() == "0x0");
    BOOST_CHECK(response["value"].asString() == "0x0");

    BOOST_CHECK_THROW(rpc->getTransactionByHash(invalidGroup, txHash), JsonRpcException);
}

BOOST_AUTO_TEST_CASE(SM_testGetTransactionByBlockHashAndIndex)
{
    std::string blockHash = "0x067150c07dab4facb7160e075548007e067150c07dab4facb7160e075548007e";
    std::string index = "0x0";
    Json::Value response = rpc->getTransactionByBlockHashAndIndex(groupId, blockHash, index);

    BOOST_CHECK(response["blockHash"].asString() ==
                "0x067150c07dab4facb7160e075548007e067150c07dab4facb7160e075548007e");
    BOOST_CHECK(response["blockNumber"].asString() == "0x0");
    if (g_BCOSConfig.version() == RC1_VERSION)
    {
        BOOST_CHECK_EQUAL(
            response["from"].asString(), "0xb5e55058e234c6a58cde2d0f128546d31ea80c1d");
        BOOST_CHECK(response["gas"].asString() == "0x9184e729fff");
        BOOST_CHECK(response["gasPrice"].asString() == "0x174876e7ff");
        BOOST_CHECK_EQUAL(response["hash"].asString(),
            "0x9319b663d2982b6d3894b455757843b5b68ca84a94356eebccdfa6d1eb34d680");
        BOOST_CHECK_EQUAL(response["nonce"].asString(),
            "0x65f0d06e39dc3c08e32ac10a5070858962bc6c0f5760baca823f2d5582d144");
        BOOST_CHECK(response["to"].asString() == "0xd6f1a71052366dbae2f7ab2d5d5845e77965cf0d");
    }
    else
    {
        BOOST_CHECK_EQUAL(response["hash"].asString(),
            "0xe1054f38907ab3bef251caf8cbd8b25e779a40445b8dd4fcf15fce917d3e4ebe");
        BOOST_CHECK_EQUAL(response["to"].asString(), "0xbab78cea98af2320ad4ee81bba8a7473e0c8c48d");
        BOOST_CHECK_EQUAL(
            response["from"].asString(), "0x1a3fc157bd47c3fc2e260b34abbf481730d0f80f");
        BOOST_CHECK_EQUAL(response["gas"].asString(), "0xb2d05e00");
        BOOST_CHECK_EQUAL(response["gasPrice"].asString(), "0x11e1a300");
        BOOST_CHECK_EQUAL(response["nonce"].asString(),
            "0x3eebc46c9c0e3b84799097c5a6ccd6657a9295c11270407707366d0750fcd59");
    }
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

BOOST_AUTO_TEST_CASE(SM_testGetTransactionByBlockNumberAndIndex)
{
    std::string blockNumber = "1";
    std::string index = "0x0";
    Json::Value response = rpc->getTransactionByBlockNumberAndIndex(groupId, blockNumber, index);

    BOOST_CHECK_EQUAL(response["blockHash"].asString(),
        "0xa5529e6035e28f69977bb893888d338b9f76f023124216484e4056219d4e7e68");
    BOOST_CHECK(response["blockNumber"].asString() == "0x0");
    if (g_BCOSConfig.version() == RC1_VERSION)
    {
        BOOST_CHECK_EQUAL(
            response["from"].asString(), "0xb5e55058e234c6a58cde2d0f128546d31ea80c1d");
        BOOST_CHECK(response["gas"].asString() == "0x9184e729fff");
        BOOST_CHECK(response["gasPrice"].asString() == "0x174876e7ff");
        BOOST_CHECK_EQUAL(response["hash"].asString(),
            "0x9319b663d2982b6d3894b455757843b5b68ca84a94356eebccdfa6d1eb34d680");
        BOOST_CHECK_EQUAL(response["nonce"].asString(),
            "0x65f0d06e39dc3c08e32ac10a5070858962bc6c0f5760baca823f2d5582d144");
        BOOST_CHECK(response["to"].asString() == "0xd6f1a71052366dbae2f7ab2d5d5845e77965cf0d");
    }
    else
    {
        BOOST_CHECK_EQUAL(response["hash"].asString(),
            "0xe1054f38907ab3bef251caf8cbd8b25e779a40445b8dd4fcf15fce917d3e4ebe");
        BOOST_CHECK_EQUAL(response["to"].asString(), "0xbab78cea98af2320ad4ee81bba8a7473e0c8c48d");
        BOOST_CHECK_EQUAL(
            response["from"].asString(), "0x1a3fc157bd47c3fc2e260b34abbf481730d0f80f");
        BOOST_CHECK_EQUAL(response["gas"].asString(), "0xb2d05e00");
        BOOST_CHECK_EQUAL(response["gasPrice"].asString(), "0x11e1a300");
        BOOST_CHECK_EQUAL(response["nonce"].asString(),
            "0x3eebc46c9c0e3b84799097c5a6ccd6657a9295c11270407707366d0750fcd59");
    }
    BOOST_CHECK(response["transactionIndex"].asString() == "0x0");
    BOOST_CHECK(response["value"].asString() == "0x0");

    BOOST_CHECK_THROW(rpc->getTransactionByBlockNumberAndIndex(invalidGroup, blockNumber, index),
        JsonRpcException);
    index = "0x1";
    BOOST_CHECK_THROW(
        rpc->getTransactionByBlockHashAndIndex(groupId, blockNumber, index), JsonRpcException);
}

BOOST_AUTO_TEST_CASE(SM_testGetTransactionReceipt)
{
    std::string txHash = "0x7536cf1286b5ce6c110cd4fea5c891467884240c9af366d678eb4191e1c31c6f";
    Json::Value response = rpc->getTransactionReceipt(groupId, txHash);

    BOOST_CHECK(response["transactionHash"].asString() ==
                "0x7536cf1286b5ce6c110cd4fea5c891467884240c9af366d678eb4191e1c31c6f");
    BOOST_CHECK(response["transactionIndex"].asString() == "0x0");
    BOOST_CHECK(response["blockNumber"].asString() == "0x0");
    BOOST_CHECK(response["blockHash"].asString() ==
                "0x067150c07dab4facb7160e075548007e067150c07dab4facb7160e075548007e");
    if (g_BCOSConfig.version() == RC1_VERSION)
    {
        BOOST_CHECK_EQUAL(
            response["from"].asString(), "0xb5e55058e234c6a58cde2d0f128546d31ea80c1d");
        BOOST_CHECK(response["to"].asString() == "0xd6f1a71052366dbae2f7ab2d5d5845e77965cf0d");
    }
    else
    {
        BOOST_CHECK_EQUAL(
            response["from"].asString(), "0x1a3fc157bd47c3fc2e260b34abbf481730d0f80f");
        BOOST_CHECK_EQUAL(response["to"].asString(), "0xbab78cea98af2320ad4ee81bba8a7473e0c8c48d");
    }
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
BOOST_AUTO_TEST_CASE(SM_testGetpendingTransactions)
{
    Json::Value response = rpc->getPendingTransactions(groupId);

    if (g_BCOSConfig.version() == RC1_VERSION)
    {
        BOOST_CHECK_EQUAL(
            response[0]["from"].asString(), "0xb5e55058e234c6a58cde2d0f128546d31ea80c1d");
        BOOST_CHECK(response[0]["gas"].asString() == "0x9184e729fff");
        BOOST_CHECK(response[0]["gasPrice"].asString() == "0x174876e7ff");
        BOOST_CHECK_EQUAL(response[0]["nonce"].asString(),
            "0x65f0d06e39dc3c08e32ac10a5070858962bc6c0f5760baca823f2d5582d144");
        BOOST_CHECK(response[0]["to"].asString() == "0xd6f1a71052366dbae2f7ab2d5d5845e77965cf0d");
    }
    else
    {
        BOOST_CHECK_EQUAL(
            response[0]["to"].asString(), "0xbab78cea98af2320ad4ee81bba8a7473e0c8c48d");
        BOOST_CHECK_EQUAL(
            response[0]["from"].asString(), "0x1a3fc157bd47c3fc2e260b34abbf481730d0f80f");
        BOOST_CHECK_EQUAL(response[0]["gas"].asString(), "0xb2d05e00");
        BOOST_CHECK_EQUAL(response[0]["gasPrice"].asString(), "0x11e1a300");
        BOOST_CHECK_EQUAL(response[0]["nonce"].asString(),
            "0x3eebc46c9c0e3b84799097c5a6ccd6657a9295c11270407707366d0750fcd59");
    }
    BOOST_CHECK(response[0]["value"].asString() == "0x0");
    // no need to avoid getPendingTransactions for the node that doesn't belong to the group
    // BOOST_CHECK_THROW(rpc->getPendingTransactions(invalidGroup), JsonRpcException);
}

BOOST_AUTO_TEST_CASE(SM_testGetCode)
{
    std::string address = "0xa94f5374fce5edbc8e2a8697c15331677e6ebf0b";
    std::string response = rpc->getCode(groupId, address);
    BOOST_CHECK(response == "0x");

    BOOST_CHECK_THROW(rpc->getCode(invalidGroup, address), JsonRpcException);
}

BOOST_AUTO_TEST_CASE(SM_testGetTotalTransactionCount)
{
    Json::Value response = rpc->getTotalTransactionCount(groupId);
    BOOST_CHECK(response["txSum"].asString() == "0x0");
    BOOST_CHECK(response["blockNumber"].asString() == "0x0");

    BOOST_CHECK_THROW(rpc->getTotalTransactionCount(invalidGroup), JsonRpcException);
}

BOOST_AUTO_TEST_CASE(SM_testCall)
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

BOOST_AUTO_TEST_CASE(SM_testSendRawTransaction)
{
    std::string rlpStr =
        "f901309f65f0d06e39dc3c08e32ac10a5070858962bc6c0f5760baca823f2d5582d14485174876e7ff8609"
        "184e729fff8204a294d6f1a71052366dbae2f7ab2d5d5845e77965cf0d80b86448f85bce00000000000000"
        "0000000000000000000000000000000000000000000000001bf5bd8a9e7ba8b936ea704292ff4aaa5797bf"
        "671fdc8526dcd159f23c1f5a05f44e9fa862834dc7cb4541558f2b4961dc39eaaf0af7f7395028658d0e01"
        "b86a37b840c7ca78e7ab80ee4be6d3936ba8e899d8fe12c12114502956ebe8c8629d36d88481dec9973574"
        "2ea523c88cf3becba1cc4375bc9e225143fe1e8e43abc8a7c493a0ba3ce8383b7c91528bede9cf890b4b1e"
        "9b99c1d8e56d6f8292c827470a606827a0ed511490a1666791b2bd7fc4f499eb5ff18fb97ba68ff9aee206"
        "8fd63b88e817";
    if (g_BCOSConfig.version() >= RC2_VERSION)
    {
        rlpStr =
            "f90114a003eebc46c9c0e3b84799097c5a6ccd6657a9295c11270407707366d0750fcd598411e1a30084b2"
            "d05e008201f594bab78cea98af2320ad4ee81bba8a7473e0c8c48d80a48fff0fc400000000000000000000"
            "000000000000000000000000000000000000000000040101a48fff0fc40000000000000000000000000000"
            "000000000000000000000000000000000004b8408234c544a9f3ce3b401a92cc7175602ce2a1e29b1ec135"
            "381c7d2a9e8f78f3edc9c06ee55252857c9a4560cb39e9d70d40f4331cace4d2b3121b967fa7a829f0a00f"
            "16d87c5065ad5c3b110ef0b97fe9a67b62443cb8ddde60d4e001a64429dc6ea03d2569e0449e9a900c2365"
            "41afb9d8a8d5e1a36844439c7076f6e75ed624256f";
    }

    std::string response = rpc->sendRawTransaction(groupId, rlpStr);

    if (g_BCOSConfig.version() == RC1_VERSION)
    {
        BOOST_CHECK_EQUAL(
            response, "0x9319b663d2982b6d3894b455757843b5b68ca84a94356eebccdfa6d1eb34d680");
    }
    else
    {
        BOOST_CHECK_EQUAL(
            response, "0xe1054f38907ab3bef251caf8cbd8b25e779a40445b8dd4fcf15fce917d3e4ebe");
    }

    BOOST_CHECK_THROW(rpc->sendRawTransaction(invalidGroup, rlpStr), JsonRpcException);
}
BOOST_AUTO_TEST_SUITE_END()

BOOST_FIXTURE_TEST_SUITE(RpcTest, RpcTestFixure)
BOOST_AUTO_TEST_CASE(testSystemConfig)
{
    std::string value = rpc->getSystemConfigByKey(groupId, "tx_gas_limit");
    BOOST_CHECK(value == "300000000");
    BOOST_CHECK_THROW(rpc->getSystemConfigByKey(invalidGroup, "tx_gas_limit"), JsonRpcException);
}

BOOST_AUTO_TEST_CASE(testConsensusPart)
{
    std::string blockNumber = rpc->getBlockNumber(groupId);
    BOOST_CHECK(blockNumber == "0x0");
    BOOST_CHECK_THROW(rpc->getBlockNumber(invalidGroup), JsonRpcException);

    std::string pbftView = rpc->getPbftView(groupId);
    BOOST_CHECK(pbftView == "0x0");
    BOOST_CHECK_THROW(rpc->getPbftView(invalidGroup), JsonRpcException);

    Json::Value status = rpc->getConsensusStatus(groupId);
    BOOST_CHECK_THROW(rpc->getConsensusStatus(invalidGroup), JsonRpcException);

    Json::Value sealerList = rpc->getSealerList(groupId);
    BOOST_CHECK(sealerList.size() == 1);
    BOOST_CHECK_THROW(rpc->getSealerList(invalidGroup), JsonRpcException);

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
    Json::Value version = rpc->getClientVersion();
    BOOST_CHECK(version.size() != 0);

    Json::Value response = rpc->getPeers(groupId);
    BOOST_CHECK(response[0]["NodeID"].asString() != "");
    BOOST_CHECK(response[0]["IPAndPort"].asString() == "127.0.0.1:30310");
    BOOST_CHECK(response[0]["Topic"][0].asString() == "Topic1");

    response = rpc->getGroupPeers(groupId);
    BOOST_CHECK(response.size() == 1);

    response = rpc->getGroupList();
    BOOST_CHECK(response.size() == 0);

    response = rpc->getNodeIDList(groupId);
    BOOST_CHECK(response.size() == 2);
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

    if (g_BCOSConfig.version() == RC1_VERSION)
    {
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
    }
    else
    {
        BOOST_CHECK(response["transactions"][0]["hash"].asString() ==
                    "0x0accad4228274b0d78939f48149767883a6e99c95941baa950156e926f1c96ba");
        BOOST_CHECK(response["transactions"][0]["to"].asString() ==
                    "0xd6c8a04b8826b0a37c6d4aa0eaa8644d8e35b79f");
        BOOST_CHECK(response["transactions"][0]["from"].asString() ==
                    "0x148947262ec5e21739fe3a931c29e8b84ee34a0f");
        BOOST_CHECK(response["transactions"][0]["gas"].asString() == "0x11e1a300");
        BOOST_CHECK(response["transactions"][0]["gasPrice"].asString() == "0x11e1a300");
        BOOST_CHECK(response["transactions"][0]["nonce"].asString() ==
                    "0x3922ee720bb7445e3a914d8ab8f507d1a647296d563100e49548d83fd98865c");
    }

    BOOST_CHECK(response["transactions"][0]["value"].asString() == "0x0");
    BOOST_CHECK(response["transactions"][0]["blockHash"].asString() ==
                "0x067150c07dab4facb7160e075548007e067150c07dab4facb7160e075548007e");
    BOOST_CHECK(response["transactions"][0]["transactionIndex"].asString() == "0x0");
    BOOST_CHECK(response["transactions"][0]["blockNumber"].asString() == "0x0");

    response = rpc->getBlockByHash(groupId, blockHash, false);
    if (g_BCOSConfig.version() == RC1_VERSION)
    {
        BOOST_CHECK(response["transactions"][0].asString() ==
                    "0x7536cf1286b5ce6c110cd4fea5c891467884240c9af366d678eb4191e1c31c6f");
    }
    else
    {
        BOOST_CHECK(response["transactions"][0].asString() ==
                    "0x0accad4228274b0d78939f48149767883a6e99c95941baa950156e926f1c96ba");
    }

    BOOST_CHECK_THROW(rpc->getBlockByHash(invalidGroup, blockHash, false), JsonRpcException);
    blockHash = "0x067150c07dab4facb7160e075548007e067150c07dab4facb7160e0755480070";
    BOOST_CHECK_THROW(rpc->getBlockByHash(groupId, blockHash, false), JsonRpcException);
}
BOOST_AUTO_TEST_CASE(getBlockByNumber)
{
    Json::Value response = rpc->getBlockByNumber(groupId, "0x0", true);

    BOOST_CHECK(response["number"].asString() == "0x0");
    BOOST_CHECK(response["hash"].asString() ==
                "0xba6e71fbc207e776c74b66bc031d1a599d5b35cd03fd9f5e2331fa5ecdccdc87");
    BOOST_CHECK(response["sealer"].asString() == "0x1");
    BOOST_CHECK(response["extraData"][0].asString() == "0x0a");
    BOOST_CHECK(response["gasLimit"].asString() == "0x9");
    BOOST_CHECK(response["gasUsed"].asString() == "0x8");
    BOOST_CHECK(response["timestamp"].asString() == "0x9");

    if (g_BCOSConfig.version() == RC1_VERSION)
    {
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
    }
    else
    {
        BOOST_CHECK(response["transactions"][0]["hash"].asString() ==
                    "0x0accad4228274b0d78939f48149767883a6e99c95941baa950156e926f1c96ba");
        BOOST_CHECK(response["transactions"][0]["to"].asString() ==
                    "0xd6c8a04b8826b0a37c6d4aa0eaa8644d8e35b79f");
        BOOST_CHECK(response["transactions"][0]["from"].asString() ==
                    "0x148947262ec5e21739fe3a931c29e8b84ee34a0f");
        BOOST_CHECK(response["transactions"][0]["gas"].asString() == "0x11e1a300");
        BOOST_CHECK(response["transactions"][0]["gasPrice"].asString() == "0x11e1a300");
        BOOST_CHECK(response["transactions"][0]["nonce"].asString() ==
                    "0x3922ee720bb7445e3a914d8ab8f507d1a647296d563100e49548d83fd98865c");
    }
    BOOST_CHECK(response["transactions"][0]["value"].asString() == "0x0");
    BOOST_CHECK(response["transactions"][0]["blockHash"].asString() ==
                "0xba6e71fbc207e776c74b66bc031d1a599d5b35cd03fd9f5e2331fa5ecdccdc87");
    BOOST_CHECK(response["transactions"][0]["transactionIndex"].asString() == "0x0");
    BOOST_CHECK(response["transactions"][0]["blockNumber"].asString() == "0x0");

    response = rpc->getBlockByNumber(groupId, "0x0", false);
    if (g_BCOSConfig.version() == RC1_VERSION)
    {
        BOOST_CHECK(response["transactions"][0].asString() ==
                    "0x7536cf1286b5ce6c110cd4fea5c891467884240c9af366d678eb4191e1c31c6f");
    }
    else
    {
        BOOST_CHECK(response["transactions"][0].asString() ==
                    "0x0accad4228274b0d78939f48149767883a6e99c95941baa950156e926f1c96ba");
    }

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
    if (g_BCOSConfig.version() >= RC2_VERSION)
    {
        txHash = "0x0accad4228274b0d78939f48149767883a6e99c95941baa950156e926f1c96ba";
    }
    Json::Value response = rpc->getTransactionByHash(groupId, txHash);

    BOOST_CHECK(response["blockNumber"].asString() == "0x0");
    if (g_BCOSConfig.version() == RC1_VERSION)
    {
        BOOST_CHECK(response["from"].asString() == "0x6bc952a2e4db9c0c86a368d83e9df0c6ab481102");
        BOOST_CHECK(response["gas"].asString() == "0x9184e729fff");
        BOOST_CHECK(response["gasPrice"].asString() == "0x174876e7ff");
        BOOST_CHECK(response["hash"].asString() ==
                    "0x7536cf1286b5ce6c110cd4fea5c891467884240c9af366d678eb4191e1c31c6f");
        BOOST_CHECK(response["nonce"].asString() ==
                    "0x65f0d06e39dc3c08e32ac10a5070858962bc6c0f5760baca823f2d5582d03f");
        BOOST_CHECK(response["to"].asString() == "0xd6f1a71052366dbae2f7ab2d5d5845e77965cf0d");
    }
    else
    {
        BOOST_CHECK(response["from"].asString() == "0x148947262ec5e21739fe3a931c29e8b84ee34a0f");
        BOOST_CHECK(response["gas"].asString() == "0x11e1a300");
        BOOST_CHECK(response["gasPrice"].asString() == "0x11e1a300");
        BOOST_CHECK(response["hash"].asString() ==
                    "0x0accad4228274b0d78939f48149767883a6e99c95941baa950156e926f1c96ba");
        BOOST_CHECK(response["nonce"].asString() ==
                    "0x3922ee720bb7445e3a914d8ab8f507d1a647296d563100e49548d83fd98865c");
        BOOST_CHECK(response["to"].asString() == "0xd6c8a04b8826b0a37c6d4aa0eaa8644d8e35b79f");
    }
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
    if (g_BCOSConfig.version() == RC1_VERSION)
    {
        BOOST_CHECK(response["from"].asString() == "0x6bc952a2e4db9c0c86a368d83e9df0c6ab481102");
        BOOST_CHECK(response["gas"].asString() == "0x9184e729fff");
        BOOST_CHECK(response["gasPrice"].asString() == "0x174876e7ff");
        BOOST_CHECK(response["hash"].asString() ==
                    "0x7536cf1286b5ce6c110cd4fea5c891467884240c9af366d678eb4191e1c31c6f");
        BOOST_CHECK(response["nonce"].asString() ==
                    "0x65f0d06e39dc3c08e32ac10a5070858962bc6c0f5760baca823f2d5582d03f");
        BOOST_CHECK(response["to"].asString() == "0xd6f1a71052366dbae2f7ab2d5d5845e77965cf0d");
    }
    else
    {
        BOOST_CHECK(response["hash"].asString() ==
                    "0x0accad4228274b0d78939f48149767883a6e99c95941baa950156e926f1c96ba");
        BOOST_CHECK(response["to"].asString() == "0xd6c8a04b8826b0a37c6d4aa0eaa8644d8e35b79f");
        BOOST_CHECK(response["from"].asString() == "0x148947262ec5e21739fe3a931c29e8b84ee34a0f");
        BOOST_CHECK(response["gas"].asString() == "0x11e1a300");
        BOOST_CHECK(response["gasPrice"].asString() == "0x11e1a300");
        BOOST_CHECK(response["nonce"].asString() ==
                    "0x3922ee720bb7445e3a914d8ab8f507d1a647296d563100e49548d83fd98865c");
    }
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
                "0xba6e71fbc207e776c74b66bc031d1a599d5b35cd03fd9f5e2331fa5ecdccdc87");
    BOOST_CHECK(response["blockNumber"].asString() == "0x0");
    if (g_BCOSConfig.version() == RC1_VERSION)
    {
        BOOST_CHECK(response["from"].asString() == "0x6bc952a2e4db9c0c86a368d83e9df0c6ab481102");
        BOOST_CHECK(response["gas"].asString() == "0x9184e729fff");
        BOOST_CHECK(response["gasPrice"].asString() == "0x174876e7ff");
        BOOST_CHECK(response["hash"].asString() ==
                    "0x7536cf1286b5ce6c110cd4fea5c891467884240c9af366d678eb4191e1c31c6f");
        BOOST_CHECK(response["nonce"].asString() ==
                    "0x65f0d06e39dc3c08e32ac10a5070858962bc6c0f5760baca823f2d5582d03f");
        BOOST_CHECK(response["to"].asString() == "0xd6f1a71052366dbae2f7ab2d5d5845e77965cf0d");
    }
    else
    {
        BOOST_CHECK(response["hash"].asString() ==
                    "0x0accad4228274b0d78939f48149767883a6e99c95941baa950156e926f1c96ba");
        BOOST_CHECK(response["to"].asString() == "0xd6c8a04b8826b0a37c6d4aa0eaa8644d8e35b79f");
        BOOST_CHECK(response["from"].asString() == "0x148947262ec5e21739fe3a931c29e8b84ee34a0f");
        BOOST_CHECK(response["gas"].asString() == "0x11e1a300");
        BOOST_CHECK(response["gasPrice"].asString() == "0x11e1a300");
        BOOST_CHECK(response["nonce"].asString() ==
                    "0x3922ee720bb7445e3a914d8ab8f507d1a647296d563100e49548d83fd98865c");
    }
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
    if (g_BCOSConfig.version() == RC1_VERSION)
    {
        BOOST_CHECK(response["from"].asString() == "0x6bc952a2e4db9c0c86a368d83e9df0c6ab481102");
        BOOST_CHECK(response["to"].asString() == "0xd6f1a71052366dbae2f7ab2d5d5845e77965cf0d");
    }
    else
    {
        BOOST_CHECK(response["from"].asString() == "0x148947262ec5e21739fe3a931c29e8b84ee34a0f");
        BOOST_CHECK(response["to"].asString() == "0xd6c8a04b8826b0a37c6d4aa0eaa8644d8e35b79f");
    }
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
BOOST_AUTO_TEST_CASE(testGetPendingTransactions)
{
    Json::Value response = rpc->getPendingTransactions(groupId);

    if (g_BCOSConfig.version() == RC1_VERSION)
    {
        BOOST_CHECK(response[0]["from"].asString() == "0x6bc952a2e4db9c0c86a368d83e9df0c6ab481102");
        BOOST_CHECK(response[0]["gas"].asString() == "0x9184e729fff");
        BOOST_CHECK(response[0]["gasPrice"].asString() == "0x174876e7ff");
        BOOST_CHECK(response[0]["nonce"].asString() ==
                    "0x65f0d06e39dc3c08e32ac10a5070858962bc6c0f5760baca823f2d5582d03f");
        BOOST_CHECK(response[0]["to"].asString() == "0xd6f1a71052366dbae2f7ab2d5d5845e77965cf0d");
    }
    else
    {
        BOOST_CHECK(response[0]["to"].asString() == "0xd6c8a04b8826b0a37c6d4aa0eaa8644d8e35b79f");
        BOOST_CHECK(response[0]["from"].asString() == "0x148947262ec5e21739fe3a931c29e8b84ee34a0f");
        BOOST_CHECK(response[0]["gas"].asString() == "0x11e1a300");
        BOOST_CHECK(response[0]["gasPrice"].asString() == "0x11e1a300");
        BOOST_CHECK(response[0]["nonce"].asString() ==
                    "0x3922ee720bb7445e3a914d8ab8f507d1a647296d563100e49548d83fd98865c");
    }
    BOOST_CHECK(response[0]["value"].asString() == "0x0");

    // no need to check the node in the group or not
    // BOOST_CHECK_THROW(rpc->getPendingTransactions(invalidGroup), JsonRpcException);
}
BOOST_AUTO_TEST_CASE(testGetPendingTxSize)
{
    std::string response = rpc->getPendingTxSize(groupId);
    BOOST_CHECK(response == "0x1");

    // no need to check the node in the group or not
    // BOOST_CHECK_THROW(rpc->getPendingTxSize(invalidGroup), JsonRpcException);
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
    BOOST_CHECK(response["failedTxSum"].asString() == "0x0");

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
    if (g_BCOSConfig.version() >= RC2_VERSION)
    {
        rlpStr =
            "f8d3a003922ee720bb7445e3a914d8ab8f507d1a647296d563100e49548d83fd98865c8411e1a3008411e1"
            "a3008201f894d6c8a04b8826b0a37c6d4aa0eaa8644d8e35b79f80a466c991390000000000000000000000"
            "0000000000000000000000000000000000000000040101a466c99139000000000000000000000000000000"
            "00000000000000000000000000000000041ba08e0d3fae10412c584c977721aeda88df932b2a019f084fed"
            "a1e0a42d199ea979a016c387f79eb85078be5db40abe1670b8b480a12c7eab719bedee212b7972f775";
    }

    std::string response = rpc->sendRawTransaction(groupId, rlpStr);

    BOOST_CHECK(response == "0x0accad4228274b0d78939f48149767883a6e99c95941baa950156e926f1c96ba");

    BOOST_CHECK_THROW(rpc->sendRawTransaction(invalidGroup, rlpStr), JsonRpcException);
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev
