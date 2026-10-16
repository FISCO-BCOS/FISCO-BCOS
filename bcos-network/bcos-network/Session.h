
/** @file Session.h
 * @author monan <651932351@qq.com>
 * @date 2018
 */

#pragma once

#include "bcos-network/ASIOInterface.h"
#include "bcos-network/Common.h"
#include "bcos-network/FrameMeta.h"
#include "bcos-network/Socket.h"
#include "bcos-task/Channel.h"
#include "bcos-task/Task.h"
#include "bcos-task/Wait.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/Timer.h"
#include <boost/asio/buffer.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/container/small_vector.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/heap/priority_queue.hpp>
#include <boost/throw_exception.hpp>
#include <range/v3/range/concepts.hpp>
#include <range/v3/view/all.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <deque>
#include <functional>
#include <iterator>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>


namespace bcos::network
{
// The default argument lives on this first declaration; Host.h's definition does not repeat it.
// SocketT is the socket type of the sessions this host creates (production: Socket — the TLS
// alias from Socket.h; tests instantiate fakes).
template <FrameDecoder DecoderT, typename SocketT = Socket>
class Host;

// The default argument lives on this first declaration; the definition below does not repeat it.
template <FrameDecoder DecoderT, typename SocketT = Socket>
class BasicSession;

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
    /// Take-buffer handover for large frames (FrameMeta::takeBuffer): moves the whole storage
    /// into `out` with the frame at [old readPos, old readPos + frameLen), reseeds this buffer
    /// with just the tail bytes after the frame, and returns the frame's start offset (the old
    /// readPos). The caller (the read loop) must NOT call onRead for this frame afterwards —
    /// the consumed bytes left with `out`.
    std::size_t takeStorage(bytes& out, std::size_t frameLen);
    bcos::bytesConstRef asReadBuffer() const;
    bcos::bytesRef asWriteBuffer();

private:
    /// Minimum size of the fresh buffer reseeded by takeStorage — must be nonzero so the next
    /// read has somewhere to land; kept small since the NeedMoreData grow path re-expands it.
    constexpr static std::size_t TAKE_STORAGE_FLOOR = 4 * 1024;

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

namespace detail
{
// Declared here so BasicSession::sendMessage can name it (qualified lookup happens at the
// point of definition); defined after the class, once BasicSession is complete.
template <FrameDecoder DecoderT, typename SocketT, ::ranges::input_range Payloads>
task::Task<boost::system::error_code> send(
    BasicSession<DecoderT, SocketT>& session, Payloads payloads);
}  // namespace detail

// The session machinery (read loop, batched write loop) is a pure frame transport, generic over
// the frame DECODER only — the session never sees a concrete message type. Inbound, DecoderT
// splits the byte stream into FrameMeta (see FrameMeta.h) and the consumer pulls them with
// recvMessage(); outbound, the caller hands over the frame as an ordered list of byte-segment
// views in wire order. Request/response correlation is NOT this layer's
// business: it lives in libp2p (Service's pending-request table), driven by the Service receive
// pump that sits on recvMessage(). SocketT is the socket type (production: Socket, the TLS flavour
// of BasicSocket; PlainSocket is plaintext TCP; tests instantiate fakes). The gateway
// instantiates this with libp2p's P2PDecoder.
template <FrameDecoder DecoderT, typename SocketT>
class BasicSession : public std::enable_shared_from_this<BasicSession<DecoderT, SocketT>>
{
public:
    using DecoderType = DecoderT;

    // Grow ceiling: the recv buffer never grows beyond this (see the read-loop grow path).
    constexpr static const std::size_t MIN_SESSION_RECV_BUFFER_SIZE = 512 * 1024UL;
    // Inbound pull-queue bounds (recvMessage): a consumer slower than the peer lets decoded
    // frames pile up to these limits, then the read loop PARKS (co_await waitWritable) and the
    // TCP receive window throttles the sender — backpressure instead of disconnecting a merely
    // slow consumer. The byte cap matches the old 2 × allowMaxMsgSize receive-buffer ceiling.
    constexpr static const std::size_t MAX_RECV_QUEUE_FRAMES = 1024;
    constexpr static const std::size_t MAX_RECV_QUEUE_BYTES = 64 * 1024 * 1024UL;
    // FIB-184: initial recv-buffer size for a freshly created session. Previously every
    // session unconditionally allocated MIN_SESSION_RECV_BUFFER_SIZE (512KB) up front, so a
    // flood of unauthenticated/short-lived sessions caused heap exhaustion. Start small and
    // rely on the existing grow path (the read loop grows up to m_maxRecvBufferSize) to expand
    // only for sessions that actually carry large messages. Must stay well above the message
    // header length so the first read can always make forward progress.
    constexpr static const std::size_t INITIAL_SESSION_RECV_BUFFER_SIZE = 16 * 1024UL;

    BasicSession(std::shared_ptr<SocketT> socket, Host<DecoderT, SocketT>& server,
        size_t _recvBufferSize = INITIAL_SESSION_RECV_BUFFER_SIZE, bool _forceSize = false)
      : m_maxRecvBufferSize(std::max<size_t>(_recvBufferSize, MIN_SESSION_RECV_BUFFER_SIZE)),
        // FIB-184: treat _recvBufferSize as the grow CEILING, not the initial allocation.
        // Production createSession passes the config-validated session_recv_buffer_size, which is
        // forced to 2 * allow_max_msg_size = 64MB; allocating that per session up front let
        // authenticated TLS connect/close churn exhaust the heap (SIGSEGV inside malloc during
        // Session construction). Allocate only INITIAL_SESSION_RECV_BUFFER_SIZE (16KB) initially
        // and let the read loop grow the buffer up to m_maxRecvBufferSize on demand, so only
        // sessions that actually carry large messages pay for a large buffer. _forceSize keeps
        // the exact size for tests that assert a specific small buffer.
        m_recvBuffer(_forceSize ? _recvBufferSize :
                                  std::min<size_t>(_recvBufferSize, INITIAL_SESSION_RECV_BUFFER_SIZE)),
        m_server(server),
        m_socket(std::move(socket)),
        // The poster runs consumer wake-ups on the shared IO pool while the host is alive and
        // inline once it is gone — the same settlement policy as postCallback: a posted task
        // would never run after teardown, hanging every parked recvMessage().
        m_recvChannel(
            [this](std::function<void()> wake) {
                if (m_server.get().haveNetwork())
                {
                    m_server.get().asioInterface()->post(std::move(wake));
                }
                else
                {
                    wake();
                }
            },
            MAX_RECV_QUEUE_FRAMES, MAX_RECV_QUEUE_BYTES,
            [](const FrameMeta& meta) { return meta.frame.size(); }),
        m_idleCheckTimer(
            std::make_shared<Timer>(m_socket->ioService(), m_idleTimeInterval, "idleChecker"))
    {
        SESSION_LOG(INFO) << "[Session::Session] this=" << this
                          << LOG_KV("recvBufferSize", m_maxRecvBufferSize);
    }

