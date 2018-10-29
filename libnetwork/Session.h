/*
    This file is part of cpp-ethereum.

    cpp-ethereum is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    cpp-ethereum is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file Session.h
 * @author Gav Wood <i@gavwood.com>
 * @author Alex Leverington <nessence@gmail.com>
 * @date 2014
 * @author toxotguo
 * @date 2018
 */

#pragma once

#include <libdevcore/Common.h>
#include <libdevcore/Guards.h>
#include <libdevcore/RLP.h>
#include <libdevcore/ThreadPool.h>
#include <boost/heap/priority_queue.hpp>
#include <array>
#include <deque>
#include <memory>
#include <mutex>
#include <set>
#include <utility>

#include "Common.h"
#include "SessionFace.h"
#include "SocketFace.h"
namespace dev
{
namespace p2p
{
class Peer;
class P2PMsgHandler;
class Host;
/**
 * @brief The Session class
 * @todo Document fully.
 */
class Session : public SessionFace, public std::enable_shared_from_this<Session>
{
public:
    Session(Host* _server, std::shared_ptr<SocketFace> const& _s, std::shared_ptr<Peer> const& _n,
        PeerSessionInfo const& _info);

    virtual ~Session();

    void start() override;
    void disconnect(DisconnectReason _reason) override;

    bool isConnected() const override { return m_socket->isConnected(); }

    NodeID id() const override;

    PeerSessionInfo info() const override
    {
        Guard l(x_info);
        return m_info;
    }
    std::chrono::steady_clock::time_point connectionTime() override { return m_connect; }

    std::shared_ptr<Peer> peer() const override { return m_peer; }

    std::chrono::steady_clock::time_point lastReceived() const override { return m_lastReceived; }

    void setP2PMsgHandler(std::shared_ptr<P2PMsgHandler> _p2pMsgHandler) override
    {
        m_p2pMsgHandler = _p2pMsgHandler;
    }

    void send(std::shared_ptr<bytes> _msg) override;

    ///< interface to set and get topicSeq
    void setTopicSeq(uint32_t _topicSeq) override { m_topicSeq = _topicSeq; }
    uint32_t topicSeq() const { return m_topicSeq; }

    ///< interface to set and get topics
    void setTopics(std::shared_ptr<std::vector<std::string>> _topics) override
    {
        m_topics = _topics;
    }
    std::shared_ptr<std::vector<std::string>> topics() const override { return m_topics; }

    bool addSeq2Callback(uint32_t seq, ResponseCallback::Ptr const& callback) override;
    ResponseCallback::Ptr getCallbackBySeq(uint32_t seq) override;
    bool eraseCallbackBySeq(uint32_t seq) override;

    NodeIPEndpoint nodeIPEndpoint() const override { return m_socket->nodeIPEndpoint(); }

    Host* host() { return m_server; }

    ///< interface to get topicSeq of counter node
    uint32_t peerTopicSeq(NodeID const& nodeID);

    ///< interface to set topicSeq/topics of counter node
    void setTopicsAndTopicSeq(NodeID const& nodeID,
        std::shared_ptr<std::vector<std::string>> _topics, uint32_t _topicSeq);
    void setThreadPool(std::shared_ptr<dev::ThreadPool> threadPool) { m_threadPool = threadPool; }

    MessageFactory::Ptr messageFactory() const override { return m_messageFactory; }

    void setMessageFactory(MessageFactory::Ptr _messageFactory) override
    {
        m_messageFactory = _messageFactory;
    }

    const size_t bufferLength = 1024;

protected:
    /// Perform a read on the socket.
    virtual void doRead();
    //    void setTest(bool const& _test) { m_test = _test; }
    std::vector<byte> m_data;  ///< Buffer for ingress packet data.
    byte m_recvBuffer[1024];

private:
    /// Drop the connection for the reason @a _r.
    void drop(DisconnectReason _r);

    /// Check error code after reading and drop peer if error code.
    bool checkRead(boost::system::error_code _ec);

    /// Perform a single round of the write operation. This could end up calling itself
    /// asynchronously.
    void onWrite(boost::system::error_code ec, std::size_t length);
    void write();

    /// call by doRead() to deal with mesage
    void onMessage(P2PException const& e, std::shared_ptr<Session> session, Message::Ptr message);

    bool CheckGroupIDAndSender(PROTOCOL_ID protocolID, std::shared_ptr<Session> session);

    Host* m_server;                        ///< The host that owns us. Never null.
    std::shared_ptr<SocketFace> m_socket;  ///< Socket of peer's connection.
    Mutex x_framing;                       ///< Mutex for the write queue.
    MessageFactory::Ptr m_messageFactory;

    class QueueCompare
    {
    public:
        bool operator()(const std::pair<std::shared_ptr<bytes>, u256>& lhs,
            const std::pair<std::shared_ptr<bytes>, u256>& rhs) const
        {
            return false;
        }
    };

    boost::heap::priority_queue<std::pair<std::shared_ptr<bytes>, u256>,
        boost::heap::compare<QueueCompare>, boost::heap::stable<true>>
        m_writeQueue;

    bytes m_incoming;  ///< Read buffer for ingress bytes.

    std::shared_ptr<Peer> m_peer;  ///< The Peer object.
    bool m_dropped = false;  ///< If true, we've already divested ourselves of this peer. We're just
                             ///< waiting for the reads & writes to fail before the shared_ptr goes
                             ///< OOS and the destructor kicks in.

    mutable Mutex x_info;
    PeerSessionInfo m_info;  ///< Dynamic information about this peer.

    std::chrono::steady_clock::time_point m_connect;       ///< Time point of connection.
    std::chrono::steady_clock::time_point m_ping;          ///< Time point of last ping.
    std::chrono::steady_clock::time_point m_lastReceived;  ///< Time point of last message.

    unsigned m_start_t;

    boost::asio::io_service::strand* m_strand;

    std::shared_ptr<P2PMsgHandler> m_p2pMsgHandler;
    //    bool m_test = false;  /// for unit test

    uint32_t m_topicSeq = 0;  ///< Represents the topics situation at a certain stage. When topics
                              ///< change, increase m_topicSeq.
    std::shared_ptr<std::vector<std::string>> m_topics;  ///< Topic being concerned by this
                                                         ///< session/node.

    ///< A call B, the function to call after the response is received by A.
    mutable RecursiveMutex x_seq2Callback;
    std::shared_ptr<std::unordered_map<uint32_t, ResponseCallback::Ptr>> m_seq2Callback;
    std::shared_ptr<dev::ThreadPool> m_threadPool;
};

}  // namespace p2p
}  // namespace dev
