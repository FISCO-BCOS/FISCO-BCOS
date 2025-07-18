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
#include "bcos-task/Wait.h"

using namespace bcos;
using namespace bcos::consensus;
using namespace bcos::protocol;
using namespace bcos::crypto;

PBFTCache::PBFTCache(PBFTConfig::Ptr _config, bcos::protocol::BlockNumber _index)
  : m_config(std::move(_config)), m_index(_index)
{}

void PBFTCache::onCheckPointTimeout()
{
    // Note: this logic is unreachable
    if (!m_checkpointProposal ||
        std::cmp_less(utcTime() - m_checkPointStartTime, m_config->checkPointTimeoutInterval()))
    {
        return;
    }
    m_checkPointStartTime = utcTime();
    if (m_committedIndexNotifier && !m_config->timer()->running())
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
    task::wait(
        [](front::FrontServiceInterface::Ptr front, bytesPointer encodedData) -> task::Task<void> {
            co_await front->broadcastMessage(bcos::protocol::NodeType::CONSENSUS_NODE,
                ModuleID::PBFT, ::ranges::views::single(ref(*encodedData)));
        }(m_config->frontService(), std::move(encodedData)));
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
    if (_cachedReq.contains(proposalHash) && _cachedReq[proposalHash].contains(generatedFrom))
    {
        return;
    }
    auto nodeInfo = m_config->getConsensusNodeByIndex(generatedFrom);
    if (!nodeInfo)
    {
        return;
    }
    auto [it, _] = _weightInfo.try_emplace(proposalHash, 0);
    it->second += nodeInfo->voteWeight;
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
    if (m_precommitted || !m_prePrepare || m_config->timeout())
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
    task::wait(
        [](front::FrontServiceInterface::Ptr front, bytesPointer encodedData) -> task::Task<void> {
            co_await front->broadcastMessage(bcos::protocol::NodeType::CONSENSUS_NODE,
                ModuleID::PBFT, ::ranges::views::single(ref(*encodedData)));
        }(m_config->frontService(), std::move(encodedData)));
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
    PBFT_LOG(INFO) << LOG_DESC("checkAndCommit")
                   << printPBFTProposal(m_precommit->consensusProposal())
                   << m_config->printCurrentState();
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
    PBFT_LOG(INFO) << LOG_DESC("resetCache") << LOG_KV("precommit", m_precommit ? "true" : "false")
                   << LOG_KV("prePrepare", m_prePrepare ? "true" : "false")
                   << LOG_KV("prepareView", m_prePrepare ? m_prePrepare->view() : 0)
                   << LOG_KV("curView", _curView)
                   << ((m_prePrepare && m_prePrepare->consensusProposal()) ?
                              printPBFTProposal(m_prePrepare->consensusProposal()) :
                              "consensusProposal is null");
    if (!m_precommit && m_prePrepare && m_prePrepare->consensusProposal() &&
        m_prePrepare->view() < _curView)
    {
        PBFT_LOG(INFO) << LOG_DESC("resetCache : asyncResetTxsFlag")
                       << printPBFTProposal(m_prePrepare->consensusProposal());
        // reset the sealingManager, in case of the same block has been sealed twice
        m_config->notifyResetSealing(m_prePrepare->consensusProposal()->index());
        // reset the exceptioned txs to unsealed
        auto block = m_config->blockFactory().createBlock(
            m_prePrepare->consensusProposal()->data(), false, false);
        m_config->validator()->asyncResetTxsFlag(*block, false);
        m_prePrepare = nullptr;
    }
    resetExceptionCache(_curView);
    // clear the expired prepare cache
    resetCacheAfterViewChange(m_prepareCacheList, _curView);
    // clear the expired commit cache
    resetCacheAfterViewChange(m_commitCacheList, _curView);

    // recalculate m_prepareReqWeight
    recalculateQuorum(m_prepareReqWeight, m_prepareCacheList);
    // recalculate m_commitReqWeight
    recalculateQuorum(m_commitReqWeight, m_commitCacheList);
}

void PBFTCache::resetExceptionCache(ViewType _curView)
{
    if (m_exceptionPrePrepareList.empty()) [[likely]]
    {
        return;
    }
    for (auto exceptionPrePrepare = m_exceptionPrePrepareList.begin();
        exceptionPrePrepare != m_exceptionPrePrepareList.end();)
    {
        PBFT_LOG(INFO) << LOG_DESC("resetCache: asyncResetTxsFlag exceptionPrePrepare")
                       << LOG_KV("prePrepare", m_prePrepare ? "true" : "false")
                       << LOG_KV("curView", _curView)
                       << (m_precommit ? printPBFTProposal(m_precommit) : "precommit is null")
                       << ((m_prePrepare && m_prePrepare->consensusProposal()) ?
                                  printPBFTProposal(m_prePrepare->consensusProposal()) :
                                  "consensusProposal is null")
                       << printPBFTProposal((*exceptionPrePrepare)->consensusProposal());
        auto validPrePrepare = (m_precommit || (m_prePrepare && m_prePrepare->consensusProposal() &&
                                                   m_prePrepare->view() >= _curView));
        if (validPrePrepare &&
            (m_prePrepare && m_prePrepare->hash() == (*exceptionPrePrepare)->hash()))
        {
            if (c_fileLogLevel == TRACE) [[unlikely]]
            {
                PBFT_LOG(TRACE) << LOG_DESC("resetCache: exceptionPrePrepare but finally be valid")
                                << printPBFTProposal((*exceptionPrePrepare)->consensusProposal());
            }
        }
        else if (m_precommit && m_precommit->hash() == (*exceptionPrePrepare)->hash())
        {
            if (c_fileLogLevel == TRACE) [[unlikely]]
            {
                PBFT_LOG(TRACE) << LOG_DESC("resetCache: prepare finaly into commit")
                                << printPBFTProposal((*exceptionPrePrepare)->consensusProposal());
            }
        }
        else
        {
            PBFT_LOG(INFO) << LOG_DESC("resetCache: asyncResetTxsFlag exceptionPrePrepare")
                           << printPBFTProposal((*exceptionPrePrepare)->consensusProposal());
            auto block = m_config->blockFactory().createBlock(
                (*exceptionPrePrepare)->consensusProposal()->data(), false, false);
            m_config->validator()->asyncResetTxsFlag(*block, false);
            exceptionPrePrepare = m_exceptionPrePrepareList.erase(exceptionPrePrepare);
            if (exceptionPrePrepare == m_exceptionPrePrepareList.end())
            {
                m_prePrepare = nullptr;
                break;
            }
        }
        exceptionPrePrepare++;
    }
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
    // wait for the sys-proposal committed to trigger checkAndCommitStableCheckPoint
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