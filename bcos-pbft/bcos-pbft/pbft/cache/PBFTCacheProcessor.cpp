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
 * @brief cache processor for the PBFTReq
 * @file PBFTCacheProcessor.cpp
 * @author: yujiechen
 * @date 2021-04-21
 */
#include "PBFTCacheProcessor.h"
#include "bcos-task/Wait.h"
#include <bcos-framework/protocol/CommonError.h>
#include <bcos-framework/protocol/Protocol.h>
#include <boost/bind/bind.hpp>
#include <utility>

using namespace bcos;
using namespace bcos::consensus;
using namespace bcos::protocol;
using namespace bcos::crypto;

void PBFTCacheProcessor::tryToResendCheckPoint()
{
    for (auto const& cache : m_caches)
    {
        cache.second->onCheckPointTimeout();
    }
}

void PBFTCacheProcessor::initState(PBFTProposalList const& _proposals, NodeIDPtr _fromNode)
{
    for (const auto& proposal : _proposals)
    {
        // the proposal has already been committed
        if (proposal->index() <= m_config->committedProposal()->index())
        {
            PBFT_LOG(DEBUG) << LOG_DESC("initState: skip committedProposal")
                            << LOG_KV("index", proposal->index())
                            << LOG_KV("hash", proposal->hash().abridged());
            continue;
        }
        PBFT_LOG(DEBUG) << LOG_DESC("initState: apply committedProposal")
                        << LOG_KV("index", proposal->index())
                        << LOG_KV("hash", proposal->hash().abridged());
        // set the txs status to be sealed
        auto block = m_config->blockFactory().createBlock(proposal->data(), false, false);
        m_config->validator()->asyncResetTxsFlag(*block, true);
        // try to verify and load the proposal
        loadAndVerifyProposal(_fromNode, proposal);
    }
}

void PBFTCacheProcessor::loadAndVerifyProposal(
    NodeIDPtr _fromNode, PBFTProposalInterface::Ptr _proposal, size_t _retryTime)
{
    if (_retryTime > 3)
    {
        return;
    }
    // Note: to fetch the remote proposal(the from node hits all transactions)
    auto self = weak_from_this();
    m_config->validator()->verifyProposal(_fromNode, _proposal,
        [self, _fromNode, _proposal, _retryTime](Error::Ptr _error, bool _verifyResult) {
            try
            {
                auto cache = self.lock();
                if (!cache)
                {
                    return;
                }
                if (_error && _error->errorCode() == bcos::protocol::CommonError::TIMEOUT)
                {
                    PBFT_LOG(INFO)
                        << LOG_DESC("loadAndVerifyProposal failed for timeout, retry again")
                        << LOG_KV("msg", _error->errorMessage());
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    cache->loadAndVerifyProposal(_fromNode, _proposal, (_retryTime + 1));
                    return;
                }
                auto config = cache->m_config;
                if (_error || !_verifyResult)
                {
                    auto waterMark = std::min(config->lowWaterMark(), _proposal->index() - 1);
                    waterMark = std::max(waterMark, config->progressedIndex());
                    config->setLowWaterMark(waterMark);
                    PBFT_LOG(INFO)
                        << LOG_DESC("loadAndVerifyProposal failed") << printPBFTProposal(_proposal)
                        << LOG_KV("from", _fromNode->shortHex())
                        << LOG_KV("code", _error ? _error->errorCode() : 0)
                        << LOG_KV("msg", _error ? _error->errorMessage() : "requestSent")
                        << LOG_KV("verifyRet", _verifyResult)
                        << LOG_KV("lowWaterMark", config->lowWaterMark());
                }
                else
                {
                    PBFT_LOG(INFO)
                        << LOG_DESC("loadAndVerifyProposal success")
                        << LOG_KV("from", _fromNode->shortHex()) << printPBFTProposal(_proposal);
                }
                cache->m_onLoadAndVerifyProposalFinish(_verifyResult, _error, _proposal);
            }
            catch (std::exception const& e)
            {
                PBFT_LOG(WARNING) << LOG_DESC("loadAndVerifyProposal exception")
                                  << printPBFTProposal(_proposal)
                                  << LOG_KV("msg", boost::diagnostic_information(e));
            }
        });
}
// Note: please ensure the passed in _prePrepareMsg not be modified after addPrePrepareCache
void PBFTCacheProcessor::addPrePrepareCache(PBFTMessageInterface::Ptr _prePrepareMsg)
{
    addCache(m_caches, _prePrepareMsg,
        [](PBFTCache::Ptr _pbftCache, PBFTMessageInterface::Ptr proposal) {
            _pbftCache->addPrePrepareCache(std::move(proposal));
        });
    // notify the consensusing proposal index to the sync module
    notifyMaxProposalIndex(_prePrepareMsg->index());
}

void PBFTCacheProcessor::notifyMaxProposalIndex(bcos::protocol::BlockNumber _proposalIndex)
{
    // Note: should not notifyMaxProposalIndex when timeout-state, in-case of reset the
    // committedProposalNumber of the sync-module with higher blockNumber than latest block, which
    // will stop the node to syncing the latest block from other nodes
    if (m_config->timeout())
    {
        return;
    }
    // notify the consensusing proposal index to the sync module
    if (m_maxNotifyIndex < _proposalIndex)
    {
        m_maxNotifyIndex = _proposalIndex;
        notifyCommittedProposalIndex(m_maxNotifyIndex);
    }
}

bool PBFTCacheProcessor::existPrePrepare(PBFTMessageInterface::Ptr const& _prePrepareMsg) const
{
    if (!m_caches.contains(_prePrepareMsg->index()))
    {
        return false;
    }
    auto pbftCache = m_caches.at(_prePrepareMsg->index());
    return pbftCache->existPrePrepare(_prePrepareMsg);
}

