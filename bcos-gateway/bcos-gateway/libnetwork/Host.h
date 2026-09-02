/** @file Host.h
 * @author monan <651932351@qq.com>
 * @date 2018
 */
#pragma once

#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-framework/gateway/GatewayTypeDef.h"
#include "bcos-gateway/libnetwork/Common.h"
#include "bcos-gateway/libnetwork/Message.h"
#include "bcos-gateway/libnetwork/PeerBlackWhitelistInterface.h"
#include "bcos-gateway/libnetwork/SessionCallback.h"
#include "bcos-task/Task.h"
#include "bcos-utilities/Common.h"
#include <openssl/x509.h>
#include <boost/asio/ssl/stream_base.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/system/error_code.hpp>
#include <atomic>
#include <chrono>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>


namespace boost::asio::ssl
{
class verify_context;
}  // namespace boost::asio::ssl

namespace bcos
{
// FIB-186 (vector D): forward-declared so the dedicated teardown executor can be held by value-less
// pointer here without pulling boost/asio.hpp into every Host.h includer. Host.cpp includes the
// full definition to construct it.
class IOServicePool;
}  // namespace bcos

namespace bcos::gateway
{
class SessionFactory;
class SessionFace;
class SocketFace;
class ASIOInterface;

using x509PubHandler = std::function<bool(X509* x509, std::string& pubHex)>;

// Lifetime contract: a started Host must be stopped (stop()) BEFORE its last strong reference
// is released. The acceptLoop coroutine frame holds a strong Host reference for its whole life
// and exits only when stop() clears m_run and cancels the acceptor, so a Host that is started
// but never stopped is never destroyed — it keeps the ASIOInterface, the acceptor and the
// teardown executor alive with it. ~Host calls stop() defensively, but the destructor can only
// run once every strong reference — including the accept loop's — is already gone, so it cannot
// substitute for an explicit stop().
class Host : public std::enable_shared_from_this<Host>
{
public:
    Host(const Host&) = delete;
    Host(Host&&) = delete;
    Host& operator=(const Host&) = delete;
    Host& operator=(Host&&) = delete;
    Host(bcos::crypto::Hash::Ptr _hash, std::shared_ptr<ASIOInterface> _asioInterface,
        std::shared_ptr<SessionFactory> _sessionFactory, MessageFactory::Ptr _messageFactory);
    virtual ~Host();

    using Ptr = std::shared_ptr<Host>;

    virtual uint16_t listenPort() const;

    virtual void start();
    virtual void stop();

    virtual void asyncConnect(NodeIPEndpoint const& _nodeIPEndpoint,
        std::function<void(NetworkException, P2PInfo const&, std::shared_ptr<SessionFace>)>
            callback);

    virtual bool haveNetwork() const;

    virtual std::string listenHost() const;
    virtual void setHostPort(std::string host, uint16_t port);

    virtual std::function<void(NetworkException, P2PInfo const&, std::shared_ptr<SessionFace>)>
    connectionHandler() const;
    virtual void setConnectionHandler(
        std::function<void(NetworkException, P2PInfo const&, std::shared_ptr<SessionFace>)>
            connectionHandler);

    virtual std::function<bool(X509* x509, std::string& pubHex)> sslContextPubHandler();

    virtual void setSSLContextPubHandler(
        std::function<bool(X509* x509, std::string& pubHex)> _sslContextPubHandler);

    virtual std::function<bool(X509* x509, std::string& pubHex)>
    sslContextPubHandlerWithoutExtInfo();

    virtual void setSSLContextPubHandlerWithoutExtInfo(
        std::function<bool(X509* x509, std::string& pubHex)> _sslContextPubHandlerWithoutExtInfo);

    virtual void setSessionCallbackManager(
        SessionCallbackManagerInterface::Ptr sessionCallbackManager);

    virtual const std::shared_ptr<ASIOInterface>& asioInterface() const;
    virtual std::shared_ptr<SessionFactory> sessionFactory() const;
    virtual MessageFactory::Ptr messageFactory() const;
    virtual P2PInfo p2pInfo();

