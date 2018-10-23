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
    RecursiveGuard l(x_buffer);
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
        RecursiveGuard l(x_buffer);
        res = m_blocks.empty() && m_buffer->empty();
    }
    return res;
}

size_t DownloadingBlockQueue::size()
{
    RecursiveGuard l1(x_buffer), l2(x_blocks);
    size_t s = m_buffer->size() + m_blocks.size();
    return s;
}

/// pop the block sequent
BlockPtrVec DownloadingBlockQueue::popSequent(int64_t _startNumber, int64_t _limit)
{
    shared_ptr<BlockPtrVec> localBuffer;
    {
        RecursiveGuard l(x_buffer);
        localBuffer = m_buffer;                 //
        m_buffer = make_shared<BlockPtrVec>();  // m_buffer point to a new vector
    }

    // pop buffer into queue
    RecursiveGuard l(x_blocks);
    for (BlockPtr block : *localBuffer)
    {
        if (m_blocks.size() >= c_maxDownloadingBlockQueueSize)  // TODO not to use size to control
                                                                // insert
        {
            LOG(WARNING) << "DownloadingBlockQueueBuffer is full with size " << m_blocks.size();
            break;
        }

        int64_t number = block->header().number();

        if (number < _startNumber)
            continue;  // ignore smaller blocks

        auto p = m_blocks.find(number);
        if (p == m_blocks.end())
        {
            m_blocks.insert(pair<int64_t, BlockPtr>(number, block));
            m_minNumberInQueue = min(m_minNumberInQueue, number);
        }
    }

    // delete smaller blocks in queue
    for (; m_minNumberInQueue < _startNumber; ++m_minNumberInQueue)
    {
        auto p = m_blocks.find(m_minNumberInQueue);
        if (p != m_blocks.end())
            m_blocks.erase(p);
    }

    // generate a queue
    BlockPtrVec ret;
    for (; m_minNumberInQueue < _startNumber + _limit; ++m_minNumberInQueue)
    {
        auto p = m_blocks.find(m_minNumberInQueue);
        if (p != m_blocks.end())
        {
            ret.emplace_back(p->second);
            m_blocks.erase(p);
        }
        else
            break;
    }
    return ret;
}