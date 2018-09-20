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
#include <libp2p/Host.h>
#include <libp2p/P2pFactory.h>
#include <libp2p/SessionFace.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
using namespace dev;
using namespace dev::p2p;
namespace dev
{
namespace test
{
class FakeSessionForHost : public SessionFace,
                           public std::enable_shared_from_this<FakeSessionForHost>
{
public:
    FakeSessionForHost(Host* _server, std::shared_ptr<Peer> const& _n, PeerSessionInfo _info,
        std::shared_ptr<SocketFace> const& _socket = nullptr)
      : m_host(_server), m_peer(_n), m_info(_info)
    {
        m_ping = m_lastReceived = m_connectionTime = chrono::steady_clock::now();
        m_disconnect = false;
    }
    void start() override
    {
        m_connectionTime = chrono::steady_clock::now();
        m_start = true;
        m_disconnect = false;
    }
    void disconnect(DisconnectReason _reason) { m_disconnect = true; }
    bool isConnected() const { return !m_disconnect; }
    NodeID id() const { return NodeID(m_peer->id()); }
    PeerSessionInfo info() const { return m_info; }
    std::chrono::steady_clock::time_point connectionTime() { return m_connectionTime; }
    std::shared_ptr<Peer> peer() const { return m_peer; }
    std::chrono::steady_clock::time_point lastReceived() const { return m_lastReceived; }
    void setP2PMsgHandler(std::shared_ptr<P2PMsgHandler> _p2pMsgHandler) {}
    void send(std::shared_ptr<bytes> _msg) {}

public:
    bool m_start = false;
    bool m_disconnect = false;
    Host* m_host;
    std::shared_ptr<Peer> m_peer;
    PeerSessionInfo m_info;
    std::chrono::steady_clock::time_point m_connectionTime;
    std::chrono::steady_clock::time_point m_lastReceived;
    std::chrono::steady_clock::time_point m_ping;
};

class FakeSessionFactory : public SessionFactory
{
public:
    virtual std::shared_ptr<SessionFace> create_session(Host* _server,
        std::shared_ptr<SocketFace> const& _socket, std::shared_ptr<Peer> const& _peer,
        PeerSessionInfo _info)
    {
        std::shared_ptr<SessionFace> m_session =
            std::make_shared<FakeSessionForHost>(_server, _peer, _info, _socket);
        return m_session;
    }
};

/// Fakes of Host
class FakeHost : public Host
{
public:
    FakeHost(std::string const& _clientVersion, KeyPair const& _alias, NetworkConfig const& _n,
        std::shared_ptr<AsioInterface>& m_asioInterface, shared_ptr<SocketFactory>& _socketFactory,
        shared_ptr<SessionFactory>& _sessionFactory)
      : Host(_clientVersion, _alias, _n, m_asioInterface, _socketFactory, _sessionFactory)
    {
        setLastPing(chrono::steady_clock::now());
        m_run = true;
    }
    ~FakeHost() {}
    bi::tcp::endpoint tcpClient() { return m_tcpClient; }
    bi::tcp::endpoint tcpPublic() { return m_tcpPublic; }
    void setSessions(std::shared_ptr<SessionFace> session)
    {
        RecursiveGuard l(x_sessions);
        m_sessions[session->id()] = session;
        m_peers[session->id()] = session->peer();
    }

    void keepAlivePeers() { Host::keepAlivePeers(); }
    virtual void runAcceptor(boost::system::error_code boost_error = boost::system::error_code())
    {
        /// to stop runAcceptor
        if (m_loop == 1)
        {
            m_loop = 0;
            return;
        }
        m_loop = 1;
        Host::runAcceptor(boost_error);
    }

    void run(boost::system::error_code const& error) { Host::run(error); }

    std::shared_ptr<SessionFace> FakeSession(NodeIPEndpoint const& _nodeIPEndpoint)
    {
        KeyPair key_pair = KeyPair::create();
        std::shared_ptr<Peer> peer = std::make_shared<Peer>(key_pair.pub(), _nodeIPEndpoint);
        ;
        PeerSessionInfo peer_info = {key_pair.pub(), _nodeIPEndpoint.address.to_string(),
            std::chrono::steady_clock::duration(), 0};
        std::shared_ptr<SessionFace> session =
            std::make_shared<FakeSessionForHost>(this, peer, peer_info);
        session->start();
        return session;
    }

