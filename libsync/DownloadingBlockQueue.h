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
/**
 * @brief : Downloading block queue
 * @author: jimmyshi
 * @date: 2018-10-20
 */

#pragma once
#include "Common.h"
#include <libblockchain/BlockChainInterface.h>
#include <libdevcore/Guards.h>
#include <libethcore/Block.h>
#include <libflowlimit/MemoryLimiter.h>
#include <climits>
#include <queue>
#include <set>
#include <vector>

namespace dev
{
namespace sync
{
class DownloadBlocksShard
{
public:
    DownloadBlocksShard(int64_t _fromNumber, int64_t _size, bytes const& _blocksBytes)
      : fromNumber(_fromNumber), size(_size), blocksBytes(_blocksBytes)
    {}
    int64_t fromNumber;
    int64_t size;
    bytes blocksBytes;
};

struct BlockQueueCmp
{
    bool operator()(BlockPtr const& _a, BlockPtr const& _b)
    {
        // increase order
        return _a->header().number() > _b->header().number();
    }
};

class DownloadingBlockQueue
{
public:
    using ShardPtr = std::shared_ptr<DownloadBlocksShard>;
    using ShardPtrVec = std::list<ShardPtr>;

public:
    DownloadingBlockQueue(std::shared_ptr<dev::blockchain::BlockChainInterface> _blockChain,
        PROTOCOL_ID, NodeID const& _nodeId)
      : m_blockChain(_blockChain),
        m_nodeId(_nodeId),
        m_blocks(),
        m_buffer(std::make_shared<ShardPtrVec>())
    {}

    DownloadingBlockQueue()
      : m_blockChain(nullptr), m_blocks(), m_buffer(std::make_shared<ShardPtrVec>())
    {}

    /// PUsh a block packet
    void push(RLP const& _rlps);
    void push(BlockPtrVec _blocks);

    /// Is the queue empty?
    bool empty();

    /// get the total size of th block queue
    size_t size();

    /// pop the top unit of the block queue
    void pop();

    /// get the top unit of the block queue
    BlockPtr top(bool isFlushBuffer = false);

    /// clear queue and buffer
    void clear();

    /// clear queue
    void clearQueue();

    /// flush m_buffer into queue
    void flushBufferToQueue();

    void clearFullQueueIfNotHas(int64_t _blockNumber);

    void setMaxBlockQueueSize(int64_t const& _maxBlockQueueSize)
    {
        m_maxBlockQueueSize = _maxBlockQueueSize;
    }

    int64_t maxRequestBlocks() const { return m_maxRequestBlocks; }
    void adjustMaxRequestBlocks();

    void increaseMemoryUsed(int64_t const& _memoryUsed)
    {
        if (!m_memoryLimiter)
        {
            return;
        }
        m_memoryLimiter->increaseMemoryUsed(_memoryUsed);
    }

    void decreaseMemoryUsed(int64_t const& _memoryUsed)
    {
        if (!m_memoryLimiter)
        {
            return;
        }
        m_memoryLimiter->decreaseMemoryUsed(_memoryUsed);
    }

    void setMemoryLimiter(dev::flowlimit::MemoryLimiter::Ptr _memoryLimiter)
    {
        m_memoryLimiter = _memoryLimiter;
    }

private:
    bool flushOneShard(ShardPtr _blocksShard);

private:
    std::shared_ptr<dev::blockchain::BlockChainInterface> m_blockChain;
    NodeID m_nodeId;
    std::priority_queue<BlockPtr, BlockPtrVec, BlockQueueCmp> m_blocks;  //
    std::shared_ptr<ShardPtrVec> m_buffer;  // use buffer for faster push return

    mutable SharedMutex x_blocks;
    mutable SharedMutex x_buffer;
    // default max block buffer size is 512MB
    int64_t m_maxBlockQueueSize = 512 * 1024 * 1024;
    // the memory size occupied by the sync module
    std::atomic<int64_t> m_blockQueueSize = {0};
    // the max number of blocks this node can requested to
    std::atomic<int64_t> m_maxRequestBlocks = {8};
    // the average size of synced blocks
    std::atomic<int64_t> m_averageBlockSize = {0};
    std::atomic<int64_t> m_averageCalCount = {0};
    // the expand coeff of memory-size after block-decode
    int64_t const m_blockSizeExpandCoeff = 3;

    dev::flowlimit::MemoryLimiter::Ptr m_memoryLimiter;

private:
    bool isNewerBlock(std::shared_ptr<dev::eth::Block> _block);
};

}  // namespace sync
}  // namespace dev