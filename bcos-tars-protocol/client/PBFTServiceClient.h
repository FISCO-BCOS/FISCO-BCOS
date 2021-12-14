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
 * @brief client for the PBFTService
 * @file PBFTServiceClient.h
 * @author: yujiechen
 * @date 2021-06-29
 */

#pragma once

#include "bcos-framework/interfaces/sealer/SealerInterface.h"
#include "bcos-tars-protocol/ErrorConverter.h"
#include "bcos-tars-protocol/tars/PBFTService.h"
#include <bcos-framework/interfaces/consensus/ConsensusInterface.h>
#include <bcos-framework/interfaces/sync/BlockSyncInterface.h>

namespace bcostars
{
class PBFTServiceCommonCallback : public bcostars::PBFTServicePrxCallback
{
public:
    PBFTServiceCommonCallback(std::function<void(bcos::Error::Ptr)> _callback)
      : PBFTServicePrxCallback(), m_callback(_callback)
    {}
    ~PBFTServiceCommonCallback() override {}

    void callback_asyncNoteUnSealedTxsSize(const bcostars::Error& ret) override
    {
        m_callback(toBcosError(ret));
    }
    void callback_asyncNoteUnSealedTxsSize_exception(tars::Int32 ret) override
    {
        m_callback(toBcosError(ret));
    }
    void callback_asyncNotifyConsensusMessage(const bcostars::Error& ret) override
    {
        m_callback(toBcosError(ret));
    }
    void callback_asyncNotifyConsensusMessage_exception(tars::Int32 ret) override
    {
        m_callback(toBcosError(ret));
    }
    void callback_asyncNotifyNewBlock(const bcostars::Error& ret) override
    {
        m_callback(toBcosError(ret));
    }

    void callback_asyncNotifyNewBlock_exception(tars::Int32 ret) override
    {
        m_callback(toBcosError(ret));
    }
    void callback_asyncSubmitProposal(const bcostars::Error& ret) override
    {
        m_callback(toBcosError(ret));
    }
    void callback_asyncSubmitProposal_exception(tars::Int32 ret) override
    {
        m_callback(toBcosError(ret));
    }

    void callback_asyncNotifyBlockSyncMessage(const bcostars::Error& ret) override
    {
        m_callback(toBcosError(ret));
    }

