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
 * @brief queue to maintain the download request
 * @file DownloadRequestQueue.cpp
 * @author: jimmyshi
 * @date 2021-05-24
 */
#include "DownloadRequestQueue.h"
#include "bcos-sync/utilities/Common.h"

using namespace bcos;
using namespace bcos::sync;
using namespace bcos::protocol;

void DownloadRequestQueue::push(BlockNumber _fromNumber, size_t _size)
{
    UpgradableGuard l(x_reqQueue);
    // Note: the requester must has retry logic
    if (m_reqQueue.size() >= m_config->maxDownloadRequestQueueSize())
    {
        BLKSYNC_LOG(DEBUG) << LOG_BADGE("Download") << LOG_BADGE("Request")
                           << LOG_DESC("Drop request for reqQueue full")
                           << LOG_KV("reqQueueSize", m_reqQueue.size())
                           << LOG_KV("fromNumber", _fromNumber) << LOG_KV("size", _size)
                           << LOG_KV("nodeId", m_config->nodeID()->shortHex());
        return;
    }
    UpgradeGuard ul(l);
    m_reqQueue.push(std::make_shared<DownloadRequest>(_fromNumber, _size));
    BLKSYNC_LOG(DEBUG) << LOG_BADGE("Download") << LOG_BADGE("Request")
                       << LOG_DESC("Push request in reqQueue req") << LOG_KV("from", _fromNumber)
                       << LOG_KV("to", _fromNumber + _size - 1)
                       << LOG_KV("currentNumber", m_config->blockNumber())
                       << LOG_KV("queueSize", m_reqQueue.size())
                       << LOG_KV("peer", m_nodeId->shortHex())
                       << LOG_KV("nodeId", m_config->nodeID()->shortHex());
}

DownloadRequest::Ptr DownloadRequestQueue::topAndPop()
{
    WriteGuard l(x_reqQueue);
    if (m_reqQueue.empty())
    {
        return nullptr;
    }
    // "Tops" means that the merge result of all tops can merge at one turn
    // Example:
    // top[x] (fromNumber, size)    range       merged range    merged tops(fromNumber, size)
    // top[0] (1, 3)                [1, 4)      [1, 4)          (1, 3)
    // top[1] (1, 4)                [1, 5)      [1, 5)          (1, 4)
    // top[2] (2, 1)                [2, 3)      [1, 5)          (1, 4)
    // top[3] (2, 4)                [2, 6)      [1, 6)          (1, 5)
    // top[4] (6, 2)                [6, 8)      [1, 8)          (1, 7)
    // top[5] (10, 2)               [10, 12]    can not merge into (1, 7) leave it for next turn
    size_t fromNumber = m_reqQueue.top()->fromNumber();
    size_t size = 0;
    while (!m_reqQueue.empty() && (fromNumber + size) >= (size_t)(m_reqQueue.top()->fromNumber()))
    {
        auto topReq = m_reqQueue.top();
        // m_queue is increasing by fromNumber, so fromNumber must no more than
        // merged tops
        size = std::max(size, (size_t)(topReq->fromNumber() + topReq->size() - fromNumber));
        m_reqQueue.pop();
    }
    BLKSYNC_LOG(TRACE) << LOG_BADGE("Download") << LOG_BADGE("Request")
                       << LOG_DESC("Pop reqQueue top req") << LOG_KV("from", fromNumber)
                       << LOG_KV("to", fromNumber + size - 1);
    return std::make_shared<DownloadRequest>(fromNumber, size);
}

bool DownloadRequestQueue::empty()
{
    ReadGuard l(x_reqQueue);
    return m_reqQueue.empty();
}
