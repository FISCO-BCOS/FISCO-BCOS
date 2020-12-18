/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 */

/**
 * @brief Contract ABI function signature parser tool.
 * @author: octopuswang
 * @date: 2019-04-01
 */

#include "ABIParser.h"
#include <libutilities/FixedBytes.h>

using namespace std;
using namespace bcos;
using namespace bcos::protocol;
using namespace bcos::protocol::abi;

// uint<M>: unsigned integer type of M bits, 0 < M <= 256, M % 8 == 0. e.g. uint32, uint8,
// uint256.
static const std::set<std::string> setUint{"uint", "uint8", "uint16", "uint24", "uint32", "uint40",
    "uint48", "uint56", "uint64", "uint72", "uint80", "uint88", "uint96", "uint104", "uint112",
    "uint120", "uint128", "uint136", "uint144", "uint152", "uint160", "uint168", "uint176",
    "uint184", "uint192", "uint200", "uint208", "uint216", "uint224", "uint232", "uint240",
    "uint248", "uint256"};

// int<M>: two’s complement signed integer type of M bits, 0 < M <= 256, M % 8 == 0.
static const std::set<std::string> setInt{"int", "int8", "int16", "int24", "int32", "int40",
    "int48", "int56", "int64", "int72", "int80", "int88", "int96", "int104", "int112", "int120",
    "int128", "int136", "int144", "int152", "int160", "int168", "int176", "int184", "int192",
    "int200", "int208", "int216", "int224", "int232", "int240", "int248", "int256"};

// bytes<M>: binary type of M bytes, 0 < M <= 32.
static const std::set<std::string> setByteN{"bytes1", "bytes2", "bytes3", "bytes4", "bytes5",
    "bytes6", "bytes7", "bytes8", "bytes9", "bytes10", "bytes11", "bytes12", "bytes13", "bytes14",
    "bytes15", "bytes16", "bytes17", "bytes18", "bytes19", "bytes20", "bytes21", "bytes22",
    "bytes23", "bytes24", "bytes25", "bytes26", "bytes27", "bytes28", "bytes29", "bytes30",
    "bytes31", "bytes32"};

// bool: equivalent to uint8 restricted to the values 0 and 1. For computing the function
// selector, bool is used.
static const std::string strBool = "bool";
// bytes: dynamic sized byte sequence.
static const std::string strBytes = "bytes";
// bytes: dynamic sized byte sequence.
static const std::string strString = "string";
// address: equivalent to uint160, except for the assumed interpretation and language typing.
// For computing the function selector, address is used.
static const std::string strAddr = "address";

// Remove the white space characters on both sides
static void trim(std::string& _str)
{
    _str.erase(0, _str.find_first_not_of(" "));
    _str.erase(_str.find_last_not_of(" ") + 1);
}

ABI_ELEMENTARY_TYPE ABIInType::getEnumType(const std::string& _strType)
{
    auto type = ABI_ELEMENTARY_TYPE::INVALID;
    if (_strType == strBool)
    {
        type = ABI_ELEMENTARY_TYPE::BOOL;
    }
    else if (_strType == strAddr)
    {
        type = ABI_ELEMENTARY_TYPE::ADDR;
    }
    else if (_strType == strString)
    {
        type = ABI_ELEMENTARY_TYPE::STRING;
    }
    else if (_strType == strBytes)
    {
        type = ABI_ELEMENTARY_TYPE::BYTES;
    }
    else if (setUint.find(_strType) != setUint.end())
    {
        type = ABI_ELEMENTARY_TYPE::UINT;
    }
    else if (setInt.find(_strType) != setInt.end())
    {
        type = ABI_ELEMENTARY_TYPE::INT;
    }
    else if (setByteN.find(_strType) != setByteN.end())
    {
        type = ABI_ELEMENTARY_TYPE::BYTESN;
    }

    return type;
}

void ABIInType::clear()
{
    aet = ABI_ELEMENTARY_TYPE::INVALID;
    strEleType.clear();
    strType.clear();
    extents.clear();
}

