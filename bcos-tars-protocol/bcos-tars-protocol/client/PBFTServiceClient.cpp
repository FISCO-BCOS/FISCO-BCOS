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
 * @file PBFTServiceClient.cpp
 * @author: yujiechen
 * @date 2021-06-29
 */

#include "PBFTServiceClient.h"
#include "bcos-tars-protocol/Common.h"
#include "bcos-tars-protocol/protocol/BlockImpl.h"
using namespace bcostars;

class PBFTServiceCommonCallback : public bcostars::PBFTServicePrxCallback
{
public:
    PBFTServiceCommonCallback(std::function<void(bcos::Error::Ptr)> _callback)
      : PBFTServicePrxCallback(), m_callback(_callback)
    {}
    ~PBFTServiceCommonCallback() override {}

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

void PBFTServiceClient::asyncSubmitProposal(bool _containSysTxs,
    const bcos::protocol::Block& proposal, bcos::protocol::BlockNumber _proposalIndex,
    bcos::crypto::HashType const& _proposalHash,
    std::function<void(bcos::Error::Ptr)> _onProposalSubmitted)
{
    const auto& tarsBlock = dynamic_cast<const bcostars::protocol::BlockImpl&>(proposal);
    m_proxy->async_asyncSubmitProposal(new PBFTServiceCommonCallback(_onProposalSubmitted),
        _containSysTxs, tarsBlock.inner(), _proposalIndex,
        std::vector<char>(_proposalHash.begin(), _proposalHash.end()));
}

void PBFTServiceClient::asyncGetPBFTView(
    std::function<void(bcos::Error::Ptr, bcos::consensus::ViewType)> _onGetView)
{
    class Callback : public PBFTServicePrxCallback
    {
    public:
        explicit Callback(
            std::function<void(bcos::Error::Ptr, bcos::consensus::ViewType)> _callback)
          : PBFTServicePrxCallback(), m_callback(_callback)
        {}
        ~Callback() override {}

        void callback_asyncGetPBFTView(const bcostars::Error& ret, tars::Int64 _view) override
        {
            m_callback(toBcosError(ret), _view);
        }
        void callback_asyncGetPBFTView_exception(tars::Int32 ret) override
        {
            m_callback(toBcosError(ret), 0);
        }

    private:
        std::function<void(bcos::Error::Ptr, bcos::consensus::ViewType)> m_callback;
    };
    m_proxy->async_asyncGetPBFTView(new Callback(_onGetView));
}

// the sync module calls this interface to check block
void PBFTServiceClient::asyncCheckBlock(
    bcos::protocol::Block::Ptr _block, std::function<void(bcos::Error::Ptr, bool)> _onVerifyFinish)
{
    class Callback : public PBFTServicePrxCallback
    {
    public:
        explicit Callback(std::function<void(bcos::Error::Ptr, bool)> _callback)
          : PBFTServicePrxCallback(), m_callback(_callback)
        {}
        ~Callback() override {}

        void callback_asyncCheckBlock(const bcostars::Error& ret, tars::Bool _verifyResult) override
        {
            m_callback(toBcosError(ret), _verifyResult);
        }
        void callback_asyncCheckBlock_exception(tars::Int32 ret) override
        {
            m_callback(toBcosError(ret), false);
        }

    private:
        std::function<void(bcos::Error::Ptr, bool)> m_callback;
    };

    auto blockImpl = std::dynamic_pointer_cast<bcostars::protocol::BlockImpl>(_block);
    m_proxy->async_asyncCheckBlock(new Callback(_onVerifyFinish), blockImpl->inner());
}

// the sync module calls this interface to notify new block
void PBFTServiceClient::asyncNotifyNewBlock(
    bcos::ledger::LedgerConfig::Ptr _ledgerConfig, std::function<void(bcos::Error::Ptr)> _onRecv)
{
    m_proxy->async_asyncNotifyNewBlock(
        new PBFTServiceCommonCallback(_onRecv), toTarsLedgerConfig(_ledgerConfig));
}

// called by frontService to dispatch message
void PBFTServiceClient::asyncNotifyConsensusMessage(bcos::Error::Ptr, std::string const& _uuid,
    bcos::crypto::NodeIDPtr _nodeID, bcos::bytesConstRef _data,
    std::function<void(bcos::Error::Ptr _error)> _onRecv)
{
    auto nodeIDData = _nodeID->data();
    m_proxy->async_asyncNotifyConsensusMessage(new PBFTServiceCommonCallback(_onRecv), _uuid,
        std::vector<char>(nodeIDData.begin(), nodeIDData.end()),
        std::vector<char>(_data.begin(), _data.end()));
}

void BlockSyncServiceClient::asyncGetSyncInfo(
    std::function<void(bcos::Error::Ptr, std::string)> _onGetSyncInfo)
{
    class Callback : public PBFTServicePrxCallback
    {
    public:
        explicit Callback(std::function<void(bcos::Error::Ptr, std::string)> _callback)
          : PBFTServicePrxCallback(), m_callback(_callback)
        {}
        ~Callback() override {}

        void callback_asyncGetSyncInfo(
            const bcostars::Error& ret, const std::string& _syncInfo) override
        {
            m_callback(toBcosError(ret), _syncInfo);
        }
        void callback_asyncGetSyncInfo_exception(tars::Int32 ret) override
        {
            m_callback(toBcosError(ret), "callback_asyncGetSyncInfo_exception");
        }

    private:
        std::function<void(bcos::Error::Ptr, std::string)> m_callback;
    };
    m_proxy->async_asyncGetSyncInfo(new Callback(_onGetSyncInfo));
}

std::vector<bcos::sync::PeerStatus::Ptr> BlockSyncServiceClient::getPeerStatus()
{
    return {};
}

void BlockSyncServiceClient::notifyConnectedNodes(bcos::crypto::NodeIDSet const& _connectedNodes,
    std::function<void(bcos::Error::Ptr)> _onRecvResponse)
{
    PBFTServiceClient::notifyConnectedNodes(_connectedNodes, _onRecvResponse);
}

void PBFTServiceClient::notifyConnectedNodes(bcos::crypto::NodeIDSet const& _connectedNodes,
    std::function<void(bcos::Error::Ptr)> _onResponse)
{
    class Callback : public bcostars::PBFTServicePrxCallback
    {
    public:
        Callback(std::function<void(bcos::Error::Ptr _error)> callback) : m_callback(callback) {}

        void callback_asyncNotifyConnectedNodes(const bcostars::Error& ret) override
        {
            m_callback(toBcosError(ret));
        }

        void callback_asyncNotifyConnectedNodes_exception(tars::Int32 ret) override
        {
            m_callback(toBcosError(ret));
        }

    private:
        std::function<void(bcos::Error::Ptr _error)> m_callback;
    };

    std::vector<vector<tars::Char>> tarsConnectedNodes;
    for (auto const& it : _connectedNodes)
    {
        auto nodeID = it->data();
        tarsConnectedNodes.emplace_back(nodeID.begin(), nodeID.end());
    }
    m_proxy->async_asyncNotifyConnectedNodes(new Callback(_onResponse), tarsConnectedNodes);
}

void PBFTServiceClient::asyncGetConsensusStatus(
    std::function<void(bcos::Error::Ptr, std::string)> _onGetConsensusStatus)
{
    class Callback : public PBFTServicePrxCallback
    {
    public:
        explicit Callback(std::function<void(bcos::Error::Ptr, std::string)> _callback)
          : PBFTServicePrxCallback(), m_callback(_callback)
        {}
        ~Callback() override {}

        void callback_asyncGetConsensusStatus(
            const bcostars::Error& ret, const std::string& _consensusStatus) override
        {
            m_callback(toBcosError(ret), _consensusStatus);
        }
        void callback_asyncGetConsensusStatus_exception(tars::Int32 ret) override
        {
            m_callback(toBcosError(ret), "callback_asyncGetConsensusStatus_exception");
        }

    private:
        std::function<void(bcos::Error::Ptr, std::string)> m_callback;
    };
    m_proxy->async_asyncGetConsensusStatus(new Callback(_onGetConsensusStatus));
}
bcostars::PBFTServiceClient::PBFTServiceClient(bcostars::PBFTServicePrx _proxy) : m_proxy(_proxy) {}
bcostars::PBFTServiceClient::~PBFTServiceClient() {}
void bcostars::PBFTServiceClient::notifyHighestSyncingNumber(bcos::protocol::BlockNumber _number)
{
    throw std::runtime_error(
        "PBFTServiceClient: notifyHighestSyncingNumber: unimplemented interface!");
}
void bcostars::PBFTServiceClient::asyncNotifySealProposal(
    uint64_t, uint64_t, uint64_t, std::function<void(bcos::Error::Ptr)>)
{
    throw std::runtime_error(
        "PBFTServiceClient: asyncNotifySealProposal: unimplemented interface!");
}
void bcostars::PBFTServiceClient::asyncNoteLatestBlockNumber(int64_t)
{
    throw std::runtime_error(
        "PBFTServiceClient: asyncNoteLatestBlockNumber: unimplemented interface!");
}
void bcostars::PBFTServiceClient::asyncNoteLatestBlockHash(bcos::crypto::HashType)
{
    throw std::runtime_error(
        "PBFTServiceClient: asyncNoteLatestBlockHash: unimplemented interface!");
}

void bcostars::PBFTServiceClient::asyncNoteLatestBlockTimestamp(int64_t _timestamp)
{
    throw std::runtime_error(
        "PBFTServiceClient: asyncNoteLatestBlockHash: unimplemented interface!");
}

void bcostars::PBFTServiceClient::asyncResetSealing(
    std::function<void(bcos::Error::Ptr)> _onRecvResponse)
{
    throw std::runtime_error("PBFTServiceClient: asyncResetSealing: unimplemented interface!");
}
uint16_t bcostars::PBFTServiceClient::hookWhenSealBlock(bcos::protocol::Block::Ptr)
{
    throw std::runtime_error("PBFTServiceClient: hookWhenSealBlock: unimplemented interface!");
}
void bcostars::BlockSyncServiceClient::asyncNotifyNewBlock(
    bcos::ledger::LedgerConfig::Ptr, std::function<void(bcos::Error::Ptr)>)
{
    throw std::runtime_error(
        "BlockSyncServiceClient: asyncNotifyNewBlock: unimplemented interface!");
}
void bcostars::BlockSyncServiceClient::asyncNotifyCommittedIndex(
    bcos::protocol::BlockNumber, std::function<void(bcos::Error::Ptr _error)>)
{
    throw std::runtime_error(
        "BlockSyncServiceClient: asyncNotifyCommittedIndex: unimplemented interface!");
}
bool bcostars::BlockSyncServiceClient::faultyNode(bcos::crypto::NodeIDPtr _nodeID)
{
    throw std::runtime_error("BlockSyncServiceClient: faultyNode: unimplemented interface!");
}
void bcostars::BlockSyncServiceClient::asyncNotifyBlockSyncMessage(bcos::Error::Ptr _error,
    std::string const& _uuid, bcos::crypto::NodeIDPtr _nodeID, bcos::bytesConstRef _data,
    std::function<void(bcos::Error::Ptr _error)> _onRecv)
{
    auto nodeIDData = _nodeID->data();
    m_proxy->async_asyncNotifyBlockSyncMessage(new PBFTServiceCommonCallback(_onRecv), _uuid,
        std::vector<char>(nodeIDData.begin(), nodeIDData.end()),
        std::vector<char>(_data.begin(), _data.end()));
}
void bcostars::BlockSyncServiceClient::start() {}
void bcostars::BlockSyncServiceClient::stop() {}
