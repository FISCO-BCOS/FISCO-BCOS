#pragma once

#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/protocol/ProtocolTypeDef.h"

namespace bcos::scheduler_v1
{

// Decide whether block @p blockNumber commits with an Ethereum MPT state root instead of
// the legacy XOR root (spec 5.6 / 5.10). Pure function; wired into coCommitBlock by PR-19.
inline bool shouldBuildMPT(
    bcos::ledger::Features const& features, bcos::protocol::BlockNumber blockNumber) noexcept
{
    // Scenario B: L2 Ethereum-compat chains build the MPT from genesis on. Checked FIRST:
    // at block 0 feature_mpt_state_root is not yet active, so consulting scenario A first
    // would send an L2 chain down the XOR path.
    if (features.get(ledger::Features::Flag::feature_l2_ethereum_compat))
    {
        return true;
    }

    // Scenario A: MPT enabled mid-chain by feature_mpt_state_root. Strictly-greater keeps
    // the activation block N itself on XOR as the transition boundary (spec 5.10). The
    // >= 0 guard rejects activationBlockOf == -1 — a flag set() without a storage load —
    // which would otherwise silently enable MPT since blockNumber > -1 is always true.
    if (features.get(ledger::Features::Flag::feature_mpt_state_root))
    {
        auto activationBlock =
            features.activationBlockOf(ledger::Features::Flag::feature_mpt_state_root);
        return activationBlock >= 0 && blockNumber > activationBlock;
    }

    // Neither flag: legacy XOR state root.
    return false;
}

}  // namespace bcos::scheduler_v1
