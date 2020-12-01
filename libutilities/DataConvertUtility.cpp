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
 * @file DataConvertUtility.cpp
 */

#include "DataConvertUtility.h"
#include <random>

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

std::string bcos::escaped(std::string const& _s, bool _all)
{
    static const map<char, char> prettyEscapes{{'\r', 'r'}, {'\n', 'n'}, {'\t', 't'}, {'\v', 'v'}};
    std::string ret;
    ret.reserve(_s.size() + 2);
    ret.push_back('"');
    for (auto i : _s)
        if (i == '"' && !_all)
            ret += "\\\"";
        else if (i == '\\' && !_all)
            ret += "\\\\";
        else if (prettyEscapes.count(i) && !_all)
        {
            ret += '\\';
            ret += prettyEscapes.find(i)->second;
        }
        else if (i < ' ' || _all)
        {
            ret += "\\x";
            ret.push_back("0123456789abcdef"[(uint8_t)i / 16]);
            ret.push_back("0123456789abcdef"[(uint8_t)i % 16]);
        }
        else
            ret.push_back(i);
    ret.push_back('"');
    return ret;
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
