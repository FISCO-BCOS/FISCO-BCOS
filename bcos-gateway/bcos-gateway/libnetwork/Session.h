
/** @file Session.h
 * @author monan <651932351@qq.com>
 * @date 2018
 */

#pragma once

#include "bcos-gateway/libnetwork/Common.h"
#include "bcos-gateway/libnetwork/FrameMeta.h"
#include "bcos-gateway/libnetwork/SessionCallback.h"
#include "bcos-task/Task.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/Timer.h"
#include <oneapi/tbb/concurrent_queue.h>
#include <boost/asio/buffer.hpp>
#include <boost/asio/error.hpp>
#include <boost/container/small_vector.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/heap/priority_queue.hpp>
#include <range/v3/range/concepts.hpp>
#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <unordered_set>
#include <utility>


namespace bcos::gateway
{
class Socket;
// The default argument lives on this first declaration; Host.h's definition does not repeat it.
// SocketT is the socket type of the sessions this host creates (production: Socket; tests
// instantiate fakes).
template <FrameDecoder DecoderT, typename SocketT = Socket>
class Host;

class SessionRecvBuffer
{
public:
    SessionRecvBuffer(size_t _bufferSize) : m_recvBufferSize(_bufferSize)
    {
        m_recvBuffer.resize(_bufferSize);
    }

    SessionRecvBuffer(const SessionRecvBuffer&) = delete;
    SessionRecvBuffer(SessionRecvBuffer&&) = delete;
    SessionRecvBuffer& operator=(SessionRecvBuffer&&) = delete;
    SessionRecvBuffer& operator=(const SessionRecvBuffer&) = delete;
    ~SessionRecvBuffer() = default;

    std::size_t readPos() const;
    std::size_t writePos() const;
    std::size_t dataSize() const;

    size_t recvBufferSize() const;

    bool onRead(std::size_t _dataSize);
    bool onWrite(std::size_t _dataSize);
    bool resizeBuffer(size_t _bufferSize);
    void moveToHeader();
    bcos::bytesConstRef asReadBuffer() const;
    bcos::bytesConstRef asWriteBuffer() const;

private:
    // 0         readPos    writePos       m_recvBufferSize
    // |___________|__________|____________|
    //
    std::vector<byte> m_recvBuffer;
    //
    size_t m_recvBufferSize;
    // read pos of the buffer
    std::size_t m_readPos{0};
    // write pos of the buffer
    std::size_t m_writePos{0};
};

struct Payload
{
    using MessageList = boost::container::small_vector<bytesConstRef, 3>;
    MessageList m_data;
    std::function<void(boost::system::error_code)> m_callback;

    size_t size() const;
    void toConstBuffer(std::output_iterator<boost::asio::const_buffer> auto output) const
    {
        for (const auto& ref : m_data)
        {
            *output = {ref.data(), ref.size()};
        }
    }
};

// The session machinery (read loop, batched write loop, response-callback correlation) is
// generic over the frame DECODER only — the session never sees a concrete message type.
// Inbound, DecoderT splits the byte stream into FrameMeta (see FrameMeta.h); outbound, the
// caller hands over an already-encoded header plus payload views. SocketT is the socket type
// (production: Socket; a forward declaration suffices for the default argument). The member
// definitions follow at the bottom of this header; the gateway instantiates this with libp2p's
// P2PDecoder.
template <FrameDecoder DecoderT, typename SocketT = Socket>
class BasicSession : public std::enable_shared_from_this<BasicSession<DecoderT, SocketT>>
{
public:
    using DecoderType = DecoderT;

    // Grow ceiling: the recv buffer never grows beyond this (see the read-loop grow path).
    constexpr static const std::size_t MIN_SESSION_RECV_BUFFER_SIZE = 512 * 1024UL;
    // FIB-184: initial recv-buffer size for a freshly created session. Previously every
    // session unconditionally allocated MIN_SESSION_RECV_BUFFER_SIZE (512KB) up front, so a
    // flood of unauthenticated/short-lived sessions caused heap exhaustion. Start small and
    // rely on the existing grow path (the read loop grows up to m_maxRecvBufferSize) to expand
    // only for sessions that actually carry large messages. Must stay well above the message
    // header length so the first read can always make forward progress.
    constexpr static const std::size_t INITIAL_SESSION_RECV_BUFFER_SIZE = 16 * 1024UL;

    BasicSession(std::shared_ptr<SocketT> socket, Host<DecoderT, SocketT>& server,
        size_t _recvBufferSize = INITIAL_SESSION_RECV_BUFFER_SIZE, bool _forceSize = false);

    BasicSession(const BasicSession&) = delete;
    BasicSession(BasicSession&&) = delete;
    BasicSession& operator=(BasicSession&&) = delete;
    BasicSession& operator=(const BasicSession&) = delete;
    ~BasicSession() noexcept;

    using Ptr = std::shared_ptr<BasicSession>;

    void start();

    // Read-policy seam (compile-time): identical lifecycle to start(), but the read loop is
    // compiled against an explicit ReadPolicy so read-loop test fakes can inject a policy that
    // parks / controls read completions (see ASIOInterface::awaitableReadSome). Production call
    // sites use start() (the default policy) — this template costs nothing there.
    // Definition lives in SessionReadLoop.h.
    template <typename ReadPolicy>
    void startWithPolicy();
    void disconnect(DisconnectReason _reason);

    // payloads: any input range of byte-view-like elements (bytesConstRef-convertible) — a plain
    // range, NOT a type-erased any_view: the send path is header-only and instantiated per caller
    // view type, so the per-element access compiles down to direct reads. Taken by value: views
    // are cheap to copy, and a by-value parameter is moved into the coroutine frame, so the view
    // stays valid no matter how the caller's temporaries are scoped.
    template <::ranges::input_range Payloads>
        requires std::convertible_to<::ranges::range_reference_t<Payloads>, bytesConstRef>
    task::Task<std::optional<FrameMeta>> fastSendMessage(
        bytesConstRef header, Payloads payloads, uint32_t seq, Options options);

    NodeIPEndpoint nodeIPEndpoint() const;

    bool active() const;

    bool active(Host<DecoderT, SocketT>& server) const;

    std::size_t writeQueueSize();

    Host<DecoderT, SocketT>& host();

    std::shared_ptr<SocketT> socket();
    void setSocket(const std::shared_ptr<SocketT>& socket);

    SessionCallbackManager<BasicSession>& sessionCallbackManager() const;

    const std::function<void(NetworkException, Ptr, FrameMeta)>& messageHandler();
    void setMessageHandler(
        std::function<void(NetworkException, Ptr, FrameMeta)> messageHandler);

    // FIB-184: attach an opaque object whose lifetime is bound to this session. It is destroyed
    // exactly when the session object is destroyed, which Host uses to release a session-cap
    // slot (the guard's destructor decrements the Host counters). Kept opaque so libnetwork
    // does not depend on the accounting type.
    void setLifetimeGuard(std::shared_ptr<void> _guard)
    {
        m_lifetimeGuard = std::move(_guard);
    }

    uint32_t maxReadDataSize() const;
    void setMaxReadDataSize(uint32_t _maxReadDataSize);

    uint32_t maxSendDataSize() const;
    void setMaxSendDataSize(uint32_t _maxSendDataSize);

    uint32_t allowMaxMsgSize() const;
    void setAllowMaxMsgSize(uint32_t _allowMaxMsgSize);

    SessionRecvBuffer& recvBuffer();
    const SessionRecvBuffer& recvBuffer() const;

