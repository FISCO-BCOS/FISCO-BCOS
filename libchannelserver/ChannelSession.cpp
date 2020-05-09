/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 */
/**
 * @file: ChannelSession.cpp
 * @author: monan
 *
 * @date: 2018
 */

#include "ChannelSession.h"
#include "ChannelException.h"
#include <libdevcore/Common.h>
#include <boost/bind.hpp>
#include <boost/lexical_cast.hpp>
#include <iostream>

using namespace dev::channel;

ChannelSession::ChannelSession() : m_topics(std::make_shared<std::set<std::string>>()) {}

Message::Ptr ChannelSession::sendMessage(Message::Ptr request, size_t timeout)
{
    try
    {
        class SessionCallback : public std::enable_shared_from_this<SessionCallback>
        {
        public:
            typedef std::shared_ptr<SessionCallback> Ptr;

            SessionCallback() { _mutex.lock(); }

            void onResponse(dev::channel::ChannelException error, Message::Ptr message)
            {
                _error = error;
                _response = message;

                _mutex.unlock();
            }

            dev::channel::ChannelException _error;
            Message::Ptr _response;
            std::mutex _mutex;
        };

        SessionCallback::Ptr callback = std::make_shared<SessionCallback>();

        std::function<void(dev::channel::ChannelException, Message::Ptr)> fp = std::bind(
            &SessionCallback::onResponse, callback, std::placeholders::_1, std::placeholders::_2);
        asyncSendMessage(request, fp, timeout);

        callback->_mutex.lock();
        callback->_mutex.unlock();

        if (callback->_error.errorCode() != 0)
        {
            CHANNEL_SESSION_LOG(ERROR) << LOG_DESC("asyncSendMessage callback error")
                                       << LOG_KV("errorCode", callback->_error.errorCode())
                                       << LOG_KV("what", callback->_error.what());
            BOOST_THROW_EXCEPTION(callback->_error);
        }

        return callback->_response;
    }
    catch (std::exception& e)
    {
        CHANNEL_SESSION_LOG(ERROR)
            << LOG_DESC("asyncSendMessage error") << LOG_KV("what", e.what());
    }

    return Message::Ptr();
}

void ChannelSession::asyncSendMessage(Message::Ptr request,
    std::function<void(dev::channel::ChannelException, Message::Ptr)> callback, uint32_t timeout)
{
    try
    {
        if (!_actived)
        {
            if (callback)
            {
                callback(ChannelException(-3, "Session inactived"), Message::Ptr());
            }

            return;
        }

        ResponseCallback::Ptr responseCallback = std::make_shared<ResponseCallback>();

        if (callback)
        {
            responseCallback->seq = request->seq();
            responseCallback->callback = callback;

            if (timeout > 0)
            {
                std::shared_ptr<boost::asio::deadline_timer> timeoutHandler =
                    std::make_shared<boost::asio::deadline_timer>(
                        *_ioService, boost::posix_time::milliseconds(timeout));

                auto session = std::weak_ptr<ChannelSession>(shared_from_this());
                timeoutHandler->async_wait(boost::bind(&ChannelSession::onTimeout,
                    shared_from_this(), boost::asio::placeholders::error, request->seq()));

                responseCallback->timeoutHandler = timeoutHandler;
            }
            insertResponseCallback(request->seq(), responseCallback);
        }

        std::shared_ptr<bytes> p_buffer = std::make_shared<bytes>();
        request->encode(*p_buffer);
        writeBuffer(p_buffer);
        // update the group outgoing traffic
        if (m_networkStat && request->groupID() != -1)
        {
            m_networkStat->updateGroupResponseTraffic(
                request->groupID(), request->type(), request->length());
        }
        // stat AMOP outgoing-network-traffic
        if (m_networkStat && isAMOPMessage(request))
        {
            m_networkStat->updateAMOPOutTraffic(request->length());
        }
    }
    catch (std::exception& e)
    {
        CHANNEL_SESSION_LOG(ERROR) << LOG_DESC("asyncSendMessage error")
                                   << LOG_KV("what", boost::diagnostic_information(e));
    }
}

bool ChannelSession::isAMOPMessage(Message::Ptr _request)
{
    auto reqType = _request->type();
    if (reqType == AMOP_REQUEST || reqType == AMOP_RESPONSE ||
        reqType == AMOP_CLIENT_SUBSCRIBE_TOPICS || reqType == AMOP_MULBROADCAST)
    {
        return true;
    }
    return false;
}

