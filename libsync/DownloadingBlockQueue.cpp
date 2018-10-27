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
void DownloadingBlockQueue::push(BlockPtr _block)
{
    WriteGuard l(x_buffer);
    if (m_buffer->size() >= c_maxDownloadingBlockQueueBufferSize)
    {
        LOG(WARNING) << "DownloadingBlockQueueBuffer is full with size " << m_buffer->size();
        return;
    }
    m_buffer->emplace_back(_block);
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
    WriteGuard l1(x_buffer);
    WriteGuard l2(x_blocks);
    m_buffer->clear();

    std::priority_queue<BlockPtr, BlockPtrVec, BlockQueueCmp> emptyQueue;
    swap(m_blocks, emptyQueue);  // Does memory leak here ?
}

void DownloadingBlockQueue::flushBufferToQueue()
{
    shared_ptr<BlockPtrVec> localBuffer;
    {
        WriteGuard l(x_buffer);
        localBuffer = m_buffer;                 //
        m_buffer = make_shared<BlockPtrVec>();  // m_buffer point to a new vector
    }

    // pop buffer into queue
    WriteGuard l(x_blocks);
    for (BlockPtr block : *localBuffer)
    {
        if (m_blocks.size() >= c_maxDownloadingBlockQueueSize)  // TODO not to use size to control
                                                                // insert
        {
            LOG(WARNING) << "DownloadingBlockQueueBuffer is full with size " << m_blocks.size();
            break;
        }

        m_blocks.push(block);
    }
}