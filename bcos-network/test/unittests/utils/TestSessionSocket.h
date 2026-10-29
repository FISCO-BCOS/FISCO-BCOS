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
 * @brief Shared socket test double for bcos-network's test suites.
 * @file TestSessionSocket.h
 *
 * A fake exposing the Socket interface BasicSession drives (see bcos-network/Socket.h):
 * isConnected/close/ref/sslref/stream/endpoints/ioService. Design points, migrated from the
 * per-file fakes of bcos-gateway/test/unittests (FakeSocket / FakeSocket_FIB /
 * FakeSocket_FIB97new / FakeSocket_Lifetime / ...):
 *
 *  - A connected TCP pair backs the SSL stream so drop()/closeSocket() can call
 *    sslref().async_shutdown()/ref() safely even on platforms where shutdown on an unconnected
 *    stream crashes (see the gateway FakeSocket_FIB comment). The acceptor side is closed when
 *    the constructor returns; the client side lives in the SSL stream.
 *  - stream() returns the SSL stream (not the raw TCP socket): production reads/writes
 *    dispatch on socket->stream() at compile time, and the one test that arms a REAL read
 *    (startUsesDefaultReadPolicy) relies on the un-handshaken SSL stream failing fast.
 *  - close() mirrors production Socket::close() observably: it flips the connected flag, bumps a
 *    close counter for double-teardown assertions, and closes the underlying TCP socket — so a
 *    later probe of ref().is_open() (e.g. closeSocket's SSL force-close callback) sees the same
 *    "transport already down" state a real socket would, without spuriously counting a second
 *    teardown close.
 *  - runIo: when true, a worker thread runs the socket's own io_context (needed when the test
 *    wants the session's posted teardown/write work to actually execute — e.g. the production
 *    start() read path). When false, the test drives ioService() with poll() explicitly; the
 *    default false keeps teardown timing deterministic for the drop/cap/lifetime suites.
 */
#pragma once

#include "bcos-utilities/Endpoint.h"
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <atomic>
#include <cstddef>
#include <memory>
#include <optional>
#include <thread>
#include <utility>

namespace bcos::network::test
{
class TestSessionSocket
{
public:
    explicit TestSessionSocket(bool _connected = true, bool _runIo = false)
      : m_ioContext(std::make_shared<boost::asio::io_context>()),
        m_sslContext(boost::asio::ssl::context::tlsv12)
    {
        // Create a connected TCP socket pair so the SSL stream has a valid transport (see the
        // class comment). The server end is deliberately not kept: once it closes, a later
        // async_shutdown completes with an error instead of dereferencing a dead stream.
        boost::asio::ip::tcp::acceptor acceptor(
            *m_ioContext, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), 0));
        auto endpoint = acceptor.local_endpoint();
        boost::asio::ip::tcp::socket clientSocket(*m_ioContext);
        clientSocket.connect(endpoint);
        boost::asio::ip::tcp::socket serverSocket(*m_ioContext);
        acceptor.accept(serverSocket);
        m_sslSocket = std::make_shared<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>(
            std::move(clientSocket), m_sslContext);

        m_connected.store(_connected);
        if (_runIo)
        {
            m_workGuard.emplace(boost::asio::make_work_guard(*m_ioContext));
            m_worker = std::thread([ioContext = m_ioContext] { ioContext->run(); });
        }
    }

    ~TestSessionSocket()
    {
        if (m_workGuard)
        {
            m_workGuard->reset();
        }
        m_ioContext->stop();
        if (m_worker.joinable())
        {
            if (m_worker.get_id() == std::this_thread::get_id())
            {
                m_worker.detach();
            }
            else
            {
                m_worker.join();
            }
        }
    }

    TestSessionSocket(const TestSessionSocket&) = delete;
    TestSessionSocket(TestSessionSocket&&) = delete;
    TestSessionSocket& operator=(const TestSessionSocket&) = delete;
    TestSessionSocket& operator=(TestSessionSocket&&) = delete;

    bool isConnected() const { return m_connected.load(); }
    void close()
    {
        m_connected.store(false);
        ++m_closeCount;
        // Mirror production Socket::close(): take the transport down so code probing
        // ref().is_open() afterwards (the SSL shutdown callbacks in closeSocket) sees it closed.
        // Every invocation is still counted — a spurious extra close() must stay observable.
        try
        {
            boost::system::error_code ec;
            m_sslSocket->lowest_layer().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
            if (m_sslSocket->lowest_layer().is_open())
            {
                m_sslSocket->lowest_layer().close(ec);
            }
        }
        catch (...)
        {}
    }
    // Direct flag control for tests that simulate a peer that already went away before the
    // session's (deferred) teardown runs.
    void setConnected(bool _connected) { m_connected.store(_connected); }
    std::size_t closeCount() const { return m_closeCount.load(); }

    boost::asio::ip::tcp::endpoint remoteEndpoint(boost::system::error_code = {}) { return {}; }
    boost::asio::ip::tcp::endpoint localEndpoint(boost::system::error_code = {}) { return {}; }
    boost::asio::ip::tcp::socket& ref() { return m_sslSocket->next_layer(); }
    boost::asio::ssl::stream<boost::asio::ip::tcp::socket>& sslref() { return *m_sslSocket; }
    boost::asio::ssl::stream<boost::asio::ip::tcp::socket>& stream() { return *m_sslSocket; }

    const NodeIPEndpoint& nodeIPEndpoint() const { return m_nodeIPEndpoint; }
    void setNodeIPEndpoint(NodeIPEndpoint _nodeIPEndpoint)
    {
        m_nodeIPEndpoint = std::move(_nodeIPEndpoint);
    }

    boost::asio::io_context& ioService() { return *m_ioContext; }

private:
    // Declaration order matters: members are destroyed in reverse, so the io_context outlives
    // the SSL stream that deregisters from it in its destructor, and the context outlives the
    // stream's ssl::context reference.
    std::shared_ptr<boost::asio::io_context> m_ioContext;
    boost::asio::ssl::context m_sslContext;
    std::shared_ptr<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>> m_sslSocket;
    std::optional<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>>
        m_workGuard;
    std::thread m_worker;
    NodeIPEndpoint m_nodeIPEndpoint;
    std::atomic<bool> m_connected{true};
    std::atomic<std::size_t> m_closeCount{0};
};
}  // namespace bcos::network::test
