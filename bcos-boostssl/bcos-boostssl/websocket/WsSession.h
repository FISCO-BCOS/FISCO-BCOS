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
 * @file WsSession.h
 * @author: octopus
 * @date 2021-07-28
 */
#pragma once
#include <bcos-boostssl/httpserver/Common.h>
#include <bcos-boostssl/utilities/Common.h>
#include <bcos-boostssl/utilities/ThreadPool.h>
#include <bcos-boostssl/websocket/Common.h>
#include <bcos-boostssl/websocket/WsMessage.h>
#include <bcos-boostssl/websocket/WsStream.h>
#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/thread/thread.hpp>
#include <atomic>
#include <unordered_map>

namespace bcos
{
namespace boostssl
{
namespace utilities
{
class ThreadPool;
}
namespace ws
{
class WsService;

// The websocket session for connection
class WsSession : public std::enable_shared_from_this<WsSession>
{
public:
    using Ptr = std::shared_ptr<WsSession>;
    using Ptrs = std::vector<std::shared_ptr<WsSession>>;

public:
    WsSession() { WEBSOCKET_SESSION(INFO) << LOG_KV("[NEWOBJ][WSSESSION]", this); }

    virtual ~WsSession() { WEBSOCKET_SESSION(INFO) << LOG_KV("[DELOBJ][WSSESSION]", this); }

    void drop(uint32_t _reason);

public:
    // start WsSession as client
    void startAsClient();
    // start WsSession as server
    void startAsServer(bcos::boostssl::http::HttpRequest _httpRequest);

public:
    void onHandshake(boost::beast::error_code _ec);

    void asyncRead();
    void onRead(boost::system::error_code ec, std::size_t bytes_transferred);

    void asyncWrite();
    void onWrite(std::shared_ptr<utilities::bytes> _buffer);

    // async read
    void onReadPacket(boost::beast::flat_buffer& _buffer);
    void onWritePacket();

    void ping();
    void pong();
    // void initPingPoing();

public:
    virtual bool isConnected() { return !m_isDrop && m_stream && m_stream->open(); }
    /**
     * @brief: async send message
     * @param _msg: message
     * @param _options: options
     * @param _respCallback: callback
     * @return void:
     */
    virtual void asyncSendMessage(std::shared_ptr<WsMessage> _msg, Options _options = Options(),
        RespCallBack _respCallback = RespCallBack());

public:
    std::string endPoint() const { return m_endPoint; }
    void setEndPoint(const std::string& _endPoint) { m_endPoint = _endPoint; }

    std::string connectedEndPoint() const { return m_connectedEndPoint; }
    void setConnectedEndPoint(const std::string& _connectedEndPoint)
    {
        m_connectedEndPoint = _connectedEndPoint;
    }

    void setConnectHandler(WsConnectHandler _connectHandler) { m_connectHandler = _connectHandler; }
    WsConnectHandler connectHandler() { return m_connectHandler; }

    void setDisconnectHandler(WsDisconnectHandler _disconnectHandler)
    {
        m_disconnectHandler = _disconnectHandler;
    }
    WsDisconnectHandler disconnectHandler() { return m_disconnectHandler; }

    void setRecvMessageHandler(WsRecvMessageHandler _recvMessageHandler)
    {
        m_recvMessageHandler = _recvMessageHandler;
    }
    WsRecvMessageHandler recvMessageHandler() { return m_recvMessageHandler; }

    std::shared_ptr<WsMessageFactory> messageFactory() { return m_messageFactory; }
    void setMessageFactory(std::shared_ptr<WsMessageFactory> _messageFactory)
    {
        m_messageFactory = _messageFactory;
    }

    std::shared_ptr<boost::asio::io_context> ioc() const { return m_ioc; }
    void setIoc(std::shared_ptr<boost::asio::io_context> _ioc) { m_ioc = _ioc; }

    std::shared_ptr<utilities::ThreadPool> threadPool() const { return m_threadPool; }
    void setThreadPool(std::shared_ptr<utilities::ThreadPool> _threadPool)
    {
        m_threadPool = _threadPool;
    }

    void setVersion(uint16_t _version) { m_version.store(_version); }
    uint16_t version() const { return m_version.load(); }

    WsStream::Ptr stream() { return m_stream; }
    void setStream(WsStream::Ptr _stream) { m_stream = _stream; }

    boost::beast::flat_buffer& buffer() { return m_buffer; }
    void setBuffer(boost::beast::flat_buffer _buffer) { m_buffer = std::move(_buffer); }

    int32_t sendMsgTimeout() const { return m_sendMsgTimeout; }
    void setSendMsgTimeout(int32_t _sendMsgTimeout) { m_sendMsgTimeout = _sendMsgTimeout; }

    std::size_t queueSize()
    {
        boost::shared_lock<boost::shared_mutex> lock(x_queue);
        return m_queue.size();
    }

public:
    struct CallBack
    {
        using Ptr = std::shared_ptr<CallBack>;
        RespCallBack respCallBack;
        std::shared_ptr<boost::asio::deadline_timer> timer;
    };
    void addRespCallback(const std::string& _seq, CallBack::Ptr _callback);
    CallBack::Ptr getAndRemoveRespCallback(const std::string& _seq, bool _remove = true);
    void onRespTimeout(const boost::system::error_code& _error, const std::string& _seq);

private:
    //
    std::atomic_bool m_isDrop = false;
    // websocket protocol version
    std::atomic<uint16_t> m_version = 0;

    // buffer used to read message
    boost::beast::flat_buffer m_buffer;

    std::string m_endPoint;
    std::string m_connectedEndPoint;

    //
    int32_t m_sendMsgTimeout = -1;
    //
    WsStream::Ptr m_stream;
    // callbacks
    boost::shared_mutex x_callback;
    std::unordered_map<std::string, CallBack::Ptr> m_callbacks;

    // callback handler
    WsConnectHandler m_connectHandler;
    WsDisconnectHandler m_disconnectHandler;
    WsRecvMessageHandler m_recvMessageHandler;

    // message factory
    std::shared_ptr<WsMessageFactory> m_messageFactory;
    // thread pool
    std::shared_ptr<utilities::ThreadPool> m_threadPool;
    // ioc
    std::shared_ptr<boost::asio::io_context> m_ioc;

    // send message queue
    mutable boost::shared_mutex x_queue;
    std::vector<std::shared_ptr<utilities::bytes>> m_queue;
};

}  // namespace ws
}  // namespace boostssl
}  // namespace bcos