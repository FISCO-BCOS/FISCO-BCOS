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

#include <libdevcore/Common.h>
#include <libdevcore/CommonIO.h>
#include <libdevcore/CommonJS.h>
#include <libdevcore/Exceptions.h>
#include <libdevcore/ThreadPool.h>
#include <libdevcore/easylog.h>
#include <chrono>

#include "ASIOInterface.h"
#include "Host.h"
#include "SessionFace.h"

using namespace dev;
using namespace dev::p2p;

Session::Session()
{
    m_seq2Callback = std::make_shared<std::unordered_map<uint32_t, ResponseCallback::Ptr>>();
}

Session::~Session()
{
    LOG(INFO) << "Closing peer session";

    try
    {
        if (m_socket)
        {
            bi::tcp::socket& socket = m_socket->ref();
            if (m_socket->isConnected())
            {
                boost::system::error_code ec;

                socket.close();
            }
        }
    }
    catch (...)
    {
        LOG(ERROR) << "Deconstruct Session exception";
    }
}

void Session::asyncSendMessage(
    Message::Ptr message, Options options = Options(), CallbackFunc callback = CallbackFunc())
{
    auto server = m_server.lock();
    if (!actived())
    {
        LOG(WARNING) << "Session inactived";

        server->threadPool()->enqueue(
            [callback] { callback(NetworkException(-1, "Session inactived"), Message::Ptr()); });

        return;
    }

    LOG(TRACE) << "Session sendMessage seq: " << message->seq();
    auto handler = std::make_shared<ResponseCallback>();
    handler->callbackFunc = callback;
    if (options.timeout > 0)
    {
        std::shared_ptr<boost::asio::deadline_timer> timeoutHandler =
            server->asioInterface()->newTimer(options.timeout);

        auto session = std::weak_ptr<Session>(shared_from_this());
        timeoutHandler->async_wait(boost::bind(&Session::onTimeout, shared_from_this(),
            boost::asio::placeholders::error, message->seq()));

        handler->timeoutHandler = timeoutHandler;
        handler->m_startTime = utcTime();
    }

    addSeqCallback(message->seq(), handler);
    // m_seq2Callback->insert(std::make_pair(message->seq(), handler));

    auto buffer = std::make_shared<bytes>();
    message->encode(*buffer);

    send(buffer);
}

bool Session::actived() const
{
    auto server = m_server.lock();
    if (m_actived && server && server->haveNetwork())
        return true;
    return false;
}

void Session::send(std::shared_ptr<bytes> _msg)
{
    if (!actived())
    {
        return;
    }

    if (!m_socket->isConnected())
        return;

    LOG(TRACE) << "Session send, writeQueue: " << m_writeQueue.size();
    {
        Guard l(x_writeQueue);

        m_writeQueue.push(make_pair(_msg, u256(utcTime())));
    }

    write();
}

void Session::onWrite(
    boost::system::error_code ec, std::size_t length, std::shared_ptr<bytes> buffer)
{
    if (!actived())
    {
        return;
    }

    try
    {
        if (ec)
        {
            LOG(WARNING) << "Error sending: " << ec.message() << " at " << nodeIPEndpoint().name();
            ;
            drop(TCPError);
            return;
        }

        LOG(TRACE) << "Successfully send " << length << " bytes";

        {
            Guard l(x_writeQueue);
            if (m_writing)
            {
                m_writing = false;
            }
        }

        write();
    }
    catch (std::exception& e)
    {
        LOG(ERROR) << "Error:" << e.what();
        drop(TCPError);
        return;
    }
}

void Session::write()
{
    if (!actived())
    {
        return;
    }

    try
    {
        Guard l(x_writeQueue);

        if (m_writing)
        {
            return;
        }

        m_writing = true;

        std::pair<std::shared_ptr<bytes>, u256> task;
        u256 enter_time = u256(0);

        if (m_writeQueue.empty())
        {
            m_writing = false;
            return;
        }

        task = m_writeQueue.top();
        m_writeQueue.pop();

        enter_time = task.second;
        auto session = shared_from_this();
        auto buffer = task.first;

        auto server = m_server.lock();
        if (server && server->haveNetwork())
        {
            if (m_socket->isConnected())
            {
                LOG(TRACE) << "Start send " << buffer->size() << " bytes data";
                server->asioInterface()->asyncWrite(m_socket, boost::asio::buffer(*buffer),
                    boost::bind(&Session::onWrite, session, boost::asio::placeholders::error,
                        boost::asio::placeholders::bytes_transferred, buffer));
            }
            else
            {
                LOG(WARNING) << "Error sending ssl socket is close!";
                drop(TCPError);
                return;
            }
        }
        else
        {
            LOG(WARNING) << "Host is gone";
            drop(TCPError);
            return;
        }
    }
    catch (std::exception& e)
    {
        LOG(ERROR) << "Error:" << e.what();
        drop(TCPError);
        return;
    }
}

