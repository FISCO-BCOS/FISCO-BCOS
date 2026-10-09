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
 * @file IpcClient.cpp
 */
#include "IpcClient.h"
#include "OpsError.h"
#include <boost/asio/buffers_iterator.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/write.hpp>
#include <atomic>
#include <mutex>

namespace bcos::ops
{
namespace
{
using Socket = boost::asio::local::stream_protocol::socket;

/// runs one async step with a deadline; returns false on timeout
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

struct IpcTransport
{
    boost::asio::io_context io;
    Socket socket{io};
    boost::asio::streambuf buffer;
    std::string path;
    int timeoutMs = 15000;
    std::mutex mutex;  // one request at a time on the connection
    std::atomic<int64_t> nextId{1};
    bool dead = false;  // a timed-out request left the stream out of step; never reuse it

    void connect(int _timeoutMs)
    {
        boost::system::error_code ec;
        bool done = runWithDeadline(io, _timeoutMs, [&]() {
            socket.async_connect(boost::asio::local::stream_protocol::endpoint(path),
                [&](boost::system::error_code const& _ec) {
                    ec = _ec;
                    io.stop();
                });
        });
        if (!done)
        {
            // the connect handler is still pending: close the socket and drain it while ec/this
            // frame are alive
            boost::system::error_code ignored;
            socket.close(ignored);
            io.restart();
            io.run();
            throw OpsError(c_exitUsage, "connect to socket " + path + " timed out");
        }
        if (ec)
        {
            throw OpsError(c_exitUsage, "connect to socket " + path + " failed: " + ec.message());
        }
    }

    std::string roundTrip(std::string const& _line)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (dead)
        {
            throw OpsError(c_exitUsage, "socket " + path + ": connection lost after a timeout");
        }
        boost::system::error_code ec;
        boost::asio::write(socket, boost::asio::buffer(_line), ec);
        if (ec)
        {
            dead = true;
            throw OpsError(c_exitUsage, "write to socket " + path + " failed: " + ec.message());
        }
        size_t length = 0;
        bool done = runWithDeadline(io, timeoutMs, [&]() {
            boost::asio::async_read_until(
                socket, buffer, '\n', [&](boost::system::error_code const& _ec, size_t _n) {
                    ec = _ec;
                    length = _n;
                    io.stop();
                });
        });
        if (!done)
        {
            // the read handler still references ec/length on this frame: cancel it by closing
            // the socket and run the loop until it has fired, then retire the connection so a
            // late reply can never be mistaken for the next request's answer
            dead = true;
            boost::system::error_code ignored;
            socket.close(ignored);
            io.restart();
            io.run();
            throw OpsError(c_exitUsage, "socket " + path + ": response timed out");
        }
        if (ec)
        {
            dead = true;
            throw OpsError(c_exitUsage, "read from socket " + path + " failed: " + ec.message());
        }
        auto begin = boost::asio::buffers_begin(buffer.data());
        std::string line(begin, begin + static_cast<std::ptrdiff_t>(length));
        buffer.consume(length);
        return line;
    }
};
}  // namespace

Connection makeIpcRpcCall(std::string const& _path, int _connectTimeoutMs, int _requestTimeoutMs)
{
    auto transport = std::make_shared<IpcTransport>();
    transport->path = _path;
    transport->timeoutMs = _requestTimeoutMs;
    transport->connect(_connectTimeoutMs);

    Connection connection;
    connection.source = "attach";
    connection.endpoint = _path;
    connection.keepAlive = transport;
    connection.call = [transport](std::string_view _method, Json::Value const& _params) {
        auto id = transport->nextId.fetch_add(1);
        auto line = buildJsonRpcRequest(_method, _params, id) + "\n";
        auto response = transport->roundTrip(line);
        auto envelope = parseJson(response, _method);
        if (!envelope.isMember("id") || envelope["id"].asInt64() != id)
        {
            transport->dead = true;
            throw OpsError(c_exitUsage, std::string(_method) + ": response id mismatch (expected " +
                                            std::to_string(id) + ")");
        }
        return unwrapStringResult(_method, parseJsonRpcResponse(response, _method));
    };
    return connection;
}
}  // namespace bcos::ops
