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
 * @brief implementation for the block sync status packet
 * @file BlockSyncStatusImpl.h
 * @author: yujiechen
 * @date 2021-05-23
 */
#pragma once
#include "bcos-sync/interfaces/BlockSyncStatusInterface.h"
#include "bcos-sync/protocol/PB/BlockSyncMsgImpl.h"
#include "bcos-sync/utilities/Common.h"

namespace bcos
{
namespace sync
{
class BlockSyncStatusImpl : public BlockSyncStatusInterface, public BlockSyncMsgImpl
{
public:
    using Ptr = std::shared_ptr<BlockSyncStatusImpl>;
    BlockSyncStatusImpl() : BlockSyncMsgImpl()
    {
        setPacketType(BlockSyncPacketType::BlockStatusPacket);
    }
    explicit BlockSyncStatusImpl(BlockSyncMsgImpl::Ptr _blockSyncMsg)
    {
        setPacketType(BlockSyncPacketType::BlockStatusPacket);
        m_syncMessage = _blockSyncMsg->syncMessage();
        deserializeObject();
    }

    explicit BlockSyncStatusImpl(bytesConstRef _data) : BlockSyncStatusImpl() { decode(_data); }

    ~BlockSyncStatusImpl() override {}

    void decode(bytesConstRef _data) override;
    bcos::crypto::HashType const& hash() const override { return m_hash; }
    bcos::crypto::HashType const& genesisHash() const override { return m_genesisHash; }

    void setHash(bcos::crypto::HashType const& _hash) override;
    void setGenesisHash(bcos::crypto::HashType const& _gensisHash) override;

protected:
    virtual void deserializeObject();

private:
    bcos::crypto::HashType m_hash;
    bcos::crypto::HashType m_genesisHash;
};
}  // namespace sync
}  // namespace bcos