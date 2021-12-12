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
 * @brief PB implement for BlockSyncMsgInterface
 * @file BlockSyncMsgImpl.h
 * @author: yujiechen
 * @date 2021-05-24
 */
#pragma once
#include "bcos-sync/interfaces/BlockSyncMsgInterface.h"
#include "bcos-sync/protocol/proto/BlockSync.pb.h"
#include <bcos-framework/libprotocol/Common.h>
namespace bcos
{
namespace sync
{
class BlockSyncMsgImpl : virtual public BlockSyncMsgInterface
{
public:
    using Ptr = std::shared_ptr<BlockSyncMsgImpl>;
    BlockSyncMsgImpl() : m_syncMessage(std::make_shared<BlockSyncMessage>()) {}
    explicit BlockSyncMsgImpl(bytesConstRef _data) : BlockSyncMsgImpl() { decode(_data); }

    ~BlockSyncMsgImpl() override {}

    bytesPointer encode() const override { return bcos::protocol::encodePBObject(m_syncMessage); }
    void decode(bytesConstRef _data) override
    {
        bcos::protocol::decodePBObject(m_syncMessage, _data);
    }

    int32_t version() const override { return m_syncMessage->version(); }
    bcos::protocol::BlockNumber number() const override { return m_syncMessage->number(); }
    int32_t packetType() const override { return m_syncMessage->packettype(); }


    void setVersion(int32_t _version) override { m_syncMessage->set_version(_version); }
    void setNumber(bcos::protocol::BlockNumber _number) override
    {
        m_syncMessage->set_number(_number);
    }

    void setPacketType(int32_t packetType) override { m_syncMessage->set_packettype(packetType); }

    std::shared_ptr<BlockSyncMessage> syncMessage() { return m_syncMessage; }

protected:
    std::shared_ptr<BlockSyncMessage> m_syncMessage;
};
}  // namespace sync
}  // namespace bcos