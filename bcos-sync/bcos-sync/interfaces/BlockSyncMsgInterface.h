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
 * @brief interface for the basic block syncMsg
 * @file BlockSyncMsgInterface.h
 * @author: yujiechen
 * @date 2021-05-24
 */
#pragma once
#include <bcos-framework/protocol/ProtocolTypeDef.h>
#include <memory>
namespace bcos
{
namespace sync
{
class BlockSyncMsgInterface
{
public:
    using Ptr = std::shared_ptr<BlockSyncMsgInterface>;
    BlockSyncMsgInterface() = default;
    virtual ~BlockSyncMsgInterface() {}

    virtual bytesPointer encode() const = 0;
    virtual void decode(bytesConstRef _data) = 0;

    virtual bcos::protocol::BlockNumber number() const = 0;
    virtual int32_t packetType() const = 0;
    virtual int32_t version() const = 0;

    virtual void setNumber(bcos::protocol::BlockNumber _number) = 0;
    virtual void setPacketType(int32_t packetType) = 0;
    virtual void setVersion(int32_t _version) = 0;
};
}  // namespace sync
}  // namespace bcos