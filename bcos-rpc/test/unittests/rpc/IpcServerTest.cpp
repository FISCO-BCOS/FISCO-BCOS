/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include "../common/RPCFixture.h"
#include <bcos-rpc/ipc/IpcServer.h>
#include <boost/asio/buffers_iterator.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/write.hpp>
#include <boost/test/unit_test.hpp>
#include <filesystem>
#include <future>
#include <thread>

using namespace bcos;
using namespace bcos::rpc;

namespace bcos::test
{
namespace
{
std::string shortSocketPath(std::string const& _tag)
{
    // unix socket paths are capped at ~104 bytes on macOS; keep it under /tmp
    return "/tmp/bcos-ipc-" + _tag + "-" + std::to_string(::getpid()) + ".sock";
}

/// a server on its own io thread, stopped and joined in the destructor
struct IpcHarness
{
    boost::asio::io_context io;
    std::unique_ptr<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>> work;
    IpcServer::Ptr server;
    std::thread thread;
    IpcHarness(std::string _path, IpcHandler _handler)
    {
        work = std::make_unique<
            boost::asio::executor_work_guard<boost::asio::io_context::executor_type>>(
            io.get_executor());
        server = std::make_shared<IpcServer>(io, std::move(_path), std::move(_handler));
        server->start();
        thread = std::thread([this]() { io.run(); });
    }
    ~IpcHarness()
    {
        server->stop();
        work.reset();
        io.stop();
        if (thread.joinable())
        {
            thread.join();
        }
    }
};

struct Client
{
    boost::asio::io_context io;
    boost::asio::local::stream_protocol::socket socket;
    boost::asio::streambuf buffer;
    explicit Client(std::string const& _path) : socket(io)
    {
        socket.connect(boost::asio::local::stream_protocol::endpoint(_path));
    }
    void send(std::string const& _line) { boost::asio::write(socket, boost::asio::buffer(_line)); }
    std::string readLine()
    {
        boost::system::error_code ec;
        auto n = boost::asio::read_until(socket, buffer, '\n', ec);
        if (ec)
        {
            return "";
        }
        auto begin = boost::asio::buffers_begin(buffer.data());
        std::string line(begin, begin + static_cast<std::ptrdiff_t>(n));
        buffer.consume(n);
        return line;
    }
};

Json::Value parse(std::string const& _text)
{
    Json::Value value;
    Json::CharReaderBuilder builder;
    std::string errors;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    BOOST_REQUIRE_MESSAGE(reader->parse(_text.data(), _text.data() + _text.size(), &value, &errors),
        "not json: " + _text);
    return value;
}
}  // namespace

BOOST_FIXTURE_TEST_SUITE(IpcServerTest, RPCFixture)

BOOST_AUTO_TEST_CASE(publicMethodAnswersLikeTcp)
{
    auto rpc = factory->buildLocalRpc(groupInfo, nodeService);
    rpc->groupManager()->updateGroupInfo(groupInfo);
    auto jsonRpc = rpc->jsonRpcImpl();
    auto path = shortSocketPath("public");
    IpcHarness harness(path, [jsonRpc](std::string_view _body, auto&& _sender) {
        jsonRpc->onIpcRequest(_body, std::forward<decltype(_sender)>(_sender));
    });

    Client client(path);
    client.send(R"({"jsonrpc":"2.0","id":1,"method":"getBlockNumber","params":[")" + groupId +
                R"(",""]})"
                "\n");
    auto response = parse(client.readLine());
    BOOST_CHECK_EQUAL(response["id"].asInt(), 1);
    BOOST_CHECK(!response.isMember("error") || response["error"].isNull());

    std::promise<int64_t> direct;
    jsonRpc->getBlockNumber(groupId, "",
        [&direct](Error::Ptr, Json::Value& _value) { direct.set_value(_value.asInt64()); });
    BOOST_CHECK_EQUAL(response["result"].asInt64(), direct.get_future().get());
}

BOOST_AUTO_TEST_CASE(twoRequestsOnOneConnection)
{
    auto rpc = factory->buildLocalRpc(groupInfo, nodeService);
    rpc->groupManager()->updateGroupInfo(groupInfo);
    auto jsonRpc = rpc->jsonRpcImpl();
    auto path = shortSocketPath("two");
    IpcHarness harness(path, [jsonRpc](std::string_view _body, auto&& _sender) {
        jsonRpc->onIpcRequest(_body, std::forward<decltype(_sender)>(_sender));
    });
    Client client(path);
    client.send(R"({"jsonrpc":"2.0","id":7,"method":"getBlockNumber","params":[")" + groupId +
                R"(",""]})"
                "\n");
    BOOST_CHECK_EQUAL(parse(client.readLine())["id"].asInt(), 7);
    client.send(R"({"jsonrpc":"2.0","id":8,"method":"getGroupList","params":[]})"
                "\n");
    auto second = parse(client.readLine());
    BOOST_CHECK_EQUAL(second["id"].asInt(), 8);
    BOOST_CHECK(second["result"]["groupList"].isArray());
    // a blank line is ignored, the next real line is still served
    client.send("\n");
    client.send(R"({"jsonrpc":"2.0","id":9,"method":"nope","params":[]})"
                "\n");
    auto third = parse(client.readLine());
    BOOST_CHECK_EQUAL(third["error"]["code"].asInt(), -32601);
}

BOOST_AUTO_TEST_CASE(oversizedLineClosesConnection)
{
    auto path = shortSocketPath("big");
    IpcHarness harness(path, [](std::string_view, auto&& _sender) {
        _sender(bcos::bytes{'{', '}'}, boost::beast::http::status::ok);
    });
    Client client(path);
    std::string huge(c_ipcMaxLineBytes + 16, 'x');
    boost::system::error_code ec;
    boost::asio::write(client.socket, boost::asio::buffer(huge), ec);
    // either the write already failed (peer closed) or the read sees EOF without a response
    BOOST_CHECK(ec || client.readLine().empty());
}

BOOST_AUTO_TEST_CASE(stopRemovesSocketFile)
{
    auto path = shortSocketPath("stop");
    {
        IpcHarness harness(path, [](std::string_view, auto&& _sender) {
            _sender(bcos::bytes{'{', '}'}, boost::beast::http::status::ok);
        });
        BOOST_CHECK(std::filesystem::exists(path));
        harness.server->stop();
        BOOST_CHECK(!std::filesystem::exists(path));
    }
    BOOST_CHECK(!std::filesystem::exists(path));
}

BOOST_AUTO_TEST_CASE(staleFileIsReplacedOnStart)
{
    auto path = shortSocketPath("stale");
    {
        std::ofstream stale(path);
        stale << "garbage";
    }
    IpcHarness harness(path, [](std::string_view, auto&& _sender) {
        _sender(bcos::bytes{'{', '}'}, boost::beast::http::status::ok);
    });
    Client client(path);
    client.send("{}\n");
    BOOST_CHECK_EQUAL(client.readLine(), "{}\n");
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
