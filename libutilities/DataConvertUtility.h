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
 * @file DataConvertUtility.h
 */

#pragma once

#include "Common.h"
#include <algorithm>
#include <cstring>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <vector>

namespace bcos
{
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
    return *toHexString(_data.begin(), _data.end(), "0x");
}

/// Converts a (printable) ASCII hex string into the corresponding byte stream.
/// @example fromHex("41626261") == asBytes("Abba")
/// If _throw = ThrowType::DontThrow, it replaces bad hex characters with 0's, otherwise it will
/// throw an exception.
bytes fromHex(std::string const& _hexedString);

/// @returns true if @a _s is a hex string.

/**
 * @brief determine the input string is hex string or not
 *
 * @param _string the string to be determined
 * @return true : the input string is hex string
 * @return false : the input string is not hex string
 */
bool isHexString(std::string const& _string);

/// Converts byte array to a string containing the same (binary) data. Unless
/// the byte array happens to contain ASCII data, this won't be printable.
inline std::string asString(bytes const& _b)
{
    return std::string((char const*)_b.data(), (char const*)(_b.data() + _b.size()));
}

/// Converts byte array ref to a string containing the same (binary) data. Unless
/// the byte array happens to contain ASCII data, this won't be printable.
inline std::string asString(bytesConstRef _b)
{
    return std::string((char const*)_b.data(), (char const*)(_b.data() + _b.size()));
}

/// Converts a string to a byte array containing the string's (byte) data.
inline bytes asBytes(std::string const& _b)
{
    return bytes((byte const*)_b.data(), (byte const*)(_b.data() + _b.size()));
}

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

inline bytes toBigEndian(u256 _val)
{
    bytes ret(32);
    toBigEndian(_val, ret);
    return ret;
}
inline bytes toBigEndian(u160 _val)
{
    bytes ret(20);
    toBigEndian(_val, ret);
    return ret;
}

/// Convenience function for toBigEndian.
/// @returns a byte array just big enough to represent @a _val.
template <class T>
inline bytes toCompactBigEndian(T _val, unsigned _min = 0)
{
    static_assert(std::is_same<bigint, T>::value || !std::numeric_limits<T>::is_signed,
        "only unsigned types or bigint supported");  // bigint does not carry sign bit on shift
    int i = 0;
    for (T v = _val; v; ++i, v >>= 8)
    {
    }
    bytes ret(std::max<unsigned>(_min, i), 0);
    toBigEndian(_val, ret);
    return ret;
}
inline bytes toCompactBigEndian(byte _val, unsigned _min = 0)
{
    return (_min || _val) ? bytes{_val} : bytes{};
}

/// Convenience function for toBigEndian.
/// @returns a string just big enough to represent @a _val.
template <class T>
inline std::string toCompactBigEndianString(T _val, unsigned _min = 0)
{
    static_assert(std::is_same<bigint, T>::value || !std::numeric_limits<T>::is_signed,
        "only unsigned types or bigint supported");  // bigint does not carry sign bit on shift
    int i = 0;
    for (T v = _val; v; ++i, v >>= 8)
    {
    }
    std::string ret(std::max<unsigned>(_min, i), '\0');
    toBigEndian(_val, ret);
    return ret;
}

// Algorithms for string and string-like collections.

/// Escapes a string into the C-string representation.
/// @p _all if true will escape all characters, not just the unprintable ones.
std::string escaped(std::string const& _s, bool _all = true);

/// Concatenate two vectors of elements of POD types.
template <class T>
inline std::vector<T>& operator+=(
    std::vector<typename std::enable_if<std::is_pod<T>::value, T>::type>& _a,
    std::vector<T> const& _b)
{
    auto s = _a.size();
    _a.resize(_a.size() + _b.size());
    memcpy(_a.data() + s, _b.data(), _b.size() * sizeof(T));
    return _a;
}

/// Concatenate two vectors of elements.
template <class T>
inline std::vector<T>& operator+=(
    std::vector<typename std::enable_if<!std::is_pod<T>::value, T>::type>& _a,
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

// TODO: remove all these
/// Determine bytes required to encode the given integer value. @returns 0 if @a _i is zero.
template <class T>
inline unsigned bytesRequired(T _i)
{
    static_assert(std::is_same<bigint, T>::value || !std::numeric_limits<T>::is_signed,
        "only unsigned types or bigint supported");  // bigint does not carry sign bit on shift
    unsigned i = 0;
    for (; _i != 0; ++i, _i >>= 8)
    {
    }
    return i;
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


}  // namespace bcos
