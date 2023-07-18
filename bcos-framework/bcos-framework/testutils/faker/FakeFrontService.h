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

#include <utility>

#include "../../consensus/ConsensusInterface.h"
#include "../../front/FrontServiceInterface.h"
#include "../../sync/BlockSyncInterface.h"
#include "../../txpool/TxPoolInterface.h"
#include "bcos-task/Wait.h"
using namespace bcos;
using namespace bcos::front;
using namespace bcos::crypto;
using namespace bcos::protocol;
using namespace bcos::txpool;
using namespace bcos::consensus;
using namespace bcos::sync;

namespace bcos::test
{
class FakeGroupInfo : public bcos::gateway::GroupNodeInfo
{
public:
    FakeGroupInfo() = default;

    void setGroupID(const std::string& _groupID) override { m_gid = _groupID; }
    void setNodeIDList(std::vector<std::string>&& _nodeIDList) override
    {
        m_nodeIdList = _nodeIDList;
    }
    void setNodeTypeList(std::vector<int32_t>&& _nodeTypeList) override
    {
        m_nodeTypeList = _nodeTypeList;
    }
    void appendNodeID(const std::string& _nodeID) override { m_nodeIdList.push_back(_nodeID); }
    void appendProtocol(bcos::protocol::ProtocolInfo::ConstPtr _protocol) override
    {
        m_protocolList.push_back(_protocol);
    }
    void setType(uint16_t _type) override {}
    const std::string& groupID() const override { return m_gid; }
    const std::vector<std::string>& nodeIDList() const override { return m_nodeIdList; }
    const std::vector<int32_t>& nodeTypeList() const override { return m_nodeTypeList; }
    int type() const override { return 0; }
    void setNodeProtocolList(
        std::vector<bcos::protocol::ProtocolInfo::ConstPtr>&& _protocolList) override
    {
        m_protocolList = _protocolList;
    }
    const std::vector<bcos::protocol::ProtocolInfo::ConstPtr>& nodeProtocolList() const override
    {
        return m_protocolList;
    }
    ProtocolInfo::ConstPtr protocol(uint64_t _index) const override
    {
        return m_protocolList.at(_index);
    }

private:
    std::string m_gid = "";
    std::vector<bcos::protocol::ProtocolInfo::ConstPtr> m_protocolList;
    std::vector<std::string> m_nodeIdList;
    std::vector<int32_t> m_nodeTypeList;
};

class FakeGateWay
{
public:
    using Ptr = std::shared_ptr<FakeGateWay>;
    FakeGateWay() = default;
    virtual ~FakeGateWay() = default;

    void addTxPool(NodeIDPtr _nodeId, TxPoolInterface::Ptr _txpool)
    {
        m_nodeId2TxPool[_nodeId] = std::move(_txpool);
    }

    void addSync(NodeIDPtr _nodeId, BlockSyncInterface::Ptr _sync)
    {
        m_nodeId2Sync[_nodeId] = std::move(_sync);
    }

    void addConsensusInterface(NodeIDPtr _nodeId, ConsensusInterface::Ptr _consensusInterface)
    {
        m_nodeId2Consensus[_nodeId] = std::move(_consensusInterface);
    }

    virtual void asyncSendMessageByNodeID(int _moduleId, NodeIDPtr _fromNode, NodeIDPtr _nodeId,
        bytesConstRef _data, uint32_t, CallbackFunc _responseCallback)
    {
        std::string id;
        {
            Guard l(m_mutex);
            m_uuid++;
            id = std::to_string(m_uuid);
            if (_responseCallback)
            {
                m_uuidToCallback[id] = _responseCallback;
            }
            if (_responseCallback)
            {
                std::cout << "asyncSendMessageByNodeID, from: " << _fromNode
                          << _fromNode->shortHex() << ", to: " << _nodeId->shortHex()
                          << ", id:" << id << std::endl;
            }
        }

        if (_moduleId == ModuleID::TxsSync && m_nodeId2TxPool.contains(_nodeId))
        {
            auto txpool = m_nodeId2TxPool[_nodeId];
            txpool->asyncNotifyTxsSyncMessage(nullptr, id, _fromNode, _data, nullptr);
        }
        if (_moduleId == ModuleID::ConsTxsSync && m_nodeId2TxPool.contains(_nodeId))
        {
            auto txpool = m_nodeId2TxPool[_nodeId];
            txpool->asyncNotifyTxsSyncMessage(nullptr, id, _fromNode, _data, nullptr);
        }

        if (_moduleId == ModuleID::PBFT && m_nodeId2Consensus.contains(_nodeId))
        {
            auto consensus = m_nodeId2Consensus[_nodeId];
            consensus->asyncNotifyConsensusMessage(nullptr, id, _fromNode, _data, nullptr);
        }
        if (_moduleId == ModuleID::BlockSync && m_nodeId2Sync.contains(_nodeId))
        {
            auto sync = m_nodeId2Sync[_nodeId];
            sync->asyncNotifyBlockSyncMessage(nullptr, id, _fromNode, _data, nullptr);
        }

        if (_moduleId == ModuleID::TREE_PUSH_TRANSACTION && m_nodeId2TxPool.contains(_nodeId))
        {
            auto txpool = m_nodeId2TxPool[_nodeId];
            auto txFactory =
                std::make_shared<bcostars::protocol::TransactionFactoryImpl>(m_cryptoSuite);
            auto tx = txFactory->createTransaction(_data);
            bcos::task::wait([](decltype(txpool) txpool, decltype(_data) _data,
                                 decltype(tx) tx) -> bcos::task::Task<void> {
                auto submit = co_await txpool->submitTransactionWithHook(tx, [_data, txpool]() {
                    bcos::task::wait([_data](decltype(txpool) txpool) -> task::Task<void> {
                        co_await txpool->broadcastTransactionBufferByTree(_data);
                    }(txpool));
                });
                BOOST_CHECK(submit->status() == (uint32_t)TransactionStatus::None);
            }(txpool, _data, tx));
        }

        if (_moduleId == ModuleID::SYNC_PUSH_TRANSACTION && m_nodeId2TxPool.contains(_nodeId))
        {
            auto txpool = m_nodeId2TxPool[_nodeId];
            auto txFactory =
                std::make_shared<bcostars::protocol::TransactionFactoryImpl>(m_cryptoSuite);
            auto tx = txFactory->createTransaction(_data);
            bcos::task::wait(
                [](decltype(txpool) txpool, decltype(tx) tx) -> bcos::task::Task<void> {
                    auto submit = co_await txpool->submitTransaction(tx);
                    BOOST_CHECK(submit->status() == (uint32_t)TransactionStatus::None);
                }(txpool, tx));
        }
    }

