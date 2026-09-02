
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
#include <bcos-task/Wait.h>
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
// acquired; its destructor releases the slot exactly once when the frame it rides in unwinds
// (success / failure / abort) or is destroyed. Held via weak_ptr so a Host torn down before the
// handshake completes does not crash the guard.
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

// FIB-186: reserve an in-flight-handshake slot and return an owning guard (nullptr when the cap
// is reached). Combining the acquire with the guard construction closes the "throw between
// acquire and guard" window — a throw after the acquire but before the guard existed would leak
// the slot. Returned as shared_ptr<void> so the concrete guard type stays an implementation
// detail; the guard is handed into the serverHandshake coroutine frame, so acquire and release
// live in the same frame.
std::shared_ptr<void> tryAcquireHandshakeSlotGuard(std::weak_ptr<Host> host)
{
    auto hostPtr = host.lock();
    if (!hostPtr || !hostPtr->tryAcquireHandshakeSlot())
    {
        return nullptr;
    }
    try
    {
        return std::make_shared<HandshakeSlotGuard>(std::move(host));
    }
    catch (...)
    {
        // guard construction failed (allocation): release the just-acquired slot so it is not
        // leaked, then rethrow so the accept-loop iteration is skipped
        hostPtr->releaseHandshakeSlot();
        throw;
    }
}
}  // namespace

// FIB-186: member wrapper over the anonymous-namespace factory above, so acceptLoop and tests
// drive the exact same acquire-and-guard path.
std::shared_ptr<void> Host::acquireHandshakeSlotGuard()
{
    return tryAcquireHandshakeSlotGuard(weak_from_this());
}

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
void Host::startAccept(boost::system::error_code /*boost_error*/)
{
    /// accept the connection
    if (m_run)
    {
        HOST_LOG(INFO) << LOG_DESC("P2P StartAccept") << LOG_KV("Host", m_listenHost) << ":"
                       << m_listenPort;
        // fire-and-forget: the detached task owns the coroutine chain; acceptLoop() exits when
        // Host::stop() clears m_run and cancels the acceptor. Arm the exit latch BEFORE launching
        // so stop() can never miss a started loop (see Host::stop).
        m_acceptLoopStarted.store(true, std::memory_order_release);
        task::wait(acceptLoop());
    }
}

