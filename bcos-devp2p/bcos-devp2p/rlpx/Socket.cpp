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

// The blocking RLPx socket is built on the POSIX socket API (arpa/inet.h,
// poll.h, sys/socket.h, ...), which does not exist on Windows. Compile the
// implementation only off Windows; nothing links the RLPx sync client there.
#if !defined(_WIN32)

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <stdexcept>

namespace bcos::devp2p::rlpx
{
namespace
{
// Send flags: Linux suppresses SIGPIPE per-send with MSG_NOSIGNAL, while Darwin
// and the BSDs do not define MSG_NOSIGNAL and only offer the SO_NOSIGPIPE socket
// option. Without either, send() to a closed peer raises SIGPIPE and kills the
// process.
#if defined(MSG_NOSIGNAL)
constexpr int c_sendFlags = MSG_NOSIGNAL;
#else
constexpr int c_sendFlags = 0;
#endif

// Silence SIGPIPE on the platforms that only expose the SO_NOSIGPIPE socket
// option (Darwin/BSD); a no-op elsewhere.
void applyNoSigpipe(int _fd)
{
#if defined(SO_NOSIGPIPE)
    int one = 1;
    ::setsockopt(_fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
}

// One poll() bounded by the time remaining until `_deadline`. Retries EINTR
// against the remaining budget and throws when the poll itself fails or the
// deadline expires, so the whole read/write is bounded by the caller's total
// timeout instead of by each individual poll() gap.
void pollWithRemaining(int _fd, short _events,
    std::chrono::steady_clock::time_point const& _deadline, int _totalTimeoutMs,
    std::string const& _what)
{
    while (true)
    {
        auto const now = std::chrono::steady_clock::now();
        auto const remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(_deadline - now).count();
        if (remaining <= 0)
        {
            throw std::runtime_error(
                _what + " timed out after " + std::to_string(_totalTimeoutMs) + "ms");
        }
        struct pollfd pfd;
        pfd.fd = _fd;
        pfd.events = _events;
        pfd.revents = 0;
        int const pollRc = ::poll(&pfd, 1, static_cast<int>(remaining));
        if (pollRc > 0)
        {
            return;
        }
        if (pollRc < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            throw std::runtime_error(_what + " failed: " + std::string(strerror(errno)));
        }
        throw std::runtime_error(
            _what + " timed out after " + std::to_string(_totalTimeoutMs) + "ms");
    }
}
}  // namespace

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
    applyNoSigpipe(m_fd);
}

Socket::Socket(int _fd) : m_fd(_fd)
{
    applyNoSigpipe(m_fd);
}

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
    // freeaddrinfo is not required to preserve errno, so snapshot it first.
    int const connectErrno = errno;
    ::freeaddrinfo(result);

    if (connectRc != 0 && connectErrno != EINPROGRESS)
    {
        throw std::runtime_error("Socket::connect: connect to " + _host + ":" + portStr +
                                 " failed: " + std::string(strerror(connectErrno)));
    }

    if (connectRc != 0)
    {
        // Non-blocking connect in progress (EINPROGRESS): wait for completion with a
        // bounded timeout. A black-holed peer would otherwise block here for the
        // kernel's TCP retry budget.
        auto const deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(c_connectTimeoutMs);
        pollWithRemaining(m_fd, POLLOUT, deadline, c_connectTimeoutMs,
            "Socket::connect: connect to " + _host + ":" + portStr);
        // Distinguish an actual connection error from a spurious wakeup.
        int soError = 0;
        socklen_t soLen = sizeof(soError);
        if (::getsockopt(m_fd, SOL_SOCKET, SO_ERROR, &soError, &soLen) != 0 || soError != 0)
        {
            throw std::runtime_error(
                "Socket::connect: connect to " + _host + ":" + portStr +
                " failed: " + std::string(strerror(soError != 0 ? soError : connectErrno)));
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
    // Bound the WHOLE write by one deadline (not each poll) so a peer that
    // dribbles out a byte every so often cannot hold the sync loop forever.
    auto const deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(c_ioTimeoutMs);
    size_t sent = 0;
    while (sent < _data.size())
    {
        pollWithRemaining(m_fd, POLLOUT, deadline, c_ioTimeoutMs, "Socket::sendAll: write");
        ssize_t n = ::send(m_fd, _data.data() + sent, _data.size() - sent, c_sendFlags);
        if (n < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            throw std::runtime_error(
                "Socket::sendAll: send failed: " + std::string(strerror(errno)));
        }
        sent += static_cast<size_t>(n);
    }
}

bcos::bytes Socket::recvFixed(size_t _size)
{
    bcos::bytes out(_size);
    // One deadline for the whole read: a peer that sends one byte per poll gap
    // must not be able to stall the caller past c_ioTimeoutMs in total.
    auto const deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(c_ioTimeoutMs);
    size_t received = 0;
    while (received < _size)
    {
        pollWithRemaining(m_fd, POLLIN, deadline, c_ioTimeoutMs, "Socket::recvFixed: read");
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
            throw std::runtime_error(
                "Socket::recvFixed: recv failed: " + std::string(strerror(errno)));
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
        throw std::runtime_error("TcpListener: bind failed: " + std::string(strerror(errno)));
    }
    if (::listen(m_fd, 16) != 0)
    {
        ::close(m_fd);
        m_fd = -1;
        throw std::runtime_error("TcpListener: listen failed: " + std::string(strerror(errno)));
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
        throw std::runtime_error(
            "TcpListener::accept: accept failed: " + std::string(strerror(errno)));
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

#endif  // !defined(_WIN32)
