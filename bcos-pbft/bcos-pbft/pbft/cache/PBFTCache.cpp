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
 * @brief cache for the consensus state of the proposal
 * @file PBFTCache.cpp
 * @author: yujiechen
 * @date 2021-04-23
 */
#include "PBFTCache.h"

using namespace bcos;
using namespace bcos::consensus;
using namespace bcos::protocol;
using namespace bcos::crypto;

PBFTCache::PBFTCache(PBFTConfig::Ptr _config, BlockNumber _index)
  : m_config(_config), m_index(_index)
{
    // Timer is used to manage checkpoint timeout
    m_timer = std::make_shared<PBFTTimer>(m_config->checkPointTimeoutInterval());
}
void PBFTCache::init()
{
    // register timeout handler
    auto self = std::weak_ptr<PBFTCache>(shared_from_this());
    m_timer->registerTimeoutHandler([self]() {
        try
        {
            auto cache = self.lock();
            if (!cache)
            {
                return;
            }
            cache->onCheckPointTimeout();
        }
        catch (std::exception const& e)
        {
            PBFT_LOG(WARNING) << LOG_DESC("onCheckPointTimeout error")
                              << LOG_KV("errorInfo", boost::diagnostic_information(e));
        }
    });
}
void PBFTCache::onCheckPointTimeout()
{
    checkAndCommitStableCheckPoint();
    // Note: this logic is unreachable
    if (!m_checkpointProposal)
    {
        PBFT_LOG(WARNING) << LOG_DESC("onCheckPointTimeout but the checkpoint proposal is null")
                          << m_config->printCurrentState();
        return;
    }
    // try to discover non-deterministic
    if (triggerNonDeterministic())
    {
        // set the waterMarkLimit to 1 when discover deterministic proposal
        m_config->enforceSerial();
        m_config->timer()->restart();
        PBFT_LOG(INFO) << LOG_DESC("Discover non-deterministic proposal, into recovery process")
                       << printPBFTProposal(m_checkpointProposal) << m_config->printCurrentState();
        return;
    }
    if (m_committedIndexNotifier && m_config->timer()->running() == false)
    {
        m_committedIndexNotifier(m_config->committedProposal()->index());
    }
    PBFT_LOG(WARNING) << LOG_DESC("onCheckPointTimeout: resend the checkpoint message package")
                      << LOG_KV("index", m_checkpointProposal->index())
                      << LOG_KV("hash", m_checkpointProposal->hash().abridged())
                      << m_config->printCurrentState();
    auto checkPointMsg = m_config->pbftMessageFactory()->populateFrom(PacketType::CheckPoint,
        m_config->pbftMsgDefaultVersion(), m_config->view(), utcTime(), m_config->nodeIndex(),
        m_checkpointProposal, m_config->cryptoSuite(), m_config->keyPair(), true);
    auto encodedData = m_config->codec()->encode(checkPointMsg);
    // only broadcast message to consensus node
    m_config->frontService()->asyncSendBroadcastMessage(
        bcos::protocol::NodeType::CONSENSUS_NODE, ModuleID::PBFT, ref(*encodedData));
    m_timer->restart();
}

