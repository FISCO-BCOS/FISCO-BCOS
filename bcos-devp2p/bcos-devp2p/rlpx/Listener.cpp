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
 * @file Listener.cpp
 * @brief Inbound RLPx listener implementation.
 * @date 2026/11/3
 */
#include "Listener.h"

#if !defined(_WIN32)  // like Socket.cpp: the blocking RLPx socket layer is POSIX only

#include <bcos-utilities/LogStream.h>
#include <fcntl.h>
#include <boost/asio/post.hpp>
#include <chrono>
#include <exception>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace bcos::devp2p::rlpx
{
Listener::Listener(EccKeyPair _keyPair, ListenerConfig _config, StatusProvider _statusProvider,
    SessionHandler _onSession, HandshakeOptions _handshakeOptions)
  : m_keyPair(std::move(_keyPair)),
    m_config(std::move(_config)),
    m_statusProvider(std::move(_statusProvider)),
    m_onSession(std::move(_onSession)),
    m_handshakeOptions(std::move(_handshakeOptions)),
    m_acceptor(m_io),
    m_acceptRetry(m_io)
{
    if (!m_statusProvider || !m_onSession)
    {
        throw std::invalid_argument("Listener: status provider and session handler are required");
    }
}

Listener::~Listener()
{
    stop();
}

void Listener::start()
{
    if (m_running.exchange(true))
    {
        return;
    }
    try
    {
        boost::asio::ip::tcp::endpoint endpoint(
            boost::asio::ip::make_address(m_config.bindAddress), m_config.port);
        m_acceptor.open(endpoint.protocol());
        m_acceptor.set_option(boost::asio::socket_base::reuse_address(true));
        m_acceptor.bind(endpoint);
        m_acceptor.listen(boost::asio::socket_base::max_listen_connections);
        m_boundPort = m_acceptor.local_endpoint().port();
    }
    catch (std::exception const&)
    {
        m_running.store(false);
        boost::system::error_code ignored;
        m_acceptor.close(ignored);
        throw;
    }
    m_io.restart();
    armAccept();
    m_ioThread = std::thread([self = shared_from_this()]() {
        try
        {
            self->m_io.run();
        }
        catch (std::exception const& e)
        {
            BCOS_LOG(ERROR) << LOG_BADGE("rlpx-listener") << "acceptor thread exited on exception"
                            << LOG_KV("error", e.what());
        }
    });
    BCOS_LOG(INFO) << LOG_BADGE("rlpx-listener") << "listening"
                   << LOG_KV("bind", m_config.bindAddress) << LOG_KV("port", m_boundPort);
}

void Listener::stop()
{
    if (!m_running.exchange(false))
    {
        return;
    }
    m_io.stop();
    if (m_ioThread.joinable())
    {
        m_ioThread.join();
    }
    // The io thread is gone, so nothing else touches the acceptor.
    boost::system::error_code ignored;
    m_acceptor.close(ignored);
    // Wake every connection parked in session I/O, then join them all. A connection
    // still inside the handshake has no session yet: it ends at its next socket I/O
    // timeout (15s per read), once for each handshake step a silent peer stalls.
    std::vector<std::shared_ptr<Connection>> connections;
    {
        std::unique_lock lock(m_connectionsMutex);
        connections.swap(m_connections);
    }
    {
        std::unique_lock lock(m_connectionsMutex);  // runConnection publishes `established`
        for (auto const& connection : connections)
        {
            if (connection->established)
            {
                connection->established->session.shutdown();
            }
        }
    }
    for (auto const& connection : connections)
    {
        if (connection->thread.joinable())
        {
            connection->thread.join();
        }
    }
    BCOS_LOG(INFO) << LOG_BADGE("rlpx-listener") << "stopped" << LOG_KV("port", m_boundPort);
}

size_t Listener::liveConnections() const
{
    std::unique_lock lock(m_connectionsMutex);
    size_t live = 0;
    for (auto const& connection : m_connections)
    {
        live += connection->done.load() ? 0 : 1;
    }
    return live;
}

void Listener::armAccept()
{
    m_acceptor.async_accept([weak = weak_from_this()](boost::system::error_code const& _error,
                                boost::asio::ip::tcp::socket _socket) {
        auto self = weak.lock();
        if (!self || !self->m_running.load())
        {
            return;
        }
        if (_error)
        {
            if (_error != boost::asio::error::operation_aborted)
            {
                BCOS_LOG(WARNING) << LOG_BADGE("rlpx-listener") << "accept failed"
                                  << LOG_KV("error", _error.message());
                // Transient (EMFILE, ECONNABORTED). Accepting again at once would spin: the
                // error repeats until a descriptor frees up.
                self->m_acceptRetry.expires_after(std::chrono::milliseconds(100));
                self->m_acceptRetry.async_wait(
                    [weak = self->weak_from_this()](boost::system::error_code const& _waitError) {
                        auto retry = weak.lock();
                        if (!_waitError && retry && retry->m_running.load())
                        {
                            retry->armAccept();
                        }
                    });
            }
            return;
        }
        self->onAccepted(std::move(_socket));
        self->armAccept();
    });
}

void Listener::onAccepted(boost::asio::ip::tcp::socket&& _socket)
{
    reapFinished();
    {
        std::unique_lock lock(m_connectionsMutex);
        if (m_connections.size() >= m_config.maxConnections)
        {
            BCOS_LOG(DEBUG) << LOG_BADGE("rlpx-listener") << "connection limit reached, closing";
            return;  // _socket closes on destruction in the accept handler
        }
    }
    std::string remote;
    try
    {
        auto const endpoint = _socket.remote_endpoint();
        remote = endpoint.address().to_string() + ":" + std::to_string(endpoint.port());
    }
    catch (std::exception const&)
    {
        remote = "<unknown>";
    }
    // Hand the fd to the blocking Socket the RLPx stack is built on. asio ran the
    // accept with the listener in non-blocking mode and BSD sockets inherit the file
    // status flags, so restore blocking mode explicitly (same as Socket::connect).
    boost::system::error_code releaseError;
    int const fd = static_cast<int>(_socket.release(releaseError));
    if (releaseError || fd < 0)
    {
        BCOS_LOG(WARNING) << LOG_BADGE("rlpx-listener") << "cannot take over accepted socket"
                          << LOG_KV("remote", remote) << LOG_KV("error", releaseError.message());
        return;
    }
    if (int const flags = ::fcntl(fd, F_GETFL, 0); flags >= 0)
    {
        ::fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
    }
    Socket socket(fd);

    auto connection = std::make_shared<Connection>();
    {
        std::unique_lock lock(m_connectionsMutex);
        m_connections.push_back(connection);
    }
    try
    {
        connection->thread = std::thread(
            [self = shared_from_this(), connection,
                socket = std::make_shared<Socket>(std::move(socket)), remote]() mutable {
                self->runConnection(connection, std::move(*socket), remote);
                {
                    // Release the session (and with it the socket) now that the handler is
                    // done. Left to the next accept's reapFinished() or to stop(), a quiet
                    // node would keep a finished peer's TCP connection open indefinitely.
                    std::unique_lock lock(self->m_connectionsMutex);
                    connection->established.reset();
                }
                connection->done.store(true);
            });
    }
    catch (std::system_error const& e)
    {
        // Out of threads: drop this peer. Escaping here would end the acceptor thread.
        std::unique_lock lock(m_connectionsMutex);
        std::erase(m_connections, connection);
        BCOS_LOG(WARNING) << LOG_BADGE("rlpx-listener") << "cannot start connection thread"
                          << LOG_KV("remote", remote) << LOG_KV("error", e.what());
    }
}

void Listener::runConnection(
    std::shared_ptr<Connection> const& _connection, Socket&& _socket, std::string const& _remote)
{
    try
    {
        auto config = m_statusProvider();
        config.listenPort = m_boundPort;
        auto established = std::make_shared<EstablishedSession>(
            acceptHandshake(std::move(_socket), m_keyPair, config, m_handshakeOptions));
        {
            std::unique_lock lock(m_connectionsMutex);
            _connection->established = established;
        }
        if (!m_running.load())
        {
            return;  // stop() raced the handshake: do not start serving
        }
        BCOS_LOG(INFO) << LOG_BADGE("rlpx-listener") << "inbound peer connected"
                       << LOG_KV("remote", _remote)
                       << LOG_KV("peerClient", established->peerHello.clientId)
                       << LOG_KV("eth", established->peerStatus.protocolVersion);
        m_onSession(*established);
        BCOS_LOG(INFO) << LOG_BADGE("rlpx-listener") << "inbound peer session ended"
                       << LOG_KV("remote", _remote);
    }
    catch (HandshakeRejected const& e)
    {
        BCOS_LOG(INFO) << LOG_BADGE("rlpx-listener") << "inbound peer rejected"
                       << LOG_KV("remote", _remote) << LOG_KV("reason", static_cast<int>(e.reason))
                       << LOG_KV("error", e.what());
    }
    catch (std::exception const& e)
    {
        BCOS_LOG(INFO) << LOG_BADGE("rlpx-listener") << "inbound peer dropped"
                       << LOG_KV("remote", _remote) << LOG_KV("error", e.what());
    }
    catch (...)
    {
        BCOS_LOG(WARNING) << LOG_BADGE("rlpx-listener") << "inbound peer dropped: unknown error"
                          << LOG_KV("remote", _remote);
    }
}

void Listener::reapFinished()
{
    // Join the threads of connections that already ended (called on the io thread on
    // every accept; the remaining ones are joined by stop()).
    std::vector<std::shared_ptr<Connection>> finished;
    {
        std::unique_lock lock(m_connectionsMutex);
        for (auto it = m_connections.begin(); it != m_connections.end();)
        {
            if ((*it)->done.load())
            {
                finished.push_back(std::move(*it));
                it = m_connections.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
    for (auto const& connection : finished)
    {
        if (connection->thread.joinable())
        {
            connection->thread.join();
        }
    }
}
}  // namespace bcos::devp2p::rlpx

#endif  // !defined(_WIN32)
