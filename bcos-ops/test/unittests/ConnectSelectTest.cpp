/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include <bcos-ops/Connect.h>
#include <bcos-ops/IpcClient.h>
#include <bcos-ops/OpsError.h>
#include <boost/asio/buffers_iterator.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/write.hpp>
#include <boost/test/unit_test.hpp>
#include <filesystem>
#include <fstream>
#include <future>
#include <thread>

namespace bcos::ops::test
{
namespace
{
/// minimal stand-in for the node's IpcServer (async accept + read loop on one io thread):
/// getBlockNumber → 128, getConsensusStatus → a string-wrapped object, anything else → -32601
class FakeIpcNode
{
public:
    explicit FakeIpcNode(std::string _path) : m_path(std::move(_path))
    {
        // everything that touches the acceptor lives on the io thread
        std::promise<void> ready;
        m_thread = std::thread([this, &ready]() {
            boost::asio::local::stream_protocol::acceptor acceptor(
                m_io, boost::asio::local::stream_protocol::endpoint(m_path));
            m_acceptor = &acceptor;
            accept();
            ready.set_value();
            m_io.run();
            m_acceptor = nullptr;
        });
        ready.get_future().get();
    }
    ~FakeIpcNode()
    {
        m_io.stop();
        if (m_thread.joinable())
        {
            m_thread.join();
        }
        std::filesystem::remove(m_path);
    }

private:
    struct Session : std::enable_shared_from_this<Session>
    {
        boost::asio::local::stream_protocol::socket socket;
        boost::asio::streambuf buffer;
        explicit Session(boost::asio::local::stream_protocol::socket _socket)
          : socket(std::move(_socket))
        {}
        void serve()
        {
            auto self = shared_from_this();
            boost::asio::async_read_until(
                socket, buffer, '\n', [self](boost::system::error_code const& ec, size_t n) {
                    if (ec)
                    {
                        return;
                    }
                    auto begin = boost::asio::buffers_begin(self->buffer.data());
                    std::string line(begin, begin + static_cast<std::ptrdiff_t>(n));
                    self->buffer.consume(n);
                    auto request = parseJson(line, "request");
                    Json::Value response;
                    response["jsonrpc"] = "2.0";
                    response["id"] = request["id"];
                    if (request["method"].asString() == "getBlockNumber")
                    {
                        response["result"] = 128;
                    }
                    else if (request["method"].asString() == "getConsensusStatus")
                    {
                        response["result"] = R"({"view":2})";
                    }
                    else
                    {
                        response["error"]["code"] = -32601;
                        response["error"]["message"] = "Method not found";
                    }
                    Json::StreamWriterBuilder writer;
                    writer["indentation"] = "";
                    auto text =
                        std::make_shared<std::string>(Json::writeString(writer, response) + "\n");
                    boost::asio::async_write(self->socket, boost::asio::buffer(*text),
                        [self, text](boost::system::error_code const& wec, size_t) {
                            if (!wec)
                            {
                                self->serve();
                            }
                        });
                });
        }
    };

    void accept()
    {
        m_acceptor->async_accept([this](boost::system::error_code const& ec,
                                     boost::asio::local::stream_protocol::socket socket) {
            if (ec == boost::asio::error::operation_aborted)
            {
                return;
            }
            // a peer that connected and closed before accept (the reachability probe) makes
            // macOS report EINVAL for that connection; keep accepting like IpcServer does
            if (!ec)
            {
                std::make_shared<Session>(std::move(socket))->serve();
            }
            accept();
        });
    }

    std::string m_path;
    boost::asio::io_context m_io;
    boost::asio::local::stream_protocol::acceptor* m_acceptor = nullptr;
    std::thread m_thread;
};

struct TempNode
{
    std::filesystem::path dir;
    TempNode()
    {
        // unix socket paths are short-limited; keep the node dir under /tmp
        dir = std::filesystem::path("/tmp") / ("bcos-ops-sel-" + std::to_string(::getpid()));
        std::filesystem::create_directories(dir / "data");
        std::ofstream ini(dir / "config.ini");
        ini << "[rpc]\n    listen_ip=127.0.0.1\n    listen_port=1\n    enable_ssl=false\n"
               "[storage]\n    data_path=data\n";
        std::ofstream genesis(dir / "config.genesis");
        genesis << "[chain]\n    group_id=group0\n    chain_id=chain0\n";
    }
    ~TempNode() { std::filesystem::remove_all(dir); }
    std::string socketPath() const { return (dir / "data" / "fisco-bcos.ipc").string(); }
};
}  // namespace

BOOST_AUTO_TEST_SUITE(ConnectSelectTest)

BOOST_AUTO_TEST_CASE(socketPresentSelectsAttach)
{
    TempNode node;
    FakeIpcNode fake(node.socketPath());
    ConnectOptions options;
    options.nodeDir = node.dir.string();
    options.connectTimeoutMs = 1000;
    options.requestTimeoutMs = 2000;
    auto connection = connect(options);
    BOOST_CHECK_EQUAL(connection.source, "attach");
    BOOST_CHECK_EQUAL(connection.group, "group0");
    BOOST_REQUIRE(connection.nodeDir);
    BOOST_CHECK_EQUAL(
        connection.call("getBlockNumber", Json::Value(Json::arrayValue)).asInt64(), 128);
    // string-wrapped results are unwrapped on this transport too
    auto status = connection.call("getConsensusStatus", Json::Value(Json::arrayValue));
    BOOST_CHECK_EQUAL(status["view"].asInt(), 2);
    // a JSON-RPC error surfaces as OpsError{1}
    BOOST_CHECK_EXCEPTION(connection.call("nope", Json::Value(Json::arrayValue)), OpsError,
        [](OpsError const& e) { return e.exitCode == c_exitUsage; });
}

BOOST_AUTO_TEST_CASE(socketAbsentFallsBackToRpcAndFails)
{
    TempNode node;  // no socket, rpc port 1 is unreachable
    ConnectOptions options;
    options.nodeDir = node.dir.string();
    options.connectTimeoutMs = 500;
    BOOST_CHECK(!ipcReachable(node.socketPath(), 200));
    BOOST_CHECK_EXCEPTION(connect(options), OpsError, [](OpsError const& e) {
        return e.exitCode == c_exitUsage &&
               std::string(e.what()).find("connect") != std::string::npos;
    });
}

BOOST_AUTO_TEST_CASE(staleSocketFileIsNotReachable)
{
    TempNode node;
    {
        std::ofstream stale(node.socketPath());
        stale << "x";
    }
    BOOST_CHECK(!ipcReachable(node.socketPath(), 200));
    ConnectOptions options;
    options.nodeDir = node.dir.string();
    options.allowRpc = false;
    BOOST_CHECK_EXCEPTION(connect(options), OpsError, [](OpsError const& e) {
        return std::string(e.what()).find("ipc_enable") != std::string::npos;
    });
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ops::test
