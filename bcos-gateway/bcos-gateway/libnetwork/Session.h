
/** @file Session.h
 * @author monan <651932351@qq.com>
 * @date 2018
 */

#pragma once

#include "bcos-gateway/libnetwork/Common.h"
#include "bcos-gateway/libnetwork/Message.h"
#include "bcos-gateway/libnetwork/SessionCallback.h"
#include "bcos-gateway/libnetwork/SessionFace.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/Error.h"
#include "bcos-utilities/Overloaded.h"
#include "bcos-utilities/Timer.h"
#include <oneapi/tbb/concurrent_queue.h>
#include <boost/asio/buffer.hpp>
#include <boost/container/small_vector.hpp>
#include <boost/heap/priority_queue.hpp>
#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <unordered_set>
#include <utility>
#include <variant>


namespace bcos::gateway
{
class Host;
class SocketFace;

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

class Session : public SessionFace, public std::enable_shared_from_this<Session>
{
public:
    // Grow ceiling: the recv buffer never grows beyond this (see the read-loop grow path).
    constexpr static const std::size_t MIN_SESSION_RECV_BUFFER_SIZE = 512 * 1024UL;
    // FIB-184: initial recv-buffer size for a freshly created session. Previously every
    // session unconditionally allocated MIN_SESSION_RECV_BUFFER_SIZE (512KB) up front, so a
    // flood of unauthenticated/short-lived sessions caused heap exhaustion. Start small and
    // rely on the existing grow path (the read loop grows up to m_maxRecvBufferSize) to expand
    // only for sessions that actually carry large messages. Must stay well above the message
    // header length so the first read can always make forward progress.
    constexpr static const std::size_t INITIAL_SESSION_RECV_BUFFER_SIZE = 16 * 1024UL;

    Session(std::shared_ptr<SocketFace> socket, Host& server,
        size_t _recvBufferSize = INITIAL_SESSION_RECV_BUFFER_SIZE, bool _forceSize = false);

    Session(const Session&) = delete;
    Session(Session&&) = delete;
    Session& operator=(Session&&) = delete;
    Session& operator=(const Session&) = delete;
    ~Session() noexcept override;

    using Ptr = std::shared_ptr<Session>;

    void start() override;

    // Read-policy seam (compile-time): identical lifecycle to start(), but the read loop is
    // compiled against an explicit ReadPolicy so read-loop test fakes can inject a policy that
    // parks / controls read completions (see ASIOInterface::awaitableReadSome). Production call
    // sites use the virtual start() (the default policy) — this template costs nothing there.
    // Definition lives in SessionReadLoop.h.
    template <typename ReadPolicy>
    void startWithPolicy();
    void disconnect(DisconnectReason _reason) override;

    task::Task<Message::Ptr> fastSendMessage(const Message& message,
        ::ranges::any_view<bytesConstRef> payloads, Options options) override;

    NodeIPEndpoint nodeIPEndpoint() const override;

    bool active() const override;

    bool active(Host& server) const;

    std::size_t writeQueueSize() override;

    virtual Host& host();

    std::shared_ptr<SocketFace> socket() override;
    virtual void setSocket(const std::shared_ptr<SocketFace>& socket);

    virtual MessageFactory::Ptr messageFactory() const;
    virtual void setMessageFactory(const MessageFactory::Ptr& _messageFactory);

    SessionCallbackManagerInterface::Ptr sessionCallbackManager() const;
    void setSessionCallbackManager(
        const SessionCallbackManagerInterface::Ptr& _sessionCallbackManager);

    virtual const std::function<void(NetworkException, SessionFace::Ptr, Message::Ptr)>&
    messageHandler();
    void setMessageHandler(
        std::function<void(NetworkException, SessionFace::Ptr, Message::Ptr)> messageHandler)
        override;

    // handle before sending message: if the check fails (returns an error), the message is not
    // sent and a NetworkException surfaces so coroutine retry loops can stop. The handler receives
    // the actual wire length (payload views included) as _wireLength.
    void setBeforeMessageHandler(std::function<std::optional<bcos::Error>(
        SessionFace&, const Message&, uint32_t _wireLength)> handler) override;

    void setHostInfo(P2PInfo _hostInfo);