task::Task<void> Host::acceptLoop()
{
    // The frame holds the Host alive for the whole accept loop. This deliberately extends the old
    // lifetime (the accept handler held a raw this): the loop only exits after Host::stop()
    // cancels the acceptor, so a pending async_accept can never fire on a destroyed Host.
    auto self = shared_from_this();
    // The try/catch lives INSIDE the while loop: this loop is the only thing that re-arms
    // async_accept, so an exception escaping it (newSocket allocation, remoteEndpoint, or the
    // coroutine-frame allocation inside task::wait(serverHandshake(...))) would permanently stop
    // inbound connection acceptance while the node keeps running and reports itself healthy. A
    // failed iteration must be survivable, so each one is guarded and the loop continues.
    while (m_run)
    {
        bool iterationFailed = false;
        try
        {
            auto socket = m_asioInterface->newSocket(true, NodeIPEndpoint());
            auto [ec] = co_await m_asioInterface->awaitableAccept(socket);
            /// get the endpoint information of remote client after accept the connections
            auto endpoint = socket->remoteEndpoint();
            HOST_LOG(TRACE) << LOG_DESC("P2P Recv Connect, From=") << endpoint;
            /// network accept failed
            if (ec || !m_run)
            {
                // A REAL accept error (EMFILE/ENFILE — fd exhaustion arrives here as an
                // error_code, not as a throw) must take the same retry backoff as a thrown
                // iteration below: the pending connection stays in the listen backlog, so the
                // next async_accept fails identically and an un-delayed loop would spin on the
                // acceptor's io_context thread, allocating a Socket + ssl::stream per turn and
                // starving the sessions and the shared resolver on that context.
                // operation_aborted (Host::stop()'s cancel) stays on the fast path so shutdown
                // is not delayed — and never logged at ERROR.
                if (ec && ec != boost::asio::error::operation_aborted)
                {
                    HOST_LOG(ERROR) << LOG_DESC("accept failed")
                                    << LOG_KV("message", ec.message());
                    iterationFailed = true;
                }
                socket->close();
                // NO continue here: a failed iteration must FALL THROUGH to the retry backoff
                // below (an early continue would skip it and leave iterationFailed dead).
                // Shutdown is unaffected — !m_run with no ec leaves iterationFailed false, so
                // the while condition exits without the delay.
            }
            else
            {
                /// if the connected peer over the limitation, drop socket
                socket->setNodeIPEndpoint(endpoint);
                // FIB-186: DEBUG, not INFO — under connection churn this fires on every accept and
                // would flood the log, letting a low-trust peer fill the disk.
                HOST_LOG(DEBUG) << LOG_DESC("P2P Recv Connect, From=") << endpoint;
                // FIB-186: bound admission of new connections BEFORE the CPU-heavy TLS handshake,
                // so connection churn from a low-trust peer cannot flood the shared I/O pool with
                // accept / handshake / teardown work and starve inter-validator PBFT reads (the
                // FIB-184 session caps apply only AFTER the handshake completes). Reserve the
                // in-flight-handshake slot first because it is the refundable check: if the
                // accept-rate limiter below then rejects, the guard is destroyed and no rate token
                // is spent. Checking the rate token first would instead waste a token whenever the
                // handshake cap is already saturated, needlessly lowering the effective accept rate
                // for legitimate peers arriving in that window. The guard is handed into
                // serverHandshake, so the slot's acquire and release live in the same coroutine
                // frame (see tryAcquireHandshakeSlotGuard).
                std::string remoteAddress = socket->nodeIPEndpoint().address();
                auto handshakeGuard = tryAcquireHandshakeSlotGuard(weak_from_this());
                if (!handshakeGuard)
                {
                    HOST_LOG(DEBUG) << LOG_BADGE("startAccept")
                                    << LOG_DESC("pending-handshake cap reached, reject connection")
                                    << LOG_KV("address", remoteAddress)
                                    << LOG_KV("pendingHandshakes", currentPendingHandshakes())
                                    << LOG_KV("maxPendingHandshakes", m_maxPendingHandshakes);
                    socket->close();
                    continue;
                }
                // Accept-rate token bucket: drops a churn flood cheaply (accept + close) before
                // paying handshake CPU, which the concurrency cap alone does not. On rejection the
                // handshake slot reserved just above is released by the guard going out of scope.
                if (!tryAcquireConnectionToken())
                {
                    HOST_LOG(DEBUG) << LOG_BADGE("startAccept")
                                    << LOG_DESC("connection accept-rate limit reached, reject")
                                    << LOG_KV("address", remoteAddress)
                                    << LOG_KV("maxConnectionsPerSecond", m_maxConnectionsPerSecond);
                    socket->close();
                    continue;
                }
                // Run the per-connection TLS handshake in its own coroutine and keep accepting:
                // the old code re-armed accept right after arming the async handshake, and
                // awaiting the handshake here would serialize accepts (one handshake at a time),
                // defeating the concurrency-cap design. The handshake slot guard travels with the
                // coroutine frame, so the slot is released exactly when that frame unwinds.
                task::wait(serverHandshake(std::move(socket), std::move(handshakeGuard)));
            }
        }
        catch (...)
        {
            // never let an exception escape into the resuming asio handler (see AsioAwaitable.h);
            // a failed iteration must not kill the accept loop, so log and continue
            iterationFailed = true;
            HOST_LOG(ERROR) << LOG_DESC("accept iteration exception")
                            << LOG_KV("what", boost::current_exception_diagnostic_information());
        }
        // Give a failed iteration a suspension point before retrying. Two failure shapes land
        // here: a throw (newSocket() allocation — the canonical cause is fd exhaustion —,
        // remoteEndpoint, or the frame allocation inside task::wait(serverHandshake(...))) never
        // reached the accept co_await, so falling straight through would spin synchronously; and
        // a real accept error_code (EMFILE/ENFILE, marked above) would re-fail identically on the
        // next async_accept because the pending connection stays in the listen backlog. Both
        // would starve the acceptor's io_context thread — the shared resolver and the session
        // sockets live on it. A short timer turns a persistent failure into a slow retry loop
        // instead of a livelock. (co_await is not permitted inside a catch handler, so the delay
        // lives after the try/catch, reached only on a failed iteration.)
        // The timer MUST be armed on the acceptor's own executor (newAcceptorTimer), not on a
        // round-robin pool context: co_await resumes the loop on the timer's thread, and only
        // the acceptor's single io_context thread serializes the next while (m_run) re-check and
        // async_accept re-arm against the cancelAcceptor() that Host::stop() posts to that same
        // context. Resuming on a foreign pool thread would reopen the check-then-act window —
        // stop()'s cancel could land between the m_run read and the re-arm, be consumed by a
        // acceptor with nothing pending, and leave a fresh async_accept that never completes,
        // making the Host immortal (see the m_run contract in Host.h) — and would race the
        // posted cancel() on the acceptor object itself ("Shared objects: Unsafe").
        if (iterationFailed)
        {
            // Guard the retry itself: newAcceptorTimer / async_wait run OUTSIDE the iteration's
            // try above, so a throw here (timer allocation, initiation failure rethrown by
            // await_resume) would escape acceptLoop entirely and permanently stop inbound
            // acceptance while m_run stays true. Log and let the while loop retry.
            try
            {
                auto retryTimer = m_asioInterface->newAcceptorTimer(ACCEPT_RETRY_INTERVAL_MS);
                co_await makeAsioAwaitable<boost::system::error_code>(
                    [&retryTimer](auto handler) {
                        retryTimer.async_wait(std::move(handler));
                    });
            }
            catch (...)
            {
                HOST_LOG(ERROR) << LOG_DESC("accept retry timer exception")
                                << LOG_KV(
                                       "what", boost::current_exception_diagnostic_information());
            }
        }
    }
    // Satisfy the stop() exit latch (see Host::stop): the loop has returned, so its frame's
    // strong Host reference is about to go away. The frame-destroy rescue path never reaches
    // here — that is precisely the case stop()'s bounded wait exists to diagnose.
    try
    {
        m_acceptLoopExit.set_value();
    }
    catch (...)
    {
        // a second acceptLoop after a Host restart finds the promise already satisfied
    }
}

