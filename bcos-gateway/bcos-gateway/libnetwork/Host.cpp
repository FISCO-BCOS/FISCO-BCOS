
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
#include "bcos-gateway/libnetwork/Host.h"
#include "bcos-gateway/libnetwork/ASIOInterface.h"
#include "bcos-gateway/libnetwork/Common.h"
#include "bcos-gateway/libnetwork/Session.h"
#include "bcos-gateway/libnetwork/SocketFace.h"
#include "bcos-utilities/IOServicePool.h"
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <set>
#include <utility>
#include <bcos-utilities/BoostLog.h>


using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::crypto;

namespace
{
// FIB-186: RAII guard bound to an in-flight TLS handshake. Constructed after a handshake slot is
// acquired and moved into the handshake completion handler; its destructor releases the slot when
// the handler runs (success / failure / abort) or is destroyed on shutdown. Held via weak_ptr so a
// Host torn down before the handshake completes does not crash the guard.
struct HandshakeSlotGuard
{
    std::weak_ptr<Host> host;
    explicit HandshakeSlotGuard(std::weak_ptr<Host> _host) : host(std::move(_host)) {}
    HandshakeSlotGuard(const HandshakeSlotGuard&) = delete;
    HandshakeSlotGuard& operator=(const HandshakeSlotGuard&) = delete;
    ~HandshakeSlotGuard()
    {
        if (auto h = host.lock())
        {
            h->releaseHandshakeSlot();
        }
    }
};
}  // namespace

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
                // FIB-186: DEBUG, not INFO — under connection churn this fires on every accept and
                // would flood the log, letting a low-trust peer fill the disk.
                HOST_LOG(DEBUG) << LOG_DESC("P2P Recv Connect, From=") << endpoint;
                // FIB-186: bound concurrent in-flight TLS handshakes (global cap) BEFORE starting
                // the handshake. The FIB-184 session caps apply only after the handshake completes,
                // so connection churn from a low-trust peer would otherwise flood the shared I/O
                // thread-pool with accept / handshake / teardown work and starve inter-validator
                // PBFT reads, halting consensus. Over the cap, drop the socket and re-arm accept
                // without running the (CPU-heavy) TLS handshake.
                std::string remoteAddress = socket->nodeIPEndpoint().address();
                // FIB-186: bound admission of new connections BEFORE the CPU-heavy TLS handshake,
                // so connection churn from a low-trust peer cannot flood the shared I/O pool with
                // accept / handshake / teardown work and starve inter-validator PBFT reads (the
                // FIB-184 session caps apply only AFTER the handshake completes). Reserve the
                // in-flight-handshake slot first because it is the refundable check: if the
                // accept-rate limiter below then rejects, we release the slot and no rate token is
                // spent. Checking the rate token first would instead waste a token whenever the
                // handshake cap is already saturated, needlessly lowering the effective accept rate
                // for legitimate peers arriving in that window.
                if (!tryAcquireHandshakeSlot())
                {
                    HOST_LOG(DEBUG) << LOG_BADGE("startAccept")
                                    << LOG_DESC("pending-handshake cap reached, reject connection")
                                    << LOG_KV("address", remoteAddress)
                                    << LOG_KV("pendingHandshakes", currentPendingHandshakes())
                                    << LOG_KV("maxPendingHandshakes", m_maxPendingHandshakes);
                    socket->close();
                    startAccept();
                    return;
                }
                // Accept-rate token bucket: drops a churn flood cheaply (accept + close) before
                // paying handshake CPU, which the concurrency cap alone does not. On rejection the
                // handshake slot reserved just above must be released so it is not leaked -- the
                // HandshakeSlotGuard that normally releases it is only created once both checks
                // pass.
                if (!tryAcquireConnectionToken())
                {
                    releaseHandshakeSlot();
                    HOST_LOG(DEBUG) << LOG_BADGE("startAccept")
                                    << LOG_DESC("connection accept-rate limit reached, reject")
                                    << LOG_KV("address", remoteAddress)
                                    << LOG_KV("maxConnectionsPerSecond", m_maxConnectionsPerSecond);
                    socket->close();
                    startAccept();
                    return;
                }
                // Release the slot exactly once when the handshake completes (success, failure, or
                // abort): the guard rides the completion handler and is destroyed with it.
                auto handshakeGuard = std::make_shared<HandshakeSlotGuard>(weak_from_this());
                // FIB-186: bound the handshake's lifetime. A stalled / slow TLS handshake would
                // otherwise never complete, so its admission slot (above) would never be released
                // and this Host (kept alive by the completion handler's shared_from_this) could
                // never be destroyed. On timeout close the socket; that completes async_handshake
                // with an error, so the completion handler runs, the guard is destroyed and the
                // slot released. The timer and the handshake completion run on the socket's single
                // io_context thread, so they are serialised (no race on close/cancel). Same pattern
                // as the outbound connectTimer.
                auto handshakeTimer = std::make_shared<boost::asio::steady_timer>(
                    socket->ioService(), std::chrono::milliseconds(m_handshakeTimeout));
                handshakeTimer->async_wait([socket](const boost::system::error_code& timerError) {
                    if (timerError == boost::asio::error::operation_aborted)
                    {
                        return;
                    }
                    if (socket->isConnected())
                    {
                        HOST_LOG(WARNING) << LOG_BADGE("startAccept")
                                          << LOG_DESC("in-flight handshake timed out, close socket")
                                          << LOG_KV("endpoint", socket->nodeIPEndpoint());
                        socket->close();
                    }
                });
                /// register ssl callback to get the NodeID of peers
                std::shared_ptr<std::string> endpointPublicKey = std::make_shared<std::string>();
                m_asioInterface->setVerifyCallback(socket, newVerifyCallback(endpointPublicKey));
                m_asioInterface->asyncHandshake(socket, ba::ssl::stream_base::server,
                    [self = shared_from_this(), endpointPublicKey, socket, handshakeGuard,
                        handshakeTimer](const boost::system::error_code& handshakeError) {
                        handshakeTimer->cancel();
                        self->handshakeServer(handshakeError, endpointPublicKey, socket);
                    });

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
// FIB-184: reserve a session slot under the global and per-IP caps. Returns false when either
// cap is reached; the caller must then close the socket without creating a session.
bool Host::tryAcquireSessionSlot(std::string const& _address)
{
    std::lock_guard<std::mutex> lock(x_sessionCountPerIP);
    if (m_sessionCount.load(std::memory_order_relaxed) >= m_maxConcurrentSessions)
    {
        return false;
    }
    auto& perIP = m_sessionCountPerIP[_address];
    if (perIP >= m_maxSessionsPerIP)
    {
        return false;
    }
    ++perIP;
    m_sessionCount.fetch_add(1, std::memory_order_relaxed);
    return true;
}

