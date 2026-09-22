#pragma once

#include <bcos-framework/ledger/GenesisConfig.h>
#include <cstdint>
#include <evmc/evmc.hpp>

namespace bcos::evm::opstack
{
// ────────────────────────────────────────────────────────────────────────────
// OP-Stack fork schedule (Bedrock onward) ↔ Ethereum base fork
//
// Reference: op-geth v1.101702.2 (authority) + optimism docs / specs.
// The FULL Bedrock..Karst ladder is modeled so opstack-executor + devp2p can
// replay an OP chain (e.g. op-sepolia) from genesis. op-sepolia is post-merge
// (the Merge happened at genesis), so Bedrock/Regolith map to a Paris EVM —
// never London.
//
//   OP fork      | Ethereum base | EVM rev (FB)      | FB status
//   -------------+---------------+-------------------+----------------------
//   Bedrock      | Paris (merge@ | EVMC_PARIS        | modeled (sync); genesis fork,
//                | genesis)      |                   | legacy overhead/scalar L1 fee
//   Regolith     | Paris         | EVMC_PARIS        | modeled (sync); deposit-tx fixes
//   Canyon       | Shanghai      | EVMC_SHANGHAI     | modeled (sync); EIP-4895/1153/5656/
//                |               |                   | 6780, deposit receipt version
//   Delta        | Shanghai      | EVMC_SHANGHAI     | modeled (sync); no EL change
//   Ecotone      | Cancun        | EVMC_CANCUN       | modeled; blob L1 fee (EIP-4844/4788/7516)
//   Fjord        | Cancun        | EVMC_CANCUN       | modeled; FastLZ L1 fee, p256 active
//   Granite      | Cancun        | EVMC_CANCUN       | modeled; 8 precompile size limits
//   Holocene     | Cancun        | EVMC_CANCUN       | modeled; EIP-1559 via 9B extraData
//   Isthmus      | Prague/Pectra | EVMC_PRAGUE       | modeled; EIP-7702/7623/2935/2537 + OP
//                 |               |                   | deposit changes
//   Jovian       | Prague        | EVMC_PRAGUE       | modeled; +DA footprint, operator fee ×100
//   Karst        | Osaka (Fusaka | EVMC_OSAKA        | modeled; Jovian fees + Osaka EVM
//                | EL half)      |                   |
//
// Baseline compatibility: when the schedule carries no isthmus_time (the only
// shape existing chains have — they configure jovian_time/karst_time at most),
// Isthmus stays the zero-start baseline and everything below Jovian resolves to
// it (configAt). Only a schedule with isthmus_time explicitly set activates the
// full ladder, whose fallback is Bedrock.
//
// Key facts:
//   * Isthmus = all Prague/Pectra features that apply to L2s (optimism docs
//     pectra-changes: "the upcoming Isthmus hardfork will contain all Prague
//     features"); Jovian adds OP-only DA footprint + operator-fee-fix on the
//     same Prague base — hence both map to EVMC_PRAGUE.
//   * Karst (OP "Upgrade 19") moves the EVM base to Osaka: EIP-7825 per-tx gas cap
//     (deposits exempt, see runDeposit), EIP-7823/7883 MODEXP, EIP-7939 CLZ and
//     EIP-7951 P256VERIFY all gate on EVMC_OSAKA in the vendored state layer. Fee and
//     receipt semantics carry over from Jovian; the precompile table tightens
//     bn256Pairing to 57600 and stops overriding 0x100 so EIP-7951 pricing applies.
// ────────────────────────────────────────────────────────────────────────────
enum class OpFork
{
    Bedrock,
    Regolith,
    Canyon,
    Delta,
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

/// Resolves the OP fork config for a block from the chain's genesis fork schedule
/// ([op_fork_timestamps] in config.genesis, ledger::OpForkSchedule) and that block's
/// timestamp IN SECONDS. This is op-node's own keying: rollup.json carries jovian_time /
/// karst_time and `IsJovian(ts)` is `Time != nil && ts >= *Time`
/// (op-node/rollup/types.go), with UINT64_MAX standing in for op-node's nil.
///
/// Latest fork first: Karst when `timestampSec >= m_karstTime`, else Jovian when
/// `>= m_jovianTime`, then Isthmus .. Regolith down the ladder, skipping every entry
/// left at UINT64_MAX ("not scheduled", op-node's nil).
///
/// Baseline compatibility: when m_isthmusTime is NOT set (the only shape existing
/// chains have), Isthmus is the zero-start baseline — every timestamp below
/// jovian_time resolves to isthmusConfig() and the pre-Isthmus rungs are never
/// consulted, bit-identical to the two-key schedule. When m_isthmusTime IS set the
/// full ladder is live and timestamps before the earliest scheduled fork fall back
/// to bedrockConfig() (Bedrock is the genesis fork and has no schedule entry).
///
/// The schedule's non-decreasing order is validated once, at config load
/// (NodeConfig::loadOpForkTimestamps); this function does not re-check it.
///
/// WHICH block's timestamp is the caller's decision and differs per rule — op-geth keys the
/// Holocene extraData decode and the Jovian DA-footprint branch on the PARENT header's time
/// (consensus/misc/eip1559/eip1559.go CalcBaseFee), while the L1-attributes calldata layout
/// and the Jovian payload attributes key on the CHILD's (op-node derive/l1_block_info.go,
/// derive/attributes.go). Every caller in this tree converts through
/// opstack-executor/OpCommon.h's forkTimestampSec (internal timestamps are milliseconds).
const OpForkConfig& configAt(
    const bcos::ledger::OpForkSchedule& schedule, uint64_t timestampSec) noexcept;
}  // namespace bcos::evm::opstack
