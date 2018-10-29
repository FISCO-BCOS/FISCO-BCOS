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
#include <librpc/Rpc.h>
#include <librpc/Rpc.cpp>
#include <test/tools/libutils/Common.h>
#include <test/unittests/libp2p/FakeHost.h>
#include <test/tools/libutils/TestOutputHelper.h>
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
		m_service = std::make_shared<dev::test::FakeService>(m_host, m_p2pHandler);
		m_preCompile = std::make_shared<std::unordered_map<dev::Address, dev::eth::PrecompiledContract>>();
		std::string configurationPath = getTestPath().string() + "/fisco-bcos-data/config.group10.ini";
		m_ledgerManager =  std::make_shared<LedgerManager>(m_service, m_keyPair, m_preCompile);
		m_ledgerManager->initSingleLedger<FakeLedger>(0, "", configurationPath);

		rpc = std::make_shared<Rpc>(m_ledgerManager, m_service);

		requestJson["groudId"] = 0;
		requestJson["jsonrpc"] = "2.0";
		requestJson["id"] = "1";
	}

public:
	std::shared_ptr<Rpc> rpc;
    std::shared_ptr<std::unordered_map<dev::Address, dev::eth::PrecompiledContract>> m_preCompile;
//    std::shared_ptr<dev::p2p::P2PInterface> p2p_service;
    KeyPair m_keyPair = KeyPair::create();
    std::shared_ptr<FakeService> m_service;
    std::shared_ptr<LedgerManager> m_ledgerManager;
    Json::Value requestJson;
    std::string clientVersion = "2.0";
    std::string listenIp = "127.0.0.1";
    uint16_t listenPort = 30304;
    std::shared_ptr<P2PMsgHandler> m_p2pHandler;
    std::shared_ptr<Host> m_host;
};

BOOST_FIXTURE_TEST_SUITE(RpcTest, RpcTestFixure)

BOOST_AUTO_TEST_CASE(testConsensusPart)
{
	requestJson["method"] = "blockNumber";
	Json::Value responseJson = rpc->blockNumber(requestJson);
	BOOST_CHECK(responseJson["id"] == "1");
	BOOST_CHECK(responseJson["jsonrpc"] == "2.0");
	BOOST_CHECK(responseJson["result"] == 1);

//	requestJson["method"] = "pbftView";
//	responseJson = rpc->pbftView(requestJson);
//	BOOST_CHECK(responseJson["id"] == "1");
//	BOOST_CHECK(responseJson["jsonrpc"] == "2.0");
//	BOOST_CHECK(responseJson["result"] == 1);
}

BOOST_AUTO_TEST_CASE(testP2pPart)
{
//	requestJson["method"] = "peers";
//	Json::Value responseJson = rpc->peers(requestJson);
//	BOOST_CHECK(responseJson["id"] == "1");
//	BOOST_CHECK(responseJson["jsonrpc"] == "2.0");
//	BOOST_CHECK(responseJson["result"][0]["NodeID"] ==  m_service->sessionInfos()[0].nodeID.hex()) ;
//	BOOST_CHECK(responseJson["result"][0]["IP"] == m_service->sessionInfos()[0].nodeIPEndpoint.host);
//	BOOST_CHECK(responseJson["result"][0]["TcpPort"].asInt() == m_service->sessionInfos()[0].nodeIPEndpoint.tcpPort);
//	BOOST_CHECK(responseJson["result"][0]["UdpPort"].asInt() == m_service->sessionInfos()[0].nodeIPEndpoint.udpPort);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev
