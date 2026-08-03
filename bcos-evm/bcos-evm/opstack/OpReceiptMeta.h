#pragma once

#include <bcos-codec/rlp/OpReceiptMetaCodec.h>
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

/// Serialize an OpTxReceipt's meta into the opReceiptMeta byte string (bcos-codec RLP format,
/// see OpReceiptMetaCodec.h). Never throws.
inline bcos::bytes encodeOpReceiptMeta(const OpReceiptMeta& meta)
{
    bcos::codec::rlp::OpReceiptMetaFields fields;
    if (meta.l1_gas_price)
        fields.l1_gas_price = trimBigEndian(*meta.l1_gas_price);
    if (meta.l1_fee)
        fields.l1_fee = trimBigEndian(*meta.l1_fee);
    if (meta.l1_blob_base_fee)
        fields.l1_blob_base_fee = trimBigEndian(*meta.l1_blob_base_fee);
    if (meta.l1_base_fee_scalar)
        fields.l1_base_fee_scalar = *meta.l1_base_fee_scalar;
    if (meta.l1_blob_base_fee_scalar)
        fields.l1_blob_base_fee_scalar = *meta.l1_blob_base_fee_scalar;
    if (meta.operator_fee_scalar)
        fields.operator_fee_scalar = *meta.operator_fee_scalar;
    if (meta.operator_fee_constant)
        fields.operator_fee_constant = *meta.operator_fee_constant;
    if (meta.da_footprint_gas_scalar)
        fields.da_footprint_gas_scalar = *meta.da_footprint_gas_scalar;
    if (meta.da_footprint)
        fields.da_footprint = *meta.da_footprint;
    return bcos::codec::rlp::encodeOpReceiptMeta(fields);
}

/// Serialize an OpDepositReceipt's deposit fields into the opReceiptMeta byte string.
inline bcos::bytes encodeOpDepositMeta(uint64_t deposit_nonce, uint64_t deposit_receipt_version)
{
    bcos::codec::rlp::OpReceiptMetaFields fields;
    fields.deposit_nonce = deposit_nonce;
    fields.deposit_receipt_version = deposit_receipt_version;
    return bcos::codec::rlp::encodeOpReceiptMeta(fields);
}
}  // namespace bcos::evm::opstack
