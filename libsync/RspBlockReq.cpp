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
using namespace bcos;
using namespace bcos::protocol;
using namespace bcos::sync;

void DownloadRequestQueue::push(int64_t _fromNumber, int64_t _size)
{
    {
        ReadGuard l(x_reqQueue);
        if (m_reqQueue.size() >= c_maxReceivedDownloadRequestPerPeer)
        {
            SYNC_LOG(DEBUG) << LOG_BADGE("Download") << LOG_BADGE("Request")
                            << LOG_DESC("Drop request for reqQueue full")
                            << LOG_KV("reqQueueSize", m_reqQueue.size())
                            << LOG_KV("fromNumber", _fromNumber) << LOG_KV("size", _size)
                            << LOG_KV("nodeId", m_nodeId.abridged());

            return;
        }
    }
    {
        WriteGuard l(x_reqQueue);
        m_reqQueue.push(DownloadRequest(_fromNumber, _size));

        SYNC_LOG(TRACE) << LOG_BADGE("Download") << LOG_BADGE("Request")
                        << LOG_DESC("Push request in reqQueue req") << LOG_KV("from", _fromNumber)
                        << LOG_KV("to", _fromNumber + _size - 1)
                        << LOG_KV("peer", m_nodeId.abridged());
    }
}

DownloadRequest DownloadRequestQueue::topAndPop()
{
    WriteGuard l(x_reqQueue);
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

    SYNC_LOG(TRACE) << LOG_BADGE("Download") << LOG_BADGE("Request")
                    << LOG_DESC("Pop reqQueue top req") << LOG_KV("from", fromNumber)
                    << LOG_KV("to", fromNumber + size - 1);
    return DownloadRequest(fromNumber, size);
}

bool DownloadRequestQueue::empty()
{
    ReadGuard l(x_reqQueue);
    return m_reqQueue.empty();
}
