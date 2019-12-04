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
 * @brief : Respond block requests
 * @author: jimmyshi
 * @date: 2018-11-15
 */

#pragma once
#include "Common.h"
#include <libblockchain/BlockChainInterface.h>
#include <libdevcore/Guards.h>
#include <climits>
#include <queue>
#include <set>
#include <vector>

namespace dev
{
namespace sync
{
class DownloadRequest
{
public:
    DownloadRequest(int64_t _fromNumber, int64_t _size) : fromNumber(_fromNumber), size(_size) {}
    int64_t fromNumber;
    int64_t size;
};

struct RequestQueueCmp
{
    bool operator()(DownloadRequest const& _a, DownloadRequest const& _b)
    {
        if (_a.fromNumber == _b.fromNumber)
            return _a.size < _b.size;          // decreasing order by size
        return _a.fromNumber > _b.fromNumber;  // increasing order by fromNumber
    }
};

class DownloadRequestQueue
{
public:
    DownloadRequestQueue(NodeID _nodeId) : m_nodeId(_nodeId) {}
    void push(int64_t _fromNumber, int64_t _size);
    DownloadRequest topAndPop();  // Must call use disablePush() before
    bool empty();

private:
    NodeID m_nodeId;
    mutable SharedMutex x_reqQueue;
    std::priority_queue<DownloadRequest, std::vector<DownloadRequest>, RequestQueueCmp> m_reqQueue;
};
}  // namespace sync
}  // namespace dev