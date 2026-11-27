// Copyright (C) 2026 FISCO BCOS.
// SPDX-License-Identifier: Apache-2.0

#include "bcos-gateway/libnetwork/ASIOInterface.h"
#include "bcos-gateway/libnetwork/Socket.h"
#include <boost/asio/read.hpp>
#include <boost/test/unit_test.hpp>
#include <array>
#include <chrono>
#include <optional>

using namespace bcos::gateway;

BOOST_AUTO_TEST_SUITE(ASIOWriteTest)

BOOST_AUTO_TEST_CASE(ScatterWriteDeliversBothBuffers)
{
    for (auto transport : {ASIOInterface::TCP_ONLY, ASIOInterface::SSL})
    {
        BOOST_TEST_CONTEXT("transport " << transport)
        {
            auto io = std::make_shared<ba::io_context>();
            ba::ssl::context tls(ba::ssl::context::tlsv12);
            if (transport == ASIOInterface::SSL)
            {
                tls.use_certificate_chain_file("data/ca/node.crt");
                tls.use_private_key_file("data/ca/node.key", ba::ssl::context::pem);
            }
            auto sender = std::make_shared<Socket>(io, tls, NodeIPEndpoint{});
            ba::ssl::stream<bi::tcp::socket> receiver(*io, tls);
            bi::tcp::acceptor acceptor(*io, {bi::address_v4::loopback(), 0});
            sender->ref().connect(acceptor.local_endpoint());
            acceptor.accept(receiver.next_layer());
            if (transport == ASIOInterface::SSL)
            {
                std::optional<boost::system::error_code> client, server;
                sender->sslref().async_handshake(
                    ba::ssl::stream_base::client, [&](auto ec) { client = ec; });
                receiver.async_handshake(
                    ba::ssl::stream_base::server, [&](auto ec) { server = ec; });
                io->run_for(std::chrono::seconds(5));
                BOOST_REQUIRE(client && !*client && server && !*server);
                io->restart();
            }

            ASIOInterface asio;
            asio.setType(transport);
            const std::string header = "abc", body = "defg";
            std::vector<ba::const_buffer> buffers{ba::buffer(header), ba::buffer(body)};
            std::array<char, 7> received{};
            size_t reads = 0, writes = 0;
            auto onRead = [&](auto ec, size_t n) {
                BOOST_CHECK(!ec);
                BOOST_CHECK_EQUAL(n, received.size());
                ++reads;
            };
            if (transport == ASIOInterface::SSL)
            {
                ba::async_read(receiver, ba::buffer(received), onRead);
            }
            else
            {
                ba::async_read(receiver.next_layer(), ba::buffer(received), onRead);
            }
            asio.asyncWrite(sender, buffers, [&](auto ec, size_t n) {
                BOOST_CHECK(!ec);
                BOOST_CHECK_EQUAL(n, header.size() + body.size());
                ++writes;
            });
            BOOST_CHECK_EQUAL(writes, 0);
            io->run_for(std::chrono::seconds(5));
            BOOST_CHECK_EQUAL(writes, 1);
            BOOST_CHECK_EQUAL(reads, 1);
            BOOST_CHECK_EQUAL(std::string(received.begin(), received.end()), header + body);
        }
    }
}

BOOST_AUTO_TEST_CASE(WriteReportsTransportErrorsOnceOnSocketExecutor)
{
    struct FailingSocket : Socket
    {
        using Socket::Socket;
        bi::tcp::socket& ref() override
        {
            throw boost::system::system_error(ba::error::bad_descriptor);
        }
    };
    auto io = std::make_shared<ba::io_context>();
    ba::ssl::context tls(ba::ssl::context::tlsv12);
    auto socket = std::make_shared<FailingSocket>(io, tls, NodeIPEndpoint{});
    ASIOInterface asio;
    const std::string msg = "abc";
    std::vector<ba::const_buffer> buffers{ba::buffer(msg)};
    auto checkWrite = [&](int transport, boost::system::error_code expected) {
        size_t calls = 0;
        asio.setType(transport);
        asio.asyncWrite(socket, buffers, [&](auto ec, size_t n) {
            BOOST_CHECK(ec == expected);
            BOOST_CHECK_EQUAL(n, 0);
            ++calls;
        });
        BOOST_CHECK_EQUAL(calls, 0);
        io->restart();
        io->run();
        BOOST_CHECK_EQUAL(calls, 1);
    };
    checkWrite(ASIOInterface::TCP_ONLY, ba::error::not_connected);
    socket->Socket::ref().open(bi::tcp::v4());
    checkWrite(-1, ba::error::operation_not_supported);
    checkWrite(ASIOInterface::TCP_ONLY, ba::error::bad_descriptor);
}

BOOST_AUTO_TEST_SUITE_END()
