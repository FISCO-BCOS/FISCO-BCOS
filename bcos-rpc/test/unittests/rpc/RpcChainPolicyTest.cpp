/**
 * Copyright (C) 2026 FISCO BCOS.
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * @file RpcChainPolicyTest.cpp
 * @brief The executor_version-keyed fee/gas lane helpers.
 */

#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-rpc/web3jsonrpc/utils/RpcChainPolicy.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::rpc;

BOOST_AUTO_TEST_SUITE(RpcChainPolicyTest)

// Lane boundaries follow the canonical executor_version constants.
BOOST_AUTO_TEST_CASE(laneBoundaries)
{
    // native (< ETHEREUM): no geth fee semantics, no tip.
    for (auto const v : {0, 1})
    {
        BOOST_CHECK(!usesEthereumFeeSemantics(v));
        BOOST_CHECK(!isOpStackLane(v));
        BOOST_CHECK_EQUAL(suggestedPriorityFeeWei(v), 0u);
    }
    // Ethereum executor (== ETHEREUM): geth fee semantics, non-zero tip.
    BOOST_CHECK(usesEthereumFeeSemantics(ledger::ETHEREUM_EXECUTOR_VERSION));
    BOOST_CHECK(!isOpStackLane(ledger::ETHEREUM_EXECUTOR_VERSION));
    BOOST_CHECK_EQUAL(suggestedPriorityFeeWei(ledger::ETHEREUM_EXECUTOR_VERSION), 1'000'000u);
    // OP (>= OPSTACK): geth fee semantics, OP tip floor 1e6.
    BOOST_CHECK(usesEthereumFeeSemantics(ledger::OPSTACK_EXECUTOR_VERSION));
    BOOST_CHECK(isOpStackLane(ledger::OPSTACK_EXECUTOR_VERSION));
    BOOST_CHECK_EQUAL(suggestedPriorityFeeWei(ledger::OPSTACK_EXECUTOR_VERSION), 1'000'000u);
    // Above the known set still maps to the OP lane.
    BOOST_CHECK(isOpStackLane(ledger::OPSTACK_EXECUTOR_VERSION + 5));
    BOOST_CHECK_EQUAL(suggestedPriorityFeeWei(ledger::OPSTACK_EXECUTOR_VERSION + 5), 1'000'000u);
}

BOOST_AUTO_TEST_SUITE_END()
