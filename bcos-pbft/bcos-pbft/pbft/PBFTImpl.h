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
 * @brief implementation for ConsensusInterface
 * @file PBFTImpl.h
 * @author: yujiechen
 * @date 2021-05-17
 */
#pragma once
#include "engine/BlockValidator.h"
#include "engine/PBFTEngine.h"
#include <bcos-framework/interfaces/consensus/ConsensusInterface.h>
#include <bcos-tool/LedgerConfigFetcher.h>
namespace bcos
{
namespace consensus
{
class PBFTImpl : public ConsensusInterface
{
public:
    using Ptr = std::shared_ptr<PBFTImpl>;
    explicit PBFTImpl(PBFTEngine::Ptr _pbftEngine) : m_pbftEngine(_pbftEngine)
    {
        m_blockValidator = std::make_shared<BlockValidator>(m_pbftEngine->pbftConfig());
    }
    virtual ~PBFTImpl() { stop(); }

    void start() override;
    void stop() override;

    void asyncSubmitProposal(bool _containSysTxs, bytesConstRef _proposalData,
        bcos::protocol::BlockNumber _proposalIndex, bcos::crypto::HashType const& _proposalHash,
        std::function<void(Error::Ptr)> _onProposalSubmitted) override;

    void asyncGetPBFTView(std::function<void(Error::Ptr, ViewType)> _onGetView) override;

    void asyncNotifyConsensusMessage(bcos::Error::Ptr _error, std::string const& _id,
        bcos::crypto::NodeIDPtr _nodeID, bytesConstRef _data,
        std::function<void(Error::Ptr _error)> _onRecv) override;

    // the sync module calls this interface to check block
    void asyncCheckBlock(bcos::protocol::Block::Ptr _block,
        std::function<void(Error::Ptr, bool)> _onVerifyFinish) override;

    // the sync module calls this interface to notify new block
    void asyncNotifyNewBlock(bcos::ledger::LedgerConfig::Ptr _ledgerConfig,
        std::function<void(Error::Ptr)> _onRecv) override;

    void notifyHighestSyncingNumber(bcos::protocol::BlockNumber _blockNumber) override;
    void asyncNoteUnSealedTxsSize(
        uint64_t _unsealedTxsSize, std::function<void(Error::Ptr)> _onRecvResponse) override;
    void setLedgerFetcher(bcos::tool::LedgerConfigFetcher::Ptr _ledgerFetcher)
    {
        m_ledgerFetcher = _ledgerFetcher;
    }
    PBFTEngine::Ptr pbftEngine() { return m_pbftEngine; }

    virtual void init();

    // notify the sealer seal Proposal
    void registerSealProposalNotifier(
        std::function<void(size_t, size_t, size_t, std::function<void(Error::Ptr)>)>
            _sealProposalNotifier)
    {
        m_pbftEngine->pbftConfig()->registerSealProposalNotifier(_sealProposalNotifier);
    }

    // notify the sealer the latest blockNumber
    void registerStateNotifier(std::function<void(bcos::protocol::BlockNumber)> _stateNotifier)
    {
        m_pbftEngine->pbftConfig()->registerStateNotifier(_stateNotifier);
    }
    // the sync module notify the consensus module the new block
    void registerNewBlockNotifier(
        std::function<void(bcos::ledger::LedgerConfig::Ptr, std::function<void(Error::Ptr)>)>
            _newBlockNotifier)
    {
        m_pbftEngine->pbftConfig()->registerNewBlockNotifier(_newBlockNotifier);
    }

    void registerFaultyDiscriminator(
        std::function<bool(bcos::crypto::NodeIDPtr)> _faultyDiscriminator)
    {
        m_pbftEngine->pbftConfig()->registerFaultyDiscriminator(_faultyDiscriminator);
    }

    // handler to notify the consensusing proposal index to the sync module
    void registerCommittedProposalNotifier(
        std::function<void(bcos::protocol::BlockNumber, std::function<void(Error::Ptr)>)>
            _committedProposalNotifier)
    {
        m_pbftEngine->registerCommittedProposalNotifier(_committedProposalNotifier);
    }

    // handler to notify the sealer reset the sealing proposals
    void registerSealerResetNotifier(
        std::function<void(std::function<void(Error::Ptr)>)> _sealerResetNotifier)
    {
        m_pbftEngine->pbftConfig()->registerSealerResetNotifier(_sealerResetNotifier);
    }

    ConsensusNodeList consensusNodeList() const override
    {
        return m_pbftEngine->pbftConfig()->consensusNodeList();
    }
    uint64_t nodeIndex() const override { return m_pbftEngine->pbftConfig()->nodeIndex(); }
    void asyncGetConsensusStatus(
        std::function<void(Error::Ptr, std::string)> _onGetConsensusStatus) override;

    void notifyConnectedNodes(bcos::crypto::NodeIDSet const& _connectedNodes,
        std::function<void(Error::Ptr)> _onResponse) override
    {
        m_pbftEngine->pbftConfig()->setConnectedNodeList(_connectedNodes);
        if (_onResponse)
        {
            _onResponse(nullptr);
        }
    }
    virtual void enableAsMaterNode(bool _isMasterNode);

protected:
    PBFTEngine::Ptr m_pbftEngine;
    BlockValidator::Ptr m_blockValidator;
    bcos::tool::LedgerConfigFetcher::Ptr m_ledgerFetcher;
    std::atomic_bool m_running = {false};
    std::atomic_bool m_masterNode = {false};
};
}  // namespace consensus
}  // namespace bcos