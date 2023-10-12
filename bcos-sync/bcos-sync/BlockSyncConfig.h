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
 * @brief config for the block sync
 * @file BlockSyncConfig.h
 * @author: yujiechen
 * @date 2021-05-24
 */
#pragma once
#include "bcos-sync/interfaces/BlockSyncMsgFactory.h"
#include <bcos-crypto/interfaces/crypto/KeyInterface.h>
#include <bcos-framework/consensus/ConsensusInterface.h>
#include <bcos-framework/dispatcher/SchedulerInterface.h>
#include <bcos-framework/front/FrontServiceInterface.h>
#include <bcos-framework/ledger/LedgerInterface.h>
#include <bcos-framework/protocol/BlockFactory.h>
#include <bcos-framework/protocol/TransactionSubmitResultFactory.h>
#include <bcos-framework/sync/SyncConfig.h>
#include <bcos-framework/txpool/TxPoolInterface.h>
#include <bcos-tool/NodeTimeMaintenance.h>
#include <bcos-utilities/CallbackCollectionHandler.h>

namespace bcos::sync
{
class BlockSyncConfig : public SyncConfig
{
public:
    using Ptr = std::shared_ptr<BlockSyncConfig>;
    BlockSyncConfig(bcos::crypto::PublicPtr _nodeId, bcos::ledger::LedgerInterface::Ptr _ledger,
        bcos::txpool::TxPoolInterface::Ptr _txpool, bcos::protocol::BlockFactory::Ptr _blockFactory,
        bcos::protocol::TransactionSubmitResultFactory::Ptr _txResultFactory,
        bcos::front::FrontServiceInterface::Ptr _frontService,
        bcos::scheduler::SchedulerInterface::Ptr _scheduler,
        bcos::consensus::ConsensusInterface::Ptr _consensus, BlockSyncMsgFactory::Ptr _msgFactory,
        bcos::tool::NodeTimeMaintenance::Ptr _nodeTimeMaintenance,
        bool _enableSendBlockStatusByTree = false, std::uint32_t _syncTreeWidth = 3)
      : SyncConfig(std::move(_nodeId)),
        m_ledger(std::move(_ledger)),
        m_txpool(std::move(_txpool)),
        m_blockFactory(std::move(_blockFactory)),
        m_txResultFactory(std::move(_txResultFactory)),
        m_frontService(std::move(_frontService)),
        m_scheduler(std::move(_scheduler)),
        m_consensus(std::move(_consensus)),
        m_msgFactory(std::move(_msgFactory)),
        m_nodeTimeMaintenance(std::move(_nodeTimeMaintenance)),
        m_enableSendBlockStatusByTree(_enableSendBlockStatusByTree),
        m_syncTreeWidth(_syncTreeWidth)
    {}
    ~BlockSyncConfig() override = default;

    bcos::ledger::LedgerInterface::Ptr ledger() { return m_ledger; }
    bcos::protocol::BlockFactory::Ptr blockFactory() { return m_blockFactory; }
    bcos::front::FrontServiceInterface::Ptr frontService() { return m_frontService; }
    bcos::scheduler::SchedulerInterface::Ptr scheduler() { return m_scheduler; }
    bcos::consensus::ConsensusInterface::Ptr consensus() { return m_consensus; }
    bcos::tool::NodeTimeMaintenance::Ptr nodeTimeMaintenance() { return m_nodeTimeMaintenance; }

    BlockSyncMsgFactory::Ptr msgFactory() { return m_msgFactory; }
    virtual void resetConfig(bcos::ledger::LedgerConfig::Ptr _ledgerConfig);

    bcos::crypto::HashType const& genesisHash() const { return m_genesisHash; }
    void setGenesisHash(bcos::crypto::HashType const& _hash);

    bcos::protocol::BlockNumber blockNumber() const { return m_blockNumber; }
    bcos::crypto::HashType const& hash() const;

    bcos::protocol::BlockNumber nextBlock() const { return m_nextBlock; }
    bcos::protocol::BlockNumber applyingBlock() const { return m_applyingBlock; }
    void setApplyingBlock(bcos::protocol::BlockNumber _number);
    void resetBlockInfo(
        bcos::protocol::BlockNumber _blockNumber, bcos::crypto::HashType const& _hash);

    void setKnownHighestNumber(bcos::protocol::BlockNumber _highestNumber);
    bcos::protocol::BlockNumber knownHighestNumber() { return m_knownHighestNumber; }

    void setKnownLatestHash(bcos::crypto::HashType const& _hash);

    bcos::crypto::HashType const& knownLatestHash();

    size_t maxDownloadingBlockQueueSize() const { return m_maxDownloadingBlockQueueSize; }
    void setMaxDownloadingBlockQueueSize(size_t _maxDownloadingBlockQueueSize);

    void setMaxDownloadRequestQueueSize(size_t _maxDownloadRequestQueueSize);

    size_t maxDownloadRequestQueueSize() const { return m_maxDownloadRequestQueueSize; }

    size_t downloadTimeout() const { return m_downloadTimeout; }

