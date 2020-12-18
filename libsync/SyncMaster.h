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
 * @brief : Sync implementation
 * @author: jimmyshi
 * @date: 2018-10-15
 */

#pragma once
#include "Common.h"
#include "DownloadingTxsQueue.h"
#include "GossipBlockStatus.h"
#include "NodeTimeMaintenance.h"
#include "RspBlockReq.h"
#include "SyncInterface.h"
#include "SyncMsgEngine.h"
#include "SyncMsgPacketFactory.h"
#include "SyncStatus.h"
#include "SyncTransaction.h"
#include "SyncTreeTopology.h"
#include <libblockchain/BlockChainInterface.h>
#include <libblockverifier/BlockVerifierInterface.h>
#include <libflowlimit/RateLimiter.h>
#include <libnetwork/Common.h>
#include <libnetwork/Session.h>
#include <libp2p/P2PInterface.h>
#include <libprotocol/Common.h>
#include <libprotocol/Exceptions.h>
#include <libtxpool/TxPoolInterface.h>
#include <libutilities/FixedBytes.h>
#include <libutilities/ThreadPool.h>
#include <libutilities/Worker.h>
#include <vector>


namespace bcos
{
namespace sync
{
class SyncMaster : public SyncInterface, public Worker
{
public:
    SyncMaster(std::shared_ptr<bcos::p2p::P2PInterface> _service,
        std::shared_ptr<bcos::txpool::TxPoolInterface> _txPool,
        std::shared_ptr<bcos::blockchain::BlockChainInterface> _blockChain,
        std::shared_ptr<bcos::blockverifier::BlockVerifierInterface> _blockVerifier,
        PROTOCOL_ID const& _protocolId, NodeID const& _nodeId, h256 const& _genesisHash,
        unsigned const& _idleWaitMs = 200, int64_t const& _gossipInterval = 1000,
        int64_t const& _gossipPeers = 3, bool const& _enableSendTxsByTree = false,
        bool const& _enableSendBlockStatusByTree = true, int64_t const& _syncTreeWidth = 3)
      : SyncInterface(),
        Worker("Sync-" + std::to_string(_protocolId), _idleWaitMs),
        m_service(_service),
        m_txPool(_txPool),
        m_blockChain(_blockChain),
        m_blockVerifier(_blockVerifier),
        m_protocolId(_protocolId),
        m_groupId(bcos::protocol::getGroupAndProtocol(_protocolId).first),
        m_nodeId(_nodeId),
        m_genesisHash(_genesisHash),
        m_enableSendTxsByTree(_enableSendTxsByTree),
        m_enableSendBlockStatusByTree(_enableSendBlockStatusByTree)
    {
        /// set thread name
        std::string threadName = "Sync-" + std::to_string(m_groupId);
        setName(threadName);
        // signal registration
        m_blockSubmitted = m_blockChain->onReady([&](int64_t) { this->noteNewBlocks(); });
        m_downloadBlockProcessor =
            std::make_shared<bcos::ThreadPool>("Download-" + std::to_string(m_groupId), 1);
        m_sendBlockProcessor =
            std::make_shared<bcos::ThreadPool>("SyncSend-" + std::to_string(m_groupId), 1);

        // syncStatus should be initialized firstly since it should be deconstruct at final
        m_syncStatus =
            std::make_shared<SyncMasterStatus>(_blockChain, _protocolId, _genesisHash, _nodeId);

        m_txQueue = std::make_shared<DownloadingTxsQueue>(_protocolId, _nodeId);
        m_txQueue->setService(_service);
        m_txQueue->setSyncStatus(m_syncStatus);

        if (m_enableSendTxsByTree)
        {
            auto treeRouter = std::make_shared<TreeTopology>(m_nodeId, _syncTreeWidth);
            m_txQueue->setTreeRouter(treeRouter);
            updateNodeInfo();
            SYNC_LOG(DEBUG) << LOG_DESC("enableSendTxsByTree");
        }

        if (m_enableSendBlockStatusByTree)
        {
            m_syncTreeRouter = std::make_shared<SyncTreeTopology>(_nodeId, _syncTreeWidth);
            SYNC_LOG(DEBUG) << LOG_DESC("enableSendBlockStatusByTree");
            // update the nodeInfo for syncTreeRouter
            updateNodeInfo();

            // create thread to gossip block status
            m_blockStatusGossipThread =
                std::make_shared<GossipBlockStatus>(_protocolId, _gossipInterval, _gossipPeers);
            m_blockStatusGossipThread->registerGossipHandler(
                boost::bind(&SyncMaster::sendBlockStatus, this, _1));
        }
        m_msgEngine = std::make_shared<SyncMsgEngine>(_service, _txPool, _blockChain, m_syncStatus,
            m_txQueue, _protocolId, _nodeId, _genesisHash);
        m_msgEngine->onNotifyWorker([&]() { m_signalled.notify_all(); });

        m_syncTrans = std::make_shared<SyncTransaction>(_service, _txPool, m_txQueue, _protocolId,
            _nodeId, m_syncStatus, m_msgEngine, _blockChain, _idleWaitMs);
    }

