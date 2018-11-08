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
/** @file Session.cpp
 * @author Gav Wood <i@gavwood.com>
 * @author Alex Leverington <nessence@gmail.com>
 * @date 2014
 * @author toxotguo
 * @date 2018
 */

#include "Session.h"
#include "Host.h"
#include "P2PMsgHandler.h"
#include <libdevcore/Common.h>
#include <libdevcore/CommonIO.h>
#include <libdevcore/CommonJS.h>
#include <libdevcore/Exceptions.h>
#include <libdevcore/easylog.h>
#include <chrono>
using namespace std;
using namespace dev;
using namespace dev::p2p;

Session::Session(Host* _server, std::shared_ptr<SocketFace> const& _s,
    std::shared_ptr<Peer> const& _n, PeerSessionInfo const& _info)
  : m_server(_server),
    m_socket(_s),
    m_peer(_n),
    m_info(_info),
    m_ping(chrono::steady_clock::time_point::max())
{
    m_peer->m_lastDisconnect = NoDisconnect;
    m_lastReceived = m_connect = chrono::steady_clock::now();
    DEV_GUARDED(x_info)
    m_info.socketId = m_socket->ref().native_handle();


    m_strand = _server->strand();

    m_topics = std::make_shared<std::vector<std::string>>();
    m_seq2Callback = std::make_shared<std::unordered_map<uint32_t, ResponseCallback::Ptr>>();
}

Session::~Session()
{
    ThreadContext tc(info().id.abridged());
    ThreadContext tc2(info().host);
    SESSION_LOG(INFO) << "[#Session] closing peer session :-(";
    m_peer->m_lastConnected = m_peer->m_lastAttempted - chrono::seconds(1);

    try
    {
        bi::tcp::socket& socket = m_socket->ref();
        if (m_socket->isConnected())
        {
            boost::system::error_code ec;

            // shutdown may block servals seconds - morebtcg
            // socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
            socket.close();
        }
    }
    catch (...)
    {
        SESSION_LOG(ERROR) << "[#Session] deconstruct Session exception";
    }
}

NodeID Session::id() const
{
    return m_peer ? m_peer->id() : NodeID();
}

template <class T>
vector<T> randomSelection(vector<T> const& _t, unsigned _n)
{
    if (_t.size() <= _n)
        return _t;
    vector<T> ret = _t;
    while (ret.size() > _n)
    {
        auto i = ret.begin();
        advance(i, rand() % ret.size());
        ret.erase(i);
    }
    return ret;
}

void Session::send(std::shared_ptr<bytes> _msg)
{
    bytesConstRef msg(_msg.get());

    if (!m_socket->isConnected())
        return;

    bool doWrite = false;
    DEV_GUARDED(x_framing)
    {
        m_writeQueue.push(make_pair(_msg, u256(utcTime())));
        doWrite = (m_writeQueue.size() == 1);
    }
    if (doWrite)
        write();
}

void Session::onWrite(boost::system::error_code ec, std::size_t length)
{
    try
    {
        unsigned elapsed = (unsigned)(utcTime() - m_start_t);
        if (elapsed >= 10)
        {
            SESSION_LOG(WARNING) << "[#onWrite] ba::async_write [write-time/len/id]: " << elapsed
                                 << ",len=" << length << "/" << id();
        }
        ThreadContext tc(info().id.abridged());
        ThreadContext tc2(info().host);
        // must check queue, as write callback can occur following dropped()
        if (ec)
        {
            SESSION_LOG(WARNING) << "[#onWrite] error sending: [EINFO]: " << ec.message();
            drop(TCPError);
            return;
        }
        DEV_GUARDED(x_framing)
        {
            m_writeQueue.pop();
            if (m_writeQueue.empty())
            {
                return;
            }
        }
        write();
    }
    catch (exception& e)
    {
        SESSION_LOG(ERROR) << "[#onWrite] [EINFO]: " << e.what();
        drop(TCPError);
        return;
    }
}

void Session::write()
{
    try
    {
        std::pair<std::shared_ptr<bytes>, u256> task;
        u256 enter_time = u256(0);
        DEV_GUARDED(x_framing)
        {
            task = m_writeQueue.top();
            enter_time = task.second;
        }

        auto self(shared_from_this());
        m_start_t = utcTime();
        unsigned queue_elapsed = (unsigned)(m_start_t - enter_time);
        if (queue_elapsed > 10)
        {
            SESSION_LOG(WARNING) << "[#write] [queue-time]: " << queue_elapsed;
        }

        auto session = shared_from_this();

        if (m_socket->isConnected())
        {
            m_server->ioService()->post([=] {
                m_server->asioInterface()->async_write(m_socket, boost::asio::buffer(*(task.first)),
                    boost::bind(&Session::onWrite, session, boost::asio::placeholders::error,
                        boost::asio::placeholders::bytes_transferred));
            });
        }
        else
        {
            SESSION_LOG(WARNING) << "[#write] error sending ssl socket is close!";
            drop(TCPError);
            return;
        }
    }
    catch (exception& e)
    {
        SESSION_LOG(ERROR) << "[#write] [EINFO]: " << e.what();
        drop(TCPError);
        return;
    }
}