    // FIB-184 (review): grow ceiling for the recv buffer, set from the config-validated size at
    // construction. Exposed read-only; the member itself is private (below) so external callers
    // can read but never widen this security bound.
    std::size_t maxRecvBufferSize() const { return m_maxRecvBufferSize; }
    /**
     * @brief The packets that can be sent are obtained based on the configured policy
     *
     * @param encodedMsgs
     * @param _maxSendDataSize
     * @return bool
     */
    bool tryPopSomeEncodedMsgs(std::vector<Payload>& encodedMsgs, size_t _maxSendDataSize);

    void checkNetworkStatus();

    // FIB-184 (review): keep the grow ceiling private so it can only be read via
    // maxRecvBufferSize() and never widened from outside. Declared before m_recvBuffer to preserve
    // member init order.
private:
    std::size_t m_maxRecvBufferSize;

public:
    SessionRecvBuffer m_recvBuffer;

    // The stream decoder for this session's wire format. Stateless decoders are empty classes;
    // no_unique_address keeps them zero-cost.
    [[no_unique_address]] DecoderT m_decoder{};

    // ------ for optimize send message parameters  begin ---------------
    //  // Maximum amount of data to read one time, default: 40K
    uint32_t m_maxReadDataSize = 40 * 1024;
    // Maximum amount of data to be sent one time, default: 1M
    uint32_t m_maxSendDataSize = 1024 * 1024;
    //  Maximum size of message that is allowed to send or receive, default: 32M
    uint32_t m_allowMaxMsgSize = 32 * 1024 * 1024;
    // ------ for optimize send message parameters  end ---------------

    /// Drop the connection for the reason @a _reason.
    void drop(DisconnectReason _reason);

private:
    // Read-loop coroutine, launched fire-and-forget via task::wait from startWithPolicy. Each
    // frame holds a strong reference to the session for the whole loop, so an in-flight read keeps
    // the session, its recv buffer and its socket alive — the FIB-184 lifetime invariant, made
    // structural instead of relying on completion-handler captures. ReadPolicy is the
    // compile-time read-initiation policy (see ASIOInterface::awaitableReadSome): production
    // instantiates ASIOInterface::DefaultReadPolicy, test fakes their own. Definition in
    // SessionReadLoop.h.
    template <typename ReadPolicy>
    task::Task<void> readLoop();
    // Single-writer write loop (see write()): drains m_writeQueue in batches and serializes every
    // async_write through the single in-flight loop. The frame holds a strong reference to the
    // session for the whole loop (see the body), keeping the socket and the batch buffers alive
    // across each co_await.
    task::Task<void> writeLoop();

    // FIB-184: perform the actual SSL/socket teardown (close + graceful async_shutdown). It has a
    // strict threading contract — it must run on the socket's io_context (or, on the shutdown path,
    // with the io_context threads already joined) so it never touches the ssl::stream concurrently
    // with an in-flight async_read_some/async_write. It is therefore private and reachable only via
    // drop(), which enforces that contract (post to the io_context, or inline once the network is
    // down); calling it directly from an arbitrary thread would reintroduce the original race.
    void closeSocket(DisconnectReason _reason);

public:
    /// Check error code after reading and drop peer if error code.
    bool checkRead(boost::system::error_code _ec);

    void onTimeout(const boost::system::error_code& error, uint32_t seq);

    /// Launch the write loop (writeLoop) if no write is currently in flight. Safe to call from
    /// any thread: the first caller wins the m_writingInFlight CAS and becomes the single writer;
    /// concurrent callers return immediately and rely on the in-flight writer (or writeLoop's
    /// single exit) to drain the queue.
    void write();

    /// called by the read loop to deal with a decoded frame
    void onMessage(NetworkException const& e, FrameMeta meta);

    // Response-correlation mechanism for the protocol layer's message handler: settle the pending
    // with-response send registered under meta.seq — claim its callback from the host-wide
    // manager, cancel its timeout, update the owner session's pending-seq bookkeeping and invoke
    // the callback with the frame. Returns false when nothing is registered for the seq (it
    // already timed out or was settled elsewhere). DECIDING which frames are responses for this
    // node is the protocol layer's policy — libnetwork never interprets FrameMeta::isResp/dstID.
    bool claimResponse(NetworkException const& e, FrameMeta meta);

    /// Settle one queued callback that resumes a suspended waiter, delivering `args...` to it.
    /// The settle error is chosen at the call site, per callback signature: a payload callback
    /// takes a boost::system::error_code, whereas a response callback
    /// (ResponseCallback::callback) takes (NetworkException, std::optional<FrameMeta>).
    ///
    /// Run it on the shared io pool while the host is alive, inline once it is gone, containing any
    /// exception either way. Three reasons for that shape, all of them load-bearing:
    ///  - A resumed waiter may throw (fastSendMessageWithoutResponse throws NetworkException on a
    ///    failed write) and a posted callback runs inside io_context::run(), so an escaping
    ///    exception would unwind the reactor.
    ///  - Once the host is gone a posted task would never run at all, which would hang every
    ///    waiter forever.
    ///  - The caller may itself be sitting on another waiter's await_suspend stack, so it must not
    ///    nest that waiter's whole continuation either.
    /// This is the single place that policy lives for write-queue settlement; the
    /// response-callback paths in onMessage / onTimeout settle inline under their own containment.
    /// Defined out-of-line below, in this header's definition section: the body touches Host
    /// (incomplete here), and as a member of a class template the compiler would check those
    /// non-dependent expressions eagerly at the point of definition.
    template <class Callback, class... Args>
    void postCallback(Callback&& callback, const char* description, Args... args);

    std::reference_wrapper<Host<DecoderT, SocketT>> m_server;  ///< The host that owns us. Never null.
    std::shared_ptr<SocketT> m_socket;                ///< Socket of peer's connection.

    tbb::concurrent_queue<Payload> m_writeQueue;
    // Single-flight flag guarding the write path: write() CASes it to true to claim the writer
    // role (exactly one writeLoop runs at a time); writeLoop's exit guard clears it and re-arms
    // write() when the queue is non-empty at exit. Replaces the old try_lock-as-flag std::mutex —
    // it never blocks, so no lock is ever held across a co_await.
    std::atomic<bool> m_writingInFlight{false};
    // FIB-184 (review): atomic so the active flag is read/written without a data race between the
    // network worker (set/clear in start/drop) and readers in active(). Note active() is still a
    // composite read (also m_socket / haveNetwork()), so this narrows but does not by itself make
    // the whole liveness check atomic.
    std::atomic<bool> m_active{false};

    // Owned by the Host (m_server) that created us. Never null, like m_server.
    std::reference_wrapper<SessionCallbackManager<BasicSession>> m_sessionCallbackManager;
    std::function<void(NetworkException, Ptr, FrameMeta)> m_messageHandler;

    // Seqs of with-response sends registered through this session. The callback manager above is
    // shared host-wide, so drop() uses this set to fail only THIS session's pending response
    // waiters instead of popping callbacks that belong to other sessions.
    void addPendingResponseSeq(uint32_t seq)
    {
        std::lock_guard lock(x_pendingResponseSeqs);
        m_pendingResponseSeqs.emplace(seq);
    }
    void removePendingResponseSeq(uint32_t seq)
    {
        std::lock_guard lock(x_pendingResponseSeqs);
        m_pendingResponseSeqs.erase(seq);
    }

    uint64_t m_shutDownTimeThres = 50000;
    // 1min
    uint64_t m_idleTimeInterval = 60 * 1000;

    // timer to check the connection
    std::atomic<uint64_t> m_lastReadTime;
    std::atomic<uint64_t> m_lastWriteTime;
    std::shared_ptr<bcos::Timer> m_idleCheckTimer;

    // FIB-97-new: idempotency guard. drop() may be invoked concurrently from the
    // teardown signal, explicit disconnect, or a deferred async callback that the
    // shared_ptr capture (FIB-97 primary fix) kept alive. CAS to true ensures the
    // actual teardown body runs exactly once; all subsequent callers no-op.
    std::atomic_bool m_dropped{false};

