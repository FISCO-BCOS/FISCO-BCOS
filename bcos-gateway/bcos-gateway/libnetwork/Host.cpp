
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
 * (currenncy_hint means that "A suggestion to the implementation on how many
 * threads it should allow to run simultaneously.") (since ethereum use 2, we
 * modify io_service from 1 to 2) 2.
 */
#include <bcos-gateway/libnetwork/ASIOInterface.h>
#include <bcos-gateway/libnetwork/Common.h>
#include <bcos-gateway/libnetwork/Host.h>
#include <bcos-gateway/libnetwork/Session.h>
#include <bcos-gateway/libnetwork/SocketFace.h>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <functional>
#include <memory>
#include <set>
#include <utility>


using namespace std;
using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::crypto;

/**
 * @brief: accept connection requests, maily include procedures:
 *         1. async_accept: accept connection requests
 *         2. ssl handshake: obtain node id from the certificate during ssl
 * handshake
 *         3. if ssl handshake success, call 'handshakeServer' to init client
 * socket and get caps, version of the connecting client, and startPeerSession
 *            (mainly init the caps and session, and update peer related
 * information)
 * @attention: this function is called repeatedly
 */
void Host::startAccept(boost::system::error_code boost_error)
{
    /// accept the connection
    if (m_run)
    {
        HOST_LOG(INFO) << LOG_DESC("P2P StartAccept") << LOG_KV("Host", m_listenHost) << ":"
                       << m_listenPort;
        auto socket = m_asioInterface->newSocket(true, NodeIPEndpoint());
        // get and set the accepted endpoint to socket(client endpoint)
        /// define callback after accept connections
        m_asioInterface->asyncAccept(
            socket,
            [this, socket](boost::system::error_code ec) {
                /// get the endpoint information of remote client after accept the
                /// connections
                auto endpoint = socket->remoteEndpoint();
                HOST_LOG(TRACE) << LOG_DESC("P2P Recv Connect, From=") << endpoint;
                /// network accept failed
                if (ec || !m_run)
                {
                    HOST_LOG(ERROR) << "Error: " << ec;
                    socket->close();
                    startAccept();

                    return;
                }

                /// if the connected peer over the limitation, drop socket
                socket->setNodeIPEndpoint(endpoint);
                HOST_LOG(INFO) << LOG_DESC("P2P Recv Connect, From=") << endpoint;
                /// register ssl callback to get the NodeID of peers
                std::shared_ptr<std::string> endpointPublicKey = std::make_shared<std::string>();
                m_asioInterface->setVerifyCallback(socket, newVerifyCallback(endpointPublicKey));
                m_asioInterface->asyncHandshake(socket, ba::ssl::stream_base::server,
                    boost::bind(&Host::handshakeServer, shared_from_this(), ba::placeholders::error,
                        endpointPublicKey, socket));

                startAccept();
            },
            boost_error);
    }
}

/**
 * @brief : functions called after openssl handshake,
 *          maily to get node id and verify whether the certificate has been
 * expired
 * @param nodeIDOut : also return value, pointer points to the node id string
 * @return std::function<bool(bool, boost::asio::ssl::verify_context&)>:
 *  return true: verify success
 *  return false: verify failed
 * modifications 2019.03.20: append subject name and issuer name after nodeIDOut
 * for demand of fisco-bcos-browser
 */
