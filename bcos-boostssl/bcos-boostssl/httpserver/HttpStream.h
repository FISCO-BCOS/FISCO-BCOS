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
#include <bcos-boostssl/websocket/WsStream.h>
#include <bcos-boostssl/websocket/WsTools.h>
#include <bcos-utilities/Common.h>
#include <boost/beast/ssl/ssl_stream.hpp>
#include <memory>
#include <utility>

namespace bcos::boostssl::http
{
using HttpStreamRWHandler = std::function<void(boost::system::error_code, std::size_t)>;

// The http stream
class HttpStream
{
public:
    using Ptr = std::shared_ptr<HttpStream>;
    virtual ~HttpStream() = default;
    virtual boost::beast::tcp_stream& stream() = 0;
    virtual ws::WsStreamDelegate::Ptr wsStream() = 0;

    virtual bool open() = 0;
    virtual void close() = 0;

    virtual void asyncRead(boost::beast::flat_buffer& _buffer,
        boost::optional<boost::beast::http::request_parser<boost::beast::http::string_body>>&
            _parser,
        HttpStreamRWHandler _handler) = 0;

    virtual void asyncWrite(const HttpResponse& _httpResp, HttpStreamRWHandler _handler) = 0;

    virtual std::string localEndpoint();

    virtual std::string remoteEndpoint();

protected:
    std::atomic<bool> m_closed{false};
};

// The http stream
class HttpStreamImpl : public HttpStream, public std::enable_shared_from_this<HttpStreamImpl>
{
public:
    using Ptr = std::shared_ptr<HttpStreamImpl>;

    HttpStreamImpl(std::shared_ptr<boost::beast::tcp_stream> _stream);
    ~HttpStreamImpl() override;

    boost::beast::tcp_stream& stream() override;
    ws::WsStreamDelegate::Ptr wsStream() override;

    bool open() override;
    void close() override;

    void asyncRead(boost::beast::flat_buffer& _buffer,
        boost::optional<boost::beast::http::request_parser<boost::beast::http::string_body>>&
            _parser,
        HttpStreamRWHandler _handler) override;

    void asyncWrite(const HttpResponse& _httpResp, HttpStreamRWHandler _handler) override;


private:
    std::shared_ptr<boost::beast::tcp_stream> m_stream;
};

// The http stream
class HttpStreamSslImpl : public HttpStream, public std::enable_shared_from_this<HttpStreamSslImpl>
{
public:
    using Ptr = std::shared_ptr<HttpStreamSslImpl>;

    HttpStreamSslImpl(std::shared_ptr<boost::beast::ssl_stream<boost::beast::tcp_stream>> _stream);

    ~HttpStreamSslImpl() override;

    boost::beast::tcp_stream& stream() override;

    ws::WsStreamDelegate::Ptr wsStream() override;

    bool open() override;

    void close() override;

    void asyncRead(boost::beast::flat_buffer& _buffer,
        boost::optional<boost::beast::http::request_parser<boost::beast::http::string_body>>&
            _parser,
        HttpStreamRWHandler _handler) override;

    void asyncWrite(const HttpResponse& _httpResp, HttpStreamRWHandler _handler) override;

private:
    std::shared_ptr<boost::beast::ssl_stream<boost::beast::tcp_stream>> m_stream;
};

class HttpStreamFactory
{
public:
    using Ptr = std::shared_ptr<HttpStreamFactory>;

    HttpStream::Ptr buildHttpStream(std::shared_ptr<boost::beast::tcp_stream> _stream);

    HttpStream::Ptr buildHttpStream(
        std::shared_ptr<boost::beast::ssl_stream<boost::beast::tcp_stream>> _stream);
};
}  // namespace bcos::boostssl::http