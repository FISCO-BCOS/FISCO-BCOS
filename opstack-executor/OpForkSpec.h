/// @file OpForkSpec.h
/// @brief OP Stack fork ladder mapped onto ethereum-executor primitives: for each
///        OpFork, the EVM revision, the precompile override table and the
///        fee/receipt feature flags — the OpPolicy's static per-fork
///        configuration.
///
/// This is the bcos-evm-free counterpart of bcos-evm/opstack/OpForkSchedule.cpp
/// (configAt): same ladder, same values, but expressed as constexpr data over
/// bcos::ledger::OpFork (bcos-framework/ledger/OpForkSchedule.h) so the new
/// executor path never touches the vendored evmone state layer. The
/// field-by-field equivalence against bcos-evm's configAt is pinned by
/// tests/OpForkSpecEquivalenceTest.cpp — when one side moves with a new OP
/// fork, the other must move with it.
///
/// Table values come from op-geth v1.101702.2; the citations mirror
/// bcos-evm/opstack/OpPrecompiles.cpp so a future OP Stack change can be
/// diffed against a specific line rather than re-derived.

#pragma once

#include <bcos-framework/engine/OpTime.h>
#include <bcos-framework/ledger/OpForkSchedule.h>
#include <magic_enum/magic_enum.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <evmc/evmc.hpp>
#include <span>

