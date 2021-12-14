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
 * @file Common.h
 * @author: yujiechen
 * @date 2021-04-12
 */
#pragma once
#include <bcos-framework/libutilities/Exceptions.h>
#include <bcos-framework/libutilities/Log.h>
#include <stdint.h>

#define PBFT_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("CONSENSUS") << LOG_BADGE("PBFT")
#define PBFT_STORAGE_LOG(LEVEL) \
    BCOS_LOG(LEVEL) << LOG_BADGE("CONSENSUS") << LOG_BADGE("PBFT") << LOG_BADGE("STORAGE")

namespace bcos
{
namespace consensus
{
enum PacketType : uint32_t
{
    PrePreparePacket = 0x00,
    PreparePacket = 0x01,
    CommitPacket = 0x02,
    ViewChangePacket = 0x03,
    NewViewPacket = 0x04,
    CommittedProposalRequest = 0x5,
    CommittedProposalResponse = 0x6,
    PreparedProposalRequest = 0x7,
    PreparedProposalResponse = 0x8,
    CheckPoint = 0x9,
    RecoverRequest = 0xa,
    RecoverResponse = 0xb,
};
DERIVE_BCOS_EXCEPTION(UnknownPBFTMsgType);
DERIVE_BCOS_EXCEPTION(InitPBFTException);
}  // namespace consensus
}  // namespace bcos