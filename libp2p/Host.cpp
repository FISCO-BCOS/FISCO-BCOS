/*
    This file is part of cpp-ethereum.

    cpp-ethereum is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    cpp-ethereum is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file Host.cpp
 * @author Alex Leverington <nessence@gmail.com>
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 *  @author toxotguo
 * @date 2018
 */
#include "Host.h"
#include "Capability.h"
#include "Common.h"
#include "HostCapability.h"
#include "ParseCert.h"
#include "RLPxHandshake.h"
#include "Session.h"
#include <libdevcore/Assertions.h>
#include <libdevcore/Common.h>
#include <libdevcore/CommonIO.h>
#include <libdevcore/Exceptions.h>
#include <libdevcore/FileSystem.h>
#include <libdevcore/easylog.h>
#include <libdevcrypto/Rsa.h>
#include <libethcore/CommonJS.h>
#include <boost/algorithm/string.hpp>
#include <chrono>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
using namespace std;
using namespace dev;
using namespace dev::p2p;
using namespace dev::eth;
using namespace dev::crypto;
namespace dev
{
namespace p2p
{
/// Interval at which Host::run will call keepAlivePeers to ping peers.
std::chrono::seconds const c_keepAliveInterval = std::chrono::seconds(30);
std::chrono::seconds const c_reconnectNodesInterval = std::chrono::seconds(60);

/// Disconnect timeout after failure to respond to keepAlivePeers ping.
std::chrono::milliseconds const c_keepAliveTimeOut = std::chrono::milliseconds(1000);

Host::Host(string const& _clientVersion, KeyPair const& _alias, NetworkConfig const& _n)
  : Worker("p2p", 0),
    m_clientVersion(_clientVersion),
    m_netConfigs(_n),
    m_ifAddresses(Network::getInterfaceAddresses()),
    m_ioService(2),
    m_tcp4Acceptor(m_ioService),
    m_alias(_alias),
    m_lastPing(chrono::steady_clock::time_point::min()),
    m_lastReconnect(chrono::steady_clock::time_point::min()),
    m_strand(m_ioService)
{
    LOG(INFO) << "Id:" << id();
}
/// destructor function
Host::~Host()
{
    stop();
}

/// stop the network
void Host::stop()
{
    // called to force io_service to kill any remaining tasks it might have -
    // such tasks may involve socket reads from Capabilities that maintain references
    // to resources we're about to free.
    {
        // Although m_run is set by stop() or start(), it effects m_runTimer so x_runTimer is used
        // instead of a mutex for m_run.
        Guard l(x_runTimer);
        // ignore if already stopped/stopping
        if (!m_run)
            return;

        // signal run() to prepare for shutdown and reset m_timer
        m_run = false;
    }
    // wait for m_timer to reset (indicating network scheduler has stopped)
    while (!!m_timer)
        this_thread::sleep_for(chrono::milliseconds(50));
    // stop worker thread
    if (isWorking())
        stopWorking();
}

void Host::start()
{
    DEV_TIMED_FUNCTION_ABOVE(500);
    startWorking();
    while (isWorking() && !haveNetwork())
        this_thread::sleep_for(chrono::milliseconds(10));
    // network start failed!
    if (isWorking())
        return;

    LOG(WARNING) << "Network start failed!";
    doneWorking();
}

void Host::doneWorking()
{
    // reset ioservice (cancels all timers and allows manually polling network, below)
    m_ioService.reset();

    DEV_GUARDED(x_timers)
    m_timers.clear();

    // shutdown acceptor
    m_tcp4Acceptor.cancel();
    if (m_tcp4Acceptor.is_open())
        m_tcp4Acceptor.close();

    while (m_accepting)
        m_ioService.poll();

    // stop capabilities (eth: stops syncing or block/tx broadcast)
    for (auto const& h : m_capabilities)
        h.second->onStopping();
    // disconnect pending handshake, before peers, as a handshake may create a peer
    for (unsigned n = 0;; n = 0)
    {
        DEV_GUARDED(x_connecting)
        for (auto const& i : m_connecting)
            if (auto h = i.lock())
            {
                h->cancel();
                n++;
            }
        if (!n)
            break;
        m_ioService.poll();
    }
    // disconnect peers
    for (unsigned n = 0;; n = 0)
    {
        DEV_RECURSIVE_GUARDED(x_sessions)
        for (auto i : m_sessions)
            if (auto p = i.second.lock())
                if (p->isConnected())
                {
                    p->disconnect(ClientQuit);
                    n++;
                }
        if (!n)
            break;

        // poll so that peers send out disconnect packets
        m_ioService.poll();
    }

    // stop network (again; helpful to call before subsequent reset())
    m_ioService.stop();

    // reset network (allows reusing ioservice in future)
    m_ioService.reset();

    // finally, clear out peers (in case they're lingering)
    RecursiveGuard l(x_sessions);
    m_sessions.clear();
}

///----Peer related informations
PeerSessionInfos Host::peerSessionInfo() const
{
    if (!m_run)
        return PeerSessionInfos();

    std::vector<PeerSessionInfo> ret;
    RecursiveGuard l(x_sessions);
    for (auto& i : m_sessions)
        if (auto j = i.second.lock())
            if (j->isConnected())
                ret.push_back(j->info());
    return ret;
}

size_t Host::peerCount() const
{
    unsigned retCount = 0;
    RecursiveGuard l(x_sessions);
    for (auto& i : m_sessions)
        if (std::shared_ptr<SessionFace> j = i.second.lock())
            if (j->isConnected())
                retCount++;
    return retCount;
}

bytes Host::saveNetwork() const
{
    RLPStream ret(3);
    ret << dev::p2p::c_protocolVersion << m_alias.secret().ref();
    int count = 0;
    ret.appendList(count);
    /*if (!!count)
        ret.appendRaw(network.out(), count);*/
    return ret.out();
}

void Host::startPeerSession(
    Public const& _pub, RLP const& _rlp, std::shared_ptr<RLPXSocket> const& _s)
{
    auto protocolVersion = _rlp[0].toInt<unsigned>();
    auto clientVersion = _rlp[1].toString();
    auto caps = _rlp[2].toVector<CapDesc>();
    auto listenPort = _rlp[3].toInt<unsigned short>();
    LOG(INFO) << "Host::startPeerSession! " << _pub;
    Public _id = _pub;

    // connection must be disconnect before the creation of session object and peer object -
    // morebtcg
    if (_id == id())
    {
        LOG(TRACE) << "Disconnect self: " << _id << "@" << _s->nodeIPEndpoint().address.to_string()
                   << ":" << _s->nodeIPEndpoint().tcpPort;

        _s->close();
        throw dev::ConnectionToSelfException();
        // ps->disconnect(LocalIdentity);
        return;
    }

    // connection must be disconnect before the creation of session object and peer object -
    NodeIPEndpoint _nodeIPEndpoint;
    _nodeIPEndpoint.address = _s->remoteEndpoint().address();
    _nodeIPEndpoint.tcpPort = listenPort;
    _nodeIPEndpoint.udpPort = listenPort;
    _nodeIPEndpoint.host = _s->nodeIPEndpoint().host;

    shared_ptr<Peer> p;
    DEV_RECURSIVE_GUARDED(x_sessions)
    {
        if (m_peers.count(_nodeIPEndpoint.name()))
            p = m_peers[_nodeIPEndpoint.name()];
        else
        {
            p = make_shared<Peer>(Node(_id, _nodeIPEndpoint));
            m_peers[_nodeIPEndpoint.name()] = p;
        }
    }
    p->setEndpoint(_nodeIPEndpoint);

    stringstream capslog;
    caps.erase(remove_if(caps.begin(), caps.end(),
                   [&](CapDesc const& _r) {
                       return !haveCapability(_r) ||
                              any_of(caps.begin(), caps.end(), [&](CapDesc const& _o) {
                                  return _r.first == _o.first && _o.second > _r.second &&
                                         haveCapability(_o);
                              });
                   }),
        caps.end());

    for (auto cap : caps)
        capslog << "(" << cap.first << "," << dec << cap.second << ")";

    LOG(INFO) << "Hello: " << clientVersion << "V[" << protocolVersion << "]" << _id << showbase
              << capslog.str() << dec << listenPort;

    shared_ptr<SessionFace> ps = make_shared<Session>(this, _s, p,
        PeerSessionInfo({_id, clientVersion, p->endpoint().address.to_string(), listenPort,
            chrono::steady_clock::duration(), _rlp[2].toSet<CapDesc>(), 0, map<string, string>(),
            _nodeIPEndpoint}));

    if (protocolVersion < dev::p2p::c_protocolVersion - 1)
    {
        ps->disconnect(DisconnectReason::IncompatibleProtocol);
        return;
    }
    if (caps.empty())
    {
        ps->disconnect(DisconnectReason::UselessPeer);
        return;
    }
    if (!m_requiredPeers.count(_id))
    {
        LOG(DEBUG) << "Unexpected identity from peer (got" << _id << ", must be one of "
                   << m_requiredPeers << ")";
        ps->disconnect(DisconnectReason::UnexpectedIdentity);
        return;
    }

    {
        RecursiveGuard l(x_sessions);
        if (m_sessions.count(_id) && !!m_sessions[_id].lock())
        {
            if (auto s = m_sessions[_id].lock())
            {
                if (s->isConnected())
                {
                    // Already connected.
                    LOG(WARNING) << "Session already exists for peer with id: " << _id;
                    ps->disconnect(DisconnectReason::DuplicatePeer);
                    return;
                }
            }
            NodeIPEndpoint endpoint(_s->remoteEndpoint().address(), 0, _s->remoteEndpoint().port());
            auto it = _staticNodes.find(endpoint);
            if (it != _staticNodes.end())
            {
                it->second = _id;
            }
        }
        if (!peerSlotsAvailable())
        {
            LOG(INFO) << "too many  peer ! ";
            ps->disconnect(TooManyPeers);
            return;
        }

        unsigned offset = (unsigned)UserPacket;
        uint16_t cnt = 1;

        for (auto const& i : caps)
        {
            auto pcap = m_capabilities[i];
            if (!pcap)
                return ps->disconnect(IncompatibleProtocol);
            pcap->newPeerCapability(ps, offset, i, 0);
            offset += pcap->messageCount();
        }

        ps->start();
        m_sessions[_id] = ps;
    }
    LOG(INFO) << "p2p.host.peer.register: " << _id;
}


void Host::connect(NodeIPEndpoint const& _nodeIPEndpoint)
{
    if (!m_run)
        return;
    /// in case of repeated connections
    if (m_peers.count(_nodeIPEndpoint.name()))
    {
        LOG(INFO) << "Don't Repeat Connect (" << _nodeIPEndpoint.name() << ","
                  << _nodeIPEndpoint.host << ")";
        if (!_nodeIPEndpoint.host.empty())
            m_peers[_nodeIPEndpoint.name()]->endpoint().host = _nodeIPEndpoint.host;
        return;
    }
    {
        Guard l(x_pendingNodeConns);
        if (m_pendingPeerConns.count(_nodeIPEndpoint.name()))
            return;
        m_pendingPeerConns.insert(_nodeIPEndpoint.name());
    }
    LOG(INFO) << "Attempting connection to node "
              << "@" << _nodeIPEndpoint.name() << "," << _nodeIPEndpoint.host << " from " << id();
    std::shared_ptr<RLPXSocket> socket;
    socket.reset(new RLPXSocket(m_ioService, _nodeIPEndpoint));

    m_tcpClient = socket->remoteEndpoint();
    socket->sslref().set_verify_mode(ba::ssl::verify_peer);
    socket->sslref().set_verify_depth(3);
    socket->ref().async_connect(
        _nodeIPEndpoint, m_strand.wrap([=](boost::system::error_code const& ec) {
            if (ec)
            {
                LOG(ERROR) << "Connection refused to node"
                           << "@" << _nodeIPEndpoint.name() << "(" << ec.message() << ")";

                Guard l(x_pendingNodeConns);
                m_pendingPeerConns.erase(_nodeIPEndpoint.name());
            }
            else
            {
                std::shared_ptr<std::string> endpointPublicKey = std::make_shared<std::string>();
                socket->sslref().set_verify_callback(newVerifyCallback(endpointPublicKey));
                socket->sslref().async_handshake(ba::ssl::stream_base::client,
                    m_strand.wrap(boost::bind(&Host::handshakeClient, this, ba::placeholders::error,
                        socket, endpointPublicKey, _nodeIPEndpoint)));
            }
        }));
}

void Host::handshakeServer(const boost::system::error_code& error,
    std::shared_ptr<std::string>& endpointPublicKey, std::shared_ptr<RLPXSocket> socket)
{
    NodeID node_id = NodeID(*endpointPublicKey);
    if (error)
    {
        LOG(ERROR) << "Host::async_handshake err:" << error.message();
    }
    if (node_id == id())
    {
        // connect to myself
        LOG(TRACE) << "Disconnect self" << node_id;
        socket->close();
        return;
    }

    bool success = false;
    try
    {
        // incoming connection; we don't yet know nodeid
        auto handshake = make_shared<RLPXHandshake>(this, socket, node_id);
        m_connecting.push_back(handshake);
        handshake->start();
        success = true;
    }
    catch (Exception const& _e)
    {
        LOG(ERROR) << "ERROR: " << diagnostic_information(_e);
    }
    catch (std::exception const& _e)
    {
        LOG(ERROR) << "ERROR: " << _e.what();
    }
    if (!success)
        socket->ref().close();
    runAcceptor();
}

void Host::handshakeClient(const boost::system::error_code& error,
    std::shared_ptr<RLPXSocket> socket, std::shared_ptr<std::string>& endpointPublicKey,
    NodeIPEndpoint& _nodeIPEndpoint)
{
    dev::p2p::NodeID node_id(*endpointPublicKey);
    if (error)
    {
        m_pendingPeerConns.erase(_nodeIPEndpoint.name());
        LOG(DEBUG) << "Host::handshakeClient Err:" << error.message();
        return;
    }
    if (node_id == id())
    {
        // connect to myself
        LOG(TRACE) << "Disconnect self" << _nodeIPEndpoint;

        Guard l(x_pendingNodeConns);
        m_pendingPeerConns.erase(_nodeIPEndpoint.name());

        RecursiveGuard m(x_sessions);
        auto it = _staticNodes.find(_nodeIPEndpoint);
        if (it != _staticNodes.end())
        {
            it->second = node_id;  // modify node ID
        }
        socket->close();
        return;
    }
    auto handshake = make_shared<RLPXHandshake>(this, socket, node_id);
    {
        Guard l(x_connecting);
        m_connecting.push_back(handshake);
    }
    handshake->start();

    Guard l(x_pendingNodeConns);
    m_pendingPeerConns.erase(_nodeIPEndpoint.name());
}

bool Host::isExpired(ba::ssl::verify_context& ctx)
{
    ParseCert certParser;
    certParser.ParseInfo(ctx);
    return certParser.getExpire();
}

std::function<bool(bool, boost::asio::ssl::verify_context&)> Host::newVerifyCallback(
    std::shared_ptr<std::string> nodeIDOut)
{
    return [nodeIDOut](bool preverified, boost::asio::ssl::verify_context& ctx) {
        try
        {
            /// check certificate expiration
            if (Host::isExpired(ctx))
            {
                LOG(ERROR) << "The Certificate has been expired";
                return false;
            }
            /// get node id
            X509* cert = X509_STORE_CTX_get_current_cert(ctx.native_handle());
            if (!cert)
            {
                LOG(ERROR) << "Get cert failed";
                return preverified;
            }

            int crit = 0;
            BASIC_CONSTRAINTS* basic =
                (BASIC_CONSTRAINTS*)X509_get_ext_d2i(cert, NID_basic_constraints, &crit, NULL);
            if (!basic)
            {
                LOG(ERROR) << "Get ca casic failed";
                return preverified;
            }

            if (basic->ca)
            {
                // ca or agency certificate
                LOG(TRACE) << "Ignore CA certificate";
                return preverified;
            }

            EVP_PKEY* evpPublicKey = X509_get_pubkey(cert);
            if (!evpPublicKey)
            {
                LOG(ERROR) << "Get evpPublicKey failed";
                return preverified;
            }

            ec_key_st* ecPublicKey = EVP_PKEY_get1_EC_KEY(evpPublicKey);
            if (!ecPublicKey)
            {
                LOG(ERROR) << "Get ecPublicKey failed";
                return preverified;
            }

            const EC_POINT* ecPoint = EC_KEY_get0_public_key(ecPublicKey);
            if (!ecPoint)
            {
                LOG(ERROR) << "Get ecPoint failed";
                return preverified;
            }

            std::shared_ptr<char> hex =
                std::shared_ptr<char>(EC_POINT_point2hex(EC_KEY_get0_group(ecPublicKey), ecPoint,
                                          EC_KEY_get_conv_form(ecPublicKey), NULL),
                    [](char* p) { OPENSSL_free(p); });

            if (hex)
            {
                nodeIDOut->assign(hex.get());
                if (nodeIDOut->find("04") == 0)
                {
                    /// remove 04
                    nodeIDOut->erase(0, 2);
                }
                LOG(DEBUG) << "Get endpoint publickey:" << *nodeIDOut;
            }
            return preverified;
        }
        catch (std::exception& e)
        {
            LOG(ERROR) << "Cert verify failed: " << boost::diagnostic_information(e);
            return preverified;
        }
    };
}

/**
 * @brief
 *
 */
void Host::startedWorking()
{
    asserts(!m_timer);
    {
        Guard l(x_runTimer);
        m_timer.reset(new boost::asio::deadline_timer(m_ioService));
        m_run = true;
    }
    /// start capabilities
    for (auto const& h : m_capabilities)
        h.second->onStarting();
    /// start listen and get the listen port
    int port = Network::tcp4Listen(m_tcp4Acceptor, m_netConfigs);
    if (port > 0)
    {
        LOG(DEBUG) << "set m_listenPort to port:" << port;
        m_listenPort = port;
        determinePublic();
        runAcceptor();
    }
    else
    {
        LOG(INFO) << "p2p.start.notice id:" << id() << "TCP Listen port is invalid or unavailable.";
        LOG(ERROR) << "P2pPort Bind Fail！"
                   << "\n";
        exit(-1);
    }
    LOG(INFO) << "p2p.started id:" << id();
    run(boost::system::error_code());
}

void Host::doWork()
{
    try
    {
        if (m_run)
            m_ioService.run();
    }
    catch (std::exception const& _e)
    {
        LOG(WARNING) << "Exception in Network Thread:" << _e.what();
        LOG(WARNING) << "Network Restart is Recommended.";
    }

    if (m_ioService.stopped())
    {
        m_ioService.reset();
    }
}

void Host::keepAlivePeers()
{
    auto now = chrono::steady_clock::now();

    if ((now - c_keepAliveInterval < m_lastPing) && (!m_reconnectnow))
        return;

    RecursiveGuard l(x_sessions);
    for (auto it = m_sessions.begin(); it != m_sessions.end();)
    {
        if (auto p = it->second.lock())
        {
            if (p->isConnected())
            {
                if (now - c_keepAliveTimeOut > m_lastPing && p->lastReceived() < m_lastPing)
                {
                    LOG(WARNING) << "Host::keepAlivePeers  timeout disconnect " << p->id();
                    p->disconnect(PingTimeout);
                }
                else
                    p->ping();

                ++it;
            }
            else
            {
                if (m_peers.count(p->info().nodeIPEndpoint.name()))
                    m_peers.erase(p->info().nodeIPEndpoint.name());
                LOG(WARNING) << "Host::keepAlivePeers m_peers erase " << p->id() << ","
                             << p->info().nodeIPEndpoint.name();
                it = m_sessions.erase(it);
            }
        }
        else
        {
            LOG(WARNING) << "Host::keepAlivePeers erase Session " << it->first;
            it = m_sessions.erase(it);
        }
    }

    for (auto it = m_peers.begin(); it != m_peers.end();)
    {
        if (!havePeerSession(it->second->id()))
        {
            LOG(WARNING) << "Host::keepAlivePeers m_peers erase " << it->second->id() << ","
                         << it->second->endpoint().name();
            it = m_peers.erase(it);
        }
        else
            ++it;
    }

    m_lastPing = chrono::steady_clock::now();
}

bool Host::isConnected(const NodeID& nodeID)
{
    auto session = peerSession(nodeID);
    if (session && session->isConnected())
    {
        return true;
    }

    return false;
}

void Host::reconnectAllNodes()
{
    if (chrono::steady_clock::now() - c_reconnectNodesInterval < m_lastReconnect)
        return;

    RecursiveGuard l(x_sessions);

    LOG(TRACE) << "Try reconnect all node: " << _staticNodes.size();

    /// try to connect to nodes recorded in configurations
    for (auto it : _staticNodes)
    {
        LOG(DEBUG) << "try connect: " << it.first.address.to_string() << ":" << it.first.tcpPort;
        /// exclude myself
        if (((!m_netConfigs.listenIPAddress.empty() &&
                 it.first.address ==
                     boost::asio::ip::address::from_string(m_netConfigs.listenIPAddress)) ||
                (!m_netConfigs.publicIPAddress.empty() &&
                    it.first.address ==
                        boost::asio::ip::address::from_string(m_netConfigs.publicIPAddress)) ||
                m_ifAddresses.find(it.first.address) != m_ifAddresses.end() ||
                it.first.address == m_tcpPublic.address() ||
                it.first.address == m_tcpClient.address()) &&
            it.first.tcpPort == m_netConfigs.listenPort)
        {
            LOG(INFO) << "Ignore connect self" << it.first;
            continue;
        }

        if (it.second == id())
        {
            LOG(INFO) << "Ignore connect self: " << it.first;
            continue;
        }

        if (it.second != NodeID() && isConnected(it.second))
        {
            LOG(INFO) << "Ignore connected node: " << it.second;
            continue;
        }
        if (it.first.address.to_string().empty())
        {
            LOG(INFO) << "Target Node Ip Is Empty  (" << it.first.name() << ")";
            continue;
        }
        connect(it.first);
    }

    m_lastReconnect = chrono::steady_clock::now();
}

/**
 * @brief:
 *
 */
void Host::runAcceptor()
{
    assert(m_listenPort > 0);
    if (m_run && !m_accepting)
    {
        LOG(INFO) << "Listening on local port " << m_listenPort << " (public: " << m_tcpPublic
                  << ")";
        m_accepting = true;

        LOG(INFO) << "P2P Start Accept";
        std::shared_ptr<RLPXSocket> socket;
        socket.reset(new RLPXSocket(m_ioService, NodeIPEndpoint()));
        m_tcp4Acceptor.async_accept(socket->ref(), m_strand.wrap([=](boost::system::error_code ec) {
            auto remoteEndpoint = socket->ref().remote_endpoint();
            LOG(INFO) << "P2P Recv Connect: " << remoteEndpoint.address().to_string() << ":"
                      << remoteEndpoint.port();

            m_accepting = false;
            if (ec || !m_run)
            {
                socket->close();
                return;
            }
            if (peerCount() > peerSlots(Ingress))
            {
                LOG(INFO) << "Dropping incoming connect due to maximum peer count (" << Ingress
                          << " * ideal peer count): " << socket->remoteEndpoint();
                socket->close();
                if (ec.value() < 1)
                    runAcceptor();
                return;
            }

            m_tcpClient = socket->remoteEndpoint();
            socket->setNodeIPEndpoint(
                NodeIPEndpoint(m_tcpClient.address(), (uint16_t)0, m_tcpClient.port()));
            LOG(DEBUG) << "client port:" << m_tcpClient.port()
                       << "|ip:" << m_tcpClient.address().to_string();
            LOG(DEBUG) << "server port:" << m_listenPort
                       << "|ip:" << m_tcpPublic.address().to_string();
            /// register ssl callback to get the NodeID of peers
            std::shared_ptr<std::string> endpointPublicKey = std::make_shared<std::string>();
            socket->sslref().set_verify_callback(newVerifyCallback(endpointPublicKey));
            socket->sslref().async_handshake(ba::ssl::stream_base::server,
                m_strand.wrap(boost::bind(&Host::handshakeServer, this, ba::placeholders::error,
                    endpointPublicKey, socket)));
        }));
    }
}


void Host::run(boost::system::error_code const&)
{
    if (!m_run)
    {
        m_ioService.stop();
        m_timer.reset();
        return;
    }
    DEV_GUARDED(x_connecting)
    m_connecting.remove_if([](std::weak_ptr<RLPXHandshake> h) { return h.expired(); });
    DEV_GUARDED(x_timers)
    m_timers.remove_if([](std::shared_ptr<boost::asio::deadline_timer> t) {
        return t->expires_from_now().total_milliseconds() < 0;
    });

    keepAlivePeers();
    reconnectAllNodes();

    auto runcb = [this](boost::system::error_code const& error) { run(error); };
    m_timer->expires_from_now(boost::posix_time::milliseconds(c_timerInterval));
    m_timer->async_wait(m_strand.wrap(runcb));
}
}  // namespace p2p
}  // namespace dev
