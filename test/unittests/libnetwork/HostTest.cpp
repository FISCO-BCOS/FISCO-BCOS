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
 * @author 122025861@qq.com
 * @date 2018
 */

#include "libnetwork/Host.h"
#include "FakeASIOInterface.h"
#include "libnetwork/Session.h"
#include "libp2p/P2PMessageFactory.h"
#include "libutilities/ThreadPool.h"
#include "test/tools/libutils/Common.h"
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
using namespace bcos::test;

namespace test_Host
{
struct HostFixture
{
    HostFixture()
    {
        boost::property_tree::ptree pt;
        pt.put("secure.data_path", getTestPath().string() + "/fisco-bcos-data/");
        if (g_BCOSConfig.SMCrypto())
        {
            m_sslContext =
                std::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::tlsv12);
        }
        else
        {
            auto secureInitializer = std::make_shared<bcos::initializer::SecureInitializer>();
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
        m_host->setConnectionHandler(
            [&](bcos::network::NetworkException e, bcos::network::NodeInfo const&,
                std::shared_ptr<bcos::network::SessionFace> p) {
                if (e.errorCode())
                {
                    LOG(ERROR) << e.what();
                    return;
                }
                LOG(INFO) << "start new session " << p->socket()->nodeIPEndpoint()
                          << ",error:" << e.what();
                m_sessions.push_back(p);
                p->start();
            });
        m_connectionHandler = m_host->connectionHandler();
    }

