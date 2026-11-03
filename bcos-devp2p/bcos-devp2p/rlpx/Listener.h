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
 * @file Listener.h
 * @brief Inbound RLPx listener: a boost::asio acceptor on its own io_context thread
 *        hands every accepted TCP connection to a per-connection thread that runs the
 *        recipient-side ECIES handshake, the Hello/Status exchange (acceptHandshake)
 *        and then the caller's session handler (the eth serving loop).
 * @date 2026/11/3
 */
#pragma once

#include "Client.h"
#include "Crypto.h"
#include "Session.h"
#include "Socket.h"
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace bcos::devp2p::rlpx
{
struct ListenerConfig
{
    std::string bindAddress{"0.0.0.0"};
    uint16_t port{30303};  // 0 = ephemeral (tests); the bound port is port() after start()
};

// Owns the acceptor thread and every connection thread it spawned; stop() closes the
// acceptor, shuts the live sessions down and joins all of them. Handlers run on the
// connection threads and may block in session I/O: a Session::shutdown() from stop()
// wakes them. Always hold a Listener in a shared_ptr (async handlers capture weak_ptr).
class Listener : public std::enable_shared_from_this<Listener>
{
public:
    // The Status fields (clientId, networkId, genesisHash, forkId, block range) for ONE
    // inbound handshake, read at accept time so the announced head is current.
    using StatusProvider = std::function<PeerConfig()>;
    // Drives the established session until it ends (returns or throws); runs on the
    // connection thread. The session is shut down by stop() if still alive.
    using SessionHandler = std::function<void(EstablishedSession&)>;

    Listener(EccKeyPair _keyPair, ListenerConfig _config, StatusProvider _statusProvider,
        SessionHandler _onSession, HandshakeOptions _handshakeOptions = {});
    ~Listener();
    Listener(Listener const&) = delete;
    Listener& operator=(Listener const&) = delete;

    // Bind + listen (throws on failure, nothing started) and spawn the acceptor thread.
    void start();
    // Idempotent. Returns once every thread is joined.
    void stop();

    // The bound port (valid after start()).
    uint16_t port() const { return m_boundPort; }
    size_t liveConnections() const;

private:
    struct Connection
    {
        std::thread thread;
        std::shared_ptr<EstablishedSession> established;  // set once the handshake is done
        std::atomic<bool> done{false};
    };

    void armAccept();
    void onAccepted(boost::asio::ip::tcp::socket&& _socket);
    void runConnection(std::shared_ptr<Connection> const& _connection, Socket&& _socket,
        std::string const& _remote);
    void reapFinished();

    EccKeyPair m_keyPair;
    ListenerConfig m_config;
    StatusProvider m_statusProvider;
    SessionHandler m_onSession;
    HandshakeOptions m_handshakeOptions;

    boost::asio::io_context m_io;
    boost::asio::ip::tcp::acceptor m_acceptor;
    std::thread m_ioThread;
    std::atomic<bool> m_running{false};
    uint16_t m_boundPort{0};

    mutable std::mutex m_connectionsMutex;
    std::vector<std::shared_ptr<Connection>> m_connections;
};
}  // namespace bcos::devp2p::rlpx