namespace bcos::executor_v1::opstack
{
using bcos::ledger::OpFork;
using bcos::ledger::OpForkSchedule;

/// The only gas-override (non-length-limit-only) precompile currently implemented: 0x100
/// P256Verify.
inline constexpr evmc::address OP_P256_VERIFY_ADDRESS{0x100};

/// One OP precompile override: the address, the gas override (-1 = keep the
/// revision's own pricing, only the input cap applies) and the input-size cap
/// (0 = uncapped). Mirrors bcos-evm's PrecompileOverrides::Entry.
struct OpPrecompileOverride
{
    evmc::address addr;
    int64_t gas_cost_override;
    size_t max_input_size;
};

// Input-size limits — op-geth params/protocol_params.go:
//   bn256Pairing  112687 (:172, Bn256PairingMaxInputSizeGranite)
//   BLS G1 MSM    513760 (:186)   G2 MSM  488448 (:187)   pairing  235008 (:188)  [Isthmus]
//   bn256Pairing   81984 (:194)   G1 MSM  288960 (:195)
//   G2 MSM        278784 (:196)   pairing 156672 (:197)                            [Jovian]
//
// Fork membership — core/vm/contracts.go:182-251. Isthmus reuses bn256PairingGranite, which is
// why its bn256 limit stays at the Granite value rather than getting one of its own; Jovian
// re-tightens all four.
//
// P256Verify gas 3450 = P256VerifyGasFjord (protocol_params.go:183), NOT the default
// P256VerifyGas 6900 (:184) — op-geth binds 0x100 to p256VerifyFjord from Fjord onward
// (contracts.go:193).
//
// Addresses 0x0c / 0x0e / 0x0f are EIP-2537 G1 MSM / G2 MSM / pairing. op-geth caps only the
// MSM and pairing precompiles; G1Add (0x0b), G2Add (0x0d) and the Map ops (0x10, 0x11) carry no
// limit, hence their absence from these tables.
inline constexpr OpPrecompileOverride OP_ISTHMUS_PRECOMPILE_OVERRIDES[] = {
    {.addr = evmc::address{0x08}, .gas_cost_override = -1, .max_input_size = 112687},
    {.addr = OP_P256_VERIFY_ADDRESS, .gas_cost_override = 3450, .max_input_size = 0},
    {.addr = evmc::address{0x0c}, .gas_cost_override = -1, .max_input_size = 513760},
    {.addr = evmc::address{0x0e}, .gas_cost_override = -1, .max_input_size = 488448},
    {.addr = evmc::address{0x0f}, .gas_cost_override = -1, .max_input_size = 235008},
};

inline constexpr OpPrecompileOverride OP_JOVIAN_PRECOMPILE_OVERRIDES[] = {
    {.addr = evmc::address{0x08}, .gas_cost_override = -1, .max_input_size = 81984},
    {.addr = OP_P256_VERIFY_ADDRESS, .gas_cost_override = 3450, .max_input_size = 0},
    {.addr = evmc::address{0x0c}, .gas_cost_override = -1, .max_input_size = 288960},
    {.addr = evmc::address{0x0e}, .gas_cost_override = -1, .max_input_size = 278784},
    {.addr = evmc::address{0x0f}, .gas_cost_override = -1, .max_input_size = 156672},
};

// Karst has no op-geth constant to cite: the optimism branch of params/protocol_params.go carries
// nothing named Karst, its newest is Bn256PairingMaxInputSizeJovian = 81984. Source is the spec —
// specs.optimism.io/protocol/karst/exec-engine.html: bn256Pairing drops "from the Jovian limit of
// 81,984 bytes (427 pairs) to 57,600 bytes (300 pairs)", and "the other variable-input precompile
// limits are unchanged from Jovian", which is why the three BLS entries below are Jovian's.
// P256Verify deliberately has NO Karst entry: Karst adopts EIP-7951 (P256VERIFY at gas 6900) and
// the Karst revision is EVMC_OSAKA, so with no override the dispatch falls through to the
// Osaka-gated p256verify (gas 6900) with exactly the EIP-7951 pricing -- the pre-Karst 3450
// (RIP-7212 P256VerifyGasFjord) entries stop at Jovian.
inline constexpr OpPrecompileOverride OP_KARST_PRECOMPILE_OVERRIDES[] = {
    {.addr = evmc::address{0x08}, .gas_cost_override = -1, .max_input_size = 57600},
    {.addr = evmc::address{0x0c}, .gas_cost_override = -1, .max_input_size = 288960},
    {.addr = evmc::address{0x0e}, .gas_cost_override = -1, .max_input_size = 278784},
    {.addr = evmc::address{0x0f}, .gas_cost_override = -1, .max_input_size = 156672},
};

// Fjord: only P256Verify. No bn256 limit yet (that arrives with Granite) and no BLS at all
// (CANCUN). Citations as above.
inline constexpr OpPrecompileOverride OP_FJORD_PRECOMPILE_OVERRIDES[] = {
    {.addr = OP_P256_VERIFY_ADDRESS, .gas_cost_override = 3450, .max_input_size = 0},
};

inline constexpr OpPrecompileOverride OP_GRANITE_PRECOMPILE_OVERRIDES[] = {
    {.addr = evmc::address{0x08}, .gas_cost_override = -1, .max_input_size = 112687},
    {.addr = OP_P256_VERIFY_ADDRESS, .gas_cost_override = 3450, .max_input_size = 0},
};

/// Look up @p addr in an override table (linear scan — the tables are 1-5 entries).
[[nodiscard]] inline constexpr const OpPrecompileOverride* findOpPrecompileOverride(
    std::span<const OpPrecompileOverride> entries, const evmc::address& addr) noexcept
{
    for (const auto& entry : entries)
    {
        if (entry.addr == addr)
            return &entry;
    }
    return nullptr;
}

/// The static per-fork execution configuration the OpPolicy carries (mirrors
/// bcos-evm's OpForkConfig field for field).
struct OpForkSpec
{
    OpFork fork;
    evmc_revision rev;
    std::span<const OpPrecompileOverride> precompile_overrides;
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

// Bedrock and Regolith run a Paris EVM: op-sepolia-class OP chains are post-merge (the
// Merge happened at genesis), so the pre-Canyon ladder never touches London. An empty
// override table selects the revision's built-in precompile set; the OP-specific
// override tables only exist Fjord+.
inline constexpr OpForkSpec OP_BEDROCK_SPEC{
    .fork = OpFork::Bedrock,
    .rev = EVMC_PARIS,
    .precompile_overrides = {},
    .disable_prague_requests = true,
    .has_operator_fee = false,
    .has_jovian_operator_formula = false,
    .has_da_footprint = false,
    .has_ecotone_l1_formula = false,
    .has_legacy_l1_formula = true,
    .regolith_deposit_fixes = false,
    .has_deposit_receipt_version = false,
    .has_withdrawals = false,
};

inline constexpr OpForkSpec OP_REGOLITH_SPEC = [] {
    OpForkSpec c = OP_BEDROCK_SPEC;
    c.fork = OpFork::Regolith;
    c.regolith_deposit_fixes = true;
    return c;
}();

// Canyon moves the EVM base to Shanghai (EIP-1153/5656/6780; 4895 is consensus-only on an
// L2 — headers carry an always-empty withdrawals list) and introduces depositReceiptVersion.
inline constexpr OpForkSpec OP_CANYON_SPEC = [] {
    OpForkSpec c = OP_REGOLITH_SPEC;
    c.fork = OpFork::Canyon;
    c.rev = EVMC_SHANGHAI;
    c.has_deposit_receipt_version = true;
    c.has_withdrawals = true;
    return c;
}();

// Delta changes nothing on the EL (span batches are a derivation-layer feature); it is
// kept in the ladder to mirror op-node's naming and rollup.json keying.
inline constexpr OpForkSpec OP_DELTA_SPEC = [] {
    OpForkSpec c = OP_CANYON_SPEC;
    c.fork = OpFork::Delta;
    return c;
}();

inline constexpr OpForkSpec OP_ECOTONE_SPEC{
    .fork = OpFork::Ecotone,
    .rev = EVMC_CANCUN,
    .precompile_overrides = {},
    .disable_prague_requests = true,
    .has_operator_fee = false,
    .has_jovian_operator_formula = false,
    .has_da_footprint = false,
    .has_ecotone_l1_formula = true,
    .has_legacy_l1_formula = false,
    .regolith_deposit_fixes = true,
    .has_deposit_receipt_version = true,
    .has_withdrawals = true,
};

inline constexpr OpForkSpec OP_FJORD_SPEC{
    .fork = OpFork::Fjord,
    .rev = EVMC_CANCUN,
    .precompile_overrides = OP_FJORD_PRECOMPILE_OVERRIDES,
    .disable_prague_requests = true,
    .has_operator_fee = false,
    .has_jovian_operator_formula = false,
    .has_da_footprint = false,
    .has_ecotone_l1_formula = false,
    .has_legacy_l1_formula = false,
    .regolith_deposit_fixes = true,
    .has_deposit_receipt_version = true,
    .has_withdrawals = true,
};

inline constexpr OpForkSpec OP_GRANITE_SPEC = [] {
    OpForkSpec c = OP_FJORD_SPEC;
    c.fork = OpFork::Granite;
    c.precompile_overrides = OP_GRANITE_PRECOMPILE_OVERRIDES;
    return c;
}();

inline constexpr OpForkSpec OP_HOLOCENE_SPEC = [] {
    OpForkSpec c = OP_FJORD_SPEC;
    c.fork = OpFork::Holocene;
    c.precompile_overrides = OP_GRANITE_PRECOMPILE_OVERRIDES;
    return c;
}();

inline constexpr OpForkSpec OP_ISTHMUS_SPEC{
    .fork = OpFork::Isthmus,
    .rev = EVMC_PRAGUE,
    .precompile_overrides = OP_ISTHMUS_PRECOMPILE_OVERRIDES,
    .disable_prague_requests = true,
    .has_operator_fee = true,
    .has_jovian_operator_formula = false,
    .has_da_footprint = false,
    .has_ecotone_l1_formula = false,
    .has_legacy_l1_formula = false,
    .regolith_deposit_fixes = true,
    .has_deposit_receipt_version = true,
    .has_withdrawals = true,
};

inline constexpr OpForkSpec OP_JOVIAN_SPEC{
    .fork = OpFork::Jovian,
    .rev = EVMC_PRAGUE,
    .precompile_overrides = OP_JOVIAN_PRECOMPILE_OVERRIDES,
    .disable_prague_requests = true,
    .has_operator_fee = true,
    .has_jovian_operator_formula = true,
    .has_da_footprint = true,
    .has_ecotone_l1_formula = false,
    .has_legacy_l1_formula = false,
    .regolith_deposit_fixes = true,
    .has_deposit_receipt_version = true,
    .has_withdrawals = true,
};

// Karst (OP "Upgrade 19") on top of Jovian: the EVM revision moves to Osaka -- EIP-7825 per-tx
// gas cap (normal transactions only; deposits stay exempt, see OpEthDeposit.h), EIP-7823/7883
// MODEXP, EIP-7939 CLZ and EIP-7951 P256VERIFY all gate on EVMC_OSAKA. Fee/receipt semantics
// (operator fee, DA footprint) are derived from OP_JOVIAN_SPEC so future Jovian changes carry
// into Karst.
inline constexpr OpForkSpec OP_KARST_SPEC = [] {
    OpForkSpec c = OP_JOVIAN_SPEC;
    c.fork = OpFork::Karst;
    c.rev = EVMC_OSAKA;
    c.precompile_overrides = OP_KARST_PRECOMPILE_OVERRIDES;
    return c;
}();

/// The spec for one resolved fork (total over the ladder): one row per rung, in
/// OpFork declaration order — the array index IS static_cast<size_t>(fork).
inline constexpr std::array<OpForkSpec, 11> c_opForkSpecs{{
    OP_BEDROCK_SPEC,
    OP_REGOLITH_SPEC,
    OP_CANYON_SPEC,
    OP_DELTA_SPEC,
    OP_ECOTONE_SPEC,
    OP_FJORD_SPEC,
    OP_GRANITE_SPEC,
    OP_HOLOCENE_SPEC,
    OP_ISTHMUS_SPEC,
    OP_JOVIAN_SPEC,
    OP_KARST_SPEC,
}};
static_assert(
    [] {
        auto const rungs = magic_enum::enum_values<OpFork>();
        if (rungs.size() != c_opForkSpecs.size())
        {
            return false;
        }
        for (std::size_t i = 0; i < rungs.size(); ++i)
        {
            // Both invariants the row order carries: the enum values are contiguous in
            // declaration order AND row i dispatches rung i (the identity check its sibling
            // c_opForkLadder pin uses — contiguity alone leaves a swapped table row silently
            // mis-mapping opForkSpec's EVM revision and precompile set).
            if (static_cast<std::size_t>(rungs[i]) != i || c_opForkSpecs[i].fork != rungs[i])
            {
                return false;
            }
        }
        return true;
    }(),
    "c_opForkSpecs must have exactly one row per OpFork rung in declaration order");

[[nodiscard]] inline constexpr OpForkSpec opForkSpec(OpFork fork) noexcept
{
    return c_opForkSpecs[static_cast<std::size_t>(fork)];
}

/// Maps the fork that bcos::ledger::resolveOpFork (the single OP fork-activation
/// parser — see ledger/OpForkSchedule.h for the ladder semantics: isthmus-unset =
/// Isthmus zero-start baseline, unscheduled intermediate rungs skipped,
/// UINT64_MAX = not scheduled, `ts >= forkTime` activates) resolves for
/// `timestampSec` onto that fork's execution spec.
[[nodiscard]] inline OpForkSpec opForkSpecAt(
    const OpForkSchedule& schedule, uint64_t timestampSec) noexcept
{
    return opForkSpec(bcos::ledger::resolveOpFork(schedule, timestampSec));
}

/// Internal timestamps are MILLISECONDS everywhere in this node
/// (BlockHeader::timestamp, PayloadAttributes::timestamp,
/// ExecutionPayload::timestamp); the OP fork schedule ([op_fork_timestamps],
/// op-node's rollup.json jovian_time/karst_time) is SECONDS. Every fork judgement on the new OP
/// lane converts through this alias, which delegates to the framework's single implementation
/// (bcos-framework/engine/OpTime.h) — the same conversion the RPC's EIP-7825 gate uses, so the
/// two cannot round differently (the alias is kept because the OP lane's call sites name the
/// domain conversion; the bcos-evm-free successor of the retired OpCommon.h's
/// detail::forkTimestampSec).
[[nodiscard]] inline constexpr uint64_t opForkTimestampSec(int64_t internalTimestampMs) noexcept
{
    return bcos::engine::unixSecondsFromInternalMillis(static_cast<uint64_t>(internalTimestampMs));
}
}  // namespace bcos::executor_v1::opstack
