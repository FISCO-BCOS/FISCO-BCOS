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
 * @file Common.h
 * @author: kyonGuo
 * @date 2024/4/7
 */

#pragma once
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <concepts/bcos-concepts/Basic.h>
#include <utility>
#include <vector>

// Note:https://ethereum.org/en/developers/docs/data-structures-and-encoding/rlp/
namespace bcos::codec::rlp
{
/// based on length threshold, the head of the rlp encoding is different
/// if the length of the payload is less than 56, the head is 0x80 + length
/// if the length of the payload is greater than or equal to 56, the head is 0xb7 +
/// lengthBytes.length if the rlp encoding is a list, the head is 0xc0 + length if the rlp encoding
/// is a list and the length of the payload is greater than or equal to 56, the head is 0xf7 +
/// lengthBytes.length
constexpr static uint8_t LENGTH_THRESHOLD{0x38};      // 56
constexpr static uint8_t BYTES_HEAD_BASE{0x80};       // 128
constexpr static uint8_t LONG_BYTES_HEAD_BASE{0xb7};  // 183
constexpr static uint8_t LIST_HEAD_BASE{0xc0};        // 192
constexpr static uint8_t LONG_LIST_HEAD_BASE{0xf7};   // 247
template <typename T>
concept UnsignedIntegral =
    std::unsigned_integral<T> || std::same_as<T, u160> || std::same_as<T, u256>;

struct Header
{
    bool isList{false};
    size_t payloadLength{0};
};

// Error codes for RLP
enum [[nodiscard]] DecodingError : int32_t
{
    Overflow,
    LeadingZero,
    InputTooShort,
    InputTooLong,
    NonCanonicalSize,
    UnexpectedLength,
    UnexpectedString,
    UnexpectedList,
    UnexpectedListElements,
    InvalidVInSignature,         // v != 27 && v != 28 && v < 35, see EIP-155
    UnsupportedTransactionType,  // EIP-2718
    InvalidFieldset,
    UnexpectedEip2718Serialization,
    InvalidHashesLength,  // trie::Node decoding
    InvalidMasksSubsets,  // trie::Node decoding
};

inline size_t lengthOfLength(std::unsigned_integral auto payloadLength) noexcept
{
    if (payloadLength < LENGTH_THRESHOLD)
    {
        return 1;
    }
    else
    {
        auto significantBytes =
            (sizeof(payloadLength) * 8 - std::countl_zero(payloadLength) + 7) / 8;
        return 1 + significantBytes;
    }
}
// get the length of the rlp encoding
inline size_t length(bytesConstRef const& bytes) noexcept
{
    size_t len = bytes.size();
    if (bytes.size() != 1 || bytes[0] >= BYTES_HEAD_BASE)
    {
        len += lengthOfLength(bytes.size());
    }
    return len;
}

inline size_t length(std::unsigned_integral auto n) noexcept
{
    if (n < BYTES_HEAD_BASE)
    {
        return 1;
    }
    else
    {
        // Note: significant bytes=(bit size - leading 0 bit size)/ 8, for round down, plus 7
        // example: 64bit uint 0x01, there is 63 bit of 0, so the significantBytes is 1
        auto significantBytes = (sizeof(n) * 8 - std::countl_zero(n) + 7) / 8;
        const size_t n_bytes{significantBytes};
        return n_bytes + lengthOfLength(n_bytes);
    }
}

template <typename T>
inline size_t length(T const& n) noexcept
    requires(std::convertible_to<T, bigint> && !std::integral<T>)
{
    if (n < BYTES_HEAD_BASE)
    {
        return 1;
    }
    size_t word = n.backend().internal_limb_count;
    for (; word > 0; --word)
    {
        if (n.backend().limbs()[word - 1] != 0)
        {
            break;
        }
    }
    if (word != 0)
    {
        auto significantBytes =
            (64u - std::countl_zero(n.backend().limbs()[word - 1]) + 7) / 8u + (word - 1) * 8;
        const size_t n_bytes{significantBytes};
        return n_bytes + lengthOfLength(n_bytes);
    }
    return 1;
}

inline size_t length(bool) noexcept
{
    return 1;
}

inline size_t length(bcos::concepts::StringLike auto const& bytes) noexcept
{
    return length(bytesConstRef((const byte*)bytes.data(), bytes.size()));
}

inline size_t length(const bcos::bytes& v) noexcept
{
    return length(bcos::bytesConstRef(v.data(), v.size()));
}

template <unsigned N>
inline size_t length(const bcos::FixedBytes<N>& v) noexcept
{
    return length(bcos::bytesConstRef(v.data(), v.size()));
}

template <typename T>
    requires(!std::same_as<std::remove_cvref_t<T>, bcos::byte>)
inline size_t length(const std::vector<T>& v) noexcept;

template <typename T>
inline size_t lengthOfItems(const std::span<const T>& v) noexcept
{
    return std::accumulate(
        v.begin(), v.end(), size_t{0}, [](size_t sum, const T& x) { return sum + length(x); });
}

template <typename T>
inline size_t length(const std::span<const T>& v) noexcept
{
    const size_t payload_length = lengthOfItems(v);
    return lengthOfLength(payload_length) + payload_length;
}

template <typename T>
inline size_t lengthOfItems(const std::vector<T>& v) noexcept
{
    return lengthOfItems(std::span<const T>{v.data(), v.size()});
}

template <typename T>
    requires(!std::same_as<std::remove_cvref_t<T>, bcos::byte>)
inline size_t length(const std::vector<T>& v) noexcept
{
    return length(std::span<const T>{v.data(), v.size()});
}

template <typename Arg1, typename Arg2>
inline size_t lengthOfItems(const Arg1& arg1, const Arg2& arg2) noexcept
{
    return length(arg1) + length(arg2);
}

template <typename Arg1, typename Arg2, typename... Args>
inline size_t lengthOfItems(const Arg1& arg1, const Arg2& arg2, const Args&... args) noexcept
{
    return length(arg1) + lengthOfItems(arg2, args...);
}

template <typename Arg1, typename Arg2, typename... Args>
inline size_t length(const Arg1& arg1, const Arg2& arg2, const Args&... args) noexcept
{
    const size_t payload_length = lengthOfItems(arg1, arg2, args...);
    return lengthOfLength(payload_length) + payload_length;
}

}  // namespace bcos::codec::rlp
