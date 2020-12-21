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
 * @file CommonProtocolType.h
 * @author chaychen
 * @date 2018.9.26
 *
 * ProtocolID identify.
 */

#pragma once
#include <libconfig/GlobalConfigure.h>
#include <libutilities/FixedBytes.h>
#include <stdint.h>
#include <iosfwd>

#define MAX_VALUE_IN_MB (INT64_MAX / (1024 * 1024))
#define MAX_VALUE_IN_Mb (INT64_MAX / (1024 * 128))

namespace bcos
{
using GROUP_ID = int16_t;
using MODULE_ID = uint16_t;
using PROTOCOL_ID = int32_t;
using PACKET_TYPE = uint16_t;
using VERSION_TYPE = uint16_t;

using VMFlagType = uint64_t;
struct EVMFlags
{
    static const VMFlagType FreeStorageGas = ((VMFlagType)1 << (sizeof(VMFlagType) * 8 - 1));
};

inline bool enableFreeStorage(VMFlagType const& _vmFlags)
{
    return _vmFlags & EVMFlags::FreeStorageGas;
}

static const GROUP_ID maxGroupID = 32767;

namespace protocol
{
/// The log bloom's size (2048-bit).
using LogBloom = h2048;
using LogBlooms = std::vector<LogBloom>;

using NonceKeyType = u256;
using BlockNumber = int64_t;
// the max blocknumber value
static const BlockNumber MAX_BLOCK_NUMBER = INT64_MAX;
// the max number of topic in event logd
static const uint32_t MAX_NUM_TOPIC_EVENT_LOG = 4;

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