void ChannelSession::run()
{
    try
    {
        if (!_actived)
        {
            _actived = true;

            updateIdleTimer();

            startRead();
        }
    }
    catch (std::exception& e)
    {
        CHANNEL_SESSION_LOG(ERROR) << "error" << LOG_KV("what", boost::diagnostic_information(e));
    }
}

void ChannelSession::setSSLSocket(
    std::shared_ptr<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>> socket)
{
    _sslSocket = socket;

    _idleTimer = std::make_shared<boost::asio::deadline_timer>(_sslSocket->get_io_service());
}

void ChannelSession::startRead()
{
    try
    {
        if (_actived)
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);

            auto session = shared_from_this();
            if (_enableSSL)
            {
                _sslSocket->async_read_some(boost::asio::buffer(_recvBuffer, bufferLength),
                    [session](const boost::system::error_code& error, size_t bytesTransferred) {
                        auto s = session;
                        if (s)
                        {
                            s->onRead(error, bytesTransferred);
                        }
                    });
            }
            else
            {
                _sslSocket->next_layer().async_read_some(
                    boost::asio::buffer(_recvBuffer, bufferLength),
                    [session](const boost::system::error_code& error, size_t bytesTransferred) {
                        auto s = session;
                        if (s)
                        {
                            s->onRead(error, bytesTransferred);
                        }
                    });
            }
        }
        else
        {
            CHANNEL_SESSION_LOG(ERROR) << "ChannelSession inactived";
        }
    }
    catch (std::exception& e)
    {
        CHANNEL_SESSION_LOG(ERROR)
            << "startRead error" << LOG_KV("what", boost::diagnostic_information(e));
    }
}


void ChannelSession::onRead(const boost::system::error_code& error, size_t bytesTransferred)
{
    try
    {
        if (!_actived)
        {
            CHANNEL_SESSION_LOG(ERROR) << "ChannelSession inactived";

            return;
        }

        updateIdleTimer();

        if (!error)
        {
            _recvProtocolBuffer.insert(
                _recvProtocolBuffer.end(), _recvBuffer, _recvBuffer + bytesTransferred);

            while (true)
            {
                auto message = _messageFactory->buildMessage();

                ssize_t result =
                    message->decode(_recvProtocolBuffer.data(), _recvProtocolBuffer.size());

                if (result > 0)
                {
                    onMessage(ChannelException(0, ""), message);

                    _recvProtocolBuffer.erase(
                        _recvProtocolBuffer.begin(), _recvProtocolBuffer.begin() + result);
                }
                else if (result == 0)
                {
                    startRead();

                    break;
                }
                else if (result < 0)
                {
                    CHANNEL_SESSION_LOG(ERROR) << "onRead Protocol parser error";

                    disconnect(ChannelException(-1, "Protocol parser error, disconnect"));

                    break;
                }
            }
        }
        else
        {
            CHANNEL_SESSION_LOG(WARNING)
                << LOG_DESC("onRead failed") << LOG_KV("value", error.value())
                << LOG_KV("message", error.message());

            if (_actived)
            {
                disconnect(ChannelException(-1, "Read failed, disconnect"));
            }
        }
    }
    catch (std::exception& e)
    {
        CHANNEL_SESSION_LOG(ERROR)
            << "onRead error" << LOG_KV("what", boost::diagnostic_information(e));
    }
}

void ChannelSession::startWrite()
{
    if (!_actived)
    {
        CHANNEL_SESSION_LOG(ERROR) << "startWrite ChannelSession inactived";

        return;
    }

    if (_writing)
    {
        return;
    }

    if (!_sendBufferList.empty())
    {
        _writing = true;

        auto buffer = _sendBufferList.front();
        _sendBufferList.pop();

        auto session = std::weak_ptr<ChannelSession>(shared_from_this());

        _sslSocket->get_io_service().post([session, buffer] {
            auto s = session.lock();
            if (s)
            {
                if (s->enableSSL())
                {
                    boost::asio::async_write(*s->sslSocket(),
                        boost::asio::buffer(buffer->data(), buffer->size()),
                        [=](const boost::system::error_code& error, size_t bytesTransferred) {
                            auto s = session.lock();
                            if (s)
                            {
                                s->onWrite(error, buffer, bytesTransferred);
                            }
                        });
                }
                else
                {
                    boost::asio::async_write(s->sslSocket()->next_layer(),
                        boost::asio::buffer(buffer->data(), buffer->size()),
                        [=](const boost::system::error_code& error, size_t bytesTransferred) {
                            auto s = session.lock();
                            if (s)
                            {
                                s->onWrite(error, buffer, bytesTransferred);
                            }
                        });
                }
            }
        });
    }
    else
    {
        _writing = false;
    }
}

