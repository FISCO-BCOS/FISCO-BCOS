/*
 *  Copyright (C) 2020 FISCO BCOS.
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
 * @file JsonDataConvertUtility.h
 */

#pragma once
#include "DataConvertUtility.h"
#include "Exceptions.h"
#include "FixedBytes.h"
#include <string>

namespace bcos
{
inline std::string toJonString(byte _data)
{
    return "0x" + std::to_string(_data);
}
inline std::string toJonString(bytes const& _bytesData)
{
    return toHexStringWithPrefix(_bytesData);
}
template <unsigned N>
std::string toJonString(boost::multiprecision::number<boost::multiprecision::cpp_int_backend<N, N,
        boost::multiprecision::unsigned_magnitude, boost::multiprecision::unchecked, void>> const&
        _arithNumber)
{
    auto hexString = toHexString(toCompactBigEndian(_arithNumber, 1));
    return "0x" + ((*hexString)[0] == '0' ? (hexString->substr(1)) : *hexString);
}

template <unsigned T>
std::string toJonString(SecureFixedBytes<T> const& _i)
{
    std::stringstream stream;
    stream << "0x" << _i.makeInsecure().hex();
    return stream.str();
}

template <typename T>
std::string toJonString(T const& _i)
{
    std::stringstream stream;
    stream << "0x" << std::hex << _i;
    return stream.str();
}

enum class OnFailed
{
    InterpretRaw,
    Empty,
    Throw
};

/// Convert string to byte array. Input parameter is hex, optionally prefixed by "0x".
/// Returns empty array if invalid input.
bytes jonStringToBytes(std::string const& _stringData, OnFailed _action = OnFailed::Throw);

template <unsigned N>
FixedBytes<N> jsToFixed(std::string const& _s)
{
    if (_s.substr(0, 2) == "0x")
        // Hex
        return FixedBytes<N>(_s.substr(2 + std::max<unsigned>(N * 2, _s.size() - 2) - N * 2));
    else if (_s.find_first_not_of("0123456789") == std::string::npos)
        // Decimal
        return (typename FixedBytes<N>::ArithType)(_s);
    else
        // Binary
        return FixedBytes<N>();
}

template <unsigned N>
boost::multiprecision::number<boost::multiprecision::cpp_int_backend<N * 8, N * 8,
    boost::multiprecision::unsigned_magnitude, boost::multiprecision::unchecked, void>>
jonStringToInt(std::string const& _s)
{
    // Hex
    if (_s.substr(0, 2) == "0x")
    {
        return fromBigEndian<boost::multiprecision::number<boost::multiprecision::cpp_int_backend<
            N * 8, N * 8, boost::multiprecision::unsigned_magnitude,
            boost::multiprecision::unchecked, void>>>(*fromHexString(_s.substr(2)));
    }
    // Decimal
    else if (_s.find_first_not_of("0123456789") == std::string::npos)
    {
        return boost::multiprecision::number<boost::multiprecision::cpp_int_backend<N * 8, N * 8,
            boost::multiprecision::unsigned_magnitude, boost::multiprecision::unchecked, void>>(_s);
    }
    else
    {
        BOOST_THROW_EXCEPTION(
            bcos::BadCast() << errinfo_comment("can't convert " + _s + " to int"));
    }
}

inline u256 jonStringToU256(std::string const& _s)
{
    return jonStringToInt<32>(_s);
}

/// Convert a string representation of a number to an int
/// String can be a normal decimal number, or a hex prefixed by 0x or 0X, or an octal if prefixed by
/// 0 Returns 0 in case of failure
inline int64_t jonStringToInt(std::string const& _s)
{
    return int64_t(jonStringToInt<8>(_s));
}
}  // namespace bcos
