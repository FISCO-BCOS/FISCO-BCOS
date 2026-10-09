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
 * @brief the local attach channel: newline-delimited JSON-RPC over a unix socket in the data
 *        directory. One request line → one response line; a connection is served serially.
 *        Authentication is the socket file's permission (umask of the node process).
 * @file IpcServer.h
 */
#pragma once

#include <bcos-utilities/Common.h>
#include <boost/asio/io_context.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/beast/http/status.hpp>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>

namespace bcos::rpc
{
/// (request body, reply) — the reply callback may be invoked from any thread
using IpcHandler = std::function<void(
    std::string_view, std::function<void(bcos::bytes, boost::beast::http::status)>)>;

/// one request line in flight at a time; rejects lines above the limit by closing
constexpr size_t c_ipcMaxLineBytes = 16 * 1024 * 1024;

class IpcSession : public std::enable_shared_from_this<IpcSession>
{
public:
    using Ptr = std::shared_ptr<IpcSession>;
    IpcSession(boost::asio::local::stream_protocol::socket _socket, IpcHandler _handler,
        std::function<void(IpcSession::Ptr)> _onClose);
    void start();
    void close();

private:
    void readLine();
    void onLine(size_t _length);
    void write(bcos::bytes _response);

    boost::asio::local::stream_protocol::socket m_socket;
    boost::asio::streambuf m_buffer;
    IpcHandler m_handler;
    std::function<void(IpcSession::Ptr)> m_onClose;
    bool m_closed = false;
};

class IpcServer : public std::enable_shared_from_this<IpcServer>
{
public:
    using Ptr = std::shared_ptr<IpcServer>;
    IpcServer(boost::asio::io_context& _io, std::string _path, IpcHandler _handler);
    ~IpcServer();
    /// unlinks a stale file, binds, listens, accepts; throws on bind failure
    void start();
    /// closes the acceptor and every session, unlinks the file
    void stop();
    std::string const& path() const { return m_path; }
    bool running() const { return m_running; }

private:
    void accept();
    void removeSession(IpcSession::Ptr _session);

    boost::asio::io_context& m_io;
    std::string m_path;
    IpcHandler m_handler;
    std::shared_ptr<boost::asio::local::stream_protocol::acceptor> m_acceptor;  // touched on m_io
                                                                                // only
    std::mutex m_sessionsMutex;
    std::set<IpcSession::Ptr> m_sessions;
    std::atomic<bool> m_running{false};
};
}  // namespace bcos::rpc