std::function<bool(bool, boost::asio::ssl::verify_context&)> Host::newVerifyCallback(
    std::shared_ptr<std::string> nodeIDOut)
{
    auto host = std::weak_ptr<Host>(shared_from_this());
    return [host, nodeIDOut](bool preverified, boost::asio::ssl::verify_context& ctx) {
        auto hostPtr = host.lock();
        if (!hostPtr)
        {
            return false;
        }

        try
        {
            /// return early when the certificate verify failed
            if (!preverified && hostPtr->m_enableSSLVerify)
            {
                HOST_LOG(DEBUG) << LOG_DESC("ssl handshake certificate verify failed")
                                << LOG_KV("preverified", preverified);
                return false;
            }
            /// get the object points to certificate
            X509* cert = X509_STORE_CTX_get_current_cert(ctx.native_handle());
            if (!cert)
            {
                HOST_LOG(ERROR) << LOG_DESC("Get cert failed");
                return preverified;
            }
            // For compatibility, p2p communication between nodes still uses the old public key
            // analysis method
            if (!hostPtr->sslContextPubHandler()(cert, *nodeIDOut))
            {
                return preverified;
            }
            ////  always return true when disable ssl, return preverified when enable ssl ///
            int crit = 0;
            auto* basic =
                (BASIC_CONSTRAINTS*)X509_get_ext_d2i(cert, NID_basic_constraints, &crit, NULL);
            if (!basic)
            {
                HOST_LOG(INFO) << LOG_DESC("Get ca basic failed");
                return preverified || (!hostPtr->m_enableSSLVerify);
            }

            /// ignore ca
            if (basic->ca)
            {
                // ca or agency certificate
                HOST_LOG(TRACE) << LOG_DESC("Ignore CA certificate");
                BASIC_CONSTRAINTS_free(basic);
                return preverified || (!hostPtr->m_enableSSLVerify);
            }

            BASIC_CONSTRAINTS_free(basic);

            // The new public key analysis method is used for black and white lists
            std::string nodeIDOutWithoutExtInfo;
            if (!hostPtr->sslContextPubHandlerWithoutExtInfo()(cert, nodeIDOutWithoutExtInfo))
            {
                return preverified;
            }
            nodeIDOutWithoutExtInfo = boost::to_upper_copy(nodeIDOutWithoutExtInfo);

            // If the node ID exists in the black and white lists at the same time, the black list
            // takes precedence
            if (nullptr != hostPtr->peerBlacklist() &&
                hostPtr->peerBlacklist()->has(nodeIDOutWithoutExtInfo))
            {
                HOST_LOG(INFO) << LOG_DESC("NodeID in certificate blacklist")
                               << LOG_KV("nodeID", P2PNodeID(nodeIDOutWithoutExtInfo).abridged());
                return false;
            }

            if (nullptr != hostPtr->peerWhitelist() &&
                !hostPtr->peerWhitelist()->has(nodeIDOutWithoutExtInfo))
            {
                HOST_LOG(INFO) << LOG_DESC("NodeID is not in certificate whitelist")
                               << LOG_KV("nodeID", P2PNodeID(nodeIDOutWithoutExtInfo).abridged());
                return false;
            }

            /// append cert-name and issuer name after node ID
            /// get subject name
            const char* certName = X509_NAME_oneline(X509_get_subject_name(cert), NULL, 0);
            /// get issuer name
            const char* issuerName = X509_NAME_oneline(X509_get_issuer_name(cert), NULL, 0);
            /// format: {nodeID}#{issuer-name}#{cert-name}
            nodeIDOut->append("#");
            nodeIDOut->append(nodeIDOutWithoutExtInfo);
            nodeIDOut->append("#");
            nodeIDOut->append(issuerName);
            nodeIDOut->append("#");
            nodeIDOut->append(certName);
            OPENSSL_free((void*)certName);
            OPENSSL_free((void*)issuerName);

            return preverified || (!hostPtr->m_enableSSLVerify);
        }
        catch (std::exception& e)
        {
            HOST_LOG(ERROR) << LOG_DESC("Cert verify failed") << boost::diagnostic_information(e);
            return preverified;
        }
    };
}

P2PInfo Host::p2pInfo()
{
    try
    {
        if (m_p2pInfo.p2pID.empty())
        {
            /// get certificate
            auto* sslContext = m_asioInterface->srvContext()->native_handle();
            X509* cert = SSL_CTX_get0_certificate(sslContext);

            /// get issuer name
            const char* issuer = X509_NAME_oneline(X509_get_issuer_name(cert), NULL, 0);
            std::string issuerName(issuer);

            /// get subject name
            const char* subject = X509_NAME_oneline(X509_get_subject_name(cert), NULL, 0);
            std::string subjectName(subject);

            /// get p2pID
            std::string nodeIDOut;
            if (m_sslContextPubHandler(cert, nodeIDOut))
            {
                m_p2pInfo.p2pID = boost::to_upper_copy(nodeIDOut);
                HOST_LOG(INFO) << LOG_DESC("Get node information from cert")
                               << LOG_KV("shortP2pid", printShortP2pID(m_p2pInfo.p2pID))
                               << LOG_KV("rawP2pID", printShortP2pID(m_p2pInfo.rawP2pID));
            }

            std::string nodeIDOutWithoutExtInfo;
            if (m_sslContextPubHandlerWithoutExtInfo(cert, nodeIDOutWithoutExtInfo))
            {
                m_p2pInfo.p2pIDWithoutExtInfo = boost::to_upper_copy(nodeIDOutWithoutExtInfo);
                HOST_LOG(INFO) << LOG_DESC("Get node information without ext info from cert")
                               << LOG_KV("p2pid without ext info", m_p2pInfo.p2pIDWithoutExtInfo);
            }

            /// fill in the node informations
            m_p2pInfo.agencyName = obtainCommonNameFromSubject(issuerName);
            m_p2pInfo.nodeName = obtainCommonNameFromSubject(subjectName);
            m_p2pInfo.nodeIPEndpoint = NodeIPEndpoint(m_listenHost, m_listenPort);
            /// free resources
            OPENSSL_free((void*)issuer);
            OPENSSL_free((void*)subject);
        }
    }
    catch (std::exception& e)
    {
        HOST_LOG(ERROR) << LOG_DESC("Get node information from cert failed.")
                        << boost::diagnostic_information(e);
        return m_p2pInfo;
    }
    return m_p2pInfo;
}

