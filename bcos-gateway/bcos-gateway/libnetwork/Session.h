
/** @file Session.h
 * @author monan <651932351@qq.com>
 * @date 2018
 */

#pragma once

#include "bcos-gateway/libnetwork/Message.h"
#include "bcos-utilities/BoostLog.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/Error.h"
#include "bcos-utilities/ObjectCounter.h"
#include <bcos-gateway/libnetwork/Common.h>
#include <bcos-gateway/libnetwork/SessionCallback.h>
#include <bcos-gateway/libnetwork/SessionFace.h>
#include <bcos-utilities/Timer.h>
#include <boost/heap/priority_queue.hpp>
#include <cstddef>
#include <memory>
#include <utility>

namespace bcos
{
namespace gateway
{
class Host;
class SocketFace;

class SessionRecvBuffer : public bcos::ObjectCounter<SessionRecvBuffer>
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

    inline std::size_t readPos() const { return m_readPos; }
    inline std::size_t writePos() const { return m_writePos; }
    inline std::size_t dataSize() const { return m_writePos - m_readPos; }

    inline size_t recvBufferSize() const { return m_recvBufferSize; }

    bool onRead(std::size_t _dataSize)
    {
        if (m_readPos + _dataSize <= m_writePos)
        {
            m_readPos += _dataSize;
            return true;
        }
        return false;
    }


    bool onWrite(std::size_t _dataSize)
    {
        if (m_writePos + _dataSize <= m_recvBufferSize)
        {
            m_writePos += _dataSize;
            return true;
        }
        return false;
    }


    bool resizeBuffer(size_t _bufferSize)
    {
        if (_bufferSize > m_recvBufferSize)
        {
            m_recvBuffer.resize(_bufferSize);
            m_recvBufferSize = _bufferSize;

            return true;
        }

        return false;
    }

    // void realloc() {}

    void moveToHeader()
    {
        if (m_writePos > m_readPos)
        {
            memmove(m_recvBuffer.data(), m_recvBuffer.data() + m_readPos, m_writePos - m_readPos);
            m_writePos -= m_readPos;
            m_readPos = 0;
        }
        else if (m_writePos == m_readPos)
        {
            m_readPos = 0;
            m_writePos = 0;
        }
    }

    inline bcos::bytesConstRef asReadBuffer() const
    {
        return {m_recvBuffer.data() + m_readPos, m_writePos - m_readPos};
    }

