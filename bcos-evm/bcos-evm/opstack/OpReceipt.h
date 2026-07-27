#pragma once

#include <bcos-evm/eth/state/transaction.hpp>
#include <cstdint>
#include <evmc/bytes.hpp>
#include <intx/intx.hpp>
#include <optional>
#include <variant>

namespace bcos::evm::opstack
{
struct OpFeeParams;
struct OpForkConfig;

struct OpReceiptMeta
{
    // L1 passthrough (op-geth: L1GasPrice / L1BlobBaseFee / L1BaseFeeScalar / L1BlobBaseFeeScalar /
    // L1Fee)
    std::optional<intx::uint256> l1_gas_price;      // = fee.l1_base_fee
    std::optional<intx::uint256> l1_blob_base_fee;  // = fee.blob_base_fee
    std::optional<uint32_t> l1_base_fee_scalar;
    std::optional<uint32_t> l1_blob_base_fee_scalar;
    std::optional<intx::uint256> l1_fee;  // = l1_cost
    // operator (Isthmus+)
    std::optional<intx::uint256> operator_fee;    // FISCO extension: actually-charged value
                                                  // (op-geth receipt has no such field)
    std::optional<uint32_t> operator_fee_scalar;  // filled only when (scalar != 0 || constant != 0)
    std::optional<uint64_t> operator_fee_constant;
    // DA footprint (Jovian+; op-geth receipt BlobGasUsed semantics)
    std::optional<uint64_t> da_footprint_gas_scalar;
    std::optional<uint64_t> da_footprint;
};

struct OpTxReceipt
{
    evmone::state::TransactionReceipt receipt;
    OpReceiptMeta meta;
};

struct OpDepositReceipt
{
    evmone::state::TransactionReceipt receipt;
    uint64_t deposit_nonce;            // pre-execution depositor nonce
    uint64_t deposit_receipt_version;  // = 1 (Canyon+)
};

OpReceiptMeta deriveOpReceiptMeta(const OpForkConfig& cfg, const OpFeeParams& fee, uint32_t flzLen,
    intx::uint256 l1_cost, intx::uint256 operator_fee_at_used, bool fill_operator_scalars) noexcept;

/// receipts-root leaf encoding (op-geth Receipts.EncodeIndex semantics, receipt.go:568-592 --
/// note this is NOT MarshalBinary :279-288; the two deliberately differ for a receipt that
/// "has nonce, has no version", and the function-header comment :564-567 explicitly forbids
/// changing that; this module's supported surface always comes in pairs so it has no such fork,
/// yet still follows EncodeIndex).
/// deposit: 0x7E || rlp([status, cumulativeGasUsed, logsBloom, logs, depositNonce,
/// depositReceiptVersion]) (depositReceiptRLP :136-148, the last two fields come as a pair).
/// The prefix is always 0x7e rather than reading r.receipt.type -- equivalent under the module
/// invariant (runDeposit, the sole construction point, always sets kDepositTxType).
/// normal tx: delegates to evmone rlp_encode (typed raw-byte prefix + [status, cumGas, bloom,
/// logs], byte-for-byte identical to EncodeIndex for type 0/1/2/4).
[[nodiscard]] evmc::bytes encodeReceiptForRoot(const OpDepositReceipt& r);
[[nodiscard]] evmc::bytes encodeReceiptForRoot(const OpTxReceipt& r);
[[nodiscard]] evmc::bytes encodeReceiptForRoot(
    const std::variant<OpDepositReceipt, OpTxReceipt>& r);
}  // namespace bcos::evm::opstack
