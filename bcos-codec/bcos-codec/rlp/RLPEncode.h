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
 * @file RLPEncode.h
 * @author: kyonGuo
 * @date 2024/4/7
 */


#pragma once
#include "Common.h"
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <concepts/bcos-concepts/Basic.h>

#include <utility>
#include <vector>

// THANKS TO: RLP implement based on silkworm: https://github.com/erigontech/silkworm.git
// Note:https://ethereum.org/en/developers/docs/data-structures-and-encoding/rlp/
namespace bcos::codec::rlp
{

inline void encodeHeader(bcos::bytes& to, Header const& header) noexcept
{
    if (header.payloadLength < LENGTH_THRESHOLD)
    {
        // if the length of the payload is less than 56, the head's first byte is 0x80 + length
        const uint8_t head =
            header.payloadLength + (header.isList ? LIST_HEAD_BASE : BYTES_HEAD_BASE);
        to.push_back(head);
    }
    else
    {
        // if the length of the payload is greater than or equal to 56, the head's first byte is
        // 0xb7 + lengthBytes.length, and the following bytes are the lengthBytes
        // lengthBytes is the the length of the payload, in big-endian bytes way
        auto&& lengthBytes = bcos::toCompactBigEndian(header.payloadLength);
        const uint8_t head =
            (header.isList ? LONG_LIST_HEAD_BASE : LONG_BYTES_HEAD_BASE) + lengthBytes.size();
        to.push_back(head);
        to.insert(to.end(), lengthBytes.begin(), lengthBytes.end());
    }
}

// encode single byte
template <typename T>
concept UnsignedByte = UnsignedIntegral<T> || std::same_as<T, unsigned char>;
inline void encode(bcos::bytes& to, UnsignedByte auto const& b) noexcept
{
    if (b == 0)
    {
        to.push_back(BYTES_HEAD_BASE);
        return;
    }
    if (b < BYTES_HEAD_BASE)
    {
        to.push_back(static_cast<bcos::byte>(b));
    }
    else
    {
        auto&& be = toCompactBigEndian(b);
        encodeHeader(to, {.isList = false, .payloadLength = be.size()});
        to.insert(to.end(), be.begin(), be.end());
    }
}

// encode the bytes into rlp encoding
inline void encode(bcos::bytes& to, bytesConstRef const& bytes) noexcept
{
    if (bytes.size() != 1 || bytes[0] >= BYTES_HEAD_BASE)
    {
        encodeHeader(to, {.isList = false, .payloadLength = bytes.size()});
    }
    to.insert(to.end(), bytes.begin(), bytes.end());
}

inline void encode(bcos::bytes& to, bcos::concepts::StringLike auto const& bytes) noexcept
{
    encode(to, bytesConstRef{(const byte*)bytes.data(), bytes.size()});
}

inline void encode(bcos::bytes& to, bcos::bytes const& in) noexcept
{
    encode(to, bytesConstRef{in.data(), in.size()});
}

template <unsigned N>
inline void encode(bcos::bytes& to, bcos::FixedBytes<N> const& in) noexcept
{
    encode(to, bytesConstRef{in.data(), in.size()});
}

template <typename T>
inline void encodeItems(bcos::bytes& to, const std::span<const T>& v) noexcept;

template <typename T>
inline void encodeItems(bcos::bytes& to, const std::vector<T>& v) noexcept
{
    encodeItems(to, std::span<const T>{v.data(), v.size()});
}

template <typename T>
inline void encode(bcos::bytes& to, const std::span<const T>& v) noexcept
{
    const Header h{.isList = true, .payloadLength = lengthOfItems(v)};
    to.reserve(to.size() + lengthOfLength(h.payloadLength) + h.payloadLength);
    encodeHeader(to, h);
    encodeItems(to, v);
}

// only for list
template <typename T>
    requires(!std::same_as<std::remove_cvref_t<T>, bcos::byte>)
inline void encode(bcos::bytes& to, const std::vector<T>& v) noexcept
{
    encode(to, std::span<const T>{v.data(), v.size()});
}

template <typename Arg1, typename Arg2>
inline void encodeItems(bcos::bytes& to, const Arg1& arg1, const Arg2& arg2) noexcept
{
    encode(to, arg1);
    encode(to, arg2);
}

template <typename Arg1, typename Arg2, typename... Args>
inline void encodeItems(
    bcos::bytes& to, const Arg1& arg1, const Arg2& arg2, const Args&... args) noexcept
{
    encode(to, arg1);
    encodeItems(to, arg2, args...);
}

template <typename Arg1, typename Arg2, typename... Args>
inline void encode(
    bcos::bytes& to, const Arg1& arg1, const Arg2& arg2, const Args&... args) noexcept
{
    const Header h{.isList = true, .payloadLength = lengthOfItems(arg1, arg2, args...)};
    to.reserve(to.size() + lengthOfLength(h.payloadLength) + h.payloadLength);
    encodeHeader(to, h);
    encodeItems(to, arg1, arg2, args...);
}

template <typename T>
inline void encodeItems(bcos::bytes& to, const std::span<const T>& v) noexcept
{
    for (auto& x : v)
    {
        encode(to, x);
    }
}
}  // namespace bcos::codec::rlp