    BasicSession(const BasicSession&) = delete;
    BasicSession(BasicSession&&) = delete;
    BasicSession& operator=(BasicSession&&) = delete;
    BasicSession& operator=(const BasicSession&) = delete;
    ~BasicSession() noexcept
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

    using Ptr = std::shared_ptr<BasicSession>;

    void start()
    {
        // Production read policy (default): the read loop compiles against
        // ASIOInterface::DefaultReadPolicy — a direct async_read_some call. Read-loop test fakes
        // that want to park reads call the templated startWithPolicy<FakePolicy>() instead.
        startWithPolicy<ASIOInterface::DefaultReadPolicy>();
    }

    // Read-policy seam (compile-time): identical lifecycle to start(), but the read loop is
    // compiled against an explicit ReadPolicy so read-loop test fakes can inject a policy that
    // parks / controls read completions (see ASIOInterface::awaitableReadSome). Production call
    // sites use start() (the default policy), which delegates here with
    // ASIOInterface::DefaultReadPolicy — this template adds no runtime cost in production. Tests
    // instantiate it with a fake policy, e.g. session->startWithPolicy<FakeASIO::ReadPolicy>().
    template <typename ReadPolicy>
    void startWithPolicy()
    {
        SESSION_LOG(INFO) << "[Session::start] this=" << this;
        if (!m_active && m_server.get().haveNetwork())
        {
            m_active = true;
            m_lastWriteTime.store(utcSteadyTime());
            m_lastReadTime.store(utcSteadyTime());
            // fire-and-forget: the detached task owns the coroutine chain; readLoop() holds the
            // session alive in its frame (FIB-184) and exits on error, drop or shutdown.
            task::wait(readLoop<ReadPolicy>());
        }

        auto self = this->weak_from_this();
        // Idle-drop disabled (setIdleTimeInterval(0)): never arm the idle checker, so the
        // session is kept alive regardless of read/write inactivity.
        if (m_idleTimeInterval > 0)
        {
            m_idleCheckTimer->registerTimeoutHandler([self]() {
                auto session = self.lock();
                if (session)
                {
                    session->checkNetworkStatus();
                }
            });
            m_idleCheckTimer->start();
        }
    }

    void disconnect(DisconnectReason _reason) { drop(_reason); }

    // payloads: any input range of byte-view-like elements (bytesConstRef-convertible) — the
    // complete frame, as an ordered list of byte segments in wire order. The session is a pure
    // frame transport and does not know the frame's structure (header vs payload is the
    // protocol layer's business; a caller with a header prepends it to the range itself).
    // A plain range, NOT a type-erased any_view: the send path is header-only and instantiated
    // per caller view type, so the per-element access compiles down to direct reads. Taken by
    // value: views are cheap to copy, and a by-value parameter is moved into the coroutine frame,
    // so the view stays valid no matter how the caller's temporaries are scoped.
    //
    // Pure frame send: co_await completes when the frame's buffers have been handed to the socket
    // (or the write failed). Returns false when the session was already inactive at entry;
    // throws NetworkException on a write failure or an oversized frame. Nothing about
    // request/response correlation is known here — that is the protocol layer's business.
    template <::ranges::input_range Payloads>
        requires std::convertible_to<::ranges::range_reference_t<Payloads>, bytesConstRef>
    task::Task<bool> sendMessage(Payloads payloads)
    {
        if (!active())
        {
            SESSION_LOG(WARNING) << "Session inactive";
            co_return false;
        }

        // Materialize the payload views once: the input range may be single-pass, while the send
        // below iterates the payloads again. Copying the (cheap) views into a forward container
        // makes every later pass multi-pass safe without changing the interface contract. The
        // frame on the wire is the concatenation of these segments, zero-copy: they stay views
        // into caller-owned buffers that outlive this coroutine (the caller co_awaits the task).
        boost::container::small_vector<bytesConstRef, 3> payloadRefs;
        uint32_t totalLength = 0;
        for (auto const& ref : payloads)
        {
            totalLength += ref.size();
            payloadRefs.push_back(ref);
        }

        if (totalLength > allowMaxMsgSize())
        {
            SESSION_LOG(WARNING) << LOG_BADGE("sendMessage") << LOG_DESC("msg size overflow")
                                 << LOG_KV("msgSize", totalLength)
                                 << LOG_KV("allowMaxMsgSize", allowMaxMsgSize());
            BOOST_THROW_EXCEPTION(makeNetworkException(-1, "Msg size overflow"));
        }

        if (c_fileLogLevel <= LogLevel::TRACE)
        {
            SESSION_LOG(TRACE) << LOG_DESC("Session sendMessage")
                               << LOG_KV("endpoint", nodeIPEndpoint())
                               << LOG_KV("wireLength", totalLength) << LOG_KV("this", this);
        }

        // Coroutine send: suspends until the zero-copy async_write completed (or failed), i.e. the
        // frame's buffers are no longer referenced.
        auto errorCode = co_await detail::send(*this, ::ranges::views::all(payloadRefs));
        if (errorCode.failed())
        {
            BOOST_THROW_EXCEPTION(makeNetworkException(errorCode.value(), errorCode.message()));
        }
        co_return true;
    }