task::Task<void> Host::serverHandshake(
    std::shared_ptr<SocketFace> socket, std::shared_ptr<void> handshakeGuard)
{
    auto self = shared_from_this();
    // The handshakeGuard owns the reserved FIB-186 admission slot; it is destroyed exactly when
    // this frame unwinds (handshake success, failure, abort, or the completion-or-cancel rescue
    // destroying the frame), releasing the slot exactly once. Acquired in acceptLoop so the
    // slot/token admission ordering is preserved (see acceptLoop).
    try
    {
        // FIB-186: bound the handshake's lifetime. A stalled / slow TLS handshake would
        // otherwise never complete, so its admission slot would never be released and this Host
        // (kept alive by the coroutine frame's shared_from_this) could never be destroyed. On
        // timeout close the socket; that completes async_handshake with an error, so the
        // coroutine resumes, the guard is destroyed and the slot released. The timer and the
        // handshake completion run on the socket's single io_context thread, so they are
        // serialised (no race on close/cancel). Same pattern as the outbound connectTimer.
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
        auto [handshakeError] =
            co_await m_asioInterface->awaitableHandshake(socket, ba::ssl::stream_base::server);
        handshakeTimer->cancel();
        handshakeServer(handshakeError, endpointPublicKey, socket);
    }
    catch (...)
    {
        // never let an exception escape into the resuming asio handler (see AsioAwaitable.h);
        // the HandshakeSlotGuard still releases the admission slot on unwind
        HOST_LOG(ERROR) << LOG_DESC("server handshake exception")
                        << LOG_KV("endpoint", socket->nodeIPEndpoint())
                        << LOG_KV("what", boost::current_exception_diagnostic_information());
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

// FIB-186: release a previously reserved handshake slot. Runs from the HandshakeSlotGuard riding
// the serverHandshake coroutine frame, i.e. exactly once when that frame unwinds.
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
        if (m_asioInterface->acceptor() != nullptr)
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
    // fire-and-forget: the coroutine frame owns the connect/handshake chain (and the strong Host
    // reference) until it completes — the old nested-lambda chain did the same via captures.
    task::wait(clientConnect(std::move(socket), _nodeIPEndpoint, std::move(callback)));
}

task::Task<void> Host::clientConnect(std::shared_ptr<SocketFace> socket,
    NodeIPEndpoint _nodeIPEndpoint,
    std::function<void(NetworkException, P2PInfo const&, std::shared_ptr<SessionFace>)> callback)
{
    auto self = shared_from_this();
    try
    {
        /// if async connect timeout, close the socket directly
        auto connectTimer = std::make_shared<boost::asio::steady_timer>(
            socket->ioService(), std::chrono::milliseconds(m_connectTimeThre));
        connectTimer->async_wait(
            [this, socket, _nodeIPEndpoint](const boost::system::error_code& error) {
                /// return when cancel has been called
                if (error == boost::asio::error::operation_aborted)
                {
                    HOST_LOG(DEBUG)
                        << LOG_DESC("AsyncConnect handshake handler revoke this operation");
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
        auto [ec] = co_await m_asioInterface->awaitableResolveConnect(socket);
        if (ec)
        {
            HOST_LOG(ERROR) << LOG_DESC("TCP Connection refused by node")
                            << LOG_KV("endpoint", _nodeIPEndpoint)
                            << LOG_KV("message", ec.message());
            // Settle on the SOCKET's io_context: on RESOLVE failure this coroutine resumed on
            // the resolver's context (resolveConnect invokes the handler inline from the
            // resolver completion), while connectTimer's async_wait handler runs on the
            // socket's — close()/cancel() from here would race it ("Shared objects: Unsafe").
            boost::asio::post(socket->ioService(),
                [socket, connectTimer, callback = std::move(callback)]() mutable {
                    socket->close();
                    connectTimer->cancel();
                    callback(NetworkException(ConnectError, "Connect failed"), {}, {});
                });
            co_return;
        }
        insertPendingConns(_nodeIPEndpoint);
        /// get the public key of the server during handshake
        std::shared_ptr<std::string> endpointPublicKey = std::make_shared<std::string>();
        m_asioInterface->setVerifyCallback(socket, newVerifyCallback(endpointPublicKey));
        /// call handshakeClient after handshake succeed
        auto [handshakeError] =
            co_await m_asioInterface->awaitableHandshake(socket, ba::ssl::stream_base::client);
        connectTimer->cancel();
        // Pass COPIES of socket/callback, not moves: if handshakeClient itself throws, the
        // catch(...) below settles the operation through socket->close() and the callback —
        // both would be moved-from (null) here had they been moved into the call.
        handshakeClient(handshakeError, socket, endpointPublicKey, callback, _nodeIPEndpoint);
    }
    catch (...)
    {
        // never let an exception escape into the resuming asio handler (see AsioAwaitable.h)
        HOST_LOG(ERROR) << LOG_DESC("client connect exception")
                        << LOG_KV("endpoint", _nodeIPEndpoint)
                        << LOG_KV("what", boost::current_exception_diagnostic_information());
        // Total completion for the caller-facing callback: an exception between
        // insertPendingConns() and handshakeClient() (bad_alloc on endpointPublicKey,
        // setVerifyCallback, or an initiation failure rethrown by await_resume) would otherwise
        // leak the pending-connection entry — permanently blocking every future reconnect to
        // this peer — and orphan the caller's callback. Settle the operation exactly like the
        // error paths do: erase the entry, close the socket and answer the callback. The
        // socket teardown is POSTED to the socket's io_context — this catch is reachable on
        // the resolver's thread (see the resolve-failure branch above) as well as on a
        // producer's stack inside an await_suspend, and close() from here would race the
        // connect timer's handler ("Shared objects: Unsafe").
        erasePendingConns(_nodeIPEndpoint);
        boost::asio::post(socket->ioService(),
            [socket, callback = std::move(callback)]() mutable {
                socket->close();
                if (callback)
                {
                    callback(NetworkException(ConnectError, "Connect failed"), {}, {});
                }
            });
    }
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
    NodeIPEndpoint _nodeIPEndpoint)
{
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
    // Cancel the acceptor so the accept loop's pending async_accept completes with
    // operation_aborted: acceptLoop's coroutine frame holds a strong reference to this Host, and
    // only the completed accept lets the loop observe m_run == false and exit, releasing it.
    // Posted to the acceptor's io_context (asio objects are not thread-safe, and stop() runs off
    // the pool thread); the ASIOInterface copy keeps the acceptor alive until the cancel runs.
    if (auto asioInterface = m_asioInterface; asioInterface && asioInterface->acceptor() != nullptr)
    {
        try
        {
            // evaluate the executor BEFORE the move below: the evaluation order of post()'s
            // arguments is unspecified, so moving asioInterface into the lambda first would leave
            // a null shared_ptr for the acceptor() call
            auto executor = asioInterface->acceptor()->get_executor();
            boost::asio::post(executor,
                [asioInterface = std::move(asioInterface)]() { asioInterface->cancelAcceptor(); });
        }
        catch (...)
        {
            // stop() also runs from ~Host, which must not throw. A lost cancel is NOT
            // self-healing: the accept loop's coroutine frame holds this Host (and with it the
            // ASIOInterface and the acceptor) alive, so "acceptor destruction" can never end the
            // loop from the outside — only the NEXT completed accept lets the loop observe
            // m_run == false and exit. With no inbound connection, a Host whose cancel was lost
            // here stays alive until stop() is retried. The bounded wait below is what makes
            // this diagnosable instead of silent.
            HOST_LOG(WARNING) << LOG_DESC("cancel acceptor on stop failed")
                              << LOG_KV("what", boost::current_exception_diagnostic_information());
        }
    }
    // Bounded wait for the accept loop to exit (see the latch contract in Host.h): the loop's
    // frame holds a strong Host reference, so a lost cancel above — or an acceptor io_context
    // that was already stopped/drained before the post — would otherwise leak the whole Host
    // graph silently. After a successful cancel the loop exits within one event-loop turn (plus
    // at most one ACCEPT_RETRY_INTERVAL_MS backoff), so 10s is generous; a timeout means the
    // cancel never landed and this Host will outlive its teardown. Callers run stop() off the
    // pool threads (Service::stop / ~Host on the shutdown path), so waiting here cannot block
    // the acceptor's io_context thread the loop needs to exit.
    if (m_acceptLoopStarted.load(std::memory_order_acquire))
    {
        try
        {
            if (m_acceptLoopExit.get_future().wait_for(std::chrono::seconds(10)) !=
                std::future_status::ready)
            {
                HOST_LOG(ERROR) << LOG_DESC("accept loop did not exit within 10s of stop(); "
                                            "the posted cancel was likely lost and this Host "
                                            "(ASIOInterface, acceptor, teardown pool) may leak");
            }
        }
        catch (...)
        {
            HOST_LOG(WARNING) << LOG_DESC("accept loop exit wait failed")
                              << LOG_KV("what", boost::current_exception_diagnostic_information());
        }
    }
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
    // The accept loop's coroutine frame holds a strong Host reference, so reaching ~Host with
    // m_run still set means the loop was never started — a started-but-never-stopped Host simply
    // never gets here (see the stop() contract on the class comment). Flag the missing stop()
    // rather than letting it pass silently.
    if (m_run)
    {
        HOST_LOG(WARNING) << LOG_DESC("Host destroyed without stop()");
    }
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
