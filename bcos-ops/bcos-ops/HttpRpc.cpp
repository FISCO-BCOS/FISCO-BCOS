/**
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @brief see HttpRpc.h
 * @file HttpRpc.cpp
 */
#include "HttpRpc.h"
#include "bcos-ops/Connect.h"
#include "bcos-ops/OpsError.h"
#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <atomic>
#include <chrono>
#include <memory>

namespace bcos::ops
{
RpcCall makeHttpRpcCall(std::string _host, uint16_t _port, int _timeoutMs)
{
    auto nextId = std::make_shared<std::atomic<int64_t>>(1);
    return [host = std::move(_host), _port, _timeoutMs, nextId](
               std::string_view _method, Json::Value const& _params) -> Json::Value {
        namespace beast = boost::beast;
        namespace http = beast::http;
        using tcp = boost::asio::ip::tcp;
        auto where = host + ":" + std::to_string(_port);
        http::request<http::string_body> request{http::verb::post, "/", 11};
        request.set(http::field::host, where);
        request.set(http::field::content_type, "application/json");
        request.body() = buildJsonRpcRequest(_method, _params, nextId->fetch_add(1));
        request.prepare_payload();
        http::response<http::string_body> response;
        beast::flat_buffer buffer;
        beast::error_code result;
        // the tcp_stream deadline only bounds asynchronous operations, so the exchange runs as
        // one async chain on a private io_context
        boost::asio::io_context ioc;
        tcp::resolver resolver(ioc);
        beast::tcp_stream stream(ioc);
        try
        {
            auto endpoints = resolver.resolve(host, std::to_string(_port));
            stream.expires_after(std::chrono::milliseconds(_timeoutMs));
            stream.async_connect(endpoints, [&](beast::error_code _ec, tcp::endpoint const&) {
                if (_ec)
                {
                    result = _ec;
                    return;
                }
                http::async_write(stream, request, [&](beast::error_code _written, size_t) {
                    if (_written)
                    {
                        result = _written;
                        return;
                    }
                    http::async_read(stream, buffer, response,
                        [&](beast::error_code _read, size_t) { result = _read; });
                });
            });
            ioc.run();
        }
        catch (std::exception const& e)
        {
            throw OpsError(
                c_exitUsage, std::string(_method) + " to web3 " + where + " failed: " + e.what());
        }
        if (result)
        {
            throw OpsError(c_exitUsage,
                std::string(_method) + " to web3 " + where + " failed: " + result.message());
        }
        beast::error_code ignored;
        stream.socket().shutdown(tcp::socket::shutdown_both, ignored);
        return parseJsonRpcResponse(response.body(), _method);
    };
}

RpcCall makeHttpRpcCall(std::string const& _endpoint, int _timeoutMs)
{
    auto [host, port] = parseHostPort(_endpoint);
    return makeHttpRpcCall(host, port, _timeoutMs);
}
}  // namespace bcos::ops
