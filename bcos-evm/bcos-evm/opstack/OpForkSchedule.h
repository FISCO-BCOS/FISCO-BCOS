#pragma once

#include <bcos-framework/ledger/GenesisConfig.h>
#include <evmc/evmc.hpp>

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace bcos::evm::opstack
{
// ────────────────────────────────────────────────────────────────────────────
// OP-Stack fork schedule (Bedrock onward) ↔ Ethereum base fork
//
// Reference: op-geth params/config_op.go (EL fork order, no Delta) + op-node.
// FB MODELS Regolith+ (the enum below, in protocol order). The ledger codec
// accepts every contiguous EL fork range by name (S2 landed) and the Engine
// admits the pre-Isthmus payload shapes via its version/FCU windows (S3 landed),
// so a Regolith..Karst schedule is reachable end to end.
//
// clang-format off
//   OP fork  | Ethereum base | EVM rev (FB)  | FB status
//   ---------+---------------+---------------+--------------------------------------------
//   Bedrock  | London        | —             | not modeled (unreachable; first fork is Regolith)
//   Regolith | London        | EVMC_LONDON   | modeled; deposit-tx fixes, Bedrock L1 fee
//   Canyon   | Shanghai      | EVMC_SHANGHAI | modeled; EIP-4895/1153/5656/6780, Bedrock L1 fee
//   Ecotone  | Cancun        | EVMC_CANCUN   | modeled; blob L1 fee (EIP-4844/4788/7516)
//   Fjord    | Cancun        | EVMC_CANCUN   | modeled; FastLZ L1 fee, p256 active
//   Granite  | Cancun        | EVMC_CANCUN   | modeled; 8 precompile size limits
//   Holocene | Cancun        | EVMC_CANCUN   | modeled; EIP-1559 via 9B extraData
//   Isthmus  | Prague/Pectra | EVMC_PRAGUE   | modeled; EIP-7702/7623/2935/2537 + deposits
//   Jovian   | Prague        | EVMC_PRAGUE   | modeled; +DA footprint, operator fee ×100
//   Karst    | Osaka         | EVMC_OSAKA    | modeled; Jovian fees + Osaka EVM + Karst caps
//
// Key facts:
//   * Isthmus = all Prague/Pectra features that apply to L2s (optimism docs
//     pectra-changes: "the upcoming Isthmus hardfork will contain all Prague
//     features"); Jovian adds OP-only DA footprint + operator-fee-fix on the
//     same Prague base — hence both map to EVMC_PRAGUE.
//   * Karst maps to EVMC_OSAKA with an independent precompile-override object
//     (bn256Pairing's input limit tightens to 57600; P256VERIFY/0x100's override moves
//     from RIP-7212's 3450 to EIP-7951's 6900, pinned by an explicit 0x100 entry in
//     karstPrecompileOverrides). EIP-7825 per-tx gas cap gates on Osaka with deposits
//     exempt (see runDeposit). Production parse accepts any contiguous EL fork range,
//     so Karst is nameable as a baseline or after any earlier activation.
// ────────────────────────────────────────────────────────────────────────────
enum class OpFork
{
    Regolith,
    Canyon,
    Ecotone,
    Fjord,
    Granite,
    Holocene,
    Isthmus,
    Jovian,
    Karst,
};

struct PrecompileOverrides;

/// L1 fee formula family for a fork (specs.optimism.io/protocol/exec-engine.html):
/// Bedrock = (calldataGas + overhead) * l1BaseFee * scalar / 1e6 (slots 1/5/6),
/// Ecotone = calldataGas * (16*l1BaseFee*l1BaseFeeScalar + blob...) / 16e6 (slots 1/3/7),
/// Fjord   = FastLZ calldata estimate feeding the Ecotone formula.
/// Single selector for the L1 data-fee formula. The Ecotone activation block's
/// zero slots are handled at use time (ecotoneL1SlotsLive), not by a second flag.
enum class L1FeeModel
{
    Bedrock,
    Ecotone,
    Fjord,
};

struct OpForkConfig
{
    OpFork fork{};
    evmc_revision rev{};
    const PrecompileOverrides* precompiles{};
    bool disable_prague_requests{};
    bool has_operator_fee{};
    bool has_jovian_operator_formula{};
    bool has_da_footprint{};
    // When true, runDeposit passes enforce_max_tx_gas=false (EIP-7825 deposit exemption).
    bool deposit_exempt_from_max_tx_gas{};
    L1FeeModel l1_fee_model{};
};

const OpForkConfig& regolithConfig() noexcept;
const OpForkConfig& canyonConfig() noexcept;
const OpForkConfig& ecotoneConfig() noexcept;
const OpForkConfig& fjordConfig() noexcept;
const OpForkConfig& graniteConfig() noexcept;
const OpForkConfig& holoceneConfig() noexcept;
const OpForkConfig& isthmusConfig() noexcept;
const OpForkConfig& jovianConfig() noexcept;
const OpForkConfig& karstConfig() noexcept;

struct OpForkActivation
{
    OpFork fork{};
    uint64_t timestamp{};
};

/// Timestamp schedule: Unix-second activations select any modeled EL fork
/// (regolith…karst). Production parse goes through the ledger codec, which
/// requires a timestamp-0 baseline and strictly contiguous fork order.
class OpForkSchedule
{
public:
    static OpForkSchedule parse(std::string_view canonical);
    static OpForkSchedule legacy(bool jovianActive);
    /// Release line's [op_fork_timestamps] shorthand (jovian_time/karst_time, UINT64_MAX =
    /// unscheduled) folded to the canonical activation list by ledger::foldOpForkShorthand:
    /// timestamp-0 baseline, forks in protocol order; equal jovian/karst times merge into the
    /// later fork (op-geth CheckConfigForkOrder compares with `>`), so jovian_time ==
    /// karst_time == 0 folds to "0:karst". An unscheduled Jovian is the all-Isthmus legacy
    /// chain; karst earlier than jovian (or set with jovian unscheduled) throws.
    static OpForkSchedule fromLedgerSchedule(const bcos::ledger::OpForkSchedule& schedule);
    /// Canonical ledger-codec text for this activation list ("0:isthmus,100:jovian,..." form):
    /// the inverse of parse(). Lets the [op_fork_timestamps] shorthand fold into the canonical
    /// channel at Initializer wiring time instead of bypassing the resolver.
    [[nodiscard]] std::string canonicalText() const;
    explicit OpForkSchedule(std::vector<OpForkActivation> activations);
    /// Test-only: skip ledger codec validation (and the Karst/Osaka consistency check).
    struct TestBypass
    {
    };
    OpForkSchedule(std::vector<OpForkActivation> activations, TestBypass);
    [[nodiscard]] OpFork forkAt(uint64_t timestampSeconds) const;
    /// Unix-second baseline of the first activation record.
    [[nodiscard]] uint64_t baselineTimestamp() const;
    /// Config for the fork active at a block timestamp IN SECONDS: latest activation with
    /// `timestamp <= timestampSeconds` wins — op-node's own keying (rollup/types.go:
    /// `IsJovian(ts)` is `Time != nil && ts >= *Time`, with UINT64_MAX standing in for
    /// nil). WHICH block's timestamp each rule keys on is the caller's decision: op-geth
    /// keys the Holocene extraData decode and the Jovian DA-footprint branch on the PARENT
    /// header's time (consensus/misc/eip1559/eip1559.go CalcBaseFee), while the
    /// L1-attributes calldata layout and the Jovian payload attributes key on the CHILD's
    /// (op-node derive/l1_block_info.go, derive/attributes.go). Every caller in this tree
    /// converts through bcos-framework/engine/OpTime.h's unixSecondsFromInternalMillis
    /// (internal timestamps are milliseconds).
    [[nodiscard]] const OpForkConfig& configAt(uint64_t timestampSeconds) const;
    /// Named Jovian/Karst activations (Q5 deposits-only). Classify new forks in the .cpp
    /// switch. Computed once at construction; called per block on hot paths, so this is a
    /// view over the cached list, not a fresh vector.
    [[nodiscard]] std::span<const OpForkActivation> jovianAndLaterActivations() const;

private:
    std::vector<OpForkActivation> m_activations;
    std::vector<OpForkActivation> m_jovianAndLater;
};
}  // namespace bcos::evm::opstack