    // FIB-184: opaque guard whose destructor releases the Host session-cap slot. Destroyed with
    // the session, so the slot is freed exactly once on session teardown.
    std::shared_ptr<void> m_lifetimeGuard;

    std::mutex x_pendingResponseSeqs;
    std::unordered_set<uint32_t> m_pendingResponseSeqs;
};

// Concrete session factory, held by Host<DecoderT, SocketT>. The gateway instantiates it with
// libp2p's P2PDecoder and the default Socket.
template <FrameDecoder DecoderT, typename SocketT = Socket>
class BasicSessionFactory
{
public:
    BasicSessionFactory(uint32_t _sessionRecvBufferSize, uint32_t _allowMaxMsgSize,
        uint32_t _maxReadDataSize, uint32_t _maxSendDataSize)
      : m_sessionRecvBufferSize(_sessionRecvBufferSize),
        m_allowMaxMsgSize(_allowMaxMsgSize),
        m_maxReadDataSize(_maxReadDataSize),
        m_maxSendDataSize(_maxSendDataSize)
    {}
    BasicSessionFactory(const BasicSessionFactory&) = delete;
    BasicSessionFactory(BasicSessionFactory&&) = delete;
    BasicSessionFactory& operator=(BasicSessionFactory&&) = delete;
    BasicSessionFactory& operator=(const BasicSessionFactory&) = delete;
    ~BasicSessionFactory() = default;

    std::shared_ptr<BasicSession<DecoderT, SocketT>> createSession(
        Host<DecoderT, SocketT>& _server, std::shared_ptr<SocketT> const& _socket);

private:
    uint32_t m_sessionRecvBufferSize;
    uint32_t m_allowMaxMsgSize{0};
    uint32_t m_maxReadDataSize{0};
    uint32_t m_maxSendDataSize{0};
};

}  // namespace bcos::gateway

// ---------------------------------------------------------------------------
// Member definitions of the BasicSession / BasicSessionFactory templates (declared above).
// Template definitions must be visible at every instantiation point, so they live in this
// header, below the class declarations; the includes they need — including the complete Host —
// are kept here at the point of use, NOT hoisted to the top: Host.h includes this header, so
// hoisting would make the Session.h -> Host.h -> Session.h cycle unresolvable.
#include "bcos-gateway/libnetwork/ASIOInterface.h"
#include "bcos-gateway/libnetwork/Common.h"
#include "bcos-gateway/libnetwork/Host.h"
#include "bcos-gateway/libnetwork/SessionReadLoop.h"
#include "bcos-gateway/libnetwork/Socket.h"
#include "bcos-utilities/BoostLog.h"
#include <bcos-task/Wait.h>
#include <boost/asio/buffer.hpp>
#include <boost/asio/post.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/throw_exception.hpp>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <functional>
#include <iterator>
#include <range/v3/numeric/accumulate.hpp>
#include <range/v3/view/all.hpp>
#include <range/v3/view/concat.hpp>
#include <range/v3/view/single.hpp>
#include <utility>

