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
 * @brief: unit test for common
 *
 * @file Common.cpp
 * @author: yujiechen
 * @date 2018-09-10
 */
#include <libdevcore/Assertions.h>
#include <libp2p/Common.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>

using namespace dev;
using namespace dev::p2p;
namespace dev
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(P2pCommonTest, TestOutputHelperFixture)
/// test isPublicAddress
BOOST_AUTO_TEST_CASE(testisPublicAddress)
{
    std::string address_to_check = "0.0.0.0";
    BOOST_CHECK(isPublicAddress(address_to_check) == false);
    address_to_check = "";
    BOOST_CHECK(isPublicAddress(address_to_check) == false);
    address_to_check = "10.10";
    BOOST_CHECK_THROW(isPublicAddress(address_to_check), std::exception);
    address_to_check = "10.10.10.10";
    BOOST_CHECK(isPublicAddress(address_to_check) == true);
    address_to_check = "127.0.0.1";
    BOOST_CHECK(isPublicAddress(address_to_check) == true);
}

/// test isLocalHostAddress
BOOST_AUTO_TEST_CASE(testisLocalHostAddress)
{
    std::string address_to_check = "0.0.0.0";
    BOOST_CHECK(isLocalHostAddress(address_to_check) == true);
    address_to_check = "127.0.0.1";
    BOOST_CHECK(isLocalHostAddress(address_to_check) == true);
    address_to_check = "";
    BOOST_CHECK(isLocalHostAddress(address_to_check) == false);
    address_to_check = "10.01";
    BOOST_CHECK_THROW(isLocalHostAddress(address_to_check), std::exception);
    address_to_check = "192.10.10.18";
    BOOST_CHECK(isLocalHostAddress(address_to_check) == false);
}

/// test exceptions related to p2p
BOOST_AUTO_TEST_CASE(testException)
{
    BOOST_CHECK_THROW(
        assertThrow(false, NetworkStartRequired, "NetworkStartRequired"), NetworkStartRequired);
    BOOST_CHECK_THROW(assertThrow(false, InvalidPublicIPAddress, "InvalidPublicIPAddress"),
        InvalidPublicIPAddress);
    BOOST_CHECK_THROW(
        assertThrow(false, InvalidHostIPAddress, "InvalidHostIPAddress"), InvalidHostIPAddress);
}

/// test reasonOf and disconnect reasons
BOOST_AUTO_TEST_CASE(testDisconnectReason)
{
    reasonOf(DisconnectRequested);
    BOOST_CHECK_MESSAGE(true, "Disconnect was requested.");
    reasonOf(TCPError);
    BOOST_CHECK_MESSAGE(true, "Low-level TCP communication error.");
    reasonOf(BadProtocol);
    BOOST_CHECK_MESSAGE(true, "Data format error.");
    reasonOf(UselessPeer);
    BOOST_CHECK_MESSAGE(true, "Peer had no use for this node.");
    reasonOf(TooManyPeers);
    BOOST_CHECK_MESSAGE(true, "Peer had too many connections.");
    reasonOf(DuplicatePeer);
    BOOST_CHECK_MESSAGE(true, "Peer was already connected.");
    reasonOf(IncompatibleProtocol);
    BOOST_CHECK_MESSAGE(true, "Peer protocol versions are incompatible.");
    reasonOf(NullIdentity);
    BOOST_CHECK_MESSAGE(true, "Null identity given.");
    reasonOf(ClientQuit);
    BOOST_CHECK_MESSAGE(true, "Peer is exiting.");
    reasonOf(UnexpectedIdentity);
    BOOST_CHECK_MESSAGE(true, "Unexpected identity given.");
    reasonOf(LocalIdentity);
    BOOST_CHECK_MESSAGE(true, "Connected to ourselves.");
    reasonOf(UserReason);
    BOOST_CHECK_MESSAGE(true, "Subprotocol reason.");
    reasonOf(NoDisconnect);
    BOOST_CHECK_MESSAGE(true, "(No disconnect has happened.)");
    reasonOf(DisconnectReason(0x11));
    BOOST_CHECK_MESSAGE(true, "Unknown reason.");
}

BOOST_AUTO_TEST_CASE(testHostResolver)
{
    std::string host = "www.baidu.com";
    BOOST_REQUIRE_NO_THROW(HostResolver::query(host));
    host = "test";
    BOOST_CHECK(HostResolver::query(host).to_string() == "0.0.0.0");
    host = "0.0.0.0";
    BOOST_CHECK(HostResolver::query(host).to_string() == host);
    host = "127.0.0.1";
    BOOST_CHECK(HostResolver::query(host).to_string() == host);
}

/// test NodeInfo
BOOST_AUTO_TEST_CASE(testNodeInfo)
{
    /// test default constructor
    NodeInfo node_info;
    BOOST_CHECK(node_info.id == NodeID());
    BOOST_CHECK(node_info.address == "");
    BOOST_CHECK(node_info.port == 0);
    BOOST_CHECK(node_info.version == "");
    /// test copy constructor
    NodeID tmp_node_id = KeyPair::create().pub();
    NodeInfo node_info2(tmp_node_id, "127.0.0.1", 30303, "9.0");
    BOOST_CHECK(node_info2.id == tmp_node_id);
    BOOST_CHECK(node_info2.address == "127.0.0.1");
    BOOST_CHECK(node_info2.port == 30303);
    BOOST_CHECK(node_info2.version == "9.0");
    /// test encode
    node_info2.enode();
    BOOST_CHECK_MESSAGE(true, "enode://" + node_info2.id.hex() + "@" + node_info2.address + ":" +
                                  toString(node_info2.port));
}

