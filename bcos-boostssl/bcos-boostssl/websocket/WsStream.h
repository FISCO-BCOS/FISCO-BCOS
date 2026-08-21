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
#include <bcos-boostssl/websocket/Common.h>
#include <bcos-boostssl/websocket/WsTools.h>
#include <bcos-utilities/BoostLog.h>
#include <bcos-utilities/Common.h>
#include <boost/asio/buffer.hpp>
#include <boost/asio/error.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/core/error.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/stream.hpp>
#include <boost/beast/websocket/stream_base.hpp>
#include <boost/system/detail/errc.hpp>
#include <boost/system/detail/error_code.hpp>
#include <boost/thread/thread.hpp>
#include <algorithm>
#include <functional>
#include <memory>
#include <utility>
#include <variant>

namespace bcos::boostssl::ws
{
using WsStreamRWHandler = std::function<void(boost::system::error_code, std::size_t)>;
using WsStreamHandshakeHandler = std::function<void(boost::system::error_code)>;

template <typename STREAM>
class WsStream
{
public:
    using Ptr = std::shared_ptr<WsStream>;

    explicit WsStream(boost::beast::websocket::stream<STREAM> _stream)
      : m_stream(std::move(_stream))
    {
        initDefaultOpt();
        WEBSOCKET_STREAM(INFO) << LOG_KV("[NEWOBJ][WsStream]", this);
    }

    ~WsStream()
    {
        WEBSOCKET_STREAM(INFO) << LOG_KV("[DELOBJ][WsStream]", this);
        if (open())
        {
            close();
        }
    }

    void initDefaultOpt()
    {
        /*
        // default close compress option because the loss of network compression is relatively large
        with boost websocket
        {
            boost::beast::websocket::permessage_deflate opt;
            opt.client_enable = true;  // for clients
            opt.server_enable = true;  // for servers

            m_stream->set_option(opt);
        }
        */

        // default timeout option
        {
            boost::beast::websocket::stream_base::timeout opt;
            // Note: make it config
            opt.handshake_timeout = std::chrono::milliseconds(30000);
            // idle time
            opt.idle_timeout = std::chrono::milliseconds(10000);
            // open ping/pong option
            opt.keep_alive_pings = true;

            m_stream.set_option(opt);
            m_stream.auto_fragment(false);
            m_stream.secure_prng(false);
            m_stream.write_buffer_bytes(2 * 1024 * 1024);
        }
    }

    void setMaxReadMsgSize(uint32_t _maxValue) { m_stream.read_message_max(_maxValue); }

    bool open() { return !m_closed.load() && m_stream.is_open(); }

    void close()
    {
        bool trueValue = true;
        bool falseValue = false;
        if (m_closed.compare_exchange_strong(falseValue, trueValue))
        {
            // websocket stream
            boost::beast::error_code ec;
            m_stream.close(boost::beast::websocket::close_code::normal, ec);

            // ssl stream
            shutdown(m_stream.next_layer());

            // tcp stream
            tcpStream().cancel();
            tcpStream().close();

            // socket
            auto& ss = boost::beast::get_lowest_layer(m_stream);
            ws::WsTools::close(ss.socket());
            WEBSOCKET_STREAM(INFO)
                << LOG_DESC("the real action to close the stream") << LOG_KV("this", this);
        }
    }

    void shutdown(boost::beast::tcp_stream& tcpStream)
    {
        // do nothing
    }

    void shutdown(boost::beast::ssl_stream<boost::beast::tcp_stream>& sslStream)
    {
        // websocket stream
        boost::beast::error_code ec;
        sslStream.shutdown(ec);
    }

    boost::beast::tcp_stream& tcpStream() { return boost::beast::get_lowest_layer(m_stream); }

    boost::beast::websocket::stream<STREAM>& stream() { return m_stream; }

public:
    void asyncWrite(const bcos::bytes& _buffer, WsStreamRWHandler _handler)
    {
        m_stream.binary(true);
        m_stream.async_write(boost::asio::buffer(_buffer), _handler);
    }

    void asyncRead(boost::beast::flat_buffer& _buffer, WsStreamRWHandler _handler)
    {
        m_stream.async_read(_buffer, _handler);
    }

    void asyncHandshake(const std::string& _host, const std::string& _target,
        std::function<void(boost::beast::error_code)> _handler)
    {
        m_stream.async_handshake(_host, _target, _handler);
    }

    void asyncAccept(
        bcos::boostssl::http::HttpRequest _httpRequest, WsStreamHandshakeHandler _handler)
    {
        m_stream.async_accept(_httpRequest, boost::beast::bind_front_handler(_handler));
    }

    std::string localEndpoint()
    {
        try
        {
            auto& s = tcpStream();
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

    std::string remoteEndpoint()
    {
        try
        {
            auto& s = tcpStream();
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

private:
    std::atomic<bool> m_closed{false};
    boost::beast::websocket::stream<STREAM> m_stream;
};

using RawWsStream = WsStream<boost::beast::tcp_stream>;
using SslWsStream = WsStream<boost::beast::ssl_stream<boost::beast::tcp_stream>>;

class WsStreamDelegate
{
public:
    using Ptr = std::shared_ptr<WsStreamDelegate>;

public:
    explicit WsStreamDelegate(RawWsStream::Ptr _rawStream);
    explicit WsStreamDelegate(SslWsStream::Ptr _sslStream);

public:
    void setMaxReadMsgSize(uint32_t _maxValue);
    bool open();
    void close();
    std::string localEndpoint();
    std::string remoteEndpoint();

    void asyncWrite(const bcos::bytes& _buffer, WsStreamRWHandler _handler);

    void asyncRead(boost::beast::flat_buffer& _buffer, WsStreamRWHandler _handler);

    void asyncWsHandshake(const std::string& _host, const std::string& _target,
        std::function<void(boost::beast::error_code)> _handler);

    void asyncAccept(
        bcos::boostssl::http::HttpRequest _httpRequest, WsStreamHandshakeHandler _handler);

    void asyncHandshake(std::function<void(boost::beast::error_code)> _handler);

    boost::beast::tcp_stream& tcpStream();

    void setVerifyCallback(bool _disableSsl, VerifyCallback callback, bool = true);

private:
    std::variant<RawWsStream::Ptr, SslWsStream::Ptr> m_stream;
};

class WsStreamDelegateBuilder
{
public:
    using Ptr = std::shared_ptr<WsStreamDelegateBuilder>;

public:
    WsStreamDelegate::Ptr build(boost::beast::tcp_stream _tcpStream);

    WsStreamDelegate::Ptr build(boost::beast::ssl_stream<boost::beast::tcp_stream> _sslStream);

    WsStreamDelegate::Ptr build(bool _disableSsl, std::shared_ptr<boost::asio::ssl::context> _ctx,
        std::shared_ptr<boost::beast::tcp_stream> _tcpStream);
};

}  // namespace bcos::boostssl::ws
