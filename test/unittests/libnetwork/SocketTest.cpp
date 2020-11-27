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
 * @brief: unit test for Socket
 *
 * @file Socket.cpp
 * @author: yujiechen
 * @date 2018-09-19
 */

#include "libnetwork/Socket.h"

#include <libinitializer/SecureInitializer.h>
#include <openssl/ssl.h>
#include <test/tools/libutils/Common.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::network;

namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(SocketTest, TestOutputHelperFixture)
BOOST_AUTO_TEST_CASE(testSocket)
{
    ba::io_service m_io_service;
    std::string address = "127.0.0.1";
    int port = 30303;
    NodeIPEndpoint m_endpoint(boost::asio::ip::make_address(address), port);
    boost::property_tree::ptree pt;
    pt.put("secure.data_path", getTestPath().string() + "/fisco-bcos-data/");
    auto secureInitializer = std::make_shared<bcos::initializer::SecureInitializer>();
    /// secureInitializer->setDataPath(getTestPath().string() + "/fisco-bcos-data/");
    secureInitializer->initConfig(pt);
    auto sslContext = secureInitializer->SSLContext();

    Socket m_socket(m_io_service, *sslContext, m_endpoint);
    m_socket.ref();
    m_socket.sslref();
    m_socket.wsref();
    auto endpoint = m_socket.nodeIPEndpoint();
    m_socket.setNodeIPEndpoint(endpoint);
    BOOST_CHECK(m_socket.isConnected() == false);
    BOOST_CHECK(SSL_get_verify_mode(m_socket.sslref().native_handle()) ==
                (boost::asio::ssl::verify_peer | boost::asio::ssl::verify_fail_if_no_peer_cert));
    /// close socket
    m_socket.close();
    BOOST_CHECK(m_socket.isConnected() == false);
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
