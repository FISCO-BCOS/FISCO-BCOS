/**
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
 * @brief Common files for block sync
 * @file Common.h
 * @author: yujiechen
 * @date 2021-05-23
 */
#pragma once
#include <bcos-framework/Common.h>
#include <tbb/parallel_for.h>

#define BLKSYNC_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("BLOCK SYNC")
namespace bcos
{
namespace sync
{
enum BlockSyncPacketType : int32_t
{
    BlockStatusPacket = 0x00,
    BlockRequestPacket = 0x01,
    BlockResponsePacket = 0x02,
};
enum SyncState : int32_t
{
    Idle = 0x00,         //< Initial chain sync complete. Waiting for new packets
    Downloading = 0x01,  //< Downloading blocks
};
}  // namespace sync
}  // namespace bcos