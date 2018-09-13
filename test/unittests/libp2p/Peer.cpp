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
 * @brief: unit test for Peer
 *
 * @file Peer.cpp
 * @author: yujiechen
 * @date 2018-09-10
 */
#include "FakeHost.h"
#include <libp2p/Common.h>
#include <libp2p/Peer.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>

using namespace dev;
using namespace dev::p2p;
namespace dev
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(PeerTest, TestOutputHelperFixture)
BOOST_AUTO_TEST_CASE(testNode)
{
    uint16_t udp;
    uint16_t tcp = udp = 30304;
    Node node = NodeFixture::getNode("127.0.0.1", udp, tcp);
    /// check get interfaces
    BOOST_CHECK(node.id());
    BOOST_CHECK(node.address() == node.publicKey());
    BOOST_CHECK(node.publicKey() == node.id());
    BOOST_CHECK(node.endpoint().address.to_string() == "127.0.0.1");
    BOOST_CHECK(node.endpoint().host.empty() == true);  /// DNS
    BOOST_CHECK(node.endpoint().tcpPort == tcp);
    BOOST_CHECK(node.endpoint().udpPort == udp);
    BOOST_CHECK(bool(node));
    /// test setting intefaces
    node.setId(Public(0));
    node.endpoint().host = "www.baidu.com";
    BOOST_CHECK(!bool(node));
    BOOST_CHECK(!node.endpoint().host.empty());
}

BOOST_AUTO_TEST_CASE(testPeer)
{
    uint16_t udp;
    uint16_t tcp = udp = 30304;
    Peer m_peer(NodeFixture::getNode("127.0.0.1", udp, tcp));
    ///--------check shouldReconnect--------
    /// 1. check NoDisconnect
    BOOST_CHECK(m_peer.lastDisconnect() == NoDisconnect);
    m_peer.setLastAttempted(chrono::system_clock::now() - chrono::seconds(6));
    BOOST_CHECK(m_peer.shouldReconnect());
    m_peer.setLastAttempted(chrono::system_clock::now());
    BOOST_CHECK(m_peer.shouldReconnect() == false);
    /// 2. check BadProtocol
    m_peer.setLastDisconnect(BadProtocol);
    m_peer.setLastAttempted(chrono::system_clock::now() - chrono::seconds(31));
    BOOST_CHECK(m_peer.shouldReconnect());
    m_peer.setLastAttempted(chrono::system_clock::now() - chrono::seconds(29));
    BOOST_CHECK(m_peer.shouldReconnect() == false);
    /// 3. check uselessPeer && TooManyPeers
    m_peer.setLastDisconnect(UselessPeer);
    BOOST_CHECK(m_peer.shouldReconnect());
    m_peer.setLastDisconnect(TooManyPeers);
    BOOST_CHECK(m_peer.shouldReconnect());
    m_peer.setLastAttempted(chrono::system_clock::now() - chrono::seconds(24));
    BOOST_CHECK(m_peer.shouldReconnect() == false);
    /// 4. check ClientQuit
    m_peer.setLastDisconnect(ClientQuit);
    BOOST_CHECK(m_peer.shouldReconnect());
    m_peer.setLastAttempted(chrono::system_clock::now() - chrono::seconds(14));
    BOOST_CHECK(m_peer.shouldReconnect() == false);
    /// 5. check default
    m_peer.setLastDisconnect(DisconnectRequested);
    BOOST_CHECK(m_peer.shouldReconnect());
    m_peer.setLastAttempted(chrono::system_clock::now() - chrono::seconds(4));
    BOOST_CHECK(m_peer.shouldReconnect() == false);
    /// 5 < m_failedAttempts < 15
    m_peer.setfailedAttempts(10);
    m_peer.setLastAttempted(chrono::system_clock::now() - chrono::seconds(76));
    BOOST_CHECK(m_peer.shouldReconnect() == true);
    m_peer.setLastAttempted(chrono::system_clock::now() - chrono::seconds(74));
    BOOST_CHECK(m_peer.shouldReconnect() == false);
    /// m_failedAttempts >= 15
    m_peer.setfailedAttempts(16);
    m_peer.setLastAttempted(chrono::system_clock::now() - chrono::seconds(146));
    BOOST_CHECK(m_peer.shouldReconnect());
    m_peer.setLastAttempted(chrono::system_clock::now() - chrono::seconds(144));
    BOOST_CHECK(m_peer.shouldReconnect() == false);
    ///--------check operator < --------
    Peer m_peer_copy(NodeFixture::getNode("127.0.0.1", udp, tcp));
    BOOST_CHECK(m_peer_copy < m_peer);
    std::chrono::system_clock::time_point last_attempted = chrono::system_clock::now();
    m_peer_copy.setLastAttempted(last_attempted);
    m_peer.setLastAttempted(last_attempted);
    BOOST_CHECK(m_peer_copy < m_peer);
    m_peer_copy.setfailedAttempts(16);
    BOOST_CHECK((m_peer_copy < m_peer) == (m_peer_copy.id() < m_peer.id()));
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev
