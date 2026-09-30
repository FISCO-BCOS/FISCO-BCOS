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
 * @file MPTFeatureGates.h
 * @brief The gate deciding whether a block commits an Ethereum MPT state root
 *        (shouldBuildMPT). Moved here from
 *        transaction-scheduler/BaselineSchedulerMPTHelpers.h so every state-root producer
 *        (BaselineScheduler, EngineServiceImpl) applies the SAME transition rule; the old
 *        header remains as a namespace-alias shim. The account-table encoding is node-local
 *        (nodeAddressTableMode) and plays no role in these gates — the MPT delta scan
 *        classifies both account-table layouts.
 */
#pragma once

#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/protocol/ProtocolTypeDef.h"

namespace bcos::ledger::mpt
{

// Decide whether block @p blockNumber commits with an Ethereum MPT state root instead of
// the legacy XOR root (spec 5.6 / 5.10). The execute path (coExecuteBlock) consults it once
// per block when the MPT branch is wired in.
//
// Pure: no throw, no side effect. The account-table encoding is NOT part of the decision:
// it is a node-local physical layout (nodeAddressTableMode), and the MPT delta scan
// classifies both the 40-hex and the 20-byte raw-address account table layouts
// (Classify.h parseAccountTable), so the encoding and the MPT state root combine freely.
//
// @param executorVersion  the chain's executor_version (genesis-fixed; governance writes
//                         crossing ETHEREUM_EXECUTOR_VERSION are refused). The Ethereum lane
//                         (>= 2) builds the MPT from genesis on ("scenario B").
inline bool shouldBuildMPT(int64_t executorVersion, bcos::ledger::Features const& features,
    bcos::protocol::BlockNumber blockNumber)
{
    bool buildMPT = false;
    // Scenario B: Ethereum-lane chains (executor_version >= 2) build the MPT from genesis on.
    // Checked FIRST: at block 0 feature_mpt_state_root is not yet active, so consulting
    // scenario A first would send an Ethereum-lane chain down the XOR path.
    if (executorVersion >= ledger::ETHEREUM_EXECUTOR_VERSION)
    {
        buildMPT = true;
    }
    // Scenario A: MPT enabled mid-chain by feature_mpt_state_root. Strictly-greater keeps
    // the activation block N itself on XOR as the transition boundary (spec 5.10). The
    // >= 0 guard rejects activationBlockOf == -1 — a flag set() without a storage load —
    // which would otherwise silently enable MPT since blockNumber > -1 is always true.
    else if (features.get(ledger::Features::Flag::feature_mpt_state_root))
    {
        auto activationBlock =
            features.activationBlockOf(ledger::Features::Flag::feature_mpt_state_root);
        buildMPT = activationBlock >= 0 && blockNumber > activationBlock;
    }
    // Neither: legacy XOR state root (buildMPT stays false).
    return buildMPT;
}

}  // namespace bcos::ledger::mpt
