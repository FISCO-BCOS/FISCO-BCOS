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
 * @file PBFTImpl.cpp
 * @author: yujiechen
 * @date 2021-05-17
 */
#include "PBFTImpl.h"
#include <json/json.h>
using namespace bcos;
using namespace bcos::consensus;

void PBFTImpl::start()
{
    if (m_running)
    {
        PBFT_LOG(WARNING) << LOG_DESC("The PBFT module has already been started!");
        return;
    }
    m_running = true;
    m_pbftEngine->start();
    recoverState();
    PBFT_LOG(INFO) << LOG_DESC("Start the PBFT module.");
}

void PBFTImpl::recoverState()
{
    // Note: only replay the PBFT state when all-modules ready
    PBFT_LOG(INFO) << LOG_DESC("fetch PBFT state");
    auto config = m_pbftEngine->pbftConfig();
    auto stateProposals = config->storage()->loadState(config->committedProposal()->index());
    if (stateProposals && stateProposals->size() > 0)
    {
        m_pbftEngine->initState(*stateProposals, config->keyPair()->publicKey());
        auto lowWaterMarkIndex = stateProposals->size() - 1;
        auto lowWaterMark = ((*stateProposals)[lowWaterMarkIndex])->index();
        config->setLowWaterMark(lowWaterMark + 1);
        PBFT_LOG(INFO) << LOG_DESC("init PBFT state")
                       << LOG_KV("stateProposals", stateProposals->size())
                       << LOG_KV("lowWaterMark", lowWaterMark)
                       << LOG_KV("highWaterMark", config->highWaterMark());
    }
    config->timer()->start();
}

void PBFTImpl::stop()
{
    if (!m_running)
    {
        PBFT_LOG(WARNING) << LOG_DESC("The PBFT module has already been stopped!");
        return;
    }
    m_blockValidator->stop();
    m_pbftEngine->stop();
    m_running = false;
    PBFT_LOG(INFO) << LOG_DESC("Stop the PBFT module.");
}

void PBFTImpl::asyncSubmitProposal(bool _containSysTxs, bytesConstRef _proposalData,
    bcos::protocol::BlockNumber _proposalIndex, bcos::crypto::HashType const& _proposalHash,
    std::function<void(Error::Ptr)> _onProposalSubmitted)
{
    return m_pbftEngine->asyncSubmitProposal(
        _containSysTxs, _proposalData, _proposalIndex, _proposalHash, _onProposalSubmitted);
}

void PBFTImpl::asyncGetPBFTView(std::function<void(Error::Ptr, ViewType)> _onGetView)
{
    auto view = m_pbftEngine->pbftConfig()->view();
    if (!_onGetView)
    {
        return;
    }
    _onGetView(nullptr, view);
}

void PBFTImpl::asyncNotifyConsensusMessage(bcos::Error::Ptr _error, std::string const& _id,
    bcos::crypto::NodeIDPtr _nodeID, bytesConstRef _data,
    std::function<void(Error::Ptr _error)> _onRecv)
{
    m_pbftEngine->onReceivePBFTMessage(_error, _id, _nodeID, _data);
    if (!_onRecv)
    {
        return;
    }
    _onRecv(nullptr);
}

// the sync module calls this interface to check block
void PBFTImpl::asyncCheckBlock(
    bcos::protocol::Block::Ptr _block, std::function<void(Error::Ptr, bool)> _onVerifyFinish)
{
    m_blockValidator->asyncCheckBlock(_block, _onVerifyFinish);
}

// the sync module calls this interface to notify new block
void PBFTImpl::asyncNotifyNewBlock(
    bcos::ledger::LedgerConfig::Ptr _ledgerConfig, std::function<void(Error::Ptr)> _onRecv)
{
    m_pbftEngine->asyncNotifyNewBlock(_ledgerConfig, _onRecv);
}

void PBFTImpl::notifyHighestSyncingNumber(bcos::protocol::BlockNumber _blockNumber)
{
    m_pbftEngine->pbftConfig()->setSyncingHighestNumber(_blockNumber);
}

void PBFTImpl::asyncNoteUnSealedTxsSize(
    uint64_t _unsealedTxsSize, std::function<void(Error::Ptr)> _onRecvResponse)
{
    m_pbftEngine->pbftConfig()->setUnSealedTxsSize(_unsealedTxsSize);
    if (_onRecvResponse)
    {
        _onRecvResponse(nullptr);
    }
}