void Session::drop(DisconnectReason _reason)
{
    auto server = m_server.lock();
    if (!m_actived)
        return;

    m_actived = false;

    int errorCode = P2PExceptionType::Disconnect;
    if (_reason == DuplicatePeer)
    {
        errorCode = P2PExceptionType::DuplicateSession;
    }

    LOG(INFO) << "Session::drop, call and erase all callbackFunc in this session!";
    for (auto it : *m_seq2Callback)
    {
        if (it.second->timeoutHandler)
        {
            it.second->timeoutHandler->cancel();
        }
        if (it.second->callbackFunc)
        {
            LOG(TRACE) << "Session::drop, call callbackFunc by seq=" << it.first;
            if (server)
            {
                auto callback = it.second;
                server->threadPool()->enqueue([callback, errorCode]() {
                    callback->callbackFunc(
                        NetworkException(errorCode, g_P2PExceptionMsg[errorCode]), Message::Ptr());
                });
            }
        }
    }
    m_seq2Callback->clear();

    if (server && m_messageHandler)
    {
        auto handler = m_messageHandler;
        auto self = shared_from_this();
        server->threadPool()->enqueue([handler, self, errorCode]() {
            handler(
                NetworkException(errorCode, g_P2PExceptionMsg[errorCode]), self, Message::Ptr());
        });
    }

    bi::tcp::socket& socket = m_socket->ref();
    if (m_socket->isConnected())
    {
        try
        {
            boost::system::error_code ec;

            LOG(WARNING) << "Closing " << socket.remote_endpoint(ec) << "(" << reasonOf(_reason)
                         << ")" << m_socket->nodeIPEndpoint().address << "," << ec.message();

            socket.close();
        }
        catch (...)
        {
        }
    }
}

void Session::disconnect(DisconnectReason _reason)
{
    LOG(WARNING) << "Disconnecting (our reason:" << reasonOf(_reason) << ")";
    drop(_reason);
}

void Session::start()
{
    if (!m_actived)
    {
        auto server = m_server.lock();
        if (server && server->haveNetwork())
        {
            server->asioInterface()->strandPost(
                boost::bind(&Session::doRead, shared_from_this()));  // doRead();

            m_actived = true;
        }
    }
}

void Session::doRead()
{
    auto server = m_server.lock();
    if (m_actived && server && server->haveNetwork())
    {
        auto self(shared_from_this());
        auto asyncRead = [this, self](boost::system::error_code ec, std::size_t bytesTransferred) {
            if (ec)
            {
                LOG(WARNING) << "Error reading: " << ec.message() << " at "
                             << self->nodeIPEndpoint().name();
                drop(TCPError);
                return;
            }
            LOG(TRACE) << "Read: " << bytesTransferred << " bytes data:"
                       << std::string(m_recvBuffer, m_recvBuffer + bytesTransferred);
            m_data.insert(m_data.end(), m_recvBuffer, m_recvBuffer + bytesTransferred);

            while (true)
            {
                Message::Ptr message = m_messageFactory->buildMessage();
                ssize_t result = message->decode(m_data.data(), m_data.size());
                LOG(TRACE) << "Parse result: " << result;
                if (result > 0)
                {
                    LOG(TRACE) << "Decode success: " << result;
                    NetworkException e(
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
                    LOG(ERROR) << "Decode message error: " << result;
                    onMessage(NetworkException(P2PExceptionType::ProtocolError,
                                  g_P2PExceptionMsg[P2PExceptionType::ProtocolError]),
                        self, message);
                    break;
                }
            }
        };

        if (m_socket->isConnected())
        {
            LOG(TRACE) << "Start read";
            server->asioInterface()->asyncReadSome(
                m_socket, boost::asio::buffer(m_recvBuffer, BUFFER_LENGTH), asyncRead);
        }
        else
        {
            LOG(WARNING) << "Error Reading ssl socket is close!";
            drop(TCPError);
            return;
        }
    }
}

bool Session::checkRead(boost::system::error_code _ec)
{
    if (_ec && _ec.category() != boost::asio::error::get_misc_category() &&
        _ec.value() != boost::asio::error::eof)
    {
        LOG(WARNING) << "Error reading: " << _ec.message();
        drop(TCPError);

        return false;
    }

    return true;
}

void Session::onMessage(
    NetworkException const& e, std::shared_ptr<Session> session, Message::Ptr message)
{
    auto server = m_server.lock();
    if (m_actived && server && server->haveNetwork())
    {
        auto it = m_seq2Callback->find(message->seq());
        if (it != m_seq2Callback->end())
        {
            LOG(TRACE) << "Found callback: " << message->seq();

            if (it->second->timeoutHandler)
            {
                it->second->timeoutHandler->cancel();
            }

            if (it->second->callbackFunc)
            {
                auto callback = it->second->callbackFunc;
                if (callback)
                {
                    auto self = std::weak_ptr<Session>(shared_from_this());
                    server->threadPool()->enqueue([e, callback, self, message]() {
                        callback(e, message);

                        auto s = self.lock();
                        if (s)
                        {
                            s->removeSeqCallback(message->seq());
                        }
                    });
                }
            }
        }
        else
        {
            LOG(TRACE) << "Not found callback, call messageHandler: " << message->seq();

            if (m_messageHandler)
            {
                auto session = shared_from_this();
                auto handler = m_messageHandler;

                server->threadPool()->enqueue(
                    [session, handler, e, message]() { handler(e, session, message); });
            }
            else
            {
                LOG(WARNING) << "MessageHandler not found";
            }
        }
    }
}

void Session::onTimeout(const boost::system::error_code& error, uint32_t seq)
{
    if (error)
    {
        LOG(TRACE) << "timer cancel" << error;
        return;
    }

    auto server = m_server.lock();

    auto it = m_seq2Callback->find(seq);
    if (it != m_seq2Callback->end())
    {
        if (server)
        {
            server->threadPool()->enqueue([=]() {
                NetworkException e(P2PExceptionType::NetworkTimeout,
                    g_P2PExceptionMsg[P2PExceptionType::NetworkTimeout]);
                it->second->callbackFunc(e, Message::Ptr());

                m_seq2Callback->erase(it);
            });
        }
    }
}
