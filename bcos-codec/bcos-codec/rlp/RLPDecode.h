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
 * @file RLPDecode.h
 * @author: kyonGuo
 * @date 2024/4/7
 */

#pragma once
#include "Common.h"
#include "bcos-utilities/Error.h"
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <algorithm>
#include <cstring>
#include <limits>
#include <optional>
#include <utility>

// THANKS TO: RLP implement based on silkworm: https://github.com/erigontech/silkworm.git
// Note:https://ethereum.org/en/developers/docs/data-structures-and-encoding/rlp/
namespace bcos::codec::rlp
{

inline std::tuple<bcos::Error::UniquePtr, Header> decodeHeader(bytesRef& from) noexcept
{
    if (from.size() == 0)
    {
        return {BCOS_ERROR_UNIQUE_PTR(DecodingError::InputTooShort, "Input data is too short"),
            Header()};
    }
    Header header{.isList = false};
    const auto byte{from[0]};
    if (byte < BYTES_HEAD_BASE)
    {
        // it means single byte
        header.payloadLength = 1;
    }
    else if (byte <= LONG_BYTES_HEAD_BASE)
    {
        // it means bytes length is less than 56
        // remove first byte
        from = from.getCroppedData(1);
        header.payloadLength = byte - BYTES_HEAD_BASE;
        if (header.payloadLength == 1)
        {
            if (from.empty())
            {
                return {BCOS_ERROR_UNIQUE_PTR(InputTooShort, "Input data is too short"), Header()};
            }
            if (from[0] < 0x80)
            {
                return {BCOS_ERROR_UNIQUE_PTR(NonCanonicalSize, "NonCanonicalSize"), Header()};
            }
        }
    }
    else if (byte < LIST_HEAD_BASE)
    {
        // it means it is a long bytes, length is GE than 56
        from = from.getCroppedData(1);
        const auto lenOfLen{byte - LONG_BYTES_HEAD_BASE};
        if (std::cmp_greater(lenOfLen, from.size()))
        {
            return {BCOS_ERROR_UNIQUE_PTR(InputTooShort, "Input data is too short"), Header()};
        }
        // C1 (final review batch B): a multi-byte length prefix whose leading byte is zero is
        // non-canonical — op-geth rlp/decode.go readUint() rejects it with ErrCanonSize (the
        // `default`/size>=2 case). lenOfLen==1 (0xb8 ..) is DELIBERATELY not tightened here: it is
        // left to the `< 56` check below, exactly as op-geth's readUint `case 1` (which does not
        // check for a leading zero). Do not "complete" this to lenOfLen>=1 — that would diverge
        // from op-geth and change 0xb8 0x00's rejection reason.
        // Note (morebtcg #5429): op-geth actually has TWO decode paths — the Stream path
        // (decode.go readKind -> readUint, size==1 does not check a leading zero) and the raw path
        // (raw.go readKind -> readSize, which checks b[0]==0 for EVERY slen including slen==1).
        // Both reject 0xb8 0x00, but with different reasons (<56 vs leading-zero); since lenOfLen==1
        // with from[0]==0 is exactly payloadLength==0 < 56, this implementation's `< 56` check
        // covers both paths with identical observable behavior.
        if (lenOfLen >= 2 && from[0] == 0)
        {
            return {BCOS_ERROR_UNIQUE_PTR(
                        NonCanonicalSize, "Non-canonical length prefix: leading zero byte"),
                Header()};
        }
        // Migration note (W8): canonicality now enforced for ALL consumers of this shared decoder,
        // not just the OP path. FISCO's own encoder (RLPEncode.h) always writes a minimal length
        // prefix (round-trip tested), so only externally-supplied non-canonical bytes are affected;
        // a legacy chain that historically accepted such bytes would now reject them on replay.
        auto payloadSize =
            fromBigEndian<uint64_t, bcos::bytesConstRef>(from.getCroppedData(0, lenOfLen));
        header.payloadLength = payloadSize;
        from = from.getCroppedData(lenOfLen);
        if (header.payloadLength < 56)
        {
            return {BCOS_ERROR_UNIQUE_PTR(
                        NonCanonicalSize, "The length of the payload is less than 56"),
                Header()};
        }
    }
    else if (byte <= LONG_LIST_HEAD_BASE)
    {
        // it means it is a list, length is less than 56
        from = from.getCroppedData(1);
        header.isList = true;
        header.payloadLength = byte - LIST_HEAD_BASE;
    }
    else
    {
        // it means it is a list, length is GE than 56
        from = from.getCroppedData(1);
        header.isList = true;
        const auto lenOfLen{byte - LONG_LIST_HEAD_BASE};
        if (std::cmp_greater(lenOfLen, from.size()))
        {
            return {BCOS_ERROR_UNIQUE_PTR(DecodingError::InputTooShort, "Input data is too short"),
                Header()};
        }
        // C1 (final review batch B): long-list length prefix, same canonical rule as the
        // long-string branch above — op-geth rlp/decode.go readUint() `default` case. lenOfLen==1
        // (0xf8 ..) is left to the `< 56` check below to match op-geth's readUint `case 1`.
        if (lenOfLen >= 2 && from[0] == 0)
        {
            return {BCOS_ERROR_UNIQUE_PTR(DecodingError::NonCanonicalSize,
                        "Non-canonical length prefix: leading zero byte"),
                Header()};
        }
        auto payloadSize =
            fromBigEndian<uint64_t, bcos::bytesConstRef>(from.getCroppedData(0, lenOfLen));
        header.payloadLength = payloadSize;
        from = from.getCroppedData(lenOfLen);
        if (header.payloadLength < 56)
        {
            return {BCOS_ERROR_UNIQUE_PTR(DecodingError::NonCanonicalSize,
                        "The length of the payload is less than 56"),
                Header()};
        }
    }
    if (header.payloadLength > from.size())
    {
        return {BCOS_ERROR_UNIQUE_PTR(DecodingError::InputTooShort, "Input data is too short"),
            Header()};
    }
    return {nullptr, header};
}

inline bcos::Error::UniquePtr decode(bytesRef& from, bcos::concepts::ByteBuffer auto& to) noexcept
{
    auto&& [error, header] = decodeHeader(from);
    if (error)
    {
        return std::move(error);
    }
    if (header.isList)
    {
        return BCOS_ERROR_UNIQUE_PTR(DecodingError::UnexpectedList, "Unexpected list");
    }
    if constexpr (std::same_as<std::decay_t<decltype(to)>, bcos::bytes>)
    {
        to = from.getCroppedData(0, header.payloadLength).toBytes();
    }
    else if constexpr (std::same_as<std::decay_t<decltype(to)>, bcos::bytesRef>)
    {
        to = from.getCroppedData(0, header.payloadLength);
    }
    else if constexpr (bcos::concepts::StringLike<std::decay_t<decltype(to)>>)
    {
        to =
            from.getCroppedData(0, header.payloadLength).toStringLike<std::decay_t<decltype(to)>>();
    }
    else if constexpr (std::same_as<std::decay_t<decltype(to)>, bcos::FixedBytes<32>> ||
                       std::same_as<std::decay_t<decltype(to)>, bcos::FixedBytes<20>> ||
                       std::same_as<std::decay_t<decltype(to)>, bcos::FixedBytes<8>>)
    {
        // Fixed-size hashes/addresses must be exactly their declared size. A short or long
        // payload is malformed input — silently right-aligning (zero-padding) or truncating
        // would re-encode differently and change the keccak hash on hash-sensitive bridges.
        // NOTE: this is a behaviour change for the shared RLP codec (previously short payloads
        // were right-aligned/zero-padded, long ones truncated). Canonical inputs are unaffected;
        // all existing in-tree decode callers (Web3Transaction, MPT, ledger, tx RLP) have been
        // verified to only feed fixed-size payloads here.
        using FixedT = std::decay_t<decltype(to)>;
        if (header.payloadLength != FixedT::SIZE)
        {
            return BCOS_ERROR_UNIQUE_PTR(
                DecodingError::UnexpectedLength, "Unexpected fixed-bytes length");
        }
        to = FixedT{from.getCroppedData(0, header.payloadLength)};
    }
    else if constexpr (std::same_as<std::decay_t<decltype(to)>, std::array<bcos::byte, 256>>)
    {
        // Ethereum's logsBloom is exactly 256 bytes; anything shorter or longer is rejected
        // rather than silently padded/truncated (a padded bloom would re-encode differently
        // and change the keccak hash). Fixed-size byte blobs are left-aligned here, unlike the
        // big-endian scalar FixedBytes branches above which right-align.
        if (header.payloadLength != to.size())
        {
            return BCOS_ERROR_UNIQUE_PTR(
                DecodingError::UnexpectedLength, "Unexpected bloom length");
        }
        auto payload = from.getCroppedData(0, header.payloadLength);
        std::memcpy(to.data(), payload.data(), payload.size());
    }
    else
    {
        static_assert(!sizeof(to), "Unsupported type");
    }
    from = from.getCroppedData(header.payloadLength);
    return nullptr;
}

inline bcos::Error::UniquePtr decode(bytesRef& from, UnsignedIntegral auto& to) noexcept
{
    auto&& [error, header] = decodeHeader(from);
    if (error)
    {
        return std::move(error);
    }
    if (header.isList)
    {
        return BCOS_ERROR_UNIQUE_PTR(DecodingError::UnexpectedList, "Unexpected list");
    }
    // Reject integers wider than the target type instead of silently truncating via fromBigEndian
    // (op-geth parity). Use digits/8, NOT sizeof(T): boost u256 has sizeof 48 but 32 payload bytes.
    constexpr auto maxBytes = std::numeric_limits<std::decay_t<decltype(to)>>::digits / 8;
    if (header.payloadLength > maxBytes)
    {
        return BCOS_ERROR_UNIQUE_PTR(DecodingError::UnexpectedLength,
            "integer wider than target type");
    }
    to = fromBigEndian<std::decay_t<decltype(to)>, bcos::bytesRef>(
        from.getCroppedData(0, header.payloadLength));
    from = from.getCroppedData(header.payloadLength);
    return nullptr;
}

inline bcos::Error::UniquePtr decode(bytesRef& from, bool& to) noexcept
{
    auto&& [error, header] = decodeHeader(from);
    if (error)
    {
        return std::move(error);
    }
    if (header.isList)
    {
        return BCOS_ERROR_UNIQUE_PTR(DecodingError::UnexpectedList, "Unexpected list");
    }
    if (header.payloadLength != 1)
    {
        return BCOS_ERROR_UNIQUE_PTR(DecodingError::UnexpectedLength, "Unexpected length");
    }
    to = from[0] != 0;
    from = from.getCroppedData(1);
    return nullptr;
}

template <typename T>
    requires(!std::same_as<std::remove_cvref_t<T>, bcos::byte>)
inline bcos::Error::UniquePtr decode(bytesRef& from, std::vector<T>& to) noexcept
{
    auto&& [error, header] = decodeHeader(from);
    if (error)
    {
        return std::move(error);
    }
    if (!header.isList)
    {
        return BCOS_ERROR_UNIQUE_PTR(DecodingError::UnexpectedString, "Unexpected string");
    }
    to.clear();
    auto payloadView = from.getCroppedData(0, header.payloadLength);
    while (!payloadView.empty())
    {
        to.emplace_back();
        if (auto decodeError = decode(payloadView, to.back()); decodeError != nullptr)
        {
            return decodeError;
        }
    }
    from = from.getCroppedData(header.payloadLength);
    return nullptr;
}

// Decodes an optional element. Presence is decided by "the view is exhausted" — i.e.
// from.empty() — so this overload is only meaningful on a bytesRef already cropped to the
// current list's payload (the variadic list decode does this). Calling it on a raw buffer
// that still holds trailing data will silently treat the remaining fields as absent rather
// than reporting malformed input; keep the list-context invariant in mind.
template <typename T>
inline bcos::Error::UniquePtr decode(bytesRef& from, std::optional<T>& to) noexcept
{
    if (from.empty())
    {
        to.reset();
        return nullptr;
    }
    T value;
    if (auto decodeError = decode(from, value); decodeError != nullptr)
    {
        return decodeError;
    }
    to = std::move(value);
    return nullptr;
}

template <typename... Args>
    requires(sizeof...(Args) > 1)
inline bcos::Error::UniquePtr decodeItems(bytesRef& from, Args&... args) noexcept
{
    bcos::Error::UniquePtr decodeError;
    ((decodeError = decode(from, args)) || ...);
    if (decodeError != nullptr)
    {
        return decodeError;
    }
    return nullptr;
}

template <typename... Args>
    requires(sizeof...(Args) > 1)
inline bcos::Error::UniquePtr decode(bytesRef& from, Args&... args) noexcept
{
    auto&& [error, header] = decodeHeader(from);
    if (error)
    {
        return std::move(error);
    }
    if (!header.isList)
    {
        return BCOS_ERROR_UNIQUE_PTR(DecodingError::UnexpectedString, "Unexpected string");
    }
    // Crop to this list's payload before decoding the items: decode(std::optional) decides
    // presence by "the view is exhausted", which must mean "this list is exhausted" — not
    // "the whole buffer is consumed". Without the crop, a header nested inside a block RLP
    // would try to decode the trailing transactions as optional fields.
    auto payloadView = from.getCroppedData(0, header.payloadLength);
    if (auto decodeError = decodeItems(payloadView, args...); decodeError != nullptr)
    {
        return decodeError;
    }
    if (!payloadView.empty())
    {
        return BCOS_ERROR_UNIQUE_PTR(
            DecodingError::UnexpectedListElements, "Unexpected list elements");
    }
    from = from.getCroppedData(header.payloadLength);
    return {};
}

}  // namespace bcos::codec::rlp