    virtual void setPeerBlacklist(PeerBlackWhitelistInterface::Ptr _peerBlacklist);
    virtual PeerBlackWhitelistInterface::Ptr peerBlacklist();
    virtual void setPeerWhitelist(PeerBlackWhitelistInterface::Ptr _peerWhitelist);
    virtual PeerBlackWhitelistInterface::Ptr peerWhitelist();

    // FIB-186 (vector D): run a session-teardown notification on the dedicated teardown executor
    // instead of the shared I/O pool. Teardown of established sessions (Service::onMessage's error
    // path -> onDisconnect -> onRemoveNodeIDs -> syncLatestNodeIDList) and inbound P2P message
    // delivery both ran on the same shared reactor, so a persistent bulk-disconnect flooded it and
    // starved inter-validator consensus-message delivery -- consensus halted and never recovered
    // (CertiK FIB-186 vector D). Keeping teardown on its own single-threaded executor keeps it off
    // the delivery reactor; the single thread also bounds teardown concurrency so a disconnect
    // flood cannot itself swamp the node.
    //
    // NOTE: this must stay a DEDICATED executor with its own thread. Serializing teardown onto a
    // bcos::Strand over the shared IOServicePool would not preserve the fix: a Strand only orders
    // tasks, it still dispatches them round-robin onto the shared pool's threads -- the very
    // threads that carry consensus-message delivery.
    void postTeardown(std::function<void()> f);

    void setEnableSslVerify(bool _enableSSLVerify);

    // FIB-184: caps on concurrent inbound sessions to bound memory under TLS connect/close
    // churn. With no cap, an authenticated peer can open sessions faster than they tear down
    // and exhaust the heap (each session allocates a recv buffer). GatewayFactory now plumbs the
    // configured values (p2p.max_concurrent_sessions / p2p.max_sessions_per_ip) via the setters
    // below; these constants are the defaults used when the config omits them.
    constexpr static std::size_t DEFAULT_MAX_CONCURRENT_SESSIONS = 1024;
    constexpr static std::size_t DEFAULT_MAX_SESSIONS_PER_IP = 32;

    std::size_t maxConcurrentSessions() const { return m_maxConcurrentSessions; }
    void setMaxConcurrentSessions(std::size_t _limit) { m_maxConcurrentSessions = _limit; }
    std::size_t maxSessionsPerIP() const { return m_maxSessionsPerIP; }
    void setMaxSessionsPerIP(std::size_t _limit) { m_maxSessionsPerIP = _limit; }
    std::size_t currentSessionCount() const { return m_sessionCount.load(); }

    // FIB-184: try to reserve a session slot for the given remote address. Returns true and
    // increments the global / per-IP counters if both caps allow; returns false (caller must
    // close the socket) otherwise. The matching releaseSessionSlot() runs from the session's
    // lifetime guard when the session object is destroyed. Public so the lifetime guard can
    // release the slot.
    bool tryAcquireSessionSlot(std::string const& _address);
    void releaseSessionSlot(std::string const& _address);

    // FIB-186: cap concurrent in-flight TLS handshakes (global). The FIB-184 session caps above
    // apply only AFTER the handshake completes, so they do not bound accept / handshake / teardown
    // work. Connection churn from a low-trust peer floods the shared I/O thread-pool with that work
    // and starves inter-validator PBFT reads, halting consensus without any node crash. Bounding
    // in-flight handshakes keeps the shared pool from saturating so consensus reads are not
    // starved. GatewayFactory plumbs the configured value (p2p.max_pending_handshakes); this
    // constant is the default when the config omits it.
    //
    // This is a GLOBAL cap, not per-IP: a per-IP cap only raises the bar from one source IP to a
    // handful (any real churn attacker rotates source addresses), while risking false rejections of
    // legitimate peers behind a shared egress IP (NAT / same datacenter). The global cap bounds the
    // aggregate handshake work regardless of how the attacker spreads it across source IPs.
    constexpr static std::size_t DEFAULT_MAX_PENDING_HANDSHAKES = 64;
    // FIB-186: default inbound TLS handshake timeout (ms). Without a timeout a stalled / slow
    // handshake never completes, so its admission slot is never released and (because the
    // serverHandshake coroutine frame holds shared_from_this) this Host can never be destroyed.
    // Bounding it lets a slow-loris peer hold at most this long before its slot is reclaimed.
    // GatewayFactory plumbs the configured value (p2p.handshake_timeout_ms); this constant is
    // the default when the config omits it.
    constexpr static int DEFAULT_HANDSHAKE_TIMEOUT_MS = 10000;