    virtual void setMaxBlockQueueSize(int64_t const& _maxBlockQueueSize)
    {
        m_syncStatus->bq().setMaxBlockQueueSize(_maxBlockQueueSize);
    }

    virtual void setTxsStatusGossipMaxPeers(unsigned const& _txsStatusGossipMaxPeers)
    {
        m_syncTrans->setTxsStatusGossipMaxPeers(_txsStatusGossipMaxPeers);
    }

    void setSyncMsgPacketFactory(SyncMsgPacketFactory::Ptr _syncMsgPacketFactory)
    {
        m_syncMsgPacketFactory = _syncMsgPacketFactory;
        m_msgEngine->setSyncMsgPacketFactory(_syncMsgPacketFactory);
    }

    void setNodeTimeMaintenance(NodeTimeMaintenance::Ptr _nodeTimeMaintenance)
    {
        m_msgEngine->setNodeTimeMaintenance(_nodeTimeMaintenance);
        m_nodeTimeMaintenance = _nodeTimeMaintenance;
    }

    NodeTimeMaintenance::Ptr nodeTimeMaintenance() { return m_nodeTimeMaintenance; }

    virtual ~SyncMaster() { stop(); };
    /// start blockSync
    virtual void start() override;
    /// stop blockSync
    virtual void stop() override;
    /// executeTask every idleWaitMs
    virtual void executeTask() override;
    virtual void taskProcessLoop() override;

    /// get status of block sync
    /// @returns Synchonization status
    virtual SyncStatus status() const override;
    virtual std::string const syncInfo() const override;
    virtual void noteSealingBlockNumber(int64_t _number) override;
    virtual bool isSyncing() const override;
    // is my number is far smaller than max block number of this block chain
    bool blockNumberFarBehind() const override;
    /// protocol id used when register handler to p2p module
    virtual PROTOCOL_ID const& protocolId() const override { return m_protocolId; };
    virtual void setProtocolId(PROTOCOL_ID const _protocolId) override
    {
        m_protocolId = _protocolId;
        m_groupId = bcos::protocol::getGroupAndProtocol(m_protocolId).first;
    };

    virtual void registerConsensusVerifyHandler(
        std::function<bool(bcos::protocol::Block const&)> _handler) override
    {
        fp_isConsensusOk = _handler;
    };

    void noteNewTransactions() { m_syncTrans->noteNewTransactions(); }

    void noteNewBlocks()
    {
        m_newBlocks = true;
        m_signalled.notify_all();  // awake executeTask
    }

    void noteDownloadingBegin()
    {
        if (m_syncStatus->state == SyncState::Idle)
            m_syncStatus->state = SyncState::Downloading;
    }

    void noteDownloadingFinish()
    {
        if (m_syncStatus->state == SyncState::Downloading)
            m_syncStatus->state = SyncState::Idle;
    }

    int64_t protocolId() { return m_protocolId; }

    NodeID nodeId() { return m_nodeId; }

    h256 genesisHash() { return m_genesisHash; }

    std::shared_ptr<SyncMasterStatus> syncStatus() { return m_syncStatus; }

    std::shared_ptr<SyncMsgEngine> msgEngine() { return m_msgEngine; }

    void maintainTransactions() { m_syncTrans->maintainTransactions(); }
    void maintainDownloadingTransactions() { m_syncTrans->maintainDownloadingTransactions(); }
    void broadcastSyncStatus(
        bcos::protocol::BlockNumber const& _blockNumber, bcos::h256 const& _currentHash);

    void sendSyncStatusByTree(
        bcos::protocol::BlockNumber const& _blockNumber, h256 const& _currentHash);

    bool sendSyncStatusByNodeId(bcos::protocol::BlockNumber const& blockNumber,
        bcos::h256 const& currentHash, bcos::network::NodeID const& nodeId);
    void sendBlockStatus(int64_t const& _gossipPeersNumber);

    void registerTxsReceiversFilter(
        std::function<std::shared_ptr<bcos::p2p::NodeIDs>(std::shared_ptr<std::set<NodeID>>)>
            _handler) override
    {
        m_syncTrans->registerTxsReceiversFilter(_handler);
    }

    void updateNodeListInfo(bcos::h512s const& _nodeList) override
    {
        if (!m_syncTreeRouter)
        {
            return;
        }
        m_syncTreeRouter->updateNodeListInfo(_nodeList);
    }