/**
 * @brief: obtain the common name from the subject of certificate
 *
 * @param subject : the subject of the certificat
 *   the subject format is: /CN=xx/O=xxx/OU=xxx/ commonly
 * @return std::string: the common name of the certificate
 */
std::string Host::obtainCommonNameFromSubject(std::string const& subject)
{
    std::vector<std::string> fields;
    boost::split(fields, subject, boost::is_any_of("/"), boost::token_compress_on);
    for (auto field : fields)
    {
        std::size_t pos = field.find("CN");
        if (pos != std::string::npos)
        {
            std::vector<std::string> cn_fields;
            boost::split(cn_fields, field, boost::is_any_of("="), boost::token_compress_on);
            /// use the whole fields as the common name
            if (cn_fields.size() < 2)
            {
                return field;
            }
            /// return real common name
            return cn_fields[1];
        }
    }
    return subject;
}

/// obtain p2pInfo from given vector
void Host::obtainNodeInfo(P2PInfo& info, std::string const& node_info)
{
    std::vector<std::string> node_info_vec;
    boost::split(node_info_vec, node_info, boost::is_any_of("#"), boost::token_compress_on);
    if (!node_info_vec.empty())
    {
        // raw p2pID
        info.rawP2pID = node_info_vec[0];
        HashType p2pIDHash = m_hashImpl->hash(
            bcos::bytesConstRef((bcos::byte const*)info.rawP2pID.data(), info.rawP2pID.size()));
        // the p2pID, hash(rawP2pID)
        info.p2pID = std::string(p2pIDHash.begin(), p2pIDHash.end());
    }
    if (node_info_vec.size() > 1)
    {
        info.p2pIDWithoutExtInfo = node_info_vec[1];
    }
    if (node_info_vec.size() > 2)
    {
        info.agencyName = obtainCommonNameFromSubject(node_info_vec[2]);
    }
    if (node_info_vec.size() > 3)
    {
        info.nodeName = obtainCommonNameFromSubject(node_info_vec[3]);
    }

    HOST_LOG(INFO) << "obtainP2pInfo " << LOG_KV("node_info", node_info)
                   << LOG_KV("p2pid", printShortP2pID(info.p2pID))
                   << LOG_KV("rawP2pID", printShortP2pID(info.rawP2pID));
}

/**
 * @brief: server calls handshakeServer to after handshake
 *         mainly calls RLPxHandshake to obtain informations(client version,
 * caps, etc), start peer session and start accepting procedure repeatedly
 * @param error: error information triggered in the procedure of ssl handshake
 * @param endpointPublicKey: public key obtained from certificate during
 * handshake
 * @param socket: socket related to the endpoint of the connected client
 */
void Host::handshakeServer(const boost::system::error_code& error,
    std::shared_ptr<std::string> endpointPublicKey, std::shared_ptr<SocketFace> socket)
{
    if (error)
    {
        HOST_LOG(INFO) << LOG_DESC("handshakeServer Handshake failed")
                       << LOG_KV("value", error.value()) << LOG_KV("message", error.message())
                       << LOG_KV("endpoint", socket->nodeIPEndpoint());
        socket->close();
        return;
    }
    const std::string& nodeInfo = *endpointPublicKey;
    if (nodeInfo.empty())
    {
        HOST_LOG(INFO) << LOG_DESC("handshakeServer get p2pID failed")
                       << LOG_KV("remote endpoint", socket->remoteEndpoint());
        socket->close();
        return;
    }
    if (m_run)
    {
        /// node info splitted with #
        /// format: {nodeId}{#}{agencyName}{#}{nodeName}
        P2PInfo info;
        obtainNodeInfo(info, nodeInfo);
        HOST_LOG(INFO) << LOG_DESC("handshakeServer succ")
                       << LOG_KV("remote endpoint", socket->remoteEndpoint())
                       << LOG_KV("shortP2pid", printShortP2pID(info.p2pID))
                       << LOG_KV("rawP2pID", printShortP2pID(info.rawP2pID));
        startPeerSession(info, socket, m_connectionHandler);
    }
}