bool PBFTCache::triggerNonDeterministic()
{
    RecursiveGuard l(m_mutex);
    if (!m_checkpointProposal)
    {
        return false;
    }
    std::vector<bcos::crypto::PublicPtr> nodesWithInconsistentCheckPoint;
    uint64_t collectedCheckPointQurom = 0;
    for (auto const& cache : m_checkpointCacheList)
    {
        auto const& msgList = cache.second;
        for (auto const& msg : msgList)
        {
            if (msg.second->index() != m_index)
            {
                continue;
            }
            auto generatedFrom = msg.first;
            auto nodeInfo = m_config->getConsensusNodeByIndex(generatedFrom);
            if (!nodeInfo)
            {
                continue;
            }
            collectedCheckPointQurom += nodeInfo->weight();
            if (msg.second->hash() != m_checkpointProposal->hash() &&
                !m_nodesWithInconsistentCheckPoint.count(nodeInfo->nodeID()))
            {
                m_nodesWithInconsistentCheckPoint.insert(std::make_pair(nodeInfo->nodeID(), false));
                nodesWithInconsistentCheckPoint.emplace_back(nodeInfo->nodeID());
            }
        }
    }
    if (collectedCheckPointQurom < m_config->minRequiredQuorum())
    {
        m_nodesWithInconsistentCheckPoint.clear();
        return false;
    }
    // stall the pipeline
    m_config->setWaitSealUntil(m_index);
    m_config->setWaitResealUntil(m_index);
    m_config->setExpectedCheckPoint(m_index);
    // request proposalState to the nodes with inconsistent checkPoint
    auto txsStateRequest = m_config->pbftMessageFactory()->populateFrom(
        PacketType::StateRequest, m_index, m_checkpointProposal->hash());
    txsStateRequest->setGeneratedFrom(m_config->nodeIndex());
    auto encodedData = m_config->codec()->encode(txsStateRequest);
    auto self = std::weak_ptr<PBFTCache>(shared_from_this());
    for (auto const& nodeID : nodesWithInconsistentCheckPoint)
    {
        PBFT_LOG(INFO) << LOG_DESC("request state to ") << nodeID->shortHex()
                       << LOG_KV("index", txsStateRequest->index());
        m_config->frontService()->asyncSendMessageByNodeID(ModuleID::PBFT, nodeID,
            ref(*encodedData), 0,
            [self](Error::Ptr _error, NodeIDPtr _nodeID, bytesConstRef _data, const std::string&,
                std::function<void(bytesConstRef _respData)>) {
                auto cache = self.lock();
                if (!cache)
                {
                    return;
                }
                cache->onReceiveExecResult(_error, _nodeID, _data);
            });
    }
    return true;
}