    void updateConsensusNodeInfo(
        bcos::h512s const& _consensusNodes, bcos::h512s const& _nodeList) override
    {
        m_txQueue->updateConsensusNodeInfo(_consensusNodes);
        if (!m_syncTreeRouter)
        {
            return;
        }
        m_syncTreeRouter->updateAllNodeInfo(_consensusNodes, _nodeList);
    }

    bool syncTreeRouterEnabled() override { return (m_syncTreeRouter != nullptr); }
    void noteForwardRemainTxs(bcos::h512 const& _targetNodeId) override
    {
        m_syncTrans->noteForwardRemainTxs(_targetNodeId);
    }

    void setBandwidthLimiter(bcos::flowlimit::RateLimiter::Ptr _bandwidthLimiter)
    {
        m_bandwidthLimiter = _bandwidthLimiter;
    }

    void setNodeBandwidthLimiter(bcos::flowlimit::RateLimiter::Ptr _nodeBandwidthLimiter)
    {
        m_nodeBandwidthLimiter = _nodeBandwidthLimiter;
    }

private:
    // init via blockchain when the sync thread started
    void updateNodeInfo()
    {
        auto sealerList = m_blockChain->sealerList();
        std::sort(sealerList.begin(), sealerList.end());
        auto observerList = m_blockChain->observerList();
        auto nodeList = sealerList + observerList;
        std::sort(nodeList.begin(), nodeList.end());
        updateConsensusNodeInfo(sealerList, nodeList);
    }

protected:
    // factory used to create sync related packet
    SyncMsgPacketFactory::Ptr m_syncMsgPacketFactory;

private:
    /// p2p service handler
    std::shared_ptr<bcos::p2p::P2PInterface> m_service;
    /// transaction pool handler
    std::shared_ptr<bcos::txpool::TxPoolInterface> m_txPool;
    /// handler of the block chain module
    std::shared_ptr<bcos::blockchain::BlockChainInterface> m_blockChain;
    /// block verifier
    std::shared_ptr<bcos::blockverifier::BlockVerifierInterface> m_blockVerifier;

    /// Downloading txs queue
    std::shared_ptr<DownloadingTxsQueue> m_txQueue;

    /// Block queue and peers
    std::shared_ptr<SyncMasterStatus> m_syncStatus;

    /// Message handler of p2p
    std::shared_ptr<SyncMsgEngine> m_msgEngine;

    bcos::ThreadPool::Ptr m_downloadBlockProcessor = nullptr;
    bcos::ThreadPool::Ptr m_sendBlockProcessor = nullptr;

    // Internal data
    PROTOCOL_ID m_protocolId;
    GROUP_ID m_groupId;
    NodeID m_nodeId;  ///< Nodeid of this node
    h256 m_genesisHash;
    bool m_enableSendTxsByTree;
    bool m_enableSendBlockStatusByTree;

    int64_t m_maxRequestNumber = 0;
    uint64_t m_lastDownloadingRequestTime = 0;
    int64_t m_lastDownloadingBlockNumber = 0;
    int64_t m_currentSealingNumber = 0;
    int64_t m_eachBlockDownloadingRequestTimeout = 1000;

    // Internal coding variable
    /// mutex to access m_signalled
    boost::mutex x_signalled;
    /// mutex to protect m_currentSealingNumber
    mutable SharedMutex x_currentSealingNumber;

    /// signal to notify all thread to work
    boost::condition_variable m_signalled;

    // sync state
    std::atomic_bool m_newBlocks = {false};
    uint64_t m_maintainBlocksTimeout = 0;
    bool m_needSendStatus = true;
    bool m_isGroupMember = false;

    // sync transactions
    SyncTransaction::Ptr m_syncTrans = nullptr;

    // handler for find the tree router
    SyncTreeTopology::Ptr m_syncTreeRouter = nullptr;

    // thread to gossip block status
    GossipBlockStatus::Ptr m_blockStatusGossipThread = nullptr;

    // settings
    bcos::protocol::Handler<int64_t> m_blockSubmitted;

    // verify handler to check downloading block
    std::function<bool(bcos::protocol::Block const&)> fp_isConsensusOk = nullptr;

    bcos::flowlimit::RateLimiter::Ptr m_bandwidthLimiter;
    bcos::flowlimit::RateLimiter::Ptr m_nodeBandwidthLimiter;
    NodeTimeMaintenance::Ptr m_nodeTimeMaintenance;

public:
    void maintainBlocks();
    void maintainPeersStatus();
    bool maintainDownloadingQueue();  /// return true if downloading finish
    void maintainDownloadingQueueBuffer();
    void maintainPeersConnection();
    void maintainBlockRequest();

private:
    bool isNextBlock(BlockPtr _block);
    void printSyncInfo();
};

}  // namespace sync
}  // namespace bcos
