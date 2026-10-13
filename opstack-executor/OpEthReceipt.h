/// @file OpEthReceipt.h
/// @brief OP Stack receipt metadata for the ethereum-executor-based OP path —
///        the bcos-evm-free counterpart of OpTransition.cpp's OpReceiptMeta /
///        deriveOpReceiptMeta / toOpStackMeta and the receipt projection
///        helpers (status collapse, contractAddress, logsBloom).
///
/// OpTxSnapshot is the validate-time snapshot (the former OpTxProperties fee
/// half): opValidate's discipline — validate and transition MUST price from
/// the same snapshot, and the receipt meta reads ONLY the snapshot — is
/// preserved here. The OpPolicy writes it in additionalMaxCost (validate) and
/// reads it in settleFees / buildReceipt (transition).

#pragma once

#include <ethereum-executor/EVMSupport.h>
#include <opstack-executor/OpFeeParams.h>
#include <opstack-executor/OpRollupCost.h>
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-utilities/Bloom.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <array>
#include <cstdint>
#include <evmc/evmc.hpp>
#include <intx/intx.hpp>
#include <optional>
#include <string>

namespace bcos::executor_v1::opstack
{
/// The validate-time fee snapshot for one OP transaction (ported OpTxProperties,
/// minus the evmone TransactionProperties / evm_tx fields the new path gets from
/// eth::EthTxProperties / the protocol::Transaction itself).
///
/// NOTE: three adjacent bools (has_operator_fee / jovian_operator_formula /
/// has_da_footprint) — keep the order stable; callers can swap two and still
/// compile cleanly.
struct OpTxSnapshot
{
    intx::uint256 l1_cost;
    intx::uint256 operator_cost_at_gas_limit;
    // OpFeeParams snapshot used when computing l1_cost/operator_cost_at_gas_limit;
    // the transition reuses it directly rather than receiving a separate fee parameter,
    // avoiding the validate/transition calls being fed different OpFeeParams across the
    // two invocations, which would underflow operator_cost_at_gas_limit - opAtUsed (both
    // must come from the same fee read).
    OpFeeParams fee;
    uint32_t flz_len = 0;  // Fjord+ single FastLZ result; 0 for Ecotone
    // Operator-fee formula selection captured at validate time, so the transition charges
    // the vault with the SAME formula that priced the sender's pre-charge
    // (operator_cost_at_gas_limit). Without this, a cfg mismatch across validate/transition
    // (fork boundary) would credit the vault by a different formula than the sender was
    // charged → non-conservation / mint.
    bool has_operator_fee = false;
    bool jovian_operator_formula = false;
    // Likewise for the receipt's DA-footprint fields: they must describe the fork the
    // transaction was PRICED under, not whichever spec reaches the transition.
    bool has_da_footprint = false;
    // calldataGasUsed under the Ecotone formula (= zeroes*4 + ones*16); not filled under
    // Fjord+ (flz_len drives the Fjord formula). Snapshot at validate time (the envelope is
    // available there); read by deriveOpReceiptMeta at transition -- preserving the no-spec
    // invariant.
    std::optional<uint64_t> ecotone_calldata_gas_used = std::nullopt;
    // Bedrock–Delta legacy formula: l1GasUsed = txDataGas + overhead (the receipt's
    // L1GasUsed, op-geth rollup_cost.go newL1CostFuncBedrockHelper's second return).
    // Presence marks the transaction as legacy-priced: deriveOpReceiptMeta then reports
    // this value and omits the Ecotone-only scalar/blob fields.
    std::optional<uint64_t> legacy_l1_gas_used = std::nullopt;
    // Filled by settleFees (the actually-charged operator fee at gas_used); read by
    // deriveOpReceiptMeta. Zero when has_operator_fee is false.
    intx::uint256 operator_fee_at_used{0};
};

/// The execution-layer OP receipt metadata, before it is projected into the framework's
/// bcos::protocol::OpStackReceiptMeta (the typed view over the tars opStackMeta hex-string
/// fields) via toOpStackMeta. Deliberately mirrors the op-geth receipt extension fields.
struct OpReceiptMeta
{
    // L1 passthrough (op-geth: L1GasPrice / L1BlobBaseFee / L1BaseFeeScalar / L1BlobBaseFeeScalar /
    // L1Fee)
    std::optional<intx::uint256> l1_gas_price;      // = fee.l1_base_fee
    std::optional<intx::uint256> l1_blob_base_fee;  // = fee.blob_base_fee
    std::optional<uint32_t> l1_base_fee_scalar;
    std::optional<uint32_t> l1_blob_base_fee_scalar;
    std::optional<intx::uint256> l1_fee;  // = l1_cost
    std::optional<uint64_t> l1_gas_used;  // Fjord+; wire index 11
    // operator (Isthmus+)
    std::optional<intx::uint256> operator_fee;    // FISCO extension: actually-charged value
                                                  // (op-geth receipt has no such field)
    std::optional<uint32_t> operator_fee_scalar;  // filled only when (scalar != 0 || constant != 0)
    std::optional<uint64_t> operator_fee_constant;
    // DA footprint (Jovian+; op-geth receipt BlobGasUsed semantics)
    std::optional<uint64_t> da_footprint_gas_scalar;
    std::optional<uint64_t> da_footprint;
    // Effective gas price (base_fee + priority_gas_price) is deliberately NOT here: it is carried
    // on the tars effectiveGasPrice base field instead (op-geth api.go:1775, RPC top-level output).
};

/// Build the non-consensus receipt metadata. Deliberately takes NO OpForkSpec: the receipt has
/// to describe what the transaction was actually priced and charged under, and those decisions
/// are frozen in the validate-time snapshot (OpTxSnapshot). Passing a spec here would give this
/// function a second, independent source of truth that can disagree with the charge whenever
/// validate and transition straddle a fork boundary — which is exactly the bug this signature
/// now makes unrepresentable.
///
/// operator_fee_at_used defaults to the snapshot field settleFees filled; the explicit
/// parameter keeps the bcos-evm signature shape for the dual-run tests.
inline OpReceiptMeta deriveOpReceiptMeta(const OpTxSnapshot& snapshot,
    intx::uint256 operator_fee_at_used, bool fill_operator_scalars) noexcept
{
    const auto& fee = snapshot.fee;
    OpReceiptMeta m;
    m.l1_gas_price = fee.l1_base_fee;
    m.l1_fee = snapshot.l1_cost;
    if (snapshot.legacy_l1_gas_used.has_value())
    {
        // Bedrock–Delta (op-geth pre-Ecotone receipt): L1GasUsed is the overhead-inclusive
        // legacy gas; the blob-base-fee and 32-bit scalar fields do not exist pre-Ecotone (the
        // legacy scalar is a whole-slot uint256 that does not fit the uint32 meta field, and
        // op-geth reports it separately as L1FeeScalar — left out here).
        m.l1_gas_used = *snapshot.legacy_l1_gas_used;
    }
    else
    {
        m.l1_blob_base_fee = fee.blob_base_fee;
        m.l1_base_fee_scalar = fee.base_fee_scalar;
        m.l1_blob_base_fee_scalar = fee.blob_base_fee_scalar;
        // L1 calldata gas used. Ecotone: bedrockCalldataGasUsed on the envelope (zeroes*4 +
        // ones*16), snapped into the snapshot at validate time. Fjord+ (op-geth
        // rollup_cost.go:623-624):
        //   L1GasUsed = estimatedDASizeScaled(fastLzSize) * 16 / 1e6.
        // deriveOPStackFields emits it on every non-deposit receipt; ecotone_calldata_gas_used is
        // unset under Fjord+, where flz_len drives the formula. Bounded: with uint32 flz the
        // scaled term is ≤ 5.8e10, far below uint64 max, so the cast cannot wrap.
        if (snapshot.ecotone_calldata_gas_used.has_value())
            m.l1_gas_used = *snapshot.ecotone_calldata_gas_used;
        else
            m.l1_gas_used =
                static_cast<uint64_t>(estimatedDaSizeScaled(snapshot.flz_len) * 16 / 1'000'000);
    }
    if (snapshot.has_operator_fee)
    {
        m.operator_fee = operator_fee_at_used;
        if (fill_operator_scalars &&
            (fee.operator_fee_scalar != 0 || fee.operator_fee_constant != 0))
        {
            m.operator_fee_scalar = fee.operator_fee_scalar;
            m.operator_fee_constant = fee.operator_fee_constant;
        }
    }
    if (snapshot.has_da_footprint)
    {
        const auto scalar = static_cast<uint64_t>(fee.da_footprint_gas_scalar);
        m.da_footprint_gas_scalar = scalar;
        m.da_footprint = estimatedDaSizeFromFlz(snapshot.flz_len) * scalar;
    }
    return m;
}

/// Local helper: intx::uint256 → bcos::u256, full-width big-endian conversion. intx only exposes
/// an explicit low-64-bit cast operator, so a plain `static_cast<bcos::u256>` would silently drop
/// the high 192 bits — go through a big-endian byte store + bcos::fromBigEndian instead (same
/// pattern as ReceiptResponse.cpp's decode path).
inline bcos::u256 intxToBcosU256(intx::uint256 const& val)
{
    auto be = intx::be::store<evmc::uint256be>(val);
    return bcos::fromBigEndian<bcos::u256>(
        bcos::bytesConstRef{reinterpret_cast<bcos::byte const*>(be.bytes), sizeof(be.bytes)});
}

/// intxToBcosU256 at 512 bits: what the admission rollup cost is carried at (a 256-bit L1 fee
/// plus a 256-bit operator fee, never saturated -- see TxValidator.h RollupCostFn).
inline bcos::u512 intxToBcosU512(intx::uint512 const& val)
{
    std::array<bcos::byte, 64> be{};
    intx::be::unsafe::store(be.data(), val);
    return bcos::fromBigEndian<bcos::u512>(bcos::bytesConstRef{be.data(), be.size()});
}

/// The inverse of intxToBcosU256.
inline intx::uint256 bcosU256ToIntx(bcos::u256 const& val)
{
    std::array<bcos::byte, 32> be{};
    bcos::toBigEndian(val, be);  // writes 32 big-endian bytes, no allocation.
    return intx::be::unsafe::load<intx::uint256>(be.data());
}

/// Convert the execution layer's OpReceiptMeta into the framework layer's
/// bcos::protocol::OpStackReceiptMeta (the typed view over the tars opStackMeta hex-string
/// fields). uint256 fields use intxToBcosU256 (full-width); uint64/uint32 scalar fields are
/// assigned directly. Presence is preserved per-field. effective_gas_price is
/// written on the receipt top-level field, not in this OP extension object.
inline bcos::protocol::OpStackReceiptMeta toOpStackMeta(const OpReceiptMeta& meta)
{
    bcos::protocol::OpStackReceiptMeta out;
    if (meta.l1_gas_price)
        out.l1_gas_price = intxToBcosU256(*meta.l1_gas_price);
    if (meta.l1_fee)
        out.l1_fee = intxToBcosU256(*meta.l1_fee);
    if (meta.l1_blob_base_fee)
        out.l1_blob_base_fee = intxToBcosU256(*meta.l1_blob_base_fee);
    if (meta.l1_base_fee_scalar)
        out.l1_base_fee_scalar =
            *meta.l1_base_fee_scalar;  // uint32 -> uint64 scalar, direct assignment
    if (meta.l1_blob_base_fee_scalar)
        out.l1_blob_base_fee_scalar = *meta.l1_blob_base_fee_scalar;
    if (meta.operator_fee_scalar)
        out.operator_fee_scalar = *meta.operator_fee_scalar;
    if (meta.operator_fee_constant)
        out.operator_fee_constant = *meta.operator_fee_constant;
    if (meta.da_footprint_gas_scalar)
        out.da_footprint_gas_scalar = *meta.da_footprint_gas_scalar;
    if (meta.da_footprint)
        out.da_footprint = *meta.da_footprint;
    if (meta.l1_gas_used)
        out.l1_gas_used = *meta.l1_gas_used;
    if (meta.operator_fee)
        out.operator_fee = intxToBcosU256(*meta.operator_fee);
    return out;
}

/// FISCO status convention: 0 == success (precompiled contracts / BlockExecutive.cpp precedent).
/// evmc_status_code's finer-grained failure taxonomy collapses to a single non-zero code — the
/// OP receipt consensus encoding carries exactly this 0/1 (encodeReceiptLeaf reads
/// status() == 0), unlike the L1 receipt which keeps the finer TransactionStatus codes.
inline int32_t toFiscoOpStatus(evmc_status_code status) noexcept
{
    return status == EVMC_SUCCESS ? 0 : 1;
}

/// Contract-creation address for a top-level transaction, in the FISCO receipt's string form
/// (40-char lowercase hex, NO "0x" — the RPC layer adds "0x" + checksum). Mirrors op-geth
/// state_processor.go MakeReceipt: a creation tx (to == nil) records
/// `CreateAddress(from, nonce)` where nonce is the sender's pre-execution account nonce —
/// tx.Nonce() for a normal tx, statedb.GetNonce(from) (i.e. preNonce) for a Regolith+ deposit.
/// contractAddress is an RPC-only field: it is NOT part of the OP consensus receipt RLP (status /
/// cumGas / bloom / logs / deposit nonce+version), so filling it never affects receiptsRoot or
/// stateRoot.
inline std::string toFiscoContractAddress(const evmc::address& sender, uint64_t nonce)
{
    const auto addr = eth::evm::compute_create_address(sender, nonce);
    // evmc::address::bytes is a plain uint8_t[20]; brace-init a bytes_view.
    return std::string(evmc::hex({addr.bytes, sizeof(addr.bytes)}));
}
}  // namespace bcos::executor_v1::opstack
