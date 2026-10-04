/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief TLS loopback helpers for wire-level gateway unit tests.
 * @file TlsLoopback.h
 *
 * Wire-level tests drive the PRODUCTION Socket, whose reads/writes dispatch on its ssl::stream
 * at compile time (see libnetwork/Socket.h) — the old runtime TCP/SSL switch that let tests
 * exchange PLAINTEXT frames through socket->ref() is gone. These helpers run a REAL TLS
 * handshake over a loopback TCP pair so the session and its peer speak TLS, which is also
 * closer to production than the plaintext harness was.
 */
#pragma once

#include "bcos-gateway/libnetwork/Socket.h"
#include <boost/asio/buffer.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <future>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace bcos::gateway::testutil
{
// Self-signed RSA-2048 test certificate (CN=fisco-bcos-test, 100-year validity), generated once
// with: openssl req -x509 -newkey rsa:2048 -keyout key.pem -out cert.pem -days 36500 -nodes
// -subj "/CN=fisco-bcos-test". Embedded so the tests have no file dependency. Test-only key —
// never reuse it anywhere else.
inline constexpr std::string_view kTestCertPem = R"PEM(-----BEGIN CERTIFICATE-----
MIIDFzCCAf+gAwIBAgIUR5oz49LnDW5I5lq1khL7yWT46g8wDQYJKoZIhvcNAQEL
BQAwGjEYMBYGA1UEAwwPZmlzY28tYmNvcy10ZXN0MCAXDTI2MDkyNDA1MjEyOVoY
DzIxMjYwODMxMDUyMTI5WjAaMRgwFgYDVQQDDA9maXNjby1iY29zLXRlc3QwggEi
MA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQDGk9N+b8bwexzpadZK9OfPX/7D
A5cNZrmFvbzSSYHoyxcdSjJpgVUNtC02JYbrT4v7LEZw8bcFY+NxUdaAjCSnQ8d0
e1bkQhqna2a4HNWVdDlLpVauFn8iu86APDFs6I3aQ50hhqezVqFahpFaCFEmxaYz
6dFVHFYntEYaAGAordTrYXYwuib4pmI9pjFtbTrK7rn8cytnovYCPyFds0hjKzPL
QFRnLnZQeJ0vJHMXpKYxYinc2rw6T6pJKVdlgHVOUbCDBo0alBUdP0Iifwil5VYe
vdtxXw6qMFjr5gaZVGtBkg3/LtBEjRGoEvHfVFTp0HRgv2pb+iv868BvTPUzAgMB
AAGjUzBRMB0GA1UdDgQWBBTOwQ+X9lBGHDi7MA7EI1FLYiHZLTAfBgNVHSMEGDAW
gBTOwQ+X9lBGHDi7MA7EI1FLYiHZLTAPBgNVHRMBAf8EBTADAQH/MA0GCSqGSIb3
DQEBCwUAA4IBAQACMLL74f6LXkZPJPEt3YXnKuZWZxla9EpG7lNWbSuzM6KSXaLJ
Smy6I221mM9TB7edBKbZz/ITZJ3DmbruDBXsImgtgTwjnmCjeWf3uq/b4BQUtPVh
4HMfHLvwP3E5oKfVI9BWyABo848Jd76m04dSaGsJo1l65rcFNiPCYCOt6I44vMiu
czTYQiGfsxFD45rR+ldTJ8konxHdNtuc1v3QDT9hsuHNJWMi5Ih3NtvrDpl4qT/h
0z77tJyF5TTshD3S0DkCpC270JtDdbw+Wx5aXVeK0Tcec+TQ92u72tuH+fWxVUtz
CQO85XUqexDQgKeOVU7BR8AZKTZh563jdS/Z
-----END CERTIFICATE-----
)PEM";