/**
 * @brief: start peer sessions after handshake succeed(called by
 * RLPxHandshake), mainly include four functions:
 *         1. disconnect connecting host with invalid capability
 *         2. modify m_peers && disconnect already-connected session
 *         3. modify m_sessions and m_staticNodes
 *         4. start new session (session->start())
 * @param _pub: node id of the connecting client
 * @param _rlp: informations obtained from the client-peer during handshake
 *              now include protocolVersion, clientVersion, caps and
 * listenPort
 * @param _s : connected socket(used to init session object)
 */
// TODO: asyncConnect pass handle to startPeerSession, make use of it
void Host::startPeerSession(P2PInfo const& p2pInfo, std::shared_ptr<SocketFace> const& socket,
    std::function<void(NetworkException, P2PInfo const&, std::shared_ptr<SessionFace>)>)
{
    auto weakHost = weak_from_this();
    std::shared_ptr<SessionFace> session =
        m_sessionFactory->createSession(*this, socket, m_messageFactory, m_sessionCallbackManager);

    m_taskArena.execute([&]() {
        m_asyncGroup.run([weakHost, session = std::move(session), p2pInfo]() {
            auto host = weakHost.lock();
            if (!host)
            {
                return;
            }
            if (host->m_connectionHandler)
            {
                host->m_connectionHandler(NetworkException(0, ""), p2pInfo, session);
            }
            else
            {
                HOST_LOG(WARNING) << LOG_DESC("No connectionHandler, new connection may lost");
            }
        });
    });
    HOST_LOG(INFO) << LOG_DESC("startPeerSession, Remote=") << socket->remoteEndpoint()
                   << LOG_KV("local endpoint", socket->localEndpoint())
                   << LOG_KV("shortP2pid", printShortP2pID(p2pInfo.p2pID))
                   << LOG_KV("rawP2pID", printShortP2pID(p2pInfo.rawP2pID));
}

/**
 * @brief: remove expired timer
 *         modify alived peers to m_peers
 *         reconnect all nodes recorded in m_staticNodes periodically
 */
void Host::start()
{
    /// if the p2p network has been stoped, then stop related service
    if (!haveNetwork())
    {
        m_run = true;
        m_asioInterface->init(m_listenHost, m_listenPort);
        if (m_asioInterface->acceptor())
        {
            startAccept();
        }
        m_asioInterface->start();
    }
}

/**
 * @brief : connect to the server
 * @param _nodeIPEndpoint : the endpoint of the connected server
 */
