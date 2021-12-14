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
 * @brief PB implementation for BlockRequestInterface
 * @file BlockRequestImpl.h
 * @author: yujiechen
 * @date 2021-05-24
 */
#pragma once
#include "bcos-sync/interfaces/BlockRequestInterface.h"
#include "bcos-sync/protocol/PB/BlockSyncMsgImpl.h"
#include "bcos-sync/utilities/Common.h"
namespace bcos
{
namespace sync
{
class BlockRequestImpl : public BlockRequestInterface, public BlockSyncMsgImpl
{
public:
    BlockRequestImpl() : BlockSyncMsgImpl()
    {
        setPacketType(BlockSyncPacketType::BlockRequestPacket);
    }
    explicit BlockRequestImpl(BlockSyncMsgImpl::Ptr _blockSyncMsg)
      : BlockRequestImpl(_blockSyncMsg->syncMessage())
    {}

    explicit BlockRequestImpl(bytesConstRef _data) : BlockRequestImpl() { decode(_data); }
    ~BlockRequestImpl() override {}

    size_t size() const override { return m_syncMessage->size(); }
    void setSize(size_t _size) override { m_syncMessage->set_size(_size); }

protected:
    explicit BlockRequestImpl(std::shared_ptr<BlockSyncMessage> _syncMessage)
    {
        setPacketType(BlockSyncPacketType::BlockRequestPacket);
        m_syncMessage = _syncMessage;
    }
};
}  // namespace sync
}  // namespace bcos