    ~HostFixture() {}
    string m_hostIP = "127.0.0.1";
    uint16_t m_port = 8845;
    std::shared_ptr<bcos::network::Host> m_host;
    std::shared_ptr<boost::asio::ssl::context> m_sslContext;
    MessageFactory::Ptr m_messageFactory;
    std::shared_ptr<SessionFactory> m_sessionFactory;
    std::shared_ptr<bcos::ThreadPool> m_threadPool;
    std::vector<std::string> m_certBlacklist;
    std::shared_ptr<ASIOInterface> m_asioInterface;
    std::vector<std::shared_ptr<bcos::network::SessionFace>> m_sessions;
    std::function<void(NetworkException, NodeInfo const&, std::shared_ptr<SessionFace>)>
        m_connectionHandler;
};

struct SM_HostFixture : public SM_CryptoTestFixture, public HostFixture
{
    SM_HostFixture() : SM_CryptoTestFixture(), HostFixture() {}
};

BOOST_FIXTURE_TEST_SUITE(Host, HostFixture)

BOOST_AUTO_TEST_CASE(ASIOInterface)
{
    m_asioInterface->ioService();
    m_asioInterface->sslContext();
    m_asioInterface->sslContext();
    m_asioInterface->newTimer(0);
    m_asioInterface->reset();
    m_asioInterface->stop();
    m_asioInterface->ASIOInterface::newSocket();
}

BOOST_AUTO_TEST_CASE(Hostfunctions)
{
    BOOST_CHECK(m_port == m_host->listenPort());
    BOOST_CHECK(m_hostIP == m_host->listenHost());
    string hostIP = "0.0.0.0";
    uint16_t port = 8546;
    m_host->setHostPort(hostIP, port);
    BOOST_CHECK(port == m_host->listenPort());
    BOOST_CHECK(hostIP == m_host->listenHost());
    BOOST_CHECK(false == m_host->haveNetwork());
    BOOST_CHECK(m_certBlacklist == m_host->certBlacklist());
    m_host->connectionHandler();
    BOOST_CHECK(m_asioInterface == m_host->asioInterface());
    BOOST_CHECK(m_sessionFactory == m_host->sessionFactory());
    BOOST_CHECK(m_messageFactory == m_host->messageFactory());
    BOOST_CHECK(m_threadPool == m_host->threadPool());
}

BOOST_AUTO_TEST_CASE(Host_run)
{
    m_host->start();
    // start() will create a new thread and call host->startAccept, so wait
    this_thread::sleep_for(chrono::milliseconds(50));
    BOOST_CHECK(true == m_host->haveNetwork());
    auto fakeAsioInterface = dynamic_pointer_cast<FakeASIOInterface>(m_asioInterface);
    while (!fakeAsioInterface->m_acceptorInfo.first)
    {
    }
    auto socket = fakeAsioInterface->m_acceptorInfo.first;
    auto nodeIP = NodeIPEndpoint(boost::asio::ip::address::from_string("127.0.0.1"), 8888);
    socket->setNodeIPEndpoint(nodeIP);
    // TODO: fix the ut
    BOOST_CHECK(true == m_sessions.empty());
    boost::system::error_code ec;
    // accept successfully
    fakeAsioInterface->callAcceptHandler(ec);
    this_thread::sleep_for(chrono::milliseconds(50));
    BOOST_CHECK(1u == m_sessions.size());

    // accept failed
    fakeAsioInterface->callAcceptHandler(boost::asio::error::operation_aborted);
    // accept failed, cert is empty
    socket = fakeAsioInterface->m_acceptorInfo.first;
    nodeIP =
        NodeIPEndpoint(boost::asio::ip::address::from_string("127.0.0.1"), EMPTY_CERT_SOCKET_PORT);
    socket->setNodeIPEndpoint(nodeIP);
    fakeAsioInterface->callAcceptHandler(ec);
    BOOST_CHECK(1u == m_sessions.size());
    auto fp = [](NetworkException, NodeInfo const&, std::shared_ptr<SessionFace>) {};
    m_certBlacklist.push_back(
        string("7dcce48da1c464c7025614a54a4e26df7d6f92cd4d315601e057c1659796736c5c8730e380fcbe6"
               "37191cc2aebf4746846c0db2604adebf9c70c7f418d4d5a61"));
    m_host->setCRL(m_certBlacklist);
    // connect failed, nodeID is in blackList
    m_host->asyncConnect(nodeIP, fp);
    this_thread::sleep_for(chrono::milliseconds(50));
    BOOST_CHECK(1u == m_sessions.size());
    // clear blacklist
    m_certBlacklist.clear();
    m_host->setCRL(m_certBlacklist);
    // connect failed, cert is empty
    m_host->asyncConnect(nodeIP, fp);
    this_thread::sleep_for(chrono::milliseconds(50));
    BOOST_CHECK(1u == m_sessions.size());
    // connect failed, operation_aborted
    nodeIP = NodeIPEndpoint(boost::asio::ip::address::from_string("127.0.0.1"), ERROR_SOCKET_PORT);
    m_host->asyncConnect(nodeIP, fp);
    BOOST_CHECK(1u == m_sessions.size());
    // connect success
    nodeIP = NodeIPEndpoint(boost::asio::ip::address::from_string("127.0.0.1"), 8890);
    m_host->asyncConnect(nodeIP, fp);
    this_thread::sleep_for(chrono::milliseconds(50));
    BOOST_CHECK(2u == m_sessions.size());

    // Session unit test
    auto s = std::dynamic_pointer_cast<Session>(m_sessions[0]);
    s->nodeIPEndpoint();
    BOOST_CHECK(m_host == s->host().lock());
    BOOST_CHECK(m_messageFactory == s->messageFactory());
    BOOST_CHECK(true == s->actived());

    // close socket
    auto fakeSocket = std::dynamic_pointer_cast<FakeSocket>(s->socket());
    fakeSocket->close();
    this_thread::sleep_for(chrono::milliseconds(50));
    BOOST_CHECK(false == s->actived());
    BOOST_CHECK(2u == m_sessions.size());
    auto messageHandler = std::function<void(NetworkException, SessionFace::Ptr, Message::Ptr)>(
        [](NetworkException, SessionFace::Ptr, Message::Ptr m) { LOG(INFO) << m->length(); });
    s->setMessageHandler(messageHandler);
    // empty callback
    // TODO: fix this ut
    BOOST_CHECK(nullptr == s->getCallbackBySeq(0u));

    m_host->stop();
}
BOOST_AUTO_TEST_SUITE_END()

BOOST_FIXTURE_TEST_SUITE(SM_Host, SM_HostFixture)

BOOST_AUTO_TEST_CASE(ASIOInterface)
{
    m_asioInterface->ioService();
    m_asioInterface->sslContext();
    m_asioInterface->sslContext();
    m_asioInterface->newTimer(0);
    m_asioInterface->reset();
    m_asioInterface->stop();
    m_asioInterface->ASIOInterface::newSocket();
}

BOOST_AUTO_TEST_CASE(Hostfunctions)
{
    BOOST_CHECK(m_port == m_host->listenPort());
    BOOST_CHECK(m_hostIP == m_host->listenHost());
    string hostIP = "0.0.0.0";
    uint16_t port = 8546;
    m_host->setHostPort(hostIP, port);
    BOOST_CHECK(port == m_host->listenPort());
    BOOST_CHECK(hostIP == m_host->listenHost());
    BOOST_CHECK(false == m_host->haveNetwork());
    BOOST_CHECK(m_certBlacklist == m_host->certBlacklist());
    m_host->connectionHandler();
    BOOST_CHECK(m_asioInterface == m_host->asioInterface());
    BOOST_CHECK(m_sessionFactory == m_host->sessionFactory());
    BOOST_CHECK(m_messageFactory == m_host->messageFactory());
    BOOST_CHECK(m_threadPool == m_host->threadPool());
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test_Host
