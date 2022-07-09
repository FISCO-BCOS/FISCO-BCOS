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
 * @brief fake frontService
 * @file FakeFrontService.h
 * @author: yujiechen
 * @date 2021-05-25
 */
#pragma once

#include "../../interfaces/consensus/ConsensusInterface.h"
#include "../../interfaces/front/FrontServiceInterface.h"
#include "../../interfaces/sync/BlockSyncInterface.h"
#include "../../interfaces/txpool/TxPoolInterface.h"
using namespace bcos;
using namespace bcos::front;
using namespace bcos::crypto;
using namespace bcos::protocol;
using namespace bcos::txpool;
using namespace bcos::consensus;
using namespace bcos::sync;

namespace bcos
{
namespace test
{
class FakeGateWay
{
public:
    using Ptr = std::shared_ptr<FakeGateWay>;
    FakeGateWay() = default;
    virtual ~FakeGateWay() {}

    void addTxPool(NodeIDPtr _nodeId, TxPoolInterface::Ptr _txpool)
    {
        m_nodeId2TxPool[_nodeId] = _txpool;
    }

    void addSync(NodeIDPtr _nodeId, BlockSyncInterface::Ptr _sync)
    {
        m_nodeId2Sync[_nodeId] = _sync;
    }

    void addConsensusInterface(NodeIDPtr _nodeId, ConsensusInterface::Ptr _consensusInterface)
    {
        m_nodeId2Consensus[_nodeId] = _consensusInterface;
    }

    virtual void asyncSendMessageByNodeID(int _moduleId, NodeIDPtr _fromNode, NodeIDPtr _nodeId,
        bytesConstRef _data, uint32_t, CallbackFunc _responseCallback)
    {
        m_uuid++;
        auto id = std::to_string(m_uuid);
        insertCallback(_moduleId, id, _responseCallback);

        if (_moduleId == ModuleID::TxsSync && m_nodeId2TxPool.count(_nodeId))
        {
            auto txpool = m_nodeId2TxPool[_nodeId];
            txpool->asyncNotifyTxsSyncMessage(nullptr, id, _fromNode, _data, nullptr);
        }
        if (_moduleId == ModuleID::PBFT && m_nodeId2Consensus.count(_nodeId))
        {
            auto consensus = m_nodeId2Consensus[_nodeId];
            consensus->asyncNotifyConsensusMessage(nullptr, id, _fromNode, _data, nullptr);
        }
        if (_moduleId == ModuleID::BlockSync && m_nodeId2Sync.count(_nodeId))
        {
            auto sync = m_nodeId2Sync[_nodeId];
            sync->asyncNotifyBlockSyncMessage(nullptr, id, _fromNode, _data, nullptr);
        }
    }

    virtual void asyncSendResponse(const std::string& _id, int _moduleId,
        bcos::crypto::NodeIDPtr _nodeID, bytesConstRef _responseData, ReceiveMsgFunc)
    {
        if (m_uuidToCallback.count(_moduleId) && m_uuidToCallback[_moduleId].count(_id))
        {
            auto callback = m_uuidToCallback[_moduleId][_id];
            removeCallback(_moduleId, _id);
            if (callback)
            {
                callback(nullptr, _nodeID, _responseData, "", nullptr);
            }
        }
    }

    void insertCallback(int _moduleID, std::string const& _id, CallbackFunc _callback)
    {
        Guard l(m_mutex);
        m_uuidToCallback[_moduleID][_id] = _callback;
    }

    void removeCallback(int _moduleId, std::string const& _id)
    {
        Guard l(m_mutex);
        m_uuidToCallback[_moduleId].erase(_id);
        if (m_uuidToCallback.count(_moduleId) && m_uuidToCallback[_moduleId].size() == 0)
        {
            m_uuidToCallback.erase(_moduleId);
        }
    }

private:
    std::map<int, std::map<std::string, CallbackFunc>> m_uuidToCallback;
    Mutex m_mutex;

