
/** @file Session.h
 * @author monan <651932351@qq.com>
 * @date 2018
 */

#pragma once

#include <bcos-utilities/Common.h>
#include <boost/heap/priority_queue.hpp>
#include <array>
#include <deque>
#include <memory>
#include <mutex>
#include <set>
#include <utility>

#include <bcos-gateway/libnetwork/Common.h>
#include <bcos-gateway/libnetwork/SessionFace.h>

namespace bcos
{
namespace gateway
{
class Host;
class SocketFace;

class Session : public SessionFace, public std::enable_shared_from_this<Session>
{
public:
    Session(size_t _bufferSize = 4096);
    virtual ~Session();

    using Ptr = std::shared_ptr<Session>;

    void start() override;
    void disconnect(DisconnectReason _reason) override;

    void asyncSendMessage(
        Message::Ptr, Options = Options(), SessionCallbackFunc = SessionCallbackFunc()) override;

    NodeIPEndpoint nodeIPEndpoint() const override;

    bool actived() const override;

    virtual std::weak_ptr<Host> host() { return m_server; }
    virtual void setHost(std::weak_ptr<Host> host);

    std::shared_ptr<SocketFace> socket() override { return m_socket; }
    virtual void setSocket(std::shared_ptr<SocketFace> socket) { m_socket = socket; }

    virtual MessageFactory::Ptr messageFactory() const { return m_messageFactory; }
    virtual void setMessageFactory(MessageFactory::Ptr _messageFactory)
    {
        m_messageFactory = _messageFactory;
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

    void setHostNodeID(std::string const& _hostNodeID) { m_hostNodeID = _hostNodeID; }

protected:
    virtual void addSeqCallback(uint32_t seq, ResponseCallback::Ptr callback)
    {
        RecursiveGuard l(x_seq2Callback);
        m_seq2Callback->insert(std::make_pair(seq, callback));
    }
    virtual void removeSeqCallback(uint32_t seq)
    {
        RecursiveGuard l(x_seq2Callback);
        m_seq2Callback->erase(seq);
    }
    virtual void clearSeqCallback()
    {
        RecursiveGuard l(x_seq2Callback);
        m_seq2Callback->clear();
    }

    ResponseCallback::Ptr getCallbackBySeq(uint32_t seq)
    {
        RecursiveGuard l(x_seq2Callback);
        auto it = m_seq2Callback->find(seq);
        if (it != m_seq2Callback->end())
        {
            return it->second;
        }
        else
        {
            return NULL;
        }
    }

    void callDefaultMsgHandler(NetworkException const& e, Message::Ptr message);

private:
    void send(std::shared_ptr<bytes> _msg);

    void doRead();
    std::vector<byte> m_data;  ///< Buffer for ingress packet data.
    std::vector<byte> m_recvBuffer;
    const size_t bufferSize;

    /// Drop the connection for the reason @a _r.
    void drop(DisconnectReason _r);

    /// Check error code after reading and drop peer if error code.
    bool checkRead(boost::system::error_code _ec);

    void onTimeout(const boost::system::error_code& error, uint32_t seq);
    void updateIdleTimer(std::shared_ptr<boost::asio::deadline_timer> _idleTimer);
    void onIdle(const boost::system::error_code& error);

    /// Perform a single round of the write operation. This could end up calling
    /// itself asynchronously.
    void onWrite(boost::system::error_code ec, std::size_t length, std::shared_ptr<bytes> buffer);
    void write();

    /// call by doRead() to deal with mesage
    void onMessage(NetworkException const& e, Message::Ptr message);

    std::weak_ptr<Host> m_server;          ///< The host that owns us. Never null.
    std::shared_ptr<SocketFace> m_socket;  ///< Socket of peer's connection.

    MessageFactory::Ptr m_messageFactory;

    class QueueCompare
    {
    public:
        bool operator()(const std::pair<std::shared_ptr<bytes>, u256>&,
            const std::pair<std::shared_ptr<bytes>, u256>&) const
        {
            return false;
        }
    };

    boost::heap::priority_queue<std::pair<std::shared_ptr<bytes>, u256>,
        boost::heap::compare<QueueCompare>, boost::heap::stable<true>>
        m_writeQueue;
    std::atomic_bool m_writing = {false};
    bcos::Mutex x_writeQueue;

    mutable bcos::Mutex x_info;

    bool m_actived = false;

    ///< A call B, the function to call after the response is received by A.
    mutable bcos::RecursiveMutex x_seq2Callback;
    std::shared_ptr<std::unordered_map<uint32_t, ResponseCallback::Ptr>> m_seq2Callback;

    std::function<void(NetworkException, SessionFace::Ptr, Message::Ptr)> m_messageHandler;
    uint64_t m_shutDownTimeThres = 50000;
    // 1min
    uint64_t m_idleTimeInterval = 60;

    // timer to check the connection
    std::shared_ptr<boost::asio::deadline_timer> m_readIdleTimer;
    std::shared_ptr<boost::asio::deadline_timer> m_writeIdleTimer;

    std::string m_hostNodeID;
};

class SessionFactory
{
public:
    SessionFactory(std::string const& _hostNodeID) : m_hostNodeID(_hostNodeID) {}
    virtual ~SessionFactory(){};

    virtual std::shared_ptr<SessionFace> create_session(std::weak_ptr<Host> _server,
        std::shared_ptr<SocketFace> const& _socket, MessageFactory::Ptr _messageFactory)
    {
        std::shared_ptr<Session> session = std::make_shared<Session>();
        session->setHostNodeID(m_hostNodeID);
        session->setHost(_server);
        session->setSocket(_socket);
        session->setMessageFactory(_messageFactory);
        return session;
    }

private:
    std::string m_hostNodeID;
};

}  // namespace gateway
}  // namespace bcos
