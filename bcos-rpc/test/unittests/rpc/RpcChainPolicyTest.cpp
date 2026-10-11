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

#include <optional>

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
        BOOST_CHECK_EQUAL(suggestedPriorityFeeWei(v), 0u);
    }
    // Ethereum executor (== ETHEREUM): geth fee semantics, non-zero tip.
    BOOST_CHECK(usesEthereumFeeSemantics(ledger::ETHEREUM_EXECUTOR_VERSION));
    BOOST_CHECK_EQUAL(suggestedPriorityFeeWei(ledger::ETHEREUM_EXECUTOR_VERSION), 1'000'000u);
    // OP mode is exactly OPSTACK_EXECUTOR_VERSION (a fixed genesis value, not a floor):
    // the == gate lives where the mode is selected (Initializer / EngineServiceInitializer),
    // not in the fee policy. A higher version still gets the Ethereum tip via
    // usesEthereumFeeSemantics (>= ETHEREUM).
    BOOST_CHECK(usesEthereumFeeSemantics(ledger::OPSTACK_EXECUTOR_VERSION));
    BOOST_CHECK_EQUAL(suggestedPriorityFeeWei(ledger::OPSTACK_EXECUTOR_VERSION), 1'000'000u);
    BOOST_CHECK_EQUAL(suggestedPriorityFeeWei(ledger::OPSTACK_EXECUTOR_VERSION + 5), 1'000'000u);

    // The lane distinction that matters here is executor_version alone: the Ethereum lane
    // (executor_version >= ETHEREUM_EXECUTOR_VERSION — the pure-Ethereum executor on a full
    // MPT state root) must keep EIP-1559 semantics, matching eth_gasPrice. There is no
    // separate OP-only predicate in this policy: blockBaseFee keys on the header shape
    // (isOpEthereumBlock) and the tip on the >= ETHEREUM floor above.
}

// The unknown-timestamp corner of the EIP-7825 gate: 0 is a VALID instant and must be
// compared, while nullopt (unreadable/pruned header) must fail CLOSED — the old
// `block ? ts : 0` ternary read "unknown" as "pre-Karst" and let an explicit gas up to
// the block cap slip the clamp on a Karst chain.
BOOST_AUTO_TEST_CASE(eip7825GateFailsClosedOnUnknownTimestamp)
{
    ledger::LedgerConfig cfg;
    cfg.setExecutorVersion(ledger::OPSTACK_EXECUTOR_VERSION);

    ledger::OpForkSchedule karstAt1000;
    karstAt1000.m_karstTime = 1000;
    cfg.setOpForkSchedule(karstAt1000);
    // Known timestamps compare against the chain's own karst activation.
    BOOST_CHECK(!eip7825InForceAt(cfg, 42, std::optional<uint64_t>(999)));
    BOOST_CHECK(eip7825InForceAt(cfg, 42, std::optional<uint64_t>(1000)));
    // Timestamp 0 is a valid instant, not "unknown": karst-from-genesis is in force at 0.
    ledger::OpForkSchedule karstFromGenesis;
    karstFromGenesis.m_karstTime = 0;
    cfg.setOpForkSchedule(karstFromGenesis);
    BOOST_CHECK(eip7825InForceAt(cfg, 42, std::optional<uint64_t>(0)));
    // UNKNOWN timestamp clamps (over-clamping a pre-Karst estimate is recoverable; an
    // unclamped over-cap estimate is the lie this gate exists to prevent).
    cfg.setOpForkSchedule(karstAt1000);
    BOOST_CHECK(eip7825InForceAt(cfg, 42, std::nullopt));
    // Known timestamp on a chain without the row stays pre-Karst (documented M1
    // semantics for pre-row OP chains) — but unknown still fails closed.
    cfg.setOpForkSchedule(std::nullopt);
    BOOST_CHECK(!eip7825InForceAt(cfg, 42, std::optional<uint64_t>(500)));
    BOOST_CHECK(eip7825InForceAt(cfg, 42, std::nullopt));
}

BOOST_AUTO_TEST_SUITE_END()
