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
 * @brief implementation of Consensus Config
 * @file ConsensusConfig.h
 * @author: yujiechen
 * @date 2021-04-09
 */
#pragma once
#include "Common.h"
#include "bcos-framework/consensus/ConsensusConfigInterface.h"
#include "bcos-framework/protocol/Protocol.h"
#include <bcos-crypto/interfaces/crypto/KeyPairInterface.h>
#include <bcos-utilities/Common.h>

namespace bcos::consensus
{
class ConsensusConfig : public ConsensusConfigInterface
{
public:
    using Ptr = std::shared_ptr<ConsensusConfig>;
    using ConstPtr = std::shared_ptr<const ConsensusConfig>;
    explicit ConsensusConfig(bcos::crypto::KeyPairInterface::Ptr _keyPair)
      : m_keyPair(std::move(_keyPair))
    {}
    virtual ~ConsensusConfig() {}

    // the NodeID of the consensus node
    bcos::crypto::PublicPtr nodeID() const override { return m_keyPair->publicKey(); }

    // the nodeIndex of this node
    IndexType nodeIndex() const override { return m_nodeIndex; }

    bool isConsensusNode() const override
    {
        return (m_nodeIndex != NON_CONSENSUS_NODE) && m_asMasterNode.load();
    }
    // the consensus node list
    ConsensusNodeList consensusNodeList() const override;
    bcos::crypto::NodeIDs consensusNodeIDList(bool _excludeSelf = true) const override;

    uint64_t consensusTimeout() const override { return m_consensusTimeout; }

    void setConsensusNodeList(ConsensusNodeList _consensusNodeList) override;

    void setConsensusTimeout(uint64_t _consensusTimeout) override
    {
        m_consensusTimeout.store(_consensusTimeout);
    }

    // Note: After the block sync,
    // need to set the committedProposal of the consensus in the ordering phase
    void setCommittedProposal(ProposalInterface::Ptr _committedProposal) override
    {
        WriteGuard l(x_committedProposal);
        m_committedProposal = _committedProposal;
        auto index = m_committedProposal->index() + 1;
        if (m_progressedIndex < index)
        {
            m_progressedIndex = index;
        }
    }

    ProposalInterface::ConstPtr committedProposal() override
    {
        ReadGuard l(x_committedProposal);
        if (!m_committedProposal)
        {
            return nullptr;
        }
        return std::const_pointer_cast<ProposalInterface const>(m_committedProposal);
    }

    virtual bcos::protocol::BlockNumber progressedIndex() { return m_progressedIndex; }
    virtual void setProgressedIndex(bcos::protocol::BlockNumber _progressedIndex)
    {
        m_progressedIndex = _progressedIndex;
        CONSENSUS_LOG(DEBUG) << LOG_DESC("PBFTConfig: setProgressedIndex")
                             << LOG_KV("progressedIndex", m_progressedIndex);
    }

    virtual void updateQuorum() = 0;
    IndexType getNodeIndexByNodeID(bcos::crypto::PublicPtr _nodeID);
    ConsensusNode* getConsensusNodeByIndex(IndexType _nodeIndex);
    bcos::crypto::KeyPairInterface::Ptr keyPair() { return m_keyPair; }

    virtual void setBlockTxCountLimit(uint64_t _blockTxCountLimit)
    {
        m_blockTxCountLimit = _blockTxCountLimit;
    }
    virtual uint64_t blockTxCountLimit() const { return m_blockTxCountLimit.load(); }
    bcos::protocol::BlockNumber syncingHighestNumber() const { return m_syncingHighestNumber; }
    void setSyncingHighestNumber(bcos::protocol::BlockNumber _number)
    {
        m_syncingHighestNumber = _number;
    }

    IndexType consensusNodesNum() const { return m_consensusNodeNum.load(); }

    void setObserverNodeList(ConsensusNodeList _observerNodeList);

    bool asMasterNode() const { return m_asMasterNode.load(); }
    virtual void enableAsMasterNode(bool _isMasterNode)
    {
        m_asMasterNode.store(_isMasterNode);
        if (m_versionNotification)
        {
            m_versionNotification(m_compatibilityVersion);
        }
    }

    virtual void registerVersionInfoNotification(std::function<void(uint32_t)> _versionNotification)
    {
        m_versionNotification = std::move(_versionNotification);
    }

    uint32_t compatibilityVersion() const { return m_compatibilityVersion; }

    virtual bool shouldRotateSealers(protocol::BlockNumber) const { return false; }
    static bool compareConsensusNode(
        ConsensusNodeList const& _left, ConsensusNodeList const& _right);

    ledger::Features features() const override;
    void setFeatures(ledger::Features features) override;

    void setSinglePointConsensus(bool singlePointConsensus)
    {
        m_singlePointConsensus = singlePointConsensus;
    }
    bool singlePointConsensus() const { return m_singlePointConsensus; }

protected:
    static bool isNodeExist(ConsensusNode const& _node, ConsensusNodeList const& _nodeList);

    bcos::crypto::KeyPairInterface::Ptr m_keyPair;
    std::atomic<IndexType> m_nodeIndex = 0;
    std::atomic<IndexType> m_consensusNodeNum = {0};

    ConsensusNodeList m_consensusNodeList;
    mutable bcos::SharedMutex x_consensusNodeList;
    ConsensusNodeList m_observerNodeList;
    mutable bcos::SharedMutex x_observerNodeList;

    std::atomic_bool m_consensusNodeListUpdated = {false};
    std::atomic_bool m_observerNodeListUpdated = {false};
    std::atomic_bool m_syncingState = {false};
    std::atomic_bool m_asMasterNode = {false};
    uint32_t m_compatibilityVersion = (uint32_t)(bcos::protocol::DEFAULT_VERSION);

    // default timeout is 3000ms
    std::atomic<uint64_t> m_consensusTimeout = {3000};
    constexpr const static uint64_t s_consensusTimeout = 3000;

    // default blockTxCountLimit is 1000
    std::atomic<uint64_t> m_blockTxCountLimit = {1000};
    ProposalInterface::Ptr m_committedProposal;
    mutable bcos::SharedMutex x_committedProposal;

    std::atomic<bcos::protocol::BlockNumber> m_progressedIndex = {0};
    bcos::protocol::BlockNumber m_syncingHighestNumber = {0};
    std::function<void(uint32_t _version)> m_versionNotification;
    ledger::Features m_features;
    bool m_singlePointConsensus = false;
};
}  // namespace bcos::consensus