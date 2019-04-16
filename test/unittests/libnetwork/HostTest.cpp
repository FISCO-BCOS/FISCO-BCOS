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
#include "libp2p/P2PMessageFactory.h"
#include <boost/test/unit_test.hpp>

using namespace dev;
using namespace std;
using namespace dev::network;
using namespace dev::p2p;

namespace test_Host
{
struct HostFixture
{
    HostFixture()
    {
        std::shared_ptr<boost::asio::ssl::context> sslContext =
            std::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::tlsv12);
        auto asioInterface = std::make_shared<dev::network::FakeASIOInterface>();
        asioInterface->setIOService(std::make_shared<ba::io_service>());
        asioInterface->setSSLContext(sslContext);
        asioInterface->setType(dev::network::ASIOInterface::SSL);

        m_host = std::make_shared<dev::network::Host>();
        m_host->setASIOInterface(asioInterface);
        m_sessionFactory = std::make_shared<dev::network::SessionFactory>();
        m_host->setSessionFactory(m_sessionFactory);
        m_messageFactory = std::make_shared<P2PMessageFactory>();
        m_host->setMessageFactory(m_messageFactory);

        m_host->setHostPort(m_hostIP, m_port);
        m_threadPool = std::make_shared<ThreadPool>("P2PTest", 2);
        m_host->setThreadPool(m_threadPool);
        m_host->setCRL(m_certBlacklist);
        m_host->setConnectionHandler(
            [](dev::network::NetworkException e, dev::network::NodeInfo const&,
                std::shared_ptr<dev::network::SessionFace>) {
                if (e.errorCode())
                {
                    LOG(ERROR) << e.what();
                }
            });
        m_connectionHandler = m_host->connectionHandler();
    }

    ~HostFixture() {}
    string m_hostIP = "127.0.0.1";
    uint16_t m_port = 8545;
    std::shared_ptr<dev::network::Host> m_host;
    MessageFactory::Ptr m_messageFactory;
    std::shared_ptr<SessionFactory> m_sessionFactory;
    std::shared_ptr<dev::ThreadPool> m_threadPool;
    std::vector<std::string> m_certBlacklist;
    std::shared_ptr<ASIOInterface> m_asioInterface;
    std::function<void(NetworkException, NodeInfo const&, std::shared_ptr<SessionFace>)>
        m_connectionHandler;
};

BOOST_FIXTURE_TEST_SUITE(Host, HostFixture)

BOOST_AUTO_TEST_CASE(functions)
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
    m_host->setASIOInterface(m_asioInterface);
    BOOST_CHECK(m_asioInterface == m_host->asioInterface());
    BOOST_CHECK(m_sessionFactory == m_host->sessionFactory());
    BOOST_CHECK(m_messageFactory == m_host->messageFactory());
    BOOST_CHECK(m_threadPool == m_host->threadPool());

    // m_host->start();
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test_Host
