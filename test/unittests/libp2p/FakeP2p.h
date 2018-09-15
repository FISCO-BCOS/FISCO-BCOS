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
 * @brief : main Fakes of p2p module
 *
 * @file FakeP2p.h
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
/// Fake of RLPXSocket
class FakeRLPXSocket : public RLPXSocket
{
public:
    FakeRLPXSocket(ba::io_service& _ioService, NodeIPEndpoint _nodeIPEndpoint)
      : RLPXSocket(_ioService, _nodeIPEndpoint)
    {}
    virtual bool isConnected() const { return m_isConnected; }
    void setConnection(bool const& _isConnected) { m_isConnected = _isConnected; }

private:
    bool m_isConnected;
};

/// Fakes of Host
class FakeHost : public Host
{
public:
    FakeHost(std::string const& _clientVersion, KeyPair const& _alias, NetworkConfig const& _n)
      : Host(_clientVersion, _alias, _n)
    {}

    ~FakeHost() {}
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

/// Peer fake
class FakePeer : public Peer
{
public:
    FakePeer(Node const& _node) : Peer(_node) {}
};

/// Session fake
class FakeSession : public Session
{
public:
    FakeSession(Host* _server, std::shared_ptr<RLPXSocket> const& _s,
        std::shared_ptr<Peer> const& _n, PeerSessionInfo _info)
      : Session(_server, _s, _n, _info)
    {}
};

/// create Host
static std::shared_ptr<Host> createFakeHost(
    std::string const& client_version, std::string const& listenIp, uint16_t const& listenPort)
{
    KeyPair key_pair = KeyPair::create();
    NetworkConfig network_config(listenIp, listenPort);
    shared_ptr<Host> m_host = std::make_shared<FakeHost>(client_version, key_pair, network_config);
    return m_host;
}

/// create session
static Session* createFakeSession(std::string const& _clientVersion, std::string const& _addr,
    uint16_t const& _udp, uint16_t const& _tcp)
{
    /// init host
    Host* m_host = createFakeHost(_clientVersion, _addr, _tcp).get();
    /// init endpoint
    NodeIPEndpoint m_endpoint(bi::address::from_string(_addr), _udp, _tcp);
    /// init FakeRLPXSocket
    std::shared_ptr<RLPXSocket> m_socket =
        std::make_shared<FakeRLPXSocket>(*(m_host->ioService()), m_endpoint);

    /// init Peers
    std::shared_ptr<Peer> m_peer =
        std::make_shared<FakePeer>(NodeFixture::getNode("127.0.0.1", 30305, 30305));
    /// init the endpoint of peers
    uint16_t m_peer_listenport = 30305;
    NodeIPEndpoint m_peer_endpoint(
        m_socket->remoteEndpoint().address(), m_peer_listenport, m_peer_listenport);

    /// init PeerSessionInfo
    PeerSessionInfo m_peerInfo = {m_host->id(), m_host->clientVersion(),
        m_peer->endpoint().address.to_string(), m_host->listenPort(),
        chrono::steady_clock::duration(), 0, map<std::string, std::string>(), m_peer_endpoint};

    Session* m_session = new FakeSession(m_host, m_socket, m_peer, m_peerInfo);
    return m_session;
}
}  // namespace test
}  // namespace dev