namespace bcos::gateway
{
template <FrameDecoder DecoderT, typename SocketT>
BasicSession<DecoderT, SocketT>::BasicSession(
    std::shared_ptr<SocketT> socket, Host<DecoderT, SocketT>& server, size_t _recvBufferSize,
    bool _forceSize)
  : m_maxRecvBufferSize(std::max<size_t>(_recvBufferSize, MIN_SESSION_RECV_BUFFER_SIZE)),
    // FIB-184: treat _recvBufferSize as the grow CEILING, not the initial allocation. Production
    // createSession passes the config-validated session_recv_buffer_size, which is forced to
    // 2 * allow_max_msg_size = 64MB; allocating that per session up front let authenticated TLS
    // connect/close churn exhaust the heap (SIGSEGV inside malloc during Session construction).
    // Allocate only INITIAL_SESSION_RECV_BUFFER_SIZE (16KB) initially and let the read loop grow
    // the buffer up to m_maxRecvBufferSize on demand, so only sessions that actually carry large
    // messages pay for a large buffer. _forceSize keeps the exact size for tests that assert a
    // specific small buffer.
    m_recvBuffer(_forceSize ? _recvBufferSize :
                              std::min<size_t>(_recvBufferSize, INITIAL_SESSION_RECV_BUFFER_SIZE)),
    m_server(server),
    m_socket(std::move(socket)),
    m_sessionCallbackManager(server.sessionCallbackManager()),
    m_idleCheckTimer(
        std::make_shared<Timer>(m_socket->ioService(), m_idleTimeInterval, "idleChecker"))
{
    SESSION_LOG(INFO) << "[Session::Session] this=" << this
                      << LOG_KV("recvBufferSize", m_maxRecvBufferSize);
}

template <FrameDecoder DecoderT, typename SocketT>
BasicSession<DecoderT, SocketT>::~BasicSession() noexcept
{
    SESSION_LOG(INFO) << "[Session::~Session] this=" << this;
    try
    {
        m_idleCheckTimer->stop();
        if (m_socket)
        {
            bi::tcp::socket& socket = m_socket->ref();
            if (m_socket->isConnected())
            {
                socket.close();
            }
        }
    }
    catch (...)
    {
        SESSION_LOG(ERROR) << "Deconstruct Session exception";
    }
}

template <FrameDecoder DecoderT, typename SocketT>
NodeIPEndpoint BasicSession<DecoderT, SocketT>::nodeIPEndpoint() const
{
    return m_socket->nodeIPEndpoint();
}

template <FrameDecoder DecoderT, typename SocketT>
bool BasicSession<DecoderT, SocketT>::active() const
{
    return active(m_server);
}

template <FrameDecoder DecoderT, typename SocketT>
bool BasicSession<DecoderT, SocketT>::active(Host<DecoderT, SocketT>& server) const
{
    return m_active && server.haveNetwork() && m_socket && m_socket->isConnected();
}

namespace detail
{
template <FrameDecoder DecoderT, typename SocketT, ::ranges::input_range Payloads>
task::Task<boost::system::error_code> send(
    BasicSession<DecoderT, SocketT>& session, Payloads payloads)
{
    if (!session.active() || !session.m_socket->isConnected())
    {
        co_return boost::asio::error::not_connected;
    }

    // Build one Payload carrying the given payload views, then wait until the write loop flushed
    // (or aborted) it. The payload's completion callback settles a GetResultAwaitable below --
    // exactly the "write completion" half the old with-response ResumeGate tracked by hand.
    task::GetResultAwaitable<boost::system::error_code>::Result result;
    Payload payload;
    payload.m_callback = [&result](boost::system::error_code ec) {
        task::GetResultAwaitable<boost::system::error_code>::complete(result, ec);
    };
    auto& vec = payload.m_data;
    if constexpr (::ranges::sized_range<Payloads>)
    {
        vec.reserve(::ranges::size(payloads));
    }
    for (const auto& data : payloads)
    {
        vec.emplace_back(data.data(), data.size());
    }
    session.m_writeQueue.push(std::move(payload));

    // FIB-185 (review): re-check the session state AFTER the push. drop() may have drained the
    // queue (CAS won) between the active() check above and this push, in which case this
    // payload's callback would never fire and this coroutine would hang. Either drop()'s drain
    // already popped our payload (its callback then fires with operation_aborted), or this
    // re-check sees an inactive session and drains the leftover payloads here. Both paths settle
    // every queued callback exactly once: the loop drains the whole queue, so it may resume
    // senders other than this coroutine — postCallback settles them off this stack.
    if (!session.active())
    {
        Payload pending;
        while (session.m_writeQueue.try_pop(pending))
        {
            if (pending.m_callback)
            {
                session.postCallback(std::move(pending.m_callback),
                    "drained write callback exception", boost::asio::error::not_connected);
            }
        }
    }
    else
    {
        session.write();
    }

    co_return std::get<0>(co_await task::GetResultAwaitable<boost::system::error_code>(result));
}
}  // namespace detail

template <FrameDecoder DecoderT, typename SocketT>
std::size_t BasicSession<DecoderT, SocketT>::writeQueueSize()
{
    return static_cast<std::size_t>(!m_writeQueue.empty());
}

template <FrameDecoder DecoderT, typename SocketT>
bool BasicSession<DecoderT, SocketT>::tryPopSomeEncodedMsgs(
    std::vector<Payload>& encodedMsgs, size_t _maxSendDataSize)  // NOLINT
{
    // Desc: Try to send multi packets one time to improve the efficiency of sending data. Stop
    // batching once the configured byte budget (p2p.session_max_send_data_size) is exhausted;
    // the remainder stays queued for the next loop iteration. The message-count budget
    // (p2p.session_max_send_msg_count) is deliberately NOT enforced here — the base had that
    // condition commented out and drained the whole queue into a single scatter-gather write,
    // and re-enabling it capped every async_write at 10 messages, costing ceil(N/10) post +
    // async_write round-trips under broadcast fan-out. The byte budget alone bounds a single
    // write's memory footprint; see the behaviour-change note in the PR description.
    size_t totalDataSize = 0;
    Payload payload;
    // Floor the budget at 1 here — the point where the invariant "the byte budget is never 0"
    // is actually needed. The GatewayConfig clamp warns the operator, but Session::
    // setMaxSendDataSize stores whatever it is given, and a 0 budget makes the while below
    // never enter: tryPop returns false, writeLoop breaks on its first iteration and every
    // outbound write on the session stalls silently and permanently.
    size_t budget = std::max<size_t>(_maxSendDataSize, 1);
    while (totalDataSize < budget && m_writeQueue.try_pop(payload))
    {
        totalDataSize += payload.size();
        encodedMsgs.emplace_back(std::move(payload));
    }

    return totalDataSize > 0;
}

template <FrameDecoder DecoderT, typename SocketT>
template <class Callback, class... Args>
void BasicSession<DecoderT, SocketT>::postCallback(Callback&& callback, const char* description,
    Args... args)
{
    static_assert(std::is_invocable_v<Callback&, Args...>,
        "postCallback: the callback cannot be invoked with the given settle arguments");

    auto deliver = [callback = std::forward<Callback>(callback), description,
                       ... args = std::move(args)]() mutable {
        try
        {
            callback(std::move(args)...);
        }
        catch (std::exception const& e)
        {
            SESSION_LOG(WARNING)
                << LOG_DESC(description) << LOG_KV("what", boost::diagnostic_information(e));
        }
    };
    if (m_server.get().haveNetwork())
    {
        m_server.get().asioInterface()->post(std::move(deliver));
    }
    else
    {
        deliver();  // no live executor: a posted task would never run
    }
}

template <FrameDecoder DecoderT, typename SocketT>
void BasicSession<DecoderT, SocketT>::write()
{
    if (m_writingInFlight.exchange(true))
    {
        // a write loop is already in flight; it drains the queue
        return;
    }
    // Launch the loop on the socket's own io thread, NOT synchronously on the caller's stack.
    // write() is reached from a sender's await_suspend (fastSendMessageWith/WithoutResponse via
    // send()): running writeLoop's first iteration there would let a shutdown race (Host::stop()
    // clearing m_run between send()'s active() re-check and writeLoop's haveNetwork() check) call
    // drop(TCPError) on that stack, and drop()'s inline drain would invoke the just-queued
    // payload's callback — resuming the very coroutine whose await_suspend is still running
    // (undefined behaviour; await_resume's throw could even destroy the frame await_suspend is
    // standing on). With the launch posted, every settlement inside writeLoop — including the
    // drop() it may trigger and failBatch's inline branch — runs on an io thread, so no callback
    // can land on a sender's own stack. Cost is one post per write-loop start; the loop already
    // suspends into awaitableWrite, so steady-state cost is unchanged.
    //
    // Release-on-unwind, matching the old std::unique_lock(try_to_lock) writer flag: if the
    // launch itself throws (post allocation failure, or bad_weak_ptr from shared_from_this()),
    // the flag must not stay set, or every later write() would early-return at the CAS above and
    // the queue would never drain again. The exception is NOT propagated to the caller (see
    // above: the caller may be a sender's await_suspend with the payload already queued);
    // swallow, log and drop, which drains m_writeQueue and completes every queued callback.
    auto socket = m_socket;
    if (!socket)
    {
        // inactive session (setSocket(nullptr) raced the active() checks): the queue is
        // settled by send()'s post-push re-check or by drop(), so just release the flag
        m_writingInFlight.store(false);
        return;
    }
    try
    {
        boost::asio::post(socket->ioService(), [self = this->shared_from_this()]() {
            try
            {
                task::wait(self->writeLoop());
            }
            catch (...)
            {
                // the loop's own catches make this unreachable in practice; if the frame
                // allocation itself threw, release the flag and settle the queue
                self->m_writingInFlight.store(false);
                SESSION_LOG(ERROR) << LOG_DESC("write loop launch failed")
                                   << LOG_KV("what",
                                          boost::current_exception_diagnostic_information());
                self->drop(TCPError);
            }
        });
    }
    catch (...)
    {
        m_writingInFlight.store(false);
        SESSION_LOG(ERROR) << LOG_DESC("write loop launch failed")
                           << LOG_KV("what", boost::current_exception_diagnostic_information());
        drop(TCPError);
    }
}

template <FrameDecoder DecoderT, typename SocketT>
task::Task<void> BasicSession<DecoderT, SocketT>::writeLoop()
{
    // FIB-184: the coroutine frame holds a strong reference to the session for the whole write
    // loop. async_write operates on m_socket and reads from the batch buffers below; the frame
    // keeps the session (socket, SSL stream, batch buffers) alive until each write completes, so
    // a concurrent teardown on another thread cannot free them mid-write.
    auto self = this->shared_from_this();

    // Batch scratch owned by this frame for the whole loop (was the m_writings member; the frame
    // is the lifetime owner now, so no shared_ptr indirection is needed).
    std::vector<Payload> payloads;
    std::vector<boost::asio::const_buffer> buffers;

    // No frame-local RAII guard is needed for the single-flight write flag. The completion
    // rescue (FireCompletion in FireAwaitable.h) RESUMES an uninvoked completion's coroutine
    // with the initial error rather than destroying its frame, so this loop always runs through
    // to its single exit below and releases m_writingInFlight there. The only frame destroyed
    // before that is one destroyed while parked at initial_suspend (a failed launch), and the
    // launch-failure catch in write() already releases the flag for that case. NOTE: a
    // destroy-based rescue would make the flag and the popped batch leak here — re-add a guard
    // like the one this comment replaced if such a rescue is ever reintroduced.

    // Fails the in-flight batch: the payloads have already been moved out of m_writeQueue and no
    // completion handler exists for them, so their callbacks would never fire (drop() only drains
    // m_writeQueue). Invoked from every exception exit so the batch is settled exactly once — a
    // batch destroyed with its callbacks unfired would pin every awaiting sender forever.
    auto failBatch = [this](std::vector<Payload>& batch) {
        for (auto& payload : batch)
        {
            if (payload.m_callback)
            {
                this->postCallback(std::move(payload.m_callback), "write callback exception",
                    boost::asio::error::operation_aborted);
            }
        }
        batch.clear();
    };

    try
    {
        while (true)
        {
            if (!m_server.get().haveNetwork())
            {
                SESSION_LOG(WARNING) << "Host has gone";
                drop(TCPError);
                break;
            }
            if (!m_socket->isConnected())
            {
                SESSION_LOG(WARNING)
                    << "Error sending ssl socket is close!" << LOG_KV("endpoint", nodeIPEndpoint());
                drop(TCPError);
                break;
            }

            if (!tryPopSomeEncodedMsgs(payloads, m_maxSendDataSize))
            {
                // queue drained; fall through to the single exit below
                break;
            }

            auto outputIt = std::back_inserter(buffers);
            for (auto& payload : payloads)
            {
                payload.toConstBuffer(outputIt);
            }
            // `size` is unused: the loop re-derives the batch layout from `payloads` on each
            // iteration, and a short/failed write is handled through `error` alone
            [[maybe_unused]] auto [error, size] =
                co_await m_server.get().asioInterface()->awaitableWrite(
                    m_socket, std::move(buffers));

            buffers.clear();
            for (auto& payload : payloads)
            {
                if (payload.m_callback)
                {
                    this->postCallback(
                        std::move(payload.m_callback), "write callback exception", error);
                }
            }
            payloads.clear();

            if (!active())
            {
                break;
            }
            m_lastWriteTime.store(utcSteadyTime());
            if (error)
            {
                SESSION_LOG(WARNING)
                    << LOG_DESC("onWrite error sending") << LOG_KV("message", error.message())
                    << LOG_KV("endpoint", nodeIPEndpoint());
                drop(TCPError);
                break;
            }
            // loop back and drain the next batch
        }
    }
    catch (std::exception& e)
    {
        SESSION_LOG(ERROR) << LOG_DESC("write error") << LOG_KV("endpoint", nodeIPEndpoint())
                           << LOG_KV("what", boost::diagnostic_information(e));
        // FIB-185 (review): when the write path throws, the payloads have already been moved into
        // the local batch and no completion handler exists for them, so their callbacks would
        // never fire (drop() only drains m_writeQueue). Fail them here — posted rather than
        // inline, for the same reason as drop()'s drain: this catch runs on the caller's stack,
        // which may still be inside an await_suspend.
        failBatch(payloads);
        drop(TCPError);
    }
    catch (...)
    {
        // never let an exception escape into the resuming asio handler (see FireAwaitable.h);
        // fail the in-flight batch here too — the catch(std::exception&) arm above does it, and a
        // batch destroyed with its callbacks unfired would pin every awaiting sender forever
        SESSION_LOG(ERROR) << LOG_DESC("write error") << LOG_KV("endpoint", nodeIPEndpoint())
                           << LOG_KV("what", boost::current_exception_diagnostic_information());
        failBatch(payloads);
        drop(TCPError);
    }

    // Single exit: clear the single-flight flag and, if the queue was refilled while this loop
    // was draining, hand the writer role to a fresh loop. Runs after every exit path (normal
    // drain, drop, exception). No wakeup is ever lost: a producer that pushed after the last
    // drain either wins the CAS in its own write() (flag already cleared) or this tail finds the
    // queue non-empty and re-arms. The re-arm (write()) posts the next loop's launch to the
    // socket's io thread, so it never extends this stack. Gated on active(): after a drop the
    // queue is drained by drop() (and late producers fail via send()'s re-check), so re-arming
    // there would just spin a fresh loop into the same teardown.
    //
    // The flag is cleared with exchange, not store: the tail never otherwise READS the flag, and
    // a plain store is release-only, so the queue re-check below would have no happens-before
    // edge from a producer's push (push, then exchange(true) observing the flag set). The seq_cst
    // exchange reads from the release sequence headed by that producer's exchange(true), giving
    // every such producer's push a happens-before edge to the re-check.
    m_writingInFlight.exchange(false);
    if (active() && !m_writeQueue.empty())
    {
        try
        {
            write();
        }
        catch (std::exception const& e)
        {
            SESSION_LOG(WARNING) << LOG_DESC("write re-arm exception")
                                 << LOG_KV("what", boost::diagnostic_information(e));
        }
    }
    co_return;
}

template <FrameDecoder DecoderT, typename SocketT>
void BasicSession<DecoderT, SocketT>::drop(DisconnectReason _reason)
{
    // FIB-97-new: idempotency guard — only the first caller (wins the CAS) proceeds
    // with the actual teardown; all subsequent or concurrent calls are no-ops.
    bool expected = false;
    if (!m_dropped.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel, std::memory_order_acquire))
    {
        return;
    }