    std::map<NodeIDPtr, TxPoolInterface::Ptr, KeyCompare> m_nodeId2TxPool;
    std::map<NodeIDPtr, ConsensusInterface::Ptr, KeyCompare> m_nodeId2Consensus;
    std::map<NodeIDPtr, BlockSyncInterface::Ptr, KeyCompare> m_nodeId2Sync;
    std::atomic<int64_t> m_uuid = 0;
};

class FakeFrontService : public FrontServiceInterface
{
public:
    using Ptr = std::shared_ptr<FakeFrontService>;
    explicit FakeFrontService(NodeIDPtr _nodeId) : m_nodeId(_nodeId) {}
    void setGateWay(FakeGateWay::Ptr _gateWay) { m_fakeGateWay = _gateWay; }

    ~FakeFrontService() override {}

    void start() override {}
    void stop() override {}

    void asyncGetGroupNodeInfo(GetGroupNodeInfoFunc) override {}
    // for gateway: useless here
    void onReceiveGroupNodeInfo(
        const std::string&, bcos::gateway::GroupNodeInfo::Ptr, ReceiveMsgFunc) override
    {}
    // for gateway: useless here
    void onReceiveMessage(const std::string&, NodeIDPtr, bytesConstRef, ReceiveMsgFunc) override {}
    void asyncSendMessageByNodeIDs(
        int _moduleId, const std::vector<NodeIDPtr>& _nodeIdList, bytesConstRef _data) override
    {
        for (auto node : _nodeIdList)
        {
            if (node->data() == m_nodeId->data())
            {
                continue;
            }
            asyncSendMessageByNodeID(_moduleId, node, _data, 0, nullptr);
        }
    }

    // useless for sync/pbft/txpool
    void asyncSendBroadcastMessage(uint16_t, int _moduleId, bytesConstRef _data) override
    {
        for (auto node : m_nodeIDList)
        {
            if (node->data() == m_nodeId->data())
            {
                continue;
            }
            asyncSendMessageByNodeID(_moduleId, node, _data, 0, nullptr);
        }
    }

    // useless for sync/pbft/txpool
    void onReceiveBroadcastMessage(
        const std::string&, NodeIDPtr, bytesConstRef, ReceiveMsgFunc) override
    {}

    void asyncSendResponse(const std::string& _id, int _moduleId, bcos::crypto::NodeIDPtr _nodeID,
        bytesConstRef _responseData, ReceiveMsgFunc _responseCallback) override
    {
        return m_fakeGateWay->asyncSendResponse(
            _id, _moduleId, _nodeID, _responseData, _responseCallback);
    }

    void asyncSendMessageByNodeID(int _moduleId, NodeIDPtr _nodeId, bytesConstRef _data,
        uint32_t _timeout, CallbackFunc _responseCallback) override
    {
        m_fakeGateWay->asyncSendMessageByNodeID(
            _moduleId, m_nodeId, _nodeId, _data, _timeout, _responseCallback);

        if (m_nodeId2AsyncSendSize.count(_nodeId))
        {
            m_nodeId2AsyncSendSize[_nodeId]++;
        }
        else
        {
            m_nodeId2AsyncSendSize[_nodeId] = 1;
        }
        m_totalSendMsgSize++;
    }

    size_t getAsyncSendSizeByNodeID(NodeIDPtr _nodeId)
    {
        if (!m_nodeId2AsyncSendSize.count(_nodeId))
        {
            return 0;
        }
        return m_nodeId2AsyncSendSize[_nodeId];
    }

    size_t totalSendMsgSize() { return m_totalSendMsgSize; }
    FakeGateWay::Ptr gateWay() { return m_fakeGateWay; }

    void setNodeIDList(bcos::crypto::NodeIDSet const& _nodeIDList) { m_nodeIDList = _nodeIDList; }

private:
    NodeIDPtr m_nodeId;
    std::map<NodeIDPtr, size_t, KeyCompare> m_nodeId2AsyncSendSize;
    size_t m_totalSendMsgSize = 0;
    FakeGateWay::Ptr m_fakeGateWay;
    bcos::crypto::NodeIDSet m_nodeIDList;
};
}  // namespace test
}  // namespace bcos