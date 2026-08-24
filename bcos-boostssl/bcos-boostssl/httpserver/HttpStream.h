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
#include <boost/atomic/atomic_flag.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>
#include <memory>
#include <variant>

namespace bcos::boostssl::http
{
using HttpStreamRWHandler = std::function<void(boost::system::error_code, std::size_t)>;

// The http stream, holds either a plain tcp stream or an ssl stream,
// dispatched statically via std::variant (the underlying type is fixed per connection)
class HttpStream
{
public:
    using Ptr = std::shared_ptr<HttpStream>;

    explicit HttpStream(boost::beast::tcp_stream _stream);
    explicit HttpStream(boost::beast::ssl_stream<boost::beast::tcp_stream> _stream);

    ~HttpStream();

    boost::beast::tcp_stream& stream();
    ws::WsStreamDelegate::Ptr wsStream();

    bool open();
    void close();

    void asyncRead(boost::beast::flat_buffer& _buffer,
        boost::beast::http::request_parser<boost::beast::http::string_body>& _parser,
        HttpStreamRWHandler _handler);

    void asyncWrite(const HttpResponse& _httpResp, HttpStreamRWHandler _handler);
    std::string endpoint(bool _local);
    std::string localEndpoint();
    std::string remoteEndpoint();

private:
    std::variant<boost::beast::tcp_stream, boost::beast::ssl_stream<boost::beast::tcp_stream>>
        m_stream;
    boost::atomic_flag m_closed;
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