void Session::drop(DisconnectReason _reason)
{
    if (m_dropped)
        return;
    m_dropped = true;

    SESSION_LOG(DEBUG) << "[#drop] call and erase all callbackFunc in this session!";
    for (auto it : *m_seq2Callback)
    {
        if (it.second->timeoutHandler)
        {
            ///< cancel timer
            it.second->timeoutHandler->cancel();
        }
        if (it.second->callbackFunc)
        {
            SESSION_LOG(DEBUG) << "[#drop] call callbackFunc [seq]: " << it.first;
            ///< TODO: use threadPool
            P2PException e(
                P2PExceptionType::Disconnect, g_P2PExceptionMsg[P2PExceptionType::Disconnect]);
            /// it.second->callbackFunc(e, Message::Ptr());
            m_threadPool->enqueue([=]() {
                it.second->callbackFunc(e, Message::Ptr());
                eraseCallbackBySeq(it.first);
            });
        }
    }

    bi::tcp::socket& socket = m_socket->ref();
    if (m_socket->isConnected())
        try
        {
            boost::system::error_code ec;

            // shutdown may block servals seconds - morebtcg
            // socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
            SESSION_LOG(WARNING) << "[#drop] closing: [socket/reason/address/ec]: "
                                 << socket.remote_endpoint(ec) << "/" << reasonOf(_reason) << "/"
                                 << m_peer->address() << "/" << ec.message();

            socket.close();
        }
        catch (...)
        {
        }
    m_peer->m_lastDisconnect = _reason;
    if (TCPError == _reason)
        m_server->reconnectNow();
}

void Session::disconnect(DisconnectReason _reason)
{
    SESSION_LOG(WARNING) << "[#disconnect] disconnecting: [our reason] :" << reasonOf(_reason);
    drop(_reason);
}

void Session::start()
{
    /// m_strand->post(boost::bind(&Session::doRead, this));  // doRead();
    m_server->asioInterface()->strand_post(
        *m_strand, boost::bind(&Session::doRead, this));  // doRead();
}

void Session::doRead()
{
    // ignore packets received while waiting to disconnect.
    if (m_dropped)
        return;
    auto self(shared_from_this());
    auto asyncRead = [this, self](boost::system::error_code ec, std::size_t bytesTransferred) {
        if (ec)
        {
            SESSION_LOG(WARNING) << "[#doRead] error Reading ssl socket is close!";
            drop(TCPError);
            return;
        }
        m_data.insert(m_data.end(), m_recvBuffer, m_recvBuffer + bytesTransferred);

        ThreadContext tc(info().id.abridged());
        ThreadContext tc2(info().host);

        while (true)
        {
            Message::Ptr message = m_messageFactory->buildMessage();
            ssize_t result = message->decode(m_data.data(), m_data.size());
            if (result > 0)
            {
                SESSION_LOG(TRACE) << "[#doRead] decode success: [result]: " << result;
                P2PException e(
                    P2PExceptionType::Success, g_P2PExceptionMsg[P2PExceptionType::Success]);
                onMessage(e, self, message);
                m_data.erase(m_data.begin(), m_data.begin() + result);
            }
            else if (result == 0)
            {
                doRead();
                break;
            }
            else
            {
                P2PException e(P2PExceptionType::ProtocolError,
                    g_P2PExceptionMsg[P2PExceptionType::ProtocolError]);
                onMessage(e, self, message);
                break;
            }
        }
    };
    if (m_socket->isConnected())
    {
        m_server->asioInterface()->async_read_some(
            m_socket, *m_strand, boost::asio::buffer(m_recvBuffer, bufferLength), asyncRead);
    }
    else
    {
        SESSION_LOG(WARNING) << "[#doRead] error Reading ssl socket is close!";
        drop(TCPError);
        return;
    }
}

bool Session::checkRead(boost::system::error_code _ec)
{
    if (_ec && _ec.category() != boost::asio::error::get_misc_category() &&
        _ec.value() != boost::asio::error::eof)
    {
        SESSION_LOG(WARNING) << "[#checkRead] error reading: [EINFO]: " << _ec.message();
        drop(TCPError);

        return false;
    }

    return true;
}

bool Session::CheckGroupIDAndSender(PROTOCOL_ID protocolID, std::shared_ptr<Session> session)
{
    std::pair<GROUP_ID, MODULE_ID> ret = getGroupAndProtocol(protocolID);
    if (0 == ret.first)
    {
        return true;
    }
    h512s nodeList;
    if (m_server->getNodeListByGroupID(int(ret.first), nodeList))
    {
        if (find(nodeList.begin(), nodeList.end(), session->id()) == nodeList.end())
        {
            ///< The message sender is not a member of the group
            SESSION_LOG(WARNING)
                << "[#CheckGroupIDAndSender] the message sender is not a member of the group!";
            return false;
        }
    }
    else
    {
        ///< I didn't join the group
        SESSION_LOG(WARNING) << "[#CheckGroupIDAndSender] I didn't join the group!";
        return false;
    }

    return true;
}

