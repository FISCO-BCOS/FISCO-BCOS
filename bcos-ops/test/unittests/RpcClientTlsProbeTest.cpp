/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include <bcos-ops/OpsError.h>
#include <bcos-ops/RpcClient.h>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include <boost/test/unit_test.hpp>
#include <thread>

namespace bcos::ops::test
{
namespace
{
/// one-shot TCP server: accepts, reads whatever arrives, replies _reply, closes
class OneShotServer
{
public:
    explicit OneShotServer(std::string _reply)
      : m_acceptor(
            m_io, boost::asio::ip::tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 0)),
        m_reply(std::move(_reply))
    {
        m_port = m_acceptor.local_endpoint().port();
        m_thread = std::thread([this]() {
            boost::system::error_code ec;
            boost::asio::ip::tcp::socket socket(m_io);
            m_acceptor.accept(socket, ec);
            if (ec)
            {
                return;
            }
            char buffer[256];
            socket.read_some(boost::asio::buffer(buffer), ec);
            boost::asio::write(socket, boost::asio::buffer(m_reply), ec);
            socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
            socket.close(ec);
        });
    }
    ~OneShotServer()
    {
        boost::system::error_code ec;
        m_acceptor.close(ec);
        if (m_thread.joinable())
        {
            m_thread.join();
        }
    }
    uint16_t port() const { return m_port; }

private:
    boost::asio::io_context m_io;
    boost::asio::ip::tcp::acceptor m_acceptor;
    std::string m_reply;
    uint16_t m_port = 0;
    std::thread m_thread;
};

uint16_t unusedPort()
{
    boost::asio::io_context io;
    boost::asio::ip::tcp::acceptor acceptor(
        io, boost::asio::ip::tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 0));
    return acceptor.local_endpoint().port();
}
}  // namespace

BOOST_AUTO_TEST_SUITE(RpcClientTlsProbeTest)

BOOST_AUTO_TEST_CASE(serverHelloIsTls)
{
    // what OpenSSL sends back to a ClientHello: a handshake record carrying ServerHello
    OneShotServer server(std::string("\x16\x03\x03\x00\x31\x02\x00\x00\x2d\x03\x03", 11));
    BOOST_CHECK(probeTls("127.0.0.1", server.port(), 2000));
}

BOOST_AUTO_TEST_CASE(alertIsTls)
{
    OneShotServer server(std::string("\x15\x03\x03\x00\x02\x02\x28", 7));
    BOOST_CHECK(probeTls("127.0.0.1", server.port(), 2000));
}

BOOST_AUTO_TEST_CASE(makeWsRpcCallRefusesTlsPort)
{
    OneShotServer server(std::string("\x16\x03\x03\x00\x31\x02\x00\x00\x2d\x03\x03", 11));
    BOOST_CHECK_EXCEPTION(
        makeWsRpcCall("127.0.0.1", server.port(), 2000), OpsError, [](OpsError const& e) {
            return e.exitCode == c_exitUsage &&
                   std::string(e.what()).find("SSL") != std::string::npos;
        });
}

BOOST_AUTO_TEST_CASE(plaintextHttpIsNotTls)
{
    OneShotServer server("HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n");
    BOOST_CHECK(!probeTls("127.0.0.1", server.port(), 2000));
}

BOOST_AUTO_TEST_CASE(silentCloseIsNotTls)
{
    OneShotServer server("");  // a plaintext server that drops a binary record
    BOOST_CHECK(!probeTls("127.0.0.1", server.port(), 2000));
}

BOOST_AUTO_TEST_CASE(hostnameResolves)
{
    auto [host, port] = resolveHost("localhost", 20200, 2000);
    BOOST_CHECK(host == "127.0.0.1" || host == "::1");
    BOOST_CHECK_EQUAL(port, 20200);
    BOOST_CHECK_EQUAL(resolveHost("10.1.2.3", 1, 2000).first, "10.1.2.3");
    BOOST_CHECK_THROW(resolveHost("no-such-host.invalid", 1, 2000), OpsError);
}

BOOST_AUTO_TEST_CASE(closedPortIsConnectError)
{
    auto port = unusedPort();
    BOOST_CHECK_EXCEPTION(probeTls("127.0.0.1", port, 2000), OpsError, [](OpsError const& e) {
        return e.exitCode == c_exitUsage &&
               std::string(e.what()).find("connect") != std::string::npos;
    });
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ops::test
