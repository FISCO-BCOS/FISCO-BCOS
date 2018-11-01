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
#include <libdevcore/easylog.h>
#include <libdevcore/ThreadPool.h>
#include <chrono>

#include "Host.h"
#include "AsioInterface.h"
#include "Host.h"

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
        bi::tcp::socket& socket = m_socket->ref();
        if (m_socket->isConnected())
        {
            boost::system::error_code ec;

            socket.close();
        }
    }
    catch (...)
    {
        LOG(ERROR) << "Deconstruct Session exception";
    }
}

void Session::asyncSendMessage(Message::Ptr message, Options options = Options(), CallbackFunc callback = CallbackFunc()) {
    auto server = m_server.lock();
    if(!actived()) {
        callback(NetworkException(-1, "Session inactived"), Message::Ptr());
    }

    auto handler = std::make_shared<ResponseCallback>();
    handler->callbackFunc = callback;
    if(options.timeout > 0) {
        std::shared_ptr<boost::asio::deadline_timer> timeoutHandler =
            std::make_shared<boost::asio::deadline_timer>(*server->ioService(),
                    boost::posix_time::milliseconds(options.timeout));

        auto session = std::weak_ptr<Session>(shared_from_this());
        timeoutHandler->async_wait(
            boost::bind(&Session::onTimeout, shared_from_this(),
                        boost::asio::placeholders::error, message->seq()));

        handler->timeoutHandler = timeoutHandler;
        handler->m_startTime = utcTime();
    }

    m_seq2Callback->insert(std::make_pair(message->seq(), handler));

    auto buffer = std::make_shared<bytes>();
    message->encode(*buffer);

    send(buffer);
}

bool Session::actived() const {
    auto server = m_server.lock();
    if(m_actived && server && server->haveNetwork()) return true;
    return false;
}

void Session::send(std::shared_ptr<bytes> _msg)
{
    if(!actived()) {
        return;
    }

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

void Session::onWrite(boost::system::error_code ec, std::size_t length, std::shared_ptr<bytes> buffer)
{
    if(!actived()) {
        return;
    }

    try
    {
        if (ec)
        {
            LOG(WARNING) << "Error sending: " << ec.message();
            drop(TCPError);
            return;
        }

        if(!m_writeQueue.empty()) {
            write();
        }
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
    if(!actived()) {
        return;
    }

    try
    {
        std::pair<std::shared_ptr<bytes>, u256> task;
        u256 enter_time = u256(0);
        DEV_GUARDED(x_framing)
        {
            task = m_writeQueue.top();
            m_writeQueue.pop();
        }

        enter_time = task.second;
        auto session = shared_from_this();
        auto buffer = task.first;

        auto server = m_server.lock();
        if(server && server->haveNetwork()) {
            if (m_socket->isConnected())
            {
                auto socket = m_socket;
                server->strand()->post([socket, server, session, buffer] {
                    server->asioInterface()->async_write(
                        socket,
                        boost::asio::buffer(*buffer),
                        boost::bind(
                            &Session::onWrite,
                            session, boost::asio::placeholders::error,
                            boost::asio::placeholders::bytes_transferred,
                            buffer
                        )
                    );
                });
            }
            else
            {
                LOG(WARNING) << "Error sending ssl socket is close!";
                drop(TCPError);
                return;
            }
        }
        else {
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

    LOG(INFO) << "Session::drop, call and erase all callbackFunc in this session!";
    for (auto it : *m_seq2Callback)
    {
        if (it.second->timeoutHandler)
        {
            it.second->timeoutHandler->cancel();
        }
        if (it.second->callbackFunc)
        {
            LOG(INFO) << "Session::drop, call callbackFunc by seq=" << it.first;
            NetworkException e(
                P2PExceptionType::Disconnect, g_P2PExceptionMsg[P2PExceptionType::Disconnect]);
            if(server) {
                server->threadPool()->enqueue([=]() {
                    it.second->callbackFunc(e, Message::Ptr());
                });
            }
        }
    }
    m_seq2Callback->clear();

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
    if(!m_actived) {
        auto server = m_server.lock();
        if(server && server->haveNetwork()) {
            server->asioInterface()->strand_post(
                *server->strand(), boost::bind(&Session::doRead, this));  // doRead();

            m_actived = true;
        }
    }
}

void Session::doRead()
{
    auto server = m_server.lock();
    if(m_actived && server && server->haveNetwork()) {
        auto self(shared_from_this());
        auto asyncRead = [this, self](boost::system::error_code ec, std::size_t bytesTransferred) {
            if (ec)
            {
                LOG(WARNING) << "Error sending: " << ec.message();
                drop(TCPError);
                return;
            }
            LOG(TRACE) << "Read: " << bytesTransferred
                       << " bytes data:" << std::string(m_recvBuffer, m_recvBuffer + bytesTransferred);
            m_data.insert(m_data.end(), m_recvBuffer, m_recvBuffer + bytesTransferred);

            while (true)
            {
                Message::Ptr message = m_messageFactory->buildMessage();
                ssize_t result = message->decode(m_data.data(), m_data.size());
                LOG(TRACE) << "Parse result: " << result;
                if (result > 0)
                {
                    LOG(TRACE) << "Decode success: " << result;
                    NetworkException e(P2PExceptionType::Success, g_P2PExceptionMsg[P2PExceptionType::Success]);
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
                    NetworkException e(P2PExceptionType::ProtocolError,
                        g_P2PExceptionMsg[P2PExceptionType::ProtocolError]);
                    onMessage(e, self, message);
                    break;
                }
            }
        };

        if (m_socket->isConnected())
        {
            LOG(TRACE) << "Start read";
            server->asioInterface()->async_read_some(
                m_socket, *server->strand(), boost::asio::buffer(m_recvBuffer, BUFFER_LENGTH), asyncRead);
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
    if(m_actived && server && server->haveNetwork()) {
        auto it = m_seq2Callback->find(message->seq());
        if(it != m_seq2Callback->end()) {
            if(it->second->timeoutHandler) {
                it->second->timeoutHandler->cancel();
            }

            if(it->second->callbackFunc) {
                server->threadPool()->enqueue([=]() {
                    it->second->callbackFunc(e, message);
                    m_seq2Callback->erase(message->seq());
                });
            }
        }
    }
    else {
        if(m_messageHandler) {
            auto session = shared_from_this();
            auto handler = m_messageHandler;

            server->threadPool()->enqueue([session, handler, e, message]() {
                handler(e, session, message);
            });
        }
    }
}

void Session::onTimeout(const boost::system::error_code& error, uint32_t seq)
{
    auto server = m_server.lock();

    auto it = m_seq2Callback->find(seq);
    if(it != m_seq2Callback->end()) {
        if(server) {
            server->threadPool()->enqueue([=]() {
                NetworkException e(P2PExceptionType::NetworkTimeout, g_P2PExceptionMsg[P2PExceptionType::NetworkTimeout]);
                it->second->callbackFunc(e, Message::Ptr());

                m_seq2Callback->erase(it);
            });
        }
    }
}
