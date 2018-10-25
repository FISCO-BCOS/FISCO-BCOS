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
#include "SyncInterface.h"
#include "SyncMsgEngine.h"
#include "SyncStatus.h"
#include <libblockchain/BlockChainInterface.h>
#include <libblockverifier/BlockVerifierInterface.h>
#include <libdevcore/FixedHash.h>
#include <libdevcore/Worker.h>
#include <libethcore/Exceptions.h>
#include <libp2p/Common.h>
#include <libp2p/P2PInterface.h>
#include <libp2p/Session.h>
#include <libtxpool/TxPoolInterface.h>
#include <vector>


namespace dev
{
namespace sync
{
class SyncMaster : public SyncInterface, public Worker
{
public:
    SyncMaster(std::shared_ptr<dev::p2p::P2PInterface> _service,
        std::shared_ptr<dev::txpool::TxPoolInterface> _txPool,
        std::shared_ptr<dev::blockchain::BlockChainInterface> _blockChain,
        std::shared_ptr<dev::blockverifier::BlockVerifierInterface> _blockVerifier,
        int16_t const& _protocolId, NodeID const& _nodeId, h256 const& _genesisHash,
        unsigned _idleWaitMs = 30)
      : SyncInterface(),
        Worker("SyncMaster-" + std::to_string(_protocolId), _idleWaitMs),
        m_service(_service),
        m_txPool(_txPool),
        m_blockChain(_blockChain),
        m_blockVerifier(_blockVerifier),
        m_protocolId(_protocolId),
        m_nodeId(_nodeId),
        m_genesisHash(_genesisHash)
    {
        m_syncStatus = std::make_shared<SyncMasterStatus>(_genesisHash);
        m_msgEngine = std::make_shared<SyncMsgEngine>(
            _service, _txPool, _blockChain, m_syncStatus, _protocolId, _nodeId, _genesisHash);

        // signal registration
        m_txPool->onReady([=]() { this->noteNewTransactions(); });
        m_blockChain->onReady([=]() { this->noteNewBlocks(); });
    }

    virtual ~SyncMaster(){};
    /// start blockSync
    virtual void start() override;
    /// stop blockSync
    virtual void stop() override;
    /// doWork every idleWaitMs
    virtual void doWork() override;
    virtual void workLoop() override;

    /// get status of block sync
    /// @returns Synchonization status
    virtual SyncStatus status() const override;
    virtual bool isSyncing() const override;
    // virtual h256 latestBlockSent() override;

    /// for rpc && sdk: broad cast transaction to all nodes
    // virtual void broadCastTransactions() override;
    /// for p2p: broad cast transaction to specified nodes
    // virtual void sendTransactions(NodeList const& _nodes) override;

    /// abort sync and reset all status of blockSyncs
    // virtual void reset() override;
    // virtual bool forceSync() override;

    /// protocol id used when register handler to p2p module
    virtual int16_t const& protocolId() const override { return m_protocolId; };
    virtual void setProtocolId(int16_t const _protocolId) override { m_protocolId = _protocolId; };

    void noteNewTransactions()
    {
        m_newTransactions = true;
        m_signalled.notify_all();  // awake doWork
    }

    void noteNewBlocks()
    {
        m_newBlocks = true;
        m_signalled.notify_all();  // awake doWork
    }

    void noteDownloadingBegin()
    {
        if (m_state == SyncState::Idle)
            m_state = SyncState::Downloading;
    }

    void noteDownloadingFinish()
    {
        if (m_state == SyncState::Downloading)
            m_state = SyncState::Idle;
    }

    int64_t protocolId() { return m_protocolId; }

    NodeID nodeId() { return m_nodeId; }

    h256 genesisHash() { return m_genesisHash; }

    std::shared_ptr<SyncMasterStatus> syncStatus() { return m_syncStatus; }

    std::shared_ptr<SyncMsgEngine> msgEngine() { return m_msgEngine; }

private:
    /// p2p service handler
    std::shared_ptr<dev::p2p::P2PInterface> m_service;
    /// transaction pool handler
    std::shared_ptr<dev::txpool::TxPoolInterface> m_txPool;
    /// handler of the block chain module
    std::shared_ptr<dev::blockchain::BlockChainInterface> m_blockChain;
    /// block verifier
    std::shared_ptr<dev::blockverifier::BlockVerifierInterface> m_blockVerifier;
    /// Block queue and peers
    std::shared_ptr<SyncMasterStatus> m_syncStatus;
    /// Message handler of p2p
    std::shared_ptr<SyncMsgEngine> m_msgEngine;

    // Internal data
    int16_t m_protocolId;
    NodeID m_nodeId;  ///< Nodeid of this node
    h256 m_genesisHash;

    unsigned m_startingBlock = 0;  ///< Last block number for the start of sync
    unsigned m_highestBlock = 0;   ///< Highest block number seen

    // Internal coding variable
    /// mutex
    mutable SharedMutex x_sync;
    /// mutex to access m_signalled
    Mutex x_signalled;
    /// signal to notify all thread to work
    std::condition_variable m_signalled;

    // sync state
    SyncState m_state = SyncState::Idle;
    bool m_newTransactions = false;
    bool m_newBlocks = false;

    // settings
    int64_t m_maxBlockDownloadQueueSize = 5;

public:
    void maintainTransactions();
    void maintainBlocks();
    void maintainPeersStatus();
    bool maintainDownloadingQueue();  /// return true if downloading finish
    void maintainPeersConnection();
};

}  // namespace sync
}  // namespace dev