    NodeIPEndpoint nodeIPEndpoint() const { return m_socket->nodeIPEndpoint(); }

    bool active() const { return active(m_server); }

    bool active(Host<DecoderT, SocketT>& server) const
    {
        return m_active && server.haveNetwork() && m_socket && m_socket->isConnected();
    }

    std::size_t writeQueueSize()
    {
        // Exact now that the queue is mutex-guarded (on the tbb::concurrent_queue this was a 0/1
        // "is non-empty" probe); the only caller is Service heartbeat logging.
        std::lock_guard<std::mutex> const lock(x_writeQueue);
        return m_writeQueue.size();
    }

    // Enqueue one outbound payload for the write loop. Thread-safe: callable from any producer
    // thread.
    void pushWriteQueue(Payload payload)
    {
        std::lock_guard<std::mutex> const lock(x_writeQueue);
        m_writeQueue.push_back(std::move(payload));
    }

    // Move every queued payload out in FIFO order and clear the queue. Callbacks are NOT invoked
    // here — the caller settles them AFTER the queue mutex has been released (postCallback may
    // run a callback inline once the host is gone, which must never happen under the lock).
    std::vector<Payload> drainWriteQueue()
    {
        std::lock_guard<std::mutex> const lock(x_writeQueue);
        std::vector<Payload> drained(std::make_move_iterator(m_writeQueue.begin()),
            std::make_move_iterator(m_writeQueue.end()));
        m_writeQueue.clear();
        return drained;
    }

    bool writeQueueEmpty()
    {
        std::lock_guard<std::mutex> const lock(x_writeQueue);
        return m_writeQueue.empty();
    }

    Host<DecoderT, SocketT>& host() { return m_server; }

    std::shared_ptr<SocketT> socket() { return m_socket; }
    void setSocket(const std::shared_ptr<SocketT>& socket) { m_socket = socket; }

    // Pull interface for inbound frames: suspends until the read loop delivers the next decoded
    // frame. Once the session is dropped, already-queued frames remain drainable, then recv()
    // rethrows the NetworkException the channel was closed with (disconnect reason, protocol
    // error). Runs the consumer's continuation on the shared IO pool via the channel poster.
    task::Task<FrameMeta> recvMessage() { co_return co_await m_recvChannel.recv(); }

    // FIB-184: attach an opaque object whose lifetime is bound to this session. It is destroyed
    // exactly when the session object is destroyed, which Host uses to release a session-cap
    // slot (the guard's destructor decrements the Host counters). Kept opaque so bcos-network
    // does not depend on the accounting type.
    void setLifetimeGuard(std::shared_ptr<void> _guard)
    {
        m_lifetimeGuard = std::move(_guard);
    }

    uint32_t maxReadDataSize() const { return m_maxReadDataSize; }
    void setMaxReadDataSize(uint32_t _maxReadDataSize) { m_maxReadDataSize = _maxReadDataSize; }

    uint32_t maxSendDataSize() const { return m_maxSendDataSize; }
    void setMaxSendDataSize(uint32_t _maxSendDataSize) { m_maxSendDataSize = _maxSendDataSize; }

    uint32_t allowMaxMsgSize() const { return m_allowMaxMsgSize; }
    void setAllowMaxMsgSize(uint32_t _allowMaxMsgSize) { m_allowMaxMsgSize = _allowMaxMsgSize; }

    SessionRecvBuffer& recvBuffer() { return m_recvBuffer; }
    const SessionRecvBuffer& recvBuffer() const { return m_recvBuffer; }

    // FIB-184 (review): grow ceiling for the recv buffer, set from the config-validated size at
    // construction. Exposed read-only; the member itself is private (below) so external callers
    // can read but never widen this security bound.
    std::size_t maxRecvBufferSize() const { return m_maxRecvBufferSize; }

    // Idle-drop policy (checkNetworkStatus). Default 60s keeps the legacy behaviour; 0 disables
    // the idle check entirely: startWithPolicy() then never arms m_idleCheckTimer and
    // checkNetworkStatus() early-returns. Non-zero values also retarget the timer's period, so
    // set before start()/startWithPolicy() (the intended use) or accept that an already-armed
    // timer picks the new period up on its next restart.
    void setIdleTimeInterval(uint64_t _idleTimeIntervalMs)
    {
        m_idleTimeInterval = _idleTimeIntervalMs;
        m_idleCheckTimer->setTimeout(static_cast<int64_t>(_idleTimeIntervalMs));
    }
    uint64_t idleTimeInterval() const { return m_idleTimeInterval; }

    // Read timeout (FrameMeta::Status::Timeout delivery). 0 = disabled (default): the read loop
    // blocks in awaitableReadSome indefinitely. When > 0, a read with no bytes arriving within
    // m_readTimeoutMs is cancelled and surfaced to the consumer as a FrameMeta{Status::Timeout}
    // frame (empty frame, consumed = 0) WITHOUT dropping the session — the consumer decides what
    // a stalled read means. Set before start()/startWithPolicy(); the read loop reads the value
    // each iteration, so later changes take effect on the next read.
    void setReadTimeoutMs(uint32_t _readTimeoutMs) { m_readTimeoutMs = _readTimeoutMs; }
    uint32_t readTimeoutMs() const { return m_readTimeoutMs; }
    /**
     * @brief The packets that can be sent are obtained based on the configured policy
     *
     * @param encodedMsgs
     * @param _maxSendDataSize
     * @return bool
     */
    bool tryPopSomeEncodedMsgs(std::vector<Payload>& encodedMsgs, size_t _maxSendDataSize)  // NOLINT
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
        // Floor the budget at 1 here — the point where the invariant "the byte budget is never 0"
        // is actually needed. The GatewayConfig clamp warns the operator, but Session::
        // setMaxSendDataSize stores whatever it is given, and a 0 budget makes the while below
        // never enter: the pop loop returns false, writeLoop breaks on its first iteration and
        // every outbound write on the session stalls silently and permanently.
        size_t budget = std::max<size_t>(_maxSendDataSize, 1);
        // One lock acquisition per batch: move the payloads out under the queue mutex. No callback
        // is invoked here — the batch callbacks are settled by writeLoop after the write
        // completes.
        std::lock_guard<std::mutex> const lock(x_writeQueue);
        while (totalDataSize < budget && !m_writeQueue.empty())
        {
            totalDataSize += m_writeQueue.front().size();
            encodedMsgs.emplace_back(std::move(m_writeQueue.front()));
            m_writeQueue.pop_front();
        }