bool PBFTCacheProcessor::tryToFillProposal(PBFTMessageInterface::Ptr _prePrepareMsg)
{
    if (!m_caches.contains(_prePrepareMsg->index()))
    {
        return false;
    }
    auto pbftCache = m_caches[_prePrepareMsg->index()];
    auto precommit = pbftCache->preCommitCache();
    if (!precommit)
    {
        return false;
    }
    auto hit = (precommit->hash() == _prePrepareMsg->hash()) &&
               (precommit->index() == _prePrepareMsg->index());
    if (!hit)
    {
        return false;
    }
    auto proposalData = precommit->consensusProposal()->data();
    _prePrepareMsg->consensusProposal()->setData(proposalData);
    return true;
}

bool PBFTCacheProcessor::conflictWithProcessedReq(PBFTMessageInterface::Ptr const& _msg) const
{
    if (!m_caches.contains(_msg->index()))
    {
        return false;
    }
    auto pbftCache = m_caches.at(_msg->index());
    return pbftCache->conflictWithProcessedReq(_msg);
}

bool PBFTCacheProcessor::conflictWithPrecommitReq(
    PBFTMessageInterface::Ptr const& _prePrepareMsg) const
{
    if (!m_caches.contains(_prePrepareMsg->index()))
    {
        return false;
    }
    auto pbftCache = m_caches.at(_prePrepareMsg->index());
    return pbftCache->conflictWithPrecommitReq(_prePrepareMsg);
}

void PBFTCacheProcessor::addCache(
    PBFTCachesType& _pbftCache, PBFTMessageInterface::Ptr _pbftReq, UpdateCacheHandler _handler)

{
    auto index = _pbftReq->index();
    if (!_pbftCache.contains(index))
    {
        _pbftCache[index] = m_cacheFactory->createPBFTCache(m_config, index,
            boost::bind(
                &PBFTCacheProcessor::notifyCommittedProposalIndex, this, boost::placeholders::_1));
    }
    _handler(_pbftCache[index], _pbftReq);
}

void PBFTCacheProcessor::checkAndPreCommit()
{
    for (auto const& cache : m_caches)
    {
        auto ret = cache.second->checkAndPreCommit();
        if (!ret)
        {
            continue;
        }
        updateCommitQueue(cache.second->preCommitCache()->consensusProposal());
        // refresh the timer when commit success
        m_config->timer()->restart();
        m_config->resetToView();
    }
    resetTimer();
}

void PBFTCacheProcessor::checkAndCommit()
{
    for (auto const& cache : m_caches)
    {
        auto ret = cache.second->checkAndCommit();
        if (!ret)
        {
            continue;
        }
        updateCommitQueue(cache.second->preCommitCache()->consensusProposal());
        // refresh the timer when commit success
        m_config->timer()->restart();
        m_config->resetToView();
    }
    resetTimer();
}

void PBFTCacheProcessor::resetTimer()
{
    for (auto const& cache : m_caches)
    {
        if (!cache.second->shouldStopTimer())
        {
            // start the timer when there has proposals in consensus
            if (!m_config->timer()->running())
            {
                m_config->timer()->start();
            }
            return;
        }
    }
    // reset the timer when has no proposals in consensus
    m_config->freshTimer();
    m_config->tryTriggerFastViewChange(m_config->getLeader());
}

void PBFTCacheProcessor::updateCommitQueue(PBFTProposalInterface::Ptr _committedProposal)
{
    assert(_committedProposal);
    if (m_executingProposals.contains(_committedProposal->hash()))
    {
        return;
    }
    // the proposal has already been committed
    auto proposalIndex = _committedProposal->index();
    if (proposalIndex <= m_config->committedProposal()->index())
    {
        return;
    }
    notifyMaxProposalIndex(proposalIndex);
    m_committedQueue.push(_committedProposal);
    m_committedProposalList.insert(proposalIndex);
    m_proposalsToStableConsensus.insert(proposalIndex);
    PBFT_LOG(INFO) << LOG_DESC("######## CommitProposal") << printPBFTProposal(_committedProposal)
                   << LOG_KV("sys", _committedProposal->systemProposal())
                   << m_config->printCurrentState();
    if (_committedProposal->systemProposal())
    {
        m_config->setWaitSealUntil(proposalIndex);
        PBFT_LOG(INFO) << LOG_DESC(
                              "Receive valid system prePrepare proposal, stop to notify sealing")
                       << LOG_KV("waitSealUntil", proposalIndex);
    }
    // Note: should notify to seal nextBlock after waitSealUntil setted, in case of the system
    // proposals are generated and committed not by serial
    notifyToSealNextBlock();
    tryToPreApplyProposal(_committedProposal);  // will query scheduler to encode message and fill
                                                // txbytes in blocks
    tryToApplyCommitQueue();
}

void PBFTCacheProcessor::notifyCommittedProposalIndex(bcos::protocol::BlockNumber _index)
{
    if (!m_committedProposalNotifier)
    {
        return;
    }
    m_committedProposalNotifier(_index, [_index](Error::Ptr _error) {
        if (!_error)
        {
            PBFT_LOG(INFO) << LOG_DESC(
                                  "notify the committed proposal index to the sync module success")
                           << LOG_KV("index", _index);
            return;
        }
        PBFT_LOG(WARNING) << LOG_DESC(
                                 "notify the committed proposal index to the sync module failed")
                          << LOG_KV("index", _index);
    });
}