bcos::protocol::Block::Ptr PBFTCache::undeterministicBlock()
{
    if (m_undeterministicBlock)
    {
        return m_undeterministicBlock;
    }
    // Note: sync interface here
    m_config->stateMachine()->getExecResult(
        m_index, [this](bcos::Error::Ptr&& _error, bcos::protocol::Block::Ptr&& _block) {
            if (_error)
            {
                PBFT_LOG(WARNING) << LOG_DESC("onReceiveExecResult: getExecResult error")
                                  << LOG_KV("code", _error->errorCode())
                                  << LOG_KV("msg", _error->errorMessage());
                return;
            }
            m_undeterministicBlock = std::move(_block);
        });
    return m_undeterministicBlock;
}
void PBFTCache::onReceiveExecResult(Error::Ptr _error, NodeIDPtr _nodeID, bytesConstRef _data)
{
    if (_error)
    {
        PBFT_LOG(WARNING) << LOG_DESC("onReceiveExecResult error")
                          << LOG_KV("code", _error->errorCode())
                          << LOG_KV("msg", _error->errorMessage())
                          << LOG_KV("from", _nodeID ? _nodeID->shortHex() : "unknown");
        return;
    }
    try
    {
        RecursiveGuard l(m_mutex);
        auto response =
            std::dynamic_pointer_cast<PBFTMessageInterface>(m_config->codec()->decode(_data));
        if (response->index() != m_index)
        {
            PBFT_LOG(WARNING) << LOG_DESC("onReceiveExecResult: receive invalid execResult")
                              << printPBFTMsgInfo(response);
            return;
        }
        if (!m_precommit)
        {
            return;
        }
        PBFT_LOG(INFO) << LOG_DESC("onReceiveExecResult") << printPBFTMsgInfo(response)
                       << LOG_KV("from", _nodeID->shortHex());
        auto execResult = m_config->stateMachine()->blockFactory()->createBlock(
            response->consensusProposal()->data());
        if (!m_localBlock)
        {
            m_localBlock = m_config->stateMachine()->blockFactory()->createBlock(
                m_precommit->consensusProposal()->data());
        }
        // Note: sync implementation here for getExecResult
        auto localExecResult = undeterministicBlock();
        if (!localExecResult)
        {
            return;
        }
        if (localExecResult->receiptsSize() != execResult->receiptsSize())
        {
            PBFT_LOG(WARNING) << LOG_DESC("onReceiveExecResult: invalid execResult")
                              << LOG_KV("localReceiptSize", execResult->receiptsSize())
                              << LOG_KV("receiveReceiptSize", execResult->receiptsSize());
            return;
        }
        if (localExecResult->blockHeader()->hash() == execResult->blockHeader()->hash())
        {
            PBFT_LOG(WARNING) << LOG_DESC("onReceiveExecResult: receive consistent execResult")
                              << LOG_KV("localHash", execResult->blockHeader()->hash().abridged())
                              << LOG_KV(
                                     "receiveHash", execResult->blockHeader()->hash().abridged());
            return;
        }
        for (size_t i = 0; i < localExecResult->receiptsSize(); i++)
        {
            auto localResult = localExecResult->receipt(i);
            auto receiveResult = execResult->receipt(i);
            if (localResult->hash() != receiveResult->hash())
            {
                if (m_localBlock->transactionsSize() > i)
                {
                    auto tx = std::const_pointer_cast<Transaction>(m_localBlock->transaction(i));
                    tx->setAbort(true);
                }
                if (m_localBlock->transactionsMetaDataSize() > i)
                {
                    auto meta = std::const_pointer_cast<TransactionMetaData>(
                        m_localBlock->transactionMetaData(i));
                    meta->setAbort(true);
                }
                auto txHash = localExecResult->transactionHash(i);
                PBFT_LOG(INFO) << LOG_DESC("onReceiveExecResult: detect inconsistent tx")
                               << LOG_KV("localReceipt", localResult->hash().abridged())
                               << LOG_KV("receiveReceipt", receiveResult->hash().abridged())
                               << LOG_KV("index", i) << LOG_KV("tx", txHash.abridged());
            }
        }
        // TODO: consensus over the droped proposal
        m_nodesWithInconsistentCheckPoint[_nodeID] = true;
        for (auto const& it : m_nodesWithInconsistentCheckPoint)
        {
            if (!it.second)
            {
                return;
            }
        }
        PBFT_LOG(INFO) << LOG_DESC(
                              "onReceiveExecResult: receive all proposalState, re-execute "
                              "the proposal")
                       << LOG_KV("index", localExecResult->blockHeader()->number())
                       << LOG_KV("hash", localExecResult->blockHeader()->hash().abridged());
        // remove the executed proposal
        // re-exec block
        auto encodedData = std::make_shared<bytes>();
        m_localBlock->blockHeader()->setUndeterministic(true);
        m_localBlock->encode(*encodedData);
        // update the proposal
        m_precommit->consensusProposal()->setData(ref(*encodedData));
        m_precommit->consensusProposal()->setReExecFlag(true);
        // TODO: provid proof
        m_precommit->setPacketType(PacketType::DeterministicState);
        auto payLoad = m_config->codec()->encode(m_precommit);
        m_config->frontService()->asyncSendBroadcastMessage(
            bcos::protocol::NodeType::CONSENSUS_NODE, ModuleID::PBFT, ref(*payLoad));
        if (m_reExecHandler && !m_reExecuted)
        {
            m_reExecuted = true;
            m_checkpointProposal = nullptr;
            m_reExecHandler(m_precommit->consensusProposal());
            checkAndCommitStableCheckPoint();
        }
    }
    catch (std::exception const& e)
    {
        PBFT_LOG(WARNING) << LOG_DESC("onReceiveExecResult exception")
                          << LOG_KV("error", boost::diagnostic_information(e));
    }
}

bool PBFTCache::existPrePrepare(PBFTMessageInterface::Ptr _prePrepareMsg)
{
    if (!m_prePrepare)
    {
        return false;
    }
    return (_prePrepareMsg->hash() == m_prePrepare->hash()) &&
           (m_prePrepare->view() >= _prePrepareMsg->view());
}

