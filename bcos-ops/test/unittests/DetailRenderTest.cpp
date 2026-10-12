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
 * @file DetailRenderTest.cpp
 */
#include "bcos-ops/tui/Model.h"
#include "bcos-ops/tui/Nav.h"
#include "bcos-ops/tui/Refresher.h"
#include "bcos-ops/tui/views/Pages.h"
#include "bcos-ops/tx/Abi.h"
#include "bcos-ops/tx/Presets.h"
#include "fixtures/Blocks.h"
#include "fixtures/FakeRpc.h"
#include "fixtures/Render.h"
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>

namespace bcos::ops::test
{
namespace
{
constexpr int64_t c_detailNow = 1760000000000 + 128 * 1000 + 2000;

void detailFill(FakeRpc& _rpc, tui::Model& _model, std::vector<tui::DetailWant> _wants = {})
{
    Connection connection;
    connection.call = _rpc.call();
    connection.source = "rpc";
    connection.group = "group0";
    tui::Refresher refresher(
        connection, LocalFallbacks{15000, 3000}, Thresholds{}, _model, nullptr, 100000);
    refresher.setRowCapacity([]() { return size_t(24); });
    refresher.setDetailWants([_wants]() { return _wants; });
    refresher.runOnce();
}

/// Error(string) output of a revert: selector + ABI string
std::string errorOutput(std::string const& _reason)
{
    auto data = Abi(false).encodeMethodBySignature("Error(string)", "[\"" + _reason + "\"]");
    return "0x" + toHex(data);
}

Json::Value fakeTransaction(std::string const& _hash, std::string const& _to)
{
    Json::Value tx;
    tx["hash"] = _hash;
    tx["from"] = "0x3f12000000000000000000000000000000009a00";
    tx["to"] = _to;
    tx["nonce"] = "0x313233";  // the node hex-encodes the decimal digits: "123"
    tx["blockLimit"] = Json::Int64(628);
    tx["input"] = "0x4ed3885e0000000000000000000000000000000000000000000000000000000000000020";
    tx["abi"] = "";
    return tx;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(DetailRenderTest)

BOOST_AUTO_TEST_CASE(blockDetailShowsHeaderFieldsAndItsTransactions)
{
    FakeRpc rpc;
    scriptChain(rpc, 128, 2);
    auto block = fakeBlock(127, 2);
    block["gasUsed"] = "42000";
    block["txsRoot"] = fakeHash(9001);
    block["receiptsRoot"] = fakeHash(9002);
    block["stateRoot"] = fakeHash(9003);
    block["signatureList"] = Json::Value(Json::arrayValue);
    block["signatureList"].append(Json::Value(Json::objectValue));
    block["signatureList"].append(Json::Value(Json::objectValue));
    block["signatureList"].append(Json::Value(Json::objectValue));
    block.removeMember("parentHash");  // the node's shape
    block["parentInfo"] = Json::Value(Json::arrayValue);
    block["parentInfo"].append(Json::Value(Json::objectValue));
    block["parentInfo"][0]["blockNumber"] = 126;
    block["parentInfo"][0]["blockHash"] = fakeHash(126);
    rpc.setFor("getBlockByNumber", 2, "127", block);
    tui::Model model;
    detailFill(rpc, model, {{true, 127, ""}});
    BOOST_REQUIRE(model.explorer.blockDetails.contains(127));
    tui::Nav nav;
    nav.stack.push_back({.kind = tui::DetailKind::block, .number = 1});  // second row = 127
    tui::Frame frame{80, 24, c_detailNow, nullptr};
    auto text = renderText(tui::renderFrame(model, nav, frame, ""), 80, 24);
    BOOST_CHECK(nav.stack[0].resolved);
    BOOST_CHECK_EQUAL(nav.stack[0].number, 127);
    BOOST_CHECK(text.find(" block 127 ") != std::string::npos);
    BOOST_CHECK(text.find(fakeHash(127)) != std::string::npos);
    BOOST_CHECK(text.find("parentHash") != std::string::npos);
    BOOST_CHECK(text.find(fakeHash(126)) != std::string::npos);
    BOOST_CHECK(text.find("gasUsed     42000") != std::string::npos);
    BOOST_CHECK(text.find("signatures  3") != std::string::npos);
    BOOST_CHECK(text.find("sealer      3") != std::string::npos);
    BOOST_CHECK(text.find("transactions") != std::string::npos);
    BOOST_CHECK(text.find("create") != std::string::npos);  // the second tx has no `to`
    BOOST_CHECK_EQUAL(nav.detailTxs.count, 2U);
    // the home list moves on while the detail stays on block 127
    model.explorer.blocks.push_front(tui::BlockRow{129, fakeHash(129), 0, 0, "1", false});
    auto later = renderText(tui::renderFrame(model, nav, frame, ""), 80, 24);
    BOOST_CHECK(later.find(" block 127 ") != std::string::npos);
    // wide terminal: both columns on one row
    tui::Frame wide{120, 40, c_detailNow, nullptr};
    auto wideText = renderText(tui::renderFrame(model, nav, wide, ""), 120, 40);
    BOOST_CHECK(lineWith(wideText, "number").find("gasUsed") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(txOpenedFromABlockResolvesThroughTheCachedBlock)
{
    FakeRpc rpc;
    scriptChain(rpc, 128, 2);
    auto hash = fakeHash(127 * 100 + 1, 't');
    rpc.setFor("getTransaction", 2, hash, fakeTransaction(hash, "0xf505"));
    rpc.setFor("getTransactionReceipt", 2, hash, fakeReceipt(hash, 0, 127));
    tui::Model model;
    detailFill(rpc, model, {{true, 127, ""}, {false, -1, hash}});
    tui::Nav nav;
    nav.stack.push_back({.kind = tui::DetailKind::block, .number = 1});
    nav.stack.push_back({.kind = tui::DetailKind::tx, .number = 1});  // second tx of block 127
    tui::Frame frame{80, 24, c_detailNow, nullptr};
    auto text = renderText(tui::renderFrame(model, nav, frame, ""), 80, 24);
    BOOST_REQUIRE(nav.stack[1].resolved);
    BOOST_CHECK_EQUAL(nav.stack[1].hash, hash);
    BOOST_CHECK_EQUAL(nav.stack[1].number, 127);
    BOOST_CHECK(text.find("hash  " + hash) != std::string::npos);
    BOOST_CHECK(text.find("status      0 ok") != std::string::npos);
    BOOST_CHECK(text.find("blockLimit  628") != std::string::npos);
    BOOST_CHECK(text.find("nonce       123") != std::string::npos);
    BOOST_CHECK(text.find("input       0x4ed3885e") != std::string::npos);
    BOOST_CHECK(text.find("(36 bytes)") != std::string::npos);
    BOOST_CHECK(text.find("logs        0") != std::string::npos);
    auto wants = tui::detailWants(nav);
    BOOST_REQUIRE_EQUAL(wants.size(), 2U);
    BOOST_CHECK(wants[0].block);
    BOOST_CHECK_EQUAL(wants[0].number, 127);
    BOOST_CHECK(!wants[1].block);
    BOOST_CHECK_EQUAL(wants[1].hash, hash);
}

BOOST_AUTO_TEST_CASE(failedTxShowsTheDecodedRevertReasonInRed)
{
    FakeRpc rpc;
    scriptChain(rpc, 128, 1);
    auto hash = fakeHash(128 * 100, 't');
    rpc.setFor("getTransaction", 2, hash, fakeTransaction(hash, ""));
    auto receipt = fakeReceipt(hash, 16, 128);
    receipt["output"] = errorOutput("boom");
    receipt["message"] = "revert";
    rpc.setFor("getTransactionReceipt", 2, hash, receipt);
    tui::Model model;
    detailFill(rpc, model, {{false, -1, hash}});
    tui::Nav nav;
    nav.focus = 1;
    nav.stack.push_back({.kind = tui::DetailKind::tx, .number = 0});  // newest tx row
    tui::Frame frame{80, 24, c_detailNow, nullptr};
    auto raw = renderRaw(tui::renderFrame(model, nav, frame, ""), 80, 24);
    auto text = stripAnsi(raw);
    BOOST_CHECK(text.find("status      16 FAIL") != std::string::npos);
    BOOST_CHECK(text.find("revert      boom") != std::string::npos);
    BOOST_CHECK(text.find("message     revert") != std::string::npos);
    BOOST_CHECK(text.find("to          create") != std::string::npos);
    // red = ANSI 31 somewhere on the message line
    auto messageLine = lineWith(raw, "message");
    BOOST_CHECK(messageLine.find("\x1b[31m") != std::string::npos);
    BOOST_CHECK_EQUAL(tui::detailRevertReason(errorOutput("boom")), "boom");
    BOOST_CHECK_EQUAL(tui::detailRevertReason("0x"), "");
    BOOST_CHECK_EQUAL(tui::detailRevertReason("0x12345678"), "");
}

BOOST_AUTO_TEST_CASE(pendingAndUnloadedStatesAreShown)
{
    FakeRpc rpc;
    scriptChain(rpc, 128, 1);
    tui::Model model;
    detailFill(rpc, model);
    tui::Nav nav;
    nav.stack.push_back({.kind = tui::DetailKind::tx, .hash = "0xabc", .resolved = true});
    tui::Frame frame{80, 24, c_detailNow, nullptr};
    auto text = renderText(tui::renderFrame(model, nav, frame, ""), 80, 24);
    BOOST_CHECK(text.find("loading transaction") != std::string::npos);
    model.explorer.txDetails["0xabc"] = tui::TxDetail{Json::Value(), Json::Value(), true};
    text = renderText(tui::renderFrame(model, nav, frame, ""), 80, 24);
    BOOST_CHECK(text.find("status      pending") != std::string::npos);
    // a sent tx decodes its input through the preset it was sent with
    auto const* hello = findPreset("hello", "set");
    BOOST_REQUIRE(hello != nullptr);
    auto input = Abi(false).encodeMethodBySignature(std::string(hello->signature), "[\"hi\"]");
    Json::Value tx = fakeTransaction("0xabc", "0xf505");
    tx["input"] = "0x" + toHex(input);
    model.explorer.txDetails["0xabc"] = tui::TxDetail{tx, fakeReceipt("0xabc", 0, 128), false};
    model.sentPresets["0xabc"] = hello;
    text = renderText(tui::renderFrame(model, nav, frame, ""), 80, 24);
    BOOST_CHECK(text.find("method      set(string)") != std::string::npos);
    BOOST_CHECK(text.find("params      [\"hi\"]") != std::string::npos);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ops::test
