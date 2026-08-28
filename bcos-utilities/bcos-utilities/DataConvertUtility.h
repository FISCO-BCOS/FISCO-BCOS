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
#include "bcos-utilities/Exceptions.h"
#include <boost/algorithm/hex.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/throw_exception.hpp>
#include <algorithm>
#include <cstring>
#include <iterator>
#include <optional>
#include <range/v3/range/concepts.hpp>
#include <range/v3/view/concat.hpp>
#include <range/v3/view/single.hpp>
#include <set>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <vector>

namespace bcos
{
DERIVE_BCOS_EXCEPTION(BadHexCharacter);
template <class Binary, class Out = std::string>
    requires ::ranges::range<Binary> && ::ranges::sized_range<Binary>
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
concept Binary = ::ranges::contiguous_range<T>;
std::string toQuantity(const Binary auto& binary)
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

u256 safeCastToU256(const concepts::StringLike auto& value)
{
    if (value.empty())
    {
        return u256{};
    }
    try
    {
        return boost::lexical_cast<u256>(value);
    }
    catch (...)
    {
        return {};
    }
}

template <class T>
concept Number = std::is_integral_v<T>;
std::string toQuantity(Number auto number)
{
    std::basic_string<byte> bytes(8, '\0');
    boost::endian::store_big_u64(bytes.data(), number);
    return toQuantity(bytes);
}

template <class T>
concept BigNumber = !std::is_integral_v<T> && std::convertible_to<T, bigint>;
std::string toQuantity(BigNumber auto number);

template <class Hex, class Out = bytes>
Out fromHex(const Hex& hex)
{
    std::string_view hexView = [&]() -> std::string_view {
        if constexpr (std::is_convertible_v<Hex, std::string_view>)
        {
            return std::string_view{hex};
        }
        else
        {
            return std::string_view{hex.data(), hex.size()};
        }
    }();

    auto payload = hexView;
    if (payload.starts_with("0x") || payload.starts_with("0X"))
    {
        payload.remove_prefix(2);
    }
    if (payload.empty())
    {
        return {};
    }

    const bool needPadding = (payload.size() % 2 != 0);

    Out out;
    out.reserve((payload.size() + (needPadding ? 1 : 0)) / 2);
    try
    {
        if (needPadding)
        {
            auto padded = ::ranges::views::concat(::ranges::views::single('0'), payload);
            boost::algorithm::unhex(
                ::ranges::begin(padded), ::ranges::end(padded), std::back_inserter(out));
        }
        else
        {
            boost::algorithm::unhex(payload.begin(), payload.end(), std::back_inserter(out));
        }
    }
    catch (...)
    {
        BOOST_THROW_EXCEPTION(BadHexCharacter());
    }

    return out;
}

template <class Hex, class Out = bytes>
std::optional<Out> safeFromHex(const Hex& hex)
{
    try
    {
        auto out = fromHex(hex);
        return std::make_optional(std::move(out));
    }
    catch (...)
    {
        return std::nullopt;
    }
}

template <class Hex, class Out = bytes>
std::optional<Out> safeFromHexWithPrefix(const Hex& hex)
{
    return safeFromHex(hex);
}

template <class Hex, class Out = bytes>
Out fromHexWithPrefix(const Hex& hex)
{
    return fromHex(hex);
}

/// Parse a hex quantity string ("0x..."/"0X...", or bare hex digits) to uint64_t.
/// Strict: no leading sign, no trailing garbage, must fit in uint64; the value must
/// be non-empty after an optional 0x/0X prefix. Throws std::invalid_argument on any
/// malformed input (unlike the previous stoull-based version, which silently stopped
/// at the first non-hex character and accepted a leading '-').
uint64_t fromQuantity(std::string const& quantity);

/// Non-throwing strict hex-quantity parser, mirroring the fromHex / safeFromHex
/// pairing above. Returns nullopt on any parse failure (empty, sign, trailing
/// garbage, or overflow) instead of throwing.
std::optional<uint64_t> safeFromQuantity(std::string_view quantity);

u256 fromBigQuantity(std::string_view quantity);

/// Strict non-throwing parser for the wide (u256) hex quantities, the safeFromQuantity
/// counterpart of fromBigQuantity. fromBigQuantity delegates to hex2u, which swallows
/// every parse failure and returns 0 — so malformed input is indistinguishable from a
/// genuine zero — and silently truncates anything wider than 32 bytes. This returns
/// nullopt instead, for an empty value (including a bare "0x"), any non-hex character,
/// or more than 64 hex digits.
std::optional<u256> safeFromBigQuantity(std::string_view quantity);

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

template <class T>
std::string toPaddingHexStringWithPrefix(size_t paddingSize, T const& _data)
{
    std::string out;
    out.reserve((paddingSize * 2) + 2);
    out = "0x";

    // Add leading zeros if needed
    if (paddingSize > _data.size())
    {
        out.append((paddingSize - _data.size()) * 2, '0');
    }

    boost::algorithm::hex_lower(_data.begin(), _data.end(), std::back_inserter(out));

    return out;
}

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

namespace detail
{
/// Matches the repo's fixed-width unsigned multiprecision types (u160/u256/u512 and
/// FixedBytes<N>::ArithType). Primary template: not one of them.
template <class T>
struct FixedWidthUnsigned
{
    static constexpr bool value = false;
    static constexpr size_t bits = 0;
};
// note: Bits must be std::size_t, matching cpp_int_backend's MinBits/MaxBits parameter type —
// with a mismatched type (e.g. unsigned) GCC correctly never selects this specialization
template <std::size_t Bits, boost::multiprecision::expression_template_option ET>
struct FixedWidthUnsigned<boost::multiprecision::number<
    boost::multiprecision::cpp_int_backend<Bits, Bits, boost::multiprecision::unsigned_magnitude,
        boost::multiprecision::unchecked, void>,
    ET>>
{
    static constexpr bool value = true;
    static constexpr size_t bits = Bits;
};

/// The limb-level fast path needs 64-bit limbs and a multi-limb fixed width: >128 bits keeps it
/// off boost's single-double_limb "trivial" backend, whose storage layout differs. Anything else
/// (bigint, builtin unsigned, 32-bit-limb builds) takes the generic shift loop.
template <class T>
concept LimbReadable = FixedWidthUnsigned<T>::value && FixedWidthUnsigned<T>::bits % 8 == 0 &&
                       FixedWidthUnsigned<T>::bits > 128 &&
                       sizeof(boost::multiprecision::limb_type) == 8;

// the specialization must recognize the repo types on every platform (the limb-size condition
// above may still veto the fast path, e.g. on 32-bit-limb builds, but a silent recognition
// failure would quietly reroute production onto the slow loop)
static_assert(FixedWidthUnsigned<u256>::bits == 256 && FixedWidthUnsigned<u160>::bits == 160 &&
                  FixedWidthUnsigned<u512>::bits == 512,
    "FixedWidthUnsigned must match the repo's fixed-width unsigned typedefs");

/// Byte @a _k (0 = least significant) of @a _val, read straight from the limb array so no
/// wide shifts run. Bytes at or above the significant limbs read as zero.
template <LimbReadable T>
inline uint8_t limbByte(T const& _val, size_t _k)
{
    using Limb = boost::multiprecision::limb_type;
    size_t limbIndex = _k / sizeof(Limb);
    if (limbIndex >= _val.backend().size())
    {
        return 0;
    }
    return (uint8_t)(_val.backend().limbs()[limbIndex] >> (8 * (_k % sizeof(Limb))));
}

/// Stores @a _val at @a out as exactly bits/8 big-endian bytes: whole-limb stores for the full
/// limbs, byte reads for a partial top limb (e.g. u160's upper 4 bytes).
template <LimbReadable T>
inline void storeBigEndian(uint8_t* out, T const& _val)
{
    using Limb = boost::multiprecision::limb_type;
    constexpr size_t valueBytes = FixedWidthUnsigned<T>::bits / 8;
    constexpr size_t fullLimbs = valueBytes / sizeof(Limb);
    auto const* limbs = _val.backend().limbs();
    size_t significant = _val.backend().size();
    for (size_t limbIndex = 0; limbIndex < fullLimbs; ++limbIndex)
    {
        Limb bigEndianLimb =
            boost::endian::native_to_big(limbIndex < significant ? limbs[limbIndex] : (Limb)0);
        std::memcpy(
            out + valueBytes - sizeof(Limb) * (limbIndex + 1), &bigEndianLimb, sizeof(Limb));
    }
    for (size_t k = fullLimbs * sizeof(Limb); k < valueBytes; ++k)
    {
        out[valueBytes - 1 - k] = limbByte(_val, k);
    }
}

/// Contiguous single-byte-element input: the shape every production caller passes (bytes,
/// std::string, std::array, C arrays, bytesConstRef). Anything else keeps the generic loop.
template <class In>
concept ContiguousByteInput = requires(In const& input) {
    std::data(input);
    std::size(input);
} && sizeof(std::remove_cvref_t<decltype(*std::data(std::declval<In const&>()))>) == 1;
}  // namespace detail

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
    if constexpr (detail::LimbReadable<T>)
    {
        size_t outSize = o_out.size();
        if constexpr (sizeof(typename Out::value_type) == 1 && requires { std::data(o_out); })
        {
            if (outSize == detail::FixedWidthUnsigned<T>::bits / 8)
            {
                detail::storeBigEndian(reinterpret_cast<uint8_t*>(std::data(o_out)), _val);
                return;
            }
        }
        for (size_t i = 0; i < outSize; ++i)
        {
            o_out[outSize - 1 - i] = (typename Out::value_type)detail::limbByte(_val, i);
        }
    }
    else
    {
        for (auto i = o_out.size(); i != 0; _val >>= 8, i--)
        {
            T v = _val & (T)0xff;
            o_out[i - 1] = (typename Out::value_type)(uint8_t)v;
        }
    }
}

