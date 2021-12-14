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
 * @brief scale decoder
 * @file ScaleDecoderStream.cpp
 */
#include "ScaleDecoderStream.h"
using namespace bcos;
using namespace bcos::codec::scale;

ScaleDecoderStream::ScaleDecoderStream(gsl::span<byte const> _span)
  : m_span(_span), m_currentIterator(m_span.begin()), m_currentIndex(0)
{}

boost::optional<bool> ScaleDecoderStream::decodeOptionalBool()
{
    auto byte = nextByte();
    switch (byte)
    {
    case static_cast<uint8_t>(OptionalBool::NoneValue):
        return boost::none;
        break;
    case static_cast<uint8_t>(OptionalBool::FalseValue):
        return false;
    case static_cast<uint8_t>(OptionalBool::TrueValue):
        return true;
    default:
        BOOST_THROW_EXCEPTION(ScaleDecodeException() << errinfo_comment(
                                  "decodeOptionalBool exception for unexpected value"));
    }
}

CompactInteger decodeCompactInteger(ScaleDecoderStream& stream)
{
    auto first_byte = stream.nextByte();
    const uint8_t flag = (first_byte)&0b00000011u;
    size_t number = 0u;
    switch (flag)
    {
    case 0b00u:
    {
        number = static_cast<size_t>(first_byte >> 2u);
        break;
    }
    case 0b01u:
    {
        auto second_byte = stream.nextByte();
        number = (static_cast<size_t>((first_byte)&0b11111100u) +
                     static_cast<size_t>(second_byte) * 256u) >>
                 2u;
        break;
    }
    case 0b10u:
    {
        number = first_byte;
        size_t multiplier = 256u;
        if (!stream.hasMore(3u))
        {
            // not enough data to decode integer
            BOOST_THROW_EXCEPTION(ScaleDecodeException() << errinfo_comment(
                                      "decodeOptionalBool exception for not enough data"));
        }
        for (auto i = 0u; i < 3u; ++i)
        {
            // we assured that there are 3 more bytes,
            // no need to make checks in a loop
            number += (stream.nextByte()) * multiplier;
            multiplier = multiplier << 8u;
        }
        number = number >> 2u;
        break;
    }
    case 0b11:
    {
        auto bytes_count = ((first_byte) >> 2u) + 4u;
        if (!stream.hasMore(bytes_count))
        {
            // not enough data to decode integer
            BOOST_THROW_EXCEPTION(ScaleDecodeException() << errinfo_comment(
                                      "decodeCompactInteger exception for not enough data"));
        }

        CompactInteger multiplier{1u};
        CompactInteger value = 0;
        // we assured that there are m more bytes,
        // no need to make checks in a loop
        for (auto i = 0u; i < bytes_count; ++i)
        {
            value += (stream.nextByte()) * multiplier;
            multiplier *= 256u;
        }

        return value;  // special case
    }
    default:
        BOOST_THROW_EXCEPTION(
            ScaleDecodeException() << errinfo_comment(
                "decodeCompactInteger exception for not supported flag " + std::to_string(flag)));
    }

    return CompactInteger{number};
}


bool ScaleDecoderStream::decodeBool()
{
    auto byte = nextByte();
    switch (byte)
    {
    case 0u:
        return false;
    case 1u:
        return true;
    default:
        BOOST_THROW_EXCEPTION(
            ScaleDecodeException() << errinfo_comment("decodeBool exception for UNEXPECTED_VALUE"));
    }
}

ScaleDecoderStream& ScaleDecoderStream::operator>>(CompactInteger& v)
{
    v = decodeCompactInteger(*this);
    return *this;
}

ScaleDecoderStream& ScaleDecoderStream::operator>>(std::string& v)
{
    std::vector<uint8_t> collection;
    *this >> collection;
    v.clear();
    v.append(collection.begin(), collection.end());
    return *this;
}

bool ScaleDecoderStream::hasMore(uint64_t n) const
{
    return static_cast<SizeType>(m_currentIndex + n) <= m_span.size();
}

ScaleDecoderStream& ScaleDecoderStream::operator>>(u256& v)
{
    bytes decodedBigEndianData;
    byte size = 32;
    decodedBigEndianData.resize(size);
    decodedBigEndianData.assign(m_currentIterator, m_currentIterator + size);
    m_currentIterator += size;
    m_currentIndex += size;
    v = fromBigEndian<u256>(decodedBigEndianData);
    return *this;
}