    void setStaticNodes(const std::map<NodeIPEndpoint, NodeID>& staticNodes)
    {
        Host::setStaticNodes(staticNodes);
    }
    void connect(NodeIPEndpoint const& _nodeIPEndpoint,
        boost::system::error_code ec = boost::system::error_code())
    {
        Host::connect(_nodeIPEndpoint, ec);
    }

    bool havePeerSession(NodeID const& _id) { return Host::havePeerSession(_id); }
    void reconnectAllNodes() { return Host::reconnectAllNodes(); }
    bool accepting() { return m_accepting; }
    void setRun(bool run) { m_run = run; }
    void setAccepting(bool isAccept) { m_accepting = isAccept; }
    void setLastPing(std::chrono::steady_clock::time_point time_point) { m_lastPing = time_point; }
    std::function<bool(bool, boost::asio::ssl::verify_context&)> newVerifyCallback(
        std::shared_ptr<std::string> nodeIDOut)
    {
        return [this, nodeIDOut](bool preverified, boost::asio::ssl::verify_context& ctx) {
            std::string node_id = toHex(m_node_id);
            nodeIDOut->assign(node_id.c_str());
            return m_verify;
        };
    }
    void handshakeClient(const boost::system::error_code& error, std::shared_ptr<SocketFace> socket,
        std::shared_ptr<std::string>& endpointPublicKey, NodeIPEndpoint& _nodeIPEndpoint)
    {
        Host::handshakeClient(error, socket, endpointPublicKey, _nodeIPEndpoint);
    }

    void handshakeServer(const boost::system::error_code& error,
        std::shared_ptr<std::string>& endpointPublicKey, std::shared_ptr<SocketFace> socket)
    {
        Host::handshakeServer(error, endpointPublicKey, socket);
    }
    void disableReconnect() { m_reconnectnow = false; }
    void setNodeId(NodeID const& _nodeid) { m_node_id = _nodeid; }
    void setVerifyResult(bool _verify) { m_verify = _verify; }
    bool hasPendingConn(std::string const& name) { return m_pendingPeerConns.count(name); }
    bi::tcp::acceptor const& getAcceptor() const { return m_tcp4Acceptor; }
    NodeID m_node_id;
    bool m_verify;
    int m_loop = 0;
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

/// fake RLPXSocket
class FakeSocket : public SocketFace
{
public:
    FakeSocket(ba::io_service& _ioService, NodeIPEndpoint const& remote_endpoint)
      : m_sslContext(ba::ssl::context::tlsv12)
    {
        m_remote = std::make_shared<bi::tcp::endpoint>(
            remote_endpoint.address, remote_endpoint.tcpPort + 10);
        m_nodeIPEndpoint = remote_endpoint;
        if (remote_endpoint.tcpPort == 0)
        {
            std::cout << "####fake remote_point: 30314" << std::endl;
            NodeIPEndpoint m_fake_endpoint = fakeEndPoint("127.0.0.1", 30304);
            m_remote->address(m_fake_endpoint.address);
            m_remote->port(30314);
            m_nodeIPEndpoint = m_fake_endpoint;
            m_nodeIPEndpoint.host = "127.0.0.1";
        }
        m_close = false;
        m_sslContext.set_options(
            boost::asio::ssl::context::default_workarounds | boost::asio::ssl::context::no_sslv2 |
            boost::asio::ssl::context::no_sslv3 | boost::asio::ssl::context::no_tlsv1);
        m_sslContext.set_verify_depth(3);
        m_sslContext.set_verify_mode(ba::ssl::verify_peer);
        m_sslSocket = std::make_shared<ba::ssl::stream<bi::tcp::socket> >(_ioService, m_sslContext);
    }

