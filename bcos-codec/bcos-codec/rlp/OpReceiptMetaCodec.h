// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
#pragma once

// OP Stack (Isthmus/Jovian) receipt metadata codec — serializes the fields an OP receipt's RPC
// response carries beyond the ETH basics (op-geth ethapi.MarshalReceipt, api.go:1744-1830) into
// one byte string stored on bcos::protocol::TransactionReceipt::opReceiptMeta() (tars
// `opReceiptMeta`).
//
// Scope is deliberately narrow: only the fields op-geth emits for OP blocks (see the wire list
// below). `operatorFee` (the actually-charged value, OpReceiptMeta::operator_fee) is a FISCO
// extension with no op-geth receipt field, and `l1GasUsed` was deprecated as of Fjord — both are
// deliberately NOT serialized here.
//
// Format: RLP list [ presence(uint16), v0, v1, ... ] — a presence bitmask followed by only the
// present fields, in fixed order. Presence (not the value) distinguishes "0" from "absent", the
// same nil semantics op-geth's `if receipt.X != nil` uses. uint256 fields (l1GasPrice/l1Fee/
// l1BlobBaseFee) travel as big-endian bytes (leading zeros trimmed, matching hexutil.Big);
// scalar fields as plain uint64 RLP integers.
//
// This header lives in bcos-codec (not bcos-evm) because both the OP execution layer (which
// encodes) and bcos-rpc (which decodes for output) link `codec`; the functions take/return plain
// scalar/bytes values rather than bcos::evm::opstack::OpReceiptMeta, keeping the codec free of an
// evmone dependency.

#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <optional>
#include <vector>

#include "Common.h"
#include "RLPDecode.h"
#include "RLPEncode.h"