    std::size_t maxPendingHandshakes() const { return m_maxPendingHandshakes; }
    void setMaxPendingHandshakes(std::size_t _limit) { m_maxPendingHandshakes = _limit; }
    std::size_t currentPendingHandshakes() const
    {
        std::lock_guard<std::mutex> lock(x_pendingHandshakes);
        return m_pendingHandshakes;
    }
    int handshakeTimeout() const { return m_handshakeTimeout; }
    void setHandshakeTimeout(int _timeoutMs) { m_handshakeTimeout = _timeoutMs; }

    // FIB-186: reserve / release an in-flight-handshake slot. tryAcquire returns false (caller must
    // close the socket and re-arm accept) when the global cap is reached. release runs from the
    // HandshakeSlotGuard riding the serverHandshake coroutine frame, i.e. exactly once when that
    // frame unwinds (handshake success, failure, or abort).
    bool tryAcquireHandshakeSlot();
    void releaseHandshakeSlot();
    // FIB-186: reserve a slot AND return the owning RAII guard in one step (nullptr when the cap
    // is reached). The guard rides the caller's coroutine frame, so acquire and release live in
    // the same frame — and the "throw between acquire and guard" window cannot leak a slot.
    std::shared_ptr<void> acquireHandshakeSlotGuard();

    // FIB-186: rate-limit accepted new connections (not just their concurrency). The pending-
    // handshake cap above bounds how many handshakes run at once, but on a fast link a churn flood
    // still cycles through the slots and consumes unbounded handshake CPU (asymmetric crypto). A
    // token bucket refilled at m_maxConnectionsPerSecond bounds the accept RATE: over the limit the
    // connection is dropped before the (CPU-heavy) TLS handshake. 0 = unlimited.
    //
    // Why the rate + concurrency caps are the fix, and NOT a dedicated acceptor thread-pool: a
    // boost::asio SSL stream is welded to its io_context for life, so a connection's TLS handshake
    // and the reads of the session that follows run on the SAME pool. An inbound churn connection
    // and an inbound validator connection are indistinguishable at accept time (identity is only
    // known after the handshake), so no pool assignment can separate "attacker handshake" from
    // "validator consensus reads" -- they always land on the same pool. Splitting the acceptor onto
    // its own pool only moves WHICH thread the handshake CPU lands on; it does not remove it from a
    // thread that also carries consensus reads. Thread isolation is not CPU isolation: once that
    // thread is saturated by handshake crypto the consensus reads sharing it are starved and
    // consensus halts anyway -- a pool split merely delays the onset. The only effective lever is
    // to stop the expensive handshake from running at all: reject churn cheaply BEFORE the
    // handshake. Keeping this rate cap (and the concurrency cap) small enough that the admitted
    // handshake CPU cannot saturate the pool is what actually prevents the halt; an acceptor pool
    // would add code and config for no measurable protection (validated on a 3-node churn harness).
    constexpr static uint32_t DEFAULT_MAX_CONNECTIONS_PER_SECOND = 100;
    uint32_t maxConnectionsPerSecond() const { return m_maxConnectionsPerSecond; }
    // NOTE: this and the other cap setters are called by GatewayFactory during construction, before
    // Host::start(), so they need no locking and tryAcquireConnectionToken()'s lock-free read of
    // m_maxConnectionsPerSecond is race-free.
    void setMaxConnectionsPerSecond(uint32_t _limit)
    {
        m_maxConnectionsPerSecond = _limit;
        // Keep the token bucket consistent with the configured rate: m_connectionTokens is default-
        // initialized to the compile-time default, so without this a non-default rate would start
        // with the wrong burst capacity until the first refill (~1s).
        m_connectionTokens = static_cast<double>(_limit);
    }
    bool tryAcquireConnectionToken();

