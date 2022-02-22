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
 * @brief scale encoder
 * @file ScaleEncoderStream.h
 */
#include "ScaleEncoderStream.h"
#include "Common.h"

using namespace bcos;
using namespace bcos::codec::scale;

// must not use these functions outside encodeInteger
inline void encodeFirstCategory(uint8_t value, ScaleEncoderStream& out)
{
    // only values from [0, kMinUint16) can be put here
    out << static_cast<uint8_t>(value << 2u);
}

inline void encodeSecondCategory(uint16_t value, ScaleEncoderStream& out)
{
    // only values from [kMinUint16, kMinUint32) can be put here
    auto v = value;
    v <<= 2u;  // v *= 4
    v += 1u;   // set 0b01 flag
    auto minor_byte = static_cast<uint8_t>(v & 0xFFu);
    v >>= 8u;
    auto major_byte = static_cast<uint8_t>(v & 0xFFu);

    out << minor_byte << major_byte;
}

inline void encodeThirdCategory(uint32_t value, ScaleEncoderStream& out)
{
    // only values from [kMinUint32, kMinBigInteger) can be put here
    uint32_t v = (value << 2u) + 2;
    encodeInteger<uint32_t>(v, out);
}

/**
 * @brief compact-encodes CompactInteger
 * @param value source CompactInteger value
 */
void encodeCompactInteger(const CompactInteger& value, ScaleEncoderStream& out)
{
    // cannot encode negative numbers
    // there is no description how to encode compact negative numbers
    if (value < 0)
    {
        BOOST_THROW_EXCEPTION(ScaleEncodeException() << errinfo_comment(
                                  "encodeCompactInteger exception for NEGATIVE_COMPACT_INTEGER"));
    }

    if (value < EncodingCategoryLimits::kMinUint16)
    {
        encodeFirstCategory(value.convert_to<uint8_t>(), out);
        return;
    }

    if (value < EncodingCategoryLimits::kMinUint32)
    {
        encodeSecondCategory(value.convert_to<uint16_t>(), out);
        return;
    }

    if (value < EncodingCategoryLimits::kMinBigInteger)
    {
        encodeThirdCategory(value.convert_to<uint32_t>(), out);
        return;
    }

    // number of bytes required to represent value
    size_t bigIntLength = countBytes(value);
    // number of bytes to scale-encode value
    // 1 byte is reserved for header
    size_t requiredLength = 1 + bigIntLength;
    if (bigIntLength > 67)
    {
        BOOST_THROW_EXCEPTION(ScaleEncodeException() << errinfo_comment(
                                  "encodeCompactInteger exception for COMPACT_INTEGER_TOO_BIG"));
    }

    bytes result;
    result.reserve(requiredLength);
    /* The value stored in 6 major bits of header is used
     * to encode number of bytes for storing big integer.
     * Value formed by 6 bits varies from 0 to 63 == 2^6 - 1,
     * However big integer byte count starts from 4,
     * so to store this number we should decrease this value by 4.
     * And the range of bytes number for storing big integer
     * becomes 4 .. 67. To form resulting header we need to move
     * those bits representing bytes count to the left by 2 positions
     * by means of multiplying by 4.
     * Minor 2 bits store encoding option, in our case it is 0b11 == 3
     * We just add 3 to the result of operations above
     */
    uint8_t header = (bigIntLength - 4) * 4 + 3;
    result.push_back(header);
    CompactInteger v{value};
    for (size_t i = 0; i < bigIntLength; ++i)
    {
        result.push_back(static_cast<uint8_t>(v & 0xFF));  // push back least significant byte
        v >>= 8;
    }
    for (const uint8_t c : result)
    {
        out << c;
    }
}

ScaleEncoderStream& ScaleEncoderStream::operator<<(const u256& _value)
{
    // convert u256 to big-edian bytes(Note: must be 32bytes)
    bytes bigEndianData = toBigEndian(_value);
    m_stream.insert(m_stream.end(), bigEndianData.begin(), bigEndianData.end());
    return *this;
}

bytes ScaleEncoderStream::data() const
{
    bytes buffer(m_stream.size(), 0u);
    buffer.assign(m_stream.begin(), m_stream.end());
    return buffer;
}

ScaleEncoderStream& ScaleEncoderStream::operator<<(const CompactInteger& v)
{
    encodeCompactInteger(v, *this);
    return *this;
}

ScaleEncoderStream& ScaleEncoderStream::encodeOptionalBool(const boost::optional<bool>& v)
{
    auto result = OptionalBool::TrueValue;
    if (!v.has_value())
    {
        result = OptionalBool::NoneValue;
    }
    else if (!*v)
    {
        result = OptionalBool::FalseValue;
    }
    return putByte(static_cast<uint8_t>(result));
}
