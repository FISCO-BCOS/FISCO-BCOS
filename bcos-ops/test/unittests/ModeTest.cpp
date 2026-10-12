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
 * @brief EL modes (spec §5.2.4): the node dir's [ethereum] / [web3_rpc], PBFT checks skipped,
 *        page 2 as the txpool with its three titles, the EL status band, the raw-only send
 *        page, raw transactions through eth_sendRawTransaction with receipt polling, the web3
 *        HTTP call, and the explorer dropping rows when a new block's parent does not match
 * @file ModeTest.cpp
 */
#include "bcos-ops/Mode.h"
#include "bcos-ops/Checks.h"
#include "bcos-ops/Connect.h"
#include "bcos-ops/HttpRpc.h"
#include "bcos-ops/NodeDir.h"
#include "bcos-ops/OpsError.h"
#include "bcos-ops/cmd/TxCmd.h"
#include "bcos-ops/collect/RpcCollector.h"
#include "bcos-ops/tui/Explorer.h"
#include "bcos-ops/tui/Refresher.h"
#include "bcos-ops/tui/SendForm.h"
#include "bcos-ops/tui/Sender.h"
#include "bcos-ops/tui/Txpool.h"
#include "bcos-ops/tui/views/Pages.h"
#include "bcos-ops/tx/TxSender.h"
#include "fixtures/Blocks.h"
#include "fixtures/FakeRpc.h"
#include "fixtures/Render.h"
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/write.hpp>
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

