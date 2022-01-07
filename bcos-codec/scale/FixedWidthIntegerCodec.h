/*
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @brief codec for the integer with fixed length
 * @file FixedWidthIntegerCodec.h
 */
#pragma once
#include "Common.h"
#include <boost/endian/arithmetic.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>
namespace bcos
{
namespace codec
{
namespace scale
{
/**
 * encodeInteger encodes any integer type to little-endian representation
 * @tparam T integer type
 * @tparam S output stream type
 * @param value integer value
 * @return byte array representation of value
 */
// Note: rust scale use u128 encoded as FixedWidthInteger(We don't support u128 now)
template <class T, class S, typename I = std::decay_t<T>,
    typename = std::enable_if_t<std::is_integral<I>::value>>
void encodeInteger(T value, S& out)
{  // no need to take integers by &&
    constexpr size_t size = sizeof(I);
    constexpr size_t bits = size * 8;
    boost::endian::endian_buffer<boost::endian::order::little, I, bits> buf{};
    buf = value;  // cannot initialize, only assign
    for (size_t i = 0; i < size; ++i)
    {
        out << buf.data()[i];
    }
}

/**
 * @brief decodeInteger function decodes integer from stream
 * @tparam T integer type
 * @param stream source stream
 * @return decoded value or error
 */
template <class T, class S, typename I = std::decay_t<T>,
    typename = std::enable_if_t<std::is_integral_v<I>>>
I decodeInteger(S& stream)
{
    constexpr size_t size = sizeof(I);
    static_assert(size <= 8);
    // sign bit = 2^(num_bits - 1)
    static constexpr std::array<uint64_t, 8> sign_bit = {
        0x80,               // 1 byte
        0x8000,             // 2 bytes
        0x800000,           // 3 bytes
        0x80000000,         // 4 bytes
        0x8000000000,       // 5 bytes
        0x800000000000,     // 6 bytes
        0x80000000000000,   // 7 bytes
        0x8000000000000000  // 8 bytes
    };

    static constexpr std::array<uint64_t, 8> multiplier = {
        0x1,               // 2^0
        0x100,             // 2^8
        0x10000,           // 2^16
        0x1000000,         // 2^24
        0x100000000,       // 2^32
        0x10000000000,     // 2^40
        0x1000000000000,   // 2^48
        0x100000000000000  // 2^56
    };
    if (!stream.hasMore(size))
    {
        BOOST_THROW_EXCEPTION(ScaleDecodeException()
                              << errinfo_comment("decodeInteger exception for NOT_ENOUGH_DATA"));
    }

    // get integer as 4 bytes from little-endian stream
    // and represent it as native-endian unsigned int eger
    uint64_t v = 0u;

    for (size_t i = 0; i < size; ++i)
    {
        v += multiplier[i] * static_cast<uint64_t>(stream.nextByte());
    }
    // now we have uint64 native-endian value
    // which can be signed or unsigned under the cover

    // if it is unsigned, we know that is not greater than max value for type T
    // so static_cast<T>(v) is safe

    // if it is signed, but positive it is also ok
    // we can be sure that it is less than max_value<T>/2
    // to check whether is is negative we check if the sign bit present
    // in unsigned form it means that value is more than
    // a value 2^(bits_number-1)
    bool is_positive_signed = v < sign_bit[size - 1];
    if (std::is_unsigned<I>() || is_positive_signed)
    {
        return static_cast<I>(v);
    }

    // T is signed integer type and the value v is negative
    // value is negative signed means ( - x )
    // where x is positive unsigned < sign_bits[size-1]
    // find this x, safely cast to signed and negate result
    // the bitwise negation operation affects higher bits as well
    // but it doesn't spoil the result
    // static_cast to smaller size cuts them off
    T sv = -static_cast<I>((~v) + 1);

    return sv;
}
}  // namespace scale
}  // namespace codec
}  // namespace bcos