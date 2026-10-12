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
 * @file ExplorerTest.cpp
 */
#include "bcos-ops/tui/Explorer.h"
#include "bcos-ops/tui/Model.h"
#include "bcos-ops/tui/Refresher.h"
#include "fixtures/Blocks.h"
#include "fixtures/FakeRpc.h"
#include <boost/test/unit_test.hpp>

namespace bcos::ops::test
{
BOOST_AUTO_TEST_SUITE(ExplorerTest)

BOOST_AUTO_TEST_CASE(capacityFollowsTheTerminalWithinBounds)
{
    BOOST_CHECK_EQUAL(tui::explorerCapacity(10), 20U);
    BOOST_CHECK_EQUAL(tui::explorerCapacity(24), 24U);
    BOOST_CHECK_EQUAL(tui::explorerCapacity(500), 200U);
    BOOST_CHECK_EQUAL(tui::explorerCapacity(-3), 20U);
}

BOOST_AUTO_TEST_CASE(firstFetchBackFillsCapacityNewestFirst)
{
    FakeRpc rpc;
    scriptChain(rpc, 128, 2);
    tui::ExplorerState state;
    state.capacity = 24;
    tui::explorerFetch(state, rpc.call(), "group0", 128);
    BOOST_REQUIRE_EQUAL(state.blocks.size(), 24U);
    BOOST_CHECK_EQUAL(state.blocks.front().number, 128);
    BOOST_CHECK_EQUAL(state.blocks.back().number, 105);
    BOOST_CHECK_EQUAL(state.blocks.front().txCount, 2);
    BOOST_CHECK_EQUAL(state.blocks.front().hash, fakeHash(128));
    BOOST_CHECK_EQUAL(state.blocks.front().sealer, "0");
    BOOST_CHECK_EQUAL(state.lastFetchedHeight, 128);
    // txs flattened newest block first, block order inside a block kept
    BOOST_REQUIRE_EQUAL(state.txs.size(), 24U);
    BOOST_CHECK_EQUAL(state.txs[0].hash, fakeHash(12800, 't'));
    BOOST_CHECK_EQUAL(state.txs[1].hash, fakeHash(12801, 't'));
    BOOST_CHECK_EQUAL(state.txs[2].blockNumber, 127);
    BOOST_CHECK_EQUAL(state.txs[0].from, "0x3f12000000000000000000000000000000009a0");
    BOOST_CHECK_EQUAL(state.txs[0].status, -1);
    // the request carries the full-tx flags: [group, "", n, false, false]
    auto const& first = rpc.calls().front();
    BOOST_CHECK_EQUAL(first.first, "getBlockByNumber");
    BOOST_CHECK_EQUAL(first.second[2].asInt64(), 105);
    BOOST_CHECK_EQUAL(first.second[3].asBool(), false);
    BOOST_CHECK_EQUAL(first.second[4].asBool(), false);
}

BOOST_AUTO_TEST_CASE(secondFetchIsIncremental)
{
    FakeRpc rpc;
    scriptChain(rpc, 10, 1);
    tui::ExplorerState state;
    state.capacity = 20;
    tui::explorerFetch(state, rpc.call(), "group0", 10);
    auto calls = rpc.calls().size();
    tui::explorerFetch(state, rpc.call(), "group0", 10);  // nothing new
    BOOST_CHECK_EQUAL(rpc.calls().size(), calls);
    rpc.setFor("getBlockByNumber", 2, "11", fakeBlock(11, 3));
    rpc.setFor("getBlockByNumber", 2, "12", fakeBlock(12, 0));
    tui::explorerFetch(state, rpc.call(), "group0", 12);
    BOOST_CHECK_EQUAL(rpc.calls().size(), calls + 2);
    BOOST_CHECK_EQUAL(state.blocks.front().number, 12);
    BOOST_CHECK_EQUAL(state.blocks[1].number, 11);
    BOOST_CHECK_EQUAL(state.txs.front().blockNumber, 11);
    BOOST_CHECK_EQUAL(state.txs.size(), 14U);  // 11 single-tx blocks + 3
}

BOOST_AUTO_TEST_CASE(aRoundFetchesAtMostFiftyAndMarksTheGap)
{
    FakeRpc rpc;
    scriptChain(rpc, 200, 0);
    tui::ExplorerState state;
    state.capacity = 200;
    tui::explorerFetch(state, rpc.call(), "group0", 10);
    BOOST_CHECK_EQUAL(state.blocks.size(), 11U);
    tui::explorerFetch(state, rpc.call(), "group0", 200);  // 189 new, only 151..200 fetched
    BOOST_CHECK_EQUAL(state.blocks.size(), 61U);
    BOOST_CHECK_EQUAL(state.blocks.front().number, 200);
    BOOST_CHECK_EQUAL(state.blocks[49].number, 151);
    BOOST_CHECK(state.blocks[49].gapBelow);
    BOOST_CHECK(!state.blocks[48].gapBelow);
    BOOST_CHECK_EQUAL(state.blocks[50].number, 10);
    BOOST_CHECK_EQUAL(state.lastFetchedHeight, 200);
}

BOOST_AUTO_TEST_CASE(rowsAreCappedAtCapacity)
{
    FakeRpc rpc;
    scriptChain(rpc, 60, 5);
    tui::ExplorerState state;
    state.capacity = 200;
    tui::explorerFetch(state, rpc.call(), "group0", 60);
    BOOST_CHECK_EQUAL(state.blocks.size(), 50U);  // one round
    BOOST_CHECK_EQUAL(state.txs.size(), 200U);    // 50 × 5 capped
    BOOST_CHECK_EQUAL(state.txs.back().blockNumber, 21);
    state.capacity = 20;
    tui::explorerFetch(state, rpc.call(), "group0", 60);  // nothing new, still trims
    BOOST_CHECK_EQUAL(state.blocks.size(), 20U);
    BOOST_CHECK_EQUAL(state.txs.size(), 20U);
}

BOOST_AUTO_TEST_CASE(aFailingBlockKeepsWhatArrived)
{
    FakeRpc rpc;
    scriptChain(rpc, 5, 1);
    rpc.setFor("getBlockByNumber", 2, "4", Json::Value());  // placeholder, replaced below
    tui::ExplorerState state;
    state.capacity = 20;
    // make block 4 fail: a result the fake does not know → "no connection"
    FakeRpc partial;
    partial.set("getBlockNumber", 5);
    for (int64_t n = 0; n <= 3; ++n)
    {
        partial.setFor("getBlockByNumber", 2, std::to_string(n), fakeBlock(n, 1));
    }
    partial.remove("getBlockByNumber");
    tui::explorerFetch(state, partial.call(), "group0", 5);
    BOOST_CHECK_EQUAL(state.blocks.size(), 4U);
    BOOST_CHECK_EQUAL(state.lastFetchedHeight, 3);
    partial.setFor("getBlockByNumber", 2, "4", fakeBlock(4, 1));
    partial.setFor("getBlockByNumber", 2, "5", fakeBlock(5, 1));
    tui::explorerFetch(state, partial.call(), "group0", 5);
    BOOST_CHECK_EQUAL(state.blocks.size(), 6U);
    BOOST_CHECK_EQUAL(state.lastFetchedHeight, 5);
}

BOOST_AUTO_TEST_CASE(intervalAndAgeComeFromAdjacentTimestamps)
{
    tui::ExplorerState state;
    BOOST_CHECK(!tui::avgIntervalMs(state));
    BOOST_CHECK(!tui::lastBlockAgeMs(state, 5));
    for (int64_t n = 30; n >= 0; --n)
    {
        state.blocks.push_back(tui::blockRowFromJson(fakeBlock(n, 0, 1000 + n * 1500)));
    }
    BOOST_CHECK_EQUAL(*tui::avgIntervalMs(state, 20), 1500);
    BOOST_CHECK_EQUAL(*tui::avgIntervalMs(state, 5), 1500);
    BOOST_CHECK_EQUAL(*tui::lastBlockAgeMs(state, 1000 + 30 * 1500 + 2000), 2000);
    BOOST_CHECK_EQUAL(*tui::lastBlockAgeMs(state, 0), 0);  // clock skew never goes negative
    // a gap between rows is averaged across: 2 rows, heights 10 and 0, 10 s apart → 1 s
    tui::ExplorerState gapped;
    gapped.blocks.push_back(tui::blockRowFromJson(fakeBlock(10, 0, 11000)));
    gapped.blocks.push_back(tui::blockRowFromJson(fakeBlock(0, 0, 1000)));
    BOOST_CHECK_EQUAL(*tui::avgIntervalMs(gapped, 20), 1000);
}

BOOST_AUTO_TEST_CASE(receiptStatusIsFetchedLazilyForVisibleRows)
{
    FakeRpc rpc;
    scriptChain(rpc, 3, 2);
    tui::ExplorerState state;
    state.capacity = 20;
    tui::explorerFetch(state, rpc.call(), "group0", 3);
    BOOST_REQUIRE_EQUAL(state.txs.size(), 8U);
    for (auto const& tx : state.txs)
    {
        rpc.setFor("getTransactionReceipt", 2, tx.hash,
            fakeReceipt(tx.hash, tx.blockNumber == 2 ? 16 : 0, tx.blockNumber));
    }
    auto before = rpc.calls().size();
    tui::explorerFetchStatus(state, rpc.call(), "group0", 0, 3);
    BOOST_CHECK_EQUAL(rpc.calls().size(), before + 3);
    BOOST_CHECK_EQUAL(state.txs[0].status, 0);
    BOOST_CHECK_EQUAL(state.txs[2].status, 16);
    BOOST_CHECK_EQUAL(state.txs[3].status, -1);
    tui::explorerFetchStatus(state, rpc.call(), "group0", 0, 3);  // cached: no new calls
    BOOST_CHECK_EQUAL(rpc.calls().size(), before + 3);
    tui::explorerFetchStatus(state, rpc.call(), "group0", 6, 10);  // past the end is fine
    BOOST_CHECK_EQUAL(state.txs[7].status, 0);
}

BOOST_AUTO_TEST_CASE(refresherFillsTheExplorerAndAuthFlag)
{
    FakeRpc rpc;
    scriptChain(rpc, 128, 1);
    tui::Model model;
    Connection connection;
    connection.call = rpc.call();
    connection.source = "rpc";
    connection.group = "group0";
    tui::Refresher refresher(connection, LocalFallbacks{}, Thresholds{}, model, nullptr, 100000);
    refresher.setRowCapacity([]() { return size_t(30); });
    refresher.setVisibleTxRange([]() { return std::make_pair(size_t(0), size_t(2)); });
    for (int64_t n = 127; n <= 128; ++n)
    {
        rpc.setFor("getTransactionReceipt", 2, fakeHash(n * 100, 't'), fakeReceipt("", 0, n));
    }
    refresher.runOnce();
    std::lock_guard<std::mutex> lock(model.mutex);
    BOOST_CHECK_EQUAL(model.explorer.blocks.size(), 30U);
    BOOST_CHECK_EQUAL(model.explorer.blocks.front().number, 128);
    BOOST_CHECK_EQUAL(model.explorer.txs[0].status, 0);
    BOOST_CHECK_EQUAL(model.explorer.txs[2].status, -1);
    BOOST_CHECK(!model.authChain);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ops::test
