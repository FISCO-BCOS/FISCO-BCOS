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
#include <boost/exception/diagnostic_information.hpp>
using namespace bcos;
using namespace bcos::consensus;

void PBFTImpl::start()
{
    if (m_running)
    {
        PBFT_LOG(INFO) << LOG_DESC("The PBFT module has already been started!");
        return;
    }
    m_running = true;
    m_pbftEngine->start();
    PBFT_LOG(INFO) << LOG_DESC("Start the PBFT module.");
}

void PBFTImpl::stop()
{
    if (!m_running)
    {
        PBFT_LOG(INFO) << LOG_DESC("The PBFT module has already been stopped!");
        return;
    }
    m_blockValidator->stop();
    m_pbftEngine->stop();
    m_running = false;
    PBFT_LOG(INFO) << LOG_DESC("Stop the PBFT module.");
}

void PBFTImpl::asyncSubmitProposal(bool _containSysTxs, const protocol::Block& proposal,
    bcos::protocol::BlockNumber _proposalIndex, bcos::crypto::HashType const& _proposalHash,
    std::function<void(Error::Ptr)> _onProposalSubmitted)
{
    return m_pbftEngine->asyncSubmitProposal(
        _containSysTxs, proposal, _proposalIndex, _proposalHash, _onProposalSubmitted);
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

void PBFTImpl::init()
{
    auto config = m_pbftEngine->pbftConfig();
    config->validator()->init();
    m_pbftEngine->fetchAndUpdateLedgerConfig();
    PBFT_LOG(INFO) << LOG_DESC("init PBFT success");
}

void PBFTImpl::asyncGetConsensusStatus(
    std::function<void(Error::Ptr, std::string)> _onGetConsensusStatus)
{
    auto config = m_pbftEngine->pbftConfig();
    Json::Value consensusStatus;
    consensusStatus["nodeID"] = toHex(config->nodeID()->data());
    consensusStatus["index"] = (Json::UInt64)config->nodeIndex();
    consensusStatus["leaderIndex"] = (Json::UInt64)config->getLeader();
    consensusStatus["consensusNodesNum"] = (Json::UInt64)config->consensusNodesNum();
    consensusStatus["maxFaultyQuorum"] = (Json::UInt64)config->maxFaultyQuorum();
    consensusStatus["minRequiredQuorum"] = (Json::UInt64)config->minRequiredQuorum();
    consensusStatus["isConsensusNode"] = config->isConsensusNode();
    consensusStatus["blockNumber"] = (Json::UInt64)config->committedProposal()->index();
    consensusStatus["hash"] = toHex(config->committedProposal()->hash());
    if (config->isConsensusNode())
    {
        consensusStatus["timeout"] = config->timeout();
    }
    else
    {
        consensusStatus["timeout"] = false;
    }
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
        info["nodeID"] = toHex(node.nodeID->data());
        info["weight"] = (Json::UInt64)node.voteWeight;
        info["termWeight"] = (Json::UInt64)node.termWeight;
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
    PBFT_LOG(INFO) << LOG_DESC("enableAsMasterNode: ") << _isMasterNode;
    if (!_isMasterNode)
    {
        // Demotion path: master -> backup
        // FIB-137: drain m_msgQueue in addition to clearAllCache(); both are needed to
        // discard pre-demotion PBFT traffic so it is not replayed after a later promote.
        PBFT_LOG(INFO) << LOG_DESC(
            "enableAsMasterNode: clearAllCache for the node switch into backup node");
        m_pbftEngine->clearAllCache();
        m_pbftEngine->clearMsgQueue();
        m_pbftEngine->pbftConfig()->enableAsMasterNode(false);
        m_masterNode.store(false);
        return;
    }
    // Promotion path: backup -> master
    // FIB-137: discard messages accumulated during the demoted period before resuming
    // processing (they were observed under the previous role/term and must not be
    // replayed into the freshly-recovered state).
    m_pbftEngine->clearMsgQueue();
    PBFT_LOG(INFO) << LOG_DESC("enableAsMasterNode: init and start the consensus module");
    // FIB-138: do NOT flip pbftConfig->m_asMasterNode (read by isConsensusNode and used
    // to gate consensus traffic on PBFTEngine.cpp:211/347/508/569/1141/1693/1756) until
    // init/recoverState/restart all succeed. Flipping early opens a window where
    // incoming PBFT messages are accepted while shared consensus state (ledger config,
    // recovered proposals, watermarks, caches, timers) is still being mutated. On any
    // failure both PBFTImpl::m_masterNode (PBFTImpl.h:169) and ConsensusConfig::
    // m_asMasterNode (ConsensusConfig.h:159) must roll back so the node does not
    // remain a half-initialized "master" still accepting traffic.
    try
    {
        init();
        m_pbftEngine->recoverState();
        m_pbftEngine->restart();
    }
    catch (std::exception const& e)
    {
        PBFT_LOG(ERROR) << LOG_DESC(
                               "enableAsMasterNode: promotion failed; rolling back master flags")
                        << LOG_KV("err", boost::diagnostic_information(e));
        // Both flags MUST stay/become false on failure.
        m_pbftEngine->pbftConfig()->enableAsMasterNode(false);
        m_masterNode.store(false);
        throw;
    }
    catch (...)
    {
        PBFT_LOG(ERROR) << LOG_DESC(
            "enableAsMasterNode: unknown failure, rolling back master flags");
        // Both flags MUST stay/become false on failure.
        m_pbftEngine->pbftConfig()->enableAsMasterNode(false);
        m_masterNode.store(false);
        throw;
    }
    // Init/recover/restart succeeded; now make the node visible as a master.
    m_pbftEngine->pbftConfig()->enableAsMasterNode(true);
    m_masterNode.store(true);
}

void bcos::consensus::PBFTImpl::setLedger(ledger::LedgerInterface::Ptr ledger)
{
    m_pbftEngine->setLedger(std::move(ledger));
}

void PBFTImpl::asyncNotifyTxsSize(
    uint64_t _txsSize, std::function<void(Error::Ptr)> _onRecvResponse)
{
    m_pbftEngine->pbftConfig()->setTxsSize(_txsSize);
    if (_onRecvResponse)
    {
        _onRecvResponse(nullptr);
    }
}
