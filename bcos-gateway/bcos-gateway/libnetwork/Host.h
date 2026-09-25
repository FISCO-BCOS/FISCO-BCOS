/** @file Host.h
 * @author monan <651932351@qq.com>
 * @date 2018
 */
#pragma once

#include "bcos-framework/gateway/GatewayTypeDef.h"
#include "bcos-gateway/libnetwork/Common.h"
#include "bcos-gateway/libnetwork/PeerIdentity.h"
#include "bcos-gateway/libnetwork/Session.h"
#include "bcos-gateway/libnetwork/SessionCallback.h"
#include "bcos-gateway/libnetwork/Socket.h"
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
#include <tuple>


namespace boost::asio::ssl
{
class verify_context;
}  // namespace boost::asio::ssl

namespace bcos
{
// FIB-186 (vector D): forward-declared so the class declaration can hold the dedicated teardown
// executor by pointer without naming the concrete type. The full IOServicePool definition is
// included together with the member definitions at the bottom of this header, which is where the
// executor is constructed.
class IOServicePool;
}  // namespace bcos

namespace bcos::gateway
{
class ASIOInterface;

// Lifetime contract: a started Host must be stopped (stop()) BEFORE its last strong reference
// is released. The acceptLoop coroutine frame holds a strong Host reference for its whole life
// and exits only when stop() clears m_run and cancels the acceptor, so a Host that is started
// but never stopped is never destroyed — it keeps the ASIOInterface, the acceptor and the
// teardown executor alive with it. ~Host calls stop() defensively, but the destructor can only
// run once every strong reference — including the accept loop's — is already gone, so it cannot
// substitute for an explicit stop().
//
// DecoderT: the wire-format decoder of the sessions this host creates (see FrameMeta.h);
// SocketT: the socket type of those sessions (production: Socket, the default — see the forward
// declaration in Session.h; tests instantiate fakes). The gateway instantiates Host<P2PDecoder>.
// WHO a peer is (certificate → node identity, admission) is not Host's business either — it is
// injected as a PeerIdentity (libnetwork/PeerIdentity.h).
// Member definitions follow at the bottom of this header (self-contained template header).
template <FrameDecoder DecoderT, typename SocketT>
class Host : public std::enable_shared_from_this<Host<DecoderT, SocketT>>
{
public:
    Host(const Host&) = delete;
    Host(Host&&) = delete;
    Host& operator=(const Host&) = delete;
    Host& operator=(Host&&) = delete;
    Host(std::shared_ptr<ASIOInterface> _asioInterface,
        std::shared_ptr<BasicSessionFactory<DecoderT, SocketT>> _sessionFactory);
    ~Host();

    using Ptr = std::shared_ptr<Host>;
    using SessionType = BasicSession<DecoderT, SocketT>;

    uint16_t listenPort() const;

    void start();
    void stop();

    /**
     * @brief: (coroutine) connect to the server
     * @param _nodeIPEndpoint : the endpoint of the connected server
     * @return {error, peerIdentity, session}: on success the error code is 0 and the session is
     *         the established peer session; on failure the session is nullptr and the error
     *         describes the failure. peerIdentity is the opaque IdentityToken filled by the
     *         injected PeerIdentity during the TLS handshake. A skipped connect (host not
     *         running, or the endpoint is already in the pending list) returns a success error
     *         with a nullptr session.
     * @note the caller must keep this Host alive until the returned task completes (e.g. own a
     *       shared_ptr in the awaiting coroutine frame); on success the task resumes on the
     *       connected socket's io_context thread, on failure it may resume on the resolver's
     *       thread or complete synchronously on the caller's thread.
     */
    task::Task<std::tuple<NetworkException, IdentityToken, std::shared_ptr<SessionType>>> connect(
        NodeIPEndpoint _nodeIPEndpoint);

    bool haveNetwork() const;

    std::string listenHost() const;
    void setHostPort(std::string host, uint16_t port);

