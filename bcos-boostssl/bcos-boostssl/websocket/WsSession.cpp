/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  m_limitations under the License.
 *
 * @file WsSession.cpp
 * @author: octopus
 * @date 2021-07-08
 */

#include <bcos-boostssl/websocket/WsError.h>
#include <bcos-boostssl/websocket/WsSession.h>
#include <bcos-utilities/BoostLog.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/ThreadPool.h>
#include <boost/beast/websocket/rfc6455.hpp>
#include <boost/beast/websocket/stream.hpp>
#include <boost/core/ignore_unused.hpp>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#define MESSAGE_SEND_DELAY_REPORT_MS (5000)
#define MAX_MESSAGE_SEND_DELAY_MS (5000)

using namespace bcos;
using namespace bcos::boostssl;
using namespace bcos::boostssl::ws;
using namespace bcos::boostssl::http;

WsSession::WsSession(std::string _moduleName) : m_moduleName(_moduleName)
{
    WEBSOCKET_SESSION(INFO) << LOG_KV("[NEWOBJ][WSSESSION]", this);
}
void WsSession::drop(uint32_t _reason)
{
    if (m_isDrop)
    {
        WEBSOCKET_SESSION(INFO) << LOG_BADGE("drop")
                                << LOG_DESC("the session has already been dropped")
                                << LOG_KV("endpoint", m_endPoint) << LOG_KV("session", this);
        return;
    }

    m_isDrop = true;

    WEBSOCKET_SESSION(INFO) << LOG_BADGE("drop") << LOG_KV("reason", _reason)
                            << LOG_KV("endpoint", m_endPoint) << LOG_KV("session", this);

    auto self = std::weak_ptr<WsSession>(shared_from_this());
    // call callbacks
    {
        auto error = std::make_shared<Error>(
            WsError::SessionDisconnect, "the session has been disconnected");

        ReadGuard l(x_callback);
        WEBSOCKET_SESSION(INFO) << LOG_BADGE("drop") << LOG_KV("reason", _reason)
                                << LOG_KV("endpoint", m_endPoint)
                                << LOG_KV("cb size", m_callbacks.size()) << LOG_KV("session", this);

        for (auto& cbEntry : m_callbacks)
        {
            auto callback = cbEntry.second;
            if (callback->timer)
            {
                callback->timer->cancel();
            }

            WEBSOCKET_SESSION(TRACE)
                << LOG_DESC("the session has been disconnected") << LOG_KV("seq", cbEntry.first);

            m_threadPool->enqueue(
                [callback, error]() { callback->respCallBack(error, nullptr, nullptr); });
        }
    }

    // clear callbacks
    {
        WriteGuard lock(x_callback);
        m_callbacks.clear();
    }

    if (m_wsStreamDelegate)
    {
        m_wsStreamDelegate->close();
    }

    m_threadPool->enqueue([self]() {
        auto session = self.lock();
        if (session)
        {
            session->disconnectHandler()(nullptr, session);
        }
    });
}

// start WsSession as client
void WsSession::startAsClient()
{
    if (m_connectHandler)
    {
        auto session = shared_from_this();
        m_connectHandler(nullptr, session);
    }
    // read message
    asyncRead();

    WEBSOCKET_SESSION(INFO) << LOG_BADGE("startAsClient")
                            << LOG_DESC("websocket handshake successfully")
                            << LOG_KV("endPoint", m_endPoint) << LOG_KV("session", this);
}

// start WsSession as server
void WsSession::startAsServer(HttpRequest _httpRequest)
{
    WEBSOCKET_SESSION(INFO) << LOG_BADGE("startAsServer") << LOG_DESC("start websocket handshake")
                            << LOG_KV("endPoint", m_endPoint) << LOG_KV("session", this);
    m_wsStreamDelegate->asyncAccept(
        _httpRequest, std::bind(&WsSession::onWsAccept, shared_from_this(), std::placeholders::_1));
}

void WsSession::onWsAccept(boost::beast::error_code _ec)
{
    if (_ec)
    {
        WEBSOCKET_SESSION(WARNING) << LOG_BADGE("onWsAccept") << LOG_KV("error", _ec.message());
        return drop(WsError::AcceptError);
    }

    if (connectHandler())
    {
        connectHandler()(nullptr, shared_from_this());
    }

    asyncRead();

    WEBSOCKET_SESSION(INFO) << LOG_BADGE("onWsAccept")
                            << LOG_DESC("websocket handshake successfully")
                            << LOG_KV("endPoint", endPoint()) << LOG_KV("session", this);
}