    // Capture m_socket into a local shared_ptr BEFORE setting m_active = false.
    // The test thread may call setSocket(nullptr) as soon as active() returns false,
    // which creates a data race on m_socket.  Using a local copy guarantees we
    // hold a valid reference for the entire teardown sequence.
    auto socket = m_socket;

    m_active = false;

    // Fail any queued zero-copy sends: their completion callbacks resume suspended coroutine
    // chains (fastSendMessageWithoutResponse). Without this, a session that disappears before the
    // write completes would leak the whole task::wait chain — including the strong refs to the
    // session/socket/service it captured (a broadcast fan-out multiplies this by the peer count).
    // In-flight writes (the write loop's current batch) are covered by the write loop
    // (BasicSession::writeLoop), which invokes their callbacks with the error when the socket
    // close cancels the write.
    // Also covers BasicSession::write()'s early returns: they call drop(TCPError) right after the
    // payload has been pushed into m_writeQueue.
    //
    // FIB-185 (review): complete these callbacks OFF the caller's stack whenever an executor is
    // available. writeLoop never runs on a sender's stack anymore (BasicSession::write() posts its
    // launch), but drop() remains reachable from arbitrary caller stacks — including write()'s
    // launch-failure catch, which can still sit inside a sender's await_suspend — so calling the
    // callback inline here could resume a coroutine from inside its own await_suspend. postCallback
    // posts to the shared pool and falls back to inline only when the host is already gone — the
    // same shape as the notifyDisconnect / closeSocket branches below.
    Payload payload;
    while (m_writeQueue.try_pop(payload))
    {
        if (payload.m_callback)
        {
            postCallback(std::move(payload.m_callback), "write callback exception during drop",
                boost::asio::error::operation_aborted);
        }
    }

    // Fail this session's pending response waiters: a request/response send
    // (fastSendMessageWithResponse) suspends until a matching-seq reply arrives or its timeout
    // fires, and neither happens once the session is gone. The callback manager is shared
    // host-wide, so the flush is scoped to the seqs registered through THIS session
    // (m_pendingResponseSeqs) — popping the whole manager would spuriously fail every in-flight
    // request/response on every other session. Mirror the writeQueue drain above: fire
    // NetworkTimeout off the caller's stack when an executor is available, since drop() is
    // reachable from inside a sender's own await_suspend. Waiters registered after this flush
    // are caught by the active() re-check in the awaitable.
    std::vector<typename SessionResponseCallback<BasicSession<DecoderT, SocketT>>::Ptr>
        pendingCallbacks;
    {
        std::lock_guard lock(x_pendingResponseSeqs);
        pendingCallbacks.reserve(m_pendingResponseSeqs.size());
        for (auto seq : m_pendingResponseSeqs)
        {
            if (auto handler = m_sessionCallbackManager.get().getCallback(seq, true))
            {
                pendingCallbacks.emplace_back(std::move(handler));
            }
        }
        m_pendingResponseSeqs.clear();
    }
    for (auto& callback : pendingCallbacks)
    {
        if (!callback || !callback->callback)
        {
            continue;
        }
        if (callback->timeoutHandler)
        {
            callback->timeoutHandler->cancel();
        }
        postCallback(std::move(callback->callback),
            "response callback exception during drop",
            makeNetworkException(P2PExceptionType::NetworkTimeout, "NetworkTimeout"), std::nullopt);
    }