        return totalDataSize > 0;
    }

    void checkNetworkStatus()
    {
        // Belt-and-braces for the disabled idle check (setIdleTimeInterval(0)): startWithPolicy
        // already skipped arming the timer in that case.
        if (m_idleTimeInterval == 0)
        {
            return;
        }
        m_idleCheckTimer->restart();
        try
        {
            auto now = utcSteadyTime();
            // read idle. Backpressure is NOT idleness: while the read loop is parked on a full
            // recv queue (waiting for the consumer to drain) no reads complete, but the session
            // is live — a non-empty queue is the signal. The readLoop also refreshes
            // m_lastReadTime when it resumes from a park, closing the drain-to-empty race.
            if ((m_lastReadTime + m_idleTimeInterval) < now && m_recvChannel.size() == 0)
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
    void drop(DisconnectReason _reason)
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
        // chains (sendMessage senders). Without this, a session that disappears before the
        // write completes would leak the whole task::wait chain — including the strong refs to the
        // session/socket/service it captured (a broadcast fan-out multiplies this by the peer
        // count). In-flight writes (the write loop's current batch) are covered by the write loop
        // (BasicSession::writeLoop), which invokes their callbacks with the error when the socket
        // close cancels the write.
        // Also covers BasicSession::write()'s early returns: they call drop(TCPError) right after
        // the payload has been pushed into m_writeQueue.
        //
        // FIB-185 (review): complete these callbacks OFF the caller's stack whenever an executor
        // is available. writeLoop never runs on a sender's stack anymore (BasicSession::write()
        // posts its launch), but drop() remains reachable from arbitrary caller stacks — including
        // write()'s launch-failure catch, which can still sit inside a sender's await_suspend — so
        // calling the callback inline here could resume a coroutine from inside its own
        // await_suspend. postCallback posts to the shared pool and falls back to inline only when
        // the host is already gone — the same shape as the notifyDisconnect / closeSocket branches
        // below.
        // drainWriteQueue moves the payloads out under the queue mutex; the callbacks are settled
        // here, after the mutex has been released (postCallback may run a callback inline once the
        // host is gone, which must never happen under the lock).
        for (auto& payload : drainWriteQueue())
        {
            if (payload.m_callback)
            {
                postCallback(std::move(payload.m_callback), "write callback exception during drop",
                    boost::asio::error::operation_aborted);
            }
        }

        // Note: pending request/response waiters are NOT failed here — correlation lives in libp2p
        // (Service's pending-request table). The channel close below reaches the Service receive
        // pump, whose exit path fails exactly the waiters whose outbound session this is.
        int errorCode = P2PExceptionType::Disconnect;
        std::string errorMsg = "Disconnect";
        if (_reason == DuplicatePeer)
        {
            errorCode = P2PExceptionType::DuplicateSession;
            errorMsg = "DuplicateSession";
        }

        // Pull-mode teardown notification: closing the recv channel wakes the parked consumer
        // (the Service receive pump) with the disconnect error; frames already queued remain
        // drainable ahead of the error. The consumer's resume is posted through the channel's
        // poster, so no consumer code runs on this (arbitrary caller's) stack — the property the
        // old push-mode teardown executor (FIB-186) existed to provide. Done BEFORE the
        // null-socket return below: a parked recvMessage() must be woken even when the socket is
        // already gone.
        m_recvChannel.close(std::make_exception_ptr(makeNetworkException(errorCode, errorMsg)));

        // Guard against null socket (e.g. test sets it to nullptr before destructor)
        if (!socket)
        {
            return;
        }

        SESSION_LOG(INFO) << "drop, call and erase all callback in this session!"
                          << LOG_KV("this", this) << LOG_KV("endpoint", socket->nodeIPEndpoint());

        // FIB-184: serialize the SSL/socket teardown onto the socket's own (single-threaded)
        // io_context. drop() can be invoked from another thread (Service-layer teardown,
        // duplicate-peer handling) while an async_read_some/async_write is still in flight on the
        // socket's io_context thread. Running close()/async_shutdown inline on the caller thread
        // would then touch the same ssl::stream concurrently with those handlers. Posting the
        // teardown to the socket's io_context makes it run on the same single thread that services
        // every read/write for this session — i.e. a per-session strand — so socket operations
        // never overlap. The strong self capture keeps the session (and its socket) alive until
        // the teardown runs.
        //
        // Shutdown path exception: Service::stop() calls Host::stop() BEFORE dropping sessions,
        // and the shared IOServicePool is torn down shortly after by whoever owns it, so once the
        // network is down a posted handler may never run — the socket would never be closed and
        // the posted task would pin this session in a dead io_context queue. Close inline instead,
        // matching the old synchronous teardown behaviour on shutdown.
        //
        // Do not read this as "the io_context threads are already joined": Host::stop() only
        // clears m_run now (it no longer stops the ASIO interface), so the shared pool is still
        // live here. What makes the inline close safe is the caller, not a quiesced pool — see the
        // note on the teardown-notification branch above.
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

private:
    // Read-loop coroutine, launched fire-and-forget via task::wait from startWithPolicy. Each
    // frame holds a strong reference to the session for the whole loop, so an in-flight read keeps
    // the session, its recv buffer and its socket alive — the FIB-184 lifetime invariant, made
    // structural instead of relying on completion-handler captures. ReadPolicy is the
    // compile-time read-initiation policy (see ASIOInterface::awaitableReadSome): production
    // instantiates ASIOInterface::DefaultReadPolicy, test fakes their own.
    template <typename ReadPolicy>
    task::Task<void> readLoop()
    {
        // FIB-184: the coroutine frame holds a strong reference to the session for the whole read
        // loop. While the loop is suspended at the co_await below, the buffer handed to
        // async_read_some points into this->m_recvBuffer and the stream is this->m_socket — the
        // frame keeps both alive until the read completes, so a concurrent teardown on another
        // thread can free them only after the read finishes. This supersedes the FIB-97/FIB-184
        // completion-handler captures with a structural guarantee.
        auto self = this->shared_from_this();
        try
        {
            while (m_active && m_server.get().haveNetwork())
            {
                if (!m_socket->isConnected())
                {
                    SESSION_LOG(WARNING) << LOG_DESC("Error Reading ssl socket is close!");
                    drop(TCPError);
                    co_return;
                }

                auto writeBuffer = m_recvBuffer.asWriteBuffer();
                std::size_t readSize =
                    (writeBuffer.size() > m_maxReadDataSize ? m_maxReadDataSize :
                                                              writeBuffer.size());

                // Read timeout (setReadTimeoutMs, 0 = disabled): arm a timer on the socket's own
                // io_service so its handler is serialized with the read completion AND with this
                // coroutine's resumption on that single thread — the flags need no
                // synchronization. On expiry the handler flags this iteration and cancels the
                // pending read, which wakes the co_await below with operation_aborted. readArmed
                // guards against a timer whose expiry is already QUEUED but not yet run (cancel()
                // cannot recall it): the coroutine disarms it after the read completes, so a late
                // success handler can never cancel the NEXT iteration's read. Both are
                // frame-local: the timer and the read completion keep the session alive via this
                // frame.
                std::shared_ptr<bool> readExpired;
                std::shared_ptr<bool> readArmed;
                std::shared_ptr<boost::asio::steady_timer> readTimer;
                if (m_readTimeoutMs > 0)
                {
                    readExpired = std::make_shared<bool>(false);
                    readArmed = std::make_shared<bool>(true);
                    readTimer = std::make_shared<boost::asio::steady_timer>(
                        m_socket->ioService(), std::chrono::milliseconds(m_readTimeoutMs));
                    readTimer->async_wait([readExpired, readArmed, socket = m_socket](
                                              const boost::system::error_code& timerEc) {
                        if (!timerEc && *readArmed)
                        {
                            *readExpired = true;
                            boost::system::error_code cancelEc;
                            // NOLINTNEXTLINE(bugprone-unused-return-value)
                            socket->ref().cancel(cancelEc);
                        }
                    });
                }

                auto [ec, bytesTransferred] =
                    co_await m_server.get().asioInterface()
                        ->template awaitableReadSome<ReadPolicy>(
                            m_socket, boost::asio::buffer(writeBuffer.data(), readSize));

                if (readTimer)
                {
                    // Disarm BEFORE cancel: an expiry already queued behind this read's
                    // completion observes readArmed == false and no-ops (see above).
                    *readArmed = false;
                    readTimer->cancel();
                    if (*readExpired && ec == boost::asio::error::operation_aborted)
                    {
                        // Read timed out: deliver a Timeout frame (empty frame, consumed = 0;
                        // consumers check meta.status == FrameMeta::Status::Timeout) WITHOUT
                        // dropping the session and WITHOUT refreshing m_lastReadTime — a stalled
                        // peer must not count as read activity. A closed recv channel means
                        // teardown owns the session: exit.
                        FrameMeta timeoutMeta;
                        timeoutMeta.status = FrameMeta::Status::Timeout;
                        if (!co_await m_recvChannel.waitWritable(0) ||
                            !m_recvChannel.push(std::move(timeoutMeta)))
                        {
                            co_return;
                        }
                        continue;
                    }
                }

                if (ec)
                {
                    SESSION_LOG(INFO) << LOG_DESC("readLoop failed")
                                      << LOG_KV("endpoint", nodeIPEndpoint())
                                      << LOG_KV("message", ec.message());
                    drop(TCPError);
                    co_return;
                }

                m_lastReadTime.store(utcSteadyTime());

                auto& recvBuffer = this->recvBuffer();
                // FIB-184 (review): onWrite advances the write position and returns false if the
                // just-read bytes would overrun the recv buffer. With the lazy-initial / grow-on-
                // demand buffer the read size is bounded by the write-buffer span, so this should
                // not happen; but if it ever did the bytes would be silently dropped and the
                // stream desynchronized. Treat it as a transport error and drop the session
                // instead.
                if (!recvBuffer.onWrite(bytesTransferred))
                {
                    SESSION_LOG(ERROR)
                        << LOG_BADGE("readLoop") << LOG_DESC("recv buffer overflow on write, drop")
                        << LOG_KV("bytesTransferred", bytesTransferred)
                        << LOG_KV("recvBufferSize", recvBuffer.recvBufferSize());
                    drop(TCPError);
                    co_return;
                }

                // decode every complete frame already in the buffer, then loop back for more
                while (true)
                {
                    FrameMeta meta;
                    try
                    {
                        auto bufferForWrite = recvBuffer.asWriteBuffer();
                        auto readBuffer = recvBuffer.asReadBuffer();
                        // Note: the decoder contract says no-throw; the catch is belt-and-braces
                        meta = m_decoder.tryDecode(readBuffer);
                        if (meta.status == FrameMeta::Status::Frame)
                        {
                            if (meta.takeBuffer) [[unlikely]]
                            {
                                // Large frame (the decoder left meta.frame empty): move the whole
                                // receive buffer into the frame instead of copying it out. The
                                // tail bytes after the frame reseed a fresh buffer; the frame's
                                // bytes left with the moved storage, so there is no onRead for
                                // them.
                                meta.frameOffset = static_cast<uint32_t>(
                                    recvBuffer.takeStorage(meta.frame, meta.consumed));
                                meta.takeBuffer = false;
                            }
                            else
                            {
                                recvBuffer.onRead(meta.consumed);
                            }
                            // Pull-mode delivery with backpressure: when the recv queue has no
                            // room for this frame the consumer is slower than the peer, so park
                            // the read loop until the consumer drains space — the TCP receive
                            // window then throttles the sender. No timeout: drop()/close() wakes
                            // the park on teardown, and a live consumer always makes progress.
                            // A frame larger than the entire byte budget could never be queued —
                            // drop instead of parking forever (unreachable in production:
                            // allowMaxMsgSize < the budget).
                            if (meta.frame.size() > MAX_RECV_QUEUE_BYTES)
                            {
                                SESSION_LOG(ERROR)
                                    << LOG_BADGE("readLoop")
                                    << LOG_DESC("frame larger than the recv queue budget, drop")
                                    << LOG_KV("frameSize", meta.frame.size())
                                    << LOG_KV("budget", MAX_RECV_QUEUE_BYTES)
                                    << LOG_KV("endpoint", nodeIPEndpoint());
                                drop(UserReason);
                                co_return;
                            }
                            if (!co_await m_recvChannel.waitWritable(meta.frame.size()))
                            {
                                // Channel closed: drop()/protocol-error teardown owns the session.
                                co_return;
                            }
                            // A resumed park counts as activity for the idle checker: it closes
                            // the race where the consumer drained the queue to empty (the
                            // checkNetworkStatus backlog exemption no longer applies) just as
                            // this loop woke, with the last real read long past.
                            m_lastReadTime.store(utcSteadyTime());
                            if (!m_recvChannel.push(std::move(meta)))
                            {
                                // Closed between the wake and the push: teardown owns the session.
                                co_return;
                            }
                        }
                        else if (meta.status == FrameMeta::Status::NeedMoreData)
                        {
                            auto length = meta.declaredLength;
                            if (length > allowMaxMsgSize())
                            {
                                SESSION_LOG(ERROR)
                                    << LOG_BADGE("readLoop")
                                    << LOG_DESC(
                                           "the message size exceeded the allow maximum value")
                                    << LOG_KV("msgSize", length)
                                    << LOG_KV("allowMaxMsgSize", allowMaxMsgSize());

                                m_recvChannel.close(std::make_exception_ptr(
                                    makeNetworkException(P2PExceptionType::ProtocolError,
                                        "ProtocolError(msg overflow)")));
                                drop(UserReason);
                                co_return;
                            }

                            if ((length > recvBuffer.recvBufferSize()) ||
                                (length > bufferForWrite.size()) ||
                                maxReadDataSize() > bufferForWrite.size())
                            {
                                recvBuffer.moveToHeader();

                                // the write buffer is not enough, move the left data to recv
                                // buffer header for waiting for the next read
                                if (length >= recvBuffer.recvBufferSize())
                                {
                                    auto resizeRecvBufferSize = 2 * length;
                                    resizeRecvBufferSize = std::min<std::size_t>(
                                        resizeRecvBufferSize, m_maxRecvBufferSize);
                                    recvBuffer.resizeBuffer(resizeRecvBufferSize);

                                    SESSION_LOG(INFO)
                                        << LOG_BADGE("readLoop")
                                        << LOG_DESC(
                                               "the current recv buffer size is not enough for "
                                               "the "
                                               "next message, resize the recv buffer")
                                        << LOG_KV("msgSize", length)
                                        << LOG_KV("resizeRecvBufferSize", resizeRecvBufferSize)
                                        << LOG_KV("allowMaxMsgSize", allowMaxMsgSize());
                                }
                            }

                            // need more data: continue the outer loop and arm the next read
                            // (replaces the doRead() recursion of the old callback version)
                            break;
                        }
                        else
                        {
                            SESSION_LOG(ERROR)
                                << LOG_BADGE("readLoop") << LOG_DESC("decode frame error");
                            m_recvChannel.close(std::make_exception_ptr(
                                makeNetworkException(P2PExceptionType::ProtocolError,
                                    "ProtocolError(decode frame error)")));
                            drop(UserReason);
                            co_return;
                        }
                    }
                    catch (std::exception const& e)
                    {
                        SESSION_LOG(ERROR) << LOG_DESC("Decode frame exception")
                                           << LOG_KV("message", boost::diagnostic_information(e));
                        m_recvChannel.close(std::make_exception_ptr(
                            makeNetworkException(P2PExceptionType::ProtocolError,
                                "ProtocolError(decode frame exception)")));
                        drop(UserReason);
                        co_return;
                    }
                    catch (...)
                    {
                        SESSION_LOG(ERROR) << LOG_DESC("Decode frame exception")
                                           << LOG_KV("message",
                                                  boost::current_exception_diagnostic_information());
                        m_recvChannel.close(std::make_exception_ptr(
                            makeNetworkException(P2PExceptionType::ProtocolError,
                                "ProtocolError(decode frame exception)")));
                        drop(UserReason);
                        co_return;
                    }
                }
            }
        }
        catch (...)
        {
            // never let an exception escape into the resuming asio handler (see FireAwaitable.h);
            // a session whose read loop died without a drop would zombie until the idle timer
            SESSION_LOG(ERROR) << LOG_DESC("read loop exception")
                               << LOG_KV("endpoint", nodeIPEndpoint())
                               << LOG_KV("what", boost::current_exception_diagnostic_information());
            drop(TCPError);
            co_return;
        }
    }

    // Single-writer write loop (see write()): drains m_writeQueue in batches and serializes every
    // async_write through the single in-flight loop. The frame holds a strong reference to the
    // session for the whole loop (see the body), keeping the socket and the batch buffers alive
    // across each co_await.
    task::Task<void> writeLoop()
    {
        // FIB-184: the coroutine frame holds a strong reference to the session for the whole write
        // loop. async_write operates on m_socket and reads from the batch buffers below; the frame
        // keeps the session (socket, SSL stream, batch buffers) alive until each write completes,
        // so a concurrent teardown on another thread cannot free them mid-write.
        auto self = this->shared_from_this();

        // Batch scratch owned by this frame for the whole loop (was the m_writings member; the
        // frame is the lifetime owner now, so no shared_ptr indirection is needed).
        std::vector<Payload> payloads;
        std::vector<boost::asio::const_buffer> buffers;

        // No frame-local RAII guard is needed for the single-flight write flag. The completion
        // rescue (FireCompletion in FireAwaitable.h) RESUMES an uninvoked completion's coroutine
        // with the initial error rather than destroying its frame, so this loop always runs
        // through to its single exit below and releases m_writingInFlight there. The only frame
        // destroyed before that is one destroyed while parked at initial_suspend (a failed
        // launch), and the launch-failure catch in write() already releases the flag for that
        // case. NOTE: a destroy-based rescue would make the flag and the popped batch leak here —
        // re-add a guard like the one this comment replaced if such a rescue is ever
        // reintroduced.

        // Fails the in-flight batch: the payloads have already been moved out of m_writeQueue and
        // no completion handler exists for them, so their callbacks would never fire (drop() only
        // drains m_writeQueue). Invoked from every exception exit so the batch is settled exactly
        // once — a batch destroyed with its callbacks unfired would pin every awaiting sender
        // forever.
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
                    SESSION_LOG(WARNING) << "Error sending ssl socket is close!"
                                         << LOG_KV("endpoint", nodeIPEndpoint());
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
            // FIB-185 (review): when the write path throws, the payloads have already been moved
            // into the local batch and no completion handler exists for them, so their callbacks
            // would never fire (drop() only drains m_writeQueue). Fail them here — posted rather
            // than inline, for the same reason as drop()'s drain: this catch runs on the caller's
            // stack, which may still be inside an await_suspend.
            failBatch(payloads);
            drop(TCPError);
        }
        catch (...)
        {
            // never let an exception escape into the resuming asio handler (see FireAwaitable.h);
            // fail the in-flight batch here too — the catch(std::exception&) arm above does it,
            // and a batch destroyed with its callbacks unfired would pin every awaiting sender
            // forever
            SESSION_LOG(ERROR) << LOG_DESC("write error") << LOG_KV("endpoint", nodeIPEndpoint())
                               << LOG_KV("what",
                                      boost::current_exception_diagnostic_information());
            failBatch(payloads);
            drop(TCPError);
        }

        // Single exit: clear the single-flight flag and, if the queue was refilled while this loop
        // was draining, hand the writer role to a fresh loop. Runs after every exit path (normal
        // drain, drop, exception). No wakeup is ever lost: a producer that pushed after the last
        // drain either wins the CAS in its own write() (flag already cleared) or this tail finds
        // the queue non-empty and re-arms. The re-arm (write()) posts the next loop's launch to
        // the socket's io thread, so it never extends this stack. Gated on active(): after a drop
        // the queue is drained by drop() (and late producers fail via send()'s re-check), so
        // re-arming there would just spin a fresh loop into the same teardown.
        //
        // The flag is cleared with exchange, not store: the tail never otherwise READS the flag,
        // and a plain store is release-only, so a producer's write() CAS could not be ordered
        // against it. The seq_cst exchange reads from the release sequence headed by that
        // producer's exchange(true); the queue re-check below then takes x_writeQueue, which such
        // a producer released after its push — so every push from a producer that observed the
        // flag still set is visible to the re-check, and no wakeup is lost.
        m_writingInFlight.exchange(false);
        if (active() && !writeQueueEmpty())
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

    // FIB-184: perform the actual SSL/socket teardown (close + graceful async_shutdown). It has a
    // strict threading contract — it must run on the socket's io_context (or, on the shutdown
    // path, with the io_context threads already joined) so it never touches the ssl::stream
    // concurrently with an in-flight async_read_some/async_write. It is therefore private and
    // reachable only via drop(), which enforces that contract (post to the io_context, or inline
    // once the network is down); calling it directly from an arbitrary thread would reintroduce
    // the original race.
    void closeSocket(DisconnectReason _reason)
    {
        // Take a local strong reference before touching the socket, for the same reason drop()
        // does: a concurrent setSocket(nullptr) must not turn this into a null dereference
        // mid-teardown.
        auto socket = m_socket;
        if (!socket || !socket->isConnected())
        {
            return;
        }
        try
        {
            if (_reason == DisconnectRequested || _reason == DuplicatePeer ||
                _reason == ClientQuit || _reason == UserReason)
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
            // TLS close_notify shutdown only exists on SSL sockets; plain sockets are already
            // fully closed above and need no application-layer shutdown.
            if constexpr (requires { socket->sslref(); })
            {
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
                            << "[drop] shutdown timer failed"
                            << LOG_KV("failedValue", error.value())
                            << LOG_KV("message", error.message());
                    }
                    /// force to shutdown when timeout
                    if (socket->ref().is_open())
                    {
                        SESSION_LOG(WARNING)
                            << "[drop] timeout, force close the socket"
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
                                << "[drop] shutdown failed "
                                << LOG_KV("failedValue", error.value())
                                << LOG_KV("message", error.message());
                        }
                        /// force to close the socket
                        if (socket->ref().is_open())
                        {
                            SESSION_LOG(WARNING)
                                << LOG_DESC("force to shutdown session")
                                << LOG_KV("endpoint", socket->nodeIPEndpoint());
                            socket->close();
                        }
                    });
            }
        }
        catch (...)
        {
            SESSION_LOG(ERROR) << LOG_DESC("drop error")
                               << LOG_KV("endpoint", socket->nodeIPEndpoint());
        }
    }

