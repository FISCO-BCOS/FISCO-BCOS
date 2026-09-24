#pragma once

#include <bcos-framework/ledger/OpForkSchedule.h>
#include <cstdint>
#include <evmc/evmc.hpp>

namespace bcos::evm::opstack
{
// The OpFork ladder enum (Bedrock..Karst, ordered for `>=` comparisons) and the
// schedule struct live in bcos-framework/ledger/OpForkSchedule.h — the single
// fork-activation parser shared with the devp2p header validator. These aliases
// keep the opstack-executor/engine references (opstack::OpFork::Isthmus, ...)
// working unchanged.
using bcos::ledger::OpFork;
using bcos::ledger::OpForkSchedule;

struct PrecompileOverrides;

struct OpForkConfig
{
    OpFork fork;
    evmc_revision rev;
    const PrecompileOverrides* precompiles;
    bool disable_prague_requests;
    bool has_operator_fee;
    bool has_jovian_operator_formula;
    bool has_da_footprint;
    // L1 data-fee formula is a three-state across these two flags:
    //   has_legacy_l1_formula=true            -> Bedrock..Delta overhead/scalar formula
    //   has_ecotone_l1_formula=true           -> Ecotone calldataGas formula
    //   both false                            -> Fjord+ FastLZ formula
    // (has_legacy_l1_formula implies has_ecotone_l1_formula=false.)
    bool has_ecotone_l1_formula;
    bool has_legacy_l1_formula;
    // Regolith deposit fixes: deposits count a nonce, is_system_tx is deprecated, etc.
    bool regolith_deposit_fixes;
    // Canyon+: deposit receipts carry depositReceiptVersion=1.
    bool has_deposit_receipt_version;
    // Canyon+: headers carry the (always empty) withdrawals list field.
    bool has_withdrawals;
};

const OpForkConfig& bedrockConfig() noexcept;
const OpForkConfig& regolithConfig() noexcept;
const OpForkConfig& canyonConfig() noexcept;
const OpForkConfig& deltaConfig() noexcept;
const OpForkConfig& ecotoneConfig() noexcept;
const OpForkConfig& fjordConfig() noexcept;
const OpForkConfig& graniteConfig() noexcept;
const OpForkConfig& holoceneConfig() noexcept;
const OpForkConfig& isthmusConfig() noexcept;
const OpForkConfig& jovianConfig() noexcept;
const OpForkConfig& karstConfig() noexcept;

/// Maps the fork that bcos::ledger::resolveOpFork (the single OP fork-activation
/// parser — see ledger/OpForkSchedule.h for the ladder semantics: isthmus-unset =
/// Isthmus zero-start baseline, unscheduled intermediate rungs skipped,
/// UINT64_MAX = not scheduled, `ts >= forkTime` activates) resolves for
/// `timestampSec` onto that fork's executor config.
const OpForkConfig& configAt(
    const bcos::ledger::OpForkSchedule& schedule, uint64_t timestampSec) noexcept;
}  // namespace bcos::evm::opstack
