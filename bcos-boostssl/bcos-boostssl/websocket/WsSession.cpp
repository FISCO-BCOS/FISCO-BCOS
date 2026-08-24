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
#include <bcos-utilities/IOServicePool.h>
#include <boost/asio/post.hpp>
#include <chrono>
#include <exception>
#include <memory>
#include <string>
#include <utility>

#define MESSAGE_SEND_DELAY_REPORT_MS (5000)
#define MAX_MESSAGE_SEND_DELAY_MS (5000)

using namespace bcos;
using namespace bcos::boostssl;
using namespace bcos::boostssl::ws;
using namespace bcos::boostssl::http;

WsSession::WsSession(IOServicePool::Ptr ioServicePool) : m_ioServicePool(std::move(ioServicePool))
{
    WEBSOCKET_SESSION(INFO) << LOG_KV("[NEWOBJ][WSSESSION]", this);

    m_buffer = std::make_shared<boost::beast::flat_buffer>();
}

WsSession::~WsSession() noexcept
{
    WEBSOCKET_SESSION(INFO) << LOG_KV("[DELOBJ][WSSESSION]", this);
}

bool WsSession::isConnected()
{
    return !m_isDrop && m_wsStreamDelegate && m_wsStreamDelegate->open();
}

std::string WsSession::endPoint() const
{
    return m_endPoint;
}

void WsSession::setEndPoint(const std::string& _endPoint)
{
    m_endPoint = _endPoint;
}

void WsSession::setConnectHandler(WsConnectHandler _connectHandler)
{
    m_connectHandler = std::move(_connectHandler);
}

WsConnectHandler WsSession::connectHandler()
{
    return m_connectHandler;
}

void WsSession::setDisconnectHandler(WsDisconnectHandler _disconnectHandler)
{
    m_disconnectHandler = std::move(_disconnectHandler);
}

WsDisconnectHandler WsSession::disconnectHandler()
{
    return m_disconnectHandler;
}

void WsSession::setRecvMessageHandler(WsRecvMessageHandler _recvMessageHandler)
{
    m_recvMessageHandler = std::move(_recvMessageHandler);
}

const WsRecvMessageHandler& WsSession::recvMessageHandler()
{
    return m_recvMessageHandler;
}

std::shared_ptr<boost::asio::io_context> WsSession::ioc() const
{
    return m_ioc;
}

void WsSession::setIoc(std::shared_ptr<boost::asio::io_context> _ioc)
{
    m_ioc = std::move(_ioc);
}

void WsSession::setVersion(uint16_t _version)
{
    m_version.store(_version);
}

uint16_t WsSession::version() const
{
    return m_version.load();
}

WsStreamDelegate::Ptr WsSession::wsStreamDelegate()
{
    return m_wsStreamDelegate;
}

void WsSession::setWsStreamDelegate(WsStreamDelegate::Ptr _wsStreamDelegate)
{
    m_wsStreamDelegate = std::move(_wsStreamDelegate);
}

int32_t WsSession::sendMsgTimeout() const
{
    return m_sendMsgTimeout;
}

void WsSession::setSendMsgTimeout(int32_t _sendMsgTimeout)
{
    m_sendMsgTimeout = _sendMsgTimeout;
}

int32_t WsSession::maxWriteMsgSize() const
{
    return m_maxWriteMsgSize;
}

void WsSession::setMaxWriteMsgSize(int32_t _maxWriteMsgSize)
{
    m_maxWriteMsgSize = _maxWriteMsgSize;
}

std::size_t WsSession::writeQueueSize()
{
    bcos::Guard lockGuard(x_writeQueue);
    return m_writeQueue.size();
}

std::size_t WsSession::callbackQueueSize()
{
    bcos::Guard lockGuard(x_callback);
    return m_callbacks.size();
}

std::string WsSession::nodeId()
{
    return m_nodeId;
}

void WsSession::setNodeId(std::string _nodeId)
{
    m_nodeId = std::move(_nodeId);
}

bool WsSession::needCheckRspPacket() const
{
    return m_needCheckRspPacket;
}