    void callback_asyncNotifyBlockSyncMessage_exception(tars::Int32 ret) override
    {
        m_callback(toBcosError(ret));
    }

private:
    std::function<void(bcos::Error::Ptr)> m_callback;
};

class PBFTServiceClient : virtual public bcos::consensus::ConsensusInterface,
                          virtual public bcos::sealer::SealerInterface
{
public:
    using Ptr = std::shared_ptr<PBFTServiceClient>;
    PBFTServiceClient(bcostars::PBFTServicePrx _proxy) : m_proxy(_proxy) {}
    ~PBFTServiceClient() override {}

    void start() override {}
    void stop() override {}
    // called by frontService to dispatch message
    void asyncNotifyConsensusMessage(bcos::Error::Ptr _error, std::string const& _uuid,
        bcos::crypto::NodeIDPtr _nodeID, bcos::bytesConstRef _data,
        std::function<void(bcos::Error::Ptr _error)> _onRecv) override;
    void asyncGetPBFTView(
        std::function<void(bcos::Error::Ptr, bcos::consensus::ViewType)> _onGetView) override;

    // the txpool notify the unsealed txsSize to the sealer module
    void asyncNoteUnSealedTxsSize(
        size_t _unsealedTxsSize, std::function<void(bcos::Error::Ptr)> _onRecvResponse) override;

    // the sealer submit proposal to the consensus module
    // Note: if the sealer module integrates with the PBFT module, no need to implement this
    // interface
    void asyncSubmitProposal(bool _containSysTxs, bcos::bytesConstRef _proposalData,
        bcos::protocol::BlockNumber _proposalIndex, bcos::crypto::HashType const& _proposalHash,
        std::function<void(bcos::Error::Ptr)> _onProposalSubmitted) override;

    // the sync module calls this interface to check block
    // Note: if the sync module integrates with the PBFT module, no need to implement this interface
    void asyncCheckBlock(bcos::protocol::Block::Ptr _block,
        std::function<void(bcos::Error::Ptr, bool)> _onVerifyFinish) override;

    // the sync module calls this interface to notify new block
    // Note: if the sync module integrates with the PBFT module, no need to implement this interface
    void asyncNotifyNewBlock(bcos::ledger::LedgerConfig::Ptr _ledgerConfig,
        std::function<void(bcos::Error::Ptr)> _onRecv) override;

    // for the sync module to notify the syncing number
    // Note: since the sync module is integrated with the PBFT module, no need to implement the
    // client interface
    void notifyHighestSyncingNumber(bcos::protocol::BlockNumber _number) override
    {
        throw std::runtime_error("notifyHighestSyncingNumber: unimplemented interface!");
    }
    void asyncNotifySealProposal(
        size_t, size_t, size_t, std::function<void(bcos::Error::Ptr)>) override
    {
        throw std::runtime_error("asyncNotifySealProposal: unimplemented interface!");
    }
    // for the consensus module to notify the latest blockNumber to the sealer module
    // Note: since the sealer module is integrated with the PBFT module, no need to implement the
    // client interface
    void asyncNoteLatestBlockNumber(int64_t) override
    {
        throw std::runtime_error("asyncNoteLatestBlockNumber: unimplemented interface!");
    }

    // the consensus module notify the sealer to reset sealing when viewchange
    void asyncResetSealing(std::function<void(bcos::Error::Ptr)> _onRecvResponse) override
    {
        throw std::runtime_error("asyncResetSealing: unimplemented interface!");
    }

    void asyncGetConsensusStatus(
        std::function<void(bcos::Error::Ptr, std::string)> _onGetConsensusStatus) override;

    void notifyConnectedNodes(bcos::crypto::NodeIDSet const& _connectedNodes,
        std::function<void(bcos::Error::Ptr)> _onResponse) override;

protected:
    bcostars::PBFTServicePrx m_proxy;
};

class BlockSyncServiceClient : virtual public bcos::sync::BlockSyncInterface,
                               public PBFTServiceClient
{
public:
    using Ptr = std::shared_ptr<BlockSyncServiceClient>;
    BlockSyncServiceClient(bcostars::PBFTServicePrx _proxy) : PBFTServiceClient(_proxy) {}
    ~BlockSyncServiceClient() override {}

    // called by the consensus module when commit a new block
    void asyncNotifyNewBlock(
        bcos::ledger::LedgerConfig::Ptr, std::function<void(bcos::Error::Ptr)>) override
    {}

    // called by the consensus module to notify the consensusing block number
    void asyncNotifyCommittedIndex(
        bcos::protocol::BlockNumber, std::function<void(bcos::Error::Ptr _error)>) override
    {}

    // called by the RPC to get the sync status
    void asyncGetSyncInfo(
        std::function<void(bcos::Error::Ptr, std::string)> _onGetSyncInfo) override;

    // called by the frontService to dispatch message
    void asyncNotifyBlockSyncMessage(bcos::Error::Ptr _error, std::string const& _uuid,
        bcos::crypto::NodeIDPtr _nodeID, bcos::bytesConstRef _data,
        std::function<void(bcos::Error::Ptr _error)> _onRecv) override
    {
        auto nodeIDData = _nodeID->data();
        m_proxy->async_asyncNotifyBlockSyncMessage(new PBFTServiceCommonCallback(_onRecv), _uuid,
            std::vector<char>(nodeIDData.begin(), nodeIDData.end()),
            std::vector<char>(_data.begin(), _data.end()));
    }

    void notifyConnectedNodes(bcos::crypto::NodeIDSet const& _connectedNodes,
        std::function<void(bcos::Error::Ptr)> _onRecvResponse);

protected:
    void start() override {}
    void stop() override {}
};
}  // namespace bcostars