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
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-sync/utilities/Common.h"

using namespace bcos;
using namespace bcos::sync;
using namespace bcos::protocol;

void DownloadRequestQueue::push(
    BlockNumber _fromNumber, size_t _size, size_t _interval, int32_t _dataFlag)
{
    UpgradableGuard lock(x_reqQueue);
    // Note: the requester must have retry logic
    if (m_reqQueue.size() >= m_config->maxDownloadRequestQueueSize())
    {
        BLKSYNC_LOG(DEBUG) << LOG_BADGE("Download") << LOG_BADGE("Request")
                           << LOG_DESC("Drop request for reqQueue full")
                           << LOG_KV("reqQueueSize", m_reqQueue.size())
                           << LOG_KV("fromNumber", _fromNumber) << LOG_KV("size", _size)
                           << LOG_KV("nodeId", m_config->nodeID()->shortHex());
        return;
    }
    UpgradeGuard ulock(lock);
    m_reqQueue.push(std::make_shared<DownloadRequest>(_fromNumber, _size, _interval, _dataFlag));
    BLKSYNC_LOG(DEBUG) << LOG_BADGE("Download") << LOG_BADGE("Request")
                       << LOG_DESC("Push request in reqQueue req") << LOG_KV("from", _fromNumber)
                       << LOG_KV("size", _size) << LOG_KV("interval", _interval)
                       << LOG_KV("currentNumber", m_config->blockNumber())
                       << LOG_KV("queueSize", m_reqQueue.size())
                       << LOG_KV("peer", m_nodeId->shortHex())
                       << LOG_KV("nodeId", m_config->nodeID()->shortHex());
}

DownloadRequest::UniquePtr DownloadRequestQueue::topAndPop()
{
    UpgradableGuard lock(x_reqQueue);
    if (m_reqQueue.empty())
    {
        return nullptr;
    }

    BlockNumber fromNumber = m_reqQueue.top()->fromNumber();
    size_t size = m_reqQueue.top()->size();
    size_t interval = m_reqQueue.top()->interval();
    BlockNumber toNumber = m_reqQueue.top()->toNumber();
    int32_t dataFlag = m_reqQueue.top()->blockDataFlag();
    UpgradeGuard ulock(lock);
    while (!m_reqQueue.empty() &&
           std::cmp_greater_equal(toNumber + interval, m_reqQueue.top()->fromNumber()) &&
           m_reqQueue.top()->interval() == interval)
    {
        auto topReq = m_reqQueue.top();
        if (interval == 0)
        {
            /// means all use (number,size)
            // clang-format off
            // "Tops" means that the merge result of all tops can merge at one turn
            // Example:
            // top[x] (fromNumber, size)    range       merged range    merged tops(fromNumber, size)
            // top[0] (1, 3)                [1, 4)      [1, 4)          (1, 3)
            // top[1] (1, 4)                [1, 5)      [1, 5)          (1, 4)
            // top[2] (2, 1)                [2, 3)      [1, 5)          (1, 4)
            // top[3] (2, 4)                [2, 6)      [1, 6)          (1, 5)
            // top[4] (6, 2)                [6, 8)      [1, 8)          (1, 7)
            // top[5] (10, 2)               [10, 12]    can not merge into (1, 7) leave it for next turn
            // clang-format on
            size = std::max(size, (size_t)(topReq->fromNumber() + topReq->size() - fromNumber));
            toNumber = fromNumber + size;
        }
        else
        {
            /// means all use (number,size,interval)
            // clang-format off
            // "Tops" means that the merge result of all tops can merge at one turn
            // Example:
            // top[x] (f, s, it)   range            merged range     merged tops(f, s, it)
            // top[0] (1, 3, 3)    [1, 4, 7]        [1, 4, 7]           (1, 3, 3)
            // top[1] (1, 4, 3)    [1, 4, 7, 10]    [1, 4, 7, 10]       (1, 4, 3)
            // top[2] (4, 2, 3)    [4, 7]           [1, 4, 7, 10]       (1, 4, 3)
            // top[3] (7, 4, 3)    [7, 10, 13, 16]  [1,4,7,10,13,16]    (1, 6, 3)
            // top[4] (16, 1, 3)   [16]             [1,4,7,10,13,16]    (1, 6, 3)
            // top[5] (19, 1, 3)   [19]             [1,4,7,10,13,16,19] (1, 7, 3)
            // top[6] (20, 1, 3)   [20]             can not merge, break
            // clang-format on
            auto mergable = (toNumber >= topReq->fromNumber()) &&
                            ((toNumber - topReq->fromNumber()) % interval == 0);
            if (mergable || std::cmp_equal(interval + toNumber, topReq->fromNumber()))
            {
                // having intersection sequence, take union
                size = (std::max(topReq->toNumber(), toNumber) - fromNumber) / interval + 1;
                toNumber = fromNumber + (size - 1) * interval;
            }
            else
            {
                break;
            }
        }
        m_reqQueue.pop();
    }
    if (c_fileLogLevel == LogLevel::TRACE)
    {
        BLKSYNC_LOG(TRACE) << LOG_BADGE("Download") << LOG_BADGE("Request")
                           << LOG_DESC("Pop reqQueue top req") << LOG_KV("from", fromNumber)
                           << LOG_KV("size", size) << LOG_KV("interval", interval)
                           << LOG_KV("dataFlag", dataFlag);
    }
    return std::make_unique<DownloadRequest>(fromNumber, size, interval, dataFlag);
}

std::set<DownloadRequestQueue::BlockRequest> DownloadRequestQueue::mergeAndPop()
{
    UpgradableGuard lock(x_reqQueue);
    if (m_reqQueue.empty())
    {
        return {};
    }
    UpgradeGuard ulock(lock);
    std::set<DownloadRequestQueue::BlockRequest> fetchSet{};

    while (!m_reqQueue.empty())
    {
        auto topReq = m_reqQueue.top();
        auto interval = (topReq->interval() == 0 ? 1 : topReq->interval());
        for (BlockNumber i = topReq->fromNumber(); i <= topReq->toNumber(); i += interval)
        {
            fetchSet.insert({i, topReq->blockDataFlag() > 0 ?
                                    topReq->blockDataFlag() :
                                    (bcos::ledger::HEADER | bcos::ledger::TRANSACTIONS)});
        }
        m_reqQueue.pop();
    }
    if (c_fileLogLevel == LogLevel::TRACE)
    {
        BLKSYNC_LOG(TRACE) << LOG_BADGE("Download") << LOG_BADGE("Request")
                           << LOG_DESC("Pop reqQueue top req") << LOG_KV("size", fetchSet.size());
    }
    return fetchSet;
}


bool DownloadRequestQueue::empty()
{
    ReadGuard lock(x_reqQueue);
    return m_reqQueue.empty();
}
