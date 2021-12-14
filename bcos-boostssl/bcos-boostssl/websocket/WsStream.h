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
 *  limitations under the License.
 *
 * @file WsStream.h
 * @author: octopus
 * @date 2021-10-29
 */
#pragma once

#include <bcos-boostssl/httpserver/Common.h>
#include <bcos-boostssl/utilities/BoostLog.h>
#include <bcos-boostssl/utilities/Common.h>
#include <bcos-boostssl/websocket/Common.h>
#include <bcos-boostssl/websocket/WsTools.h>
#include <boost/asio/buffer.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/stream.hpp>
#include <boost/thread/thread.hpp>
#include <algorithm>
#include <memory>
#include <utility>

namespace bcos
{
namespace boostssl
{
namespace ws
{
enum WS_STREAM_TYPE
{
    WS = 1,
    WS_SSL = 2
};

using WsStreamRWHandler = std::function<void(boost::system::error_code, std::size_t)>;
using WsStreamHandshakeHandler = std::function<void(boost::system::error_code)>;

class WsStream
{
public:
    using Ptr = std::shared_ptr<WsStream>;

public:
    virtual ~WsStream() {}

public:
    virtual boost::beast::tcp_stream& stream() = 0;

    virtual void ping() = 0;
    virtual void pong() = 0;

    virtual bool open() = 0;
    virtual void close() = 0;

    virtual void asyncHandshake(
        bcos::boostssl::http::HttpRequest _httpRequest, WsStreamHandshakeHandler _handler) = 0;
    virtual void asyncRead(boost::beast::flat_buffer& _buffer, WsStreamRWHandler _handler) = 0;
    virtual void asyncWrite(
        const bcos::boostssl::utilities::bytes& _buffer, WsStreamRWHandler _handler) = 0;

    virtual std::string localEndpoint()
    {
        try
        {
            auto& s = stream();
            auto localEndPoint = s.socket().local_endpoint();
            auto endPoint =
                localEndPoint.address().to_string() + ":" + std::to_string(localEndPoint.port());
            return endPoint;
        }
        catch (const std::exception& e)
        {
            WEBSOCKET_STREAM(WARNING) << LOG_BADGE("localEndpoint") << LOG_KV("e", e.what());
        }

        return std::string("");
    }

    virtual std::string remoteEndpoint()
    {
        try
        {
            auto& s = stream();
            auto remoteEndpoint = s.socket().remote_endpoint();
            auto endPoint =
                remoteEndpoint.address().to_string() + ":" + std::to_string(remoteEndpoint.port());
            return endPoint;
        }
        catch (const std::exception& e)
        {
            WEBSOCKET_STREAM(WARNING) << LOG_BADGE("remoteEndpoint") << LOG_KV("e", e.what());
        }

        return std::string("");
    }

protected:
    std::atomic<bool> m_closed{false};
};

class WsStreamImpl : public WsStream
{
public:
    using Ptr = std::shared_ptr<WsStreamImpl>;

public:
    WsStreamImpl(std::shared_ptr<boost::beast::websocket::stream<boost::beast::tcp_stream>> _stream)
      : m_stream(_stream)
    {
        WEBSOCKET_STREAM(INFO) << LOG_KV("[NEWOBJ][WsStreamImpl]", this);
    }

    virtual ~WsStreamImpl()
    {
        WEBSOCKET_STREAM(INFO) << LOG_KV("[DELOBJ][WsStreamImpl]", this);
        close();
    }

public:
    virtual boost::beast::tcp_stream& stream() override { return m_stream->next_layer(); }

    void ping() override
    {
        boost::system::error_code error;
        m_stream->ping(boost::beast::websocket::ping_data(), error);
    }

    void pong() override
    {
        boost::system::error_code error;
        m_stream->pong(boost::beast::websocket::ping_data(), error);
    }

    bool open() override { return !m_closed.load() && m_stream->next_layer().socket().is_open(); }

