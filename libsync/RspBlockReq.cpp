#include "RspBlockReq.h"

using namespace std;
using namespace dev;
using namespace dev::eth;
using namespace dev::sync;

void DownloadRequestQueue::push(int64_t _fromNumber, int64_t _size)
{
    if (!m_canPush)
    {
        SYNCLOG(TRACE) << "[Download] Drop request when blocks responding [fromNumber/size/nodeId] "
                       << _fromNumber << "/" << _size << "/" << m_nodeId << endl;
        return;
    }

    {
        ReadGuard l(x_reqQueue);
        if (m_reqQueue.size() >= c_maxReceivedDownloadRequestPerPeer)
        {
            SYNCLOG(TRACE) << "[Download] Drop request for reqQueue full "
                              "[reqQueueSize/fromNumber/size/nodeId] "
                           << m_reqQueue.size() << "/" << _fromNumber << "/" << _size << "/"
                           << m_nodeId << endl;
            return;
        }
    }

    WriteGuard l(x_reqQueue);
    m_reqQueue.push(DownloadRequest(_fromNumber, _size));
    SYNCLOG(TRACE) << "[Download] Accept request req[" << _fromNumber << ", "
                   << _fromNumber + _size - 1 << "] from " << m_nodeId << endl;
}

DownloadRequest DownloadRequestQueue::topAndPop()
{
    if (m_canPush)
    {
        SYNCLOG(ERROR) << "[Download] DownloadRequestQueue: canPush must disable before topAndPop()"
                       << endl;
        return DownloadRequest(0, 0);
    }

    {
        ReadGuard l(x_reqQueue);
        if (m_reqQueue.empty())
            return DownloadRequest(0, 0);
    }

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
    WriteGuard l(x_reqQueue);
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

    SYNCLOG(TRACE) << "[Download] Pop top request req[" << fromNumber << ", "
                   << fromNumber + size - 1 << "]" << endl;
    return DownloadRequest(fromNumber, size);
}

bool DownloadRequestQueue::empty()
{
    ReadGuard l(x_reqQueue);
    return m_reqQueue.empty();
}
