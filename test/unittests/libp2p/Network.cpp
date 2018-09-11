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
 * @brief: unit test for Network.*
 *
 * @file Network.cpp
 * @author: yujiechen
 * @date 2018-09-11
 */
#include <libp2p/Network.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>

using namespace dev;
using namespace dev::p2p;
namespace dev
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(NetworkTest, TestOutputHelperFixture)

/// test getInterfaceAddresses
BOOST_AUTO_TEST_CASE(testGetInterfaceAddresses)
{
    std::set<bi::address> addresses = Network::getInterfaceAddresses();
    for (auto address : addresses)
    {
        BOOST_CHECK(address.is_loopback() == false);
        BOOST_CHECK(address.is_multicast() == false);
        BOOST_CHECK(address.to_string() != "127.0.0.1");
        BOOST_CHECK(address.to_string() != "0.0.0.0");
    }
}
/// test tcp4Listen
BOOST_AUTO_TEST_CASE(testTcp4Listen)
{
    ba::io_service m_ioService(1);
    bi::tcp::acceptor m_acceptor(m_ioService);
    NetworkConfig m_netConfig("127.0.0.1", 8033);
    BOOST_CHECK(Network::tcp4Listen(m_acceptor, m_netConfig) == m_netConfig.listenPort);
    /// bind again
    BOOST_CHECK(Network::tcp4Listen(m_acceptor, m_netConfig) == -1);
}

/// test resolveHost
BOOST_AUTO_TEST_CASE(testResolveHost)
{
    // bi::tcp::endpoint Network::resolveHost(string const& _addr)
    std::string ip_addr = "10.10.0.1";
    std::string port = "8080";
    std::string addr = ip_addr + ":" + port;
    /// test normal case
    bi::tcp::endpoint m_endpoint = Network::resolveHost(addr);
    BOOST_CHECK(m_endpoint.address().to_string() == ip_addr);
    BOOST_CHECK(toString(m_endpoint.port()) == port);
    /// test default port case
    m_endpoint = Network::resolveHost(ip_addr);
    BOOST_CHECK(m_endpoint.port() == dev::p2p::c_defaultIPPort);
    BOOST_CHECK(m_endpoint.address().to_string() == ip_addr);
    /// test invalid ip_addr
    ip_addr = "1000.100.0.1:12324324324324";
    m_endpoint = Network::resolveHost(ip_addr);
    bi::tcp::endpoint m_endpoint_empty;
    BOOST_CHECK(m_endpoint == m_endpoint_empty);
    /// test dns
    /*addr = "www.baidu.com:8080";
    m_endpoint = Network::resolveHost(addr);
    std::cout<<"address:"<<m_endpoint.address().to_string()<<std::endl;*/
}
BOOST_AUTO_TEST_CASE(testDeterminPublic)
{
    auto ifAddresses = Network::getInterfaceAddresses();
    bi::address addr;
    for (auto address : ifAddresses)
    {
        if (address.is_v4())
        {
            addr = address;
            break;
        }
    }
    /// test normal case
    uint16_t listen_port = 8033;
    NetworkConfig m_netConfig(addr.to_string(), listen_port);
    bi::tcp::endpoint m_endpoint = Network::determinePublic(m_netConfig);
    BOOST_CHECK(m_endpoint.address().to_string() == addr.to_string());
    /// test listen address 0.0.0.0 case
    m_netConfig.listenIPAddress = "0.0.0.0";
    m_endpoint = Network::determinePublic(m_netConfig);
    BOOST_CHECK(m_endpoint.address().to_string() != m_netConfig.listenIPAddress);
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev
