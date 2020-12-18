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
/** @file Protocol.h
 * @author chaychen
 * @date 2018.9.26
 *
 * ProtocolID identify.
 */

#pragma once
#include <libconfig/GlobalConfigure.h>
#include <stdint.h>
#include <iosfwd>

#define MAX_VALUE_IN_MB (INT64_MAX / (1024 * 1024))
#define MAX_VALUE_IN_Mb (INT64_MAX / (1024 * 128))

namespace bcos
{
typedef int16_t GROUP_ID;
typedef uint16_t MODULE_ID;
typedef int32_t PROTOCOL_ID;
typedef uint16_t PACKET_TYPE;
typedef uint16_t VERSION_TYPE;
static const GROUP_ID maxGroupID = 32767;
namespace protocol
{
enum ProtocolID
{
    AMOP = 1,
    Topic = 2,
    PBFT = 8,
    BlockSync = 9,
    TxPool = 10,
    Raft = 11
};

enum VersionFlag
{
    CompressFlag = 0x8000  /// compressFlag
};

enum ExtraIndex
{
    PBFTExtraData = 0,
    ExtraIndexNum
};

// check if groupID valid groupID
inline bool validGroupID(int _groupID)
{
    return _groupID > 0 && _groupID <= maxGroupID;
}

inline PROTOCOL_ID getGroupProtoclID(GROUP_ID groupID, MODULE_ID moduleID)
{
    if (groupID < 0)
        return 0;
    return (groupID << (8 * sizeof(MODULE_ID))) | moduleID;
}

inline std::pair<GROUP_ID, MODULE_ID> getGroupAndProtocol(PROTOCOL_ID id)
{
    ///< The base should be 1, not 2.
    int32_t high = (1 << (8 * sizeof(GROUP_ID))) - 1;
    int32_t low = (1 << (8 * sizeof(MODULE_ID))) - 1;
    return std::make_pair((id >> (8 * sizeof(MODULE_ID))) & high, id & low);
}

}  // namespace protocol
}  // namespace bcos