void PBFTImpl::init()
{
    auto config = m_pbftEngine->pbftConfig();
    config->validator()->init();
    PBFT_LOG(INFO) << LOG_DESC("fetch LedgerConfig information");

    m_ledgerFetcher->fetchBlockNumberAndHash();
    m_ledgerFetcher->fetchConsensusNodeList();
    // Note: must fetchObserverNode here to notify the latest sealerList and observerList to txpool
    m_ledgerFetcher->fetchObserverNodeList();
    m_ledgerFetcher->fetchBlockTxCountLimit();
    m_ledgerFetcher->fetchConsensusLeaderPeriod();
    m_ledgerFetcher->fetchCompatibilityVersion();
    auto ledgerConfig = m_ledgerFetcher->ledgerConfig();
    PBFT_LOG(INFO) << LOG_DESC("fetch LedgerConfig information success")
                   << LOG_KV("blockNumber", ledgerConfig->blockNumber())
                   << LOG_KV("hash", ledgerConfig->hash().abridged())
                   << LOG_KV("maxTxsPerBlock", ledgerConfig->blockTxCountLimit())
                   << LOG_KV("consensusNodeList", ledgerConfig->consensusNodeList().size());
    config->resetConfig(ledgerConfig);
    if (!m_masterNode)
    {
        return;
    }
    PBFT_LOG(INFO) << LOG_DESC("init PBFT success");
}

void PBFTImpl::asyncGetConsensusStatus(
    std::function<void(Error::Ptr, std::string)> _onGetConsensusStatus)
{
    auto config = m_pbftEngine->pbftConfig();
    Json::Value consensusStatus;
    consensusStatus["nodeID"] = *toHexString(config->nodeID()->data());
    consensusStatus["index"] = config->nodeIndex();
    consensusStatus["leaderIndex"] = config->getLeader();
    consensusStatus["consensusNodesNum"] = config->consensusNodesNum();
    consensusStatus["maxFaultyQuorum"] = config->maxFaultyQuorum();
    consensusStatus["minRequiredQuorum"] = config->minRequiredQuorum();
    consensusStatus["isConsensusNode"] = config->isConsensusNode();
    consensusStatus["blockNumber"] = config->committedProposal()->index();
    consensusStatus["hash"] = *toHexString(config->committedProposal()->hash());
    consensusStatus["timeout"] = config->timeout();
    consensusStatus["changeCycle"] = config->timer()->changeCycle();
    consensusStatus["view"] = config->view();
    consensusStatus["connectedNodeList"] = (int64_t)((config->connectedNodeList()).size());

    // print the nodeIndex of all other nodes
    auto nodeList = config->consensusNodeList();
    Json::Value consensusNodeInfo(Json::arrayValue);
    size_t i = 0;
    for (auto const& node : nodeList)
    {
        Json::Value info;
        info["nodeID"] = *toHexString(node->nodeID()->data());
        info["weight"] = node->weight();
        info["index"] = (int64_t)(i);
        consensusNodeInfo.append(info);
        i++;
    }
    consensusStatus["consensusNodeList"] = consensusNodeInfo;
    Json::FastWriter fastWriter;
    std::string statusStr = fastWriter.write(consensusStatus);
    _onGetConsensusStatus(nullptr, statusStr);
}

void PBFTImpl::enableAsMaterNode(bool _isMasterNode)
{
    if (m_masterNode == _isMasterNode)
    {
        PBFT_LOG(INFO) << LOG_DESC("enableAsMaterNode: The masterNodeState is not changed")
                       << LOG_KV("master", _isMasterNode);
        return;
    }
    if (!m_masterNode)
    {
        PBFT_LOG(INFO) << LOG_DESC(
            "enableAsMaterNode: clearAllCache for the node switch into backup node");
        m_pbftEngine->clearAllCache();
    }
    PBFT_LOG(INFO) << LOG_DESC("enableAsMaterNode: ") << _isMasterNode;
    m_masterNode.store(_isMasterNode);
    m_pbftEngine->pbftConfig()->enableAsMaterNode(_isMasterNode);
    if (!_isMasterNode)
    {
        return;
    }
    PBFT_LOG(INFO) << LOG_DESC("enableAsMaterNode: init and start the consensus module");
    init();
    recoverState();
    m_pbftEngine->restart();
}