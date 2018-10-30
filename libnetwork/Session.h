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

#define CallbackFunc std::function<void(NetworkException, Message::Ptr)>
#define CallbackFuncWithSession \
    std::function<void(NetworkException, std::shared_ptr<dev::p2p::Session>, Message::Ptr)>

struct ResponseCallback : public std::enable_shared_from_this<ResponseCallback>
{
    typedef std::shared_ptr<ResponseCallback> Ptr;

    uint64_t m_startTime;
    CallbackFunc callbackFunc;
    std::shared_ptr<boost::asio::deadline_timer> timeoutHandler;
};

class Session : public SessionFace, public std::enable_shared_from_this<Session>
{
public:
    Session(std::weak_ptr<Host> _server,
        std::shared_ptr<SocketFace> const& _s, std::shared_ptr<Peer> const& _n,
        PeerSessionInfo const& _info);

    virtual ~Session();

    typedef std::shared_ptr<Session> Ptr;

    void start() override;
    void disconnect(DisconnectReason _reason) override;

    virtual bool isConnected() const override { return m_socket->isConnected(); }

    virtual PeerSessionInfo info() const override
    {
        Guard l(x_info);
        return m_info;
    }

    virtual std::shared_ptr<Peer> peer() const override { return m_peer; }

    virtual void asyncSendMessage(Message::Ptr message, Options options = Options(), CallbackFunc callback = CallbackFunc());
    virtual Message::Ptr sendMessage(Message::Ptr message, Options options = Options());

    virtual void send(std::shared_ptr<bytes> _msg) override;

    virtual NodeIPEndpoint nodeIPEndpoint() const override { return m_socket->nodeIPEndpoint(); }

    virtual std::weak_ptr<Host> host() { return m_server; }

    virtual MessageFactory::Ptr messageFactory() const override { return m_messageFactory; }

    virtual void onTimeout(const boost::system::error_code& error, uint32_t seq);

    void setMessageFactory(MessageFactory::Ptr _messageFactory) override
    {
        m_messageFactory = _messageFactory;
    }

    const size_t bufferLength = 1024;

protected:
    /// Perform a read on the socket.
    virtual void doRead();
    std::vector<byte> m_data;  ///< Buffer for ingress packet data.
    byte m_recvBuffer[1024];

private:
    /// Drop the connection for the reason @a _r.
    void drop(DisconnectReason _r);

    /// Check error code after reading and drop peer if error code.
    bool checkRead(boost::system::error_code _ec);

    /// Perform a single round of the write operation. This could end up calling itself
    /// asynchronously.
    void onWrite(boost::system::error_code ec, std::size_t length, std::shared_ptr<bytes> buffer);
    void write();

    /// call by doRead() to deal with mesage
    void onMessage(NetworkException const& e, std::shared_ptr<Session> session, Message::Ptr message);

    std::weak_ptr<Host> m_server;                        ///< The host that owns us. Never null.
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

    std::shared_ptr<Peer> m_peer;  ///< The Peer object.

    mutable Mutex x_info;
    PeerSessionInfo m_info;  ///< Dynamic information about this peer.

    bool m_actived = false;

    ///< A call B, the function to call after the response is received by A.
    mutable RecursiveMutex x_seq2Callback;
    std::shared_ptr<std::unordered_map<uint32_t, ResponseCallback::Ptr>> m_seq2Callback;
    std::shared_ptr<dev::ThreadPool> m_threadPool;

    std::function<void(NetworkException, Session::Ptr, Message::Ptr)> m_messageHandler;
};

}  // namespace p2p
}  // namespace dev
