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
 * @brief PB implementation for BlocksMsgInterface
 * @file BlocksMsgImpl.h
 * @author: yujiechen
 * @date 2021-05-24
 */
#pragma once
#include "bcos-sync/interfaces/BlocksMsgInterface.h"
#include "bcos-sync/protocol/PB/BlockSyncMsgImpl.h"
#include "bcos-sync/utilities/Common.h"
namespace bcos
{
namespace sync
{
class BlocksMsgImpl : public BlocksMsgInterface, public BlockSyncMsgImpl
{
public:
    using Ptr = std::shared_ptr<BlocksMsgImpl>;
    BlocksMsgImpl() : BlockSyncMsgImpl()
    {
        setPacketType(BlockSyncPacketType::BlockResponsePacket);
    }
    explicit BlocksMsgImpl(BlockSyncMsgImpl::Ptr _blockSyncMsg)
      : BlocksMsgImpl(_blockSyncMsg->syncMessage())
    {}

    explicit BlocksMsgImpl(bytesConstRef _data) : BlocksMsgImpl() { decode(_data); }
    ~BlocksMsgImpl() override {}

    size_t blocksSize() const override { return m_syncMessage->blocksdata_size(); }
    bytesConstRef blockData(size_t _index) const override
    {
        auto const& blockData = m_syncMessage->blocksdata(_index);
        return bytesConstRef((byte const*)blockData.data(), blockData.size());
    }

    void appendBlockData(bytes&& _blockData) override
    {
        auto index = blocksSize();
        auto blockSize = _blockData.size();
        m_syncMessage->add_blocksdata();
        m_syncMessage->set_blocksdata(index, (std::move(_blockData)).data(), blockSize);
    }

    void appendBlockData(bytes const& _blockData) override
    {
        auto index = blocksSize();
        auto blockSize = _blockData.size();
        m_syncMessage->add_blocksdata();
        m_syncMessage->set_blocksdata(index, _blockData.data(), blockSize);
    }

protected:
    explicit BlocksMsgImpl(std::shared_ptr<BlockSyncMessage> _syncMessage)
    {
        setPacketType(BlockSyncPacketType::BlockResponsePacket);
        m_syncMessage = _syncMessage;
    }
};
}  // namespace sync
}  // namespace bcos