    std::function<void(NetworkException, IdentityToken const&, std::shared_ptr<SessionType>)>
    connectionHandler() const;
    void setConnectionHandler(
        std::function<void(NetworkException, IdentityToken const&, std::shared_ptr<SessionType>)>
            connectionHandler);

    // The identity seam (libnetwork/PeerIdentity.h): certificate → node-identity extraction and
    // admission policy, injected by the factory (the gateway uses libp2p::P2PPeerIdentity)
    // before start(). Fail-closed: a handshake with no identity wired in is rejected.
    void setPeerIdentity(std::shared_ptr<PeerIdentity> _identity) { m_identity = std::move(_identity); }

    // host-wide response-callback manager shared by every session this host creates
    SessionCallbackManager<SessionType>& sessionCallbackManager();

    const std::shared_ptr<ASIOInterface>& asioInterface() const;
    std::shared_ptr<BasicSessionFactory<DecoderT, SocketT>> sessionFactory() const;
    // Host-wide message seq allocator. The response-callback manager is shared by every session
    // of this host (and a routed response can be claimed on a different session than the request
    // went out on), so seqs must be unique host-wide, not per-connection.
    uint32_t newSeq();

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
    /// called by 'startedWorking' to accept connections
    void startAccept(boost::system::error_code error = boost::system::error_code());
    /// functions called after openssl handshake,
    /// maily to get node id and verify whether the certificate has been expired
    /// @param identitySlot: per-handshake opaque slot filled by the injected PeerIdentity with
    ///        the connected peer's identity
    std::function<bool(bool, boost::asio::ssl::verify_context&)> newVerifyCallback(
        IdentityToken identitySlot);

    /// server calls handshakeServer to after handshake, mainly calls
    /// RLPxHandshake to obtain informations(client version, caps, etc),start peer
    /// session and start accepting procedure repeatedly
    /// @param peerIdentity: the opaque identity token filled during the TLS handshake
    void handshakeServer(const boost::system::error_code& error,
        IdentityToken peerIdentity, std::shared_ptr<SocketT> socket);

    std::shared_ptr<SessionType> startPeerSession(std::shared_ptr<SocketT> const& socket);

    std::tuple<NetworkException, IdentityToken, std::shared_ptr<SessionType>> handshakeClient(
        const boost::system::error_code& error, std::shared_ptr<SocketT> socket,
        IdentityToken peerIdentity, NodeIPEndpoint _nodeIPEndpoint);

    void erasePendingConns(NodeIPEndpoint const& nodeIPEndpoint);

    void insertPendingConns(NodeIPEndpoint const& nodeIPEndpoint);

private:
    // Coroutine bodies for the accept/connect paths. The accept path is launched fire-and-forget
    // (task::wait) from startAccept(); the connect path is co_awaited by connect()'s caller. Each
    // frame holds a strong Host reference for its whole lifetime — structurally replacing the
    // per-operation shared_from_this() captures of the old completion handlers. acceptLoop
    // additionally keeps the Host alive until Host::stop() cancels the acceptor, which completes
    // the pending async_accept with operation_aborted and lets the loop exit.
    task::Task<void> acceptLoop();
    // handshakeGuard owns the reserved FIB-186 in-flight-handshake admission slot (acquired in
    // acceptLoop so the slot/token admission ordering is preserved); it is released exactly when
    // this coroutine frame unwinds. Held as shared_ptr<void> to keep the concrete guard type an
    // implementation detail of the member definitions below.
    task::Task<void> serverHandshake(
        std::shared_ptr<SocketT> socket, std::shared_ptr<void> handshakeGuard);
    task::Task<std::tuple<NetworkException, IdentityToken, std::shared_ptr<SessionType>>> clientConnect(
        std::shared_ptr<SocketT> socket, NodeIPEndpoint _nodeIPEndpoint);

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
    // Host-wide response-callback manager, owned here and shared by every session this host
    // creates (sessions hold a non-owning pointer to it).
    SessionCallbackManager<SessionType> m_sessionCallbackManager;