    // FIB-184: attach an opaque object whose lifetime is bound to this session. It is destroyed
    // exactly when the session object is destroyed, which Host uses to release a session-cap
    // slot (the guard's destructor decrements the Host counters). Kept opaque so libnetwork
    // does not depend on the accounting type.
    void setLifetimeGuard(std::shared_ptr<void> _guard) override
    {
        m_lifetimeGuard = std::move(_guard);
    }

    uint32_t maxReadDataSize() const;
    void setMaxReadDataSize(uint32_t _maxReadDataSize);

    uint32_t maxSendDataSize() const;
    void setMaxSendDataSize(uint32_t _maxSendDataSize);

    uint32_t allowMaxMsgSize() const;
    void setAllowMaxMsgSize(uint32_t _allowMaxMsgSize);

    void setEnableCompress(bool _enableCompress);
    bool enableCompress() const;

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

    virtual void checkNetworkStatus();

    // FIB-184 (review): keep the grow ceiling private so it can only be read via
    // maxRecvBufferSize() and never widened from outside. Declared before m_recvBuffer to preserve
    // member init order.
private:
    std::size_t m_maxRecvBufferSize;

public:
    SessionRecvBuffer m_recvBuffer;

    // ------ for optimize send message parameters  begin ---------------
    //  // Maximum amount of data to read one time, default: 40K
    uint32_t m_maxReadDataSize = 40 * 1024;
    // Maximum amount of data to be sent one time, default: 1M
    uint32_t m_maxSendDataSize = 1024 * 1024;
    //  Maximum size of message that is allowed to send or receive, default: 32M
    uint32_t m_allowMaxMsgSize = 32 * 1024 * 1024;
    //
    bool m_enableCompress = true;
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

    /// called by the read loop to deal with a decoded message
    void onMessage(NetworkException const& e, Message::Ptr message);

    std::reference_wrapper<Host> m_server;  ///< The host that owns us. Never null.
    std::shared_ptr<SocketFace> m_socket;   ///< Socket of peer's connection.

    MessageFactory::Ptr m_messageFactory;
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

    SessionCallbackManagerInterface::Ptr m_sessionCallbackManager;
    std::function<void(NetworkException, SessionFace::Ptr, Message::Ptr)> m_messageHandler;
    std::function<std::optional<bcos::Error>(
        SessionFace&, const Message&, uint32_t)> m_beforeMessageHandler;

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
    P2PInfo m_hostInfo;

    // FIB-184: opaque guard whose destructor releases the Host session-cap slot. Destroyed with
    // the session, so the slot is freed exactly once on session teardown.
    std::shared_ptr<void> m_lifetimeGuard;

    std::mutex x_pendingResponseSeqs;
    std::unordered_set<uint32_t> m_pendingResponseSeqs;
};

class SessionFactory
{
public:
    SessionFactory(P2PInfo _hostInfo, uint32_t _sessionRecvBufferSize,  // NOLINT
        uint32_t _allowMaxMsgSize, uint32_t _maxReadDataSize, uint32_t _maxSendDataSize,
        bool _enableCompress)
      : m_hostInfo(std::move(_hostInfo)),
        m_sessionRecvBufferSize(_sessionRecvBufferSize),
        m_allowMaxMsgSize(_allowMaxMsgSize),
        m_maxReadDataSize(_maxReadDataSize),
        m_maxSendDataSize(_maxSendDataSize),
        m_enableCompress(_enableCompress)
    {}
    SessionFactory(const SessionFactory&) = delete;
    SessionFactory(SessionFactory&&) = delete;
    SessionFactory& operator=(SessionFactory&&) = delete;
    SessionFactory& operator=(const SessionFactory&) = delete;
    virtual ~SessionFactory() = default;

    virtual std::shared_ptr<SessionFace> createSession(Host& _server,
        std::shared_ptr<SocketFace> const& _socket, MessageFactory::Ptr& _messageFactory,
        SessionCallbackManagerInterface::Ptr& _sessionCallbackManager);

private:
    P2PInfo m_hostInfo;
    uint32_t m_sessionRecvBufferSize;
    uint32_t m_allowMaxMsgSize{0};
    uint32_t m_maxReadDataSize{0};
    uint32_t m_maxSendDataSize{0};
    bool m_enableCompress = true;
};

}  // namespace bcos::gateway