namespace bcos::ops::test
{
namespace
{
Connection modeConnection(FakeRpc& _rpc, std::string _mode)
{
    Connection connection;
    connection.call = _rpc.call();
    connection.source = "rpc";
    connection.group = "group0";
    connection.mode = std::move(_mode);
    return connection;
}

/// a web3 stand-in on 127.0.0.1: answers each POST with _respond(method), one connection per
/// request, until _requests have been served
struct ScriptedWeb3
{
    boost::asio::io_context ioc;
    boost::asio::ip::tcp::acceptor acceptor{ioc, {boost::asio::ip::make_address("127.0.0.1"), 0}};
    std::thread thread;
    std::string endpoint() const
    {
        return "127.0.0.1:" + std::to_string(acceptor.local_endpoint().port());
    }
    void serve(int _requests, std::function<std::string(std::string const&)> _respond)
    {
        thread = std::thread([this, _requests, respond = std::move(_respond)]() {
            namespace asio = boost::asio;
            for (int i = 0; i < _requests; ++i)
            {
                asio::ip::tcp::socket socket(ioc);
                boost::system::error_code ec;
                acceptor.accept(socket, ec);
                if (ec || stopping)
                {
                    return;
                }
                asio::streambuf buffer;
                asio::read_until(socket, buffer, "\r\n\r\n", ec);
                std::string head(
                    asio::buffers_begin(buffer.data()), asio::buffers_end(buffer.data()));
                auto lengthPos = head.find("Content-Length: ");
                size_t length =
                    lengthPos == std::string::npos ? 0 : std::stoul(head.substr(lengthPos + 16));
                auto bodyStart = head.find("\r\n\r\n") + 4;
                while (!ec && head.size() - bodyStart < length)
                {
                    char chunk[256];
                    auto n = socket.read_some(asio::buffer(chunk), ec);
                    head.append(chunk, n);
                }
                auto request = head.substr(bodyStart);
                auto methodPos = request.find(R"("method":")");
                auto method = methodPos == std::string::npos ?
                                  std::string() :
                                  request.substr(methodPos + 10,
                                      request.find('"', methodPos + 10) - methodPos - 10);
                auto body = respond(method);
                std::string response =
                    "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                    std::to_string(body.size()) + "\r\n\r\n" + body;
                asio::write(socket, asio::buffer(response), ec);
            }
        });
    }
    std::atomic<bool> stopping{false};
    ~ScriptedWeb3()
    {
        // a blocking accept is not woken by close() on every platform: connect once instead
        stopping = true;
        if (thread.joinable())
        {
            boost::asio::io_context wake;
            boost::asio::ip::tcp::socket poke(wake);
            boost::system::error_code ignored;
            poke.connect(acceptor.local_endpoint(), ignored);
            thread.join();
        }
        boost::system::error_code ignored;
        acceptor.close(ignored);
    }
};

Connection elRawConnection(FakeRpc& _rpc, std::string const& _web3)
{
    auto connection = modeConnection(_rpc, "el");
    connection.web3Endpoint = _web3;
    connection.requestTimeoutMs = 3000;
    return connection;
}

void modeFill(Connection const& _connection, tui::Model& _model)
{
    tui::Refresher refresher(
        _connection, LocalFallbacks{15000, 3000}, Thresholds{}, _model, nullptr, 100000);
    refresher.runOnce();
}

/// table-driven web3: eth_sendRawTransaction answers a hash; eth_getTransactionReceipt answers
/// null _pendingPolls times, then the receipt
RpcCall fakeWeb3(int _pendingPolls, std::vector<std::string>& _calls, Json::Value _receipt)
{
    auto polls = std::make_shared<int>(0);
    return [&_calls, _pendingPolls, polls, _receipt](
               std::string_view _method, Json::Value const& _params) -> Json::Value {
        _calls.push_back(std::string(_method) + " " + _params[0].asString());
        if (_method == "eth_sendRawTransaction")
        {
            return "0xfeed";
        }
        if (_method == "eth_getTransactionReceipt")
        {
            return (*polls)++ < _pendingPolls ? Json::Value(Json::nullValue) : _receipt;
        }
        throw OpsError(c_exitUsage, "unexpected " + std::string(_method));
    };
}

Json::Value ethReceipt(std::string const& _status)
{
    Json::Value receipt;
    receipt["transactionHash"] = "0xfeed";
    receipt["status"] = _status;
    receipt["blockNumber"] = "0x1a";
    receipt["gasUsed"] = "0x5208";
    receipt["contractAddress"] = Json::nullValue;
    return receipt;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(ModeTest)

BOOST_AUTO_TEST_CASE(nodeDirReadsEthereumModeAndWeb3Listener)
{
    auto dir = std::filesystem::temp_directory_path() / "ops-mode-nodedir";
    std::filesystem::create_directories(dir);
    std::ofstream(dir / "config.ini")
        << "[rpc]\nlisten_port=20200\n[ethereum]\nmode=opstack-el\n"
           "[web3_rpc]\nenable=true\nlisten_ip=0.0.0.0\nlisten_port=8755\n";
    auto node = NodeDir::load(dir.string());
    BOOST_CHECK_EQUAL(node.ethereumMode, "opstack-el");
    BOOST_CHECK(node.web3Enable);
    BOOST_CHECK_EQUAL(node.web3Endpoint(), "127.0.0.1:8755");
    std::ofstream(dir / "config.ini") << "[rpc]\nlisten_port=20200\n";
    auto plain = NodeDir::load(dir.string());
    BOOST_CHECK_EQUAL(plain.ethereumMode, "none");
    BOOST_CHECK(!plain.web3Enable);
    BOOST_CHECK_EQUAL(plain.web3Endpoint(), "127.0.0.1:8545");
    BOOST_CHECK(isElMode("el") && isElMode("opstack-el") && !isElMode("none"));
    BOOST_CHECK_THROW(checkedMode("pbft"), OpsError);
}

BOOST_AUTO_TEST_CASE(elModesSkipThePbftChecks)
{
    NodeStatus status;
    status.inTimeout = true;  // would fail consensus_stable in a FISCO chain
    status.connectedGroupNodes = 1;
    status.minRequiredQuorum = 3;
    status.mode = "opstack-el";
    auto checks = evaluate(status, Thresholds{}, 0);
    for (auto const& name : {"consensus_stable", "quorum_connected"})
    {
        auto it = std::find_if(
            checks.begin(), checks.end(), [&](Check const& c) { return c.name == name; });
        BOOST_REQUIRE(it != checks.end());
        BOOST_CHECK_EQUAL(it->state, "skipped");
        BOOST_CHECK_EQUAL(it->detail, "el mode");
    }
    BOOST_CHECK(!anyFailed(checks));
    status.mode = "none";
    BOOST_CHECK(anyFailed(evaluate(status, Thresholds{}, 0)));
    BOOST_CHECK_EQUAL(status.toJson()["identity"]["mode"].asString(), "none");
    // getConsensusStatus is never sent in an EL mode: there is no PBFT, and on a 3.18 el-mode
    // node the call stops the node (observed in ticket 17's live run)
    FakeRpc rpc;
    auto collected = collectFromRpc(rpc.call(), "group0", "rpc", LocalFallbacks{}, "el");
    BOOST_CHECK_EQUAL(collected.mode, "el");
    BOOST_CHECK(collected.reasons.at("view").find("el mode") != std::string::npos);
    tui::Model model;
    modeFill(modeConnection(rpc, "el"), model);
    BOOST_CHECK(std::none_of(rpc.calls().begin(), rpc.calls().end(),
        [](auto const& _call) { return _call.first == "getConsensusStatus"; }));
    BOOST_CHECK(model.consensus.isNull());
}

BOOST_AUTO_TEST_CASE(pageTwoIsTheTxpoolWithTheReasonInTheTitle)
{
    FakeRpc rpc;
    scriptChain(rpc, 20, 0);
    rpc.set("getPendingTxSize", 3);
    tui::Model model;
    modeFill(modeConnection(rpc, "el"), model);
    BOOST_CHECK_EQUAL(model.mode, "el");
    tui::Nav nav;
    nav.page = tui::Page::consensus;
    tui::Frame frame{.width = 100, .height = 24, .nowMs = 0};
    auto text = renderText(tui::renderFrame(model, nav, frame, ""), 100, 24);
    BOOST_CHECK(text.find("2 txpool") != std::string::npos);
    BOOST_CHECK(text.find("2 consensus") == std::string::npos);
    BOOST_CHECK(text.find("txpool content not exposed (mode=el)") != std::string::npos);
    BOOST_CHECK(text.find("pending 3") != std::string::npos);
    BOOST_CHECK(text.find("mode el") != std::string::npos);  // top bar
    // opstack-el without the web3 listener
    tui::Model op;
    modeFill(modeConnection(rpc, "opstack-el"), op);
    auto opText = renderText(tui::renderFrame(op, nav, frame, ""), 100, 24);
    BOOST_CHECK(opText.find("(web3_rpc disabled)") != std::string::npos);
    // the band: peers / syncing / highest / lag instead of view / leader / quorum
    tui::Nav home;
    auto band = renderText(tui::renderFrame(op, home, frame, ""), 100, 24);
    BOOST_CHECK(band.find("peers") != std::string::npos);
    BOOST_CHECK(band.find("highest") != std::string::npos);
    BOOST_CHECK(band.find("leader") == std::string::npos);
    BOOST_CHECK(band.find("quorum") == std::string::npos);
    // skipped checks are not counted as ok
    op.checks = {Check{.name = "height_advancing", .state = "ok"},
        Check{.name = "consensus_stable", .state = "skipped", .detail = "el mode"}};
    auto counted = renderText(tui::renderFrame(op, home, frame, ""), 100, 24);
    BOOST_CHECK(counted.find("checks 1 ok (1 skip)") != std::string::npos);
    // a FISCO chain keeps the consensus page
    tui::Model fisco;
    modeFill(modeConnection(rpc, "none"), fisco);
    auto fiscoText = renderText(tui::renderFrame(fisco, nav, frame, ""), 100, 24);
    BOOST_CHECK(fiscoText.find("2 consensus") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(txpoolContentFlattensPendingThenQueued)
{
    Json::Value content;
    auto tx = [](std::string const& _hash, std::string const& _from) {
        Json::Value value;
        value["hash"] = _hash;
        value["from"] = _from;
        value["to"] = "0x43c1";
        value["value"] = "0x0";
        return value;
    };
    content["pending"]["0xbb"]["3"] = tx("0x03", "0xbb");
    content["pending"]["0xaa"]["10"] = tx("0x0a", "0xaa");
    content["pending"]["0xaa"]["2"] = tx("0x02", "0xaa");
    content["queued"]["0xaa"]["12"] = tx("0x0c", "0xaa");
    auto rows = tui::flattenTxpoolContent(content);
    BOOST_REQUIRE_EQUAL(rows.size(), 4U);
    BOOST_CHECK_EQUAL(rows[0].hash, "0x02");  // pending, sender 0xaa, nonce 2 before 10
    BOOST_CHECK_EQUAL(rows[1].hash, "0x0a");
    BOOST_CHECK_EQUAL(rows[1].nonce, 10);
    BOOST_CHECK_EQUAL(rows[2].hash, "0x03");
    BOOST_CHECK(!rows[3].pending);
    BOOST_CHECK_EQUAL(rows[3].nonce, 12);
    // through txpoolFetch with a web3 call
    RpcCall web3 = [content](std::string_view _method, Json::Value const&) -> Json::Value {
        BOOST_CHECK_EQUAL(std::string(_method), "txpool_content");
        return content;
    };
    auto state = tui::txpoolFetch("opstack-el", &web3);
    BOOST_CHECK(state.content);
    BOOST_CHECK_EQUAL(state.rows.size(), 4U);
    tui::Model model;
    model.mode = "opstack-el";
    model.txpool = state;
    auto text =
        renderText(tui::renderTxpool(model, tui::Frame{.width = 100, .height = 20}), 100, 20);
    BOOST_CHECK(text.find("pending 3") != std::string::npos);
    BOOST_CHECK(text.find("queued 1") != std::string::npos);
    // a failing call keeps the page and shows the error
    RpcCall broken = [](std::string_view, Json::Value const&) -> Json::Value {
        throw OpsRpcError("txpool_content", -32601, "not on the OP lane");
    };
    auto failed = tui::txpoolFetch("opstack-el", &broken);
    BOOST_CHECK(!failed.content);
    BOOST_CHECK(failed.error.find("txpool_content") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(rawThroughWeb3PollsForTheReceipt)
{
    std::vector<std::string> calls;
    auto web3 = fakeWeb3(2, calls, ethReceipt("0x1"));
    auto receipt = sendRawEth(web3, "0xF8ab\n", 5000, 1);
    BOOST_CHECK(receipt.ok());
    BOOST_CHECK_EQUAL(receipt.txHash, "0xfeed");
    BOOST_CHECK_EQUAL(receipt.blockNumber, 26);
    BOOST_CHECK_EQUAL(receipt.gasUsed, "21000");
    BOOST_REQUIRE_EQUAL(calls.size(), 4U);  // send + 2 null polls + the receipt
    BOOST_CHECK_EQUAL(calls[0], "eth_sendRawTransaction 0xF8ab");
    // a failed tx (status 0x0) and a tx with no receipt in time
    std::vector<std::string> failedCalls;
    BOOST_CHECK(!sendRawEth(fakeWeb3(0, failedCalls, ethReceipt("0x0")), "f8", 5000, 1).ok());
    std::vector<std::string> pendingCalls;
    auto pending = sendRawEth(fakeWeb3(1000, pendingCalls, ethReceipt("0x1")), "f8", 0, 1);
    BOOST_CHECK_EQUAL(pending.status, -1);
    BOOST_CHECK_EQUAL(pending.message, "pending");
    BOOST_CHECK_THROW(sendRawEth(web3, "zz", 0, 1), OpsError);
    // tx raw in an EL mode without a web3 listener says how to fix it
    FakeRpc rpc;
    std::ostringstream out;
    BOOST_CHECK_EXCEPTION(
        runTxOn(modeConnection(rpc, "el"), Args({"raw", "0xf8ab"}, {"json"}), out), OpsError,
        [](OpsError const& e) { return std::string(e.what()).find("web3") != std::string::npos; });
    BOOST_CHECK(rpc.calls().empty());  // nothing went to sendTransaction
}

BOOST_AUTO_TEST_CASE(sendPageInElModesIsRawOnly)
{
    auto form = tui::makeSendForm(true);
    form.rebuild();
    BOOST_REQUIRE_EQUAL(form.entries.size(), 1U);
    BOOST_CHECK(form.entries[0].kind == tui::SendEntryKind::raw);
    form.tree.selected = 0;
    form.rawText = "0xf8ab";
    tui::SendContext context;
    auto request = tui::sendFormRequest(form, context);
    BOOST_CHECK(request.web3Raw);
    BOOST_CHECK(!request.needsKey());  // signed elsewhere
    auto rows = tui::sendConfirmRows(request, context);
    BOOST_CHECK_EQUAL(rows[1].second, "eth_sendRawTransaction (web3)");
    auto fisco = tui::makeSendForm(false);
    fisco.rebuild();
    BOOST_CHECK(fisco.entries.size() > 1);
}

BOOST_AUTO_TEST_CASE(httpCallPostsJsonRpcAndReadsTheResult)
{
    namespace asio = boost::asio;
    asio::io_context ioc;
    asio::ip::tcp::acceptor acceptor(ioc, {asio::ip::make_address("127.0.0.1"), 0});
    auto port = acceptor.local_endpoint().port();
    std::string seenRequest;
    std::thread server([&]() {
        asio::ip::tcp::socket socket(ioc);
        acceptor.accept(socket);
        asio::streambuf buffer;
        asio::read_until(socket, buffer, "\r\n\r\n");
        std::string head(asio::buffers_begin(buffer.data()), asio::buffers_end(buffer.data()));
        auto lengthPos = head.find("Content-Length: ");
        size_t length =
            lengthPos == std::string::npos ? 0 : std::stoul(head.substr(lengthPos + 16));
        auto bodyStart = head.find("\r\n\r\n") + 4;
        while (head.size() - bodyStart < length)
        {
            char chunk[256];
            auto n = socket.read_some(asio::buffer(chunk));
            head.append(chunk, n);
        }
        seenRequest = head.substr(bodyStart);
        std::string body = R"({"jsonrpc":"2.0","id":1,"result":"0x2a"})";
        std::string response =
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
            "Content-Length: " +
            std::to_string(body.size()) + "\r\n\r\n" + body;
        asio::write(socket, asio::buffer(response));
    });
    auto call = makeHttpRpcCall("127.0.0.1:" + std::to_string(port), 3000);
    Json::Value params(Json::arrayValue);
    auto result = call("eth_blockNumber", params);
    server.join();
    BOOST_CHECK_EQUAL(result.asString(), "0x2a");
    BOOST_CHECK(seenRequest.find(R"("method":"eth_blockNumber")") != std::string::npos);
    // nothing listening: a transport error, not a hang
    acceptor.close();
    auto dead = makeHttpRpcCall("127.0.0.1:" + std::to_string(port), 500);
    BOOST_CHECK_THROW(dead("eth_blockNumber", params), OpsError);
}

BOOST_AUTO_TEST_CASE(explorerDropsRowsWhenTheParentDoesNotMatch)
{
    FakeRpc rpc;
    scriptChain(rpc, 10, 1);
    tui::ExplorerState state;
    state.capacity = 20;
    tui::explorerFetch(state, rpc.call(), "group0", 10, nullptr);
    BOOST_REQUIRE(!state.blocks.empty());
    BOOST_CHECK_EQUAL(state.blocks.front().number, 10);
    // block 10 was replaced on the chain: the new 11 points at another 10
    auto newTen = fakeBlock(10, 1);
    newTen["hash"] = fakeHash(1010);
    auto eleven = fakeBlock(11, 1);
    eleven["parentHash"] = fakeHash(1010);
    rpc.setFor("getBlockByNumber", 2, "10", newTen);
    rpc.setFor("getBlockByNumber", 2, "11", eleven);
    tui::explorerFetch(state, rpc.call(), "group0", 11, nullptr);
    BOOST_CHECK_EQUAL(state.reorgs, 1);
    BOOST_CHECK_EQUAL(state.blocks.front().number, 9);  // 10 dropped, 11 not taken
    BOOST_CHECK(std::none_of(state.txs.begin(), state.txs.end(),
        [](tui::TxRow const& t) { return t.blockNumber >= 10; }));
    // the next round refetches from the fork point and lands on the new branch
    tui::explorerFetch(state, rpc.call(), "group0", 11, nullptr);
    BOOST_CHECK_EQUAL(state.blocks.front().number, 11);
    BOOST_CHECK_EQUAL(state.blocks[1].hash, fakeHash(1010));
    BOOST_CHECK_EQUAL(state.reorgs, 1);
    // the node's shape (parentInfo) is read too
    Json::Value block;
    block["parentInfo"][0]["blockHash"] = "0xAB";
    BOOST_CHECK_EQUAL(tui::blockParentHash(block), "0xAB");
}

BOOST_AUTO_TEST_CASE(explorerFindsADeepForkPointInOneRound)
{
    FakeRpc rpc;
    scriptChain(rpc, 10, 1);
    tui::ExplorerState state;
    state.capacity = 20;
    tui::explorerFetch(state, rpc.call(), "group0", 10, nullptr);
    BOOST_REQUIRE_EQUAL(state.blocks.front().number, 10);
    // 8, 9 and 10 were replaced; 7 is still the common ancestor
    for (int64_t n = 8; n <= 10; ++n)
    {
        auto replaced = fakeBlock(n, 1);
        replaced["hash"] = fakeHash(1000 + n);
        rpc.setFor("getBlockByNumber", 2, std::to_string(n), replaced);
    }
    auto eleven = fakeBlock(11, 1);
    eleven["parentHash"] = fakeHash(1010);
    rpc.setFor("getBlockByNumber", 2, "11", eleven);
    tui::explorerFetch(state, rpc.call(), "group0", 11, nullptr);
    BOOST_CHECK_EQUAL(state.reorgs, 1);
    BOOST_CHECK_EQUAL(state.blocks.front().number, 7);
    BOOST_CHECK_EQUAL(state.lastFetchedHeight, 7);
    BOOST_CHECK(std::none_of(state.txs.begin(), state.txs.end(),
        [](tui::TxRow const& t) { return t.blockNumber >= 8; }));
}

BOOST_AUTO_TEST_CASE(explorerNeverForksOnARowWithoutAHash)
{
    FakeRpc rpc;
    scriptChain(rpc, 10, 1);
    tui::ExplorerState state;
    state.capacity = 20;
    tui::explorerFetch(state, rpc.call(), "group0", 10, nullptr);
    state.blocks.front().hash.clear();  // a node that returned no hash for 10
    auto eleven = fakeBlock(11, 1);
    eleven["parentHash"] = fakeHash(4242);
    rpc.setFor("getBlockByNumber", 2, "11", eleven);
    tui::explorerFetch(state, rpc.call(), "group0", 11, nullptr);
    BOOST_CHECK_EQUAL(state.reorgs, 0);
    BOOST_CHECK_EQUAL(state.blocks.front().number, 11);
}


BOOST_AUTO_TEST_CASE(modeFlagWinsThenNodeDirThenUnknown)
{
    ConnectOptions options;
    NodeDir node;
    node.ethereumMode = "opstack-el";
    BOOST_CHECK_EQUAL(connectionMode(options, &node), "opstack-el");
    options.mode = "none";
    BOOST_CHECK_EQUAL(connectionMode(options, &node), "none");  // --mode wins
    BOOST_CHECK_EQUAL(connectionMode(options, nullptr), "none");
    options.mode.reset();
    BOOST_CHECK_EQUAL(connectionMode(options, nullptr), "unknown");  // bare --rpc
    node.ethereumMode = "pbft";
    BOOST_CHECK_EXCEPTION(connectionMode(options, &node), OpsError, [](OpsError const& e) {
        return std::string(e.what()).find("config.ini [ethereum] mode") != std::string::npos;
    });
    options.mode = "pbft";
    BOOST_CHECK_EXCEPTION(connectionMode(options, nullptr), OpsError, [](OpsError const& e) {
        return std::string(e.what()).find("--mode expects") != std::string::npos;
    });
}

BOOST_AUTO_TEST_CASE(unknownModeNeverSendsGetConsensusStatus)
{
    FakeRpc rpc;
    auto status = collectFromRpc(rpc.call(), "group0", "rpc", {}, "unknown");
    for (auto const& call : rpc.calls())
    {
        BOOST_CHECK_NE(call.first, "getConsensusStatus");
    }
    auto checks = evaluate(status, Thresholds{}, 0);
    for (auto const& check : checks)
    {
        if (check.name == "consensus_stable" || check.name == "quorum_connected")
        {
            BOOST_CHECK_EQUAL(check.state, "skipped");
            BOOST_CHECK_EQUAL(check.detail, "pass --mode none to read PBFT");
        }
    }
    FakeRpc pbft;
    collectFromRpc(pbft.call(), "group0", "rpc", {}, "none");
    BOOST_CHECK(std::ranges::any_of(
        pbft.calls(), [](auto const& c) { return c.first == "getConsensusStatus"; }));
}

BOOST_AUTO_TEST_CASE(elStallLimitHasAThirtySecondFloor)
{
    NodeStatus status;
    status.mode = "el";
    status.latestTimestamp = 0;
    status.consensusTimeoutMs = 3000;
    status.pendingTxSize = 5;
    auto ageOk = evaluate(status, Thresholds{}, 20000);  // 20 s: one slow L1 block, not a stall
    BOOST_CHECK_EQUAL(ageOk[0].state, "ok");
    auto stalled = evaluate(status, Thresholds{}, 31000);
    BOOST_CHECK_EQUAL(stalled[0].state, "fail");
    status.mode = "none";
    BOOST_CHECK_EQUAL(evaluate(status, Thresholds{}, 7000)[0].state, "fail");  // 2 x 3 s
}

BOOST_AUTO_TEST_CASE(elRawExitCodesForAReceiptAndForPending)
{
    FakeRpc rpc;
    {
        ScriptedWeb3 web3;
        web3.serve(2, [](std::string const& _method) -> std::string {
            if (_method == "eth_sendRawTransaction")
            {
                return R"({"jsonrpc":"2.0","id":1,"result":"0xaa"})";
            }
            return R"({"jsonrpc":"2.0","id":2,"result":{"transactionHash":"0xaa","status":"0x1","blockNumber":"0x5","gasUsed":"0x5208","logs":[]}})";
        });
        std::ostringstream out;
        auto code = runTxOn(elRawConnection(rpc, web3.endpoint()),
            Args({"raw", "0xf8ab", "--wait", "5", "--json"}, {"json"}), out);
        BOOST_CHECK_EQUAL(code, c_exitOk);
    }
    {
        ScriptedWeb3 web3;
        web3.serve(20, [](std::string const& _method) -> std::string {
            if (_method == "eth_sendRawTransaction")
            {
                return R"({"jsonrpc":"2.0","id":1,"result":"0xbb"})";
            }
            return R"({"jsonrpc":"2.0","id":2,"result":null})";
        });
        std::ostringstream out;
        auto code = runTxOn(elRawConnection(rpc, web3.endpoint()),
            Args({"raw", "0xf8ab", "--wait", "1", "--json"}, {"json"}), out);
        BOOST_CHECK_EQUAL(code, c_exitPending);
        auto json = parseJson(out.str(), "pending receipt");
        BOOST_CHECK(json["pending"].asBool());
    }
    std::ostringstream out;
    BOOST_CHECK_THROW(runTxOn(elRawConnection(rpc, "127.0.0.1:1"),
                          Args({"raw", "0xf8ab", "--wait", "0"}, {"json"}), out),
        OpsError);
    BOOST_CHECK_THROW(runTxOn(elRawConnection(rpc, "127.0.0.1:1"),
                          Args({"raw", "0xf8ab", "--wait", "4000"}, {"json"}), out),
        OpsError);
}

BOOST_AUTO_TEST_CASE(httpCallTimesOutWhenTheServerNeverAnswersAndReportsHttpErrors)
{
    namespace asio = boost::asio;
    asio::io_context ioc;
    asio::ip::tcp::acceptor acceptor(ioc, {asio::ip::make_address("127.0.0.1"), 0});
    auto port = acceptor.local_endpoint().port();
    asio::ip::tcp::socket held(ioc);
    std::thread server([&]() { acceptor.accept(held); });  // accepts, never answers
    auto call = makeHttpRpcCall("127.0.0.1:" + std::to_string(port), 300);
    Json::Value params(Json::arrayValue);
    auto started = std::chrono::steady_clock::now();
    BOOST_CHECK_EXCEPTION(call("eth_blockNumber", params), OpsError, [](OpsError const& e) {
        return std::string(e.what()).find("no answer within 300 ms") != std::string::npos;
    });
    BOOST_CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(3));
    server.join();

    ScriptedWeb3 web3;
    web3.thread = std::thread([&web3]() {
        namespace asio = boost::asio;
        asio::ip::tcp::socket socket(web3.ioc);
        boost::system::error_code ec;
        web3.acceptor.accept(socket, ec);
        asio::streambuf buffer;
        asio::read_until(socket, buffer, "\r\n\r\n", ec);
        std::string body = "payload \x1b[2J too large";
        std::string response =
            "HTTP/1.1 413 Payload Too Large\r\nContent-Length: " + std::to_string(body.size()) +
            "\r\n\r\n" + body;
        asio::write(socket, asio::buffer(response), ec);
    });
    auto refused = makeHttpRpcCall(web3.endpoint(), 3000);
    BOOST_CHECK_EXCEPTION(refused("eth_blockNumber", params), OpsError, [](OpsError const& e) {
        std::string what = e.what();
        return what.find("HTTP 413") != std::string::npos && what.find('\x1b') == std::string::npos;
    });
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ops::test