    // The injected identity seam (libnetwork/PeerIdentity.h). Null until the factory wires it;
    // the handshake paths fail closed (reject) when it is missing.
    std::shared_ptr<PeerIdentity> m_identity;

    /// representing to the network state
    std::shared_ptr<ASIOInterface> m_asioInterface;
    std::shared_ptr<BasicSessionFactory<DecoderT, SocketT>> m_sessionFactory;
    int m_connectTimeThre = 50000;

    std::set<NodeIPEndpoint> m_pendingConns;
    bcos::Mutex x_pendingConns;
    // host-wide seq source, see newSeq()
    std::atomic<uint32_t> m_seq{1};

    std::string m_listenHost;
    uint16_t m_listenPort = 0;
    // enable ssl verify or not
    bool m_enableSSLVerify = true;

    std::function<void(NetworkException, IdentityToken const&, std::shared_ptr<SessionType>)>
        m_connectionHandler;

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

// ---------------------------------------------------------------------------
// Member definitions of the Host<DecoderT, SocketT> template (declared above). Template
// definitions must be visible at every instantiation point, so they live in this header, below
// the class declaration; the includes they need are kept here at the point of use.
#include "bcos-gateway/libnetwork/ASIOInterface.h"
#include "bcos-gateway/libnetwork/Common.h"
#include "bcos-gateway/libnetwork/Session.h"
#include "bcos-utilities/BoostLog.h"
#include "bcos-utilities/IOServicePool.h"
#include <bcos-task/Wait.h>
#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <set>
#include <utility>

namespace bcos::gateway
{
namespace detail
{
// FIB-186: RAII guard bound to an in-flight TLS handshake. Constructed after a handshake slot is
// acquired; its destructor releases the slot exactly once when the frame it rides in unwinds
// (success / failure / abort) or is destroyed. Held via weak_ptr so a Host torn down before the
// handshake completes does not crash the guard.
template <typename HostT>
struct HandshakeSlotGuard
{
    std::weak_ptr<HostT> host;
    explicit HandshakeSlotGuard(std::weak_ptr<HostT> _host) : host(std::move(_host)) {}
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
template <typename HostT>
std::shared_ptr<void> tryAcquireHandshakeSlotGuard(std::weak_ptr<HostT> host)
{
    auto hostPtr = host.lock();
    if (!hostPtr || !hostPtr->tryAcquireHandshakeSlot())
    {
        return nullptr;
    }
    try
    {
        return std::make_shared<HandshakeSlotGuard<HostT>>(std::move(host));
    }
    catch (...)
    {
        // guard construction failed (allocation): release the just-acquired slot so it is not
        // leaked, then rethrow so the accept-loop iteration is skipped
        hostPtr->releaseHandshakeSlot();
        throw;
    }
}

// FIB-184: RAII guard bound to a session's lifetime. Constructed after a slot is acquired and
// attached to the session via setLifetimeGuard(); its destructor (running in ~Session) releases
// the slot. Held via weak_ptr so a Host destroyed before the session does not crash the guard.
template <typename HostT>
struct SessionSlotGuard
{
    std::weak_ptr<HostT> host;
    std::string address;
    SessionSlotGuard(std::weak_ptr<HostT> _host, std::string _address)
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
}  // namespace detail

// FIB-186: member wrapper over the detail factory above, so acceptLoop and tests drive the exact
// same acquire-and-guard path.
template <FrameDecoder DecoderT, typename SocketT>
std::shared_ptr<void> Host<DecoderT, SocketT>::acquireHandshakeSlotGuard()
{
    return detail::tryAcquireHandshakeSlotGuard(this->weak_from_this());
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
template <FrameDecoder DecoderT, typename SocketT>
void Host<DecoderT, SocketT>::startAccept(boost::system::error_code /*boost_error*/)
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

template <FrameDecoder DecoderT, typename SocketT>
task::Task<void> Host<DecoderT, SocketT>::acceptLoop()
{
    // The frame holds the Host alive for the whole accept loop. This deliberately extends the old
    // lifetime (the accept handler held a raw this): the loop only exits after Host::stop()
    // cancels the acceptor, so a pending async_accept can never fire on a destroyed Host.
    auto self = this->shared_from_this();
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
            auto socket = m_asioInterface->newSocket<SocketT>(true, NodeIPEndpoint());
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
                auto handshakeGuard =
                    detail::tryAcquireHandshakeSlotGuard(this->weak_from_this());
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
            // never let an exception escape into the resuming asio handler (see FireAwaitable.h);
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
                co_await task::makeFireAwaitable<boost::system::error_code>(
                    [&retryTimer](auto handler) {
                        retryTimer.async_wait(std::move(handler));
                    },
                    boost::asio::error::operation_aborted);
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

template <FrameDecoder DecoderT, typename SocketT>
task::Task<void> Host<DecoderT, SocketT>::serverHandshake(
    std::shared_ptr<SocketT> socket, std::shared_ptr<void> handshakeGuard)
{
    auto self = this->shared_from_this();
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
        /// register ssl callback to get the NodeID of peers; the identity slot is created and
        /// filled by the injected PeerIdentity — Host carries it opaquely
        IdentityToken peerIdentity = m_identity ? m_identity->newIdentitySlot() : nullptr;
        m_asioInterface->setVerifyCallback(socket, newVerifyCallback(peerIdentity));
        auto [handshakeError] =
            co_await m_asioInterface->awaitableHandshake(socket, ba::ssl::stream_base::server);
        handshakeTimer->cancel();
        handshakeServer(handshakeError, peerIdentity, socket);
    }
    catch (...)
    {
        // never let an exception escape into the resuming asio handler (see FireAwaitable.h);
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
 * @param identitySlot : per-handshake opaque slot filled by the injected PeerIdentity with the
 *  peer's identity
 * @return std::function<bool(bool, boost::asio::ssl::verify_context&)>:
 *  return true: verify success
 *  return false: verify failed
 */
template <FrameDecoder DecoderT, typename SocketT>
std::function<bool(bool, boost::asio::ssl::verify_context&)> Host<DecoderT, SocketT>::newVerifyCallback(
    IdentityToken identitySlot)
{
    auto host = std::weak_ptr<Host>(this->shared_from_this());
    return [host, identitySlot = std::move(identitySlot)](
               bool preverified, boost::asio::ssl::verify_context& ctx) {
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
            // WHO the peer is — identity extraction and admission policy — is the injected
            // PeerIdentity's business (the gateway wires in libp2p/P2PIdentity.h); libnetwork
            // only runs the TLS plumbing and the generic chain policy. Fail closed when the
            // seam was never wired.
            if (!hostPtr->m_identity || !identitySlot)
            {
                HOST_LOG(ERROR) << LOG_DESC("No peer identity set, reject connection");
                return false;
            }
            if (hostPtr->m_identity->verifyPeer(cert, identitySlot) ==
                PeerIdentity::Verdict::Reject)
            {
                return false;
            }
            ////  always return true when disable ssl, return preverified when enable ssl ///
            return preverified || !hostPtr->m_enableSSLVerify;
        }
        catch (std::exception& e)
        {
            HOST_LOG(ERROR) << LOG_DESC("Cert verify failed") << boost::diagnostic_information(e);
            return preverified;
        }
    };
}

/**
 * @brief: server calls handshakeServer to after handshake
 *         mainly calls RLPxHandshake to obtain informations(client version,
 * caps, etc), start peer session and start accepting procedure repeatedly
 * @param error: error information triggered in the procedure of ssl handshake
 * @param peerIdentity: opaque identity token filled by the injected PeerIdentity during the
 * handshake
 * @param socket: socket related to the endpoint of the connected client
 */
template <FrameDecoder DecoderT, typename SocketT>
void Host<DecoderT, SocketT>::handshakeServer(const boost::system::error_code& error,
    IdentityToken peerIdentity, std::shared_ptr<SocketT> socket)
{
    if (error)
    {
        HOST_LOG(INFO) << LOG_DESC("handshakeServer Handshake failed")
                       << LOG_KV("value", error.value()) << LOG_KV("message", error.message())
                       << LOG_KV("endpoint", socket->nodeIPEndpoint());
        socket->close();
        return;
    }
    if (m_run)
    {
        // NOTE: Host cannot tell whether the handshake actually produced a peer identity —
        // the token is opaque. A handshake that extracted none (every certificate skipped by
        // the PeerIdentity) is dropped downstream by the connection handler (Service::onConnect
        // rejects an empty identity).
        HOST_LOG(INFO) << LOG_DESC("handshakeServer succ")
                       << LOG_KV("remote endpoint", socket->remoteEndpoint());
        auto session = startPeerSession(socket);
        if (!session)
        {
            return;
        }
        // inbound (accepted) connections have no awaiting connect() coroutine to receive the
        // session, so deliver it to the registered connection handler — posted to the socket's
        // io_context so the handler runs off the accept path
        auto weakHost = this->weak_from_this();
        boost::asio::post(socket->ioService(),
            [weakHost, session = std::move(session), peerIdentity = std::move(peerIdentity)]() {
                auto host = weakHost.lock();
                if (!host)
                {
                    return;
                }
                if (host->m_connectionHandler)
                {
                    host->m_connectionHandler(NetworkException{}, peerIdentity, session);
                }
                else
                {
                    HOST_LOG(WARNING) << LOG_DESC("No connectionHandler, new connection may lost");
                }
            });
    }
}

// FIB-184: reserve a session slot under the global and per-IP caps. Returns false when either
// cap is reached; the caller must then close the socket without creating a session.
template <FrameDecoder DecoderT, typename SocketT>
bool Host<DecoderT, SocketT>::tryAcquireSessionSlot(std::string const& _address)
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
template <FrameDecoder DecoderT, typename SocketT>
void Host<DecoderT, SocketT>::releaseSessionSlot(std::string const& _address)
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
template <FrameDecoder DecoderT, typename SocketT>
bool Host<DecoderT, SocketT>::tryAcquireHandshakeSlot()
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
template <FrameDecoder DecoderT, typename SocketT>
void Host<DecoderT, SocketT>::releaseHandshakeSlot()
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
template <FrameDecoder DecoderT, typename SocketT>
bool Host<DecoderT, SocketT>::tryAcquireConnectionToken()
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

/**
 * @brief: start a peer session after the handshake succeeded: enforce the FIB-184 session
 *         caps, create the session via the factory and bind its slot-release guard
 * @param socket : connected socket(used to init session object)
 */
template <FrameDecoder DecoderT, typename SocketT>
std::shared_ptr<typename Host<DecoderT, SocketT>::SessionType> Host<DecoderT, SocketT>::startPeerSession(
    std::shared_ptr<SocketT> const& socket)
{
    auto weakHost = this->weak_from_this();

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
        return nullptr;
    }

    std::shared_ptr<SessionType> session = m_sessionFactory->createSession(*this, socket);
    // Bind a slot-release guard to the session; the slot is freed when the session is destroyed.
    session->setLifetimeGuard(
        std::make_shared<detail::SessionSlotGuard<Host>>(weakHost, remoteAddress));

    HOST_LOG(INFO) << LOG_DESC("startPeerSession, Remote=") << socket->remoteEndpoint()
                   << LOG_KV("local endpoint", socket->localEndpoint());
    return session;
}

/**
 * @brief: remove expired timer
 *         modify alived peers to m_peers
 *         reconnect all nodes recorded in m_staticNodes periodically
 */
template <FrameDecoder DecoderT, typename SocketT>
void Host<DecoderT, SocketT>::start()
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
template <FrameDecoder DecoderT, typename SocketT>
task::Task<std::tuple<NetworkException, IdentityToken, std::shared_ptr<typename Host<DecoderT, SocketT>::SessionType>>>
Host<DecoderT, SocketT>::connect(NodeIPEndpoint _nodeIPEndpoint)
{
    if (!m_run)
    {
        co_return std::make_tuple(
            NetworkException{}, IdentityToken(), std::shared_ptr<SessionType>());
    }
    HOST_LOG(INFO) << LOG_DESC("Connecting to node") << LOG_KV("endpoint", _nodeIPEndpoint);
    {
        Guard l(x_pendingConns);
        auto it = m_pendingConns.find(_nodeIPEndpoint);
        if (it != m_pendingConns.end())
        {
            BCOS_LOG(TRACE) << LOG_DESC("connected node is in the pending list")
                            << LOG_KV("endpoint", _nodeIPEndpoint);
            co_return std::make_tuple(
                NetworkException{}, IdentityToken(), std::shared_ptr<SessionType>());
        }
    }

    auto socket = m_asioInterface->newSocket<SocketT>(false, _nodeIPEndpoint);
    co_return co_await clientConnect(std::move(socket), std::move(_nodeIPEndpoint));
}

template <FrameDecoder DecoderT, typename SocketT>
task::Task<std::tuple<NetworkException, IdentityToken, std::shared_ptr<typename Host<DecoderT, SocketT>::SessionType>>>
Host<DecoderT, SocketT>::clientConnect(std::shared_ptr<SocketT> socket, NodeIPEndpoint _nodeIPEndpoint)
{
    auto self = this->shared_from_this();
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
            boost::asio::post(socket->ioService(), [socket, connectTimer]() {
                socket->close();
                connectTimer->cancel();
            });
            co_return std::make_tuple(makeNetworkException(ConnectError, "Connect failed"),
                IdentityToken(), std::shared_ptr<SessionType>());
        }
        insertPendingConns(_nodeIPEndpoint);
        /// get the public key of the server during handshake; the identity slot is created and
        /// filled by the injected PeerIdentity — Host carries it opaquely
        IdentityToken peerIdentity = m_identity ? m_identity->newIdentitySlot() : nullptr;
        m_asioInterface->setVerifyCallback(socket, newVerifyCallback(peerIdentity));
        /// call handshakeClient after handshake succeed
        auto [handshakeError] =
            co_await m_asioInterface->awaitableHandshake(socket, ba::ssl::stream_base::client);
        connectTimer->cancel();
        // Pass COPIES of socket/_nodeIPEndpoint, not moves: if handshakeClient itself throws, the
        // catch(...) below settles the operation through socket->close() and
        // erasePendingConns(_nodeIPEndpoint) — both would be moved-from here had they been moved
        // into the call.
        co_return handshakeClient(
            handshakeError, socket, std::move(peerIdentity), _nodeIPEndpoint);
    }
    catch (...)
    {
        // never let an exception escape into the resuming asio handler (see FireAwaitable.h)
        HOST_LOG(ERROR) << LOG_DESC("client connect exception")
                        << LOG_KV("endpoint", _nodeIPEndpoint)
                        << LOG_KV("what", boost::current_exception_diagnostic_information());
        // Total completion for the awaiting caller: an exception between insertPendingConns() and
        // handshakeClient() (bad_alloc on peerIdentity, setVerifyCallback, or an initiation
        // failure rethrown by await_resume) would otherwise leak the pending-connection entry —
        // permanently blocking every future reconnect to this peer — and orphan the caller's
        // co_await. Settle the operation exactly like the error paths do: erase the entry, close
        // the socket and return the error. The socket teardown is POSTED to the socket's
        // io_context — this catch is reachable on the resolver's thread (see the resolve-failure
        // branch above) as well as on a producer's stack inside an await_suspend, and close()
        // from here would race the connect timer's handler ("Shared objects: Unsafe").
        erasePendingConns(_nodeIPEndpoint);
        boost::asio::post(socket->ioService(), [socket]() { socket->close(); });
        co_return std::make_tuple(makeNetworkException(ConnectError, "Connect failed"),
            IdentityToken(), std::shared_ptr<SessionType>());
    }
}

/**
 * @brief : start RLPxHandshake procedure after ssl handshake succeed
 * @param error: error returned by ssl handshake
 * @param socket : ssl socket
 * @param peerIdentity: opaque identity token filled by the injected PeerIdentity from the
 * server's certificate during the handshake
 * @param _nodeIPEndpoint : endpoint of the server to connect
 */
template <FrameDecoder DecoderT, typename SocketT>
std::tuple<NetworkException, IdentityToken, std::shared_ptr<typename Host<DecoderT, SocketT>::SessionType>>
Host<DecoderT, SocketT>::handshakeClient(const boost::system::error_code& error,
    std::shared_ptr<SocketT> socket, IdentityToken peerIdentity,
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
        return std::make_tuple(makeNetworkException(ConnectError, "Handshake failed"), IdentityToken{},
            std::shared_ptr<SessionType>());
    }

