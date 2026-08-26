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
 * @file Socket.cpp
 * @brief Blocking TCP socket implementation (POSIX sockets).
 * @date 2026/8/18
 */
#include "Socket.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <stdexcept>

namespace bcos::devp2p::rlpx
{
// Connect timeout for outbound RLPx dials. A bootnode behind a black-hole firewall
// (packets silently dropped) would otherwise make the blocking connect() hang for
// the kernel's TCP timeout (tens of seconds to minutes), stalling the whole sync
// loop. With a bounded connect the loop moves on to the next bootnode and retries.
constexpr int c_connectTimeoutMs = 5000;
// I/O timeout for the blocking sendAll/recvFixed helpers. A peer that accepts the
// TCP connection but then neither sends (auth handshake, frames) nor reads our
// output would otherwise block the sync thread forever — the RLPx handshake and
// download loop would stall on a single silent peer. Bounded I/O lets the sync
// loop treat it as a failed peer and move on to the next bootnode.
constexpr int c_ioTimeoutMs = 15000;
Socket::Socket()
{
    m_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (m_fd < 0)
    {
        throw std::runtime_error("Socket: socket() failed: " + std::string(strerror(errno)));
    }
}

Socket::Socket(int _fd) : m_fd(_fd) {}

Socket::~Socket()
{
    close();
}

Socket::Socket(Socket&& _other) noexcept : m_fd(_other.m_fd)
{
    _other.m_fd = -1;
}

Socket& Socket::operator=(Socket&& _other) noexcept
{
    if (this != &_other)
    {
        close();
        m_fd = _other.m_fd;
        _other.m_fd = -1;
    }
    return *this;
}

void Socket::connect(std::string const& _host, uint16_t _port)
{
    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* result = nullptr;
    std::string portStr = std::to_string(_port);
    int rc = ::getaddrinfo(_host.c_str(), portStr.c_str(), &hints, &result);
    if (rc != 0 || result == nullptr)
    {
        throw std::runtime_error("Socket::connect: getaddrinfo failed for " + _host);
    }

    // Set the socket non-blocking so a black-holed peer makes connect() return
    // EINPROGRESS instead of blocking for the kernel's TCP timeout; completion is
    // then awaited with a bounded poll() below.
    int flags = ::fcntl(m_fd, F_GETFL, 0);
    if (flags >= 0)
    {
        ::fcntl(m_fd, F_SETFL, flags | O_NONBLOCK);
    }

    int connectRc = ::connect(m_fd, result->ai_addr, result->ai_addrlen);
    ::freeaddrinfo(result);

    if (connectRc != 0 && errno != EINPROGRESS)
    {
        throw std::runtime_error(
            "Socket::connect: connect to " + _host + ":" + portStr + " failed: " +
            std::string(strerror(errno)));
    }

    if (connectRc != 0)
    {
        // Non-blocking connect in progress (EINPROGRESS): wait for completion with a
        // bounded timeout. A black-holed peer would otherwise block here for the
        // kernel's TCP retry budget.
        struct pollfd pfd;
        pfd.fd = m_fd;
        pfd.events = POLLOUT;
        pfd.revents = 0;
        int pollRc = ::poll(&pfd, 1, c_connectTimeoutMs);
        if (pollRc <= 0)
        {
            throw std::runtime_error("Socket::connect: connect to " + _host + ":" + portStr +
                                     " timed out after " + std::to_string(c_connectTimeoutMs) +
                                     "ms");
        }
        // Distinguish an actual connection error from a spurious wakeup.
        int soError = 0;
        socklen_t soLen = sizeof(soError);
        if (::getsockopt(m_fd, SOL_SOCKET, SO_ERROR, &soError, &soLen) != 0 || soError != 0)
        {
            throw std::runtime_error("Socket::connect: connect to " + _host + ":" + portStr +
                                     " failed: " + std::string(strerror(soError != 0 ? soError :
                                                                                       errno)));
        }
    }

    // Restore the blocking mode the rest of the RLPx session relies on (sendAll /
    // recvFixed are blocking full-read/full-write helpers).
    int restoreFlags = ::fcntl(m_fd, F_GETFL, 0);
    if (restoreFlags >= 0)
    {
        ::fcntl(m_fd, F_SETFL, restoreFlags & ~O_NONBLOCK);
    }
}

void Socket::close()
{
    if (m_fd >= 0)
    {
        ::close(m_fd);
        m_fd = -1;
    }
}

void Socket::sendAll(bytesConstRef _data)
{
    size_t sent = 0;
    while (sent < _data.size())
    {
        // Wait for writability with a bounded timeout so a peer that never reads
        // our output (full send buffer) cannot stall the sync loop forever.
        struct pollfd pfd;
        pfd.fd = m_fd;
        pfd.events = POLLOUT;
        pfd.revents = 0;
        int pollRc = ::poll(&pfd, 1, c_ioTimeoutMs);
        if (pollRc <= 0)
        {
            throw std::runtime_error(
                "Socket::sendAll: write timed out after " + std::to_string(c_ioTimeoutMs) +
                "ms");
        }
        ssize_t n = ::send(m_fd, _data.data() + sent, _data.size() - sent, MSG_NOSIGNAL);
        if (n < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            throw std::runtime_error("Socket::sendAll: send failed: " + std::string(strerror(errno)));
        }
        sent += static_cast<size_t>(n);
    }
}

bcos::bytes Socket::recvFixed(size_t _size)
{
    bcos::bytes out(_size);
    size_t received = 0;
    while (received < _size)
    {
        // Wait for readability with a bounded timeout so a silent peer (accepts
        // the connection but never sends) cannot block the sync thread forever.
        struct pollfd pfd;
        pfd.fd = m_fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        int pollRc = ::poll(&pfd, 1, c_ioTimeoutMs);
        if (pollRc <= 0)
        {
            throw std::runtime_error(
                "Socket::recvFixed: read timed out after " + std::to_string(c_ioTimeoutMs) +
                "ms");
        }
        ssize_t n = ::recv(m_fd, out.data() + received, _size - received, 0);
        if (n == 0)
        {
            throw std::runtime_error("Socket::recvFixed: connection closed by peer");
        }
        if (n < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            throw std::runtime_error("Socket::recvFixed: recv failed: " + std::string(strerror(errno)));
        }
        received += static_cast<size_t>(n);
    }
    return out;
}

TcpListener::TcpListener(uint16_t _port)
{
    m_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (m_fd < 0)
    {
        throw std::runtime_error("TcpListener: socket() failed");
    }
    int opt = 1;
    ::setsockopt(m_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(_port);
    if (::bind(m_fd, (struct sockaddr*)&addr, sizeof(addr)) != 0)
    {
        ::close(m_fd);
        m_fd = -1;
        throw std::runtime_error(
            "TcpListener: bind failed: " + std::string(strerror(errno)));
    }
    if (::listen(m_fd, 16) != 0)
    {
        ::close(m_fd);
        m_fd = -1;
        throw std::runtime_error(
            "TcpListener: listen failed: " + std::string(strerror(errno)));
    }
}

TcpListener::~TcpListener()
{
    if (m_fd >= 0)
    {
        ::close(m_fd);
    }
}

Socket TcpListener::accept()
{
    struct sockaddr_in addr;
    socklen_t addrLen = sizeof(addr);
    int fd = ::accept(m_fd, (struct sockaddr*)&addr, &addrLen);
    if (fd < 0)
    {
        throw std::runtime_error("TcpListener::accept: accept failed: " + std::string(strerror(errno)));
    }
    return Socket(fd);
}

uint16_t TcpListener::port() const
{
    struct sockaddr_in addr;
    socklen_t addrLen = sizeof(addr);
    if (::getsockname(m_fd, (struct sockaddr*)&addr, &addrLen) != 0)
    {
        throw std::runtime_error("TcpListener::port: getsockname failed");
    }
    return ntohs(addr.sin_port);
}

}  // namespace bcos::devp2p::rlpx