/// Converts a big-endian byte-stream represented on a templated collection to a templated integer
/// value.
/// @a _In will typically be either std::string or bytes.
/// @a T will typically by unsigned, u160, u256 or bigint.
template <class T, class _In>
inline T fromBigEndian(_In const& _bytes)
{
    if constexpr (detail::LimbReadable<T> && detail::ContiguousByteInput<_In>)
    {
        using Limb = boost::multiprecision::limb_type;
        constexpr size_t valueBytes = detail::FixedWidthUnsigned<T>::bits / 8;
        constexpr size_t limbCount = (detail::FixedWidthUnsigned<T>::bits + 63) / 64;
        T ret = (T)0;
        auto& backend = ret.backend();
        backend.resize(limbCount, limbCount);
        Limb* limbs = backend.limbs();
        std::fill_n(limbs, limbCount, (Limb)0);
        auto const* input = reinterpret_cast<const uint8_t*>(std::data(_bytes));
        size_t inputSize = std::size(_bytes);
        // only the trailing min(inputSize, valueBytes) bytes contribute: the same
        // trailing-bytes-win truncation the shift-fold loop below produces on over-wide input
        size_t take = std::min(inputSize, valueBytes);
        size_t fullLimbs = take / sizeof(Limb);
        for (size_t limbIndex = 0; limbIndex < fullLimbs; ++limbIndex)
        {
            Limb bigEndianLimb;
            std::memcpy(
                &bigEndianLimb, input + inputSize - sizeof(Limb) * (limbIndex + 1), sizeof(Limb));
            limbs[limbIndex] = boost::endian::big_to_native(bigEndianLimb);
        }
        for (size_t k = fullLimbs * sizeof(Limb); k < take; ++k)
        {
            limbs[k / sizeof(Limb)] |= (Limb)input[inputSize - 1 - k] << (8 * (k % sizeof(Limb)));
        }
        backend.normalize();
        return ret;
    }
    else
    {
        T ret = (T)0;
        for (auto i : _bytes)
            ret = (T)((ret << 8) | (byte)(typename std::make_unsigned<decltype(i)>::type)i);
        return ret;
    }
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