ProposalInterface::ConstPtr PBFTCacheProcessor::getAppliedCheckPointProposal(
    bcos::protocol::BlockNumber _index)
{
    if (_index == m_config->committedProposal()->index())
    {
        return m_config->committedProposal();
    }

    if (!m_caches.contains(_index))
    {
        return nullptr;
    }
    return (m_caches[_index])->checkPointProposal();
}

bool PBFTCacheProcessor::tryToPreApplyProposal(ProposalInterface::Ptr _proposal)
{
    m_config->stateMachine()->asyncPreApply(
        std::move(_proposal), [](bool success) { (void)success; });

    return true;
}

bool PBFTCacheProcessor::tryToApplyCommitQueue()
{
    notifyToSealNextBlock();
    while (!m_committedQueue.empty() &&
           m_committedQueue.top()->index() < m_config->expectedCheckPoint())
    {
        PBFT_LOG(INFO) << LOG_DESC("updateCommitQueue: remove invalid proposal")
                       << LOG_KV("index", m_committedQueue.top()->index())
                       << LOG_KV("expectedIndex", m_config->expectedCheckPoint())
                       << m_config->printCurrentState();
        m_committedQueue.pop();
    }
    // try to execute the proposal
    if (!m_committedQueue.empty() &&
        m_committedQueue.top()->index() == m_config->expectedCheckPoint())
    {
        auto committedIndex = m_config->committedProposal()->index();
        // must wait for the sys-proposal committed to execute new proposal
        auto dependsProposal =
            std::min((m_config->expectedCheckPoint() - 1), m_config->waitSealUntil());
        // enforce to serial execute if the system-proposal not committed
        if (committedIndex < dependsProposal)
        {
            return false;
        }
        auto proposal = m_committedQueue.top();
        auto lastAppliedProposal = getAppliedCheckPointProposal(m_config->expectedCheckPoint() - 1);
        if (!lastAppliedProposal)
        {
            PBFT_LOG(WARNING) << LOG_DESC("The last proposal has not been applied")
                              << m_config->printCurrentState();
            return false;
        }
        if (m_executingProposals.contains(proposal->hash()))
        {
            m_config->timer()->restart();
            PBFT_LOG(INFO) << LOG_DESC("the proposal is executing, not executed again")
                           << LOG_KV("index", proposal->index())
                           << LOG_KV("hash", proposal->hash().abridged())
                           << m_config->printCurrentState();
            return false;
        }
        // commit the proposal
        m_committedQueue.pop();
        // in case of the same block execute more than once
        m_executingProposals[proposal->hash()] = proposal->index();
        applyStateMachine(lastAppliedProposal, proposal);
        return true;
    }
    return false;
}

void PBFTCacheProcessor::notifyToSealNextBlock()
{
    // find the non-consecutive minimum proposal index and notify the corresponding leader to pack
    // the block
    auto committedIndex = m_config->committedProposal()->index();
    bcos::protocol::BlockNumber lastIndex = committedIndex;
    for (auto const& proposalIndex : m_proposalsToStableConsensus)
    {
        if (proposalIndex <= committedIndex)
        {
            lastIndex = proposalIndex;
            continue;
        }
        if (lastIndex + 1 < proposalIndex)
        {
            break;
        }
        lastIndex = proposalIndex;
    }
    auto nextProposalIndex = std::max(lastIndex + 1, committedIndex + 1);
    m_config->notifySealer(nextProposalIndex);
    PBFT_LOG(INFO) << LOG_DESC("notify to seal next proposal")
                   << LOG_KV("nextProposalIndex", nextProposalIndex);
}

// execute the proposal and broadcast checkpoint message
void PBFTCacheProcessor::applyStateMachine(
    ProposalInterface::ConstPtr _lastAppliedProposal, PBFTProposalInterface::Ptr _proposal)
{
    PBFT_LOG(INFO) << LOG_DESC("applyStateMachine") << LOG_KV("index", _proposal->index())
                   << LOG_KV("hash", _proposal->hash().abridged()) << m_config->printCurrentState()
                   << LOG_KV("unAppliedProposals", m_committedQueue.size());
    auto executedProposal = m_config->pbftMessageFactory()->createPBFTProposal();
    auto self = weak_from_this();
    auto startT = utcTime();
    m_config->stateMachine()->asyncApply(m_config->timer()->timeout(),
        std::move(_lastAppliedProposal), _proposal, executedProposal,
        [self, startT, _proposal, executedProposal](int64_t _ret) {
            try
            {
                auto cache = self.lock();
                if (!cache)
                {
                    return;
                }
                auto config = cache->m_config;
                if (config->committedProposal()->index() >= _proposal->index())
                {
                    PBFT_LOG(WARNING)
                        << LOG_DESC("applyStateMachine: give up the proposal for expired")
                        << config->printCurrentState()
                        << LOG_KV("beforeExec", _proposal->hash().abridged())
                        << LOG_KV("afterExec", executedProposal->hash().abridged())
                        << LOG_KV("timecost", utcTime() - startT);
                    return;
                }
                if (cache->m_proposalAppliedHandler)
                {
                    cache->m_proposalAppliedHandler(_ret, _proposal, executedProposal);
                }
                PBFT_LOG(INFO) << LOG_DESC("applyStateMachine finished")
                               << LOG_KV("index", _proposal->index())
                               << LOG_KV("beforeExec", _proposal->hash().abridged())
                               << LOG_KV("afterExec", executedProposal->hash().abridged())
                               << config->printCurrentState()
                               << LOG_KV("timecost", utcTime() - startT);
            }
            catch (std::exception const& e)
            {
                PBFT_LOG(WARNING) << LOG_DESC("applyStateMachine failed")
                                  << LOG_KV("message", boost::diagnostic_information(e));
            }
        });
}