void WsSession::onReadPacket(boost::beast::flat_buffer& _buffer)
{
    try
    {
        auto data = boost::asio::buffer_cast<byte*>(boost::beast::buffers_front(_buffer.data()));
        auto size = boost::asio::buffer_size(m_buffer.data());

        auto message = m_messageFactory->buildMessage();
        if (message->decode(bytesConstRef(data, size)) < 0)
        {  // invalid packet, stop this session ?
            WEBSOCKET_SESSION(WARNING)
                << LOG_BADGE("onReadPacket") << LOG_DESC("decode packet error")
                << LOG_KV("endpoint", endPoint()) << LOG_KV("session", this);
            return drop(WsError::PacketError);
        }

        m_buffer.consume(_buffer.size());
        onMessage(message);
    }
    catch (std::exception const& e)
    {
        WEBSOCKET_SESSION(WARNING) << LOG_DESC("onReadPacket: decode message exception")
                                   << LOG_KV("error", boost::diagnostic_information(e));
    }
}

void WsSession::onMessage(bcos::boostssl::MessageFace::Ptr _message)
{
    auto self = std::weak_ptr<WsSession>(shared_from_this());
    // task enqueue
    m_threadPool->enqueue([_message, self]() {
        auto session = self.lock();
        if (!session)
        {
            return;
        }
        auto callback = session->getAndRemoveRespCallback(_message->seq(), true, _message);
        if (callback)
        {
            if (callback->timer)
            {
                callback->timer->cancel();
            }

            callback->respCallBack(nullptr, _message, session);
        }
        else
        {
            session->recvMessageHandler()(_message, session);
        }
    });
}

void WsSession::asyncRead()
{
    if (!isConnected())
    {
        WEBSOCKET_SESSION(TRACE) << LOG_BADGE("asyncRead")
                                 << LOG_DESC("session has been disconnected")
                                 << LOG_KV("endpoint", endPoint()) << LOG_KV("session", this);
        return;
    }
    try
    {
        m_wsStreamDelegate->asyncRead(m_buffer, std::bind(&WsSession::onRead, shared_from_this(),
                                                    std::placeholders::_1, std::placeholders::_2));
    }
    catch (const std::exception& _e)
    {
        WEBSOCKET_SESSION(WARNING)
            << LOG_BADGE("asyncRead") << LOG_DESC("exception") << LOG_KV("endpoint", endPoint())
            << LOG_KV("session", this) << LOG_KV("what", std::string(_e.what()));
        drop(WsError::ReadError);
    }
}

void WsSession::onRead(boost::system::error_code _ec, std::size_t)
{
    if (_ec)
    {
        WEBSOCKET_SESSION(WARNING) << LOG_BADGE("asyncRead") << LOG_KV("error", _ec.message())
                                   << LOG_KV("endpoint", endPoint()) << LOG_KV("session", this);

        return drop(WsError::ReadError);
    }

    onReadPacket(buffer());
    asyncRead();
}

void WsSession::onWritePacket()
{
    if (m_writing)
    {
        return;
    }
    WriteGuard l(x_writeQueue);
    if (m_writing)
    {
        return;
    }
    if (m_writeQueue.empty())
    {
        m_writing = false;
        return;
    }
    m_writing = true;
    auto msg = m_writeQueue.top();
    m_writeQueue.pop();
    asyncWrite(msg->buffer);
}

void WsSession::asyncWrite(std::shared_ptr<bcos::bytes> _buffer)
{
    if (!isConnected())
    {
        WEBSOCKET_SESSION(TRACE) << LOG_BADGE("asyncWrite")
                                 << LOG_DESC("session has been disconnected")
                                 << LOG_KV("endpoint", endPoint()) << LOG_KV("session", this);
        return;
    }

    try
    {
        auto self = std::weak_ptr<WsSession>(shared_from_this());
        // Note: add one simple way to monitor message sending latency
        // Note: the lamda[] should not include session directly, this will cause memory leak
        m_wsStreamDelegate->asyncWrite(
            *_buffer, [self, _buffer](boost::beast::error_code _ec, std::size_t) {
                auto session = self.lock();
                if (!session)
                {
                    return;
                }
                if (_ec)
                {
                    BCOS_LOG(WARNING) << LOG_BADGE(session->moduleName()) << LOG_BADGE("Session")
                                      << LOG_BADGE("asyncWrite") << LOG_KV("message", _ec.message())
                                      << LOG_KV("endpoint", session->endPoint());
                    return session->drop(WsError::WriteError);
                }
                if (session->m_writing)
                {
                    session->m_writing = false;
                }
                session->onWritePacket();
            });
    }
    catch (const std::exception& _e)
    {
        WEBSOCKET_SESSION(WARNING)
            << LOG_BADGE("asyncWrite") << LOG_DESC("async_write throw exception")
            << LOG_KV("session", this) << LOG_KV("endpoint", endPoint())
            << LOG_KV("what", std::string(_e.what()));
        drop(WsError::WriteError);
    }
}

void WsSession::send(std::shared_ptr<bytes> _buffer)
{
    auto msg = std::make_shared<Message>();
    msg->buffer = _buffer;

    {
        WriteGuard l(x_writeQueue);
        // data to be sent is always enqueue first
        m_writeQueue.push(msg);
    }
    onWritePacket();
}

