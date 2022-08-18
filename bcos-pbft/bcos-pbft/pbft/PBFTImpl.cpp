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
    m_pbftEngine->recoverState();
    PBFT_LOG(INFO) << LOG_DESC("Start the PBFT module.");
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
    m_pbftEngine->fetchAndUpdatesLedgerConfig();
    PBFT_LOG(INFO) << LOG_DESC("init PBFT success");
}

void PBFTImpl::asyncGetConsensusStatus(
    std::function<void(Error::Ptr, std::string)> _onGetConsensusStatus)
{
    auto config = m_pbftEngine->pbftConfig();
    Json::Value consensusStatus;
    consensusStatus["nodeID"] = *toHexString(config->nodeID()->data());
    consensusStatus["index"] = (Json::UInt64)config->nodeIndex();
    consensusStatus["leaderIndex"] = (Json::UInt64)config->getLeader();
    consensusStatus["consensusNodesNum"] = (Json::UInt64)config->consensusNodesNum();
    consensusStatus["maxFaultyQuorum"] = (Json::UInt64)config->maxFaultyQuorum();
    consensusStatus["minRequiredQuorum"] = (Json::UInt64)config->minRequiredQuorum();
    consensusStatus["isConsensusNode"] = config->isConsensusNode();
    consensusStatus["blockNumber"] = (Json::UInt64)config->committedProposal()->index();
    consensusStatus["hash"] = *toHexString(config->committedProposal()->hash());
    consensusStatus["timeout"] = config->timeout();
    consensusStatus["changeCycle"] = (Json::UInt64)config->timer()->changeCycle();
    consensusStatus["view"] = (Json::UInt64)config->view();
    consensusStatus["connectedNodeList"] = (Json::UInt64)((config->connectedNodeList()).size());

    // print the nodeIndex of all other nodes
    auto nodeList = config->consensusNodeList();
    Json::Value consensusNodeInfo(Json::arrayValue);
    size_t i = 0;
    for (auto const& node : nodeList)
    {
        Json::Value info;
        info["nodeID"] = *toHexString(node->nodeID()->data());
        info["weight"] = (Json::UInt64)node->weight();
        info["index"] = (Json::Int64)(i);
        consensusNodeInfo.append(info);
        i++;
    }
    consensusStatus["consensusNodeList"] = consensusNodeInfo;
    Json::FastWriter fastWriter;
    std::string statusStr = fastWriter.write(consensusStatus);
    _onGetConsensusStatus(nullptr, statusStr);
}

void PBFTImpl::enableAsMasterNode(bool _isMasterNode)
{
    if (m_masterNode == _isMasterNode)
    {
        PBFT_LOG(INFO) << LOG_DESC("enableAsMasterNode: The masterNodeState is not changed")
                       << LOG_KV("master", _isMasterNode);
        return;
    }
    if (!m_masterNode)
    {
        PBFT_LOG(INFO) << LOG_DESC(
            "enableAsMasterNode: clearAllCache for the node switch into backup node");
        m_pbftEngine->clearAllCache();
    }
    PBFT_LOG(INFO) << LOG_DESC("enableAsMasterNode: ") << _isMasterNode;
    m_masterNode.store(_isMasterNode);
    m_pbftEngine->pbftConfig()->enableAsMasterNode(_isMasterNode);
    if (!_isMasterNode)
    {
        return;
    }
    PBFT_LOG(INFO) << LOG_DESC("enableAsMasterNode: init and start the consensus module");
    init();
    m_pbftEngine->recoverState();
    m_pbftEngine->restart();
}