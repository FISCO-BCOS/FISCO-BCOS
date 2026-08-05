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
 * @file OpHeaderCodec.cpp
 * @date 2026-08-05
 */
#include "OpHeaderCodec.h"
#include "RLPDecode.h"
#include "RLPEncode.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace bcos::codec::rlp
{
namespace
{
// Field readers for decodeOpHeader. Local to this TU on purpose (final review B4-1/B4-2 boundary):
// RLPDecode.h's generic overloads have other consumers whose behaviour must not change, and these
// are deliberately STRICTER than they are. Names are `op`-prefixed so they do not collide with the
// retired EthBlockHeader.cpp's helpers while both files coexist in the UNITY_BUILD TU
// (bcos-codec/CMakeLists.txt:34); that file is deleted in Task 2.

Error::UniquePtr opExpectString(bytesRef& in, Header& header)
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

/// Fixed-width field: the payload must be EXACTLY N bytes.
template <unsigned N>
Error::UniquePtr opDecodeFixed(bytesRef& in, FixedBytes<N>& out)
{
    Header header;
    if (auto error = opExpectString(in, header))
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
Error::UniquePtr opDecodeScalarBytes(
    bytesRef& in, std::size_t maxBytes, bcos::byte* out, std::size_t outSize)
{
    Header header;
    if (auto error = opExpectString(in, header))
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

Error::UniquePtr opDecodeU64(bytesRef& in, uint64_t& out)
{
    std::array<bcos::byte, sizeof(uint64_t)> buffer{};
    if (auto error = opDecodeScalarBytes(in, buffer.size(), buffer.data(), buffer.size()))
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

Error::UniquePtr opDecodeU256(bytesRef& in, u256& out)
{
    std::array<bcos::byte, 32> buffer{};
    if (auto error = opDecodeScalarBytes(in, buffer.size(), buffer.data(), buffer.size()))
    {
        return error;
    }
    out = fromBigEndian<u256>(bytesConstRef(buffer.data(), buffer.size()));
    return nullptr;
}

Error::UniquePtr opDecodeByteString(bytesRef& in, bcos::bytes& out)
{
    Header header;
    if (auto error = opExpectString(in, header))
    {
        return error;
    }
    out = in.getCroppedData(0, header.payloadLength).toBytes();
    in = in.getCroppedData(header.payloadLength);
    return nullptr;
}

/// The gas fields are u256 on the FISCO interface but u64-bounded in the OP protocol (and by the
/// engine's step-2 validation). An overflow is an internal invariant violation, never a payload
/// verdict — throwing is the loud, correct behaviour.
uint64_t opNarrowU256(u256 const& value, std::string_view what)
{
    if (value > std::numeric_limits<uint64_t>::max())
    {
        throw std::overflow_error(std::string(what) + " exceeds uint64 in encodeOpHeader");
    }
    return static_cast<uint64_t>(value);
}
}  // namespace

bcos::bytes encodeOpHeader(const bcos::protocol::BlockHeader& h, const OpHeaderConst& c)
{
    bcos::bytes out;
    // Field order is load-bearing: spec §5.1's 21-field order (== go-ethereum core/types.Header),
    // pinned byte-for-byte by the golden gate (EthBlockHeaderTest.cpp, 33 vectors). The header's
    // timestamp is tars-stored in MILLISECONDS; the RLP field is SECONDS — this /1000 is what
    // keeps the bytes identical to the golden corpus (spec §7).
    bcos::codec::rlp::encode(out, h.parentInfo().blockHash, c.ommersHash, h.coinbase(),
        h.stateRoot(), h.txsRoot(), h.receiptsRoot(), h.logsBloom(), c.difficulty,
        static_cast<uint64_t>(h.number()), opNarrowU256(h.gasLimit(), "gasLimit"),
        opNarrowU256(h.gasUsed(), "gasUsed"), static_cast<uint64_t>(h.timestamp()) / 1000,
        h.extraData(), h.prevRandao(), c.nonce, h.baseFee().value(), h.withdrawalsRoot().value(),
        opNarrowU256(h.blobGasUsed().value(), "blobGasUsed"),
        opNarrowU256(h.excessBlobGas().value(), "excessBlobGas"), h.parentBeaconBlockRoot().value(),
        h.requestsHash().value());
    return out;
}

bcos::h256 opHeaderHash(const bcos::protocol::BlockHeader& h, const OpHeaderConst& c)
{
    auto encoded = encodeOpHeader(h, c);
    return bcos::crypto::keccak256Hash(bcos::ref(encoded));
}

bcos::Error::UniquePtr decodeOpHeader(
    bcos::bytesRef in, bcos::protocol::BlockHeader& h, OpHeaderConst& c)
{
    // Same 21 fields, same order as encodeOpHeader() — the golden round-trip assertion
    // (EthBlockHeaderTest.cpp) pins the two to each other.
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

    bcos::h256 parentHash, ommersHash, stateRoot, transactionsRoot, receiptsRoot, prevRandao;
    bcos::h256 withdrawalsRoot, parentBeaconBlockRoot, requestsHash;
    bcos::Address feeRecipient;
    bcos::h2048 logsBloom;
    bcos::u256 difficulty, baseFeePerGas;
    uint64_t number = 0, gasLimit = 0, gasUsed = 0, timestamp = 0, blobGasUsed = 0,
             excessBlobGas = 0;
    bcos::bytes extraData;
    bcos::h64 nonce;

    Error::UniquePtr error;
    const auto step = [&](Error::UniquePtr fieldError) {
        if (!error)
        {
            error = std::move(fieldError);
        }
    };
    step(opDecodeFixed(in, parentHash));
    step(opDecodeFixed(in, ommersHash));
    step(opDecodeFixed(in, feeRecipient));
    step(opDecodeFixed(in, stateRoot));
    step(opDecodeFixed(in, transactionsRoot));
    step(opDecodeFixed(in, receiptsRoot));
    step(opDecodeFixed(in, logsBloom));
    step(opDecodeU256(in, difficulty));
    step(opDecodeU64(in, number));
    step(opDecodeU64(in, gasLimit));
    step(opDecodeU64(in, gasUsed));
    step(opDecodeU64(in, timestamp));
    step(opDecodeByteString(in, extraData));
    step(opDecodeFixed(in, prevRandao));
    step(opDecodeFixed(in, nonce));
    step(opDecodeU256(in, baseFeePerGas));
    step(opDecodeFixed(in, withdrawalsRoot));
    step(opDecodeU64(in, blobGasUsed));
    step(opDecodeU64(in, excessBlobGas));
    step(opDecodeFixed(in, parentBeaconBlockRoot));
    step(opDecodeFixed(in, requestsHash));
    if (error)
    {
        return error;
    }
    if (!in.empty())
    {
        return BCOS_ERROR_UNIQUE_PTR(
            DecodingError::UnexpectedListElements, "Block header RLP has extra fields");
    }

    // Write the 18 tars-carried fields back through the interface; the 3 constants have no carrier
    // and return via `c`. `parentInfo` is the single-parent carrier: the RLP carries only the
    // parent hash, and the parent height is not in the RLP. `blockNumber = number - 1` is the
    // consistent convention with `rebuildOpEthHeader` (审查 F1:decode 必须与生产者同语义,否则
    // round-trip 后 parentInfo.blockNumber 会漂移;golden 全是 block 1,N-1=0,测试断言它)。
    h.setParentInfo(bcos::protocol::ParentInfo{
        .blockNumber = static_cast<bcos::protocol::BlockNumber>(number) - 1,
        .blockHash = parentHash});
    h.setCoinbase(feeRecipient);
    h.setStateRoot(stateRoot);
    h.setTxsRoot(transactionsRoot);
    h.setReceiptsRoot(receiptsRoot);
    h.setLogsBloom(bytesConstRef(logsBloom.data(), logsBloom.size()));
    h.setNumber(static_cast<bcos::protocol::BlockNumber>(number));
    h.setGasLimit(bcos::u256(gasLimit));
    h.setGasUsed(bcos::u256(gasUsed));
    // RLP seconds -> tars milliseconds (spec §7).
    h.setTimestamp(static_cast<int64_t>(timestamp) * 1000);
    h.setExtraData(std::move(extraData));
    h.setPrevRandao(prevRandao);
    h.setBaseFee(baseFeePerGas);
    h.setWithdrawalsRoot(withdrawalsRoot);
    h.setBlobGasUsed(bcos::u256(blobGasUsed));
    h.setExcessBlobGas(bcos::u256(excessBlobGas));
    h.setParentBeaconBlockRoot(parentBeaconBlockRoot);
    h.setRequestsHash(requestsHash);
    c = OpHeaderConst{.ommersHash = ommersHash, .difficulty = difficulty, .nonce = nonce};
    return nullptr;
}

}  // namespace bcos::codec::rlp
