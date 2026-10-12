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
 * @file HomeRenderTest.cpp
 */
#include "bcos-ops/Output.h"
#include "bcos-ops/tui/Model.h"
#include "bcos-ops/tui/Nav.h"
#include "bcos-ops/tui/Refresher.h"
#include "bcos-ops/tui/views/Pages.h"
#include "fixtures/Blocks.h"
#include "fixtures/FakeRpc.h"
#include "fixtures/Render.h"
#include <boost/test/unit_test.hpp>
#include <ftxui/screen/screen.hpp>

namespace bcos::ops::test
{
namespace
{
constexpr int64_t c_homeNow = 1760000000000 + 128 * 1000 + 2000;  // block 128 is 2 s old

void homeFill(FakeRpc& _rpc, tui::Model& _model, size_t _capacity = 24)
{
    Connection connection;
    connection.call = _rpc.call();
    connection.source = "rpc";
    connection.group = "group0";
    tui::Refresher refresher(
        connection, LocalFallbacks{15000, 3000}, Thresholds{}, _model, nullptr, 100000);
    refresher.setRowCapacity([_capacity]() { return _capacity; });
    refresher.runOnce();
}

}  // namespace

BOOST_AUTO_TEST_SUITE(HomeRenderTest)

BOOST_AUTO_TEST_CASE(narrowTerminalStacksTheListsAndDropsColumns)
{
    FakeRpc rpc;
    scriptChain(rpc, 128, 2);
    tui::Model model;
    homeFill(rpc, model);
    tui::Nav nav;
    tui::Frame frame{80, 24, c_homeNow, nullptr};
    auto text = renderText(tui::renderFrame(model, nav, frame, ""), 80, 24);
    // top bar
    BOOST_CHECK(text.find("fisco-bcos tui") != std::string::npos);
    BOOST_CHECK(text.find("node 3a1f00") != std::string::npos);
    BOOST_CHECK(text.find("source rpc") != std::string::npos);
    BOOST_CHECK(text.find("mode pbft") != std::string::npos);
    BOOST_CHECK(text.find("key none") != std::string::npos);
    // status band
    BOOST_CHECK(text.find("height 128") != std::string::npos);
    BOOST_CHECK(text.find("leader 2") != std::string::npos);
    BOOST_CHECK(text.find("nodes 4/4") != std::string::npos);
    BOOST_CHECK(text.find("quorum 3") != std::string::npos);
    BOOST_CHECK(text.find("pending 0/15000") != std::string::npos);
    BOOST_CHECK(text.find("checks 5 ok") != std::string::npos);
    BOOST_CHECK(text.find("last block 2s ago") != std::string::npos);
    BOOST_CHECK(text.find("avg interval 1.0s (last 20)") != std::string::npos);
    // stacked: the blocks window ends before the txs window starts
    auto blocksPos = text.find(" blocks ");
    auto txsPos = text.find(" txs ");
    BOOST_REQUIRE(blocksPos != std::string::npos && txsPos != std::string::npos);
    BOOST_CHECK(text.find('\n', blocksPos) < txsPos);
    // block rows: newest first, hash abridged, age; at 78 columns every block column fits
    auto row128 = lineWith(text, "128     ");
    BOOST_CHECK(row128.find(abridged(fakeHash(128), 4)) != std::string::npos);
    BOOST_CHECK(row128.find(" 2 ") != std::string::npos);  // txs
    BOOST_CHECK(row128.find("2s") != std::string::npos);   // age
    BOOST_CHECK(text.find("height  hash        txs  time  age") != std::string::npos);
    // tx rows: hash, from, to, status (* until the receipt arrives), height
    BOOST_CHECK(text.find("hash        from      to        status height") != std::string::npos);
    auto txRow = lineWith(text, abridged(fakeHash(12800, 't'), 4));
    BOOST_CHECK(txRow.find("3f12") != std::string::npos);
    BOOST_CHECK(txRow.find("*") != std::string::npos);
    BOOST_CHECK(txRow.find("128") != std::string::npos);
    // bottom bar: pages, the current one first
    BOOST_CHECK(text.find("1 home") != std::string::npos);
    BOOST_CHECK(text.find("5 node") != std::string::npos);
    BOOST_CHECK(text.find("6 gov") == std::string::npos);  // not an auth chain
    BOOST_CHECK(text.find("? help  q quit") != std::string::npos);
    BOOST_CHECK(text.find("Tab focus") == std::string::npos);  // short hint under 100 columns
    BOOST_CHECK_EQUAL(nav.barRow, 23);
    BOOST_CHECK_EQUAL(nav.barHits.size(), 5U);
    // the renderer told Nav what it drew
    BOOST_CHECK_EQUAL(nav.blocks.count, 24U);
    BOOST_CHECK_EQUAL(nav.txs.count, 24U);
    BOOST_CHECK(nav.blocks.pageRows >= 5 && nav.blocks.pageRows <= 8);
    // 36 columns (34 inside the border): time goes first on the block side, to and from on
    // the tx side; height/hash/txs/age and hash/status/height stay
    tui::Frame narrow{36, 24, c_homeNow, nullptr};
    auto small = renderText(tui::renderFrame(model, nav, narrow, ""), 36, 24);
    BOOST_CHECK(small.find("height  hash        txs  age") != std::string::npos);
    BOOST_CHECK(small.find("time") == std::string::npos);
    BOOST_CHECK(small.find("hash        status height") != std::string::npos);
    BOOST_CHECK(small.find("from") == std::string::npos);
}

BOOST_AUTO_TEST_CASE(wideTerminalPutsTheListsSideBySide)
{
    FakeRpc rpc;
    scriptChain(rpc, 128, 1);
    tui::Model model;
    homeFill(rpc, model, 40);
    tui::Nav nav;
    nav.focus = 1;
    nav.txs.selected = 1;
    tui::Frame frame{120, 40, c_homeNow, nullptr};
    auto raw = renderRaw(tui::renderFrame(model, nav, frame, ""), 120, 40);
    auto text = stripAnsi(raw);
    auto blocksPos = text.find(" blocks ");
    auto txsPos = text.find(" txs ");
    BOOST_REQUIRE(blocksPos != std::string::npos && txsPos != std::string::npos);
    BOOST_CHECK(text.find('\n', blocksPos) > txsPos);  // same screen line
    // both header rows on one line
    auto header = lineWith(text, "height  hash        txs  time  age");
    BOOST_CHECK(header.find("status height") != std::string::npos);
    BOOST_CHECK(text.find("Tab focus  j k select") != std::string::npos);
    BOOST_CHECK_EQUAL(nav.blocks.count, 40U);
    BOOST_CHECK(nav.blocks.pageRows >= 30);
    // the focused list draws its selected row inverted (ANSI 7)
    BOOST_CHECK(raw.find("\x1b[7m") != std::string::npos);
    // 40 rows kept, the page shows the newest ones, no gap marker
    BOOST_CHECK(text.find("98      ") != std::string::npos);
    BOOST_CHECK(text.find("skipped") == std::string::npos);
}

BOOST_AUTO_TEST_CASE(gapAndFailuresAreMarked)
{
    FakeRpc rpc;
    scriptChain(rpc, 128, 1);
    tui::Model model;
    homeFill(rpc, model, 30);
    // a later round that jumps 80 blocks ahead: 50 fetched, the oldest of them marks the gap
    for (int64_t n = 129; n <= 208; ++n)
    {
        rpc.setFor("getBlockByNumber", 2, std::to_string(n), fakeBlock(n, 1));
    }
    rpc.set("getBlockNumber", 208);
    rpc.setConsensus("timeout", true);
    homeFill(rpc, model, 200);
    model.explorer.txs[0].status = 16;
    tui::Nav nav;
    tui::Frame frame{120, 60, c_homeNow + 80000, nullptr};
    auto text = renderText(tui::renderFrame(model, nav, frame, ""), 120, 60);
    BOOST_CHECK(text.find("older rows skipped") != std::string::npos);
    auto row159 = lineWith(text, "159     ");
    BOOST_CHECK(!row159.empty());
    BOOST_CHECK(text.find("FAIL") != std::string::npos);  // the tx status word
    BOOST_CHECK(text.find("checks 1 FAIL") != std::string::npos);
    BOOST_CHECK(text.find("view change in progress") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(detailAndHelpLayersRenderOverThePage)
{
    FakeRpc rpc;
    scriptChain(rpc, 128, 2);
    tui::Model model;
    homeFill(rpc, model);
    tui::Nav nav;
    tui::Frame frame{80, 24, c_homeNow, nullptr};
    renderText(tui::renderFrame(model, nav, frame, ""), 80, 24);  // sizes the lists
    tui::navHandle(nav, ftxui::Event::ArrowDown);
    tui::navHandle(nav, ftxui::Event::Return);
    BOOST_REQUIRE(nav.inDetail());
    auto detail = renderText(tui::renderFrame(model, nav, frame, ""), 80, 24);
    BOOST_CHECK(detail.find(" block 127 ") != std::string::npos);
    BOOST_CHECK(detail.find(fakeHash(127)) != std::string::npos);
    BOOST_CHECK(detail.find("Esc back") != std::string::npos);
    BOOST_CHECK_EQUAL(nav.detailTxs.count, 2U);
    tui::navHandle(nav, ftxui::Event::Escape);
    tui::navHandle(nav, ftxui::Event::Tab);
    tui::navHandle(nav, ftxui::Event::Return);
    auto tx = renderText(tui::renderFrame(model, nav, frame, ""), 80, 24);
    BOOST_CHECK(tx.find(" tx ") != std::string::npos);
    BOOST_CHECK(tx.find(fakeHash(12800, 't')) != std::string::npos);
    tui::navHandle(nav, ftxui::Event::Escape);
    tui::navHandle(nav, ftxui::Event::Character('?'));
    auto help = renderText(tui::renderFrame(model, nav, frame, ""), 80, 24);
    BOOST_CHECK(help.find(" help ") != std::string::npos);
    BOOST_CHECK(help.find("Tab   next focus area") != std::string::npos);
    BOOST_CHECK(help.find("fisco-bcos tui") != std::string::npos);  // the page stays underneath
}

BOOST_AUTO_TEST_CASE(logPageFiltersLinesAndShowsTheFilter)
{
    tui::Model model;
    model.logTail.push_back(*parseLine(
        "info|2026-10-10 05:00:03.000000|x|[CONSENSUS][PBFT]ViewChangeTriggered,reason=restart"));
    model.logTail.push_back(
        *parseLine("info|2026-10-10 05:00:04.000000|x|[TXPOOL]TxsRemoved,number=2"));
    tui::Nav nav;
    nav.page = tui::Page::log;
    tui::Frame frame{80, 24, c_homeNow, nullptr};
    auto all = renderText(tui::renderFrame(model, nav, frame, ""), 80, 24);
    BOOST_CHECK(all.find("ViewChangeTriggered") != std::string::npos);
    BOOST_CHECK(all.find("TxsRemoved") != std::string::npos);
    BOOST_CHECK(all.find("filter>") != std::string::npos);
    auto filtered = renderText(tui::renderFrame(model, nav, frame, "ViewChange"), 80, 24);
    BOOST_CHECK(filtered.find("ViewChangeTriggered") != std::string::npos);
    BOOST_CHECK(filtered.find("TxsRemoved") == std::string::npos);
    BOOST_CHECK(filtered.find("filter> ViewChange") != std::string::npos);
    BOOST_CHECK(filtered.find("4 log") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(errorBannerAndAuthChainBar)
{
    tui::Model model;
    model.error = "connect refused";
    model.authChain = true;
    tui::Nav nav;
    tui::Frame frame{80, 24, c_homeNow, nullptr};
    auto text = renderText(tui::renderFrame(model, nav, frame, ""), 80, 24);
    BOOST_CHECK(text.find("connect refused") != std::string::npos);
    BOOST_CHECK(text.find("6 gov") != std::string::npos);
    BOOST_CHECK(text.find("waiting for the first refresh") != std::string::npos);
    BOOST_CHECK(text.find("checks -") != std::string::npos);
    BOOST_CHECK_EQUAL(nav.barHits.size(), 6U);
    BOOST_CHECK(nav.authChain);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ops::test
