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
    // a well-formed JSON-RPC POST so a plaintext node answers 200 instead of logging an error
    std::string body = buildJsonRpcRequest("getGroupList", Json::Value(Json::arrayValue), 1);
    std::string probe =
        "POST / HTTP/1.1\r\nHost: " + _host +
        "\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) +
        "\r\nConnection: close\r\n\r\n" + body;
    boost::system::error_code writeEc;
    boost::asio::write(socket, boost::asio::buffer(probe), writeEc);
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
        // closed without a byte: cannot tell; let the WebSocket connect produce the real error
        return false;
    }
    return first == c_tlsAlert || first == c_tlsHandshake;
}

Connection makeWsRpcCall(std::string const& _host, uint16_t _port, int _timeoutMs)
{
    auto endpoint = _host + ":" + std::to_string(_port);
    if (probeTls(_host, _port, _timeoutMs))
    {
        throw OpsError(c_exitUsage, "RPC port " + endpoint +
                                        " has SSL enabled; this tool only supports plaintext RPC "
                                        "(set [rpc] disable_ssl=true or use the local socket)");
    }
    auto config = std::make_shared<bcos::boostssl::ws::WsConfig>();
    config->setModel(bcos::boostssl::ws::WsModel::Client);
    config->setDisableSsl(true);
    config->setThreadPoolSize(1);
    config->setSendMsgTimeout(_timeoutMs);
    auto peers = std::make_shared<bcos::boostssl::ws::EndPoints>();
    peers->insert(bcos::boostssl::NodeIPEndpoint(_host, _port));
    config->setConnectPeers(peers);

    auto transport = std::make_shared<WsTransport>();
    transport->factory = std::make_shared<bcos::cppsdk::SdkFactory>();
    transport->sdk = transport->factory->buildSdk(config, false);
    transport->sdk->service()->wsService()->setWaitConnectFinishTimeout(_timeoutMs);
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
    connection.call = [transport, _timeoutMs](
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
        if (future.wait_for(std::chrono::milliseconds(_timeoutMs + 500)) !=
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