void Session::onMessage(
    P2PException const& e, std::shared_ptr<Session> session, Message::Ptr message)
{
    PROTOCOL_ID protocolID = message->protocolID();
    if (message->isRequestPacket())
    {
        ///< Whitelist mechanism, check groupID and sender of this message
        if (false == CheckGroupIDAndSender(protocolID, session))
            return;

        ///< request package, get callback by protocolID
        CallbackFuncWithSession callbackFunc;

        bool ret = m_p2pMsgHandler->getHandlerByProtocolID(protocolID, callbackFunc);
        if (ret && callbackFunc)
        {
            SESSION_LOG(DEBUG) << "[#onMessage] call callbackFunc: [protocolID]: " << protocolID;
            ///< execute funtion, send response packet by user in callbackFunc
            ///< TODO: use threadPool
            m_threadPool->enqueue(
                [callbackFunc, e, session, message]() { callbackFunc(e, session, message); });
        }
        else
        {
            SESSION_LOG(WARNING) << "[#onMessage] handler not found: [protocolID]: " << protocolID;
        }
    }
    else if (protocolID != 0)
    {
        ///< response package, get callback by seq
        ResponseCallback::Ptr callback = getCallbackBySeq(message->seq());
        if (callback != NULL)
        {
            if (callback->timeoutHandler)
            {
                ///< cancel timer
                callback->timeoutHandler->cancel();
            }
            if (callback->callbackFunc)
            {
                SESSION_LOG(WARNING)
                    << "[#onMessage] call callbackFunc in seq: [seq]: " << message->seq();
                ///< TODO: use threadPool
                m_threadPool->enqueue(
                    [callback, e, message]() { callback->callbackFunc(e, message); });
            }
            else
            {
                SESSION_LOG(WARNING)
                    << "[#onMessage] callbackFunc not found in seq: [seq]: " << message->seq();
            }
        }
        else
        {
            SESSION_LOG(WARNING) << "[#onMessage] callback not found by seq: [seq]: "
                                 << message->seq();
        }
    }
    else
    {
        SESSION_LOG(WARNING) << "[#onMessage] protocolID cannot be 0.";
    }
}

bool Session::addSeq2Callback(uint32_t seq, ResponseCallback::Ptr const& callback)
{
    RecursiveGuard l(x_seq2Callback);
    if (m_seq2Callback->find(seq) == m_seq2Callback->end())
    {
        m_seq2Callback->insert(std::make_pair(seq, callback));
        return true;
    }
    else
    {
        SESSION_LOG(WARNING) << "[#getCallbackBySeq] handler repetition: [seqID]: " << seq;
        return false;
    }
}

ResponseCallback::Ptr Session::getCallbackBySeq(uint32_t seq)
{
    RecursiveGuard l(x_seq2Callback);
    auto it = m_seq2Callback->find(seq);
    if (it != m_seq2Callback->end())
    {
        return it->second;
    }
    else
    {
        SESSION_LOG(WARNING) << "[#getCallbackBySeq] handler not found: [seqID]: " << seq;
        return NULL;
    }
}

bool Session::eraseCallbackBySeq(uint32_t seq)
{
    RecursiveGuard l(x_seq2Callback);
    if (m_seq2Callback->find(seq) != m_seq2Callback->end())
    {
        m_seq2Callback->erase(seq);
        return true;
    }
    else
    {
        SESSION_LOG(WARNING) << "[#eraseCallbackBySeq] handler not found: [seqID]: " << seq;
        return false;
    }
}

uint32_t Session::peerTopicSeq(NodeID const& nodeID)
{
    uint32_t seq = uint32_t(-1);
    try
    {
        RecursiveGuard l(m_server->mutexSessions());
        auto s = m_server->sessions();
        for (auto const& i : s)
        {
            if (i.first == nodeID)
            {
                seq = i.second->topicSeq();
                break;
            }
        }
    }
    catch (std::exception& e)
    {
        SESSION_LOG(ERROR) << "[#peerTopicSeq] [EINFO]: " << e.what();
    }
    return seq;
}

void Session::setTopicsAndTopicSeq(
    NodeID const& nodeID, std::shared_ptr<std::vector<std::string>> _topics, uint32_t _topicSeq)
{
    try
    {
        RecursiveGuard l(m_server->mutexSessions());
        auto s = m_server->sessions();
        for (auto const& i : s)
        {
            if (i.first == nodeID)
            {
                std::shared_ptr<SessionFace> p = i.second;
                p->setTopics(_topics);
                p->setTopicSeq(_topicSeq);
                break;
            }
        }
    }
    catch (std::exception& e)
    {
        SESSION_LOG(ERROR) << "[#setTopicsAndTopicSeq] [EINFO]: " << e.what();
    }
}