    int errorCode = P2PExceptionType::Disconnect;
    std::string errorMsg = "Disconnect";
    if (_reason == DuplicatePeer)
    {
        errorCode = P2PExceptionType::DuplicateSession;
        errorMsg = "DuplicateSession";
    }

    // Guard against null socket (e.g. test sets it to nullptr before destructor)
    if (!socket)
    {
        return;
    }

    SESSION_LOG(INFO) << "drop, call and erase all callback in this session!"
                      << LOG_KV("this", this) << LOG_KV("endpoint", socket->nodeIPEndpoint());

    if (m_messageHandler)
    {
        // FIB-186 (vector D): run the teardown notification on the dedicated teardown executor, NOT
        // the shared IOServicePool. This handler drives Service::onMessage's error path ->
        // onDisconnect -> onRemoveNodeIDs -> syncLatestNodeIDList; running it on the same reactor
        // that carries inbound message delivery let a persistent bulk-disconnect flood starve
        // inter-validator message delivery and permanently halt consensus. postTeardown keeps it
        // off the delivery reactor. Ordering vs message delivery is unchanged: onDisconnect already
        // ran asynchronously and unordered relative to delivery.
        auto notifyDisconnect = [self = this->weak_from_this(), errorCode,
                                    errorMsg = std::move(errorMsg)]() {
            auto session = self.lock();
            if (!session)
            {
                return;
            }
            session->m_messageHandler(
                makeNetworkException(errorCode, errorMsg), session, FrameMeta{});
        };
        // Once haveNetwork() is false the Host is on its way out, so run the notification inline
        // rather than handing it to an executor whose remaining lifetime we do not control here.
        //
        // NOTE, because the surrounding machinery changed under this fix: the teardown executor is
        // deliberately NOT stopped by Host::stop() (see Host.cpp) -- it lives until ~Host -- and
        // Host::stop() no longer stops the ASIO interface either, so the shared pool's threads are
        // still running at this point. In practice this branch is currently unreachable during
        // shutdown: Service::stop() calls Host::stop() first, which makes Session::active() false,
        // and P2PSession::stop() only calls disconnect() on an active session. The reachable
        // shutdown-time callers are socket error paths, which already run on the socket's own
        // io_context.
        //
        // KNOWN HAZARD of this inline branch (recorded, not a reason to keep it): any caller that
        // reaches drop() while holding x_sessions would self-deadlock here, because the inline
        // notifyDisconnect re-enters Service::onDisconnect -> getP2PSessionByNodeId, which takes
        // that same non-reentrant shared_mutex -- and Service::stop() does iterate sessions under
        // it exclusively. No such caller exists today (see the reachability note above). Making
        // this branch unconditionally async would remove the hazard outright, since the teardown
        // thread never holds x_sessions; that is worth doing on its own, with the shutdown-path
        // verification it deserves, rather than inside a merge.
        if (m_server.get().haveNetwork())
        {
            m_server.get().postTeardown(std::move(notifyDisconnect));
        }
        else
        {
            notifyDisconnect();
        }
    }

    // FIB-184: serialize the SSL/socket teardown onto the socket's own (single-threaded)
    // io_context. drop() can be invoked from another thread (Service-layer teardown, duplicate-peer
    // handling) while an async_read_some/async_write is still in flight on the socket's io_context
    // thread. Running close()/async_shutdown inline on the caller thread would then touch the same
    // ssl::stream concurrently with those handlers. Posting the teardown to the socket's io_context
    // makes it run on the same single thread that services every read/write for this session —
    // i.e. a per-session strand — so socket operations never overlap. The strong self capture keeps
    // the session (and its socket) alive until the teardown runs.
    //
    // Shutdown path exception: Service::stop() calls Host::stop() BEFORE dropping sessions, and the
    // shared IOServicePool is torn down shortly after by whoever owns it, so once the network is
    // down a posted handler may never run — the socket would never be closed and the posted task
    // would pin this session in a dead io_context queue. Close inline instead, matching the old
    // synchronous teardown behaviour on shutdown.
    //
    // Do not read this as "the io_context threads are already joined": Host::stop() only clears
    // m_run now (it no longer stops the ASIO interface), so the shared pool is still live here.
    // What makes the inline close safe is the caller, not a quiesced pool — see the note on the
    // teardown-notification branch above.
    if (m_server.get().haveNetwork())
    {
        boost::asio::post(socket->ioService(),
            [self = this->shared_from_this(), _reason]() { self->closeSocket(_reason); });
    }
    else
    {
        closeSocket(_reason);
    }
}

template <FrameDecoder DecoderT, typename SocketT>
void BasicSession<DecoderT, SocketT>::closeSocket(DisconnectReason _reason)
{
    // Take a local strong reference before touching the socket, for the same reason drop() does:
    // a concurrent setSocket(nullptr) must not turn this into a null dereference mid-teardown.
    auto socket = m_socket;
    if (!socket || !socket->isConnected())
    {
        return;
    }
    try
    {
        if (_reason == DisconnectRequested || _reason == DuplicatePeer || _reason == ClientQuit ||
            _reason == UserReason)
        {
            SESSION_LOG(DEBUG) << "[drop] closing remote " << socket->remoteEndpoint()
                               << LOG_KV("reason", reasonOf(_reason))
                               << LOG_KV("endpoint", socket->nodeIPEndpoint());
        }
        else
        {
            SESSION_LOG(INFO) << "[drop] closing remote " << socket->remoteEndpoint()
                              << LOG_KV("reason", reasonOf(_reason))
                              << LOG_KV("endpoint", socket->nodeIPEndpoint());
        }

        /// if get Host object failed, close the socket directly
        if (socket->isConnected())
        {
            socket->close();
        }
        auto shutdown_timer = std::make_shared<boost::asio::steady_timer>(
            socket->ioService(), std::chrono::milliseconds(m_shutDownTimeThres));
        /// async wait for shutdown
        shutdown_timer->async_wait([socket](const boost::system::error_code& error) {
            /// drop operation has been aborted
            if (error == boost::asio::error::operation_aborted)
            {
                SESSION_LOG(DEBUG)
                    << "[drop] operation_aborted  by async_shutdown"
                    << LOG_KV("value", error.value()) << LOG_KV("message", error.message());
                return;
            }
            /// shutdown timer error
            if (error && error != boost::asio::error::operation_aborted)
            {
                SESSION_LOG(WARNING)
                    << "[drop] shutdown timer failed" << LOG_KV("failedValue", error.value())
                    << LOG_KV("message", error.message());
            }
            /// force to shutdown when timeout
            if (socket->ref().is_open())
            {
                SESSION_LOG(WARNING) << "[drop] timeout, force close the socket"
                                     << LOG_KV("remote endpoint", socket->remoteEndpoint());
                socket->close();
            }
        });

        /// async shutdown normally
        socket->sslref().async_shutdown(
            [socket, shutdown_timer](const boost::system::error_code& error) {
                shutdown_timer->cancel();
                if (error)
                {
                    SESSION_LOG(INFO)
                        << "[drop] shutdown failed " << LOG_KV("failedValue", error.value())
                        << LOG_KV("message", error.message());
                }
                /// force to close the socket
                if (socket->ref().is_open())
                {
                    SESSION_LOG(WARNING) << LOG_DESC("force to shutdown session")
                                         << LOG_KV("endpoint", socket->nodeIPEndpoint());
                    socket->close();
                }
            });
    }
    catch (...)
    {
        SESSION_LOG(ERROR) << LOG_DESC("drop error")
                           << LOG_KV("endpoint", socket->nodeIPEndpoint());
    }
}

template <FrameDecoder DecoderT, typename SocketT>
void BasicSession<DecoderT, SocketT>::disconnect(DisconnectReason _reason)
{
    drop(_reason);
}

