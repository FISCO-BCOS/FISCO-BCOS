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
 * @brief queue to store the downloading blocks
 * @file DownloadingQueue.h
 * @author: jimmyshi
 * @date 2021-05-24
 */
#pragma once
#include "bcos-sync/BlockSyncConfig.h"
#include "bcos-sync/interfaces/BlocksMsgInterface.h"
#include <bcos-framework/interfaces/protocol/Block.h>
#include <queue>
namespace bcos
{
namespace sync
{
// increase order
struct BlockCmp
{
    bool operator()(
        bcos::protocol::Block::Ptr const& _first, bcos::protocol::Block::Ptr const& _second) const
    {
        // increase order
        return _first->blockHeader()->number() > _second->blockHeader()->number();
    }
};
using BlockQueue =
    std::priority_queue<bcos::protocol::Block::Ptr, bcos::protocol::Blocks, BlockCmp>;
class DownloadingQueue : public std::enable_shared_from_this<DownloadingQueue>
{
public:
    using BlocksMessageQueue = std::list<BlocksMsgInterface::Ptr>;
    using BlocksMessageQueuePtr = std::shared_ptr<BlocksMessageQueue>;

    using Ptr = std::shared_ptr<DownloadingQueue>;
    explicit DownloadingQueue(BlockSyncConfig::Ptr _config)
      : m_config(_config), m_blockBuffer(std::make_shared<BlocksMessageQueue>())
    {}
    virtual ~DownloadingQueue() {}

    virtual void push(BlocksMsgInterface::Ptr _blocksData);
    // Is the queue empty?
    virtual bool empty();

    // get the total size of th block queue
    virtual size_t size();

    // pop the top unit of the block queue
    virtual void pop();

    // get the top unit of the block queue
    bcos::protocol::Block::Ptr top(bool isFlushBuffer = false);

    virtual void clearFullQueueIfNotHas(bcos::protocol::BlockNumber _blockNumber);

    virtual void applyBlock(bcos::protocol::Block::Ptr _block);
    // clear queue and buffer
    virtual void clear();

    virtual void registerNewBlockHandler(
        std::function<void(bcos::ledger::LedgerConfig::Ptr)> _newBlockHandler)
    {
        m_newBlockHandler = _newBlockHandler;
    }

    // flush m_buffer into queue
    virtual void flushBufferToQueue();
    virtual void clearExpiredQueueCache();
    virtual void tryToCommitBlockToLedger();
    virtual size_t commitQueueSize()
    {
        ReadGuard l(x_commitQueue);
        return m_commitQueue.size();
    }

    virtual void onCommitFailed(bcos::Error::Ptr _error, bcos::protocol::Block::Ptr _failedBlock);

protected:
    // clear queue
    virtual void clearQueue();
    virtual void clearExpiredCache(BlockQueue& _queue, SharedMutex& _lock);
    virtual bool flushOneShard(BlocksMsgInterface::Ptr _blocksData);
    virtual bool isNewerBlock(bcos::protocol::Block::Ptr _block);

    virtual void commitBlock(bcos::protocol::Block::Ptr _block);
    virtual void commitBlockState(bcos::protocol::Block::Ptr _block);

    virtual bool checkAndCommitBlock(bcos::protocol::Block::Ptr _block);
    virtual void updateCommitQueue(bcos::protocol::Block::Ptr _block);

    virtual void finalizeBlock(
        bcos::protocol::Block::Ptr _block, bcos::ledger::LedgerConfig::Ptr _ledgerConfig);
    virtual bool verifyExecutedBlock(
        bcos::protocol::Block::Ptr _block, bcos::protocol::BlockHeader::Ptr _blockHeader);

private:
    // Note: this function should not be called frequently
    std::string printBlockHeader(bcos::protocol::BlockHeader::Ptr _header);

private:
    BlockSyncConfig::Ptr m_config;
    BlockQueue m_blocks;
    mutable SharedMutex x_blocks;

    BlocksMessageQueuePtr m_blockBuffer;
    mutable SharedMutex x_blockBuffer;

    BlockQueue m_commitQueue;
    mutable SharedMutex x_commitQueue;

    std::function<void(bcos::ledger::LedgerConfig::Ptr)> m_newBlockHandler;
};
}  // namespace sync
}  // namespace bcos