    inline bcos::bytesConstRef asWriteBuffer() const
    {
        return {m_recvBuffer.data() + m_writePos, m_recvBufferSize - m_writePos};
    }

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

class Session : public SessionFace,
                public std::enable_shared_from_this<Session>,
                public bcos::ObjectCounter<Session>
{
public:
    constexpr static const std::size_t MIN_SESSION_RECV_BUFFER_SIZE =
        static_cast<std::size_t>(512 * 1024);

    Session(size_t _recvBufferSize = MIN_SESSION_RECV_BUFFER_SIZE, bool _forceSize = false);

    Session(const Session&) = delete;
    Session(Session&&) = delete;
    Session& operator=(Session&&) = delete;
    Session& operator=(const Session&) = delete;

    ~Session() noexcept override;

    using Ptr = std::shared_ptr<Session>;

    void start() override;
    void disconnect(DisconnectReason _reason) override;

    void asyncSendMessage(Message::Ptr message, Options options,
        SessionCallbackFunc callback = SessionCallbackFunc()) override;

    NodeIPEndpoint nodeIPEndpoint() const override;

    bool active() const override;

    bool active(std::shared_ptr<bcos::gateway::Host>&) const;

    std::size_t writeQueueSize() override;

    virtual std::weak_ptr<Host> host() { return m_server; }
    virtual void setHost(std::weak_ptr<Host> host) { m_server = std::move(host); }

    std::shared_ptr<SocketFace> socket() override { return m_socket; }
    virtual void setSocket(const std::shared_ptr<SocketFace>& socket) { m_socket = socket; }

    virtual MessageFactory::Ptr messageFactory() const { return m_messageFactory; }
    virtual void setMessageFactory(const MessageFactory::Ptr& _messageFactory)
    {
        m_messageFactory = _messageFactory;
    }

    SessionCallbackManagerInterface::Ptr sessionCallbackManager()
    {
        return m_sessionCallbackManager;
    }
    void setSessionCallbackManager(
        const SessionCallbackManagerInterface::Ptr& _sessionCallbackManager)
    {
        m_sessionCallbackManager = _sessionCallbackManager;
    }

    virtual std::function<void(NetworkException, SessionFace::Ptr, Message::Ptr)> messageHandler()
    {
        return m_messageHandler;
    }
    void setMessageHandler(
        std::function<void(NetworkException, SessionFace::Ptr, Message::Ptr)> messageHandler)
        override
    {
        m_messageHandler = messageHandler;
    }

    // handle before sending message, if the check fails, meaning false is returned, the message
    // is not sent, and the SessionCallbackFunc will be performed
    void setBeforeMessageHandler(
        std::function<std::optional<bcos::Error>(SessionFace::Ptr, Message::Ptr)> handler) override
    {
        m_beforeMessageHandler = handler;
    }

    void setHostNodeID(std::string const& _hostNodeID) { m_hostNodeID = _hostNodeID; }

    uint32_t maxReadDataSize() const { return m_maxReadDataSize; }
    void setMaxReadDataSize(uint32_t _maxReadDataSize) { m_maxReadDataSize = _maxReadDataSize; }

    uint32_t maxSendDataSize() const { return m_maxSendDataSize; }
    void setMaxSendDataSize(uint32_t _maxSendDataSize) { m_maxSendDataSize = _maxSendDataSize; }

    uint32_t maxSendMsgCountS() const { return m_maxSendMsgCountS; }
    void setMaxSendMsgCountS(uint32_t _maxSendMsgCountS) { m_maxSendMsgCountS = _maxSendMsgCountS; }

    uint32_t allowMaxMsgSize() const { return m_allowMaxMsgSize; }
    void setAllowMaxMsgSize(uint32_t _allowMaxMsgSize) { m_allowMaxMsgSize = _allowMaxMsgSize; }

    void setEnableCompress(bool _enableCompress) { m_enableCompress = _enableCompress; }
    bool enableCompress() const { return m_enableCompress; }

    SessionRecvBuffer& recvBuffer() { return m_recvBuffer; }
    const SessionRecvBuffer& recvBuffer() const { return m_recvBuffer; }
    /**
     * @brief The packets that can be sent are obtained based on the configured policy
     *
     * @param encodedMsgs
     * @param _maxSendDataSize
     * @param _maxSendMsgCount
     * @return std::size_t
     */
    std::size_t tryPopSomeEncodedMsgs(std::vector<EncodedMessage::Ptr>& encodedMsgs,
        uint32_t _maxSendDataSize, uint32_t _maxSendMsgCount);

protected:
    virtual void checkNetworkStatus();

private:
    void send(EncodedMessage::Ptr& _encoder);

    void doRead();

    std::size_t m_maxRecvBufferSize;
    SessionRecvBuffer m_recvBuffer;

    std::vector<boost::asio::const_buffer> m_writeConstBuffer;

    // ------ for optimize send message parameters  begin ---------------
    //  // Maximum amount of data to read one time, default: 40K
    uint32_t m_maxReadDataSize = 40 * 1024;
    // Maximum amount of data to be sent one time, default: 1M
    uint32_t m_maxSendDataSize = 1024 * 1024;
    // Maximum number of packets to be sent one time, default: 10
    uint32_t m_maxSendMsgCountS = 10;
    //  Maximum size of message that is allowed to send or receive, default: 32M
    uint32_t m_allowMaxMsgSize = 32 * 1024 * 1024;
    //
    bool m_enableCompress = true;
    // ------ for optimize send message parameters  end ---------------

    /// Drop the connection for the reason @a _reason.
    void drop(DisconnectReason _reason);

    /// Check error code after reading and drop peer if error code.
    bool checkRead(boost::system::error_code _ec);

    void onTimeout(const boost::system::error_code& error, uint32_t seq);

    /// Perform a single round of the write operation. This could end up calling
    /// itself asynchronously.
    void onWrite(boost::system::error_code ec, std::size_t length);
    void write();

    /// call by doRead() to deal with message
    void onMessage(NetworkException const& e, Message::Ptr message);

    std::weak_ptr<Host> m_server;          ///< The host that owns us. Never null.
    std::shared_ptr<SocketFace> m_socket;  ///< Socket of peer's connection.

    MessageFactory::Ptr m_messageFactory;

    std::list<EncodedMessage::Ptr> m_writeQueue;
    std::atomic_bool m_writing = {false};
    bcos::Mutex x_writeQueue;

    mutable bcos::Mutex x_info;

    bool m_active = false;

    SessionCallbackManagerInterface::Ptr m_sessionCallbackManager;

    std::function<void(NetworkException, SessionFace::Ptr, Message::Ptr)> m_messageHandler;

    std::function<std::optional<bcos::Error>(SessionFace::Ptr, Message::Ptr)>
        m_beforeMessageHandler;

    uint64_t m_shutDownTimeThres = 50000;
    // 1min
    uint64_t m_idleTimeInterval = 60 * 1000;

    // timer to check the connection
    std::atomic<uint64_t> m_lastReadTime;
    std::atomic<uint64_t> m_lastWriteTime;
    std::shared_ptr<bcos::Timer> m_idleCheckTimer;
    std::string m_hostNodeID;
};

class SessionFactory
{
public:
    SessionFactory(std::string _hostNodeID, uint32_t _sessionRecvBufferSize,  // NOLINT
        uint32_t _allowMaxMsgSize, uint32_t _maxReadDataSize, uint32_t _maxSendDataSize,
        uint32_t _maxSendMsgCountS, bool _enableCompress)
      : m_hostNodeID(std::move(_hostNodeID)),
        m_sessionRecvBufferSize(_sessionRecvBufferSize),
        m_allowMaxMsgSize(_allowMaxMsgSize),
        m_maxReadDataSize(_maxReadDataSize),
        m_maxSendDataSize(_maxSendDataSize),
        m_maxSendMsgCountS(_maxSendMsgCountS),
        m_enableCompress(_enableCompress)
    {}
    SessionFactory(const SessionFactory&) = delete;
    SessionFactory(SessionFactory&&) = delete;
    SessionFactory& operator=(SessionFactory&&) = delete;
    SessionFactory& operator=(const SessionFactory&) = delete;
    virtual ~SessionFactory() = default;

