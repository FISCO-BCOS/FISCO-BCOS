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

namespace bcos
{
namespace boostssl
{
namespace ws
{
using WsStreamRWHandler = std::function<void(boost::system::error_code, std::size_t)>;
using WsStreamHandshakeHandler = std::function<void(boost::system::error_code)>;

template <typename STREAM>
class WsStream
{
public:
    using Ptr = std::shared_ptr<WsStream>;
    using ConstPtr = std::shared_ptr<const WsStream>;

    WsStream(
        std::shared_ptr<boost::beast::websocket::stream<STREAM>> _stream, std::string _moduleName)
      : m_stream(_stream), m_moduleName(_moduleName)
    {
        initDefaultOpt();
        WEBSOCKET_STREAM(INFO) << LOG_KV("[NEWOBJ][WsStream]", this);
    }

    virtual ~WsStream()
    {
        WEBSOCKET_STREAM(INFO) << LOG_KV("[DELOBJ][WsStream]", this);
        close();
    }

    void initDefaultOpt()
    {
        /* //TODO: close compress temp
        // default open compress option
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

            m_stream->set_option(opt);
            m_stream->auto_fragment(false);
            m_stream->secure_prng(false);
            m_stream->write_buffer_bytes(2 * 1024 * 1024);
        }
    }

public:
    //---------------  set opt params for websocket stream
    // begin-----------------------------
    void setMaxReadMsgSize(uint32_t _maxValue) { m_stream->read_message_max(_maxValue); }

    template <typename OPT>
    void setOpt(OPT _opt)
    {
        m_stream->set_option(_opt);
    }
    //---------------  set opt params for websocket stream  end
    //-------------------------------

    std::string moduleName() { return m_moduleName; }
    void setModuleName(std::string _moduleName) { m_moduleName = _moduleName; }

public:
    bool open() { return !m_closed.load() && m_stream->is_open(); }

    void close()
    {
        bool trueValue = true;
        bool falseValue = false;
        if (m_closed.compare_exchange_strong(falseValue, trueValue))
        {
            auto& ss = boost::beast::get_lowest_layer(*m_stream);
            ws::WsTools::close(ss.socket());
            WEBSOCKET_STREAM(INFO)
                << LOG_DESC("the real action to close the stream") << LOG_KV("this", this);
        }
        return;
    }

    boost::beast::tcp_stream& tcpStream() { return boost::beast::get_lowest_layer(*m_stream); }

    std::shared_ptr<boost::beast::websocket::stream<STREAM>> stream() const { return m_stream; }

public:
    void asyncWrite(const bcos::bytes& _buffer, WsStreamRWHandler _handler)
    {
        m_stream->binary(true);
        m_stream->async_write(boost::asio::buffer(_buffer), _handler);
    }

    void asyncRead(boost::beast::flat_buffer& _buffer, WsStreamRWHandler _handler)
    {
        m_stream->async_read(_buffer, _handler);
    }

    void asyncHandshake(const std::string& _host, const std::string& _target,
        std::function<void(boost::beast::error_code)> _handler)
    {
        m_stream->async_handshake(_host, _target, _handler);
    }

    void asyncAccept(
        bcos::boostssl::http::HttpRequest _httpRequest, WsStreamHandshakeHandler _handler)
    {
        m_stream->async_accept(_httpRequest, boost::beast::bind_front_handler(_handler));
    }

    virtual std::string localEndpoint()
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

    virtual std::string remoteEndpoint()
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
    std::shared_ptr<boost::beast::websocket::stream<STREAM>> m_stream;
    std::string m_moduleName = "DEFAULT";
};

using RawWsStream = WsStream<boost::beast::tcp_stream>;
using SslWsStream = WsStream<boost::beast::ssl_stream<boost::beast::tcp_stream>>;

class WsStreamDelegate
{
public:
    using Ptr = std::shared_ptr<WsStreamDelegate>;
    using ConstPtr = std::shared_ptr<const WsStreamDelegate>;

public:
    WsStreamDelegate(RawWsStream::Ptr _rawStream) : m_isSsl(false), m_rawStream(_rawStream) {}
    WsStreamDelegate(SslWsStream::Ptr _sslStream) : m_isSsl(true), m_sslStream(_sslStream) {}

public:
    void setMaxReadMsgSize(uint32_t _maxValue)
    {
        m_isSsl ? m_sslStream->setMaxReadMsgSize(_maxValue) :
                  m_rawStream->setMaxReadMsgSize(_maxValue);
    }
    bool open() { return m_isSsl ? m_sslStream->open() : m_rawStream->open(); }
    void close() { return m_isSsl ? m_sslStream->close() : m_rawStream->close(); }
    std::string localEndpoint()
    {
        return m_isSsl ? m_sslStream->localEndpoint() : m_rawStream->localEndpoint();
    }
    std::string remoteEndpoint()
    {
        return m_isSsl ? m_sslStream->remoteEndpoint() : m_rawStream->remoteEndpoint();
    }

    void asyncWrite(const bcos::bytes& _buffer, WsStreamRWHandler _handler)
    {
        return m_isSsl ? m_sslStream->asyncWrite(_buffer, _handler) :
                         m_rawStream->asyncWrite(_buffer, _handler);
    }

    void asyncRead(boost::beast::flat_buffer& _buffer, WsStreamRWHandler _handler)
    {
        return m_isSsl ? m_sslStream->asyncRead(_buffer, _handler) :
                         m_rawStream->asyncRead(_buffer, _handler);
    }

    void asyncWsHandshake(const std::string& _host, const std::string& _target,
        std::function<void(boost::beast::error_code)> _handler)
    {
        return m_isSsl ? m_sslStream->asyncHandshake(_host, _target, _handler) :
                         m_rawStream->asyncHandshake(_host, _target, _handler);
    }

    void asyncAccept(
        bcos::boostssl::http::HttpRequest _httpRequest, WsStreamHandshakeHandler _handler)
    {
        return m_isSsl ? m_sslStream->asyncAccept(_httpRequest, _handler) :
                         m_rawStream->asyncAccept(_httpRequest, _handler);
    }

    void asyncHandshake(std::function<void(boost::beast::error_code)> _handler)
    {
        if (m_isSsl)
        {
            m_sslStream->stream()->next_layer().async_handshake(
                boost::asio::ssl::stream_base::client, _handler);
        }
        else
        {  // callback directly
            _handler(make_error_code(boost::system::errc::success));
        }
    }

    boost::beast::tcp_stream& tcpStream()
    {
        return m_isSsl ? m_sslStream->tcpStream() : m_rawStream->tcpStream();
    }

    void setVerifyCallback(bool _disableSsl, VerifyCallback callback, bool = true)
    {
        if (!_disableSsl)
        {
            m_sslStream->stream()->next_layer().set_verify_callback(callback);
        }
    }

private:
    bool m_isSsl{false};

    RawWsStream::Ptr m_rawStream;
    SslWsStream::Ptr m_sslStream;
};

class WsStreamDelegateBuilder
{
public:
    using Ptr = std::shared_ptr<WsStreamDelegateBuilder>;
    using ConstPtr = std::shared_ptr<const WsStreamDelegateBuilder>;

public:
    WsStreamDelegate::Ptr build(
        std::shared_ptr<boost::beast::tcp_stream> _tcpStream, std::string _moduleName)
    {
        _tcpStream->socket().set_option(boost::asio::ip::tcp::no_delay(true));
        auto wsStream = std::make_shared<boost::beast::websocket::stream<boost::beast::tcp_stream>>(
            std::move(*_tcpStream));
        auto rawWsStream = std::make_shared<bcos::boostssl::ws::WsStream<boost::beast::tcp_stream>>(
            wsStream, _moduleName);
        return std::make_shared<WsStreamDelegate>(rawWsStream);
    }

    WsStreamDelegate::Ptr build(
        std::shared_ptr<boost::beast::ssl_stream<boost::beast::tcp_stream>> _sslStream,
        std::string _moduleName)
    {
        _sslStream->next_layer().socket().set_option(boost::asio::ip::tcp::no_delay(true));
        auto wsStream = std::make_shared<
            boost::beast::websocket::stream<boost::beast::ssl_stream<boost::beast::tcp_stream>>>(
            std::move(*_sslStream));
        auto sslWsStream = std::make_shared<
            bcos::boostssl::ws::WsStream<boost::beast::ssl_stream<boost::beast::tcp_stream>>>(
            wsStream, _moduleName);
        return std::make_shared<WsStreamDelegate>(sslWsStream);
    }

    WsStreamDelegate::Ptr build(bool _disableSsl, std::shared_ptr<boost::asio::ssl::context> _ctx,
        std::shared_ptr<boost::beast::tcp_stream> _tcpStream, std::string _moduleName)
    {
        if (_disableSsl)
        {
            return build(_tcpStream, _moduleName);
        }

        auto sslStream = std::make_shared<boost::beast::ssl_stream<boost::beast::tcp_stream>>(
            std::move(*_tcpStream), *_ctx);
        return build(sslStream, _moduleName);
    }
};

}  // namespace ws
}  // namespace boostssl
}  // namespace bcos