void ChannelSession::writeBuffer(std::shared_ptr<bytes> buffer)
{
    try
    {
        std::lock_guard<std::recursive_mutex> lock(_mutex);

        _sendBufferList.push(buffer);

        startWrite();
    }
    catch (std::exception& e)
    {
        CHANNEL_SESSION_LOG(ERROR)
            << "writeBuffer error" << LOG_KV("what", boost::diagnostic_information(e));
    }
}

void ChannelSession::onWrite(const boost::system::error_code& error, std::shared_ptr<bytes>, size_t)
{
    try
    {
        if (!_actived)
        {
            CHANNEL_SESSION_LOG(ERROR) << "ChannelSession inactived";

            return;
        }

        std::lock_guard<std::recursive_mutex> lock(_mutex);

        updateIdleTimer();

        if (error)
        {
            CHANNEL_SESSION_LOG(ERROR)
                << LOG_DESC("Write error") << LOG_KV("message", error.message());

            disconnect(ChannelException(-1, "Write error, disconnect"));
        }

        _writing = false;
        startWrite();
    }
    catch (std::exception& e)
    {
        CHANNEL_SESSION_LOG(ERROR)
            << LOG_DESC("onWrite error") << LOG_KV("what", boost::diagnostic_information(e));
    }
}

void ChannelSession::onMessage(ChannelException e, Message::Ptr message)
{
    try
    {
        if (!_actived)
        {
            CHANNEL_SESSION_LOG(ERROR) << LOG_DESC("onMessage ChannelSession inactived");

            return;
        }

        auto response_callback = findResponseCallbackBySeq(message->seq());
        if (response_callback != nullptr)
        {
            if (response_callback->timeoutHandler)
            {
                response_callback->timeoutHandler->cancel();
            }

            if (response_callback->callback)
            {
                m_responseThreadPool->enqueue([=]() {
                    response_callback->callback(e, message);
                    eraseResponseCallbackBySeq(message->seq());
                });
            }
            else
            {
                CHANNEL_SESSION_LOG(ERROR) << LOG_DESC("onMessage Callback empty");
                eraseResponseCallbackBySeq(message->seq());
            }
        }
        else
        {
            if (_messageHandler)
            {
                auto session = std::weak_ptr<dev::channel::ChannelSession>(shared_from_this());
                m_requestThreadPool->enqueue([session, message]() {
                    auto s = session.lock();
                    if (s && s->_messageHandler)
                    {
                        s->_messageHandler(s, ChannelException(0, ""), message);
                    }
                });
            }
            else
            {
                CHANNEL_SESSION_LOG(ERROR) << "MessageHandler empty";
            }
        }
        // stat AMOP incoming-network-traffic
        if (m_networkStat && isAMOPMessage(message))
        {
            m_networkStat->updateAMOPInTraffic(message->length());
        }
    }
    catch (std::exception& e)
    {
        CHANNEL_SESSION_LOG(ERROR)
            << "onMessage error" << LOG_KV("what", boost::diagnostic_information(e));
    }
}

void ChannelSession::onTimeout(const boost::system::error_code& error, std::string seq)
{
    try
    {
        if (error)
        {
            if (error != boost::asio::error::operation_aborted)
            {
                CHANNEL_SESSION_LOG(ERROR) << "Timer error" << LOG_KV("message", error.message());
            }
            return;
        }

        if (!_actived)
        {
            CHANNEL_SESSION_LOG(ERROR) << "ChannelSession inactived";

            return;
        }

        auto response_callback = findResponseCallbackBySeq(seq);
        if (response_callback)
        {
            if (response_callback->callback)
            {
                response_callback->callback(
                    ChannelException(-2, "Response timeout"), Message::Ptr());
            }
            else
            {
                CHANNEL_SESSION_LOG(ERROR) << "Callback empty";
            }
            eraseResponseCallbackBySeq(seq);
        }
        else
        {
            CHANNEL_SESSION_LOG(WARNING) << "onTimeout Seq timeout" << LOG_KV("Seq", seq);
        }
    }
    catch (std::exception& e)
    {
        CHANNEL_SESSION_LOG(ERROR)
            << "onTimeout error" << LOG_KV("what", boost::diagnostic_information(e));
    }
}