    size_t maxRequestBlocks() const { return m_maxRequestBlocks; }
    size_t maxShardPerPeer() const { return m_maxShardPerPeer; }

    void setExecutedBlock(bcos::protocol::BlockNumber _executedBlock);
    bcos::protocol::BlockNumber executedBlock() { return m_executedBlock; }

    bcos::txpool::TxPoolInterface::Ptr txpool() { return m_txpool; }
    bcos::protocol::TransactionSubmitResultFactory::Ptr txResultFactory()
    {
        return m_txResultFactory;
    }

    void setCommittedProposalNumber(bcos::protocol::BlockNumber _committedProposalNumber)
    {
        m_committedProposalNumber = _committedProposalNumber;
    }

    bcos::protocol::BlockNumber committedProposalNumber() const
    {
        return m_committedProposalNumber;
    }

    bcos::protocol::NodeType nodeType() const { return m_nodeType; }

    void registerOnNodeTypeChanged(std::function<void(bcos::protocol::NodeType)> _onNodeTypeChanged)
    {
        m_nodeTypeChanged = std::move(_onNodeTypeChanged);
    }

    void setMasterNode(bool _masterNode)
    {
        Guard lock(m_mutex);
        m_masterNode = _masterNode;
        // notify nodeType to the gateway
        if (m_nodeTypeChanged)
        {
            m_nodeTypeChanged(nodeType());
        }
    }

    bool masterNode() const { return m_masterNode; }

    bcos::protocol::BlockNumber archiveBlockNumber() const;

    bool enableSendBlockStatusByTree() const { return m_enableSendBlockStatusByTree; }
    std::int64_t syncTreeWidth() const { return m_syncTreeWidth; }

    std::string printBlockSyncState() const noexcept
    {
        std::stringstream stringstream;
        stringstream << LOG_KV("number", m_blockNumber) << LOG_KV("applyingBlock", m_applyingBlock)
                     << LOG_KV("nextBlock", m_nextBlock) << LOG_KV("executedBlock", m_executedBlock)
                     << LOG_KV("highestNumber", m_knownHighestNumber);
        return stringstream.str();
    }

protected:
    void setHash(bcos::crypto::HashType const& _hash);

    // Note: this only be called after block on-chain successfully
    virtual bcos::protocol::NodeType determineNodeType();
    bool existNode(bcos::consensus::ConsensusNodeListPtr const& _nodeList, SharedMutex& _lock,
        bcos::crypto::NodeIDPtr _nodeID);

private:
    bcos::ledger::LedgerInterface::Ptr m_ledger;
    bcos::txpool::TxPoolInterface::Ptr m_txpool;
    bcos::protocol::BlockFactory::Ptr m_blockFactory;
    bcos::protocol::TransactionSubmitResultFactory::Ptr m_txResultFactory;
    bcos::front::FrontServiceInterface::Ptr m_frontService;
    bcos::scheduler::SchedulerInterface::Ptr m_scheduler;
    bcos::consensus::ConsensusInterface::Ptr m_consensus;
    BlockSyncMsgFactory::Ptr m_msgFactory;
    bcos::tool::NodeTimeMaintenance::Ptr m_nodeTimeMaintenance;

    bcos::crypto::HashType m_genesisHash;
    std::atomic<bcos::protocol::BlockNumber> m_blockNumber = {0};
    std::atomic<bcos::protocol::BlockNumber> m_applyingBlock = {0};
    std::atomic<bcos::protocol::BlockNumber> m_nextBlock = {0};
    std::atomic<bcos::protocol::BlockNumber> m_executedBlock = {0};
    bcos::crypto::HashType m_hash;
    mutable SharedMutex x_hash;

    std::atomic<bcos::protocol::BlockNumber> m_knownHighestNumber = {0};
    bcos::crypto::HashType m_knownLatestHash;
    mutable SharedMutex x_knownLatestHash;
    mutable Mutex m_mutex;

    std::atomic<size_t> m_maxDownloadingBlockQueueSize = MAX_DOWNLOAD_BLOCK_QUEUE_SIZE;
    std::atomic<size_t> m_maxDownloadRequestQueueSize = MAX_DOWNLOAD_REQUEST_QUEUE_SIZE;
    // the max number of blocks this node can request to
    std::atomic<size_t> m_maxRequestBlocks = MAX_REQUEST_BLOCKS_COUNT;
    std::atomic<size_t> m_downloadTimeout = (DOWNLOAD_TIMEOUT_TTL * m_maxRequestBlocks);

    std::atomic<size_t> m_maxShardPerPeer = {2};
    std::atomic<bcos::protocol::BlockNumber> m_committedProposalNumber = {0};

    bcos::protocol::NodeType m_nodeType = bcos::protocol::NodeType::None;
    bcos::protocol::NodeType m_notifiedNodeType = bcos::protocol::NodeType::None;

    std::function<void(bcos::protocol::NodeType)> m_nodeTypeChanged;

    std::atomic_bool m_masterNode = {false};

    bool m_enableSendBlockStatusByTree = false;
    std::uint32_t m_syncTreeWidth;
};
}  // namespace bcos::sync