    virtual std::shared_ptr<SessionFace> create_session(std::weak_ptr<Host>& _server,
        std::shared_ptr<SocketFace> const& _socket, MessageFactory::Ptr& _messageFactory,
        SessionCallbackManagerInterface::Ptr& _sessionCallbackManager)
    {
        std::shared_ptr<Session> session = std::make_shared<Session>(m_sessionRecvBufferSize);
        session->setHostNodeID(m_hostNodeID);
        session->setHost(_server);
        session->setSocket(_socket);
        session->setMessageFactory(_messageFactory);
        session->setSessionCallbackManager(_sessionCallbackManager);
        session->setAllowMaxMsgSize(m_allowMaxMsgSize);
        session->setMaxReadDataSize(m_maxReadDataSize);
        session->setMaxSendDataSize(m_maxSendDataSize);
        session->setMaxSendMsgCountS(m_maxSendMsgCountS);
        session->setEnableCompress(m_enableCompress);
        BCOS_LOG(INFO) << LOG_BADGE("SessionFactory") << LOG_DESC("create new session")
                       << LOG_KV("sessionRecvBufferSize", m_sessionRecvBufferSize)
                       << LOG_KV("allowMaxMsgSize", m_allowMaxMsgSize)
                       << LOG_KV("maxReadDataSize", m_maxReadDataSize)
                       << LOG_KV("maxSendDataSize", m_maxSendDataSize)
                       << LOG_KV("maxSendMsgCountS", m_maxSendMsgCountS)
                       << LOG_KV("enableCompress", m_enableCompress);
        return session;
    }

private:
    std::string m_hostNodeID;
    uint32_t m_sessionRecvBufferSize;
    uint32_t m_allowMaxMsgSize{0};
    uint32_t m_maxReadDataSize{0};
    uint32_t m_maxSendDataSize{0};
    uint32_t m_maxSendMsgCountS{0};
    bool m_enableCompress = true;
};

}  // namespace gateway
}  // namespace bcos