void PBFTCache::addCache(CollectionCacheType& _cachedReq, QuorumRecoderType& _weightInfo,
    PBFTMessageInterface::Ptr _pbftCache)
{
    if (_pbftCache->index() != m_index)
    {
        return;
    }
    auto const& proposalHash = _pbftCache->hash();
    auto generatedFrom = _pbftCache->generatedFrom();
    if (_cachedReq.count(proposalHash) && _cachedReq[proposalHash].count(generatedFrom))
    {
        return;
    }
    auto nodeInfo = m_config->getConsensusNodeByIndex(generatedFrom);
    if (!nodeInfo)
    {
        return;
    }
    if (!_weightInfo.count(proposalHash))
    {
        _weightInfo[proposalHash] = 0;
    }
    _weightInfo[proposalHash] += nodeInfo->weight();
    _cachedReq[proposalHash][generatedFrom] = _pbftCache;
}

bool PBFTCache::conflictWithProcessedReq(PBFTMessageInterface::Ptr _msg)
{
    if (m_submitted || m_stableCommitted)
    {
        return true;
    }
    if (_msg->view() < m_config->view())
    {
        return true;
    }
    if (!m_prePrepare)
    {
        return false;
    }
    // expired msg
    if (_msg->view() < m_prePrepare->view())
    {
        return true;
    }
    // conflict msg
    if (_msg->view() == m_prePrepare->view())
    {
        return (_msg->hash() != m_prePrepare->hash());
    }
    return false;
}

bool PBFTCache::checkPrePrepareProposalStatus()
{
    if (m_prePrepare == nullptr)
    {
        return false;
    }
    if (m_prePrepare->view() != m_config->view())
    {
        return false;
    }
    return true;
}

bool PBFTCache::collectEnoughQuorum(
    bcos::crypto::HashType const& _hash, QuorumRecoderType& _weightInfo)
{
    if (!_weightInfo.count(_hash))
    {
        return false;
    }
    return (_weightInfo[_hash] >= m_config->minRequiredQuorum());
}

bool PBFTCache::collectEnoughPrepareReq()
{
    if (!checkPrePrepareProposalStatus())
    {
        return false;
    }
    return collectEnoughQuorum(m_prePrepare->hash(), m_prepareReqWeight);
}

bool PBFTCache::collectEnoughCommitReq()
{
    if (!checkPrePrepareProposalStatus())
    {
        return false;
    }
    return collectEnoughQuorum(m_prePrepare->hash(), m_commitReqWeight);
}

void PBFTCache::intoPrecommit()
{
    m_precommit = m_prePrepare;
    m_precommit->setGeneratedFrom(m_config->nodeIndex());
    setSignatureList(m_precommit->consensusProposal(), m_prepareCacheList);

    m_precommitWithoutData = m_precommit->populateWithoutProposal();
    auto precommitProposalWithoutData =
        m_config->pbftMessageFactory()->populateFrom(m_precommit->consensusProposal(), false);
    m_precommitWithoutData->setConsensusProposal(precommitProposalWithoutData);
    PBFT_LOG(INFO) << LOG_DESC("intoPrecommit") << printPBFTMsgInfo(m_precommit)
                   << m_config->printCurrentState();
}

void PBFTCache::setSignatureList(PBFTProposalInterface::Ptr _proposal, CollectionCacheType& _cache)
{
    assert(_cache.count(_proposal->hash()));
    _proposal->clearSignatureProof();
    for (auto const& it : _cache[_proposal->hash()])
    {
        _proposal->appendSignatureProof(it.first, it.second->consensusProposal()->signature());
    }
    PBFT_LOG(INFO) << LOG_DESC("setSignatureList")
                   << LOG_KV("signatureSize", _proposal->signatureProofSize())
                   << printPBFTProposal(_proposal);
}

