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
 * @brief The feature-flag gates deciding whether a block commits an Ethereum MPT state root
 *        (shouldBuildMPT) plus the L2 activation-block boot guard (validateMPTFlagMatrix).
 *        Moved here from transaction-scheduler/BaselineSchedulerMPTHelpers.h so every
 *        state-root producer (BaselineScheduler, EngineServiceImpl) applies the SAME
 *        transition rule; the old header remains as a namespace-alias shim. The
 *        account-table encoding is node-local (nodeAddressTableMode) and plays no role in
 *        these gates — the MPT delta scan classifies both account-table layouts.
 */
#pragma once

#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/protocol/ProtocolTypeDef.h"
#include <bcos-utilities/Exceptions.h>
#include <boost/throw_exception.hpp>

namespace bcos::ledger::mpt
{

DERIVE_BCOS_EXCEPTION(InvalidMPTFlagMatrix);

// Decide whether block @p blockNumber commits with an Ethereum MPT state root instead of
// the legacy XOR root (spec 5.6 / 5.10). The execute path (coExecuteBlock) consults it once
// per block when the MPT branch is wired in.
//
// Pure: no throw, no side effect. The account-table encoding is NOT part of the decision:
// it is a node-local physical layout (nodeAddressTableMode), and the MPT delta scan
// classifies both the 40-hex and the 20-byte raw-address account table layouts
// (Classify.h parseAccountTable), so the encoding and the MPT state root combine freely.
inline bool shouldBuildMPT(
    bcos::ledger::Features const& features, bcos::protocol::BlockNumber blockNumber)
{
    bool buildMPT = false;
    // Scenario B: L2 Ethereum-compat chains build the MPT from genesis on. Checked FIRST:
    // at block 0 feature_mpt_state_root is not yet active, so consulting scenario A first
    // would send an L2 chain down the XOR path.
    if (features.get(ledger::Features::Flag::feature_l2_ethereum_compat))
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
    // Neither flag: legacy XOR state root (buildMPT stays false).
    return buildMPT;
}

// Startup-time guard for the flag matrix shouldBuildMPT relies on (spec 5.10, M7.3).
//
// Scenario B has no transition rule: shouldBuildMPT returns true for EVERY block once
// feature_l2_ethereum_compat is set, because the flag is assumed enabled at genesis
// (activation block 0) — the chain never has XOR history to transition from. A mid-chain
// enable would silently flip the state-root scheme with no boundary, forking any node
// that replays the pre-flag blocks. Refuse to start instead.
//
// Call this with a Features loaded via readFromStorage so activation blocks are
// populated; a bare set() leaves activationBlockOf at -1, which this guard rejects for
// the same reason (an unverifiable activation must not pass a consistency check).
//
// The account-table encoding plays no role here: it is a node-local physical layout
// (nodeAddressTableMode), and the deprecated feature_raw_address flag drives nothing
// (Features::validate refuses to activate it). The OP/Eth lanes' hex-only naming
// constraint is enforced at boot by libinitializer (resolveNodeAddressTableMode refuses
// binary data on a hex-only lane), not by a flag matrix.
//
// @throws InvalidMPTFlagMatrix when feature_l2_ethereum_compat is set with a non-zero (or
//         unknown) activation block. A features object without the L2 flag always passes.
inline void validateMPTFlagMatrix(bcos::ledger::Features const& features)
{
    using Flag = bcos::ledger::Features::Flag;
    if (!features.get(Flag::feature_l2_ethereum_compat))
    {
        return;
    }
    auto activationBlock = features.activationBlockOf(Flag::feature_l2_ethereum_compat);
    if (activationBlock != 0)
    {
        BOOST_THROW_EXCEPTION(
            InvalidMPTFlagMatrix{} << bcos::errinfo_comment(
                "feature_l2_ethereum_compat must be enabled at genesis (activation block 0), "
                "but its activation block is " +
                std::to_string(activationBlock) +
                "; enabling it mid-chain would switch the state-root scheme with no "
                "transition rule (spec 5.10 scenario B)"));
    }
}

}  // namespace bcos::ledger::mpt
