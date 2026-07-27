#pragma once

#include <bcos-evm/eth/state/transaction.hpp>
#include <cstdint>
#include <evmc/bytes.hpp>
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

OpReceiptMeta deriveOpReceiptMeta(const OpForkConfig& cfg, const OpFeeParams& fee, uint32_t flzLen,
    intx::uint256 l1_cost, intx::uint256 operator_fee_at_used, bool fill_operator_scalars) noexcept;
}  // namespace bcos::evm::opstack
