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
 * @file RpcClient.cpp
 */
#include "RpcClient.h"
#include "OpsError.h"
#include <bcos-boostssl/websocket/WsConfig.h>
#include <bcos-cpp-sdk/Sdk.h>
#include <bcos-cpp-sdk/SdkFactory.h>
#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/write.hpp>
#include <atomic>
#include <chrono>
#include <future>

namespace bcos::ops
{
namespace
{
constexpr unsigned char c_tlsAlert = 0x15;
constexpr unsigned char c_tlsHandshake = 0x16;

/// runs one asio step with a deadline; returns false on timeout
template <typename Start>
bool runWithDeadline(boost::asio::io_context& _io, int _timeoutMs, Start&& _start)
{
    _io.restart();
    bool timedOut = false;
    boost::asio::steady_timer timer(_io);
    timer.expires_after(std::chrono::milliseconds(_timeoutMs));
    timer.async_wait([&](boost::system::error_code const& ec) {
        if (!ec)
        {
            timedOut = true;
            _io.stop();
        }
    });
    _start();
    _io.run();
    timer.cancel();
    return !timedOut;
}

struct WsTransport
{
    std::shared_ptr<bcos::cppsdk::SdkFactory> factory;  // owns the IOServicePool
    std::shared_ptr<bcos::cppsdk::Sdk> sdk;             // destroyed first
    std::atomic<int64_t> nextId{1};
    ~WsTransport()
    {
        if (!sdk)
        {
            return;
        }
        // WsStream::~WsStream runs a synchronous websocket close that waits for the peer's close
        // frame; the node never sends one to a client that just goes away, so a CLI would hang
        // at exit. Closing the TCP socket first makes that destructor skip the blocking close.
        for (auto const& session : sdk->service()->sessions())
        {
            try
            {
                if (auto delegate = session->wsStreamDelegate())
                {
                    boost::system::error_code ignored;
                    delegate->tcpStream().socket().close(ignored);
                }
            }
            catch (std::exception const&)
            {}
        }
        sdk->stop();
    }
};
}  // namespace

namespace
{
// A minimal TLS 1.2 ClientHello (one cipher suite, no extensions). OpenSSL answers it with a
// ServerHello record (0x16) or an alert (0x15); it answers a plaintext HTTP request on a TLS port
// with a silent close (SSL_R_HTTP_REQUEST), which is why an HTTP probe cannot detect TLS.
constexpr unsigned char c_clientHello[] = {
    0x16, 0x03, 0x01, 0x00, 0x2d,                    // record: handshake, TLS 1.0, 45 bytes
    0x01, 0x00, 0x00, 0x29,                          // handshake: ClientHello, 41 bytes
    0x03, 0x03,                                      // client version TLS 1.2
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // random (32 bytes)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00,        // session id length
    0x00, 0x02,  // cipher suites length
    0xc0, 0x2f,  // TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256
    0x01, 0x00   // compression methods: null
};
}  // namespace

std::pair<std::string, uint16_t> resolveHost(
    std::string const& _host, uint16_t _port, int _timeoutMs)
{
    boost::system::error_code ec;
    auto address = boost::asio::ip::make_address(_host, ec);
    if (!ec)
    {
        return {address.to_string(), _port};
    }
    boost::asio::io_context io;
    boost::asio::ip::tcp::resolver resolver(io);
    boost::asio::ip::tcp::resolver::results_type results;
    boost::system::error_code resolveEc;
    bool done = runWithDeadline(io, _timeoutMs, [&]() {
        resolver.async_resolve(
            _host, std::to_string(_port), [&](boost::system::error_code const& _ec, auto _results) {
                resolveEc = _ec;
                results = std::move(_results);
                io.stop();
            });
    });
    if (!done || resolveEc || results.empty())
    {
        throw OpsError(c_exitUsage,
            "cannot resolve host " + _host + (resolveEc ? ": " + resolveEc.message() : ""));
    }
    for (auto const& entry : results)  // IPv4 first
    {
        if (entry.endpoint().address().is_v4())
        {
            return {entry.endpoint().address().to_string(), _port};
        }
    }
    return {results.begin()->endpoint().address().to_string(), _port};
}

bool probeTls(std::string const& _host, uint16_t _port, int _timeoutMs)
{
    using boost::asio::ip::tcp;
    boost::asio::io_context io;
    tcp::socket socket(io);
    boost::system::error_code connectEc;
    tcp::endpoint endpoint(boost::asio::ip::make_address(_host), _port);
    bool done = runWithDeadline(io, _timeoutMs, [&]() {
        socket.async_connect(endpoint, [&](boost::system::error_code const& ec) {
            connectEc = ec;
            io.stop();
        });
    });
    if (!done)
    {
        throw OpsError(c_exitUsage, "connect to " + _host + ":" + std::to_string(_port) +
                                        " timed out after " + std::to_string(_timeoutMs) + "ms");
    }
    if (connectEc)
    {
        throw OpsError(c_exitUsage, "connect to " + _host + ":" + std::to_string(_port) +
                                        " failed: " + connectEc.message());
    }
    boost::system::error_code writeEc;
    boost::asio::write(socket, boost::asio::buffer(c_clientHello, sizeof(c_clientHello)), writeEc);
    if (writeEc)
    {
        return false;
    }
    unsigned char first = 0;
    boost::system::error_code readEc;
    size_t got = 0;
    runWithDeadline(io, _timeoutMs, [&]() {
        socket.async_read_some(
            boost::asio::buffer(&first, 1), [&](boost::system::error_code const& ec, size_t n) {
                readEc = ec;
                got = n;
                io.stop();
            });
    });
    boost::system::error_code ignored;
    socket.close(ignored);
    if (got == 0)
    {
        // a plaintext HTTP server closes on a binary record without answering: not TLS as far as
        // this probe can tell; the WebSocket connect produces the real error if it is not a node
        return false;
    }
    return first == c_tlsAlert || first == c_tlsHandshake;
}

Connection makeWsRpcCall(
    std::string const& _hostOrName, uint16_t _port, int _connectTimeoutMs, int _requestTimeoutMs)
{
    auto [_host, resolvedPort] = resolveHost(_hostOrName, _port, _connectTimeoutMs);
    auto endpoint = _hostOrName + ":" + std::to_string(_port);
    if (probeTls(_host, _port, _connectTimeoutMs))
    {
        throw OpsError(c_exitUsage, "RPC port " + endpoint +
                                        " has SSL enabled; this tool only supports plaintext RPC "
                                        "(set [rpc] disable_ssl=true or use the local socket)");
    }
    auto config = std::make_shared<bcos::boostssl::ws::WsConfig>();
    config->setModel(bcos::boostssl::ws::WsModel::Client);
    config->setDisableSsl(true);
    config->setThreadPoolSize(1);
    config->setSendMsgTimeout(_requestTimeoutMs);
    auto peers = std::make_shared<bcos::boostssl::ws::EndPoints>();
    peers->insert(bcos::boostssl::NodeIPEndpoint(_host, _port));
    config->setConnectPeers(peers);

    auto transport = std::make_shared<WsTransport>();
    transport->factory = std::make_shared<bcos::cppsdk::SdkFactory>();
    transport->sdk = transport->factory->buildSdk(config, false);
    transport->sdk->service()->wsService()->setWaitConnectFinishTimeout(_connectTimeoutMs);
    try
    {
        transport->sdk->start();
    }
    catch (std::exception const& e)
    {
        throw OpsError(c_exitUsage, "connect to " + endpoint + " failed: " + e.what());
    }

    Connection connection;
    connection.source = "rpc";
    connection.endpoint = endpoint;
    connection.keepAlive = transport;
    connection.call = [transport, _requestTimeoutMs](
                          std::string_view _method, Json::Value const& _params) -> Json::Value {
        auto id = transport->nextId.fetch_add(1);
        auto body = buildJsonRpcRequest(_method, _params, id);
        auto promise = std::make_shared<std::promise<std::pair<bcos::Error::Ptr, bcos::bytes>>>();
        auto future = promise->get_future();
        transport->sdk->jsonRpc()->genericMethod(
            body, [promise](bcos::Error::Ptr _error, bcos::bytes _response) {
                try
                {
                    promise->set_value({std::move(_error), std::move(_response)});
                }
                catch (std::future_error const&)
                {}
            });
        if (future.wait_for(std::chrono::milliseconds(_requestTimeoutMs + 500)) !=
            std::future_status::ready)
        {
            throw OpsError(c_exitUsage, std::string(_method) + " timed out");
        }
        auto [error, response] = future.get();
        if (error && error->errorCode() != 0)
        {
            throw OpsError(c_exitUsage, std::string(_method) + " failed: " + error->errorMessage());
        }
        std::string_view text(reinterpret_cast<char const*>(response.data()), response.size());
        return unwrapStringResult(_method, parseJsonRpcResponse(text, _method));
    };
    return connection;
}
}  // namespace bcos::ops