/**
 * @brief: send message with callback
 * @param _msg: message to be send
 * @param _options: options
 * @param _respCallback: callback
 * @return void:
 */
void WsSession::asyncSendMessage(
    std::shared_ptr<MessageFace> _msg, Options _options, RespCallBack _respFunc)
{
    auto seq = _msg->seq();

    if (!isConnected())
    {
        WEBSOCKET_SESSION(WARNING)
            << LOG_BADGE("asyncSendMessage") << LOG_DESC("the session has been disconnected")
            << LOG_KV("seq", seq) << LOG_KV("endpoint", endPoint());

        if (_respFunc)
        {
            auto error = std::make_shared<Error>(
                WsError::SessionDisconnect, "the session has been disconnected");
            _respFunc(error, nullptr, nullptr);
        }

        return;
    }

    // check if message size overflow
    if ((int64_t)_msg->payload()->size() > (int64_t)maxWriteMsgSize())
    {
        if (_respFunc)
        {
            auto error = std::make_shared<Error>(WsError::MessageOverflow, "Message size overflow");
            _respFunc(error, nullptr, nullptr);
        }

        WEBSOCKET_SESSION(WARNING)
            << LOG_BADGE("asyncSendMessage") << LOG_DESC("send message size overflow")
            << LOG_KV("endpoint", endPoint()) << LOG_KV("seq", seq)
            << LOG_KV("msgSize", _msg->payload()->size())
            << LOG_KV("maxWriteMsgSize", maxWriteMsgSize());
        return;
    }

    auto buffer = std::make_shared<bytes>();
    auto r = _msg->encode(*buffer);
    if (!r)
    {
        if (_respFunc)
        {
            auto error =
                std::make_shared<Error>(WsError::MessageEncodeError, "Message encode failed");
            _respFunc(error, nullptr, nullptr);
        }

        WEBSOCKET_SESSION(WARNING)
            << LOG_BADGE("asyncSendMessage") << LOG_DESC("message encode failed")
            << LOG_KV("endpoint", endPoint()) << LOG_KV("seq", seq)
            << LOG_KV("msgSize", _msg->payload()->size())
            << LOG_KV("maxWriteMsgSize", maxWriteMsgSize());
        return;
    }

    if (_respFunc)
    {  // callback
        auto callback = std::make_shared<CallBack>();
        callback->respCallBack = _respFunc;
        auto timeout = _options.timeout > 0 ? _options.timeout : m_sendMsgTimeout;
        if (timeout > 0)
        {
            // create new timer to handle timeout
            auto timer = std::make_shared<boost::asio::deadline_timer>(
                *m_ioc, boost::posix_time::milliseconds(timeout));

            callback->timer = timer;
            auto self = std::weak_ptr<WsSession>(shared_from_this());
            timer->async_wait([self, seq](const boost::system::error_code& e) {
                auto session = self.lock();
                if (session)
                {
                    session->onRespTimeout(e, seq);
                }
            });
        }

        addRespCallback(seq, callback);
    }

    {
        boost::asio::post(m_wsStreamDelegate->tcpStream().get_executor(),
            boost::beast::bind_front_handler(&WsSession::send, shared_from_this(), buffer));
    }
}

void WsSession::addRespCallback(const std::string& _seq, CallBack::Ptr _callback)
{
    WriteGuard lock(x_callback);
    m_callbacks[_seq] = _callback;
}

WsSession::CallBack::Ptr WsSession::getAndRemoveRespCallback(
    const std::string& _seq, bool _remove, std::shared_ptr<MessageFace> _message)
{
    // Sesseion need check response packet and message isn't a respond packet, so message don't have
    // a callback. Otherwise message has a callback.
    if (needCheckRspPacket() && _message && !_message->isRespPacket())
    {
        return nullptr;
    }

    CallBack::Ptr callback = nullptr;
    {
        UpgradableGuard l(x_callback);
        auto it = m_callbacks.find(_seq);
        if (it != m_callbacks.end())
        {
            callback = it->second;
            if (_remove)
            {
                UpgradeGuard ul(l);
                m_callbacks.erase(it);
            }
        }
    }

    return callback;
}

void WsSession::onRespTimeout(const boost::system::error_code& _error, const std::string& _seq)
{
    if (_error)
    {
        return;
    }

    auto callback = getAndRemoveRespCallback(_seq);
    if (!callback)
    {
        return;
    }

    WEBSOCKET_SESSION(WARNING) << LOG_BADGE("onRespTimeout") << LOG_KV("seq", _seq);

    auto error =
        std::make_shared<Error>(WsError::TimeOut, "waiting for message response timed out");
    m_threadPool->enqueue([callback, error]() { callback->respCallBack(error, nullptr, nullptr); });
}
