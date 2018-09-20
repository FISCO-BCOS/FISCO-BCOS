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
 * @author toxotguo
 * @date 2018
 *
 * @ author: yujiechen
 * @ date: 2018-09-19
 * @ modifications:
 *  1. modify io_service value from 1 to 2
 * (construction of io_service is io_service(std::size_t concurrency_hint);)
 * (currenncy_hint means that "A suggestion to the implementation on how many threads it should
 * allow to run simultaneously.") (since ethereum use 2, we modify io_service from 1 to 2) 2.
 */
#include "Host.h"
#include "Common.h"
#include "ParseCert.h"
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

Host::Host(string const& _clientVersion, KeyPair const& _alias, NetworkConfig const& _n,
    shared_ptr<AsioInterface>& _asioInterface, shared_ptr<SocketFactory>& _socketFactory,
    shared_ptr<SessionFactory>& _sessionFactory)
  : Worker("p2p", 0),
    m_clientVersion(_clientVersion),
    m_netConfigs(_n),
    m_ifAddresses(Network::getInterfaceAddresses()),
    m_ioService(2),
    m_tcp4Acceptor(m_ioService),
    m_alias(_alias),
    m_lastPing(chrono::steady_clock::time_point::min()),
    m_lastReconnect(chrono::steady_clock::time_point::min()),
    m_strand(m_ioService),
    m_listenPort(_n.listenPort),
    m_asioInterface(_asioInterface),
    m_socketFactory(_socketFactory),
    m_sessionFactory(_sessionFactory)
{
    LOG(INFO) << "Id:" << id();
}

/// destructor function
Host::~Host()
{
    stop();
}

/// start the network
/// (main function should callback this function to launch the network)
void Host::start()
{
    DEV_TIMED_FUNCTION_ABOVE(500);
    /// implemented by the parent class Worker(libdevcore/Worker.*)
    startWorking();
    /// check the network and worker thread status
    /// if the worker thread has been started, while the network
    // has not been inited, then sleep to wait for the network initialization
    while (isWorking() && !haveNetwork())
        this_thread::sleep_for(chrono::milliseconds(10));
    /// network start successfully
    if (isWorking())
        return;
    /// network start failed
    LOG(WARNING) << "Network start failed!";
    /// clean resources (include both network, socket resources) when stop working
    doneWorking();
}

/**
 * @brief : called by startWorking() of start() to start the network
 *          1. set network status to run
 *          2. start capabilities by calling onStarting
 *          3. listen and bind listen port
 *          4. accept connections from the client,
 *
 */
void Host::startedWorking()
{
    /// set network status to run(m_run=true)
    asserts(!m_timer);
    {
        Guard l(x_runTimer);
        m_timer.reset(new boost::asio::deadline_timer(m_ioService));
        m_run = true;
    }
    /// listen and bind listen port
    int port = Network::tcp4Listen(m_tcp4Acceptor, m_netConfigs);
    if (port != -1)
    {
        /// get the public address endpoint, and assign it to m_tcpPublic
        determinePublic();
        /// accept connections from the client
        runAcceptor();
    }
    else
    {
        LOG(ERROR) << "p2p.start.notice id:" << id()
                   << "TCP Listen port is invalid or unavailable. P2pPort Bind Fail!";
        exit(-1);
    }
    LOG(INFO) << "p2p.started id:" << id();
    run(boost::system::error_code());
}

/**
 * @brief: accept connection requests, maily include procedures:
 *         1. async_accept: accept connection requests
 *         2. ssl handshake: obtain node id from the certificate during ssl handshake
 *         3. if ssl handshake success, call 'handshakeServer' to init client socket
 *            and get caps, version of the connecting client, and startPeerSession
 *            (mainly init the caps and session, and update peer related information)
 * @attention: this function is called repeatedly
 */
void Host::runAcceptor(boost::system::error_code boost_error)
{
    /// make sure that listen port has been inited
    assert(m_listenPort != (uint16_t)(-1));
    /// accept the connection
    if (m_run && !m_accepting)
    {
        m_accepting = true;
        LOG(INFO) << "Listening on local port " << m_listenPort << " (public: " << m_tcpPublic
                  << "), P2P Start Accept";
        std::shared_ptr<SocketFace> socket =
            m_socketFactory->create_socket(m_ioService, NodeIPEndpoint());
        // get and set the accepted endpoint to socket(client endpoint)
        /// define callback after accept connections
        m_asioInterface->async_accept(m_tcp4Acceptor, socket, m_strand,
            [=](boost::system::error_code ec) {
                /// get the endpoint information of remote client after accept the connections
                auto remoteEndpoint = socket->remote_endpoint();
                LOG(INFO) << "P2P Recv Connect: " << remoteEndpoint.address().to_string() << ":"
                          << remoteEndpoint.port();
                /// reset accepting status
                m_accepting = false;
                /// network acception failed
                if (ec || !m_run)
                {
                    socket->close();
                    return;
                }
                /// if the connected peer over the limitation, drop socket
                if (peerCount() > peerSlots(Ingress))
                {
                    LOG(INFO) << "Dropping incoming connect due to maximum peer count (" << Ingress
                              << " * ideal peer count): " << socket->remoteEndpoint();
                    socket->close();
                    if (ec.value() < 1)
                    {
                        runAcceptor();
                    }
                    return;
                }
                m_tcpClient = socket->remoteEndpoint();
                socket->setNodeIPEndpoint(
                    NodeIPEndpoint(m_tcpClient.address(), m_tcpClient.port(), m_tcpClient.port()));
                LOG(DEBUG) << "client port:" << m_tcpClient.port()
                           << "|ip:" << m_tcpClient.address().to_string();
                LOG(DEBUG) << "server port:" << m_listenPort
                           << "|ip:" << m_tcpPublic.address().to_string();
                /// register ssl callback to get the NodeID of peers
                std::shared_ptr<std::string> endpointPublicKey = std::make_shared<std::string>();
                m_asioInterface->set_verify_callback(socket, newVerifyCallback(endpointPublicKey));
                m_asioInterface->async_handshake(socket, m_strand, ba::ssl::stream_base::server,
                    boost::bind(&Host::handshakeServer, this, ba::placeholders::error,
                        endpointPublicKey, socket));
            },
            boost_error);
    }
}

/// get the number of connected peers
size_t Host::peerCount() const
{
    unsigned retCount = 0;
    RecursiveGuard l(x_sessions);
    for (auto& i : m_sessions)
    {
        auto session = i.second;
        if (session->isConnected())
        {
            retCount++;
        }
    }
    return retCount;
}  // namespace p2p

/// @return true: the given certificate has been expired
/// @return false: the given certificate has not been expired
bool Host::isExpired(ba::ssl::verify_context& ctx)
{
    ParseCert certParser(ctx);
    return certParser.isExpire();
}

/**
 * @brief : functions called after openssl handshake,
 *          maily to get node id and verify whether the certificate has been expired
 * @param nodeIDOut : also return value, pointer points to the node id string
 * @return std::function<bool(bool, boost::asio::ssl::verify_context&)>:
 *  return true: verify success
 *  return false: verify failed
 */
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
            /// get the object points to certificate
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
            /// ignore ca
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
            /// get public key of the certificate
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
 * @brief: server calls handshakeServer to after handshake
 *         mainly calls RLPxHandshake to obtain informations(client version, caps, etc),
 *         start peer session and start accepting procedure repeatedly
 * @param error: error information triggered in the procedure of ssl handshake
 * @param endpointPublicKey: public key obtained from certificate during handshake
 * @param socket: socket related to the endpoint of the connected client
 */
void Host::handshakeServer(const boost::system::error_code& error,
    std::shared_ptr<std::string>& endpointPublicKey, std::shared_ptr<SocketFace> socket)
{
    std::string node_id_str(*endpointPublicKey);
    NodeID node_id = NodeID(node_id_str);
    /// handshake failed
    if (error)
    {
        LOG(ERROR) << "Host::async_handshake err:" << error.message();
        socket->close();
        return;
    }
    /// forbid connect to node-self
    if (node_id == id())
    {
        LOG(TRACE) << "Disconnect self" << node_id;
        socket->close();
        return;
    }
    bool success = false;
    try
    {
        /// start peer session
        startPeerSession(node_id, socket);
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
        socket->close();
    /// repeat runAcceptor procedure after start peer session
    runAcceptor();
}

/**
 * @brief: start peer sessions after handshake succeed(called by RLPxHandshake),
 *         mainly include four functions:
 *         1. disconnect connecting host with invalid capability
 *         2. modify m_peers && disconnect already-connected session
 *         3. modify m_sessions and m_staticNodes
 *         4. start new session (session->start())
 * @param _pub: node id of the connecting client
 * @param _rlp: informations obtained from the client-peer during handshake
 *              now include protocolVersion, clientVersion, caps and listenPort
 * @param _s : connected socket(used to init session object)
 */
void Host::startPeerSession(Public const& _pub, std::shared_ptr<SocketFace> const& _s)
{
    LOG(INFO) << "Host::startPeerSession! " << _pub;
    Public node_id = _pub;
    if (node_id == NodeID())
    {
        LOG(ERROR) << "No nodeid! disconnect";
        _s->close();
        return;
    }
    if (node_id == id())
    {
        LOG(DEBUG) << "Ignore Connect to self! disconnect";
        _s->close();
        return;
    }
    shared_ptr<Peer> p;
    NodeIPEndpoint nodeIPEndpoint;
    nodeIPEndpoint.address = _s->remoteEndpoint().address();
    nodeIPEndpoint.tcpPort = 0;
    nodeIPEndpoint.udpPort = 0;
    nodeIPEndpoint.host = _s->nodeIPEndpoint().host;
    DEV_RECURSIVE_GUARDED(x_sessions)
    {
        /// existed peer: obtain peer object from m_peers
        if (m_peers.count(node_id))
        {
            p = m_peers[node_id];
        }
        /// non-existed peer: new peer object and update m_peers
        else
        {
            p = make_shared<Peer>(Node(node_id, nodeIPEndpoint));
            m_peers[node_id] = p;
        }
        NodeIPEndpoint remote_endpoint(_s->remoteEndpoint().address(), _s->remoteEndpoint().port(),
            _s->remoteEndpoint().port());
        auto it = m_staticNodes.find(NodeIPEndpoint(remote_endpoint));
        if (it != m_staticNodes.end())
        {
            remote_endpoint.tcpPort = it->first.tcpPort;
            remote_endpoint.udpPort = it->first.udpPort;
            p->setEndpoint(remote_endpoint);
        }
    }

    shared_ptr<SessionFace> ps = m_sessionFactory->create_session(this, _s, p,
        PeerSessionInfo(
            {node_id, p->endpoint().address.to_string(), chrono::steady_clock::duration(), 0}));
    {
        RecursiveGuard l(x_sessions);
        if (m_sessions.count(node_id))
        {
            auto s = m_sessions[node_id];
            /// disconnect already-connected session
            if (s->isConnected())
            {
                LOG(WARNING) << "Session already exists for peer with id: " << node_id;
                ps->disconnect(DisconnectReason::DuplicatePeer);
                return;
            }
            NodeIPEndpoint endpoint(_s->remoteEndpoint().address(), _s->remoteEndpoint().port(),
                _s->remoteEndpoint().port());
            auto it = m_staticNodes.find(endpoint);
            /// modify m_staticNodes(including accept cases, namely the client endpoint)
            if (it != m_staticNodes.end())
            {
                it->second = node_id;
            }
        }
        if (!peerSlotsAvailable())
        {
            LOG(INFO) << "too many  peer ! ";
            ps->disconnect(TooManyPeers);
            return;
        }
        /// set P2PMsgHandler to session before start session
        ps->setP2PMsgHandler(m_p2pMsgHandler);
        /// start session and modify m_sessions
        ps->start();
        m_sessions[node_id] = ps;
    }
    LOG(INFO) << "start a new peer: " << node_id;
}

/**
 * @brief: remove expired timer
 *         modify alived peers to m_peers
 *         reconnect all nodes recorded in m_staticNodes periodically
 */
void Host::run(boost::system::error_code const&)
{
    /// if the p2p network has been stoped, then stop related service
    if (!m_run)
    {
        m_ioService.stop();
        m_timer.reset();
        return;
    }
    /// modify alived peers to m_peers
    keepAlivePeers();
    /// reconnect all nodes recorded in m_staticNodes periodically
    reconnectAllNodes();
    auto runcb = [this](boost::system::error_code const& error) { run(error); };
    m_timer->expires_from_now(boost::posix_time::milliseconds(c_timerInterval));
    m_asioInterface->async_wait(m_timer.get(), m_strand, runcb);
}

/**
 * @brief: drop the unconnected/invalid sessions periodically(c_keepAliveInterval)
 *         and update the peers correspondingly
 */
void Host::keepAlivePeers()
{
    auto now = chrono::steady_clock::now();
    if ((now - c_keepAliveInterval < m_lastPing) && (!m_reconnectnow))
        return;
    RecursiveGuard l(x_sessions);
    /// update m_sessions by excluding unconnected/invalid sessions
    for (auto it = m_sessions.begin(); it != m_sessions.end();)
    {
        auto p = it->second;
        /// ping connected sessions
        if (p->isConnected())
        {
            ++it;
        }
        /// erase unconnected sessions
        else
        {
            if (m_peers.count(p->info().id))
                m_peers.erase(p->info().id);
            LOG(WARNING) << "Host::keepAlivePeers m_peers erase " << p->id();
            it = m_sessions.erase(it);
        }
    }
    /// update peers according the current-alive sessions
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
    /// update the timer
    m_lastPing = chrono::steady_clock::now();
}

/**
 * @brief : reconnect to all unconnected peers recorded in m_staticNodes
 * @attention: how to avoid repeated connection:
 *   1. modify [endpoint(both client-endpoint and server-endpoint), nodeid] map when call
 * startPeerSession
 *   2. go through m_staticNodes to avoid repeated connection according to node-id
 */
void Host::reconnectAllNodes()
{
    if (chrono::steady_clock::now() - c_reconnectNodesInterval < m_lastReconnect)
        return;
    RecursiveGuard l(x_sessions);
    LOG(TRACE) << "Try reconnect all node: " << m_staticNodes.size();
    /// try to connect to nodes recorded in configurations
    for (auto it : m_staticNodes)
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
            LOG(DEBUG) << "Ignore connect self" << it.first;
            continue;
        }
        if (it.second == id())
        {
            LOG(DEBUG) << "Ignore connect self: " << it.first;
            continue;
        }
        /// use node id as the key to avoid repeated connection
        if (it.second != NodeID() && isConnected(it.second))
        {
            LOG(DEBUG) << "Ignore connected node: " << it.second;
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
 * @brief : connect to the server
 * @param _nodeIPEndpoint : the endpoint of the connected server
 */
void Host::connect(NodeIPEndpoint const& _nodeIPEndpoint, boost::system::error_code boost_error)
{
    if (!m_run)
        return;
    {
        /// update the pending connections
        Guard l(x_pendingNodeConns);
        if (m_pendingPeerConns.count(_nodeIPEndpoint.name()))
            return;
        m_pendingPeerConns.insert(_nodeIPEndpoint.name());
    }
    LOG(INFO) << "Attempting connection to node "
              << "@" << _nodeIPEndpoint.name() << "," << _nodeIPEndpoint.host << " from " << id();
    std::shared_ptr<SocketFace> socket =
        m_socketFactory->create_socket(m_ioService, _nodeIPEndpoint);
    // socket.reset(socket);
    m_tcpClient = socket->remoteEndpoint();
    socket->sslref().set_verify_mode(ba::ssl::verify_peer);
    socket->sslref().set_verify_depth(3);
    /// connect to the server
    m_asioInterface->async_connect(socket, m_strand, _nodeIPEndpoint,
        [=](boost::system::error_code const& ec) {
            if (ec)
            {
                LOG(ERROR) << "Connection refused to node"
                           << "@" << _nodeIPEndpoint.name() << "(" << ec.message() << ")";
                Guard l(x_pendingNodeConns);
                m_pendingPeerConns.erase(_nodeIPEndpoint.name());
                socket->close();
                return;
            }
            else
            {
                /// get the public key of the server during handshake
                std::shared_ptr<std::string> endpointPublicKey = std::make_shared<std::string>();
                m_asioInterface->set_verify_callback(socket, newVerifyCallback(endpointPublicKey));
                /// call handshakeClient after handshake succeed
                m_asioInterface->async_handshake(socket, m_strand, ba::ssl::stream_base::client,
                    boost::bind(&Host::handshakeClient, this, ba::placeholders::error, socket,
                        endpointPublicKey, _nodeIPEndpoint));
            }
        },
        boost_error);
}

/**
 * @brief : start RLPxHandshake procedure after ssl handshake succeed
 * @param error: error returned by ssl handshake
 * @param socket : ssl socket
 * @param endpointPublicKey: public key of the server obtained from the certificate
 * @param _nodeIPEndpoint : endpoint of the server to connect
 */
void Host::handshakeClient(const boost::system::error_code& error,
    std::shared_ptr<SocketFace> socket, std::shared_ptr<std::string>& endpointPublicKey,
    NodeIPEndpoint& _nodeIPEndpoint)
{
    /// get the node id of the server
    dev::p2p::NodeID node_id(*endpointPublicKey);
    if (error)
    {
        Guard l(x_pendingNodeConns);
        m_pendingPeerConns.erase(_nodeIPEndpoint.name());
        LOG(ERROR) << "P2P Handshake failed:" << error.message();
        socket->close();
        return;
    }
    /// avoid to connect to node-self
    if (node_id == id())
    {
        // connect to myself
        LOG(TRACE) << "Disconnect self" << _nodeIPEndpoint;
        Guard l(x_pendingNodeConns);
        m_pendingPeerConns.erase(_nodeIPEndpoint.name());

        RecursiveGuard m(x_sessions);
        /// obtain the node id of specified connect when handshakeClient
        auto it = m_staticNodes.find(_nodeIPEndpoint);
        if (it != m_staticNodes.end())
        {
            it->second = node_id;  // modify node ID
        }
        socket->close();
        return;
    }
    /// start handshake
    startPeerSession(node_id, socket);
    Guard l(x_pendingNodeConns);
    m_pendingPeerConns.erase(_nodeIPEndpoint.name());
}

/// stop the network and worker thread
void Host::stop()
{
    // called to force io_service to kill any remaining tasks it might have -
    // such tasks may involve socket reads from Capabilities that maintain references
    // to resources we're about to free.
    {
        // Although m_run is set by stop() or start(), it effects m_runTimer so x_runTimer is
        // used instead of a mutex for m_run.
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

/// clean resources (include both network, socket resources) when stop working
void Host::doneWorking()
{
    try
    {
        // reset ioservice (cancels all timers and allows manually polling network, below)
        m_ioService.reset();
        // shutdown acceptor
        if (m_tcp4Acceptor.is_open())
        {
            m_tcp4Acceptor.cancel();
            m_tcp4Acceptor.close();
        }
        while (m_accepting)
            m_ioService.poll();
        // disconnect peers
        for (unsigned n = 0;; n = 0)
        {
            DEV_RECURSIVE_GUARDED(x_sessions)
            for (auto i : m_sessions)
            {
                auto p = i.second;
                if (p->isConnected())
                {
                    p->disconnect(ClientQuit);
                    n++;
                }
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
        /// disconnect all sessions by callback keepAlivePeers
        reconnectNow();
        keepAlivePeers();
    }
    catch (std::exception& err)
    {
        LOG(ERROR) << "callback doneWorking failed, error message:" << err.what();
    }
}

bytes Host::saveNetwork() const
{
    RLPStream ret(3);
    ret << m_alias.secret().ref();
    int count = 0;
    ret.appendList(count);
    /*if (!!count)
        ret.appendRaw(network.out(), count);*/
    return ret.out();
}

/// start the network by callback io_serivce.run
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
    }
    if (m_ioService.stopped())
        m_ioService.reset();
}
}  // namespace p2p
}  // namespace dev