void PBFTCacheProcessor::setCheckPointProposal(PBFTProposalInterface::Ptr _proposal)
{
    auto index = _proposal->index();
    if (!m_caches.contains(index))
    {
        // Note: since cache is created and freed frequently, it should be safer to use weak_ptr in
        // the callback
        auto self = weak_from_this();
        m_caches[index] = m_cacheFactory->createPBFTCache(
            m_config, index, [self](bcos::protocol::BlockNumber _proposalIndex) {
                try
                {
                    auto cache = self.lock();
                    if (!cache)
                    {
                        return;
                    }
                    cache->notifyCommittedProposalIndex(_proposalIndex);
                }
                catch (std::exception const& e)
                {
                    PBFT_LOG(WARNING) << LOG_DESC("notifyCommittedProposalIndex error")
                                      << LOG_KV("index", _proposalIndex)
                                      << LOG_KV("msg", boost::diagnostic_information(e));
                }
            });
    }
    (m_caches[index])->setCheckPointProposal(_proposal);
}

void PBFTCacheProcessor::addCheckPointMsg(PBFTMessageInterface::Ptr _checkPointMsg)
{
    addCache(m_caches, std::move(_checkPointMsg),
        [](PBFTCache::Ptr _pbftCache, PBFTMessageInterface::Ptr _checkPointMsg) {
            _pbftCache->addCheckPointMsg(std::move(_checkPointMsg));
        });
}

void PBFTCacheProcessor::addViewChangeReq(ViewChangeMsgInterface::Ptr _viewChange)
{
    auto reqView = _viewChange->view();
    auto fromIdx = _viewChange->generatedFrom();
    if (m_viewChangeCache.contains(reqView) && m_viewChangeCache[reqView].contains(fromIdx))
    {
        return;
    }

    auto* nodeInfo = m_config->getConsensusNodeByIndex(fromIdx);
    if (!nodeInfo)
    {
        return;
    }
    m_viewChangeCache[reqView][fromIdx] = _viewChange;
    m_viewChangeWeight.try_emplace(reqView, 0);
    m_viewChangeWeight[reqView] += nodeInfo->voteWeight;
    auto committedIndex = _viewChange->committedProposal()->index();
    if (!m_maxCommittedIndex.contains(reqView) || m_maxCommittedIndex[reqView] < committedIndex)
    {
        m_maxCommittedIndex[reqView] = committedIndex;
    }
    // get the max precommitIndex
    for (const auto& precommit : _viewChange->preparedProposals())
    {
        auto precommitIndex = precommit->index();
        if (!m_maxPrecommitIndex.contains(reqView) || m_maxPrecommitIndex[reqView] < precommitIndex)
        {
            m_maxPrecommitIndex[reqView] = precommitIndex;
        }
    }
    // print the prepared proposal info
    std::stringstream preparedProposalInfo;
    preparedProposalInfo << " preparedProposalInfo: ";
    for (const auto& proposal : _viewChange->preparedProposals())
    {
        preparedProposalInfo << LOG_KV("propIndex", proposal->index())
                             << LOG_KV("propHash", proposal->hash().abridged())
                             << LOG_KV("fromIdx", proposal->generatedFrom())
                             << LOG_KV("dataSize", proposal->consensusProposal()->data().size());
    }
    PBFT_LOG(INFO) << LOG_DESC("addViewChangeReq") << printPBFTMsgInfo(_viewChange)
                   << LOG_KV("weight", m_viewChangeWeight[reqView])
                   << LOG_KV("maxCommittedIndex", m_maxCommittedIndex[reqView])
                   << LOG_KV("maxPrecommitIndex", m_maxPrecommitIndex[reqView])
                   << LOG_DESC(preparedProposalInfo.str()) << m_config->printCurrentState();
}

PBFTMessageList PBFTCacheProcessor::generatePrePrepareMsg(
    std::map<IndexType, ViewChangeMsgInterface::Ptr> _viewChangeCache)
{
    auto toView = m_config->toView();
    auto committedIndex = m_config->committedProposal()->index();
    auto maxCommittedIndex = committedIndex;
    if (m_maxCommittedIndex.contains(toView))
    {
        maxCommittedIndex = m_maxCommittedIndex[toView];
    }
    auto maxPrecommitIndex = committedIndex;
    if (m_maxPrecommitIndex.contains(toView))
    {
        maxPrecommitIndex = m_maxPrecommitIndex[toView];
    }
    // should not handle the proposal future than the system proposal
    if (m_config->waitSealUntil() > committedIndex)
    {
        maxPrecommitIndex = std::min(m_config->waitSealUntil(), maxPrecommitIndex);
    }
    std::map<BlockNumber, PBFTMessageInterface::Ptr> preparedProposals;
    for (const auto& it : _viewChangeCache)
    {
        auto viewChangeReq = it.second;
        for (const auto& proposal : viewChangeReq->preparedProposals())
        {
            // invalid precommit proposal
            if (proposal->index() < maxCommittedIndex)
            {
                continue;
            }
            // repeated precommit proposal
            if (preparedProposals.contains(proposal->index()))
            {
                auto precommitProposal = preparedProposals[proposal->index()];
                if (precommitProposal->index() != proposal->index() ||
                    precommitProposal->hash() != proposal->hash())
                {
                    // fatal case: two proposals in the same view with different hash
                    if (precommitProposal->view() == proposal->view()) [[unlikely]]
                    {
                        PBFT_LOG(FATAL)
                            << LOG_DESC(
                                   "generatePrePrepareMsg error: found conflict precommit "
                                   "proposals")
                            << LOG_DESC("proposal already exist:")
                            << printPBFTProposal(precommitProposal)
                            << LOG_DESC("conflicted proposal:") << printPBFTProposal(proposal);
                    }
                    // newer precommit proposal
                    if (precommitProposal->view() < proposal->view())
                    {
                        preparedProposals[proposal->index()] = proposal;
                    }
                }
                continue;
            }
            // new precommit proposal
            preparedProposals[proposal->index()] = proposal;
        }
    }
    // generate prepareMsg from maxCommittedIndex to maxPrecommitIndex
    PBFTMessageList prePrepareMsgList;
    for (auto i = (maxCommittedIndex + 1); i <= maxPrecommitIndex; i++)
    {
        PBFTProposalInterface::Ptr prePrepareProposal = nullptr;
        auto generatedFrom = m_config->nodeIndex();
        bool empty = false;
        if (preparedProposals.contains(i))
        {
            prePrepareProposal = preparedProposals[i]->consensusProposal();
            generatedFrom = preparedProposals[i]->generatedFrom();
        }
        else
        {
            // empty prePrepare
            prePrepareProposal =
                m_config->validator()->generateEmptyProposal(m_config->compatibilityVersion(),
                    m_config->pbftMessageFactory(), i, m_config->nodeIndex());
            empty = true;
        }
        auto prePrepareMsg = m_config->pbftMessageFactory()->populateFrom(
            PacketType::PrePreparePacket, prePrepareProposal, m_config->pbftMsgDefaultVersion(),
            m_config->toView(), utcTime(), generatedFrom);
        prePrepareMsgList.push_back(prePrepareMsg);
        PBFT_LOG(INFO) << LOG_DESC("generatePrePrepareMsg") << printPBFTMsgInfo(prePrepareMsg)
                       << LOG_KV("dataSize", prePrepareMsg->consensusProposal()->data().size())
                       << LOG_KV("emptyProposal", empty);
    }
    return prePrepareMsgList;
}

