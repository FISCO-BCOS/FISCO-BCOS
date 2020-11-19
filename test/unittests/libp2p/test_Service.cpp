/*
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
 */
/** @file Host.h
 * @author xingqiangbai
 * @date 2018
 */

#include "../libnetwork/FakeASIOInterface.h"
#include "libnetwork/Host.h"
#include "libnetwork/Session.h"
#include "libp2p/P2PMessageFactory.h"
#include "libp2p/Service.h"
#include "libutilities/ThreadPool.h"
#include "test/tools/libutils/Common.h"
#include <libconfig/GlobalConfigure.h>
#include <libinitializer/SecureInitializer.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace std;
using namespace bcos::network;
using namespace bcos::p2p;
using namespace bcos::test;

namespace test_Host
{
struct ServiceFixture
{
    ServiceFixture()
    {
        boost::property_tree::ptree pt;
        pt.put("secure.data_path", getTestPath().string() + "/fisco-bcos-data/");
        auto secureInitializer = std::make_shared<bcos::initializer::SecureInitializer>();
        if (g_BCOSConfig.SMCrypto())
        {
            m_sslContext =
                std::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::tlsv12);
        }
        else
        {
            secureInitializer->initConfig(pt);
            m_sslContext = secureInitializer->SSLContext();
        }
        m_asioInterface = std::make_shared<bcos::network::FakeASIOInterface>();
        m_asioInterface->setIOService(std::make_shared<ba::io_service>());
        m_asioInterface->setSSLContext(m_sslContext);
        m_asioInterface->setType(bcos::network::ASIOInterface::SSL);

        m_host = std::make_shared<bcos::network::Host>();
        m_host->setASIOInterface(m_asioInterface);
        m_sessionFactory = std::make_shared<bcos::network::SessionFactory>();
        m_host->setSessionFactory(m_sessionFactory);
        m_messageFactory = std::make_shared<P2PMessageFactory>();
        m_host->setMessageFactory(m_messageFactory);

        m_host->setHostPort(m_hostIP, m_port);
        m_threadPool = std::make_shared<ThreadPool>("P2PTest", 2);
        m_host->setThreadPool(m_threadPool);

        m_host->setCRL(m_certBlacklist);

        m_connectionHandler = m_host->connectionHandler();
        m_p2pService = std::make_shared<Service>();
        m_p2pService->setHost(m_host);
        m_p2pService->setKeyPair(secureInitializer->keyPair());
        m_p2pService->setP2PMessageFactory(m_messageFactory);
    }
    ~ServiceFixture() {}
    std::shared_ptr<Service> m_p2pService;
    string m_hostIP = "127.0.0.1";
    std::map<bcos::network::NodeIPEndpoint, NodeID> m_nodes;
    uint16_t m_port = 8845;
    std::shared_ptr<bcos::network::Host> m_host;
    std::shared_ptr<boost::asio::ssl::context> m_sslContext;
    P2PMessageFactory::Ptr m_messageFactory;
    std::shared_ptr<SessionFactory> m_sessionFactory;
    std::shared_ptr<bcos::ThreadPool> m_threadPool;
    std::vector<std::string> m_certBlacklist;
    std::shared_ptr<ASIOInterface> m_asioInterface;
    std::vector<std::shared_ptr<bcos::network::SessionFace>> m_sessions;
    std::function<void(NetworkException, NodeInfo const&, std::shared_ptr<SessionFace>)>
        m_connectionHandler;
};

struct SM_ServiceFixture : public SM_CryptoTestFixture, public ServiceFixture
{
    SM_ServiceFixture() : SM_CryptoTestFixture(), ServiceFixture() {}
};

BOOST_FIXTURE_TEST_SUITE(Service, ServiceFixture)

BOOST_AUTO_TEST_CASE(Service_run)
{
    auto nodeIP = NodeIPEndpoint(boost::asio::ip::address::from_string("127.0.0.1"), 8888);
    m_nodes[nodeIP] = NodeID(
        "7dcce48da1c464c7025614a54a4e26df7d6f92cd4d315601e057c1659796736c5c8730e380fcbe637191cc2aeb"
        "f4746846c0db2604adebf9c70c7f418d4d5a61");
    m_p2pService->setStaticNodes(m_nodes);
    m_p2pService->start();
    m_p2pService->stop();
}

BOOST_FIXTURE_TEST_CASE(SM_Service_run, SM_ServiceFixture)
{
    auto nodeIP = NodeIPEndpoint(boost::asio::ip::address::from_string("127.0.0.1"), 8888);
    m_nodes[nodeIP] = NodeID(
        "7dcce48da1c464c7025614a54a4e26df7d6f92cd4d315601e057c1659796736c5c8730e380fcbe637191cc2aeb"
        "f4746846c0db2604adebf9c70c7f418d4d5a61");
    m_p2pService->setStaticNodes(m_nodes);
    m_p2pService->start();
    m_p2pService->stop();
}
BOOST_AUTO_TEST_SUITE_END()

}  // namespace test_Host
