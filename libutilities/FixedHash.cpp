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

#include "FixedHash.h"
#include <boost/algorithm/string.hpp>

namespace bcos
{
std::random_device s_fixedHashEngine;

h128 fromUUID(std::string const& _uuid)
{
    try
    {
        return h128(boost::replace_all_copy(_uuid, "-", ""));
    }
    catch (...)
    {
        return h128();
    }
}

std::string toUUID(h128 const& _uuid)
{
    std::string ret = toHex(_uuid.ref());
    for (unsigned i : {20, 16, 12, 8})
        ret.insert(ret.begin() + i, '-');
    return ret;
}

bool isNodeIDOk(bcos::h512 const& _nodeID)
{
    return bcos::h512() != _nodeID;
}

bool isNodeIDOk(const std::string& _nodeID)
{
    try
    {
        // check node id length, must be 130bits or 128bits
        if (_nodeID.length() != 130 && _nodeID.length() != 128)
        {
            return false;
        }
        // if the length of the node id is 130, must be start with 0x
        if (_nodeID.length() == 130 && _nodeID.compare(0, 2, "0x") != 0)
        {
            return false;
        }
        bcos::h512 nodeID = bcos::h512(_nodeID);
        return isNodeIDOk(nodeID);
    }
    catch (...)
    {
        return false;
    }
    return false;
}

}  // namespace bcos
