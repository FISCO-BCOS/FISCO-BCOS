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
#include "bcos-boostssl/websocket/WsError.h"
#include <bcos-boostssl/httpserver/Common.h>
#include <bcos-boostssl/websocket/Common.h>
#include <bcos-boostssl/websocket/WsMessage.h>
#include <bcos-boostssl/websocket/WsStream.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/IOServicePool.h>
#include <bcos-utilities/Timer.h>
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/thread/thread.hpp>
#include <atomic>
#include <memory>
#include <mutex>
#include <queue>
#include <shared_mutex>
#include <unordered_map>
#include <utility>

namespace bcos::boostssl::ws
{
class WsService;
// The websocket session for connection
class WsSession : public std::enable_shared_from_this<WsSession>
{
public:
    using Ptr = std::shared_ptr<WsSession>;
    using Ptrs = std::vector<std::shared_ptr<WsSession>>;

public:
    explicit WsSession(IOServicePool::Ptr ioServicePool);

    ~WsSession() noexcept;

    void drop(boostssl::ws::WsError _reason);

public:
    // start WsSession as client
    void startAsClient();
    // start WsSession as server
    void startAsServer(bcos::boostssl::http::HttpRequest _httpRequest);

    void onMessage(WsMessage _message);

    bool isConnected();
    /**
     * @brief: async send message
     * @param _msg: message
     * @param _options: options
     * @param _respCallback: callback
     * @return void:
     */
    void asyncSendMessage(const WsMessage& _msg, Options _options = Options(),
        RespCallBack _respCallback = RespCallBack());


    std::string endPoint() const;
    void setEndPoint(const std::string& _endPoint);

    void setConnectHandler(WsConnectHandler _connectHandler);
    WsConnectHandler connectHandler();

    void setDisconnectHandler(WsDisconnectHandler _disconnectHandler);
    WsDisconnectHandler disconnectHandler();

    void setRecvMessageHandler(WsRecvMessageHandler _recvMessageHandler);
    const WsRecvMessageHandler& recvMessageHandler();

    // whether messages on this session use the raw wire format (fixed per service)
    bool rawMessage() const { return m_rawMessage; }
    void setRawMessage(bool _rawMessage) { m_rawMessage = _rawMessage; }

    std::shared_ptr<boost::asio::io_context> ioc() const;
    void setIoc(std::shared_ptr<boost::asio::io_context> _ioc);

    void setVersion(uint16_t _version);
    uint16_t version() const;

    WsStreamDelegate::Ptr wsStreamDelegate();
    void setWsStreamDelegate(WsStreamDelegate::Ptr _wsStreamDelegate);

    int32_t sendMsgTimeout() const;
    void setSendMsgTimeout(int32_t _sendMsgTimeout);

    int32_t maxWriteMsgSize() const;
    void setMaxWriteMsgSize(int32_t _maxWriteMsgSize);

    std::size_t writeQueueSize();

    std::size_t callbackQueueSize();

    std::string nodeId();
    void setNodeId(std::string _nodeId);

    bool needCheckRspPacket() const;
    void setNeedCheckRspPacket(bool _needCheckRespPacket);

    struct CallBack
    {
        using Ptr = std::shared_ptr<CallBack>;
        RespCallBack respCallBack;
        std::shared_ptr<boost::asio::steady_timer> timer;
    };
    void addRespCallback(const std::string& _seq, CallBack::Ptr _callback);
    CallBack::Ptr getAndRemoveRespCallback(
        const std::string& _seq, const WsMessage* _message = nullptr);
    void onRespTimeout(const boost::system::error_code& _error, const std::string& _seq);

    void onWsAccept(boost::beast::error_code _ec);

    struct Message;

    void asyncRead();
    void asyncWrite(std::shared_ptr<Message> _message);

    void send(std::shared_ptr<Message> _message);

    // async read
    void onReadPacket();
    void onWritePacket();

    struct Message
    {
        bcos::bytes buffer;
    };

protected:
    IOServicePool::Ptr m_ioServicePool;

    // flag for message that need to check respond packet like p2p message
    bool m_needCheckRspPacket = false;
    //
    std::atomic_bool m_isDrop = false;
    // websocket protocol version
    std::atomic<uint16_t> m_version = 0;

    // buffer used to read message
    std::shared_ptr<boost::beast::flat_buffer> m_buffer;

    std::string m_endPoint;
    std::string m_nodeId;

    //
    int32_t m_sendMsgTimeout = -1;
    //
    int32_t m_maxWriteMsgSize = -1;

    //
    WsStreamDelegate::Ptr m_wsStreamDelegate;
    // callbacks
    mutable bcos::Mutex x_callback;
    std::unordered_map<std::string, CallBack::Ptr> m_callbacks;

    // callback handler
    WsConnectHandler m_connectHandler;
    WsDisconnectHandler m_disconnectHandler;
    WsRecvMessageHandler m_recvMessageHandler;

    // raw wire format flag
    bool m_rawMessage = false;

    // ioc
    std::shared_ptr<boost::asio::io_context> m_ioc;
    // send message queue
    mutable bcos::Mutex x_writeQueue;
    std::priority_queue<std::shared_ptr<Message>> m_writeQueue;
    std::atomic_bool m_writing = {false};
};

}  // namespace bcos::boostssl::ws