inline constexpr std::string_view kTestKeyPem = R"PEM(-----BEGIN PRIVATE KEY-----
MIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQDGk9N+b8bwexzp
adZK9OfPX/7DA5cNZrmFvbzSSYHoyxcdSjJpgVUNtC02JYbrT4v7LEZw8bcFY+Nx
UdaAjCSnQ8d0e1bkQhqna2a4HNWVdDlLpVauFn8iu86APDFs6I3aQ50hhqezVqFa
hpFaCFEmxaYz6dFVHFYntEYaAGAordTrYXYwuib4pmI9pjFtbTrK7rn8cytnovYC
PyFds0hjKzPLQFRnLnZQeJ0vJHMXpKYxYinc2rw6T6pJKVdlgHVOUbCDBo0alBUd
P0Iifwil5VYevdtxXw6qMFjr5gaZVGtBkg3/LtBEjRGoEvHfVFTp0HRgv2pb+iv8
68BvTPUzAgMBAAECggEAD6MpdvNEwRWOJSAFVNlsqGsV9p5sQTe90/cpN41mp6yC
hqHGpCyf7th+7PIklJgq3PMoyDvInqngicEY4/46jq9q3/8sdwTGRjknbsfagTk/
Snfb17wEHqSXx4e17q5w0+dvfWngqoyKX+9pN86gMVju1HdiWXzwmJInfmRZg3+/
w19FcLtIv6+x2OkxRoaVA3MC9SghFtD4B8XzIInAFHKHssQ/+GE5+U1dELFk0m8t
tDoIAnBTllU6z+4VDkWcYKgwLddYIS8WbOb64Vsti38xiw47DULoj8sKi0+JrIEK
qU2CPoc5XEyrdUFSsTGd8kVz1yja6VYcPPXCCeqduQKBgQD+MAcasdqlEhaPPC2p
RY2aZMqZt13ZHr6zdHfdHrGEm/OkDwHJdn4dL0CKTFICCSQw77+rUZ81/s5kdNyg
TELNii410Jt/poHsWYxy57xnFYPh96VPY/22Mgfsl4ovaMmW8jIqmFePgRNPIuAH
ll0P2FKxxvvp+nBjCmqdcdpkKQKBgQDH/krZSs1YTPCXrHSHipoDW7K6DTl0TKzc
BqxtYqMfOwmvzFGWW/tOwtJZmkC6XSqC9c2BkjwgZztn8v5VLbHDdxdoo7WVqEso
U6e1efhMuTnxHECDvnHycI4YbljuZOW2gWq9tXUEWj8l0uHIEt2AatE2yPxR3g1s
TKCm7dbZ+wKBgEgOMVVd6Y81q3N7Ka58awHDZLNiiZYM4x5X+8qQ8t2Wn+B36JfF
oUaAqJkLvYuaL8o3jGvyPWG/E57iidBfDejaLPNQaWQsPVRUpj3Ed2H5dWNPImt4
+uj1Ec39v0xlNhA2JAZzHQ8vFdwvFLbR5xugxQBkWfEf71AEGej7517RAoGASWsu
GSUfy0m7jULPKK8WKSNxsmGGdQ5s6v08MY/jaGSpArOURAmScXCN/jzlhEUNhTQt
dGK11gNvyJJpEeYLe0FuA4kN8Vnt9Wj4iWO0Zp4dCkmf2X2BoUn2sDtaRHIf7mcG
Q4yo36ctxXnsTyG11R3hniYckwVckiRzWbS4ih8CgYEA1OwdOpL0Mi4xfaMP8OHM
MGmKkYap4IXEjichOrKuaie2dm3ciHIviINUb2+4cY+Y6QwtWxVLGN0mWEbsPVUI
yGDh6fSLNQJA/Hfq34TPigjb10f71gGXWH9UN4/hAbFX44aONUa1F5PNZ+p1nSVt
cQ5E+pc1V8pcUjWQFWwmDFk=
-----END PRIVATE KEY-----
)PEM";

// Server-side context carrying the test cert/key: the loopback PEER authenticates with it.
inline boost::asio::ssl::context makeTlsServerContext()
{
    boost::asio::ssl::context ctx(boost::asio::ssl::context::tlsv12);
    ctx.use_certificate_chain(boost::asio::buffer(kTestCertPem.data(), kTestCertPem.size()));
    ctx.use_private_key(boost::asio::buffer(kTestKeyPem.data(), kTestKeyPem.size()),
        boost::asio::ssl::context::pem);
    return ctx;
}

// Client-side context: no cert, no verification.
inline boost::asio::ssl::context makeTlsClientContext()
{
    return boost::asio::ssl::context(boost::asio::ssl::context::tlsv12);
}

// The loopback peer's TLS stream: read/verify wire frames from it (boost::asio::read and
// read_some work on it like on a plain socket).
using PeerSslStream = boost::asio::ssl::stream<boost::asio::ip::tcp::socket>;

// Wrap a CONNECTED tcp socket into a production Socket and drive the client side of the TLS
// handshake to completion. The peer (server) side must handshake concurrently — typically a
// sync peer.handshake(ssl::stream_base::server) on the test's peer thread. The socket's
// io_context must be running (it drives the async handshake). clientCtx must outlive the
// returned socket (the ssl::stream references it). Throws std::runtime_error on handshake
// failure.
inline std::shared_ptr<Socket> makeTlsSessionSocket(
    std::shared_ptr<boost::asio::io_context> io, boost::asio::ssl::context& clientCtx,
    boost::asio::ip::tcp::socket clientSocket)
{
    auto socket = std::make_shared<Socket>(std::move(io), &clientCtx, NodeIPEndpoint());
    socket->ref() = std::move(clientSocket);
    std::promise<boost::system::error_code> handshakeDone;
    auto future = handshakeDone.get_future();
    socket->sslref().async_handshake(boost::asio::ssl::stream_base::client,
        [&handshakeDone](const boost::system::error_code& ec) { handshakeDone.set_value(ec); });
    auto ec = future.get();
    if (ec)
    {
        throw std::runtime_error("makeTlsSessionSocket: TLS handshake failed: " + ec.message());
    }
    return socket;
}
}  // namespace bcos::gateway::testutil