bool PBFTCache::conflictWithPrecommitReq(PBFTMessageInterface::Ptr _prePrepareMsg)
{
    if (!m_precommit)
    {
        return false;
    }
    if (m_precommit->index() < m_config->progressedIndex())
    {
        return false;
    }
    if (_prePrepareMsg->index() == m_precommit->index() &&
        _prePrepareMsg->hash() != m_precommit->hash())
    {
        PBFT_LOG(INFO) << LOG_DESC(
                              "the received pre-prepare msg is conflict with the preparedCache")
                       << printPBFTMsgInfo(_prePrepareMsg);
        return true;
    }
    return false;
}

bool PBFTCache::checkAndPreCommit()
{
    // already precommitted
    if (m_precommitted)
    {
        return false;
    }
    // avoid to intoPrecommit when in timeout state
    if (m_config->timeout())
    {
        return false;
    }
    if (!m_prePrepare)
    {
        return false;
    }
    if (m_precommit && m_precommit->view() >= m_prePrepare->view())
    {
        return false;
    }
    if (m_prePrepare && m_prePrepare->view() != m_config->view())
    {
        return false;
    }
    if (!collectEnoughPrepareReq())
    {
        return false;
    }
    // update and backup the proposal into precommit-status
    intoPrecommit();
    // generate the commitReq
    auto commitReq = m_config->pbftMessageFactory()->populateFrom(PacketType::CommitPacket,
        m_config->pbftMsgDefaultVersion(), m_config->view(), utcTime(), m_config->nodeIndex(),
        m_precommitWithoutData->consensusProposal(), m_config->cryptoSuite(), m_config->keyPair());
    // add the commitReq to local cache
    addCommitCache(commitReq);
    // broadcast the commitReq
    PBFT_LOG(INFO) << LOG_DESC("checkAndPreCommit: broadcast commitMsg")
                   << LOG_KV("Idx", m_config->nodeIndex())
                   << LOG_KV("hash", commitReq->hash().abridged())
                   << LOG_KV("index", commitReq->index());
    auto encodedData = m_config->codec()->encode(commitReq, m_config->pbftMsgDefaultVersion());
    // only broadcast message to consensus nodes
    m_config->frontService()->asyncSendBroadcastMessage(
        bcos::protocol::NodeType::CONSENSUS_NODE, ModuleID::PBFT, ref(*encodedData));
    m_precommitted = true;
    // collect the commitReq and try to commit
    return checkAndCommit();
}

bool PBFTCache::checkAndCommit()
{
    // avoid to intoPrecommit when in timeout state
    if (m_config->timeout())
    {
        return false;
    }
    if (m_submitted)
    {
        return false;
    }
    // collect enough commit message before intoPrecommit
    // can only into commit status when precommitted
    if (!m_precommit)
    {
        return false;
    }
    if (m_precommit->view() != m_config->view())
    {
        return false;
    }
    if (!collectEnoughCommitReq())
    {
        return false;
    }
    if (m_consStartTime > 0)
    {
        PBFT_LOG(INFO) << LOG_DESC("checkAndCommit success")
                       << LOG_KV("consTime", (utcTime() - m_consStartTime))
                       << printPBFTProposal(m_precommit->consensusProposal())
                       << m_config->printCurrentState();
    }
    m_submitted.store(true);
    return true;
}

bool PBFTCache::shouldStopTimer()
{
    if (m_index <= m_config->committedProposal()->index())
    {
        return true;
    }
    return m_submitted;
}

void PBFTCache::resetCache(ViewType _curView)
{
    m_submitted = false;
    m_precommitted = false;
    // clear the expired prepare cache
    resetCacheAfterViewChange(m_prepareCacheList, _curView);
    // clear the expired commit cache
    resetCacheAfterViewChange(m_commitCacheList, _curView);

    // recalculate m_prepareReqWeight
    recalculateQuorum(m_prepareReqWeight, m_prepareCacheList);
    // recalculate m_commitReqWeight
    recalculateQuorum(m_commitReqWeight, m_commitCacheList);
}

