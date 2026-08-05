#pragma once

#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-evm/eth/state/transaction.hpp>
#include <cstdint>
#include <evmc/bytes.hpp>
#include <evmc/evmc.hpp>
#include <intx/intx.hpp>
#include <optional>

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
    std::optional<uint64_t> l1_gas_used;  // Fjord+; wire index 11
    // operator (Isthmus+)
    std::optional<intx::uint256> operator_fee;    // FISCO extension: actually-charged value
                                                  // (op-geth receipt has no such field)
    std::optional<uint32_t> operator_fee_scalar;  // filled only when (scalar != 0 || constant != 0)
    std::optional<uint64_t> operator_fee_constant;
    // DA footprint (Jovian+; op-geth receipt BlobGasUsed semantics)
    std::optional<uint64_t> da_footprint_gas_scalar;
    std::optional<uint64_t> da_footprint;
    // Effective gas price (base_fee + priority_gas_price). NOT serialized into opReceiptMeta —
    // carried on the tars effectiveGasPrice base field instead (op-geth api.go:1775, RPC layer
    // top-level output).
    std::optional<intx::uint256> effective_gas_price;
};

struct OpTxReceipt
{
    evmone::state::TransactionReceipt receipt;
    OpReceiptMeta meta;
};

OpReceiptMeta deriveOpReceiptMeta(const OpForkConfig& cfg, const OpFeeParams& fee, uint32_t flzLen,
    intx::uint256 l1_cost, intx::uint256 operator_fee_at_used, bool fill_operator_scalars) noexcept;

/// Convert an intx::uint256 to its trimmed big-endian byte form (op-geth hexutil.Big semantics:
/// no leading zeros, zero -> empty bytes).
inline bcos::bytes trimBigEndian(intx::uint256 value)
{
    auto be = intx::be::store<evmc::uint256be>(value);
    size_t first = 0;
    while (first < sizeof(be.bytes) && be.bytes[first] == 0)
    {
        ++first;
    }
    return bcos::bytes(be.bytes + first, be.bytes + sizeof(be.bytes));
}

}  // namespace bcos::evm::opstack