BOOST_AUTO_TEST_CASE(testNodeIPEndpoint)
{
    /// test default construct
    NodeIPEndpoint m_endpoint;
    BOOST_CHECK(NodeIPEndpoint::test_allowLocal == false);
    BOOST_CHECK(m_endpoint.address.to_string() == "0.0.0.0");
    BOOST_CHECK(m_endpoint.udpPort == 0);
    BOOST_CHECK(m_endpoint.tcpPort == 0);
    BOOST_CHECK(m_endpoint.host == "");
    BOOST_CHECK(bool(m_endpoint) == false);
    BOOST_CHECK(m_endpoint.isAllowed() == false);
    BOOST_CHECK(m_endpoint.name() == "0.0.0.0:0:0");
    BOOST_CHECK(m_endpoint.isValid() == false);
    /// "0.0.0.0" not the public address
    m_endpoint.address = bi::address::from_string("0.0.0.0");
    BOOST_CHECK(m_endpoint.isAllowed() == false);
    /// "0.0.0.0" is not the specified address
    NodeIPEndpoint::test_allowLocal = true;
    BOOST_CHECK(m_endpoint.isAllowed() == false);
    m_endpoint.address = bi::address::from_string("10.0.0.0");
    BOOST_CHECK(m_endpoint.isAllowed() == true);
    /// test construct: NodeIPEndpoint(bi::address _addr, uint16_t _udp, uint16_t _tcp)
    uint16_t port = 30303;
    NodeIPEndpoint m_endpoint2(bi::address::from_string("127.0.0.1"), port, port);
    BOOST_CHECK(m_endpoint2.address.to_string() == "127.0.0.1");
    BOOST_CHECK(m_endpoint2.udpPort == port);
    BOOST_CHECK(m_endpoint2.tcpPort == port);
    BOOST_CHECK(m_endpoint2.host == "");
    BOOST_CHECK(bool(m_endpoint2) == true);
    m_endpoint2.name();
    BOOST_CHECK_MESSAGE(true, "127.0.0.1:" + toString(port) + ":" + toString(port));
    /// specified address
    BOOST_CHECK(m_endpoint.isAllowed() == true);
    BOOST_CHECK(m_endpoint2.isValid() == true);
    /// test endpoint convert
    BOOST_CHECK(bi::udp::endpoint(m_endpoint2) ==
                bi::udp::endpoint(m_endpoint2.address, m_endpoint2.udpPort));
    BOOST_CHECK(bi::tcp::endpoint(m_endpoint2) ==
                bi::tcp::endpoint(m_endpoint2.address, m_endpoint2.tcpPort));
    /// test operators
    /// operator ==
    BOOST_CHECK(m_endpoint2 == NodeIPEndpoint(bi::address::from_string("127.0.0.1"), port, port));
    /// operator !=
    BOOST_CHECK(m_endpoint2 != m_endpoint);
    /// opearator <
    BOOST_CHECK(m_endpoint < m_endpoint2);
    /// test construct: NodeIPEndpoint(std::string _host, uint16_t _udp, uint16_t _tcp)
    NodeIPEndpoint m_endpoint3("www.baidu.com", port, port);
    BOOST_CHECK(m_endpoint3.host == "www.baidu.com");
    BOOST_CHECK(m_endpoint3.address.to_string().empty() == false);
    BOOST_CHECK(m_endpoint3.address.to_string() != "127.0.0.1");
    BOOST_CHECK(m_endpoint3.address.to_string() != "0.0.0.0");
    /// test map
    std::map<NodeIPEndpoint, bool> m_endpoint_map;
    m_endpoint_map[m_endpoint] = true;
    BOOST_CHECK(m_endpoint_map.size() == 1);
    m_endpoint_map[m_endpoint2] == false;
    BOOST_CHECK(m_endpoint_map.size() == 2);
    BOOST_CHECK(m_endpoint_map[m_endpoint2] == false);
    m_endpoint_map[NodeIPEndpoint(bi::address::from_string("127.0.0.1"), port, port)] = true;
    BOOST_CHECK(m_endpoint_map[m_endpoint2] == true);
}
/// test PeerSessionInfo
BOOST_AUTO_TEST_CASE(testPeerSessionInfo)
{
    NodeID node_id = KeyPair::create().pub();
    PeerSessionInfo peer_session_info = {
        node_id, "www.baidu.com", std::chrono::steady_clock::duration(), 0};
    BOOST_CHECK(peer_session_info.id == node_id);
    BOOST_CHECK(peer_session_info.host == "www.baidu.com");
}

/// test NodeSpec
BOOST_AUTO_TEST_CASE(testNodeSpec)
{
    std::string address = "127.0.0.1";
    uint16_t port = 30303;
    NodeID node_id = KeyPair::create().pub();
    /// test default constructor
    NodeSpec node_spec;
    node_spec.setAddress(address);
    node_spec.setTcpPort(port);
    node_spec.setUdpPort(port);
    node_spec.setNodeId(node_id);
    BOOST_CHECK(node_spec.id() == node_id);
    BOOST_CHECK(node_spec.nodeIPEndpoint() == NodeIPEndpoint(address, port, port));
    BOOST_CHECK(node_spec.address() == address);
    BOOST_CHECK(node_spec.tcpPort() == port);
    BOOST_CHECK(node_spec.udpPort() == port);
    std::string node_spec_str = node_spec.enode();
    /// construct NodeSpec from string
    NodeSpec node_spec2(node_spec_str);
    BOOST_CHECK(node_spec2.id() == node_id);
    BOOST_CHECK(node_spec2.nodeIPEndpoint() == NodeIPEndpoint(address, port, port));
    BOOST_CHECK(node_spec2.address() == address);
    BOOST_CHECK(node_spec2.tcpPort() == port);
    BOOST_CHECK(node_spec2.udpPort() == port);
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev
