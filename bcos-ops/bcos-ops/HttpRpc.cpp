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
#include "bcos-ops/Output.h"
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
namespace
{
/// a busy pool's txpool_content is the largest answer this client reads; beast's default
/// response cap (8 MB) would turn it into a transport error
constexpr uint64_t c_httpRpcBodyLimit = 64ULL * 1024 * 1024;
constexpr size_t c_httpRpcErrorExcerpt = 200;
}  // namespace

RpcCall makeHttpRpcCall(std::string _host, uint16_t _port, int _timeoutMs)
{
    auto nextId = std::make_shared<std::atomic<int64_t>>(1);
    return [host = std::move(_host), _port, _timeoutMs, nextId](
               std::string_view _method, Json::Value const& _params) -> Json::Value {
        namespace beast = boost::beast;
        namespace http = beast::http;
        using tcp = boost::asio::ip::tcp;
        auto where = host + ":" + std::to_string(_port);
        auto fail = [&](std::string const& _why) {
            return OpsError(
                c_exitUsage, std::string(_method) + " to web3 " + where + " failed: " + _why);
        };
        http::request<http::string_body> request{http::verb::post, "/", 11};
        request.set(http::field::host, where);
        request.set(http::field::content_type, "application/json");
        request.body() = buildJsonRpcRequest(_method, _params, nextId->fetch_add(1));
        request.prepare_payload();
        http::response_parser<http::string_body> parser;
        parser.body_limit(c_httpRpcBodyLimit);
        beast::flat_buffer buffer;
        beast::error_code result;
        bool done = false;
        // one async chain on a private io_context, run for at most _timeoutMs: the deadline
        // covers connect / write / read. An IP literal is resolved numerically (no lookup); a
        // host name goes through getaddrinfo, which cancel() cannot interrupt, so a hanging DNS
        // server can hold the call past the deadline until the lookup returns
        boost::asio::io_context ioc;
        tcp::resolver resolver(ioc);
        beast::tcp_stream stream(ioc);
        boost::system::error_code parsed;
        boost::asio::ip::make_address(host, parsed);
        auto flags = parsed ? tcp::resolver::flags{} : tcp::resolver::numeric_host;
        resolver.async_resolve(host, std::to_string(_port), flags,
            [&](beast::error_code _resolved, tcp::resolver::results_type _endpoints) {
                if (_resolved)
                {
                    result = _resolved;
                    done = true;
                    return;
                }
                stream.async_connect(_endpoints, [&](beast::error_code _ec, tcp::endpoint const&) {
                    if (_ec)
                    {
                        result = _ec;
                        done = true;
                        return;
                    }
                    http::async_write(stream, request, [&](beast::error_code _written, size_t) {
                        if (_written)
                        {
                            result = _written;
                            done = true;
                            return;
                        }
                        http::async_read(
                            stream, buffer, parser, [&](beast::error_code _read, size_t) {
                                result = _read;
                                done = true;
                            });
                    });
                });
            });
        ioc.run_for(std::chrono::milliseconds(_timeoutMs));
        if (!done)
        {
            // cancel what is in flight and let the handlers (they capture this frame) finish
            resolver.cancel();
            beast::error_code ignored;
            stream.socket().close(ignored);
            ioc.restart();
            ioc.run();
            throw fail("no answer within " + std::to_string(_timeoutMs) + " ms");
        }
        if (result)
        {
            throw fail(result.message());
        }
        beast::error_code ignored;
        stream.socket().shutdown(tcp::socket::shutdown_both, ignored);
        auto const& response = parser.get();
        auto status = response.result_int();
        if (status < 200 || status >= 300)
        {
            // the body is the server's text: it reaches stderr / the TUI, so strip control bytes
            auto excerpt = sanitizeForTerminal(response.body().substr(0, c_httpRpcErrorExcerpt));
            throw fail("HTTP " + std::to_string(status) + ": " + excerpt);
        }
        return parseJsonRpcResponse(response.body(), _method);
    };
}

RpcCall makeHttpRpcCall(std::string const& _endpoint, int _timeoutMs)
{
    auto [host, port] = parseHostPort(_endpoint);
    return makeHttpRpcCall(host, port, _timeoutMs);
}
}  // namespace bcos::ops
