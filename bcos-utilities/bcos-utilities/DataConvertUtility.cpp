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
#include <regex>

#include "Exceptions.h"

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
    std::regex pattern("0x[0-9a-fA-F]*");
    return std::regex_match(_string, pattern);
}

std::shared_ptr<bytes> bcos::fromHexString(std::string const& _hexedString)
{
    unsigned startIndex =
        (_hexedString.size() >= 2 && _hexedString[0] == '0' && _hexedString[1] == 'x') ? 2 : 0;
    std::shared_ptr<bytes> bytesData = std::make_shared<bytes>();
    bytesData->reserve((_hexedString.size() - startIndex + 1) / 2);

    if (_hexedString.size() % 2)
    {
        int h = convertCharToHexNumber(_hexedString[startIndex++]);
        if (h == -1)
        {
            BOOST_THROW_EXCEPTION(BadHexCharacter());
        }
        bytesData->push_back(h);
    }
    for (unsigned i = startIndex; i < _hexedString.size(); i += 2)
    {
        int highValue = convertCharToHexNumber(_hexedString[i]);
        int lowValue = convertCharToHexNumber(_hexedString[i + 1]);
        if (highValue == -1 || lowValue == -1)
        {
            BOOST_THROW_EXCEPTION(BadHexCharacter());
        }
        bytesData->push_back((bcos::byte)(highValue << 4) + lowValue);
    }
    return bytesData;
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
uint64_t bcos::fromQuantity(std::string const& quantity)
{
    return std::stoull(quantity, nullptr, 16);
}
bcos::u256 bcos::fromBigQuantity(std::string_view quantity)
{
    return hex2u(quantity);
}
