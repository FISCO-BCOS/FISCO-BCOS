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
 * @file DataConvertUtility.h
 */

#pragma once

#include "Common.h"
#include "Error.h"
#include "Ranges.h"
#include <boost/algorithm/hex.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/throw_exception.hpp>
#include <algorithm>
#include <cstring>
#include <iterator>
#include <set>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <vector>

namespace bcos
{
template <class Binary, class Out = std::string>
    requires RANGES::range<Binary> && RANGES::sized_range<Binary>
Out toHex(const Binary& binary, std::string_view prefix = std::string_view())
{
    Out out;

    out.reserve(binary.size() * 2 + prefix.size());

    if (!prefix.empty())
    {
        out.insert(out.end(), prefix.begin(), prefix.end());
    }
    boost::algorithm::hex_lower(binary.begin(), binary.end(), std::back_inserter(out));
    return out;
}

template <class Out = std::string>
Out toHex(std::unsigned_integral auto number, std::string_view prefix = std::string_view())
{
    std::basic_string<byte> bytes(8, '\0');
    boost::endian::store_big_u64(bytes.data(), number);
    return toHex(bytes, prefix);
}

template <class T>
concept Binary = RANGES::contiguous_range<T>;
static std::string toQuantity(const Binary auto& binary)
{
    if (binary.empty())
    {
        return "0x0";
    }
    auto&& hex = toHex(binary);
    auto it = hex.begin();
    while ((it + 1) != hex.end())
    {
        if (*it != '0')
        {
            break;
        }
        it++;
    }
    std::string out = "0x";
    out.reserve(2 + std::distance(it, hex.end()));
    out.insert(out.end(), it, hex.end());
    return out;
}

template <class T>
concept Number = std::is_integral_v<T>;
static std::string toQuantity(Number auto number)
{
    std::basic_string<byte> bytes(8, '\0');
    boost::endian::store_big_u64(bytes.data(), number);
    return toQuantity(bytes);
}

template <class T>
concept BigNumber = !std::is_integral_v<T> && std::convertible_to<T, bigint>;
static std::string toQuantity(BigNumber auto number);

template <class Hex, class Out = bytes>
Out fromHex(const Hex& hex, std::string_view prefix = std::string_view())
{
    if (hex.empty())
    {
        BOOST_THROW_EXCEPTION(BCOS_ERROR(-1, "Empty input hex string"));
    }

    if ((hex.size() < prefix.size() + 2) || (hex.size() % 2 != 0))
    {
        // can be 0x
        if (hex == prefix)
        {
            return {};
        }
        BOOST_THROW_EXCEPTION(BCOS_ERROR(-1, "Invalid input hex string size"));
    }

    Out out;
    out.reserve(hex.size() / 2);

    boost::algorithm::unhex(hex.begin() + prefix.size(), hex.end(), std::back_inserter(out));
    return out;
}

template <class Hex, class Out = bytes>
std::optional<Out> safeFromHex(const Hex& hex, std::string_view prefix = std::string_view())
{
    try
    {
        auto out = fromHex(hex, prefix);
        return std::make_optional(out);
    }
    catch (...)
    {
        return std::nullopt;
    }
}

template <class Hex, class Out = bytes>
std::optional<Out> safeFromHexWithPrefix(const Hex& hex)
{
    return safeFromHex(hex, "0x");
}


template <class Hex, class Out = bytes>
Out fromHexWithPrefix(const Hex& hex)
{
    return fromHex(hex, "0x");
}

inline uint64_t fromQuantity(std::string const& quantity)
{
    return std::stoull(quantity, nullptr, 16);
}

inline u256 fromBigQuantity(std::string_view quantity)
{
    return hex2u(quantity);
}

/**
 * @brief convert the specified bytes data into hex string
 *
 * @tparam Iterator: the iterator type of the data
 * @param _begin : the begin pointer of the data that need to be converted to hex string
 * @param _end : the end pointer of the data that need to be converted to the hex string
 * @param _prefix: prefix of the converted hex string
 * @return std::shared_ptr<std::string> : the converted hex string
 */
template <class Iterator>
std::shared_ptr<std::string> toHexString(
    Iterator _begin, Iterator _end, std::string const& _prefix = "")
{
    static_assert(sizeof(typename std::iterator_traits<Iterator>::value_type) == 1,
        "only support byte-sized element type");
    auto hexStringSize = std::distance(_begin, _end) * 2 + _prefix.size();
    std::shared_ptr<std::string> hexString = std::make_shared<std::string>(hexStringSize, '0');
    // set the _prefix
    memcpy((void*)hexString->data(), (const void*)_prefix.data(), _prefix.size());
    static char const* hexCharsCollection = "0123456789abcdef";
    // covert the bytes into hex chars
    size_t offset = _prefix.size();
    for (auto it = _begin; it != _end; it++)
    {
        (*hexString)[offset++] = hexCharsCollection[(*it >> 4) & 0x0f];
        (*hexString)[offset++] = hexCharsCollection[*it & 0x0f];
    }
    return hexString;
}

/**
 * @brief : convert the given data to hex string(without prefix)
 *
 * @tparam T : the type of the given data
 * @param _data : the data need to be converted to hex
 * @return std::shared_ptr<std::string> : the pointer of the converted hex string
 */
template <class T>
std::shared_ptr<std::string> toHexString(T const& _data)
{
    return toHexString(_data.begin(), _data.end());
}

/**
 * @brief convert the bytes into hex string with 0x prefixed
 *
 * @tparam T : the type of data to be converted
 * @param _data : the data to be converted
 * @return std::string : the hex string
 */
template <class T>
std::string toHexStringWithPrefix(T const& _data)
{
    std::string out;
    out.reserve(_data.size() * 2 + 2);
    out = "0x";
    boost::algorithm::hex_lower(_data.begin(), _data.end(), std::back_inserter(out));

    return out;
}

/**
 * @brief convert hex string to bytes
 *
 * @param _hexedString: the hex string need to be converted
 * @return std::shared_ptr<bytes>: the converted bytes
 */
std::shared_ptr<bytes> fromHexString(std::string const& _hexedString);

/**
 * @brief determine the input string is hex string or not
 *
 * @param _string the string to be determined
 * @return true : the input string is hex string
 * @return false : the input string is not hex string
 */
bool isHexString(std::string const& _string);
bool isHexStringV2(std::string const& _string);


/// Converts byte array to a string containing the same (binary) data. Unless
/// the byte array happens to contain ASCII data, this won't be printable.
std::string asString(bytes const& _b);

/// Converts byte array ref to a string containing the same (binary) data. Unless
/// the byte array happens to contain ASCII data, this won't be printable.
std::string asString(bytesConstRef _b);

/// Converts a string to a byte array containing the string's (byte) data.
bytes asBytes(std::string const& _b);

// Big-endian to/from host endian conversion functions.

/// Converts a templated integer value to the big-endian byte-stream represented on a templated
/// collection. The size of the collection object will be unchanged. If it is too small, it will not
/// represent the value properly, if too big then the additional elements will be zeroed out.
/// @a Out will typically be either std::string or bytes.
/// @a T will typically by unsigned, u160, u256 or bigint.
template <class T, class Out>
inline void toBigEndian(T _val, Out& o_out)
{
    static_assert(std::is_same<bigint, T>::value || !std::numeric_limits<T>::is_signed,
        "only unsigned types or bigint supported");  // bigint does not carry sign bit on shift
    for (auto i = o_out.size(); i != 0; _val >>= 8, i--)
    {
        T v = _val & (T)0xff;
        o_out[i - 1] = (typename Out::value_type)(uint8_t)v;
    }
}

/// Converts a big-endian byte-stream represented on a templated collection to a templated integer
/// value.
/// @a _In will typically be either std::string or bytes.
/// @a T will typically by unsigned, u160, u256 or bigint.
template <class T, class _In>
inline T fromBigEndian(_In const& _bytes)
{
    T ret = (T)0;
    for (auto i : _bytes)
        ret = (T)((ret << 8) | (byte)(typename std::make_unsigned<decltype(i)>::type)i);
    return ret;
}

bytes toBigEndian(u256 _val);
bytes toBigEndian(u160 _val);

/// Convenience function for toBigEndian.
/// @returns a byte array just big enough to represent @a _val.
template <class T>
inline bytes toCompactBigEndian(T _val, unsigned _min = 0)
{
    static_assert(std::is_same<bigint, T>::value || !std::numeric_limits<T>::is_signed,
        "only unsigned types or bigint supported");  // bigint does not carry sign bit on shift
    unsigned i = 0;
    for (T v = _val; v; ++i, v >>= 8)
    {
    }
    bytes ret((std::max)(_min, i), 0);
    toBigEndian(_val, ret);
    return ret;
}
inline bytes toCompactBigEndian(byte _val, unsigned _min = 0);

/// Convenience function for toBigEndian.
/// @returns a string just big enough to represent @a _val.
template <class T>
inline std::string toCompactBigEndianString(T _val, unsigned _min = 0)
{
    static_assert(std::is_same<bigint, T>::value || !std::numeric_limits<T>::is_signed,
        "only unsigned types or bigint supported");  // bigint does not carry sign bit on shift
    unsigned i = 0;
    for (T v = _val; v; ++i, v >>= 8)
    {
    }
    std::string ret((std::max)(_min, i), '\0');
    toBigEndian(_val, ret);
    return ret;
}

// Algorithms for string and string-like collections.
// Concatenate two vectors of elements of POD types.
template <class T>
inline std::vector<T>& operator+=(
    std::vector<typename std::enable_if<std::is_trivial<T>::value, T>::type>& _a,
    std::vector<T> const& _b)
{
    if (!_b.empty())
    {
        auto s = _a.size();
        _a.resize(_a.size() + _b.size());
        memcpy(_a.data() + s, _b.data(), _b.size() * sizeof(T));
    }
    return _a;
}

/// Concatenate two vectors of elements.
template <class T>
inline std::vector<T>& operator+=(
    std::vector<typename std::enable_if<!std::is_trivial<T>::value, T>::type>& _a,
    std::vector<T> const& _b)
{
    _a.reserve(_a.size() + _b.size());
    for (auto& i : _b)
        _a.push_back(i);
    return _a;
}

/// Insert the contents of a container into a set
template <class T, class U>
std::set<T>& operator+=(std::set<T>& _a, U const& _b)
{
    for (auto const& i : _b)
        _a.insert(i);
    return _a;
}

/// Insert the contents of a container into an unordered_set
template <class T, class U>
std::unordered_set<T>& operator+=(std::unordered_set<T>& _a, U const& _b)
{
    for (auto const& i : _b)
        _a.insert(i);
    return _a;
}

/// Concatenate the contents of a container onto a vector
template <class T, class U>
std::vector<T>& operator+=(std::vector<T>& _a, U const& _b)
{
    for (auto const& i : _b)
        _a.push_back(i);
    return _a;
}

/// Insert the contents of a container into a set
template <class T, class U>
std::set<T> operator+(std::set<T> _a, U const& _b)
{
    return _a += _b;
}

/// Insert the contents of a container into an unordered_set
template <class T, class U>
std::unordered_set<T> operator+(std::unordered_set<T> _a, U const& _b)
{
    return _a += _b;
}

/// Concatenate the contents of a container onto a vector
template <class T, class U>
std::vector<T> operator+(std::vector<T> _a, U const& _b)
{
    return _a += _b;
}

/// Concatenate two vectors of elements.
template <class T>
inline std::vector<T> operator+(std::vector<T> const& _a, std::vector<T> const& _b)
{
    std::vector<T> ret(_a);
    return ret += _b;
}

template <class T>
inline std::ostream& operator<<(std::ostream& _out, std::vector<T> const& _e)
{
    _out << "[";
    for (auto const& element : _e)
    {
        _out << "," << element;
    }

    _out << "]";
    return _out;
}

template <class T, class U>
std::shared_ptr<std::vector<U>> convertMapToVector(std::map<T, U> const& _map)
{
    std::shared_ptr<std::vector<U>> convertedVec = std::make_shared<std::vector<U>>();
    for (auto const& it : _map)
    {
        convertedVec->push_back(it.second);
    }
    return convertedVec;
}

/// Make normal string from fixed-length string.
std::string toString(string32 const& _s);

/// Converts arbitrary value to string representation using std::stringstream.
template <class _T>
inline std::string toString(_T const& _t)
{
    std::ostringstream o;
    o << _t;
    return o.str();
}

template <>
inline std::string toString<std::string>(std::string const& _s)
{
    return _s;
}

template <>
inline std::string toString<uint8_t>(uint8_t const& _u)
{
    std::ostringstream o;
    o << static_cast<uint16_t>(_u);
    return o.str();
}

std::string toQuantity(BigNumber auto number)
{
    if (number == 0)
    {
        return "0x0";
    }
    auto bytes = toCompactBigEndian(number);
    return toQuantity(bytes);
}

}  // namespace bcos