namespace bcos::codec::rlp
{

/// Wire field set, in serialization order. Mirrors op-geth MarshalReceipt's OP fields:
///   1. l1GasPrice       (hexutil.Big)      — meta.l1_gas_price
///   2. l1Fee            (hexutil.Big)      — meta.l1_fee
///   3. l1BlobBaseFee    (hexutil.Big)      — meta.l1_blob_base_fee
///   4. l1BaseFeeScalar  (hexutil.Uint64)   — meta.l1_base_fee_scalar
///   5. l1BlobBaseFeeScalar (hexutil.Uint64) — meta.l1_blob_base_fee_scalar
///   6. operatorFeeScalar   (hexutil.Uint64) — meta.operator_fee_scalar
///   7. operatorFeeConstant (hexutil.Uint64) — meta.operator_fee_constant
///   8. daFootprintGasScalar (hexutil.Uint64) — meta.da_footprint_gas_scalar
///   9. blobGasUsed       (hexutil.Uint64)  — meta.da_footprint (Jovian reuses blobGasUsed)
///  10. depositNonce     (hexutil.Uint64)   — OpDepositReceipt::deposit_nonce
///  11. depositReceiptVersion (hexutil.Uint64) — OpDepositReceipt::deposit_receipt_version
struct OpReceiptMetaFields
{
    std::optional<bcos::bytes> l1_gas_price;      // big-endian, trimmed
    std::optional<bcos::bytes> l1_fee;            // big-endian, trimmed
    std::optional<bcos::bytes> l1_blob_base_fee;  // big-endian, trimmed
    std::optional<uint64_t> l1_base_fee_scalar;
    std::optional<uint64_t> l1_blob_base_fee_scalar;
    std::optional<uint64_t> operator_fee_scalar;
    std::optional<uint64_t> operator_fee_constant;
    std::optional<uint64_t> da_footprint_gas_scalar;
    std::optional<uint64_t> da_footprint;
    std::optional<uint64_t> deposit_nonce;
    std::optional<uint64_t> deposit_receipt_version;
};

inline constexpr size_t kOpReceiptMetaFieldCount = 11;

/// Encode the field set into the opReceiptMeta byte string. The RLP list always has exactly
/// `kOpReceiptMetaFieldCount + 1` items (presence mask first) — a fixed shape, so decode never
/// has to guess.
inline bcos::bytes encodeOpReceiptMeta(OpReceiptMetaFields const& fields)
{
    // Presence bitmask: bit i set == field i present (order as in the struct above).
    uint16_t presence = 0;
    std::vector<bcos::bytes> present;
    present.reserve(kOpReceiptMetaFieldCount);
    auto push = [&](size_t idx, auto const& opt) {
        if (opt.has_value())
        {
            presence = static_cast<uint16_t>(presence | (uint16_t{1} << idx));
            present.push_back(opt.value());
        }
    };
    // uint64 scalar -> bytes big-endian so all present fields are uniform bcos::bytes.
    auto pushU64 = [&](size_t idx, std::optional<uint64_t> const& opt) {
        if (opt.has_value())
        {
            presence = static_cast<uint16_t>(presence | (uint16_t{1} << idx));
            present.push_back(toCompactBigEndian(opt.value()));
        }
    };
    push(0, fields.l1_gas_price);
    push(1, fields.l1_fee);
    push(2, fields.l1_blob_base_fee);
    pushU64(3, fields.l1_base_fee_scalar);
    pushU64(4, fields.l1_blob_base_fee_scalar);
    pushU64(5, fields.operator_fee_scalar);
    pushU64(6, fields.operator_fee_constant);
    pushU64(7, fields.da_footprint_gas_scalar);
    pushU64(8, fields.da_footprint);
    pushU64(9, fields.deposit_nonce);
    pushU64(10, fields.deposit_receipt_version);

    // Items: presence mask first (as an RLP integer), then each present field as bytes.
    bcos::bytes payload;
    bcos::codec::rlp::encode(payload, static_cast<uint64_t>(presence));
    for (auto const& f : present)
    {
        bcos::codec::rlp::encode(payload, bcos::bytesConstRef{f.data(), f.size()});
    }
    Header h{.isList = true, .payloadLength = payload.size()};
    bcos::bytes out;
    encodeHeader(out, h);
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

/// Decode an opReceiptMeta byte string. Returns nullptr on success, or a DecodingError on
/// malformed input (wrong list shape, presence mask out of range, trailing bytes).
inline bcos::Error::UniquePtr decodeOpReceiptMeta(bcos::bytesConstRef in, OpReceiptMetaFields& out)
{
    out = OpReceiptMetaFields{};
    bcos::bytesRef from{const_cast<bcos::byte*>(in.data()), in.size()};
    auto&& [listError, listHeader] = decodeHeader(from);
    if (listError)
    {
        return std::move(listError);
    }
    if (!listHeader.isList)
    {
        return BCOS_ERROR_UNIQUE_PTR(
            DecodingError::UnexpectedString, "opReceiptMeta must be a list");
    }
    if (listHeader.payloadLength != from.size())
    {
        return BCOS_ERROR_UNIQUE_PTR(
            DecodingError::InputTooLong, "opReceiptMeta list header does not match its payload");
    }

    uint64_t presence = 0;
    if (auto e = bcos::codec::rlp::decode(from, presence))
    {
        return e;
    }
    if (presence >= (uint64_t{1} << kOpReceiptMetaFieldCount))
    {
        return BCOS_ERROR_UNIQUE_PTR(
            DecodingError::UnexpectedListElements, "opReceiptMeta presence mask out of range");
    }
    auto takeField = [&](size_t idx, std::optional<bcos::bytes>& dst) -> bcos::Error::UniquePtr {
        if (presence & (uint64_t{1} << idx))
        {
            bcos::bytes value;
            if (auto e = bcos::codec::rlp::decode(from, value))
            {
                return e;
            }
            dst = std::move(value);
        }
        return nullptr;
    };
    auto takeU64 = [&](size_t idx, std::optional<uint64_t>& dst) -> bcos::Error::UniquePtr {
        if (presence & (uint64_t{1} << idx))
        {
            bcos::bytes value;
            if (auto e = bcos::codec::rlp::decode(from, value))
            {
                return e;
            }
            dst = fromBigEndian<uint64_t>(bcos::bytesConstRef{value.data(), value.size()});
        }
        return nullptr;
    };
    if (auto e = takeField(0, out.l1_gas_price))
        return e;
    if (auto e = takeField(1, out.l1_fee))
        return e;
    if (auto e = takeField(2, out.l1_blob_base_fee))
        return e;
    if (auto e = takeU64(3, out.l1_base_fee_scalar))
        return e;
    if (auto e = takeU64(4, out.l1_blob_base_fee_scalar))
        return e;
    if (auto e = takeU64(5, out.operator_fee_scalar))
        return e;
    if (auto e = takeU64(6, out.operator_fee_constant))
        return e;
    if (auto e = takeU64(7, out.da_footprint_gas_scalar))
        return e;
    if (auto e = takeU64(8, out.da_footprint))
        return e;
    if (auto e = takeU64(9, out.deposit_nonce))
        return e;
    if (auto e = takeU64(10, out.deposit_receipt_version))
        return e;
    if (!from.empty())
    {
        return BCOS_ERROR_UNIQUE_PTR(
            DecodingError::InputTooLong, "opReceiptMeta has trailing bytes after its fields");
    }
    return nullptr;
}

}  // namespace bcos::codec::rlp
