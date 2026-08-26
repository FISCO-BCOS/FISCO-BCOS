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
 * @file Socket.h
 * @brief Minimal RAII blocking TCP socket used by the RLPx client/server.
 * @date 2026/8/18
 */
#pragma once

#include <bcos-utilities/Common.h>
#include <cstdint>
#include <string>

namespace bcos::devp2p::rlpx
{
// Blocking TCP socket with full-read/full-write helpers.
class Socket
{
public:
    Socket();
    explicit Socket(int _fd);
    ~Socket();
    Socket(Socket const&) = delete;
    Socket& operator=(Socket const&) = delete;
    Socket(Socket&&) noexcept;
    Socket& operator=(Socket&&) noexcept;

    // Connect to a host (IP or DNS) and port. Throws on failure.
    void connect(std::string const& _host, uint16_t _port);
    void close();
    // Write all bytes. Throws on error.
    void sendAll(bytesConstRef _data);
    // Read exactly _size bytes. Throws on EOF/error.
    bcos::bytes recvFixed(size_t _size);

    int fd() const { return m_fd; }
    bool valid() const { return m_fd >= 0; }

private:
    int m_fd{-1};
};

// Blocking TCP listener (used by the server side and by loopback tests).
class TcpListener
{
public:
    explicit TcpListener(uint16_t _port);
    ~TcpListener();
    TcpListener(TcpListener const&) = delete;
    TcpListener& operator=(TcpListener const&) = delete;

    // Block until a connection arrives and return it.
    Socket accept();

    int fd() const { return m_fd; }
    // The port this listener is bound to (useful when bound to port 0).
    uint16_t port() const;

private:
    int m_fd{-1};
};
}  // namespace bcos::devp2p::rlpx