    // Retry delay after a failed accept-loop iteration (ms): the per-iteration catch in
    // acceptLoop re-arms a short timer before retrying, so a persistently failing newSocket()
    // (e.g. fd exhaustion) degrades to a slow retry loop instead of spinning the acceptor's
    // io_context thread at 100% CPU.
    constexpr static uint32_t ACCEPT_RETRY_INTERVAL_MS = 200;

protected:
    /// obtain the common name from the subject:
    /// the subject format is: /CN=xx/O=xxx/OU=xxx/ commonly
    std::string obtainCommonNameFromSubject(std::string const& subject);

    /// called by 'startedWorking' to accept connections
    void startAccept(boost::system::error_code error = boost::system::error_code());
    /// functions called after openssl handshake,
    /// maily to get node id and verify whether the certificate has been expired
    /// @return: node id of the connected peer
    std::function<bool(bool, boost::asio::ssl::verify_context&)> newVerifyCallback(
        std::shared_ptr<std::string> nodeIDOut);

    /// obtain nodeInfo from given vector
    void obtainNodeInfo(P2PInfo& info, std::string const& node_info);

    /// server calls handshakeServer to after handshake, mainly calls
    /// RLPxHandshake to obtain informations(client version, caps, etc),start peer
    /// session and start accepting procedure repeatedly
    void handshakeServer(const boost::system::error_code& error,
        std::shared_ptr<std::string> endpointPublicKey, std::shared_ptr<SocketFace> socket);

    void startPeerSession(P2PInfo const& p2pInfo, std::shared_ptr<SocketFace> const& socket,
        std::function<void(NetworkException, P2PInfo const&, std::shared_ptr<SessionFace>)>
            handler);

    void handshakeClient(const boost::system::error_code& error, std::shared_ptr<SocketFace> socket,
        std::shared_ptr<std::string> endpointPublicKey,
        std::function<void(NetworkException, P2PInfo const&, std::shared_ptr<SessionFace>)>
            callback,
        NodeIPEndpoint _nodeIPEndpoint);

    void erasePendingConns(NodeIPEndpoint const& nodeIPEndpoint);

    void insertPendingConns(NodeIPEndpoint const& nodeIPEndpoint);

private:
    // Coroutine bodies for the accept/connect paths, launched fire-and-forget (task::wait) from
    // startAccept()/asyncConnect(). Each frame holds a strong Host reference for its whole
    // lifetime — structurally replacing the per-operation shared_from_this() captures of the old
    // completion handlers. acceptLoop additionally keeps the Host alive until Host::stop()
    // cancels the acceptor, which completes the pending async_accept with operation_aborted and
    // lets the loop exit.
    task::Task<void> acceptLoop();
    // handshakeGuard owns the reserved FIB-186 in-flight-handshake admission slot (acquired in
    // acceptLoop so the slot/token admission ordering is preserved); it is released exactly when
    // this coroutine frame unwinds. Held as shared_ptr<void> to keep the concrete guard type an
    // implementation detail of Host.cpp.
    task::Task<void> serverHandshake(
        std::shared_ptr<SocketFace> socket, std::shared_ptr<void> handshakeGuard);
    task::Task<void> clientConnect(std::shared_ptr<SocketFace> socket,
        NodeIPEndpoint _nodeIPEndpoint,
        std::function<void(NetworkException, P2PInfo const&, std::shared_ptr<SessionFace>)>
            callback);

protected:
    // FIB-186 (vector D): dedicated single-thread executor for session-teardown notifications, kept
    // separate from the shared IOServicePool so a bulk-disconnect flood cannot starve
    // inbound-message delivery. See postTeardown.
    //
    // Owned by the Host (rather than injected like the shared pool) because its whole purpose is to
    // NOT be the shared pool. ~IOServicePool stops its io_context and joins its thread, which is
    // exactly what the ThreadPool::stop() this replaces did, so the drain guarantee is unchanged;
    // Session::drop() stops posting here once haveNetwork() goes false, so nothing is enqueued
    // after Host::stop().
    std::shared_ptr<bcos::IOServicePool> m_teardownPool;
    std::shared_ptr<SessionCallbackManagerInterface> m_sessionCallbackManager;

    bcos::crypto::Hash::Ptr m_hashImpl;
    /// representing to the network state
    std::shared_ptr<ASIOInterface> m_asioInterface;
    std::shared_ptr<SessionFactory> m_sessionFactory;
    int m_connectTimeThre = 50000;

