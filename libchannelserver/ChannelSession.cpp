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
#include <libdevcore/easylog.h>
#include <boost/bind.hpp>
#include <boost/lexical_cast.hpp>
#include <iostream>

using namespace dev::channel;

ChannelSession::ChannelSession()
{
    _topics = std::make_shared<std::set<std::string> >();
}

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
            CHANNEL_LOG(ERROR) << "asyncSendMessage ERROR:" << callback->_error.errorCode() << " "
                               << callback->_error.what();
            throw callback->_error;
        }

        return callback->_response;
    }
    catch (std::exception& e)
    {
        CHANNEL_LOG(ERROR) << "ERROR:" << e.what();
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
            _responseCallbacks.insert(std::make_pair(request->seq(), responseCallback));
        }

        std::shared_ptr<bytes> p_buffer = std::make_shared<bytes>();
        request->encode(*p_buffer);
        writeBuffer(p_buffer);
    }
    catch (std::exception& e)
    {
        CHANNEL_LOG(ERROR) << "ERROR:" << e.what();
    }
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
        CHANNEL_LOG(ERROR) << "ERROR:" << e.what();
    }
}

void ChannelSession::setSSLSocket(
    std::shared_ptr<boost::asio::ssl::stream<boost::asio::ip::tcp::socket> > socket)
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
            CHANNEL_LOG(ERROR) << "ChannelSession inactived";
        }
    }
    catch (std::exception& e)
    {
        CHANNEL_LOG(ERROR) << "ERROR:" << e.what();
    }
}


void ChannelSession::onRead(const boost::system::error_code& error, size_t bytesTransferred)
{
    try
    {
        if (!_actived)
        {
            CHANNEL_LOG(ERROR) << "ChannelSession inactived";

            return;
        }

        updateIdleTimer();

        if (!error)
        {
            CHANNEL_LOG(TRACE) << "Read: " << bytesTransferred;

            _recvProtocolBuffer.insert(
                _recvProtocolBuffer.end(), _recvBuffer, _recvBuffer + bytesTransferred);

            while (true)
            {
                auto message = _messageFactory->buildMessage();

                ssize_t result =
                    message->decode(_recvProtocolBuffer.data(), _recvProtocolBuffer.size());

                if (result > 0)
                {
                    // CHANNEL_LOG(TRACE) << "Decode success: " << result;

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
                    CHANNEL_LOG(ERROR) << "Protocol parser error: " << result;

                    disconnect(ChannelException(-1, "Protocol parser error, disconnect"));

                    break;
                }
            }
        }
        else
        {
            CHANNEL_LOG(ERROR) << "Read failed:" << error.value() << "," << error.message();

            if (_actived)
            {
                disconnect(ChannelException(-1, "Read failed, disconnect"));
            }
        }
    }
    catch (std::exception& e)
    {
        CHANNEL_LOG(ERROR) << "ERROR:" << e.what();
    }
}

void ChannelSession::startWrite()
{
    if (!_actived)
    {
        CHANNEL_LOG(ERROR) << "ChannelSession inactived";

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
        CHANNEL_LOG(ERROR) << "ERROR:" << e.what();
    }
}

void ChannelSession::onWrite(
    const boost::system::error_code& error, std::shared_ptr<bytes> buffer, size_t bytesTransferred)
{
    try
    {
        if (!_actived)
        {
            CHANNEL_LOG(ERROR) << "ChannelSession inactived";

            return;
        }

        std::lock_guard<std::recursive_mutex> lock(_mutex);

        updateIdleTimer();

        if (!error)
        {
            CHANNEL_LOG(TRACE) << "Write: " << bytesTransferred;
        }
        else
        {
            CHANNEL_LOG(ERROR) << "Write error: " << error.message();

            disconnect(ChannelException(-1, "Write error, disconnect"));
        }

        _writing = false;
        startWrite();
    }
    catch (std::exception& e)
    {
        CHANNEL_LOG(ERROR) << "ERROR:" << e.what();
    }
}

