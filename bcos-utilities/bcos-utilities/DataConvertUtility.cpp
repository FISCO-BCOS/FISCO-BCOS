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
 * @file DataConvertUtility.cpp
 */

#include "DataConvertUtility.h"
#include <boost/regex.hpp>
#include <limits>
#include <optional>
#include <stdexcept>

using namespace std;
using namespace bcos;

/**
 * @brief: convert the hex char into the hex number
 *
 * @param _hexChar: the hex char to be converted
 * @return int : the converted hex number
 */
int convertCharToHexNumber(char _hexChar)
{
    if (_hexChar >= '0' && _hexChar <= '9')
        return _hexChar - '0';
    if (_hexChar >= 'a' && _hexChar <= 'f')
        return _hexChar - 'a' + 10;
    if (_hexChar >= 'A' && _hexChar <= 'F')
        return _hexChar - 'A' + 10;
    return -1;
}

bool bcos::isHexString(string const& _string)
{
    auto it = _string.begin();
    if (_string.find("0x") == 0)
    {
        it += 2;
    }
    for (; it != _string.end(); it++)
    {
        if (convertCharToHexNumber(*it) == -1)
        {
            return false;
        }
    }
    return true;
}

bool bcos::isHexStringV2(string const& _string)
{
    // if string is null, default return true
    if (_string.length() == 0)
    {
        return true;
    }
    const static boost::regex pattern("0x[0-9a-fA-F]*");
    return boost::regex_match(_string, pattern);
}

std::string bcos::toString(string32 const& _s)
{
    std::string ret;
    for (unsigned i = 0; i < 32 && _s[i]; ++i)
        ret.push_back(_s[i]);
    return ret;
}
std::string bcos::asString(bytes const& _b)
{
    return std::string((char const*)_b.data(), (char const*)(_b.data() + _b.size()));
}
std::string bcos::asString(bytesConstRef _b)
{
    return std::string((char const*)_b.data(), (char const*)(_b.data() + _b.size()));
}
bcos::bytes bcos::asBytes(std::string const& _b)
{
    return bytes((byte const*)_b.data(), (byte const*)(_b.data() + _b.size()));
}
bcos::bytes bcos::toBigEndian(u256 _val)
{
    bytes ret(32);
    toBigEndian(_val, ret);
    return ret;
}
bcos::bytes bcos::toBigEndian(u160 _val)
{
    bytes ret(20);
    toBigEndian(_val, ret);
    return ret;
}
bcos::bytes bcos::toCompactBigEndian(byte _val, unsigned _min)
{
    return (_min || _val) ? bytes{_val} : bytes{};
}
namespace
{
// Shared strict hex-quantity core: optional 0x/0X prefix, hex digits only, no
// leading sign, no trailing garbage, must fit in uint64. Returns nullopt on any
// failure. Used by both fromQuantity (throwing) and safeFromQuantity (optional).
std::optional<uint64_t> parseStrictQuantity(std::string_view quantity)
{
    auto s = quantity;
    if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
    {
        s.remove_prefix(2);
    }
    if (s.empty())
    {
        return std::nullopt;  // empty, or a bare "0x"
    }
    uint64_t result = 0;
    for (char c : s)
    {
        int digit = convertCharToHexNumber(c);
        if (digit < 0)
        {
            // sign ('+', '-'), whitespace, or trailing garbage — reject, don't truncate.
            return std::nullopt;
        }
        if (result > (std::numeric_limits<uint64_t>::max() - static_cast<uint64_t>(digit)) / 16)
        {
            return std::nullopt;  // overflow
        }
        result = result * 16 + static_cast<uint64_t>(digit);
    }
    return result;
}
}  // namespace

std::optional<uint64_t> bcos::safeFromQuantity(std::string_view quantity)
{
    return parseStrictQuantity(quantity);
}

uint64_t bcos::fromQuantity(std::string const& quantity)
{
    if (auto value = parseStrictQuantity(quantity); value)
    {
        return *value;
    }
    BOOST_THROW_EXCEPTION(std::invalid_argument("invalid quantity: " + quantity));
}
bcos::u256 bcos::fromBigQuantity(std::string_view quantity)
{
    return hex2u(quantity);
}

std::optional<bcos::u256> bcos::safeFromBigQuantity(std::string_view quantity)
{
    auto digits = quantity;
    if (digits.size() >= 2 && digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X'))
    {
        digits.remove_prefix(2);
    }
    // 64 hex digits is the full 32-byte width of u256; hex2u truncates beyond it.
    if (digits.empty() || digits.size() > 64)
    {
        return std::nullopt;
    }
    for (char c : digits)
    {
        if (convertCharToHexNumber(c) < 0)
        {
            return std::nullopt;  // sign, whitespace, or trailing garbage
        }
    }
    return hex2u(digits);
}