bool ABIInType::reset(const std::string& _s)
{
    clear();

    std::string strType = _s;
    // eg: int[1][2][][3]
    // trim blank character
    trim(strType);
    auto firstLeftBracket = strType.find('[');
    // int
    std::string strEleType = strType.substr(0, firstLeftBracket);
    trim(strEleType);
    auto t = getEnumType(strEleType);
    // invalid solidity abi string
    if (t == ABI_ELEMENTARY_TYPE::INVALID)
    {
        return false;
    }

    //  eg : [10][2][3][]
    std::vector<std::size_t> r;
    std::string::size_type leftBracket = firstLeftBracket;
    std::string::size_type rigthBracket = 0;
    std::string::size_type length = strType.size();

    while (leftBracket < length)
    {
        auto leftBracketBak = leftBracket;
        leftBracket = strType.find('[', leftBracketBak);
        rigthBracket = strType.find(']', leftBracketBak);

        if (leftBracket == std::string::npos || rigthBracket == std::string::npos ||
            leftBracket >= rigthBracket)
        {
            // invalid format
            return false;
        }

        std::string digit = strType.substr(leftBracket + 1, rigthBracket - leftBracket - 1);
        trim(digit);
        bool ok =
            std::all_of(digit.begin(), digit.end(), [](char c) { return c >= '0' && c <= '9'; });
        if (!ok)
        {
            // invalid format
            return false;
        }

        if (digit.empty())
        {
            r.push_back(0);
        }
        else
        {
            std::size_t size = strtoul(digit.c_str(), NULL, 10);
            r.push_back(size);
        }

        leftBracket = rigthBracket + 1;
    }

    this->aet = t;
    this->extents = r;
    this->strType = strType;
    this->strEleType = strEleType;

    return true;
}

bool ABIInType::dynamic()
{
    // string or bytes
    if (aet == ABI_ELEMENTARY_TYPE::STRING || aet == ABI_ELEMENTARY_TYPE::BYTES)
    {
        return true;
    }

    // dynamic array
    auto length = rank();
    for (std::size_t i = 0; i < length; ++i)
    {
        if (extent(i + 1) == 0)
        {
            return true;
        }
    }

    return false;
}

//
bool ABIInType::removeExtent()
{
    auto length = rank();
    if (length > 0)
    {
        extents.resize(length - 1);
        return true;
    }

    return false;
}

std::vector<std::string> ABIFunc::getParamsType() const
{
    std::vector<std::string> r;
    for (auto it = allParamsType.begin(); it != allParamsType.end(); ++it)
    {
        r.push_back(it->getType());
    }

    return r;
}

// parser contract abi function signature, eg: transfer(string,string,uint256)
bool ABIFunc::parser(const std::string& _sig)
{
    auto i0 = _sig.find("(");
    auto i1 = _sig.find(")");
    // transfer(string,string,uint256)
    if ((i0 == std::string::npos) || (i1 == std::string::npos) || (i1 <= i0))
    {
        return false;
    }

    // function name , eg: transfer
    std::string strFuncName = _sig.substr(0, i0);
    trim(strFuncName);
    // parameters "string,string,uint256"
    std::string strTypes = _sig.substr(i0 + 1, i1 - i0 - 1);

    std::string sig = strFuncName + "(";

    std::vector<std::string> types;
    boost::split(types, strTypes, boost::is_any_of(","));
    ABIInType at;
    for (std::string& type : types)
    {
        trim(type);
        if (!type.empty())
        {
            sig += type;
            sig += ",";
            auto ok = at.reset(type);
            if (!ok)
            {
                // invalid format
                return false;
            }
            allParamsType.push_back(at);
            continue;
        }
    }

    if (',' == sig.back())
    {
        sig.back() = ')';
    }
    else
    {
        sig += ")";
    }

    // set function name
    this->strFuncName = strFuncName;
    // set function sigature
    this->strFuncSignature = sig;

    return true;
}
