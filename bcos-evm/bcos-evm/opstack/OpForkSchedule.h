#pragma once

#include <evmc/evmc.hpp>

namespace bcos::evm::opstack
{
// ────────────────────────────────────────────────────────────────────────────
// OP-Stack fork schedule (Bedrock onward) ↔ Ethereum base fork
//
// Reference: op-geth v1.101702.2 (authority) + optimism docs / specs.
// FB only MODELS Ecotone+ (the enum below): the minimal validator loop is
// Isthmus+-only (decision A5) and the engine -38005 gate rejects pre-Isthmus
// payloads, so Bedrock/Regolith/Canyon are unreachable — they are listed for
// mapping completeness only, NOT implemented.
//
//   OP fork      | Ethereum base | EVM rev (FB)      | FB status
//   -------------+---------------+-------------------+----------------------
//   Bedrock      | London        | —                 | not modeled (unreachable)
//   Regolith     | London        | —                 | not modeled; deposit-tx fixes
//   Canyon       | Shanghai      | —                 | not modeled; EIP-4895/1153/5656/6780
//   Ecotone      | Cancun        | EVMC_CANCUN       | modeled; blob L1 fee (EIP-4844/4788/7516)
//   Fjord        | Cancun        | EVMC_CANCUN       | modeled; FastLZ L1 fee, p256 active
//   Granite      | Cancun        | EVMC_CANCUN       | modeled; 8 precompile size limits
//   Holocene     | Cancun        | EVMC_CANCUN       | modeled; EIP-1559 via 9B extraData
//   Isthmus      | Prague/Pectra | EVMC_PRAGUE       | modeled; EIP-7702/7623/2935/2537 + OP
//                 |               |                   | deposit changes
//   Jovian        | Prague        | EVMC_PRAGUE       | modeled; +DA footprint, operator fee ×100
//   Karst         | TBD           | EVMC_PRAGUE (alias)| placeholder = Jovian (op-reth only)
//
// Key facts:
//   * Isthmus = all Prague/Pectra features that apply to L2s (optimism docs
//     pectra-changes: "the upcoming Isthmus hardfork will contain all Prague
//     features"); Jovian adds OP-only DA footprint + operator-fee-fix on the
//     same Prague base — hence both map to EVMC_PRAGUE.
//   * Karst has NO real semantics in FB (nor in op-geth v1.101702.2 — IsKarst
//     carries no execution gating; Karst is developed on op-reth only).
//     karstConfig() is an honest Jovian alias, and configAt() never returns it,
//     so Karst is unreachable until a real upstream diff is adapted (its EVM
//     base, likely Osaka, will be decided then).
// ────────────────────────────────────────────────────────────────────────────
enum class OpFork
{
    Ecotone,
    Fjord,
    Granite,
    Holocene,
    Isthmus,
    Jovian,
    Karst,
};

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
    bool has_ecotone_l1_formula;  // true -> Ecotone calldataGas L1; false -> Fjord+ FastLZ
};

const OpForkConfig& ecotoneConfig() noexcept;
const OpForkConfig& fjordConfig() noexcept;
const OpForkConfig& graniteConfig() noexcept;
const OpForkConfig& holoceneConfig() noexcept;
const OpForkConfig& isthmusConfig() noexcept;
const OpForkConfig& jovianConfig() noexcept;
const OpForkConfig& karstConfig() noexcept;

/// Fork-activation flags for the OP validator loop (op-validator-minimal-loop design §4.2,
/// decision A5): no timestamp dimension — FISCO activates forks by feature flag, not by header
/// timestamp. Injected via OpSchedulerSeam's constructor (same channel as chainId) rather than
/// read from SystemConfigs — the minimal loop only distinguishes Isthmus/Jovian. Isthmus is the
/// OP-mode baseline (the engine -38005 gate admits only Isthmus+ payloads), so a single boolean
/// switch — `feature_op_jovian` (Features::Flag, read from genesis [features]) — selects Jovian
/// over Isthmus. This replaces the former timestamp thresholds (isthmusTime/jovianTime): FISCO has
/// no timestamp-based fork activation, only feature flags (the same channel that gates
/// feature_l2_ethereum_compat / feature_evm_prague).
struct OpForkFlags
{
    /// feature_op_jovian enabled → Jovian semantics (DA footprint, operator fee ×100,
    /// 17B Jovian extraData); disabled → Isthmus semantics.
    bool jovianActive = false;
};

/// Resolves the OP fork config from the chain's feature flag (decision A5, feature-flag variant):
/// `jovianActive` -> Jovian, otherwise Isthmus. Isthmus is always the baseline — there is no
/// pre-Isthmus config (the minimal loop is Isthmus+-only and the engine gate rejects pre-Isthmus
/// payloads by construction). This is the single function backing the OpSchedulerSeam
/// execution-time fork selection (design §4.2); the engine's -38005 gate no longer re-derives the
/// fork from a timestamp — OP mode itself is the Isthmus+ admission check.
const OpForkConfig& configAt(const OpForkFlags& flags) noexcept;
}  // namespace bcos::evm::opstack