NewViewMsgInterface::Ptr PBFTCacheProcessor::checkAndTryIntoNewView()
{
    // in syncing mode, no need to try into the newView period
    if (m_config->committedProposal()->index() < m_config->syncingHighestNumber())
    {
        return nullptr;
    }
    if (m_newViewGenerated || !m_config->leaderAfterViewChange())
    {
        return nullptr;
    }
    auto toView = m_config->toView();
    if (!m_viewChangeWeight.contains(toView))
    {
        return nullptr;
    }
    if (m_viewChangeWeight[toView] < m_config->minRequiredQuorum())
    {
        return nullptr;
    }
    // the next leader collect enough viewChange requests
    // set the viewchanges(without prePreparedProposals)
    auto viewChangeCache = m_viewChangeCache[toView];
    ViewChangeMsgList viewChangeList;
    for (auto const& it : viewChangeCache)
    {
        viewChangeList.push_back(it.second);
    }
    // create newView message
    auto newViewMsg = m_config->pbftMessageFactory()->createNewViewMsg();
    newViewMsg->setHash(m_config->committedProposal()->hash());
    newViewMsg->setIndex(m_config->committedProposal()->index());
    newViewMsg->setPacketType(PacketType::NewViewPacket);
    newViewMsg->setVersion(m_config->pbftMsgDefaultVersion());
    newViewMsg->setView(toView);
    newViewMsg->setTimestamp(utcTime());
    newViewMsg->setGeneratedFrom(m_config->nodeIndex());
    // set viewchangeList
    newViewMsg->setViewChangeMsgList(viewChangeList);
    // set generated pre-prepare list
    auto generatedPrePrepareList = generatePrePrepareMsg(viewChangeCache);
    newViewMsg->setPrePrepareList(generatedPrePrepareList);
    // encode and broadcast the viewchangeReq
    auto encodedData = m_config->codec()->encode(newViewMsg);
    // only broadcast message to the consensus nodes
    task::wait(
        [](front::FrontServiceInterface::Ptr front, bytesPointer encodedData) -> task::Task<void> {
            co_await front->broadcastMessage(bcos::protocol::NodeType::CONSENSUS_NODE,
                ModuleID::PBFT, ::ranges::views::single(ref(*encodedData)));
        }(m_config->frontService(), std::move(encodedData)));
    m_newViewGenerated = true;
    PBFT_LOG(INFO) << LOG_DESC("The next leader broadcast NewView request")
                   << printPBFTMsgInfo(newViewMsg) << LOG_KV("Idx", m_config->nodeIndex());
    return newViewMsg;
}

ViewType PBFTCacheProcessor::tryToTriggerFastViewChange()
{
    uint64_t greaterViewWeight = 0;
    ViewType viewToReach = 0;
    bool findViewToReach = false;
    for (auto const& it : m_viewChangeCache)
    {
        auto view = it.first;
        if (view <= m_config->toView())
        {
            continue;
        }
        if (viewToReach > view || (viewToReach == 0))
        {
            // check the quorum
            auto viewChangeCache = it.second;
            greaterViewWeight = 0;
            for (auto const& cache : viewChangeCache)
            {
                auto fromIdx = cache.first;
                auto* nodeInfo = m_config->getConsensusNodeByIndex(fromIdx);
                if (!nodeInfo)
                {
                    continue;
                }
                greaterViewWeight += nodeInfo->voteWeight;
            }
            // must ensure at least (f+1) nodes at the same view can trigger fast-viewchange
            if (greaterViewWeight >= (m_config->maxFaultyQuorum() + 1))
            {
                findViewToReach = true;
                viewToReach = view;
            }
        }
    }
    if (!findViewToReach)
    {
        return 0;
    }
    if (m_config->toView() >= viewToReach)
    {
        return 0;
    }
    if (viewToReach > 0)
    {
        // set toView to (viewToReach - 1) and then trigger timeout to increase toView to
        // viewToReach
        m_config->setToView(viewToReach - 1);
    }
    PBFT_LOG(INFO) << LOG_DESC("tryToTriggerFastViewChange") << LOG_KV("viewToReach", viewToReach)
                   << m_config->printCurrentState();
    return viewToReach;
}

