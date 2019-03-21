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

using namespace std;
using namespace dev;
using namespace dev::eth;

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