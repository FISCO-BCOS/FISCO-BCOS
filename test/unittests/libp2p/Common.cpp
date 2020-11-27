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


#include "libnetwork/Common.h"
#include "libp2p/Common.h"
#include <libp2p/P2PMessage.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>


using namespace bcos;
using namespace bcos::network;
using namespace bcos::p2p;

namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(P2pCommonTest, TestOutputHelperFixture)

/// test reasonOf and disconnect reasons
BOOST_AUTO_TEST_CASE(testDisconnectReason)
{
    reasonOf(bcos::network::DisconnectRequested);
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
    reasonOf(bcos::network::DisconnectReason(0x11));
    BOOST_CHECK_MESSAGE(true, "Unknown reason.");
}

BOOST_AUTO_TEST_CASE(testNodeIPEndpoint)
{
    /// test default construct
    bcos::network::NodeIPEndpoint m_endpoint(boost::asio::ip::make_address("0.0.0.0"), 0);
    BOOST_CHECK(m_endpoint.address() == "0.0.0.0");
    BOOST_CHECK(m_endpoint.port() == 0);
    BOOST_CHECK(boost::lexical_cast<std::string>(m_endpoint) == "0.0.0.0:0");
    /// "0.0.0.0" is not the specified address
    /// test construct: NodeIPEndpoint(bi::address _addr, uint16_t _udp, uint16_t _tcp)
    uint16_t port = 30303;
    NodeIPEndpoint m_endpoint2(boost::asio::ip::make_address("127.0.0.1"), port);
    BOOST_CHECK(m_endpoint2.address() == "127.0.0.1");
    BOOST_CHECK(m_endpoint2.port() == port);
    BOOST_CHECK_MESSAGE(true, "127.0.0.1:" + toString(port) + ":" + toString(port));

    /// test operators
    /// operator ==
    BOOST_CHECK(m_endpoint2 == NodeIPEndpoint(boost::asio::ip::make_address("127.0.0.1"), port));
    /// opearator <
    BOOST_CHECK(m_endpoint < m_endpoint2);
    /// test map
    std::map<NodeIPEndpoint, bool> m_endpoint_map;
    m_endpoint_map[m_endpoint] = true;
    BOOST_CHECK(m_endpoint_map.size() == 1);
    m_endpoint_map[m_endpoint2] = false;
    BOOST_CHECK(m_endpoint_map.size() == 2);
    BOOST_CHECK(m_endpoint_map[m_endpoint2] == false);
    m_endpoint_map[NodeIPEndpoint(boost::asio::ip::make_address("127.0.0.1"), port)] = true;
    BOOST_CHECK(m_endpoint_map[m_endpoint2] == true);
}

BOOST_AUTO_TEST_CASE(testMessage)
{
    auto msg = std::make_shared<p2p::P2PMessage>();
    msg->setProtocolID(2);
    msg->setPacketType(2);
    msg->setSeq(1);
    std::shared_ptr<bytes> buf(new bytes());
    std::string s = "hello world!";
    msg->setBuffer(buf);
    BOOST_CHECK(msg->length() == (p2p::P2PMessage::HEADER_LENGTH + buf->size()));
    buf->assign(s.begin(), s.end());

    auto buffer = std::make_shared<bytes>();
    msg->encode(*buffer);
    BOOST_CHECK(msg->length() == (p2p::P2PMessage::HEADER_LENGTH + buf->size()));

    auto message = std::make_shared<p2p::P2PMessage>();
    message->decode(buffer->data(), buffer->size());
    BOOST_CHECK(message->protocolID() == 2);
    BOOST_CHECK(message->packetType() == 2);
    BOOST_CHECK(message->seq() == 1);
    /// BOOST_CHECK(message->getResponceProtocolID() == -2);

    /*msg->encodeAMOPBuffer("topic");
    std::string t;
    msg->decodeAMOPBuffer(buffer, t);
    BOOST_CHECK_EQUAL("topic", t);*/
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
