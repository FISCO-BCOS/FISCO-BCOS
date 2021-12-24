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
#include "bcos-sync/BlockSyncConfig.h"
#include "bcos-sync/state/DownloadingQueue.h"
#include "bcos-sync/state/SyncPeerStatus.h"
#include <bcos-framework/interfaces/sync/BlockSyncInterface.h>
#include <bcos-framework/libutilities/ThreadPool.h>
#include <bcos-framework/libutilities/Timer.h>
#include <bcos-framework/libutilities/Worker.h>
namespace bcos
{
namespace sync
{
class BlockSync : public BlockSyncInterface,
                  public Worker,
                  public std::enable_shared_from_this<BlockSync>
{
public:
    using Ptr = std::shared_ptr<BlockSync>;
    BlockSync(BlockSyncConfig::Ptr _config, unsigned _idleWaitMs = 200);
    ~BlockSync() override {}

    void start() override;
    void stop() override;

    // called by the frontService to dispatch message
    void asyncNotifyBlockSyncMessage(Error::Ptr _error, std::string const& _uuid,
        bcos::crypto::NodeIDPtr _nodeID, bytesConstRef _data,
        std::function<void(Error::Ptr _error)> _onRecv) override;

    void asyncNotifyNewBlock(bcos::ledger::LedgerConfig::Ptr _ledgerConfig,
        std::function<void(Error::Ptr)> _onRecv) override;
    void asyncGetSyncInfo(std::function<void(Error::Ptr, std::string)> _onGetSyncInfo) override;

    void asyncNotifyCommittedIndex(bcos::protocol::BlockNumber _number,
        std::function<void(Error::Ptr _error)> _onRecv) override
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

protected:
    virtual void asyncNotifyBlockSyncMessage(Error::Ptr _error, bcos::crypto::NodeIDPtr _nodeID,
        bytesConstRef _data, std::function<void(bytesConstRef _respData)> _sendResponse,
        std::function<void(Error::Ptr _error)> _onRecv);

    void initSendResponseHandler();
    void executeWorker() override;
    void workerProcessLoop() override;
    // for message handle
    virtual void onPeerStatus(bcos::crypto::NodeIDPtr _nodeID, BlockSyncMsgInterface::Ptr _syncMsg);
    virtual void onPeerBlocks(bcos::crypto::NodeIDPtr _nodeID, BlockSyncMsgInterface::Ptr _syncMsg);
    virtual void onPeerBlocksRequest(
        bcos::crypto::NodeIDPtr _nodeID, BlockSyncMsgInterface::Ptr _syncMsg);

    virtual bool shouldSyncing();
    virtual bool isSyncing();
    virtual void tryToRequestBlocks();
    virtual void onDownloadTimeout();
    // block execute and submit
    virtual void maintainDownloadingQueue();
    virtual void maintainDownloadingBuffer();
    // maintain connections
    virtual void maintainPeersConnection();
    // block requests
    virtual void maintainBlockRequest();
    // broadcast sync status
    virtual void broadcastSyncStatus();

    virtual void onNewBlock(bcos::ledger::LedgerConfig::Ptr _ledgerConfig);

    virtual void downloadFinish();

protected:
    void requestBlocks(bcos::protocol::BlockNumber _from, bcos::protocol::BlockNumber _to);
    void fetchAndSendBlock(DownloadRequestQueue::Ptr _reqQueue, bcos::crypto::PublicPtr _peer,
        bcos::protocol::BlockNumber _number);
    void printSyncInfo();

protected:
    BlockSyncConfig::Ptr m_config;
    SyncPeerStatus::Ptr m_syncStatus;
    DownloadingQueue::Ptr m_downloadingQueue;

    std::function<void(std::string const& _id, int _moduleID, bcos::crypto::NodeIDPtr _dstNode,
        bytesConstRef _data)>
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
    bcos::protocol::BlockNumber c_FaultyNodeBlockDelta = 5;
};
}  // namespace sync
}  // namespace bcos