// FIB-184: release a previously reserved slot. Runs from the session lifetime guard's
// destructor, i.e. exactly once when the session object is destroyed.
void Host::releaseSessionSlot(std::string const& _address)
{
    std::lock_guard<std::mutex> lock(x_sessionCountPerIP);
    auto it = m_sessionCountPerIP.find(_address);
    if (it != m_sessionCountPerIP.end())
    {
        if (it->second > 0 && --(it->second) == 0)
        {
            m_sessionCountPerIP.erase(it);
        }
    }
    if (m_sessionCount.load(std::memory_order_relaxed) > 0)
    {
        m_sessionCount.fetch_sub(1, std::memory_order_relaxed);
    }
}

// FIB-186: reserve an in-flight-handshake slot under the global cap. Returns false when the cap is
// reached; the caller must then close the socket and re-arm accept without starting the (CPU-heavy)
// TLS handshake. Uses a dedicated mutex so it does not contend with the session-cap path. Global
// count only -- a per-IP cap is trivially bypassed by source-IP rotation (see Host.h).
bool Host::tryAcquireHandshakeSlot()
{
    std::lock_guard<std::mutex> lock(x_pendingHandshakes);
    if (m_pendingHandshakes >= m_maxPendingHandshakes)
    {
        return false;
    }
    ++m_pendingHandshakes;
    return true;
}

// FIB-186: release a previously reserved handshake slot. Runs from the HandshakeSlotGuard bound to
// the handshake completion handler, i.e. exactly once when the handshake finishes or is aborted.
void Host::releaseHandshakeSlot()
{
    std::lock_guard<std::mutex> lock(x_pendingHandshakes);
    if (m_pendingHandshakes > 0)
    {
        --m_pendingHandshakes;
    }
}

// FIB-186: token-bucket rate limiter for accepted new connections. Refills at
// m_maxConnectionsPerSecond (also the burst cap), consumes one token per accepted connection, and
// returns false once the bucket is empty so the caller drops the connection before the TLS
// handshake. 0 = unlimited. Bounds handshake CPU per unit time (the concurrency caps do not).
bool Host::tryAcquireConnectionToken()
{
    // Lock-free read: m_maxConnectionsPerSecond is set once by GatewayFactory before start() (see
    // setMaxConnectionsPerSecond), so no writer races with this fast-path check.
    if (m_maxConnectionsPerSecond == 0)
    {
        return true;
    }
    std::lock_guard<std::mutex> lock(x_connectionRate);
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - m_lastTokenRefill).count();
    m_lastTokenRefill = now;
    m_connectionTokens = std::min<double>(
        m_maxConnectionsPerSecond, m_connectionTokens + elapsed * m_maxConnectionsPerSecond);
    if (m_connectionTokens >= 1.0)
    {
        m_connectionTokens -= 1.0;
        return true;
    }
    return false;
}