void WsSession::setNeedCheckRspPacket(bool _needCheckRespPacket)
{
    m_needCheckRspPacket = _needCheckRespPacket;
}
void WsSession::drop(WsError _reason)
{
    if (m_isDrop)
    {
        WEBSOCKET_SESSION(INFO) << LOG_BADGE("drop")
                                << LOG_DESC("the session has already been dropped")
                                << LOG_KV("endpoint", m_endPoint) << LOG_KV("session", this);
        return;
    }

    m_isDrop = true;

    WEBSOCKET_SESSION(INFO) << LOG_BADGE("drop") << LOG_KV("reason", wsErrorToString(_reason))
                            << LOG_KV("endpoint", m_endPoint)
                            << LOG_KV("cb size", m_callbacks.size()) << LOG_KV("session", this);

    auto self = std::weak_ptr<WsSession>(shared_from_this());
    // call callbacks
    {
        auto error = BCOS_ERROR_PTR(WsError::SessionDisconnect,
            "Drop, the session has been disconnected, reason: " + wsErrorToString(_reason));

        Guard lockGuard(x_callback);

        for (auto& cbEntry : m_callbacks)
        {
            auto callback = cbEntry.second;
            if (callback->timer)
            {
                callback->timer->cancel();
            }

            WEBSOCKET_SESSION(TRACE)
                << LOG_DESC("the session has been disconnected") << LOG_KV("seq", cbEntry.first);

            m_ioServicePool->dispatch([callback = std::move(callback), error]() mutable {
                callback->respCallBack(error, WsMessage(), nullptr);
            });
        }
    }

    // clear callbacks
    {
        Guard lockGuard(x_callback);
        m_callbacks.clear();
    }

    if (m_wsStreamDelegate)
    {
        m_wsStreamDelegate->close();
    }

    m_ioServicePool->post([self]() {
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
        std::move(_httpRequest), [self = shared_from_this()](auto&& code) {
            self->onWsAccept(std::forward<decltype(code)>(code));
        });
}

void WsSession::onWsAccept(boost::beast::error_code _ec)
{
    if (_ec)
    {
        WEBSOCKET_SESSION(WARNING) << LOG_BADGE("onWsAccept") << LOG_KV("message", _ec.message());
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

void WsSession::onReadPacket()
{
    uint64_t size = 0;
    try
    {
        auto buffer = boost::beast::buffers_front(m_buffer->data());
        auto* data = static_cast<byte*>(const_cast<void*>(buffer.data()));
        auto size = buffer.size();

        // the message is moved through the post -> onMessage -> handler pipeline,
        // no heap allocation for the message object itself
        WsMessage message(m_rawMessage);
        if (message.decode(bytesConstRef(data, size)) < 0)
        {  // invalid packet, stop this session ?
            WEBSOCKET_SESSION(WARNING)
                << LOG_BADGE("onReadPacket") << LOG_DESC("decode packet failed")
                << LOG_KV("endpoint", endPoint()) << LOG_KV("session", this);
            drop(WsError::PacketError);
            return;
        }

        m_buffer->consume(m_buffer->size());

        auto self = weak_from_this();
        m_ioServicePool->post([self, message = std::move(message)]() mutable {
            auto session = self.lock();
            if (!session)
            {
                return;
            }
            session->onMessage(std::move(message));
        });
    }
    catch (std::exception const& e)
    {
        WEBSOCKET_SESSION(WARNING)
            << LOG_DESC("onReadPacket: decode message exception") << LOG_KV("bufSize", size)
            << LOG_KV("message", boost::diagnostic_information(e));
    }
}

void WsSession::onMessage(WsMessage _message)
{
    auto session = shared_from_this();
    auto callback = getAndRemoveRespCallback(_message.seq(), &_message);
    if (callback)
    {
        if (callback->timer)
        {
            callback->timer->cancel();
        }

        callback->respCallBack(nullptr, std::move(_message), session);
    }
    else
    {
        recvMessageHandler()(std::move(_message), session);
    }
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
        auto buffer = m_buffer;
        auto self = std::weak_ptr<WsSession>(shared_from_this());
        m_wsStreamDelegate->asyncRead(
            *m_buffer, [self, buffer](boost::beast::error_code _ec, std::size_t) {
                auto session = self.lock();
                if (!session)
                {
                    return;
                }

                if (_ec)
                {
                    BCOS_LOG(INFO) << "[WS][SESSION]" << LOG_BADGE("asyncRead")
                                   << LOG_KV("message", _ec.message())
                                   << LOG_KV("endpoint", session->endPoint())
                                   << LOG_KV("refCount", session.use_count());

                    session->drop(WsError::ReadError);
                    return;
                }

                session->onReadPacket();
                session->asyncRead();
            });
    }
    catch (const std::exception& _e)
    {
        WEBSOCKET_SESSION(WARNING)
            << LOG_BADGE("asyncRead") << LOG_DESC("exception") << LOG_KV("endpoint", endPoint())
            << LOG_KV("session", this) << LOG_KV("what", std::string(_e.what()));
        drop(WsError::ReadError);
    }
}

void WsSession::onWritePacket()
{
    if (m_writing)
    {
        return;
    }
    Guard l(x_writeQueue);
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
    asyncWrite(msg);
}

void WsSession::asyncWrite(std::shared_ptr<Message> _message)
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
        // Note: the lambda[] should not include session directly, this will cause memory leak
        m_wsStreamDelegate->asyncWrite(
            _message->buffer, [self, _message](boost::beast::error_code _ec, std::size_t) {
                auto session = self.lock();
                if (!session)
                {
                    return;
                }
                if (_ec)
                {
                    BCOS_LOG(WARNING) << LOG_BADGE("Session") << LOG_BADGE("asyncWrite")
                                      << LOG_KV("message", _ec.message())
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

void WsSession::send(std::shared_ptr<Message> message)
{
    {
        Guard lock(x_writeQueue);
        // data to be sent is always enqueue first
        m_writeQueue.push(std::move(message));
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
void WsSession::asyncSendMessage(const WsMessage& _msg, Options _options, RespCallBack _respFunc)
{
    auto seq = _msg.seq();

    if (_msg.raw() != m_rawMessage)
    {
        if (_respFunc) { _respFunc(BCOS_ERROR_PTR(WsError::MessageEncodeError, "raw mode mismatch"), WsMessage(), nullptr); }
        return;
    }

    if (!isConnected())
    {
        WEBSOCKET_SESSION(WARNING)
            << LOG_BADGE("asyncSendMessage") << LOG_DESC("the session has been disconnected")
            << LOG_KV("seq", seq) << LOG_KV("endpoint", endPoint());

        if (_respFunc)
        {
            auto error =
                BCOS_ERROR_PTR(WsError::SessionDisconnect, "the session has been disconnected");
            _respFunc(error, WsMessage(), nullptr);
        }

        return;
    }

    // check if message size overflow
    if ((int64_t)_msg.payload().size() > (int64_t)maxWriteMsgSize())
    {
        if (_respFunc)
        {
            auto error = BCOS_ERROR_PTR(WsError::MessageOverflow, "Message size overflow");
            _respFunc(error, WsMessage(), nullptr);
        }

        WEBSOCKET_SESSION(WARNING)
            << LOG_BADGE("asyncSendMessage") << LOG_DESC("send message size overflow")
            << LOG_KV("endpoint", endPoint()) << LOG_KV("seq", seq)
            << LOG_KV("msgSize", _msg.payload().size())
            << LOG_KV("maxWriteMsgSize", maxWriteMsgSize());
        return;
    }

    auto message = std::make_shared<Message>();
    auto r = _msg.encode(message->buffer);
    if (!r)
    {
        if (_respFunc)
        {
            auto error = BCOS_ERROR_PTR(WsError::MessageEncodeError, "Message encode failed");
            _respFunc(error, WsMessage(), nullptr);
        }

        WEBSOCKET_SESSION(WARNING)
            << LOG_BADGE("asyncSendMessage") << LOG_DESC("message encode failed")
            << LOG_KV("endpoint", endPoint()) << LOG_KV("seq", seq)
            << LOG_KV("packetType", _msg.packetType()) << LOG_KV("msgSize", _msg.payload().size())
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
            auto timer = std::make_shared<boost::asio::steady_timer>(
                *m_ioc, std::chrono::milliseconds(timeout));

            callback->timer = timer;
            auto self = weak_from_this();
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
            boost::beast::bind_front_handler(&WsSession::send, shared_from_this(), message));
    }
}

void WsSession::addRespCallback(const std::string& _seq, CallBack::Ptr _callback)
{
    Guard lockGuard(x_callback);
    m_callbacks[_seq] = std::move(_callback);
}

WsSession::CallBack::Ptr WsSession::getAndRemoveRespCallback(
    const std::string& _seq, const WsMessage* _message)
{
    // Session need check response packet and message isn't a respond packet, so message don't have
    // a callback. Otherwise message has a callback.
    if (needCheckRspPacket() && _message && !_message->isRespPacket())
    {
        return nullptr;
    }

    CallBack::Ptr callback = nullptr;
    {
        Guard lockGuard(x_callback);

        auto it = m_callbacks.find(_seq);
        if (it != m_callbacks.end())
        {
            callback = it->second;
            m_callbacks.erase(it);
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

    auto error = BCOS_ERROR_PTR(WsError::TimeOut, "waiting for message response timed out");

    m_ioServicePool->post([callback = std::move(callback), error = std::move(error)]() mutable {
        callback->respCallBack(error, WsMessage(), nullptr);
    });
}
