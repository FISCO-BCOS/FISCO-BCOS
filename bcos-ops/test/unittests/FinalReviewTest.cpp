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
 * @brief contracts the final branch review pinned: server error text never reaches the
 *        terminal raw, a preset write refused by the node has the tx commands' error shape and
 *        exit code, the txpool page builds only the rows that fit and shows decimal wei
 * @file FinalReviewTest.cpp
 */
#include "bcos-ops/Cli.h"
#include "bcos-ops/OpsError.h"
#include "bcos-ops/cmd/SysconfigCmd.h"
#include "bcos-ops/tui/Model.h"
#include "bcos-ops/tui/Txpool.h"
#include "bcos-ops/tui/views/Pages.h"
#include "fixtures/FakeRpc.h"
#include "fixtures/Render.h"
#include <boost/test/unit_test.hpp>
#include <sstream>

namespace bcos::ops::test
{
namespace
{
Connection finalReviewConnection(FakeRpc& _rpc)
{
    Connection connection;
    connection.call = _rpc.call();
    connection.source = "attach";
    connection.group = "group0";
    return connection;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(FinalReviewTest)

BOOST_AUTO_TEST_CASE(serverErrorTextIsSanitizedOnStderr)
{
    registerCommand("test-throws-rpc-error",
        Command{"test only", "", {}, [](Args const&, std::ostream&, std::ostream&) -> int {
                    throw OpsRpcError("getBlockNumber", 1, "\x1b[2J\x1b]0;owned\x07 boom");
                }});
    std::ostringstream out;
    std::ostringstream err;
    auto code = runOps({"fisco-bcos", "test-throws-rpc-error"}, out, err);
    BOOST_CHECK_NE(code, c_exitOk);
    BOOST_CHECK(err.str().find("boom") != std::string::npos);
    BOOST_CHECK(err.str().find('\x1b') == std::string::npos);
    BOOST_CHECK(err.str().find('\x07') == std::string::npos);
    commandTable().erase("test-throws-rpc-error");
}

BOOST_AUTO_TEST_CASE(presetWriteRefusedByTheNodeHasTheTxErrorShape)
{
    FakeRpc rpc;
    rpc.fail("sendTransaction", 10002, "TxPoolIsFull");
    std::ostringstream out;
    auto code = runConsensusOn(finalReviewConnection(rpc),
        Args({"add-sealer", "74637d36", "1", "--yes", "--json"}, {"json", "yes"}), out);
    BOOST_CHECK_EQUAL(code, c_exitChecksFailed);
    auto json = parseJson(out.str(), "refusal");
    BOOST_CHECK_EQUAL(json["error"]["code"].asInt(), 10002);
    BOOST_CHECK_EQUAL(json["error"]["method"].asString(), "sendTransaction");
}

BOOST_AUTO_TEST_CASE(txpoolBuildsOnlyVisibleRowsInDecimalWei)
{
    tui::Model model;
    model.mode = "opstack-el";
    model.txpool.content = true;
    for (int i = 0; i < 500; ++i)
    {
        tui::TxpoolRow row;
        row.hash = "0x" + std::to_string(1000 + i);
        row.from = "0xaa";
        row.nonce = i;
        row.to = "0x43c1";
        row.value = "0xde0b6b3a7640000";  // 1 ether
        model.txpool.rows.push_back(row);
    }
    auto text =
        renderText(tui::renderTxpool(model, tui::Frame{.width = 100, .height = 20}), 100, 20);
    BOOST_CHECK(text.find("1000000000000000000") != std::string::npos);
    BOOST_CHECK(text.find("0xde0b6b3a7640000") == std::string::npos);
    BOOST_CHECK(text.find("pending 500") != std::string::npos);
    // nonce 499 is far below the 20-row frame and is never turned into a table row
    BOOST_CHECK(text.find(" 499 ") == std::string::npos);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ops::test
