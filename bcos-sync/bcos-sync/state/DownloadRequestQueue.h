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
 * @file DownloadRequestQueue.h
 * @author: jimmyshi
 * @date 2021-05-24
 */
#pragma once
#include "bcos-sync/BlockSyncConfig.h"
namespace bcos
{
namespace sync
{
class DownloadRequest
{
public:
    using Ptr = std::shared_ptr<DownloadRequest>;
    DownloadRequest(bcos::protocol::BlockNumber _fromNumber, size_t _size)
      : m_fromNumber(_fromNumber), m_size(_size)
    {}

    bcos::protocol::BlockNumber fromNumber() { return m_fromNumber; }
    size_t size() { return m_size; }

private:
    bcos::protocol::BlockNumber m_fromNumber;
    size_t m_size;
};

struct DownloadRequestCmp
{
    bool operator()(DownloadRequest::Ptr const& _first, DownloadRequest::Ptr const& _second)
    {
        if (_first->fromNumber() == _second->fromNumber())
        {
            return _first->size() < _second->size();
        }
        return _first->fromNumber() > _second->fromNumber();
    }
};

class DownloadRequestQueue
{
public:
    using Ptr = std::shared_ptr<DownloadRequestQueue>;
    explicit DownloadRequestQueue(BlockSyncConfig::Ptr _config, bcos::crypto::NodeIDPtr _nodeId)
      : m_config(_config), m_nodeId(_nodeId)
    {}
    virtual ~DownloadRequestQueue() {}

    virtual void push(bcos::protocol::BlockNumber _fromNumber, size_t _size);
    virtual DownloadRequest::Ptr topAndPop();  // Must call use disablePush() before
    virtual bool empty();

private:
    BlockSyncConfig::Ptr m_config;
    bcos::crypto::NodeIDPtr m_nodeId;
    using RequestQueue = std::priority_queue<DownloadRequest::Ptr,
        std::vector<DownloadRequest::Ptr>, DownloadRequestCmp>;
    RequestQueue m_reqQueue;
    mutable SharedMutex x_reqQueue;
};
}  // namespace sync
}  // namespace bcos
