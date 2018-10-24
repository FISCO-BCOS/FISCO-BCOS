/*
    This file is part of FISCO-BCOS.

    FISCO-BCOS is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    FISCO-BCOS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file Protocol.h
 * @author chaychen
 * @date 2018.9.26
 *
 * ProtocolID identify.
 */

#pragma once
#include <stdint.h>
#include <iostream>

namespace dev
{
typedef int8_t GROUP_ID;
typedef uint8_t MODULE_ID;
typedef int16_t PROTOCOL_ID;
typedef uint16_t PACKET_TYPE;
namespace eth
{
enum ProtocolID
{
    AMOP = 1,
    Topic = 2,
    PBFT = 8,
    BlockSync = 9,
    TxPool = 10
};

enum ExtraIndex
{
    PBFTExtraData = 0,
    ExtraIndexNum
};

inline PROTOCOL_ID getGroupProtoclID(GROUP_ID groupID, MODULE_ID moduleID)
{
    if (groupID < 0)
        return 0;
    else
        return (groupID << 8) | moduleID;
}

inline std::pair<GROUP_ID, MODULE_ID> getGroupAndProtocol(PROTOCOL_ID id)
{
    return std::make_pair((id >> 8) & 0xff, id & 0xff);
}

}  // namespace eth
}  // namespace dev
