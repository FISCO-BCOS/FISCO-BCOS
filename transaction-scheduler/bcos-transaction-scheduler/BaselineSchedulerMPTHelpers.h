#pragma once

#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/protocol/ProtocolTypeDef.h"
#include <bcos-utilities/Exceptions.h>
#include <boost/throw_exception.hpp>

namespace bcos::scheduler_v1
{

DERIVE_BCOS_EXCEPTION(InvalidMPTFlagMatrix);

// Decide whether block @p blockNumber commits with an Ethereum MPT state root instead of
// the legacy XOR root (spec 5.6 / 5.10). The execute path (coExecuteBlock) consults it once
// per block when the MPT branch is wired in.
//
// Pure: no throw, no side effect. The flag-matrix rule this decision depends on is enforced
// by rejectRawAddressWithMPT below, which the execute path calls inside this predicate's
// branch — one evaluation, and the guard never has to re-ask the question.
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

// Runtime half of the flag-matrix guard. PRECONDITION: call it only where the block is
// already known to be building an MPT — i.e. inside the shouldBuildMPT branch of the execute
// path — so the predicate is evaluated once and this function only has to answer "is
// raw_address in the way?".
//
// The startup half (validateMPTFlagMatrix, below) cannot cover this case: flags activate at
// block boundaries after boot, so a node that started before feature_raw_address — or before
// either MPT flag — never saw the pair at startup.
//
// @throws InvalidMPTFlagMatrix when feature_raw_address is set. Aborting execution loudly
//         beats silently building an MPT that classifies no account table (see
//         validateMPTFlagMatrix for why the combination is unsupported) — fail-loud over
//         fail-silent. @p blockNumber is diagnostic only.
inline void rejectRawAddressWithMPT(
    bcos::ledger::Features const& features, bcos::protocol::BlockNumber blockNumber)
{
    if (features.get(ledger::Features::Flag::feature_raw_address))
    {
        BOOST_THROW_EXCEPTION(InvalidMPTFlagMatrix{} << bcos::errinfo_comment(
                                  "feature_raw_address is active while the MPT state root is "
                                  "enabled (block " +
                                  std::to_string(blockNumber) +
                                  "); raw_address stores account tables under 20-byte binary "
                                  "names, which the MPT delta scan cannot classify — every "
                                  "account would silently drop out of the MPT. Unsupported in "
                                  "v1; aborting the execute path instead of forking silently"));
    }
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
// A second invariant guards feature_raw_address: it switches account table names from
// /apps/<40-hex-address> to 20-byte binary addresses, but the MPT delta scan
// (parseAccountTable, spec 5.10) only classifies 40-hex account tables. With both active,
// every account table fails classification and silently drops out of the MPT — the chain
// keeps committing state roots that cover no account (a silent fork against any honest
// replica). Binary account tables are unsupported in v1, so refuse the combination with
// either MPT flag. rejectRawAddressWithMPT re-checks that half on the blocks that actually
// build an MPT, for activations that happen after boot.
//
// @throws InvalidMPTFlagMatrix when feature_raw_address is set together with
//         feature_mpt_state_root or feature_l2_ethereum_compat, or when
//         feature_l2_ethereum_compat is set with a non-zero (or unknown) activation
//         block. A features object with neither MPT flag always passes.
inline void validateMPTFlagMatrix(bcos::ledger::Features const& features)
{
    using Flag = bcos::ledger::Features::Flag;
    if (features.get(Flag::feature_raw_address) &&
        (features.get(Flag::feature_mpt_state_root) ||
            features.get(Flag::feature_l2_ethereum_compat)))
    {
        BOOST_THROW_EXCEPTION(InvalidMPTFlagMatrix{} << bcos::errinfo_comment(
                                  "feature_raw_address cannot be combined with "
                                  "feature_mpt_state_root or feature_l2_ethereum_compat: "
                                  "raw_address stores account tables under 20-byte binary "
                                  "names, but the MPT delta scan only classifies 40-hex "
                                  "/apps/<address> tables, so every account would silently "
                                  "drop out of the MPT state root (silent fork). Binary "
                                  "account tables are unsupported in v1 (spec 5.10)"));
    }
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

}  // namespace bcos::scheduler_v1
