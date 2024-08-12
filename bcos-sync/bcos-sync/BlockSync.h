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
 * @brief block sync implementation
 * @file BlockSync.h
 * @author: yujiechen
 * @date 2021-05-24
 */
#pragma once
#include "bcos-framework/protocol/ProtocolTypeDef.h"
#include "bcos-sync/BlockSyncConfig.h"
#include "bcos-sync/state/DownloadingQueue.h"
#include "bcos-sync/state/SyncPeerStatus.h"
#include "bcos-sync/utilities/SyncTreeTopology.h"
#include "bcos-tool/NodeTimeMaintenance.h"
#include <bcos-framework/sync/BlockSyncInterface.h>
#include <bcos-utilities/ThreadPool.h>
#include <bcos-utilities/Timer.h>
#include <bcos-utilities/Worker.h>
namespace bcos::sync
{
class BlockSync : public BlockSyncInterface,
                  public Worker,
                  public std::enable_shared_from_this<BlockSync>
{
public:
    using Ptr = std::shared_ptr<BlockSync>;
    // FIXME: make idle configable
    BlockSync(BlockSyncConfig::Ptr _config, unsigned _idleWaitMs = 200);
    ~BlockSync() override = default;

    void start() override;
    void stop() override;

    // called by the frontService to dispatch message, server-side
    void asyncNotifyBlockSyncMessage(Error::Ptr _error, std::string const& _uuid,
        bcos::crypto::NodeIDPtr _nodeID, bytesConstRef _data,
        std::function<void(Error::Ptr)> _onRecv) override;

    // consensus notify sync module to try to commit block
    void asyncNotifyNewBlock(bcos::ledger::LedgerConfig::Ptr _ledgerConfig,
        std::function<void(Error::Ptr)> _onRecv) override;
    // for rpc
    void asyncGetSyncInfo(std::function<void(Error::Ptr, std::string)> _onGetSyncInfo) override;

    std::vector<PeerStatus::Ptr> getPeerStatus() override;

    // consensus notify sync module committed block number
    void asyncNotifyCommittedIndex(
        bcos::protocol::BlockNumber _number, std::function<void(Error::Ptr)> _onRecv) override
    {
        m_config->setCommittedProposalNumber(_number);
        if (_onRecv)
        {
            _onRecv(nullptr);
        }
    }

    virtual void init();
    BlockSyncConfig::Ptr config() { return m_config; }

    void notifyConnectedNodes(bcos::crypto::NodeIDSet const& _connectedNodes,
        std::function<void(Error::Ptr)> _onResponse) override
    {
        m_config->notifyConnectedNodes(_connectedNodes, _onResponse);
    }

    // determine the specified node is faulty or not
    // used to optimize consensus
    bool faultyNode(bcos::crypto::NodeIDPtr _nodeID) override;

    void enableAsMaster(bool _masterNode);

    void setAllowFreeNodeSync(bool flag) { m_allowFreeNode = flag; }

    void setFaultyNodeBlockDelta(bcos::protocol::BlockNumber _delta)
    {
        c_FaultyNodeBlockDelta = _delta;
    }

    bool isSyncing() const override;

    virtual std::optional<std::tuple<bcos::protocol::BlockNumber, bcos::protocol::BlockNumber>>
    getSyncStatus() const override;

protected:
    virtual void asyncNotifyBlockSyncMessage(Error::Ptr _error, bcos::crypto::NodeIDPtr _nodeID,
        bytesConstRef _data, std::function<void(bytesConstRef)> _sendResponse,
        std::function<void(Error::Ptr)> _onRecv);

    void initSendResponseHandler();
    void executeWorker() override;
    void workerProcessLoop() override;
    /// for message handle
    // call when receive BlockStatusPacket, update peers status
    virtual void onPeerStatus(bcos::crypto::NodeIDPtr _nodeID, BlockSyncMsgInterface::Ptr _syncMsg);
    // call when receive BlockResponsePacket, receive block and push in queue
    virtual void onPeerBlocks(bcos::crypto::NodeIDPtr _nodeID, BlockSyncMsgInterface::Ptr _syncMsg);
    // call when receive BlockRequestPacket, send block to peer
    virtual void onPeerBlocksRequest(
        bcos::crypto::NodeIDPtr _nodeID, BlockSyncMsgInterface::Ptr _syncMsg);

    virtual bool shouldSyncing();
    virtual void tryToRequestBlocks();
    virtual void onDownloadTimeout();
    // block execute and submit
    virtual void maintainDownloadingQueue();
    virtual void maintainDownloadingBuffer();
    // maintain connections
    virtual void maintainPeersConnection();
    // block requests
    virtual void maintainBlockRequest();
    // send sync status by tree
    virtual void sendSyncStatusByTree();
    // broadcast sync status
    virtual void broadcastSyncStatus();

    virtual void onNewBlock(bcos::ledger::LedgerConfig::Ptr _ledgerConfig);

    virtual void downloadFinish();

    // update SyncTreeTopology node info
    virtual void updateTreeTopologyNodeInfo();

    virtual void syncArchivedBlockBody(bcos::protocol::BlockNumber archivedBlockNumber);
    virtual void verifyAndCommitArchivedBlock(bcos::protocol::BlockNumber archivedBlockNumber);

    void requestBlocks(
        bcos::protocol::BlockNumber _from, bcos::protocol::BlockNumber _to, int32_t blockDataFlag);
    void fetchAndSendBlock(bcos::crypto::PublicPtr const& _peer,
        bcos::protocol::BlockNumber _number, int32_t _blockDataFlag);
    void printSyncInfo();

protected:
    BlockSyncConfig::Ptr m_config;
    SyncPeerStatus::Ptr m_syncStatus;
    DownloadingQueue::Ptr m_downloadingQueue;

    std::function<void(std::string const&, int, bcos::crypto::NodeIDPtr, bytesConstRef)>
        m_sendResponseHandler;

    bcos::ThreadPool::Ptr m_downloadBlockProcessor = nullptr;
    bcos::ThreadPool::Ptr m_sendBlockProcessor = nullptr;
    std::shared_ptr<Timer> m_downloadingTimer;

    std::atomic_bool m_running = {false};
    std::atomic<SyncState> m_state = {SyncState::Idle};
    std::atomic<bcos::protocol::BlockNumber> m_maxRequestNumber = {0};

    boost::condition_variable m_signalled;
    boost::mutex x_signalled;
    bcos::protocol::BlockNumber m_waterMark = 10;
    bcos::protocol::BlockNumber c_FaultyNodeBlockDelta = 50;

    std::atomic_bool m_masterNode = {false};
    bool m_allowFreeNode = false;

    mutable SharedMutex x_archivedBlockQueue;

private:
    struct BlockLess
    {
        bool operator()(bcos::protocol::Block::Ptr const& _first,
            bcos::protocol::Block::Ptr const& _second) const
        {
            return _first->blockHeader()->number() < _second->blockHeader()->number();
        }
    };
    std::priority_queue<bcos::protocol::Block::Ptr, bcos::protocol::Blocks, BlockLess>
        m_archivedBlockQueue;  // top block is the max number block
    std::chrono::system_clock::time_point m_lastArchivedRequestTime =
        std::chrono::system_clock::now();

    SyncTreeTopology::Ptr m_syncTreeTopology{nullptr};
};
}  // namespace bcos::sync
