/**
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @file DepositTransaction.cpp
 * @brief OP Stack 0x7E deposit transaction: raw-bytes decoding and geth-shaped JSON
 */

#include "DepositTransaction.h"
#include <bcos-codec/rlp/Common.h>
#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-crypto/ChecksumAddress.h>
#include <bcos-crypto/hash/Keccak256.h>

using namespace bcos;
using namespace bcos::codec::rlp;

bcos::Error::UniquePtr bcos::rpc::decodeDepositTransaction(
    bcos::bytesRef& in, DepositTransaction& out) noexcept
{
    // The RLP layer reports errors by throwing RlpDecodeException; this function keeps its
    // Error::UniquePtr interface, so the whole body is wrapped and the exception's code
    // (errinfo_rlpErrorCode) and message (errinfo_comment) are folded back into an Error.
    try
    {
        if (in.empty() || in[0] != c_depositTxType)
        {
            return BCOS_ERROR_UNIQUE_PTR(
                UnexpectedEip2718Serialization, "Not a 0x7e deposit transaction envelope");
        }
        in = in.getCroppedData(1);
        auto header = decodeHeader(in);
        if (!header.isList)
        {
            return BCOS_ERROR_UNIQUE_PTR(
                UnexpectedString, "Deposit transaction body must be a list");
        }
        if (header.payloadLength > in.size())
        {
            return BCOS_ERROR_UNIQUE_PTR(InputTooShort, "Deposit transaction body too short");
        }
        bytesRef body(in.data(), header.payloadLength);

        decodeItems(body, out.sourceHash, out.from);
        // `to`: empty RLP item = contract creation (same convention as every Ethereum tx type).
        if (body.empty())
        {
            return BCOS_ERROR_UNIQUE_PTR(InputTooShort, "Deposit transaction missing to field");
        }
        if (body[0] == BYTES_HEAD_BASE)
        {
            out.to = std::nullopt;
            body = body.getCroppedData(1);
        }
        else
        {
            Address to{};
            decode(body, to);
            out.to.emplace(to);
        }
        // `mint`: empty RLP item = no mint. op-geth encodes a nil *big.Int as the empty item
        // and decodes the empty item back to nil — on the wire nil and zero are the same
        // (both encode to 0x80), so nullopt here matches op-geth's decode-side behavior.
        if (body.empty())
        {
            return BCOS_ERROR_UNIQUE_PTR(InputTooShort, "Deposit transaction missing mint field");
        }
        if (body[0] == BYTES_HEAD_BASE)
        {
            out.mint = std::nullopt;
            body = body.getCroppedData(1);
        }
        else
        {
            u256 mint{0};
            decode(body, mint);
            out.mint.emplace(mint);
        }
        // isSystemTx decodes as an integer: RLP-canonical false is the EMPTY item (0x80,
        // zero-length payload — op-geth encodes Go bools that way), which the shared
        // decode(bool&) rejects because it requires exactly one payload byte.
        uint64_t isSystemTxValue = 0;
        decodeItems(body, out.value, out.gas, isSystemTxValue, out.input);
        out.isSystemTx = isSystemTxValue != 0;
        if (!body.empty())
        {
            return BCOS_ERROR_UNIQUE_PTR(
                UnexpectedListElements, "Trailing bytes in deposit transaction body");
        }
        in = in.getCroppedData(header.payloadLength);
        return nullptr;
    }
    catch (RlpDecodeException const& e)
    {
        auto const* code = boost::get_error_info<errinfo_rlpErrorCode>(e);
        auto const* msg = boost::get_error_info<errinfo_comment>(e);
        return BCOS_ERROR_UNIQUE_PTR(code != nullptr ? *code : UnexpectedEip2718Serialization,
            msg != nullptr ? *msg : "RLP decode failed");
    }
}

void bcos::rpc::combineDepositTxResponse(Json::Value& result, const DepositTransaction& deposit)
{
    result["type"] = toQuantity(static_cast<uint64_t>(c_depositTxType));
    result["sourceHash"] = deposit.sourceHash.hexPrefixed();
    auto from = deposit.from.hex();
    toChecksumAddress(from, bcos::crypto::keccak256Hash(bcos::bytesConstRef(from)).hex());
    result["from"] = "0x" + std::move(from);
    if (deposit.to.has_value())
    {
        auto to = deposit.to->hex();
        toChecksumAddress(to, bcos::crypto::keccak256Hash(bcos::bytesConstRef(to)).hex());
        result["to"] = "0x" + std::move(to);
    }
    else
    {
        result["to"] = Json::nullValue;
    }
    result["gas"] = toQuantity(deposit.gas);
    result["value"] = toQuantity(deposit.value);
    result["input"] = toHexStringWithPrefix(deposit.input);
    if (deposit.mint.has_value())
    {
        // op-geth omits mint when nil and emits it when present (json:"mint,omitempty").
        result["mint"] = toQuantity(*deposit.mint);
    }
    if (deposit.isSystemTx)
    {
        // op-geth emits isSystemTx only when true.
        result["isSystemTx"] = true;
    }
    // Deposits carry no nonce (the deposit nonce lives in the receipt), no gas price and
    // no signature; op-geth emits zero quantities for these.
    result["nonce"] = "0x0";
    result["gasPrice"] = "0x0";
    result["v"] = "0x0";
    result["r"] = "0x0";
    result["s"] = "0x0";
}