    NodeIPEndpoint fakeEndPoint(std::string const& addr, uint16_t port)
    {
        NodeIPEndpoint m_endpoint(bi::address::from_string(addr), port, port);
        return m_endpoint;
    }
    virtual bool isConnected() const { return !m_close; }
    virtual void close() { m_close = true; }
    virtual bi::tcp::endpoint remoteEndpoint() { return *m_remote; }
    virtual bi::tcp::socket& ref() { return m_sslSocket->next_layer(); }
    virtual const NodeIPEndpoint& nodeIPEndpoint() const { return m_nodeIPEndpoint; }
    virtual void setNodeIPEndpoint(NodeIPEndpoint _nodeIPEndpoint)
    {
        m_nodeIPEndpoint = _nodeIPEndpoint;
    }
    virtual ba::ssl::stream<bi::tcp::socket>& sslref() { return *m_sslSocket; }
    bi::tcp::endpoint remote_endpoint() { return *m_remote; }

    ~FakeSocket() {}

    bool m_close;
    NodeIPEndpoint m_nodeIPEndpoint;
    std::shared_ptr<bi::tcp::endpoint> m_remote;
    std::shared_ptr<ba::ssl::stream<bi::tcp::socket> > m_sslSocket;
    ba::ssl::context m_sslContext;
};

class FakeSocketFactory : public SocketFactory
{
public:
    virtual std::shared_ptr<SocketFace> create_socket(
        ba::io_service& _ioService, NodeIPEndpoint _nodeIPEndpoint = NodeIPEndpoint())
    {
        std::shared_ptr<SocketFace> m_socket =
            std::make_shared<FakeSocket>(_ioService, _nodeIPEndpoint);
        return m_socket;
    }
};

class AsioTest : public AsioInterface
{
public:
    virtual void async_accept(bi::tcp::acceptor& tcp_acceptor, std::shared_ptr<SocketFace>& socket,
        boost::asio::io_service::strand& m_strand, Handler_Type handler,
        boost::system::error_code ec = boost::system::error_code())
    {
        /// execute handlers
        // m_strand.dispatch(std::bind(handler, ec));
        handler(ec);
        if (ec)
            BOOST_CHECK(socket->isConnected() == false);
        else
            BOOST_CHECK(socket->isConnected() == true);
    }

    /// default implementation of async_handshake
    virtual void async_handshake(std::shared_ptr<SocketFace> const& socket,
        boost::asio::io_service::strand& m_strand, ba::ssl::stream_base::handshake_type const& type,
        Handler_Type handler, boost::system::error_code ec = boost::system::error_code())
    {
        handler(ec);
    }

    virtual void async_connect(std::shared_ptr<SocketFace>& socket,
        boost::asio::io_service::strand& m_strand, const bi::tcp::endpoint& peer_endpoint,
        Handler_Type handler, boost::system::error_code ec = boost::system::error_code())
    {
        handler(ec);
        if (ec)
            BOOST_CHECK(socket->isConnected() == false);
        else
            BOOST_CHECK(socket->isConnected() == true);
    }

    virtual void set_verify_callback(
        std::shared_ptr<SocketFace> const& socket, VerifyCallback callback, bool verify_succ = true)
    {
        boost::asio::ssl::verify_context context(nullptr);
        callback(verify_succ, context);
    }

    virtual void async_wait(boost::asio::deadline_timer* m_timer,
        boost::asio::io_service::strand& m_strand, Handler_Type handler,
        boost::system::error_code ec = boost::system::error_code())
    {
        m_timer->cancel();
    }
};
/// create Host
static FakeHost* createFakeHost(
    std::string const& client_version, std::string const& listenIp, uint16_t const& listenPort)
{
    KeyPair key_pair = KeyPair::create();
    NetworkConfig network_config(listenIp, listenPort);
    std::shared_ptr<AsioInterface> m_asioInterface = std::make_shared<AsioTest>();
    /// create m_socketFactory
    std::shared_ptr<SocketFactory> m_socketFactory = std::make_shared<FakeSocketFactory>();
    /// create m_sessionFactory
    std::shared_ptr<SessionFactory> m_sessionFactory = std::make_shared<FakeSessionFactory>();
    FakeHost* m_host = new FakeHost(client_version, key_pair, network_config, m_asioInterface,
        m_socketFactory, m_sessionFactory);
    return m_host;
}
}  // namespace test
}  // namespace dev
