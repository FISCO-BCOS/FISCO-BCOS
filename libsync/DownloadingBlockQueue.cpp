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

#include "DownloadingBlockQueue.h"
#include "Common.h"
#include <libdevcore/easylog.h>

using namespace std;
using namespace dev;
using namespace dev::eth;
using namespace dev::sync;

/// Push a block
void DownloadingBlockQueue::push(RLP const& _rlps)
{
    WriteGuard l(x_buffer);
    if (m_buffer->size() >= c_maxDownloadingBlockQueueBufferSize)
    {
        SYNCLOG(WARNING) << "[Rcv] [Download] DownloadingBlockQueueBuffer is full with size "
                         << m_buffer->size();
        return;
    }
    ShardPtr blocksShard = make_shared<DownloadBlocksShard>(0, 0, _rlps.data().toBytes());
    m_buffer->emplace_back(blocksShard);
}

void DownloadingBlockQueue::push(BlockPtrVec _blocks)
{
    RLPStream rlpStream;
    rlpStream.appendList(_blocks.size());
    for (BlockPtr block : _blocks)
    {
        rlpStream.append(block->rlp());
        // SYNCLOG(TRACE) << "[Rcv] [Download] DownloadingBlockQueueBuffer push  [number/hash]: "
        //<< block->header().number() << "/" << block->headerHash() << endl;
    }

    std::shared_ptr<bytes> b = std::make_shared<bytes>();
    rlpStream.swapOut(*b);

    RLP rlps = RLP(ref(*b));
    push(rlps);
}

/// Is the queue empty?
bool DownloadingBlockQueue::empty()
{
    bool res;
    {
        ReadGuard l1(x_buffer);
        ReadGuard l2(x_blocks);
        res = m_blocks.empty() && (!m_buffer || m_buffer->empty());
    }
    return res;
}

size_t DownloadingBlockQueue::size()
{
    ReadGuard l1(x_buffer);
    ReadGuard l2(x_blocks);
    size_t s = (!m_buffer ? 0 : m_buffer->size()) + m_blocks.size();
    return s;
}

/// pop the block
void DownloadingBlockQueue::pop()
{
    WriteGuard l(x_blocks);
    if (!m_blocks.empty())
        m_blocks.pop();
}

BlockPtr DownloadingBlockQueue::top(bool isFlushBuffer)
{
    if (isFlushBuffer)
        flushBufferToQueue();

    ReadGuard l(x_blocks);
    if (!m_blocks.empty())
        return m_blocks.top();
    else
        return nullptr;
}

void DownloadingBlockQueue::clear()
{
    WriteGuard l(x_buffer);
    m_buffer->clear();

    clearQueue();
}

void DownloadingBlockQueue::clearQueue()
{
    WriteGuard l(x_blocks);
    std::priority_queue<BlockPtr, BlockPtrVec, BlockQueueCmp> emptyQueue;
    swap(m_blocks, emptyQueue);  // Does memory leak here ?
}

void DownloadingBlockQueue::flushBufferToQueue()
{
    shared_ptr<ShardPtrVec> localBuffer;
    {
        WriteGuard l(x_buffer);
        localBuffer = m_buffer;                 //
        m_buffer = make_shared<ShardPtrVec>();  // m_buffer point to a new vector
    }

    // pop buffer into queue
    WriteGuard l(x_blocks);


    for (ShardPtr blocksShard : *localBuffer)
    {
        if (m_blocks.size() >= c_maxDownloadingBlockQueueSize)  // TODO not to use size to control
                                                                // insert
        {
            SYNCLOG(TRACE) << "[Rcv] [Download] DownloadingBlockQueueBuffer is full with size "
                           << m_blocks.size();
            break;
        }

        SYNCLOG(TRACE) << "[Rcv] [Download] Decoding block buffer [size]: "
                       << blocksShard->blocksBytes.size() << endl;

        RLP const& rlps = RLP(ref(blocksShard->blocksBytes));
        unsigned itemCount = rlps.itemCount();
        size_t successCnt = 0;
        for (unsigned i = 0; i < itemCount; ++i)
        {
            try
            {
                shared_ptr<Block> block = make_shared<Block>(rlps[i].toBytes());
                if (isNewerBlock(block))
                {
                    successCnt++;
                    m_blocks.push(block);
                }
            }
            catch (std::exception& e)
            {
                SYNCLOG(WARNING) << "[Rcv] [Download] Invalid block RLP [reason/RLPDataSize]: "
                                 << e.what() << "/" << rlps.data().size() << endl;
                continue;
            }
        }

        SYNCLOG(TRACE)
            << "[Rcv] [Download] Flush buffer to block queue [import/rcv/downloadBlockQueue]: "
            << successCnt << "/" << itemCount << "/" << m_blocks.size() << endl;
    }
}

void DownloadingBlockQueue::clearFullQueueIfNotHas(int64_t _blockNumber)
{
    bool needClear = false;
    {
        ReadGuard l(x_blocks);

        if (m_blocks.size() == c_maxDownloadingBlockQueueSize &&
            m_blocks.top()->header().number() > _blockNumber)
            needClear = true;
    }
    if (needClear)
        clearQueue();
}

bool DownloadingBlockQueue::isNewerBlock(shared_ptr<Block> _block)
{
    if (m_blockChain != nullptr && _block->header().number() <= m_blockChain->number())
        return false;

    // if (block->header()->)
    // if //Check sig list
    // return false;

    return true;
}