void ChannelSession::onIdle(const boost::system::error_code& error)
{
    try
    {
        if (!_actived)
        {
            CHANNEL_SESSION_LOG(ERROR) << "ChannelSession inactived";

            return;
        }

        if (error != boost::asio::error::operation_aborted)
        {
            CHANNEL_SESSION_LOG(ERROR) << "Idle connection, disconnect " << _host << ":" << _port;

            disconnect(ChannelException(-1, "Idle connection, disconnect"));
        }
        else
        {
        }
    }
    catch (std::exception& e)
    {
        CHANNEL_SESSION_LOG(ERROR)
            << "onIdle error" << LOG_KV("what", boost::diagnostic_information(e));
    }
}

void ChannelSession::disconnect(dev::channel::ChannelException e)
{
    try
    {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (_actived)
        {
            _idleTimer->cancel();
            _actived = false;

            {
                ReadGuard l(x_responseCallbacks);
                for (auto& it : m_responseCallbacks)
                {
                    try
                    {
                        if (it.second->timeoutHandler.get() != NULL)
                        {
                            it.second->timeoutHandler->cancel();
                        }

                        if (it.second->callback)
                        {
                            m_responseThreadPool->enqueue(
                                [=]() { it.second->callback(e, Message::Ptr()); });
                        }
                        else
                        {
                            CHANNEL_SESSION_LOG(ERROR) << "Callback empty";
                        }
                    }
                    catch (std::exception& e)
                    {
                        CHANNEL_SESSION_LOG(ERROR)
                            << "Disconnect responseCallback" << LOG_KV("responseCallback", it.first)
                            << LOG_KV("what", boost::diagnostic_information(e));
                    }
                }
            }
            clearResponseCallbacks();
            if (_messageHandler)
            {
                try
                {
                    _messageHandler(shared_from_this(), e, Message::Ptr());
                }
                catch (std::exception& e)
                {
                    CHANNEL_SESSION_LOG(ERROR) << "disconnect messageHandler error"
                                               << LOG_KV("what", boost::diagnostic_information(e));
                }

                _messageHandler = std::function<void(
                    ChannelSession::Ptr, dev::channel::ChannelException, Message::Ptr)>();
            }

            auto sslSocket = _sslSocket;
            // force close socket after 30 seconds
            auto shutdownTimer = std::make_shared<boost::asio::deadline_timer>(
                *_ioService, boost::posix_time::milliseconds(30000));
            shutdownTimer->async_wait([sslSocket](const boost::system::error_code& error) {
                if (error && error != boost::asio::error::operation_aborted)
                {
                    LOG(WARNING) << "channel shutdown timer error"
                                 << LOG_KV("message", error.message());
                    return;
                }

                if (sslSocket->next_layer().is_open())
                {
                    LOG(WARNING) << "channel shutdown timeout force close";
                    sslSocket->next_layer().close();
                }
            });

            _sslSocket->async_shutdown(
                [sslSocket, shutdownTimer](const boost::system::error_code& error) {
                    if (error)
                    {
                        LOG(WARNING) << "async_shutdown" << LOG_KV("message", error.message());
                    }
                    shutdownTimer->cancel();

                    if (sslSocket->next_layer().is_open())
                    {
                        sslSocket->next_layer().close();
                    }
                });

            CHANNEL_SESSION_LOG(DEBUG) << "Disconnected";
        }
    }
    catch (std::exception& e)
    {
        CHANNEL_SESSION_LOG(WARNING)
            << "Close error" << LOG_KV("what", boost::diagnostic_information(e));
    }
}

void ChannelSession::updateIdleTimer()
{
    if (!_actived)
    {
        CHANNEL_SESSION_LOG(ERROR) << "ChannelSession inactived";

        return;
    }

    if (_idleTimer)
    {
        _idleTimer->expires_from_now(boost::posix_time::seconds(_idleTime));

        auto session = std::weak_ptr<ChannelSession>(shared_from_this());
        _idleTimer->async_wait([session](const boost::system::error_code& error) {
            auto s = session.lock();
            if (s)
            {
                s->onIdle(error);
            }
        });
    }
}