bool PBFTCacheProcessor::checkPrecommitMsg(PBFTMessageInterface::Ptr _precommitMsg)
{
    // check the view(the first started node no need to check the view)
    if (m_config->startRecovered() && (_precommitMsg->view() > m_config->toView()))
    {
        return false;
    }
    if (!_precommitMsg->consensusProposal())
    {
        return false;
    }
    auto ret = checkPrecommitWeight(_precommitMsg);
    if (ret)
    {
        return ret;
    }
    // avoid the failure to verify proposalWeight due to the modification of consensus node list and
    // consensus weight
    if (!m_caches.contains(_precommitMsg->index()))
    {
        return ret;
    }
    auto precommit = (m_caches.at(_precommitMsg->index()))->preCommitCache();
    if (!precommit)
    {
        return ret;
    }
    // erase the cache
    if (precommit->hash() == _precommitMsg->hash() && !checkPrecommitWeight(precommit))
    {
        m_caches.erase(precommit->index());
    }
    return ret;
}

bool PBFTCacheProcessor::checkPrecommitWeight(PBFTMessageInterface::Ptr _precommitMsg)
{
    auto precommitProposal = _precommitMsg->consensusProposal();
    // check the signature
    uint64_t weight = 0;
    auto proofSize = precommitProposal->signatureProofSize();
    for (size_t i = 0; i < proofSize; i++)
    {
        auto proof = precommitProposal->signatureProof(i);
        // check the proof
        auto* nodeInfo = m_config->getConsensusNodeByIndex(proof.first);
        if (!nodeInfo)
        {
            return false;
        }
        // verify the signature
        auto ret = m_config->cryptoSuite()->signatureImpl()->verify(
            nodeInfo->nodeID, precommitProposal->hash(), proof.second);
        if (!ret)
        {
            return false;
        }
        weight += nodeInfo->voteWeight;
    }
    // check the quorum
    return (weight >= m_config->minRequiredQuorum());
}

ViewChangeMsgInterface::Ptr PBFTCacheProcessor::fetchPrecommitData(
    BlockNumber _index, bcos::crypto::HashType const& _hash)
{
    if (!m_caches.contains(_index))
    {
        return nullptr;
    }
    auto cache = m_caches[_index];
    if (cache->preCommitCache() == nullptr || cache->preCommitCache()->hash() != _hash)
    {
        return nullptr;
    }

    PBFTMessageList precommitMessage;
    precommitMessage.push_back(cache->preCommitCache());

    auto pbftMessage = m_config->pbftMessageFactory()->createViewChangeMsg();
    pbftMessage->setPreparedProposals(precommitMessage);
    return pbftMessage;
}

void PBFTCacheProcessor::removeConsensusedCache(
    ViewType _view, bcos::protocol::BlockNumber _consensusedNumber)
{
    m_proposalsToStableConsensus.erase(_consensusedNumber);
    uint32_t eraseSize = 0;
    for (auto pcache = m_caches.begin(); pcache != m_caches.end();)
    {
        // Note: can't remove stabledCommitted cache here for need to fetch
        // lastAppliedProposalCheckPoint when apply the next proposal
        if (pcache->first <= _consensusedNumber)
        {
            m_proposalsToStableConsensus.erase(pcache->first);
            pcache->second->resetExceptionCache(_view);
            pcache = m_caches.erase(pcache);
            eraseSize++;
            continue;
        }
        pcache++;
    }
    removeInvalidViewChange(_view, _consensusedNumber);
    PBFT_LOG(INFO) << LOG_DESC("removeConsensusedCache finalizeConsensus") << LOG_KV("view", _view)
                   << LOG_KV("number", _consensusedNumber) << LOG_KV("eraseSize", eraseSize)
                   << LOG_KV("cacheSize", m_caches.size());
    m_newViewGenerated = false;
}


void PBFTCacheProcessor::resetCacheAfterViewChange(
    ViewType _view, bcos::protocol::BlockNumber _latestCommittedProposal)
{
    PBFT_LOG(INFO) << LOG_DESC("resetCacheAfterViewChange") << LOG_KV("view", _view)
                   << LOG_KV("number", _latestCommittedProposal)
                   << LOG_KV("cacheSize", m_caches.size());
    for (auto const& it : m_caches)
    {
        it.second->resetCache(_view);
    }
    m_maxPrecommitIndex.clear();
    m_maxCommittedIndex.clear();
    m_newViewGenerated = false;
    removeInvalidViewChange(_view, _latestCommittedProposal);
    removeInvalidRecoverCache(_view);
}

void PBFTCacheProcessor::removeInvalidRecoverCache(ViewType _view)
{
    for (auto it = m_recoverReqCache.begin(); it != m_recoverReqCache.end();)
    {
        auto view = it->first;
        if (view <= _view)
        {
            it = m_recoverReqCache.erase(it);
            m_recoverCacheWeight.erase(view);
            continue;
        }
        it++;
    }
}

