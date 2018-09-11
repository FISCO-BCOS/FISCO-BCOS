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
 * @brief : main Fixtures of p2p module
 *
 * @file P2pFixtures.h
 * @author: yujiechen
 * @date 2018-09-10
 */
#pragma once
#include <libp2p/Session.h>

using namespace dev;
using namespace dev::p2p;
namespace dev
{
namespace test
{
/// Fixture of RLPXSocket
class RLPXSocketFixture : public RLPXSocket
{
public:
    RLPXSocketFixture(ba::io_service& _ioService, NodeIPEndpoint _nodeIPEndpoint)
      : RLPXSocket(_ioService, _nodeIPEndpoint)
    {}
    virtual bool isConnected() const { return m_isConnected; }
    void setConnection(bool const& _isConnected) { m_isConnected = _isConnected; }

private:
    bool m_isConnected;
};

/// Fixture of Host
class HostFixture : public Host
{
public:
    HostFixture(std::string const& _clientVersion, KeyPair const& _alias,
        NetworkConfig const& _n = NetworkConfig())
      : Host(_clientVersion, _alias, _n)
    {}

    ~HostFixture() {}
};

/// class for construct Node
class NodeFixture
{
public:
    static Node& getNode(std::string const& ip_addr, uint16_t const& _udp, uint16_t const& _tcp)
    {
        KeyPair key_pair = KeyPair::create();
        NodeIPEndpoint m_endpoint(bi::address::from_string(ip_addr), _udp, _tcp);
        static Node m_node(key_pair.pub(), m_endpoint);
        return m_node;
    }
};

/// Peer Fixture
class PeerFixture : public Peer
{
public:
    PeerFixture(Node const& _node) : Peer(_node) {}
};

/// Session Fixture
class SessionFixture : public Session
{
public:
    SessionFixture(Host* _server, std::shared_ptr<RLPXSocket> const& _s,
        std::shared_ptr<Peer> const& _n, PeerSessionInfo _info)
      : Session(_server, _s, _n, _info)
    {}
};

/// create Host
static Host* creatHostFixture()
{
    std::string client_version = "libp2p-test";
    KeyPair key_pair = KeyPair::create();
    NetworkConfig network_config(30304);
    Host* m_host = new HostFixture(client_version, key_pair, network_config);
    return m_host;
}

/// create session
static Session* createSessionFixture(
    std::string const& _addr, uint16_t const& _udp, uint16_t const& _tcp)
{
    /// init host
    Host* m_host = creatHostFixture();
    /// init endpoint
    NodeIPEndpoint m_endpoint(bi::address::from_string(_addr), _udp, _tcp);
    /// init RLPXSocketFixture
    std::shared_ptr<RLPXSocket> m_socket =
        std::make_shared<RLPXSocketFixture>(*(m_host->getIOService()), m_endpoint);

    /// init Peers
    std::shared_ptr<Peer> m_peer =
        std::make_shared<PeerFixture>(NodeFixture::getNode("127.0.0.1", 30305, 30305));
    /// init the endpoint of peers
    uint16_t m_peer_listenport = 30305;
    NodeIPEndpoint m_peer_endpoint(
        m_socket->remoteEndpoint().address(), m_peer_listenport, m_peer_listenport);

    /// init PeerSessionInfo
    std::set<CapDesc> caps = {};
    PeerSessionInfo m_peerInfo = {m_host->id(), m_host->getClientVersion(),
        m_peer->endpoint().address.to_string(), m_host->listenPort(),
        chrono::steady_clock::duration(), caps, 0, map<std::string, std::string>(),
        m_peer_endpoint};

    Session* m_session = new SessionFixture(m_host, m_socket, m_peer, m_peerInfo);
    return m_session;
}

}  // namespace test
}  // namespace dev