namespace
{
// FIB-184: RAII guard bound to a session's lifetime. Constructed after a slot is acquired and
// attached to the session via setLifetimeGuard(); its destructor (running in ~Session) releases
// the slot. Held via weak_ptr so a Host destroyed before the session does not crash the guard.
struct SessionSlotGuard
{
    std::weak_ptr<Host> host;
    std::string address;
    SessionSlotGuard(std::weak_ptr<Host> _host, std::string _address)
      : host(std::move(_host)), address(std::move(_address))
    {}
    SessionSlotGuard(const SessionSlotGuard&) = delete;
    SessionSlotGuard& operator=(const SessionSlotGuard&) = delete;
    ~SessionSlotGuard()
    {
        if (auto h = host.lock())
        {
            h->releaseSessionSlot(address);
        }
    }
};
}  // namespace

void Host::startPeerSession(P2PInfo const& p2pInfo, std::shared_ptr<SocketFace> const& socket,
    std::function<void(NetworkException, P2PInfo const&, std::shared_ptr<SessionFace>)>)
{
    auto weakHost = weak_from_this();

    // FIB-184: enforce the concurrent-session and per-IP caps before creating the session.
    // Past the limit, close the socket and drop the connection so an authenticated peer cannot
    // exhaust memory by churning TLS connections.
    std::string remoteAddress = socket->nodeIPEndpoint().address();
    if (!tryAcquireSessionSlot(remoteAddress))
    {
        HOST_LOG(WARNING) << LOG_BADGE("startPeerSession")
                          << LOG_DESC("session cap reached, reject connection")
                          << LOG_KV("address", remoteAddress)
                          << LOG_KV("sessionCount", m_sessionCount.load())
                          << LOG_KV("maxConcurrentSessions", m_maxConcurrentSessions)
                          << LOG_KV("maxSessionsPerIP", m_maxSessionsPerIP);
        socket->close();
        return;
    }

    std::shared_ptr<SessionFace> session =
        m_sessionFactory->createSession(*this, socket, m_messageFactory, m_sessionCallbackManager);
    // Bind a slot-release guard to the session; the slot is freed when the session is destroyed.
    session->setLifetimeGuard(std::make_shared<SessionSlotGuard>(weakHost, remoteAddress));

    boost::asio::post(socket->ioService(), [weakHost, session = std::move(session), p2pInfo]() {
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
        if (m_asioInterface->acceptor())
        {
            startAccept();
        }
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
    auto connectTimer = std::make_shared<boost::asio::steady_timer>(
        socket->ioService(), std::chrono::milliseconds(m_connectTimeThre));
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

                boost::asio::post(socket->ioService(), [callback = std::move(callback)]() mutable {
                    callback(NetworkException(ConnectError, "Connect failed"), {}, {});
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
    NodeIPEndpoint _nodeIPEndpoint, std::shared_ptr<boost::asio::steady_timer> timerPtr)
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
    // FIB-186 (vector D): the dedicated teardown executor is deliberately NOT stopped here.
    // Clearing m_run above is what stops work arriving: Session::drop() checks haveNetwork() and
    // runs the teardown notification inline once it is false, so nothing new is enqueued after this
    // point. The executor is stopped and joined by ~IOServicePool when the Host is destroyed -- the
    // same io_context::stop() + join that the ThreadPool::stop() this replaces performed, so the
    // "no teardown notification outlives the Host" guarantee is unchanged. Stopping it here instead
    // would leave a live Host holding a dead executor, and any drop() racing the m_run store would
    // silently lose its disconnect notification.
}
bcos::gateway::Host::Host(bcos::crypto::Hash::Ptr _hash,
    std::shared_ptr<ASIOInterface> _asioInterface, std::shared_ptr<SessionFactory> _sessionFactory,
    MessageFactory::Ptr _messageFactory)
  : m_hashImpl(std::move(_hash)),
    m_asioInterface(std::move(_asioInterface)),
    m_sessionFactory(std::move(_sessionFactory)),
    m_messageFactory(std::move(_messageFactory))
{
    // FIB-186 (vector D): a single dedicated thread for session-teardown notifications, off the
    // shared IOServicePool that carries inbound-message delivery. See postTeardown / Host.h.
    // A one-worker IOServicePool is the direct replacement for the ThreadPool("p2pTeardown", 1)
    // this used to be: one owned io_context, one owned thread, stopped and joined on destruction.
    m_teardownPool = std::make_shared<bcos::IOServicePool>(1, "p2pTeardown");
}

void bcos::gateway::Host::postTeardown(std::function<void()> f)
{
    // IOServicePool::post already wraps the task in safeExecute, so a throwing teardown
    // notification cannot kill the dedicated thread and wedge every later teardown.
    m_teardownPool->post(std::move(f));
}
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
const std::shared_ptr<ASIOInterface>& bcos::gateway::Host::asioInterface() const
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
