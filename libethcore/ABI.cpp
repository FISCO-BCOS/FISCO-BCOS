/*
    This file is part of cpp-ethereum.

    cpp-ethereum is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    cpp-ethereum is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file ABI.cpp
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 */

#include "ABI.h"
#include <boost/regex.hpp>

using namespace std;
using namespace dev;
using namespace dev::eth;

const std::string TypeMeta::strBool{"bool"};
const std::string TypeMeta::strBytes{"bytes"};
const std::string TypeMeta::strString{"string"};
const std::string TypeMeta::strAddr{"address"};

const std::set<std::string> TypeMeta::setUint{"uint", "uint8", "uint16", "uint24", "uint32",
    "uint40", "uint48", "uint56", "uint64", "uint72", "uint80", "uint88", "uint96", "uint108",
    "uint116", "uint124", "uint132", "uint140", "uint148", "uint156", "uint164", "uint172",
    "uint180", "uint188", "uint196", "uint204", "uint212", "uint220", "uint228", "uint236",
    "uint244", "uint252"};

const std::set<std::string> TypeMeta::setInt{"int", "int8", "int16", "int24", "int32", "int40",
    "int48", "int56", "int64", "int72", "int80", "int88", "int96", "int108", "int116", "int124",
    "int132", "int140", "int148", "int156", "int164", "int172", "int180", "int188", "int196",
    "int204", "int212", "int220", "int228", "int236", "int244", "int252"};

const std::set<std::string> TypeMeta::setByteN{"bytes1", "bytes2", "bytes3", "bytes4", "bytes5",
    "bytes6", "bytes7", "bytes8", "bytes9", "bytes10", "bytes11", "bytes12", "bytes13", "bytes14",
    "bytes15", "bytes16", "bytes17", "bytes18", "bytes19", "bytes20", "bytes21", "bytes22",
    "bytes23", "bytes24", "bytes25", "bytes26", "bytes27", "bytes28", "bytes29", "bytes30",
    "bytes31", "bytes32"};

// parser strType
bool AbiType::doParser()
{
    // eg: int[1][2][][3]
    // trim blank character
    TypeMeta::trim(strType);
    // int
    std::string strBaseType = strType.substr(0, strType.find('['));
    TypeMeta::TYPE t = TypeMeta::getTypeByStr(strBaseType);
    // invalid solidity abi string
    if (t != TypeMeta::TYPE::INVALID)
    {
        std::vector<std::size_t> result;

        boost::regex regex("\\[[0-9]*\\]");
        boost::sregex_token_iterator iter(strType.begin(), strType.end(), regex, 0);
        boost::sregex_token_iterator end;

        for (; iter != end; ++iter)
        {
            boost::regex reg("\\[[0-9]{1,}\\]");
            std::string tempStr = *iter;
            std::size_t extentSize = strtoul(tempStr.c_str() + 1, NULL, 10);
            result.push_back(extentSize);
        }

        setType(t);
        setExtents(result);
    }

    return valid();
}

bool ContractABI::abiOutByFuncSelector(
    bytesConstRef _data, const std::vector<std::string>& _allTypes, std::vector<std::string>& _out)
{
    data = _data;
    decodeOffset = 0;

    for (const std::string& type : _allTypes)
    {
        size_t offset = 0;
        if ("int" == type || "int256" == type)
        {
            s256 s;
            offset = deserialise(s);
            stringstream ss;
            ss << s;
            _out.push_back(ss.str());
        }
        else if ("uint" == type || "uint256" == type)
        {
            u256 u;
            offset = deserialise(u);
            stringstream ss;
            ss << u;
            _out.push_back(ss.str());
        }
        else if ("address" == type)
        {
            Address addr;
            offset = deserialise(addr);
            _out.push_back(addr.hex());
        }
        else if ("string" == type)
        {
            std::string str;
            offset = deserialise(str);
            _out.push_back(str);
        }
        else
        {  // unsupport type
            return false;
        }
        decodeOffset += offset;
    }

    return true;
}

bool AbiFunction::doParser(const std::string& _strSig)
{
    std::string tempSig = _strSig;
    if (tempSig.empty())
    {
        tempSig = strFuncsignature;
    }

    auto i0 = tempSig.find("(");
    auto i1 = tempSig.find(")");
    // eg: function set(string)
    if ((i0 != std::string::npos) && (i1 != std::string::npos) && (i1 > i0))
    {
        allInputTypes.clear();
        allParamsTypes.clear();

        std::string strFuncName = tempSig.substr(0, i0);
        std::string strType = tempSig.substr(i0 + 1, i1 - i0 - 1);

        tempSig = strFuncName + "(";

        std::vector<std::string> types;
        boost::split(types, strType, boost::is_any_of(","));
        for (std::string& type : types)
        {
            TypeMeta::trim(type);
            if (!type.empty())
            {
                tempSig += type;
                tempSig += ",";

                AbiType at(type);
                auto Ok = at.doParser();
                if (Ok)
                {
                    allInputTypes.push_back(at);
                    allParamsTypes.push_back(type);
                    continue;
                }
                // invalid format
                return false;
            }
        }

        if (',' == tempSig.back())
        {
            tempSig.back() = ')';
        }
        else
        {
            tempSig += ")";
        }

        strName = strFuncName;

        strFuncsignature = tempSig;
        strSelector = "0x" + dev::sha3(strFuncsignature).hex().substr(0, 8);

        return true;
    }

    return false;
}