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
 * @file EthBlockHeader.cpp
 * @date 2026-07-28
 */

#include "EthBlockHeader.h"
#include "RLPDecode.h"
#include "RLPEncode.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <cstring>

namespace bcos::codec::rlp
{
namespace
{
// Field readers for `EthBlockHeader::decode`. They are local to this translation unit rather than
// added to RLPDecode.h on purpose (final review B4-1/B4-2 boundary): RLPDecode.h's generic
// overloads have other consumers whose behaviour must not change, and these are deliberately
// STRICTER than they are — see the header comment on `decode`.

Error::UniquePtr expectString(bytesRef& in, Header& header)
{
    auto&& [error, decoded] = decodeHeader(in);
    if (error)
    {
        return std::move(error);
    }
    if (decoded.isList)
    {
        return BCOS_ERROR_UNIQUE_PTR(DecodingError::UnexpectedList, "Unexpected list");
    }
    header = decoded;
    return nullptr;
}

/// Fixed-width field: the payload must be EXACTLY N bytes. The generic `decode(bytesRef&,
/// FixedBytes<N>&)` accepts any length and silently pads/truncates, which for a header field means
/// silently reading a different hash/address than the bytes say.
template <unsigned N>
Error::UniquePtr decodeFixed(bytesRef& in, FixedBytes<N>& out)
{
    Header header;
    if (auto error = expectString(in, header))
    {
        return error;
    }
    if (header.payloadLength != N)
    {
        return BCOS_ERROR_UNIQUE_PTR(
            DecodingError::UnexpectedLength, "Fixed-width header field has the wrong length");
    }
    std::memcpy(out.data(), in.data(), N);
    in = in.getCroppedData(N);
    return nullptr;
}

/// Canonical unsigned scalar: no leading zero byte, and no wider than `MaxBytes`.
Error::UniquePtr decodeScalarBytes(
    bytesRef& in, std::size_t maxBytes, bcos::byte* out, std::size_t outSize)
{
    Header header;
    if (auto error = expectString(in, header))
    {
        return error;
    }
    if (header.payloadLength > maxBytes)
    {
        return BCOS_ERROR_UNIQUE_PTR(
            DecodingError::Overflow, "Header scalar field is wider than its type");
    }
    if (header.payloadLength > 0 && in[0] == 0)
    {
        return BCOS_ERROR_UNIQUE_PTR(
            DecodingError::LeadingZero, "Header scalar field has a leading zero byte");
    }
    std::memset(out, 0, outSize);
    std::memcpy(out + (outSize - header.payloadLength), in.data(), header.payloadLength);
    in = in.getCroppedData(header.payloadLength);
    return nullptr;
}

Error::UniquePtr decodeU64(bytesRef& in, uint64_t& out)
{
    std::array<bcos::byte, sizeof(uint64_t)> buffer{};
    if (auto error = decodeScalarBytes(in, buffer.size(), buffer.data(), buffer.size()))
    {
        return error;
    }
    out = 0;
    for (auto byteValue : buffer)
    {
        out = (out << 8U) | byteValue;
    }
    return nullptr;
}

Error::UniquePtr decodeU256(bytesRef& in, u256& out)
{
    std::array<bcos::byte, 32> buffer{};
    if (auto error = decodeScalarBytes(in, buffer.size(), buffer.data(), buffer.size()))
    {
        return error;
    }
    out = fromBigEndian<u256>(bytesConstRef(buffer.data(), buffer.size()));
    return nullptr;
}

Error::UniquePtr decodeByteString(bytesRef& in, bcos::bytes& out)
{
    Header header;
    if (auto error = expectString(in, header))
    {
        return error;
    }
    out = in.getCroppedData(0, header.payloadLength).toBytes();
    in = in.getCroppedData(header.payloadLength);
    return nullptr;
}
}  // namespace

bcos::bytes EthBlockHeader::encode() const
{
    bcos::bytes out;
    // Field order is load-bearing: it is the spec §5.1 21-field order (== go-ethereum
    // core/types.Header field order), verified by manual RLP walkthrough against
    // golden.encodedHeaderHex (see task-3-report.md). MUST be fully qualified:
    // EthBlockHeader::encode() is itself a member named `encode`, and inside a member
    // function body ordinary unqualified lookup finds the class's own member first —
    // that hides (not overloads-with) the free `bcos::codec::rlp::encode(...)` template
    // set from RLPEncode.h, so an unqualified call here binds to the 0-arg member and
    // fails to compile ("too many arguments", found the hard way in build verification).
    bcos::codec::rlp::encode(out, parentHash, ommersHash, feeRecipient, stateRoot, transactionsRoot,
        receiptsRoot, logsBloom, difficulty, number, gasLimit, gasUsed, timestamp, extraData,
        prevRandao, nonce, baseFeePerGas, withdrawalsRoot, blobGasUsed, excessBlobGas,
        parentBeaconBlockRoot, requestsHash);
    return out;
}

bcos::h256 EthBlockHeader::hash() const
{
    auto encoded = encode();
    return bcos::crypto::keccak256Hash(bcos::ref(encoded));
}

bcos::Error::UniquePtr EthBlockHeader::decode(bcos::bytesRef in)
{
    // Same 21 fields, same order as encode() — the two must be read side by side; a field added
    // to one and not the other is caught by the golden round-trip assertion in
    // bcos-evm/test/opstack/EthBlockHeaderTest.cpp (decode(golden.encodedHeaderHex) must
    // reproduce the struct that encode()s back to those exact bytes).
    auto&& [listError, listHeader] = decodeHeader(in);
    if (listError)
    {
        return std::move(listError);
    }
    if (!listHeader.isList)
    {
        return BCOS_ERROR_UNIQUE_PTR(
            DecodingError::UnexpectedString, "Block header must be an RLP list");
    }
    if (listHeader.payloadLength != in.size())
    {
        // Trailing bytes after the list, or a list header claiming less than it was given.
        return BCOS_ERROR_UNIQUE_PTR(DecodingError::InputTooLong,
            "Block header RLP has trailing bytes after the field list");
    }

    Error::UniquePtr error;
    const auto step = [&](Error::UniquePtr fieldError) {
        if (!error)
        {
            error = std::move(fieldError);
        }
    };
    step(decodeFixed(in, parentHash));
    step(decodeFixed(in, ommersHash));
    step(decodeFixed(in, feeRecipient));
    step(decodeFixed(in, stateRoot));
    step(decodeFixed(in, transactionsRoot));
    step(decodeFixed(in, receiptsRoot));
    step(decodeFixed(in, logsBloom));
    step(decodeU256(in, difficulty));
    step(decodeU64(in, number));
    step(decodeU64(in, gasLimit));
    step(decodeU64(in, gasUsed));
    step(decodeU64(in, timestamp));
    step(decodeByteString(in, extraData));
    step(decodeFixed(in, prevRandao));
    step(decodeFixed(in, nonce));
    step(decodeU256(in, baseFeePerGas));
    step(decodeFixed(in, withdrawalsRoot));
    step(decodeU64(in, blobGasUsed));
    step(decodeU64(in, excessBlobGas));
    step(decodeFixed(in, parentBeaconBlockRoot));
    step(decodeFixed(in, requestsHash));
    if (error)
    {
        return error;
    }
    if (!in.empty())
    {
        return BCOS_ERROR_UNIQUE_PTR(
            DecodingError::UnexpectedListElements, "Block header RLP has extra fields");
    }
    return nullptr;
}

}  // namespace bcos::codec::rlp