    // NOTE: Host cannot tell whether the handshake actually produced a peer identity — the
    // token is opaque. A handshake that extracted none (every certificate skipped by the
    // PeerIdentity) is dropped downstream by the caller (Service::onConnect rejects an empty
    // identity).
    if (m_run)
    {
        HOST_LOG(INFO) << LOG_DESC("handshakeClient succ")
                       << LOG_KV("local endpoint", socket->localEndpoint());
        auto session = startPeerSession(socket);
        if (!session)
        {
            return std::make_tuple(makeNetworkException(ConnectError, "Session cap reached"),
                IdentityToken{}, std::shared_ptr<SessionType>());
        }
        return std::make_tuple(
            NetworkException{}, std::move(peerIdentity), std::move(session));
    }
    return std::make_tuple(NetworkException{}, IdentityToken{}, std::shared_ptr<SessionType>());
}

/// stop the network and worker thread
template <FrameDecoder DecoderT, typename SocketT>
void Host<DecoderT, SocketT>::stop()
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

template <FrameDecoder DecoderT, typename SocketT>
Host<DecoderT, SocketT>::Host(std::shared_ptr<ASIOInterface> _asioInterface,
    std::shared_ptr<BasicSessionFactory<DecoderT, SocketT>> _sessionFactory)
  : m_asioInterface(std::move(_asioInterface)),
    m_sessionFactory(std::move(_sessionFactory))
{
    // FIB-186 (vector D): a single dedicated thread for session-teardown notifications, off the
    // shared IOServicePool that carries inbound-message delivery. See postTeardown / Host.h.
    // A one-worker IOServicePool is the direct replacement for the ThreadPool("p2pTeardown", 1)
    // this used to be: one owned io_context, one owned thread, stopped and joined on destruction.
    m_teardownPool = std::make_shared<bcos::IOServicePool>(1, "p2pTeardown");
}

template <FrameDecoder DecoderT, typename SocketT>
void Host<DecoderT, SocketT>::postTeardown(std::function<void()> f)
{
    // IOServicePool::post already wraps the task in safeExecute, so a throwing teardown
    // notification cannot kill the dedicated thread and wedge every later teardown.
    m_teardownPool->post(std::move(f));
}

template <FrameDecoder DecoderT, typename SocketT>
Host<DecoderT, SocketT>::~Host()
{
    // The accept loop's coroutine frame holds a strong Host reference, so reaching ~Host with
    // m_run still set means the loop was never started — a started-but-never-stopped Host simply
    // never gets here (see the stop() contract on the class comment). Flag the missing stop()
    // rather than letting it pass silently.
    if (m_run)
    {
        HOST_LOG(WARNING) << LOG_DESC("Host destroyed without stop()");
    }
    // Sessions reference this host's callback manager through std::reference_wrapper, so a
    // session outliving its host is a use-after-free precondition — flag it instead of passing
    // silently (SessionSlotGuard decrements m_sessionCount exactly when a session is destroyed).
    if (m_sessionCount.load() != 0)
    {
        HOST_LOG(WARNING) << LOG_DESC("Host destroyed with live sessions")
                          << LOG_KV("sessionCount", m_sessionCount.load());
    }
    stop();
}

template <FrameDecoder DecoderT, typename SocketT>
uint16_t Host<DecoderT, SocketT>::listenPort() const
{
    return m_listenPort;
}

template <FrameDecoder DecoderT, typename SocketT>
bool Host<DecoderT, SocketT>::haveNetwork() const
{
    return m_run;
}

template <FrameDecoder DecoderT, typename SocketT>
std::string Host<DecoderT, SocketT>::listenHost() const
{
    return m_listenHost;
}

template <FrameDecoder DecoderT, typename SocketT>
void Host<DecoderT, SocketT>::setHostPort(std::string host, uint16_t port)
{
    m_listenHost = std::move(host);
    m_listenPort = port;
}

template <FrameDecoder DecoderT, typename SocketT>
std::function<void(
    NetworkException, IdentityToken const&, std::shared_ptr<typename Host<DecoderT, SocketT>::SessionType>)>
Host<DecoderT, SocketT>::connectionHandler() const
{
    return m_connectionHandler;
}

template <FrameDecoder DecoderT, typename SocketT>
void Host<DecoderT, SocketT>::setConnectionHandler(
    std::function<void(NetworkException, IdentityToken const&,
        std::shared_ptr<typename Host<DecoderT, SocketT>::SessionType>)> connectionHandler)
{
    m_connectionHandler = std::move(connectionHandler);
}

template <FrameDecoder DecoderT, typename SocketT>
SessionCallbackManager<typename Host<DecoderT, SocketT>::SessionType>&
Host<DecoderT, SocketT>::sessionCallbackManager()
{
    return m_sessionCallbackManager;
}

template <FrameDecoder DecoderT, typename SocketT>
const std::shared_ptr<ASIOInterface>& Host<DecoderT, SocketT>::asioInterface() const
{
    return m_asioInterface;
}

template <FrameDecoder DecoderT, typename SocketT>
std::shared_ptr<BasicSessionFactory<DecoderT, SocketT>> Host<DecoderT, SocketT>::sessionFactory() const
{
    return m_sessionFactory;
}

template <FrameDecoder DecoderT, typename SocketT>
uint32_t Host<DecoderT, SocketT>::newSeq()
{
    return ++m_seq;
}

template <FrameDecoder DecoderT, typename SocketT>
void Host<DecoderT, SocketT>::setEnableSslVerify(bool _enableSSLVerify)
{
    m_enableSSLVerify = _enableSSLVerify;
    HOST_LOG(INFO) << LOG_DESC("setEnableSslVerify")
                   << LOG_KV("enableSSLVerify", m_enableSSLVerify);
}

template <FrameDecoder DecoderT, typename SocketT>
void Host<DecoderT, SocketT>::erasePendingConns(NodeIPEndpoint const& nodeIPEndpoint)
{
    bcos::Guard lock(x_pendingConns);
    auto it = m_pendingConns.find(nodeIPEndpoint);
    if (it != m_pendingConns.end())
    {
        m_pendingConns.erase(it);
    }
}

template <FrameDecoder DecoderT, typename SocketT>
void Host<DecoderT, SocketT>::insertPendingConns(NodeIPEndpoint const& nodeIPEndpoint)
{
    bcos::Guard lock(x_pendingConns);
    auto it = m_pendingConns.lower_bound(nodeIPEndpoint);
    if (it == m_pendingConns.end() || *it != nodeIPEndpoint)
    {
        m_pendingConns.emplace_hint(it, nodeIPEndpoint);
    }
}
}  // namespace bcos::gateway
