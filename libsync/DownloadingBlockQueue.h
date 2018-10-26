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
#include <libdevcore/Guards.h>
#include <libethcore/Block.h>
#include <climits>
#include <queue>
#include <set>
#include <vector>

namespace dev
{
namespace sync
{
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
    DownloadingBlockQueue() : m_blocks(), m_buffer(std::make_shared<BlockPtrVec>()) {}
    /// Push a block
    void push(BlockPtr _block);

    /// Is the queue empty?
    bool empty();

    /// get the total size of th block queue
    size_t size();

    /// pop the top unit of the block queue
    void pop();

    /// get the top unit of the block queue
    BlockPtr top(bool isFlushBuffer = false);

    void clear();

    /// flush m_buffer into queue
    void flushBufferToQueue();

private:
    std::priority_queue<BlockPtr, BlockPtrVec, BlockQueueCmp> m_blocks;  //
    std::shared_ptr<BlockPtrVec> m_buffer;  // use buffer for faster push return

    mutable SharedMutex x_blocks;
    mutable SharedMutex x_buffer;
};

}  // namespace sync
}  // namespace dev