void PBFTCacheProcessor::removeInvalidViewChange(
    ViewType _view, bcos::protocol::BlockNumber _latestCommittedProposal)
{
    for (auto it = m_viewChangeCache.begin(); it != m_viewChangeCache.end();)
    {
        auto view = it->first;
        if (view <= _view)
        {
            it = m_viewChangeCache.erase(it);
            m_viewChangeWeight.erase(view);
            continue;
        }
        // Note: must use reference here, in case of erase nothing
        auto& viewChangeCache = it->second;
        for (auto pcache = viewChangeCache.begin(); pcache != viewChangeCache.end();)
        {
            auto index = pcache->second->index();
            if (index < _latestCommittedProposal)
            {
                pcache = viewChangeCache.erase(pcache);
                continue;
            }
            pcache++;
        }
        it++;
    }
    reCalculateViewChangeWeight();
}

void PBFTCacheProcessor::reCalculateViewChangeWeight()
{
    m_maxPrecommitIndex.clear();
    m_maxCommittedIndex.clear();
    for (auto const& it : m_viewChangeCache)
    {
        auto view = it.first;
        m_viewChangeWeight[view] = 0;
        auto const& viewChangeCache = it.second;
        for (auto const& cache : viewChangeCache)
        {
            auto generatedFrom = cache.second->generatedFrom();
            auto* nodeInfo = m_config->getConsensusNodeByIndex(generatedFrom);
            if (!nodeInfo)
            {
                continue;
            }
            m_viewChangeWeight[view] += nodeInfo->voteWeight;
            auto viewChangeReq = cache.second;
            auto committedIndex = viewChangeReq->committedProposal()->index();
            if (!m_maxCommittedIndex.contains(view) || m_maxCommittedIndex[view] < committedIndex)
            {
                m_maxCommittedIndex[view] = committedIndex;
            }
            // get the max precommitIndex
            for (const auto& precommit : viewChangeReq->preparedProposals())
            {
                auto precommitIndex = precommit->index();
                if (!m_maxPrecommitIndex.contains(view) ||
                    m_maxPrecommitIndex[view] < precommitIndex)
                {
                    m_maxPrecommitIndex[view] = precommitIndex;
                }
            }
        }
    }
}

void PBFTCacheProcessor::checkAndCommitStableCheckPoint()
{
    std::vector<PBFTCache::Ptr> stabledCacheList;
    for (auto const& [_, cache] : m_caches)
    {
        auto ret = cache->checkAndCommitStableCheckPoint();
        if (!ret)
        {
            continue;
        }
        stabledCacheList.emplace_back(cache);
    }
    // Note: since updateStableCheckPointQueue may update m_caches after commitBlock
    // must call it after iterator m_caches
    for (const auto& cache : stabledCacheList)
    {
        updateStableCheckPointQueue(cache->checkPointProposal());
    }
}

void PBFTCacheProcessor::updateStableCheckPointQueue(PBFTProposalInterface::Ptr _stableCheckPoint)
{
    assert(_stableCheckPoint);
    m_stableCheckPointQueue.push(_stableCheckPoint);
    PBFT_LOG(INFO) << LOG_DESC("updateStableCheckPointQueue: insert new checkpoint proposal")
                   << LOG_KV("index", _stableCheckPoint->index())
                   << LOG_KV("hash", _stableCheckPoint->hash().abridged())
                   << m_config->printCurrentState();
    tryToCommitStableCheckPoint();
}

void PBFTCacheProcessor::tryToCommitStableCheckPoint()
{
    // remove the invalid checkpoint
    while (!m_stableCheckPointQueue.empty() &&
           m_stableCheckPointQueue.top()->index() <= m_config->committedProposal()->index())
    {
        PBFT_LOG(INFO) << LOG_DESC("updateStableCheckPointQueue: remove invalid checkpoint")
                       << LOG_KV("index", m_stableCheckPointQueue.top()->index())
                       << LOG_KV("committedIndex", m_config->committedProposal()->index());
        m_committedProposalList.erase(m_stableCheckPointQueue.top()->index());
        m_stableCheckPointQueue.pop();
    }
    // submit stable-checkpoint to ledger in ordeer
    if (!m_stableCheckPointQueue.empty() &&
        m_stableCheckPointQueue.top()->index() == m_config->committedProposal()->index() + 1)
    {
        PBFT_LOG(INFO) << LOG_DESC("updateStableCheckPointQueue: commit stable checkpoint")
                       << LOG_KV("index", m_stableCheckPointQueue.top()->index())
                       << LOG_KV("committedIndex", m_config->committedProposal()->index());
        auto stableCheckPoint = m_stableCheckPointQueue.top();
        m_committedProposalList.erase(stableCheckPoint->index());
        m_stableCheckPointQueue.pop();
        m_config->storage()->asyncCommitStableCheckPoint(stableCheckPoint);
    }
}

bool PBFTCacheProcessor::shouldRequestCheckPoint(PBFTMessageInterface::Ptr _checkPointMsg)
{
    auto checkPointIndex = _checkPointMsg->index();
    auto committedIndex = m_config->committedProposal()->index();
    // expired checkpoint
    if (checkPointIndex <= committedIndex || checkPointIndex <= m_config->syncingHighestNumber())
    {
        return false;
    }
    if (checkPointIndex > (committedIndex + m_config->waterMarkLimit()))
    {
        return false;
    }
    // hit in the local committedProposalList or already been requested
    if (m_committedProposalList.contains(checkPointIndex))
    {
        return false;
    }
    // the local cache already has the checkPointProposal
    if (m_caches.contains(checkPointIndex) && m_caches[checkPointIndex]->checkPointProposal())
    {
        return false;
    }
    // request the checkpoint proposal when timeout
    if (m_config->timeout())
    {
        return true;
    }
    // no-timeout
    // has not received any checkPoint message before, wait for generating local checkPoint
    if (!m_caches.contains(checkPointIndex))
    {
        return false;
    }
    auto cache = m_caches[checkPointIndex];
    // precommitted in the local cache, wait for generating local checkPoint
    if (cache->precommitted())
    {
        return false;
    }
    // receive at least (f+1) checkPoint proposal, wait for generating local checkPoint
    auto checkPointWeight = cache->getCollectedCheckPointWeight(_checkPointMsg->hash());
    auto minRequiredCheckPointWeight = m_config->maxFaultyQuorum() + 1;
    if (checkPointWeight < minRequiredCheckPointWeight)
    {
        return false;
    }
    // collect more than (f+1) checkpoint message with the same hash
    // in case of request again
    m_committedProposalList.insert(checkPointIndex);
    return true;
}

