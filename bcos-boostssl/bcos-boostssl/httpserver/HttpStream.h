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
 * @file HttpStream.h
 * @author: octopus
 * @date 2021-10-31
 */
#pragma once
#include <bcos-boostssl/httpserver/Common.h>
#include <bcos-boostssl/utilities/Common.h>
#include <bcos-boostssl/websocket/WsStream.h>
#include <bcos-boostssl/websocket/WsTools.h>
#include <boost/beast/ssl/ssl_stream.hpp>
#include <memory>
#include <stdexcept>
#include <utility>

namespace bcos
{
namespace boostssl
{
namespace http
{
using HttpStreamRWHandler = std::function<void(boost::system::error_code, std::size_t)>;

// The http stream
class HttpStream
{
public:
    using Ptr = std::shared_ptr<HttpStream>;

    virtual ~HttpStream() {}

public:
    virtual boost::beast::tcp_stream& stream() = 0;

    virtual ws::WsStream::Ptr wsStream() = 0;

    virtual bool open() = 0;
    virtual void close() = 0;

    virtual void asyncRead(boost::beast::flat_buffer& _buffer,
        boost::optional<boost::beast::http::request_parser<boost::beast::http::string_body>>&
            _parser,
        HttpStreamRWHandler _handler) = 0;

    virtual void asyncWrite(const HttpResponse& _httpResp, HttpStreamRWHandler _handler) = 0;

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
        catch (...)
        {
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
        catch (...)
        {
        }

        return std::string("");
    }

protected:
    std::atomic<bool> m_closed{false};
};

// The http stream
class HttpStreamImpl : public HttpStream, public std::enable_shared_from_this<HttpStreamImpl>
{
public:
    using Ptr = std::shared_ptr<HttpStreamImpl>;

public:
    HttpStreamImpl(std::shared_ptr<boost::beast::tcp_stream> _stream) : m_stream(_stream)
    {
        HTTP_STREAM(DEBUG) << LOG_KV("[NEWOBJ][HttpStreamImpl]", this);
    }
    virtual ~HttpStreamImpl()
    {
        HTTP_STREAM(DEBUG) << LOG_KV("[DELOBJ][HttpStreamImpl]", this);
        close();
    }

public:
    virtual boost::beast::tcp_stream& stream() override { return *m_stream; }

    virtual ws::WsStream::Ptr wsStream() override
    {
        auto wsStream = std::make_shared<ws::WsStreamImpl>(
            std::make_shared<boost::beast::websocket::stream<boost::beast::tcp_stream>>(
                std::move(*m_stream)));
        m_closed.store(true);
        return wsStream;
    }

    virtual bool open() override
    {
        if (!m_closed.load() && m_stream)
        {
            return m_stream->socket().is_open();
        }
        return false;
    }
    virtual void close() override
    {
        if (m_closed.load())
        {
            return;
        }

        bool trueValue = true;
        bool falseValue = false;
        if (m_closed.compare_exchange_strong(falseValue, trueValue))
        {
            HTTP_STREAM(INFO) << LOG_DESC("close the stream") << LOG_KV("this", this);
            ws::WsTools::close(m_stream->socket());
        }
    }

    virtual void asyncRead(boost::beast::flat_buffer& _buffer,
        boost::optional<boost::beast::http::request_parser<boost::beast::http::string_body>>&
            _parser,
        HttpStreamRWHandler _handler) override
    {
        boost::beast::http::async_read(*m_stream, _buffer, *_parser, _handler);
    }

    virtual void asyncWrite(const HttpResponse& _httpResp, HttpStreamRWHandler _handler) override
    {
        boost::beast::http::async_write(*m_stream, _httpResp, _handler);
    }

private:
    std::shared_ptr<boost::beast::tcp_stream> m_stream;
};


// The http stream
class HttpStreamSslImpl : public HttpStream, public std::enable_shared_from_this<HttpStreamSslImpl>
{
public:
    using Ptr = std::shared_ptr<HttpStreamSslImpl>;

public:
    HttpStreamSslImpl(std::shared_ptr<boost::beast::ssl_stream<boost::beast::tcp_stream>> _stream)
      : m_stream(_stream)
    {
        HTTP_STREAM(DEBUG) << LOG_KV("[NEWOBJ][HttpStreamSslImpl]", this);
    }

    virtual ~HttpStreamSslImpl()
    {
        HTTP_STREAM(DEBUG) << LOG_KV("[DELOBJ][HttpStreamSslImpl]", this);
        close();
    }

public:
    virtual boost::beast::tcp_stream& stream() override { return m_stream->next_layer(); }

    virtual ws::WsStream::Ptr wsStream() override
    {
        auto stream = std::make_shared<
            boost::beast::websocket::stream<boost::beast::ssl_stream<boost::beast::tcp_stream>>>(
            std::move(*m_stream));
        m_closed.store(true);
        auto wsStream = std::make_shared<ws::WsStreamSslImpl>(stream);
        return wsStream;
    }

    virtual bool open() override
    {
        if (!m_closed.load() && m_stream)
        {
            return m_stream->next_layer().socket().is_open();
        }

        return false;
    }

    virtual void close() override
    {
        if (m_closed.load())
        {
            return;
        }

        bool trueValue = true;
        bool falseValue = false;
        if (m_closed.compare_exchange_strong(falseValue, trueValue))
        {
            HTTP_STREAM(INFO) << LOG_DESC("close the ssl stream") << LOG_KV("this", this);
            ws::WsTools::close(m_stream->next_layer().socket());
        }
    }

    virtual void asyncRead(boost::beast::flat_buffer& _buffer,
        boost::optional<boost::beast::http::request_parser<boost::beast::http::string_body>>&
            _parser,
        HttpStreamRWHandler _handler) override
    {
        boost::beast::http::async_read(*m_stream, _buffer, *_parser, _handler);
    }

    virtual void asyncWrite(const HttpResponse& _httpResp, HttpStreamRWHandler _handler) override
    {
        boost::beast::http::async_write(*m_stream, _httpResp, _handler);
    }

private:
    std::shared_ptr<boost::beast::ssl_stream<boost::beast::tcp_stream>> m_stream;
};

class HttpStreamFactory
{
public:
    using Ptr = std::shared_ptr<HttpStreamFactory>;

public:
    HttpStream::Ptr buildHttpStream(std::shared_ptr<boost::beast::tcp_stream> _stream)
    {
        return std::make_shared<HttpStreamImpl>(_stream);
    }

    HttpStream::Ptr buildHttpStream(
        std::shared_ptr<boost::beast::ssl_stream<boost::beast::tcp_stream>> _stream)
    {
        return std::make_shared<HttpStreamSslImpl>(_stream);
    }
};
}  // namespace http
}  // namespace boostssl
}  // namespace bcos