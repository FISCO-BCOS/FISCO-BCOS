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

#include "RspBlockReq.h"

using namespace std;
using namespace dev;
using namespace dev::eth;
using namespace dev::sync;

void DownloadRequestQueue::push(int64_t _fromNumber, int64_t _size)
{
    Guard l(x_push);
    if (!x_canPush.try_lock())
    {
        SYNCLOG(TRACE)
            << "[Download] [Request] Drop request when responding blocks [fromNumber/size/nodeId] "
            << _fromNumber << "/" << _size << "/" << m_nodeId.abridged() << endl;
        return;
    }

    if (m_reqQueue.size() >= c_maxReceivedDownloadRequestPerPeer)
    {
        SYNCLOG(TRACE) << "[Download] [Request] Drop request for reqQueue full "
                          "[reqQueueSize/fromNumber/size/nodeId] "
                       << m_reqQueue.size() << "/" << _fromNumber << "/" << _size << "/"
                       << m_nodeId.abridged() << endl;
        x_canPush.unlock();
        return;
    }

    m_reqQueue.push(DownloadRequest(_fromNumber, _size));
    SYNCLOG(TRACE) << "[Download] [Request] Push request in reqQueue req[" << _fromNumber << ", "
                   << _fromNumber + _size - 1 << "] from " << m_nodeId.abridged() << endl;

    x_canPush.unlock();
}

DownloadRequest DownloadRequestQueue::topAndPop()
{
    if (m_reqQueue.empty())
        return DownloadRequest(0, 0);

    // Merge tops of reqQueue.
    // "Tops" means that the merge result of all tops can merge at one turn
    // Example:
    // top[x] (fromNumber, size)    range       merged range    merged tops(fromNumber, size)
    // top[0] (1, 3)                [1, 4)      [1, 4)          (1, 3)
    // top[1] (1, 4)                [1, 5)      [1, 5)          (1, 4)
    // top[2] (2, 1)                [2, 3)      [1, 5)          (1, 4)
    // top[3] (2, 4)                [2, 6)      [1, 6)          (1, 5)
    // top[4] (6, 2)                [6, 8)      [1, 8)          (1, 7)
    // top[5] (10, 2)               [10, 12]    can not merge into (1, 7) leave it for next turn
    int64_t fromNumber = m_reqQueue.top().fromNumber;
    int64_t size = 0;
    while (!m_reqQueue.empty() && fromNumber + size >= m_reqQueue.top().fromNumber)
    {
        DownloadRequest const& topReq = m_reqQueue.top();

        // m_queue is increasing by fromNumber, so fromNumber must no more than
        // merged tops
        size = max(size, topReq.fromNumber + topReq.size - fromNumber);
        m_reqQueue.pop();
    }

    SYNCLOG(TRACE) << "[Download] [Request] Pop reqQueue top req[" << fromNumber << ", "
                   << fromNumber + size - 1 << "]" << endl;
    return DownloadRequest(fromNumber, size);
}

bool DownloadRequestQueue::empty()
{
    return m_reqQueue.empty();
}