    std::set<NodeIPEndpoint> m_pendingConns;
    bcos::Mutex x_pendingConns;
    MessageFactory::Ptr m_messageFactory;

    std::string m_listenHost;
    uint16_t m_listenPort = 0;
    // enable ssl verify or not
    bool m_enableSSLVerify = true;

    std::function<void(NetworkException, P2PInfo const&, std::shared_ptr<SessionFace>)>
        m_connectionHandler;

    // get the hex public key of the peer from the the SSL connection
    std::function<bool(X509* x509, std::string& pubHex)> m_sslContextPubHandler;
    std::function<bool(X509* x509, std::string& pubHex)> m_sslContextPubHandlerWithoutExtInfo;

    // Network run flag. Written by start()/stop() from the caller's thread, read as the accept
    // loop's condition on the acceptor's io_context thread (Host::acceptLoop) and by
    // haveNetwork() from every session thread. Must be atomic: the new Host contract makes
    // observing m_run == false the only mechanism that releases the accept loop's strong Host
    // reference, so a torn/stale read would re-arm async_accept after the cancel was consumed
    // and make the Host immortal.
    std::atomic<bool> m_run{false};

    // Accept-loop exit latch (see Host::stop). The loop's frame holds a strong Host reference,
    // forming a reference cycle (frame -> Host -> ASIOInterface -> IOServicePool -> io_context ->
    // pending async_accept -> the frame) whose only cut point is the cancel Host::stop() posts
    // to the acceptor's io_context — if that cancel is lost (the io_context was already stopped
    // or drained, or the post threw), nothing else ends the loop and the whole graph leaks
    // silently. stop() therefore waits on this latch with a bounded timeout and logs loudly on
    // expiry, turning the silent permanent leak into a bounded wait plus a diagnosable line.
    // m_acceptLoopStarted gates the wait (a Host whose loop never ran has nothing to wait for);
    // the promise is satisfied when acceptLoop returns. The frame-destroy rescue path never
    // satisfies it, which is exactly the case the timeout exists to diagnose.
    std::atomic<bool> m_acceptLoopStarted{false};
    std::promise<void> m_acceptLoopExit;

    P2PInfo m_p2pInfo;

    // Peer black list
    PeerBlackWhitelistInterface::Ptr m_peerBlacklist{nullptr};
    PeerBlackWhitelistInterface::Ptr m_peerWhitelist{nullptr};

    // FIB-184: session-cap accounting.
    std::size_t m_maxConcurrentSessions{DEFAULT_MAX_CONCURRENT_SESSIONS};
    std::size_t m_maxSessionsPerIP{DEFAULT_MAX_SESSIONS_PER_IP};
    std::atomic<std::size_t> m_sessionCount{0};
    std::map<std::string, std::size_t> m_sessionCountPerIP;
    std::mutex x_sessionCountPerIP;

    // FIB-186: in-flight-handshake accounting (dedicated mutex, separate from the session-cap
    // path). Global count only -- see the comment on DEFAULT_MAX_PENDING_HANDSHAKES for why there
    // is no per-IP cap.
    std::size_t m_maxPendingHandshakes{DEFAULT_MAX_PENDING_HANDSHAKES};
    // Plain counter, not atomic: every access is under x_pendingHandshakes (the diagnostic getter
    // currentPendingHandshakes() takes the mutex too), so an atomic would be redundant.
    std::size_t m_pendingHandshakes{0};
    mutable std::mutex x_pendingHandshakes;
    int m_handshakeTimeout{DEFAULT_HANDSHAKE_TIMEOUT_MS};

    // FIB-186: token bucket for the new-connection accept rate (dedicated mutex). Starts full so a
    // freshly-started node accepts an initial burst rather than rejecting the first connections.
    uint32_t m_maxConnectionsPerSecond{DEFAULT_MAX_CONNECTIONS_PER_SECOND};
    double m_connectionTokens{static_cast<double>(DEFAULT_MAX_CONNECTIONS_PER_SECOND)};
    std::chrono::steady_clock::time_point m_lastTokenRefill{std::chrono::steady_clock::now()};
    std::mutex x_connectionRate;
};
}  // namespace bcos::gateway