void PBFTCacheProcessor::eraseCommittedProposalList(bcos::protocol::BlockNumber _index)
{
    if (!m_committedProposalList.contains(_index))
    {
        return;
    }
    m_committedProposalList.erase(_index);
}

void PBFTCacheProcessor::clearExpiredExecutingProposal()
{
    auto committedIndex = m_config->committedProposal()->index();
    for (auto it = m_executingProposals.begin(); it != m_executingProposals.end();)
    {
        if (it->second > committedIndex)
        {
            it++;
            continue;
        }
        it = m_executingProposals.erase(it);
    }
}

void PBFTCacheProcessor::addRecoverReqCache(PBFTMessageInterface::Ptr _recoverResponse)
{
    auto fromIdx = _recoverResponse->generatedFrom();
    auto view = _recoverResponse->view();
    if (m_recoverReqCache.contains(view) && m_recoverReqCache[view].contains(fromIdx))
    {
        return;
    }
    m_recoverReqCache[view][fromIdx] = _recoverResponse;
    // update the weight
    auto* nodeInfo = m_config->getConsensusNodeByIndex(fromIdx);
    if (!nodeInfo)
    {
        return;
    }
    if (!m_recoverCacheWeight.contains(view))
    {
        m_recoverCacheWeight[view] = 0;
    }
    m_recoverCacheWeight[view] += nodeInfo->voteWeight;
    PBFT_LOG(INFO) << LOG_DESC("addRecoverReqCache") << LOG_KV("weight", m_recoverCacheWeight[view])
                   << printPBFTMsgInfo(_recoverResponse) << m_config->printCurrentState();
}

bool PBFTCacheProcessor::checkAndTryToRecover()
{
    ViewType recoveredView = 0;
    for (auto const& it : m_recoverCacheWeight)
    {
        auto view = it.first;
        // collect enough recover response with the same view
        if (it.second >= m_config->minRequiredQuorum() && recoveredView < view)
        {
            recoveredView = view;
        }
    }
    if (recoveredView == 0)
    {
        return false;
    }
    // the node has already been recovered
    if (!m_config->timeout())
    {
        return false;
    }
    m_config->resetNewViewState(recoveredView);
    resetCacheAfterViewChange(recoveredView, m_config->committedProposal()->index());
    // clear the recoverReqCache
    m_recoverReqCache.clear();
    m_recoverCacheWeight.clear();
    // try to preCommit/commit after no-timeout
    // Note: the checkAndPreCommit and checkAndCommit will trigger fast-view-change
    checkAndPreCommit();
    checkAndCommit();
    PBFT_LOG(INFO) << LOG_DESC("checkAndTryToRecoverView: reachNewView")
                   << LOG_KV("recoveredView", recoveredView) << m_config->printCurrentState();
    return true;
}

PBFTProposalInterface::Ptr PBFTCacheProcessor::fetchPrecommitProposal(
    bcos::protocol::BlockNumber _index)
{
    if (!m_caches.contains(_index))
    {
        return nullptr;
    }
    auto cache = m_caches[_index];
    if (cache->preCommitCache() == nullptr ||
        cache->preCommitCache()->consensusProposal() == nullptr)
    {
        return nullptr;
    }
    return cache->preCommitCache()->consensusProposal();
}

void PBFTCacheProcessor::updatePrecommit(PBFTProposalInterface::Ptr _proposal)
{
    auto pbftMessage = m_config->pbftMessageFactory()->createPBFTMsg();
    pbftMessage->setConsensusProposal(_proposal);
    pbftMessage->setIndex(_proposal->index());
    pbftMessage->setHash(_proposal->hash());
    addCache(
        m_caches, pbftMessage, [](PBFTCache::Ptr _pbftCache, PBFTMessageInterface::Ptr _precommit) {
            _pbftCache->setPrecommitCache(std::move(_precommit));
        });
}

PBFTMessageList PBFTCacheProcessor::preCommitCachesWithoutData()
{
    PBFTMessageList precommitCacheList;
    auto waitSealUntil = m_config->waitSealUntil();
    auto committedIndex = m_config->committedProposal()->index();
    for (auto it = m_caches.begin(); it != m_caches.end();)
    {
        auto precommitCache = it->second->preCommitWithoutData();
        if (precommitCache != nullptr)
        {
            // should not handle the proposal future than the system proposal
            if (waitSealUntil > committedIndex && precommitCache->index() > waitSealUntil)
            {
                it = m_caches.erase(it);
                continue;
            }
            precommitCacheList.push_back(precommitCache);
        }
        it++;
    }
    return precommitCacheList;
}

void PBFTCacheProcessor::resetUnCommittedCacheState(bcos::protocol::BlockNumber _number)
{
    PBFT_LOG(INFO) << LOG_DESC("resetUnCommittedCacheState") << LOG_KV("number", _number)
                   << m_config->printCurrentState();
    for (auto const& it : m_caches)
    {
        if (it.second->index() >= _number)
        {
            it.second->resetState();
        }
    }
    m_committedProposalList.clear();
    m_executingProposals.clear();
}