/**
 *  Copyright (C) 2022 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file Web3Transaction.cpp
 * @author: kyonGuo
 * @date 2024/4/8
 */

#include "bcos-rlp-protocol/Web3Transaction.h"
#include "bcos-rlp-protocol/Web3TxHandler.h"
#include "bcos-utilities/Common.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-framework/protocol/Transaction.h>
#include <bcos-utilities/DataConvertUtility.h>  // bcos::fromBigEndian
#include <limits>
#include <range/v3/algorithm/find_if.hpp>
#include <range/v3/algorithm/move.hpp>
#include <utility>

namespace bcos
{
namespace
{
// EIP-2 canonical-s guard: s > n/2 is "malleable" — flipping s -> n - s recovers the same
// sender, so op-geth rejects it at both admission and block processing. Threshold shared via
// Secp256k1Crypto.h (c_secp256k1n / c_secp256k1nOver2).

/// Returns a decode error if r/s fall outside EIP-2's valid range (r,s in [1, n-1], s <= n/2),
/// else nullptr. r/s are the raw 32-byte big-endian scalars (already zero-padded by the handler).
bcos::Error::UniquePtr checkEip2Signature(
    bcos::bytes const& signatureR, bcos::bytes const& signatureS)
{
    // Width gate first: padSignature only zero-pads shorter input, and fromBigEndian truncates
    // wider input — without this a 33-byte 0x00||r would pass the range check below on truncation,
    // where op-geth rejects >256-bit scalars at RLP decode.
    if (signatureR.size() > 32 || signatureS.size() > 32)
    {
        return BCOS_ERROR_UNIQUE_PTR(codec::rlp::DecodingError::InvalidVInSignature,
            "EIP-2: invalid signature (r/s wider than 32 bytes)");
    }
    // r/s are raw 32-byte big-endian scalars — decode in place instead of round-tripping through a
    // "0x"+hex string + u256 parse (4 heap allocations per tx on the shared decode funnel).
    const u256 r = bcos::fromBigEndian<u256>(signatureR);
    const u256 s = bcos::fromBigEndian<u256>(signatureS);
    if (r == 0 || r >= bcos::crypto::c_secp256k1n || s == 0 || s >= bcos::crypto::c_secp256k1n ||
        s > bcos::crypto::c_secp256k1nOver2)
    {
        return BCOS_ERROR_UNIQUE_PTR(codec::rlp::DecodingError::InvalidVInSignature,
            "EIP-2: invalid signature (r/s out of [1,n-1], or s exceeds secp256k1n/2 — "
            "malleable signature)");
    }
    return nullptr;
}
}  // namespace

namespace rpc
{
// These three using-declarations are NOT dead: they are the ADL bridge that lets the generic
// codec templates (RLPEncode.h encodeItems/Common.h lengthOfItems/RLPDecode.h decodeItems) find
// the AccessListEntry/Web3Transaction overloads defined at the bottom of this file. Those overloads
// live in namespace codec::rlp, which is not an associated namespace of bcos::rpc::AccessListEntry;
// without the using-declaration the unity build fails with "neither visible in the template
// definition nor found by argument-dependent lookup". Keep the three in sync with the overloads.
using codec::rlp::decode;
using codec::rlp::encode;
using codec::rlp::length;

bcos::bytes Web3Transaction::encodeForSign() const
{
    // Delegate to handler: signing preimage (RLP without type byte, without signature)
    return handlerFor(type).encodeForSign(*this);
}

bcos::bytes Web3Transaction::encode() const
{
    // Full RLP (with type byte, typed transaction)
    return handlerFor(type).encode(*this);
}

bcos::Error::UniquePtr Web3Transaction::decode(bcos::bytesRef& in, bool withSig)
{
    if (in.empty())
    {
        return BCOS_ERROR_UNIQUE_PTR(codec::rlp::DecodingError::InputTooShort, "Input too short");
    }
    const auto firstByte = in[0];
    // A valid transaction body is always an RLP list (≥0xC0). Use >= LIST_HEAD_BASE rather than
    // >= BYTES_HEAD_BASE: the latter would misclassify 0x80-0xBF short-string headers as Legacy.
    // A typed tx's type byte (0x01-0x04) is below 0xC0 and goes through the enum_cast dispatch
    // below.
    if (firstByte >= codec::rlp::LIST_HEAD_BASE)
    {
        // Legacy: no type byte
        type = TransactionType::Legacy;
    }
    else
    {
        auto txType = magic_enum::enum_cast<TransactionType>(firstByte);
        if (!txType.has_value())
        {
            return BCOS_ERROR_UNIQUE_PTR(codec::rlp::DecodingError::UnsupportedTransactionType,
                "Unsupported transaction type");
        }
        type = txType.value();
    }
    // ⚠️ Do not pre-strip the type byte: the typed handler consumes the envelope itself (see the
    // Web3TxHandler.h decode contract); stripping it again here would skip the list header a second
    // time and fail every typed tx decode.
    auto err = handlerFor(type).decode(in, *this, withSig);
    if (err == nullptr && !in.empty())
    {
        return BCOS_ERROR_UNIQUE_PTR(
            codec::rlp::DecodingError::InputTooLong, "Trailing bytes after RLP list");
    }
    // EIP-2: reject malleable (high-s) signatures at decode time. This member is the shared funnel
    // for BOTH OP paths — eth_sendRawTransaction (EthEndpoint.cpp) and engine newPayload (the
    // decode path feeding block execution, part 5/5) — so the check covers admission AND block
    // processing, matching op-geth. Deposits (0x7e) are unsigned; the EIP-7702 authorization
    // entries are gated separately (Eip7702Recover.h) and left untouched here.
    if (err == nullptr && withSig && type != TransactionType::Deposit)
    {
        err = checkEip2Signature(signatureR, signatureS);
    }
    return err;
}

bcos::crypto::HashType Web3Transaction::txHash() const
{
    bcos::bytes encoded{};
    codec::rlp::encode(encoded, *this);
    return bcos::crypto::keccak256Hash(bcos::ref(encoded));
}

bcos::crypto::HashType Web3Transaction::hashForSign() const
{
    auto encodeForSign = this->encodeForSign();
    return bcos::crypto::keccak256Hash(bcos::ref(encodeForSign));
}

std::ostream& operator<<(std::ostream& _out, const TransactionType& _in)
{
    _out << magic_enum::enum_name(_in);
    return _out;
}
uint64_t Web3Transaction::getSignatureV() const
{
    // EIP-155: Simple replay attack protection
    if (chainId.has_value())
    {
        return chainId.value() * 2 + 35 + signatureV;
    }
    return signatureV + 27;
}
std::string Web3Transaction::sender() const
{
    // deposit (0x7e): unsigned, so sender is taken directly from the from field
    if (type == TransactionType::Deposit)
        return toHexStringWithPrefix(from);
    bcos::bytes sign{};
    sign.reserve(crypto::SECP256K1_SIGNATURE_LEN);
    sign.insert(sign.end(), signatureR.begin(), signatureR.end());
    sign.insert(sign.end(), signatureS.begin(), signatureS.end());
    sign.push_back(signatureV);
    bcos::crypto::Keccak256 hashImpl;
    auto encodeForSign = this->encodeForSign();
    auto hash = bcos::crypto::keccak256Hash(ref(encodeForSign));
    const bcos::crypto::Secp256k1Crypto signatureImpl;
    auto [_, addr] = signatureImpl.recoverAddress(hashImpl, hash, ref(sign));
    return toHexStringWithPrefix(addr);
}
std::string Web3Transaction::toString() const noexcept
{
    std::stringstream stringstream{};
    // sender() runs EC recovery, which throws InvalidSignature when the (r,s) pair is not on
    // the curve — legal input for a tx that only passed the range checks at decode. A display
    // helper must not let that escape the noexcept border (std::terminate at the TRACE log
    // call site in EthEndpoint).
    std::string senderText;
    try
    {
        senderText = this->sender();
    }
    catch (const std::exception&)
    {
        senderText = "unrecoverable";
    }
    stringstream << " chainId: " << this->chainId.value_or(0) << " hash:" << this->txHash().hex()
                 << " type: " << static_cast<uint16_t>(this->type)
                 << " to: " << this->to.value_or(Address()).hex() << " data: " << toHex(this->data)
                 << " value: " << this->value << " nonce: " << this->nonce
                 << " gasLimit: " << this->gasLimit
                 << " maxPriorityFeePerGas: " << this->maxPriorityFeePerGas
                 << " maxFeePerGas: " << this->maxFeePerGas
                 << " maxFeePerBlobGas: " << this->maxFeePerBlobGas
                 << " blobVersionedHashes: " << this->blobVersionedHashes
                 << " sender: " << senderText << " signatureR: " << toHex(this->signatureR)
                 << " signatureS: " << toHex(this->signatureS)
                 << " signatureV: " << this->signatureV;
    return stringstream.str();
}
}  // namespace rpc

namespace codec::rlp
{
using namespace bcos::rpc;
size_t length(Web3Transaction const& tx) noexcept
{
    auto head = handlerFor(tx.type).header(tx);
    auto len = lengthOfLength(head.payloadLength) + head.payloadLength;
    len = (tx.type == TransactionType::Legacy) ? len : lengthOfLength(len + 1) + len + 1;
    return len;
}
void encode(bcos::bytes& out, const Web3Transaction& tx) noexcept
{
    // Append semantics like every other codec::rlp::encode(out, x) overload — encodeItems /
    // list-encoding chains depend on it, and an overwrite would silently drop previously
    // encoded elements. The handler returns a complete buffer: move it when out is empty
    // (the current callers) to avoid a copy, otherwise append.
    auto encoded = handlerFor(tx.type).encode(tx);
    if (out.empty())
    {
        out = std::move(encoded);
    }
    else
    {
        out.insert(out.end(), encoded.begin(), encoded.end());
    }
}

bcos::Error::UniquePtr decode(bcos::bytesRef& in, AuthorizationListEntry& out) noexcept
{
    // Each authorization entry is itself an RLP list:
    // [chain_id, address, nonce, y_parity, r, s]
    auto&& [error, header] = decodeHeader(in);
    if (error != nullptr)
    {
        return std::move(error);
    }
    if (!header.isList)
    {
        return BCOS_ERROR_UNIQUE_PTR(DecodingError::UnexpectedString, "Unexpected string");
    }
    const uint64_t leftover{in.size() - header.payloadLength};
    u256 chainId = 0;
    if (auto e = decodeItems(in, chainId, out.address); e != nullptr)
    {
        return e;
    }
    uint64_t nonce = 0;
    if (auto e = decode(in, nonce); e != nullptr)
    {
        return e;
    }
    if (auto e = decode(in, out.yParity); e != nullptr)
    {
        return e;
    }
    if (auto e = decode(in, out.r); e != nullptr)
    {
        return e;
    }
    if (auto e = decode(in, out.s); e != nullptr)
    {
        return e;
    }
    if (in.size() != leftover)
    {
        return BCOS_ERROR_UNIQUE_PTR(
            DecodingError::UnexpectedListElements, "Unexpected list elements");
    }
    out.chainId = chainId;
    out.nonce = nonce;
    return nullptr;
}
bcos::Error::UniquePtr decode(bcos::bytesRef& in, Web3Transaction& out) noexcept
{
    return out.decode(in, true);
}

bcos::Error::UniquePtr decodeFromPayload(bcos::bytesRef& in, rpc::Web3Transaction& out) noexcept
{
    return out.decode(in, false);
}

bcos::Error::UniquePtr decodeTransaction(
    bcos::bytesRef& in, rpc::Web3Transaction& out, bool withSignature) noexcept
{
    // Kept as the entry point (the call target of decodeOpEnvelope/decodeOpEnvelopeWithSig,
    // EthEndpoint.cpp:73); it now delegates to the member function so the new handler dispatch
    // path is used.
    return out.decode(in, withSignature);
}
}  // namespace codec::rlp
}  // namespace bcos