void PBFTCache::setCheckPointProposal(PBFTProposalInterface::Ptr _proposal)
{
    // expired checkpoint proposal
    if (_proposal->index() <= m_config->committedProposal()->index())
    {
        PBFT_LOG(WARNING) << LOG_DESC("setCheckPointProposal failed for expired checkpoint index")
                          << m_config->printCurrentState() << printPBFTProposal(_proposal);
        return;
    }
    if (_proposal->index() != index())
    {
        return;
    }
    m_checkpointProposal = _proposal;
    // Note: the timer can only been started after setCheckPointProposal success
    m_timer->start();
    PBFT_LOG(INFO) << LOG_DESC("setCheckPointProposal") << printPBFTProposal(m_checkpointProposal)
                   << m_config->printCurrentState();
}

bool PBFTCache::collectEnoughCheckpoint()
{
    if (!m_checkpointProposal)
    {
        return false;
    }
    return collectEnoughQuorum(m_checkpointProposal->hash(), m_checkpointCacheWeight);
}

bool PBFTCache::checkAndCommitStableCheckPoint()
{
    if (m_stableCommitted)
    {
        return false;
    }
    // Before this proposal reach checkPoint consensus,
    // it must be ensured that the dependent system transactions
    // (such as transactions including dynamically addSealer/removeNode, setConsensusWeight, etc.)
    // have been committed
    auto committedIndex = m_config->committedProposal()->index();
    auto dependsProposal = std::min((m_index - 1), m_config->waitSealUntil());
    if (committedIndex < dependsProposal)
    {
        return false;
    }
    if (committedIndex == dependsProposal)
    {
        recalculateQuorum(m_checkpointCacheWeight, m_checkpointCacheList);
    }
    if (!collectEnoughCheckpoint())
    {
        triggerNonDeterministic();
        return false;
    }
    setSignatureList(m_checkpointProposal, m_checkpointCacheList);
    m_stableCommitted = true;
    PBFT_LOG(INFO) << LOG_DESC("checkAndCommitStableCheckPoint")
                   << LOG_KV("index", m_checkpointProposal->index())
                   << LOG_KV("hash", m_checkpointProposal->hash().abridged())
                   << m_config->printCurrentState();
    if (m_config->committedProposal()->index() >= m_checkpointProposal->index())
    {
        PBFT_LOG(WARNING) << LOG_DESC("checkAndCommitStableCheckPoint: expired checkpointProposal")
                          << LOG_KV("checkPointIndex", m_checkpointProposal->index())
                          << m_config->printCurrentState();
        return false;
    }
    return true;
}

bool PBFTCache::resetPrecommitCache(PBFTMessageInterface::Ptr _precommit, bool _needReExec)
{
    RecursiveGuard l(m_mutex);
    if (m_resetted)
    {
        return false;
    }
    if (!m_precommit)
    {
        return false;
    }
    if (_precommit->hash() != m_precommit->hash())
    {
        return false;
    }
    m_config->enforceSerial();
    m_config->timer()->restart();
    m_prePrepare = _precommit;
    m_precommit = m_prePrepare;
    m_precommit->setGeneratedFrom(m_config->nodeIndex());
    m_precommit->consensusProposal()->setReExecFlag(_needReExec);
    m_precommitWithoutData = m_precommit->populateWithoutProposal();
    auto precommitProposalWithoutData =
        m_config->pbftMessageFactory()->populateFrom(m_precommit->consensusProposal(), false);
    m_precommitWithoutData->setConsensusProposal(precommitProposalWithoutData);
    if (_needReExec && !m_reExecuted && m_reExecHandler)
    {
        PBFT_LOG(INFO) << LOG_DESC("resetPrecommitCache and re-exec the proposal")
                       << printPBFTMsgInfo(m_precommit);
        m_reExecuted = true;
        // stall the pipeline
        m_config->setWaitSealUntil(m_index);
        m_config->setWaitResealUntil(m_index);
        m_config->setExpectedCheckPoint(m_index);
        // reset the checkpoint proposal
        m_checkpointProposal = nullptr;
        m_reExecHandler(m_precommit->consensusProposal());
    }
    checkAndCommitStableCheckPoint();
    m_resetted = true;
    return true;
}