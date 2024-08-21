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
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-sync/BlockSyncConfig.h"
#include <queue>
#include <utility>

namespace bcos::sync
{
class DownloadRequest
{
public:
    using Ptr = std::shared_ptr<DownloadRequest>;
    using UniquePtr = std::unique_ptr<DownloadRequest>;
    DownloadRequest(bcos::protocol::BlockNumber _fromNumber, size_t _size,
        size_t _interval = 0,
        int32_t _dataFlag = (bcos::ledger::HEADER | bcos::ledger::TRANSACTIONS))
      : m_fromNumber(_fromNumber), m_size(_size), m_dataFlag(_dataFlag), m_interval(_interval)
    {}

    bcos::protocol::BlockNumber fromNumber() const noexcept { return m_fromNumber; }
    size_t size() const noexcept { return m_size; }
    size_t interval() const noexcept { return m_interval; }
    bcos::protocol::BlockNumber toNumber() const noexcept
    {
        return (m_interval == 0) ? (protocol::BlockNumber)m_fromNumber + m_size - 1 :
                                   (protocol::BlockNumber)m_fromNumber + (m_size - 1) * m_interval;
    }
    int32_t blockDataFlag() const noexcept { return m_dataFlag; }

private:
    bcos::protocol::BlockNumber m_fromNumber;
    size_t m_size;
    int32_t m_dataFlag = 0;
    size_t m_interval = 0;
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
      : m_config(std::move(_config)), m_nodeId(std::move(_nodeId))
    {}
    virtual ~DownloadRequestQueue() = default;

    struct BlockRequest
    {
        protocol::BlockNumber number;
        int32_t dataFlag;
        bool operator<(const BlockRequest& _other) const { return number < _other.number; }
    };
    [[maybe_unused]] virtual std::set<BlockRequest> mergeAndPop();
    virtual void push(bcos::protocol::BlockNumber _fromNumber, size_t _size, size_t interval = 0,
        int _dataFlag = (bcos::ledger::HEADER | bcos::ledger::TRANSACTIONS));
    virtual DownloadRequest::UniquePtr topAndPop();  // Must call use disablePush() before
    virtual bool empty();

private:
    BlockSyncConfig::Ptr m_config;
    bcos::crypto::NodeIDPtr m_nodeId;
    using RequestQueue = std::priority_queue<DownloadRequest::Ptr,
        std::vector<DownloadRequest::Ptr>, DownloadRequestCmp>;
    RequestQueue m_reqQueue;
    mutable SharedMutex x_reqQueue;
};
}  // namespace bcos::sync