    void close() override
    {
        bool trueValue = true;
        bool falseValue = false;
        if (m_closed.compare_exchange_strong(falseValue, trueValue))
        {
            ws::WsTools::close(m_stream->next_layer().socket());
            WEBSOCKET_STREAM(INFO) << LOG_DESC("close the stream") << LOG_KV("this", this);
        }
    }

    void asyncHandshake(
        bcos::boostssl::http::HttpRequest _httpRequest, WsStreamHandshakeHandler _handler) override
    {
        m_stream->async_accept(_httpRequest, boost::beast::bind_front_handler(_handler));
    }

    void asyncRead(boost::beast::flat_buffer& _buffer, WsStreamRWHandler _handler) override
    {
        m_stream->async_read(_buffer, _handler);
    }

    void asyncWrite(
        const bcos::boostssl::utilities::bytes& _buffer, WsStreamRWHandler _handler) override
    {
        m_stream->binary(true);
        m_stream->async_write(boost::asio::buffer(_buffer), _handler);
    }

private:
    std::shared_ptr<boost::beast::websocket::stream<boost::beast::tcp_stream>> m_stream;
};


class WsStreamSslImpl : public WsStream
{
public:
    using Ptr = std::shared_ptr<WsStreamImpl>;

public:
    WsStreamSslImpl(std::shared_ptr<
        boost::beast::websocket::stream<boost::beast::ssl_stream<boost::beast::tcp_stream>>>
            _stream)
      : m_stream(_stream)
    {
        WEBSOCKET_SSL_STREAM(INFO) << LOG_KV("[NEWOBJ][WsStreamSslImpl]", this);
    }

    virtual ~WsStreamSslImpl()
    {
        WEBSOCKET_SSL_STREAM(INFO) << LOG_KV("[DELOBJ][WsStreamSslImpl]", this);
        close();
    }

public:
    virtual boost::beast::tcp_stream& stream() override
    {
        return m_stream->next_layer().next_layer();
    }

    void ping() override
    {
        boost::system::error_code error;
        m_stream->ping(boost::beast::websocket::ping_data(), error);
    }

    void pong() override
    {
        boost::system::error_code error;
        m_stream->pong(boost::beast::websocket::ping_data(), error);
    }

    bool open() override
    {
        return !m_closed.load() && m_stream->next_layer().next_layer().socket().is_open();
    }

    void close() override
    {
        bool trueValue = true;
        bool falseValue = false;
        if (m_closed.compare_exchange_strong(falseValue, trueValue))
        {
            WsTools::close(m_stream->next_layer().next_layer().socket());
            WEBSOCKET_SSL_STREAM(INFO) << LOG_DESC("close the ssl stream") << LOG_KV("this", this);
        }
    }

    void asyncHandshake(
        bcos::boostssl::http::HttpRequest _httpRequest, WsStreamHandshakeHandler _handler) override
    {
        m_stream->async_accept(_httpRequest, boost::beast::bind_front_handler(_handler));
    }

    void asyncRead(boost::beast::flat_buffer& _buffer, WsStreamRWHandler _handler) override
    {
        m_stream->async_read(_buffer, _handler);
    }

    void asyncWrite(
        const bcos::boostssl::utilities::bytes& _buffer, WsStreamRWHandler _handler) override
    {
        m_stream->binary(true);
        m_stream->async_write(boost::asio::buffer(_buffer), _handler);
    }

private:
    std::shared_ptr<
        boost::beast::websocket::stream<boost::beast::ssl_stream<boost::beast::tcp_stream>>>
        m_stream;
};

class WsStreamFactory
{
public:
    using Ptr = std::shared_ptr<WsStreamFactory>;

public:
    WsStream::Ptr buildWsStream(
        std::shared_ptr<boost::beast::websocket::stream<boost::beast::tcp_stream>> _stream)
    {
        return std::make_shared<WsStreamImpl>(_stream);
    }

    WsStream::Ptr buildWsStream(std::shared_ptr<
        boost::beast::websocket::stream<boost::beast::ssl_stream<boost::beast::tcp_stream>>>
            _stream)
    {
        return std::make_shared<WsStreamSslImpl>(_stream);
    }
};

}  // namespace ws
}  // namespace boostssl
}  // namespace bcos