    virtual void asyncSendResponse(const std::string& _id, int, bcos::crypto::NodeIDPtr _nodeID,
        bytesConstRef _responseData, ReceiveMsgFunc _receiveCallback)
    {
        CallbackFunc callback = nullptr;
        {
            Guard l(m_mutex);
            if (m_uuidToCallback.count(_id))
            {
                callback = m_uuidToCallback[_id];
                m_uuidToCallback.erase(_id);
            }
        }
        if (callback)
        {
            callback(nullptr, _nodeID, _responseData, "", nullptr);
            std::cout << "### find callback, id: " << _id << std::endl;
        }
        else
        {
            std::cout << "### not find callback for the id: " << _id << std::endl;
        }
        _receiveCallback(nullptr);
    }

public:
    std::map<std::string, CallbackFunc> m_uuidToCallback;
    Mutex m_mutex;

    std::map<NodeIDPtr, TxPoolInterface::Ptr, KeyCompare> m_nodeId2TxPool;
    std::map<NodeIDPtr, ConsensusInterface::Ptr, KeyCompare> m_nodeId2Consensus;
    std::map<NodeIDPtr, BlockSyncInterface::Ptr, KeyCompare> m_nodeId2Sync;
    std::atomic<int64_t> m_uuid = 0;
    CryptoSuite::Ptr m_cryptoSuite;
};

class FakeFrontService : public FrontServiceInterface
{
public:
    using Ptr = std::shared_ptr<FakeFrontService>;
    explicit FakeFrontService(NodeIDPtr _nodeId) : m_nodeId(std::move(_nodeId)) {}
    void setGateWay(FakeGateWay::Ptr _gateWay) { m_fakeGateWay = std::move(_gateWay); }

    ~FakeFrontService() override = default;

    void start() override {}
    void stop() override {}

    void asyncGetGroupNodeInfo(GetGroupNodeInfoFunc _func) override { _func(nullptr, m_groupInfo); }
    // for gateway: useless here
    void onReceiveGroupNodeInfo(
        const std::string&, bcos::gateway::GroupNodeInfo::Ptr, ReceiveMsgFunc) override
    {}
    // for gateway: useless here
    void onReceiveMessage(
        const std::string&, const NodeIDPtr&, bytesConstRef, ReceiveMsgFunc) override
    {}
    void asyncSendMessageByNodeIDs(
        int _moduleId, const std::vector<NodeIDPtr>& _nodeIdList, bytesConstRef _data) override
    {
        for (const auto& node : _nodeIdList)
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
        for (const auto& node : m_nodeIDList)
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

        if (m_nodeId2AsyncSendSize.contains(_nodeId))
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
        if (!m_nodeId2AsyncSendSize.contains(_nodeId))
        {
            return 0;
        }
        return m_nodeId2AsyncSendSize[_nodeId];
    }

    size_t totalSendMsgSize() { return m_totalSendMsgSize; }
    FakeGateWay::Ptr gateWay() { return m_fakeGateWay; }

    void setNodeIDList(bcos::crypto::NodeIDSet const& _nodeIDList) { m_nodeIDList = _nodeIDList; }
    void setGroupInfo(bcos::gateway::GroupNodeInfo::Ptr _groupInfo)
    {
        m_groupInfo = std::move(_groupInfo);
    }

private:
    NodeIDPtr m_nodeId;
    std::map<NodeIDPtr, size_t, KeyCompare> m_nodeId2AsyncSendSize;
    size_t m_totalSendMsgSize = 0;
    FakeGateWay::Ptr m_fakeGateWay;
    bcos::crypto::NodeIDSet m_nodeIDList;
    bcos::gateway::GroupNodeInfo::Ptr m_groupInfo;
};
}  // namespace bcos::test