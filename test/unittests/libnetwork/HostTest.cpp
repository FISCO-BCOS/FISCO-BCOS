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
        m_host->setSessionFactory(std::make_shared<dev::network::SessionFactory>());
        auto messageFactory = std::make_shared<P2PMessageFactory>();
        m_host->setMessageFactory(messageFactory);
        m_host->setHostPort("127.0.0.1", 8545);
        m_host->setThreadPool(std::make_shared<ThreadPool>("P2PTest", 2));
        std::vector<std::string> certBlacklist;
        m_host->setCRL(certBlacklist);
    }

    ~HostFixture() {}
    std::shared_ptr<dev::network::Host> m_host;
    std::shared_ptr<ASIOInterface> m_asioInterface;
};

BOOST_FIXTURE_TEST_SUITE(Host, HostFixture)

BOOST_AUTO_TEST_CASE(start)
{
    m_host->start();
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test_Host