void Host::asyncConnect(NodeIPEndpoint const& _nodeIPEndpoint,
    std::function<void(NetworkException, P2PInfo const&, std::shared_ptr<SessionFace>)> callback)
{
    if (!m_run)
    {
        return;
    }
    HOST_LOG(INFO) << LOG_DESC("Connecting to node") << LOG_KV("endpoint", _nodeIPEndpoint);
    {
        Guard l(x_pendingConns);
        auto it = m_pendingConns.find(_nodeIPEndpoint);
        if (it != m_pendingConns.end())
        {
            BCOS_LOG(TRACE) << LOG_DESC("asyncConnected node is in the pending list")
                            << LOG_KV("endpoint", _nodeIPEndpoint);
            return;
        }
    }

    std::shared_ptr<SocketFace> socket = m_asioInterface->newSocket(false, _nodeIPEndpoint);
    /// if async connect timeout, close the socket directly
    auto connectTimer = std::make_shared<boost::asio::deadline_timer>(
        socket->ioService(), boost::posix_time::milliseconds(m_connectTimeThre));
    connectTimer->async_wait(
        [this, socket, _nodeIPEndpoint](const boost::system::error_code& error) {
            /// return when cancel has been called
            if (error == boost::asio::error::operation_aborted)
            {
                HOST_LOG(DEBUG) << LOG_DESC("AsyncConnect handshake handler revoke this operation");
                return;
            }
            /// connection timer error
            if (error && error != boost::asio::error::operation_aborted)
            {
                HOST_LOG(ERROR) << LOG_DESC("AsyncConnect timer failed")
                                << LOG_KV("errorValue", error.value())
                                << LOG_KV("message", error.message());
            }
            if (socket->isConnected())
            {
                HOST_LOG(WARNING) << LOG_DESC("AsyncConnect timeout erase")
                                  << LOG_KV("endpoint", _nodeIPEndpoint);
                erasePendingConns(_nodeIPEndpoint);
                socket->close();
            }
        });
    /// callback async connect
    m_asioInterface->asyncResolveConnect(socket,
        [this, callback = std::move(callback), _nodeIPEndpoint, socket,
            connectTimer = std::move(connectTimer)](boost::system::error_code const& ec) mutable {
            if (ec)
            {
                HOST_LOG(ERROR) << LOG_DESC("TCP Connection refused by node")
                                << LOG_KV("endpoint", _nodeIPEndpoint)
                                << LOG_KV("message", ec.message());
                socket->close();

                m_taskArena.execute([&]() {
                    m_asyncGroup.run([callback = std::move(callback)]() {
                        callback(NetworkException(ConnectError, "Connect failed"), {}, {});
                    });
                });
                return;
            }
            insertPendingConns(_nodeIPEndpoint);
            /// get the public key of the server during handshake
            std::shared_ptr<std::string> endpointPublicKey = std::make_shared<std::string>();
            m_asioInterface->setVerifyCallback(socket, newVerifyCallback(endpointPublicKey));
            /// call handshakeClient after handshake succeed
            m_asioInterface->asyncHandshake(socket, ba::ssl::stream_base::client,
                [self = shared_from_this(), socket,
                    endpointPublicKey = std::move(endpointPublicKey),
                    callback = std::move(callback), nodeIPEndPoint = _nodeIPEndpoint,
                    connectTimer = std::move(connectTimer)](auto error) mutable {
                    self->handshakeClient(error, std::move(socket), endpointPublicKey,
                        std::move(callback), nodeIPEndPoint, std::move(connectTimer));
                });
        });
}

/**
 * @brief : start RLPxHandshake procedure after ssl handshake succeed
 * @param error: error returned by ssl handshake
 * @param socket : ssl socket
 * @param endpointPublicKey: public key of the server obtained from the
 * certificate
 * @param _nodeIPEndpoint : endpoint of the server to connect
 */
void Host::handshakeClient(const boost::system::error_code& error,
    std::shared_ptr<SocketFace> socket, std::shared_ptr<std::string> endpointPublicKey,
    std::function<void(NetworkException, P2PInfo const&, std::shared_ptr<SessionFace>)> callback,
    NodeIPEndpoint _nodeIPEndpoint, std::shared_ptr<boost::asio::deadline_timer> timerPtr)
{
    timerPtr->cancel();
    erasePendingConns(_nodeIPEndpoint);
    if (error)
    {
        HOST_LOG(WARNING) << LOG_DESC("handshakeClient failed")
                          << LOG_KV("endpoint", _nodeIPEndpoint) << LOG_KV("value", error.value())
                          << LOG_KV("message", error.message());

        if (socket->isConnected())
        {
            socket->close();
        }
        return;
    }
    const std::string& nodeInfo = *endpointPublicKey;
    if (nodeInfo.empty())
    {
        HOST_LOG(WARNING) << LOG_DESC("handshakeClient get p2pID failed")
                          << LOG_KV("local endpoint", socket->localEndpoint());
        socket->close();
        return;
    }

    if (m_run)
    {
        P2PInfo info;
        obtainNodeInfo(info, nodeInfo);
        HOST_LOG(INFO) << LOG_DESC("handshakeClient succ")
                       << LOG_KV("local endpoint", socket->localEndpoint());
        startPeerSession(info, socket, std::move(callback));
    }
}

/// stop the network and worker thread
void Host::stop()
{
    // ignore if already stopped/stopping
    if (!m_run)
    {
        return;
    }
    // signal run() to prepare for shutdown and reset m_timer
    m_run = false;
    if (m_asioInterface)
    {
        m_asioInterface->stop();
    }
    m_asyncGroup.wait();
}
bcos::gateway::Host::Host(bcos::crypto::Hash::Ptr _hash,
    std::shared_ptr<ASIOInterface> _asioInterface, std::shared_ptr<SessionFactory> _sessionFactory,
    MessageFactory::Ptr _messageFactory)
  : m_hashImpl(std::move(_hash)),
    m_asioInterface(std::move(_asioInterface)),
    m_sessionFactory(std::move(_sessionFactory)),
    m_messageFactory(std::move(_messageFactory)) {};