void ChannelSession::onMessage(ChannelException e, Message::Ptr message)
{
    try
    {
        if (!_actived)
        {
            CHANNEL_LOG(ERROR) << "ChannelSession inactived";

            return;
        }

        auto it = _responseCallbacks.find(message->seq());
        if (it != _responseCallbacks.end())
        {
            if (it->second->timeoutHandler)
            {
                it->second->timeoutHandler->cancel();
            }

            if (it->second->callback)
            {
                _threadPool->enqueue([=]() {
                    it->second->callback(e, message);
                    _responseCallbacks.erase(it);
                });
            }
            else
            {
                CHANNEL_LOG(ERROR) << "Callback empty";

                _responseCallbacks.erase(it);
            }
        }
        else
        {
            if (_messageHandler)
            {
                auto session = std::weak_ptr<dev::channel::ChannelSession>(shared_from_this());
                _threadPool->enqueue([session, message]() {
                    auto s = session.lock();
                    if (s && s->_messageHandler)
                    {
                        s->_messageHandler(s, ChannelException(0, ""), message);
                    }
                });
            }
            else
            {
                CHANNEL_LOG(ERROR) << "MessageHandler empty";
            }
        }
    }
    catch (std::exception& e)
    {
        CHANNEL_LOG(ERROR) << "ERROR:" << e.what();
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
                CHANNEL_LOG(ERROR) << "Timer error: " << error.message();
            }
            return;
        }

        if (!_actived)
        {
            CHANNEL_LOG(ERROR) << "ChannelSession inactived";

            return;
        }

        auto it = _responseCallbacks.find(seq);
        if (it != _responseCallbacks.end())
        {
            if (it->second->callback)
            {
                it->second->callback(ChannelException(-2, "Response timeout"), Message::Ptr());
            }
            else
            {
                CHANNEL_LOG(ERROR) << "Callback empty";
            }

            _responseCallbacks.erase(it);
        }
        else
        {
            CHANNEL_LOG(WARNING) << "Seq timeout: " << seq;
        }
    }
    catch (std::exception& e)
    {
        CHANNEL_LOG(ERROR) << "ERROR:" << e.what();
    }
}

void ChannelSession::onIdle(const boost::system::error_code& error)
{
    try
    {
        if (!_actived)
        {
            CHANNEL_LOG(ERROR) << "ChannelSession inactived";

            return;
        }

        if (error != boost::asio::error::operation_aborted)
        {
            CHANNEL_LOG(ERROR) << "Idle connection, disconnect " << _host << ":" << _port;

            disconnect(ChannelException(-1, "Idle connection, disconnect"));
        }
        else
        {
        }
    }
    catch (std::exception& e)
    {
        CHANNEL_LOG(ERROR) << "ERROR:" << e.what();
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

            if (!_responseCallbacks.empty())
            {
                for (auto it : _responseCallbacks)
                {
                    try
                    {
                        if (it.second->timeoutHandler.get() != NULL)
                        {
                            it.second->timeoutHandler->cancel();
                        }

                        if (it.second->callback)
                        {
                            _threadPool->enqueue([=]() { it.second->callback(e, Message::Ptr()); });
                        }
                        else
                        {
                            CHANNEL_LOG(ERROR) << "Callback empty";
                        }
                    }
                    catch (std::exception& e)
                    {
                        CHANNEL_LOG(ERROR)
                            << "Disconnect responseCallback: " << it.first << " error:" << e.what();
                    }
                }

                _responseCallbacks.clear();
            }

            if (_messageHandler)
            {
                try
                {
                    _messageHandler(shared_from_this(), e, Message::Ptr());
                }
                catch (std::exception& e)
                {
                    CHANNEL_LOG(ERROR) << "disconnect messageHandler error:" << e.what();
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
                    LOG(WARNING) << "channel shutdown timer error: " << error.message();
                    return;
                }

                if (sslSocket->next_layer().is_open())
                {
                    LOG(WARNING) << "channel shutdown timeout, force close";
                    sslSocket->next_layer().close();
                }
            });

            _sslSocket->async_shutdown(
                [sslSocket, shutdownTimer](const boost::system::error_code& error) {
                    if (error)
                    {
                        LOG(WARNING)
                            << "Error while shutdown the channel ssl socket: " << error.message();
                    }
                    shutdownTimer->cancel();

                    if (sslSocket->next_layer().is_open())
                    {
                        sslSocket->next_layer().close();
                    }
                });

            CHANNEL_LOG(DEBUG) << "Disconnected";
        }
    }
    catch (std::exception& e)
    {
        CHANNEL_LOG(WARNING) << "Close error: " << e.what();
    }
}

void ChannelSession::updateIdleTimer()
{
    if (!_actived)
    {
        CHANNEL_LOG(ERROR) << "ChannelSession inactived";

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