template <FrameDecoder DecoderT, typename SocketT>
void BasicSession<DecoderT, SocketT>::start()
{
    // Production read policy (default): the read loop compiles against
    // ASIOInterface::DefaultReadPolicy — a direct async_read_some call. Read-loop test fakes
    // that want to park reads call the templated startWithPolicy<FakePolicy>() instead (see
    // SessionReadLoop.h).
    startWithPolicy<ASIOInterface::DefaultReadPolicy>();
}

template <FrameDecoder DecoderT, typename SocketT>
bool BasicSession<DecoderT, SocketT>::checkRead(boost::system::error_code _ec)
{
    if (_ec && _ec.category() != boost::asio::error::get_misc_category() &&
        _ec.value() != boost::asio::error::eof)
    {
        SESSION_LOG(WARNING) << LOG_DESC("checkRead error") << LOG_KV("message", _ec.message());
        drop(TCPError);

        return false;
    }

    return true;
}


template <FrameDecoder DecoderT, typename SocketT>
void BasicSession<DecoderT, SocketT>::onMessage(NetworkException const& e, FrameMeta meta)
{
    m_server.get().asioInterface()->post(
        [self = this->weak_from_this(), e, meta = std::move(meta)]() mutable {
            try
            {
                auto session = self.lock();
                if (!session)
                {
                    return;
                }
                // in-activate session
                if (!session->m_active || !session->m_server.get().haveNetwork())
                {
                    return;
                }
                // Every decoded frame goes to the message handler: interpreting the metadata
                // (is this a response? is it addressed to this node?) is the protocol layer's
                // policy — libnetwork only transports frames. The handler drives the
                // response-correlation mechanism through claimResponse when it wants it.
                session->m_messageHandler(e, session, std::move(meta));
            }
            catch (std::exception const& e)
            {
                SESSION_LOG(WARNING) << LOG_BADGE("onMessage") << LOG_DESC("onMessage exception")
                                     << LOG_KV("msg", boost::diagnostic_information(e));
            }
        });
}

template <FrameDecoder DecoderT, typename SocketT>
bool BasicSession<DecoderT, SocketT>::claimResponse(NetworkException const& e, FrameMeta meta)
{
    auto callbackPtr = m_sessionCallbackManager.get().getCallback(meta.seq, true);
    // without callback: it already timed out or was settled elsewhere
    if (!callbackPtr)
    {
        SESSION_LOG(WARNING) << LOG_BADGE("claimResponse")
                             << LOG_DESC("callback not found, maybe the callback timeout")
                             << LOG_KV("endpoint", nodeIPEndpoint())
                             << LOG_KV("seq", meta.seq) << LOG_KV("resp", meta.isResp);
        return false;
    }
    // erase on the session that REGISTERED the seq: the callback manager is host-shared, so the
    // response can be claimed on a different session than the request went out on — erasing here
    // would miss the owner's bookkeeping
    if (auto owner = callbackPtr->owner.lock())
    {
        owner->removePendingResponseSeq(meta.seq);
    }

    // with callback
    if (callbackPtr->timeoutHandler)
    {
        callbackPtr->timeoutHandler->cancel();
    }
    auto& callback = callbackPtr->callback;
    if (!callback)
    {
        return false;
    }
    callback(e, std::move(meta));
    return true;
}

template <FrameDecoder DecoderT, typename SocketT>
void BasicSession<DecoderT, SocketT>::onTimeout(const boost::system::error_code& error, uint32_t seq)
{
    if (error)
    {
        // SESSION_LOG(TRACE) << "timer cancel" << error;
        return;
    }

    typename ResponseCallback<BasicSession<DecoderT, SocketT>>::Ptr callback =
        m_sessionCallbackManager.get().getCallback(seq, true);
    if (!callback)
    {
        return;
    }
    removePendingResponseSeq(seq);
    NetworkException e = makeNetworkException(P2PExceptionType::NetworkTimeout, "NetworkTimeout");
    callback->callback(e, std::nullopt);
}

template <FrameDecoder DecoderT, typename SocketT>
void BasicSession<DecoderT, SocketT>::checkNetworkStatus()
{
    m_idleCheckTimer->restart();
    try
    {
        auto now = utcSteadyTime();
        // read idle
        if ((m_lastReadTime + m_idleTimeInterval) < now)
        {
            SESSION_LOG(WARNING) << LOG_DESC(
                                        "Long time without read operation, maybe session "
                                        "inactivated, drop the session")
                                 << LOG_KV("endpoint", m_socket->nodeIPEndpoint());
            drop(IdleWaitTimeout);
            return;
        }
        // write idle
        if ((m_lastWriteTime + m_idleTimeInterval) < now)
        {
            SESSION_LOG(WARNING) << LOG_DESC(
                                        "Long time without write operation, maybe session "
                                        "inactivated, drop the session")
                                 << LOG_KV("endpoint", m_socket->nodeIPEndpoint());
            drop(IdleWaitTimeout);
            return;
        }
    }
    catch (std::exception const& e)
    {
        SESSION_LOG(WARNING) << LOG_DESC("checkNetworkStatus error")
                             << LOG_KV("msg", boost::diagnostic_information(e));
    }
}

namespace detail
{
template <FrameDecoder DecoderT, typename SocketT, typename View>
task::Task<std::optional<FrameMeta>> fastSendMessageWithResponse(
    BasicSession<DecoderT, SocketT>& session, uint32_t seq, View& view, Options& options)
{
    auto sessionPtr = session.shared_from_this();
    // Result slot: lives in this coroutine frame; ack / timeout / drop-flush / write-failure all
    // complete it through the registered callback. The frame stays alive across the whole
    // send + wait, so the callback's captured reference stays valid until the waiter has been
    // completed and the callback removed from the manager (every exit path below removes it).
    typename task::GetResultAwaitable<NetworkException, std::optional<FrameMeta>>::Result result;

    // register the response callback before sending: ack / timeout / drop-flush complete result
    auto handler = std::make_shared<ResponseCallback<BasicSession<DecoderT, SocketT>>>();
    handler->callback = [&result](NetworkException exception, std::optional<FrameMeta> response) {
        task::GetResultAwaitable<NetworkException, std::optional<FrameMeta>>::complete(
            result, std::move(exception), std::move(response));
    };
    if (options.timeout > 0)
    {
        handler->timeoutHandler.emplace(
            session.m_server.get().asioInterface()->newTimer(options.timeout));
        auto weakSession = std::weak_ptr<BasicSession<DecoderT, SocketT>>(sessionPtr);
        handler->timeoutHandler->async_wait(
            [weakSession, seq](const boost::system::error_code& _error) {
                try
                {
                    if (auto session = weakSession.lock())
                    {
                        session->onTimeout(_error, seq);
                    }
                }
                catch (std::exception const& e)
                {
                    SESSION_LOG(WARNING) << LOG_DESC("async_wait exception")
                                         << LOG_KV("message", boost::diagnostic_information(e));
                }
            });
        handler->startTime = utcSteadyTime();
    }
    handler->owner = sessionPtr;
    auto& callbackManager = session.m_sessionCallbackManager.get();
    callbackManager.addCallback(seq, std::move(handler));
    sessionPtr->addPendingResponseSeq(seq);

    // Coroutine send: suspends until the zero-copy async_write completed (or failed), i.e. the
    // frame's buffers are no longer referenced -- this replaces the writeDone half of ResumeGate.
    auto ec = co_await detail::send(session, ::ranges::views::all(view));
    if (ec.failed())
    {
        // Write failed (incl. session already down -> send returns not_connected): no ack can
        // arrive. Claim the registered callback back -- unless a concurrent ack/timeout/drop-flush
        // already claimed it, in which case the result is already completed and the wait below
        // returns it inline. Either way the callback never outlives this frame.
        sessionPtr->removePendingResponseSeq(seq);
        auto claimed = callbackManager.getCallback(seq, true);
        if (claimed)
        {
            if (claimed->timeoutHandler)
            {
                claimed->timeoutHandler->cancel();
            }
            task::GetResultAwaitable<NetworkException, std::optional<FrameMeta>>::complete(result,
                makeNetworkException(ec.value(), ec.message()), std::nullopt);
        }
    }

    // wait for ack / timeout / drop-flush (returns inline when already completed above)
    auto [exception, response] =
        co_await task::GetResultAwaitable<NetworkException, std::optional<FrameMeta>>(result);
    if (errorCodeOf(exception) != 0)
    {
        BOOST_THROW_EXCEPTION(exception);
    }
    co_return std::move(response);
}

template <typename View>
task::Task<void> fastSendMessageWithoutResponse(auto& session, View view)
{
    auto errorCode = co_await detail::send(session, ::ranges::views::all(view));
    if (errorCode.failed())
    {
        BOOST_THROW_EXCEPTION(makeNetworkException(errorCode.value(), errorCode.message()));
    }
    co_return;
}
}  // namespace detail