bcos::gateway::Host::~Host()
{
    stop();
};
uint16_t bcos::gateway::Host::listenPort() const
{
    return m_listenPort;
}
bool bcos::gateway::Host::haveNetwork() const
{
    return m_run;
}
std::string bcos::gateway::Host::listenHost() const
{
    return m_listenHost;
}
void bcos::gateway::Host::setHostPort(std::string host, uint16_t port)
{
    m_listenHost = std::move(host);
    m_listenPort = port;
}
std::function<void(NetworkException, P2PInfo const&, std::shared_ptr<SessionFace>)>
bcos::gateway::Host::connectionHandler() const
{
    return m_connectionHandler;
}
void bcos::gateway::Host::setConnectionHandler(
    std::function<void(NetworkException, P2PInfo const&, std::shared_ptr<SessionFace>)>
        connectionHandler)
{
    m_connectionHandler = std::move(connectionHandler);
}
std::function<bool(X509* x509, std::string& pubHex)> bcos::gateway::Host::sslContextPubHandler()
{
    return m_sslContextPubHandler;
}
void bcos::gateway::Host::setSSLContextPubHandler(
    std::function<bool(X509* x509, std::string& pubHex)> _sslContextPubHandler)
{
    m_sslContextPubHandler = std::move(_sslContextPubHandler);
}
std::function<bool(X509* x509, std::string& pubHex)>
bcos::gateway::Host::sslContextPubHandlerWithoutExtInfo()
{
    return m_sslContextPubHandlerWithoutExtInfo;
}
void bcos::gateway::Host::setSSLContextPubHandlerWithoutExtInfo(
    std::function<bool(X509* x509, std::string& pubHex)> _sslContextPubHandlerWithoutExtInfo)
{
    m_sslContextPubHandlerWithoutExtInfo = std::move(_sslContextPubHandlerWithoutExtInfo);
}
void bcos::gateway::Host::setSessionCallbackManager(
    SessionCallbackManagerInterface::Ptr sessionCallbackManager)
{
    m_sessionCallbackManager = std::move(sessionCallbackManager);
}
std::shared_ptr<ASIOInterface> bcos::gateway::Host::asioInterface() const
{
    return m_asioInterface;
}
std::shared_ptr<SessionFactory> bcos::gateway::Host::sessionFactory() const
{
    return m_sessionFactory;
}
bcos::gateway::MessageFactory::Ptr bcos::gateway::Host::messageFactory() const
{
    return m_messageFactory;
}
void bcos::gateway::Host::setPeerBlacklist(PeerBlackWhitelistInterface::Ptr _peerBlacklist)
{
    m_peerBlacklist = std::move(_peerBlacklist);
}
bcos::gateway::PeerBlackWhitelistInterface::Ptr bcos::gateway::Host::peerBlacklist()
{
    return m_peerBlacklist;
}
void bcos::gateway::Host::setPeerWhitelist(PeerBlackWhitelistInterface::Ptr _peerWhitelist)
{
    m_peerWhitelist = std::move(_peerWhitelist);
}
bcos::gateway::PeerBlackWhitelistInterface::Ptr bcos::gateway::Host::peerWhitelist()
{
    return m_peerWhitelist;
}
void bcos::gateway::Host::setEnableSslVerify(bool _enableSSLVerify)
{
    m_enableSSLVerify = _enableSSLVerify;
    HOST_LOG(INFO) << LOG_DESC("setEnableSslVerify")
                   << LOG_KV("enableSSLVerify", m_enableSSLVerify);
}
void bcos::gateway::Host::erasePendingConns(NodeIPEndpoint const& nodeIPEndpoint)
{
    bcos::Guard lock(x_pendingConns);
    auto it = m_pendingConns.find(nodeIPEndpoint);
    if (it != m_pendingConns.end())
    {
        m_pendingConns.erase(it);
    }
}
void bcos::gateway::Host::insertPendingConns(NodeIPEndpoint const& nodeIPEndpoint)
{
    bcos::Guard lock(x_pendingConns);
    auto it = m_pendingConns.lower_bound(nodeIPEndpoint);
    if (it == m_pendingConns.end() || *it != nodeIPEndpoint)
    {
        m_pendingConns.emplace_hint(it, nodeIPEndpoint);
    }
}