public:
    /// Check error code after reading and drop peer if error code.
    bool checkRead(boost::system::error_code _ec)
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

    /// Launch the write loop (writeLoop) if no write is currently in flight. Safe to call from
    /// any thread: the first caller wins the m_writingInFlight CAS and becomes the single writer;
    /// concurrent callers return immediately and rely on the in-flight writer (or writeLoop's
    /// single exit) to drain the queue.
    void write()
    {
        if (m_writingInFlight.exchange(true))
        {
            // a write loop is already in flight; it drains the queue
            return;
        }
        // Launch the loop on the socket's own io thread, NOT synchronously on the caller's stack.
        // write() is reached from a sender's await_suspend (sendMessage via send()): running
        // writeLoop's first iteration there would let a shutdown race (Host::stop() clearing m_run
        // between send()'s active() re-check and writeLoop's haveNetwork() check) call
        // drop(TCPError) on that stack, and drop()'s inline drain would invoke the just-queued
        // payload's callback — resuming the very coroutine whose await_suspend is still running
        // (undefined behaviour; await_resume's throw could even destroy the frame await_suspend is
        // standing on). With the launch posted, every settlement inside writeLoop — including the
        // drop() it may trigger and failBatch's inline branch — runs on an io thread, so no
        // callback can land on a sender's own stack. Cost is one post per write-loop start; the
        // loop already suspends into awaitableWrite, so steady-state cost is unchanged.
        //
        // Release-on-unwind, matching the old std::unique_lock(try_to_lock) writer flag: if the
        // launch itself throws (post allocation failure, or bad_weak_ptr from
        // shared_from_this()), the flag must not stay set, or every later write() would
        // early-return at the CAS above and the queue would never drain again. The exception is
        // NOT propagated to the caller (see above: the caller may be a sender's await_suspend with
        // the payload already queued); swallow, log and drop, which drains m_writeQueue and
        // completes every queued callback.
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

    /// Settle one queued write-completion callback that resumes a suspended sender, delivering
    /// `args...` to it.
    ///
    /// Run it on the shared io pool while the host is alive, inline once it is gone, containing
    /// any exception either way. Three reasons for that shape, all of them load-bearing:
    ///  - A resumed waiter may throw (sendMessage throws NetworkException on a failed write)
    ///    and a posted callback runs inside io_context::run(), so an escaping exception would
    ///    unwind the reactor.
    ///  - Once the host is gone a posted task would never run at all, which would hang every
    ///    waiter forever.
    ///  - The caller may itself be sitting on another waiter's await_suspend stack, so it must
    ///    not nest that waiter's whole continuation either.
    /// This is the single place that policy lives for write-queue settlement.
    template <class Callback, class... Args>
    void postCallback(Callback&& callback, const char* description, Args... args)
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

    std::reference_wrapper<Host<DecoderT, SocketT>> m_server;  ///< The host that owns us. Never null.
    std::shared_ptr<SocketT> m_socket;                ///< Socket of peer's connection.

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

    // Inbound pull queue: the read loop pushes decoded frames (parking on waitWritable when
    // full, for backpressure); the consumer drains them with recvMessage(). drop() closes it to
    // wake the parked consumer — and a parked read loop — with the teardown error.
    task::Channel<FrameMeta> m_recvChannel;

    uint64_t m_shutDownTimeThres = 50000;
    // 1min. 0 = idle-drop disabled (see setIdleTimeInterval).
    uint64_t m_idleTimeInterval = 60 * 1000;
    // Read timeout for the read loop, 0 = disabled (see setReadTimeoutMs).
    uint32_t m_readTimeoutMs = 0;

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

private:
    // Outbound write queue. Producers (detail::send) push under x_writeQueue; the single-flight
    // write loop (writeLoop via tryPopSomeEncodedMsgs), send()'s post-push re-check and drop()
    // drain under the same mutex. No callback is ever invoked while the mutex is held: the
    // helpers only move payloads in and out, the callers settle callbacks afterwards. This
    // replaces the old tbb::concurrent_queue: contention here is one consumer vs a handful of
    // producers, the deque amortizes allocation (no per-push node), and the mutex makes
    // empty()/size() exact.
    std::deque<Payload> m_writeQueue;
    std::mutex x_writeQueue;
};

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
    session.pushWriteQueue(std::move(payload));

    // FIB-185 (review): re-check the session state AFTER the push. drop() may have drained the
    // queue (CAS won) between the active() check above and this push, in which case this
    // payload's callback would never fire and this coroutine would hang. Either drop()'s drain
    // already popped our payload (its callback then fires with operation_aborted), or this
    // re-check sees an inactive session and drains the leftover payloads here. Both paths settle
    // every queued callback exactly once: the drain empties the whole queue, so it may resume
    // senders other than this coroutine — postCallback settles them off this stack.
    if (!session.active())
    {
        // drainWriteQueue only moves the payloads out (under the queue mutex); the callbacks are
        // settled here, after the mutex has been released.
        for (auto& pending : session.drainWriteQueue())
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
        Host<DecoderT, SocketT>& _server, std::shared_ptr<SocketT> const& _socket)
    {
        std::shared_ptr<BasicSession<DecoderT, SocketT>> session =
            std::make_shared<BasicSession<DecoderT, SocketT>>(
                _socket, _server, m_sessionRecvBufferSize);
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

private:
    uint32_t m_sessionRecvBufferSize;
    uint32_t m_allowMaxMsgSize{0};
    uint32_t m_maxReadDataSize{0};
    uint32_t m_maxSendDataSize{0};
};

}  // namespace bcos::network

// Host.h is included at the bottom, not the top, on purpose: Host.h includes this header, so a
// top-level include would make the Session.h -> Host.h -> Session.h cycle unresolvable. The
// in-class definitions above only touch Host through dependent member access (checked at
// instantiation), so the forward declaration suffices here; this include keeps the header
// self-contained for TUs that instantiate a BasicSession specialization without including
// Host.h themselves.
#include "bcos-network/Host.h"