template <FrameDecoder DecoderT, typename SocketT>
template <::ranges::input_range Payloads>
    requires std::convertible_to<::ranges::range_reference_t<Payloads>, bytesConstRef>
task::Task<std::optional<FrameMeta>> BasicSession<DecoderT, SocketT>::fastSendMessage(
    bytesConstRef header, Payloads payloads, uint32_t seq, Options options)
{
    if (!active())
    {
        SESSION_LOG(WARNING) << "Session inactive";
        co_return {};
    }

    // Materialize the payload views once: the input range may be single-pass, while the length
    // pass below iterates the payloads. Copying the (cheap) views into a forward container makes
    // every later pass multi-pass safe without changing the interface contract.
    boost::container::small_vector<bytesConstRef, 3> payloadRefs;
    for (auto const& ref : payloads)
    {
        payloadRefs.push_back(ref);
    }

    // The frame on the wire is header + payloads, zero-copy: both stay as views into
    // caller-owned buffers that outlive this coroutine (the caller co_awaits the task).
    auto view = ::ranges::views::concat(
        ::ranges::views::single(header), ::ranges::views::all(payloadRefs));
    uint32_t totalLength = header.size();
    for (auto ref : payloadRefs)
    {
        totalLength += ref.size();
    }

    if (totalLength > allowMaxMsgSize())
    {
        SESSION_LOG(WARNING) << LOG_BADGE("fastSendMessage") << LOG_DESC("msg size overflow")
                             << LOG_KV("msgSize", totalLength)
                             << LOG_KV("allowMaxMsgSize", allowMaxMsgSize());
        BOOST_THROW_EXCEPTION(makeNetworkException(-1, "Msg size overflow"));
    }

    if (c_fileLogLevel <= LogLevel::TRACE)
    {
        SESSION_LOG(TRACE) << LOG_DESC("Session fastSendMessage")
                           << LOG_KV("endpoint", nodeIPEndpoint()) << LOG_KV("seq", seq)
                           << LOG_KV("wireLength", totalLength) << LOG_KV("this", this);
    }
    if (options.response)
    {
        co_return co_await detail::fastSendMessageWithResponse(*this, seq, view, options);
    }
    else
    {
        co_await detail::fastSendMessageWithoutResponse(*this, std::move(view));
        co_return {};
    }
}

template <FrameDecoder DecoderT, typename SocketT>
Host<DecoderT, SocketT>& BasicSession<DecoderT, SocketT>::host()
{
    return m_server;
}
template <FrameDecoder DecoderT, typename SocketT>
std::shared_ptr<SocketT> BasicSession<DecoderT, SocketT>::socket()
{
    return m_socket;
}
template <FrameDecoder DecoderT, typename SocketT>
void BasicSession<DecoderT, SocketT>::setSocket(const std::shared_ptr<SocketT>& socket)
{
    m_socket = socket;
}
template <FrameDecoder DecoderT, typename SocketT>
SessionCallbackManager<BasicSession<DecoderT, SocketT>>&
BasicSession<DecoderT, SocketT>::sessionCallbackManager() const
{
    return m_sessionCallbackManager;
}
template <FrameDecoder DecoderT, typename SocketT>
const std::function<void(NetworkException, std::shared_ptr<BasicSession<DecoderT, SocketT>>,
    FrameMeta)>&
BasicSession<DecoderT, SocketT>::messageHandler()
{
    return m_messageHandler;
}
template <FrameDecoder DecoderT, typename SocketT>
void BasicSession<DecoderT, SocketT>::setMessageHandler(
    std::function<void(NetworkException, std::shared_ptr<BasicSession<DecoderT, SocketT>>,
        FrameMeta)> messageHandler)

{
    m_messageHandler = std::move(messageHandler);
}
template <FrameDecoder DecoderT, typename SocketT>
uint32_t BasicSession<DecoderT, SocketT>::maxReadDataSize() const
{
    return m_maxReadDataSize;
}
template <FrameDecoder DecoderT, typename SocketT>
void BasicSession<DecoderT, SocketT>::setMaxReadDataSize(uint32_t _maxReadDataSize)
{
    m_maxReadDataSize = _maxReadDataSize;
}
template <FrameDecoder DecoderT, typename SocketT>
uint32_t BasicSession<DecoderT, SocketT>::maxSendDataSize() const
{
    return m_maxSendDataSize;
}
template <FrameDecoder DecoderT, typename SocketT>
void BasicSession<DecoderT, SocketT>::setMaxSendDataSize(uint32_t _maxSendDataSize)
{
    m_maxSendDataSize = _maxSendDataSize;
}
template <FrameDecoder DecoderT, typename SocketT>
uint32_t BasicSession<DecoderT, SocketT>::allowMaxMsgSize() const
{
    return m_allowMaxMsgSize;
}
template <FrameDecoder DecoderT, typename SocketT>
void BasicSession<DecoderT, SocketT>::setAllowMaxMsgSize(uint32_t _allowMaxMsgSize)
{
    m_allowMaxMsgSize = _allowMaxMsgSize;
}
template <FrameDecoder DecoderT, typename SocketT>
SessionRecvBuffer& BasicSession<DecoderT, SocketT>::recvBuffer()
{
    return m_recvBuffer;
}
template <FrameDecoder DecoderT, typename SocketT>
const SessionRecvBuffer& BasicSession<DecoderT, SocketT>::recvBuffer() const
{
    return m_recvBuffer;
}
template <FrameDecoder DecoderT, typename SocketT>
std::shared_ptr<BasicSession<DecoderT, SocketT>>
BasicSessionFactory<DecoderT, SocketT>::createSession(
    Host<DecoderT, SocketT>& _server, std::shared_ptr<SocketT> const& _socket)
{
    std::shared_ptr<BasicSession<DecoderT, SocketT>> session =
        std::make_shared<BasicSession<DecoderT, SocketT>>(_socket, _server, m_sessionRecvBufferSize);
    session->setAllowMaxMsgSize(m_allowMaxMsgSize);
    session->setMaxReadDataSize(m_maxReadDataSize);
    session->setMaxSendDataSize(m_maxSendDataSize);
    BCOS_LOG(INFO) << LOG_BADGE("BasicSessionFactory") << LOG_DESC("create new session")
                   << LOG_KV("sessionRecvBufferSize", m_sessionRecvBufferSize)
                   << LOG_KV("allowMaxMsgSize", m_allowMaxMsgSize)
                   << LOG_KV("maxReadDataSize", m_maxReadDataSize)
                   << LOG_KV("maxSendDataSize", m_maxSendDataSize);
    return session;
}
}  // namespace bcos::gateway
