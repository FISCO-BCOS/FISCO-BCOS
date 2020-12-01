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
 *  @brief: Implement the fixed-size bytes
 *  @file FixedBytes.cpp
 *  @date 2020-11-20
 */

#include "FixedBytes.h"
#include <boost/algorithm/string.hpp>

namespace bcos
{
std::random_device s_fixedBytesEngine;
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
