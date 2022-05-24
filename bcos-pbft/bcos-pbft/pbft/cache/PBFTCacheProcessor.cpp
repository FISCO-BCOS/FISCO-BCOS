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
#include <bcos-framework/interfaces/protocol/CommonError.h>
#include <bcos-framework/interfaces/protocol/Protocol.h>
#include <boost/bind/bind.hpp>

using namespace bcos;
using namespace bcos::consensus;
using namespace bcos::protocol;
using namespace bcos::crypto;

void PBFTCacheProcessor::initState(PBFTProposalList const& _proposals, NodeIDPtr _fromNode)
{
    for (auto proposal : _proposals)
    {
        // the proposal has already been committed
        if (proposal->index() <= m_config->committedProposal()->index() ||
            m_proposalsToStableConsensus.count(proposal->index()))
        {
            continue;
        }
        // set the txs status to be sealed
        m_config->validator()->asyncResetTxsFlag(proposal->data(), true);
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
    auto self = std::weak_ptr<PBFTCacheProcessor>(shared_from_this());
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
                    return;
                }
                PBFT_LOG(INFO) << LOG_DESC("loadAndVerifyProposal success")
                               << LOG_KV("from", _fromNode->shortHex())
                               << printPBFTProposal(_proposal);
                cache->m_onLoadAndVerifyProposalSucc(_proposal);
            }
            catch (std::exception const& e)
            {
                PBFT_LOG(WARNING) << LOG_DESC("loadAndVerifyProposal exception")
                                  << printPBFTProposal(_proposal)
                                  << LOG_KV("error", boost::diagnostic_information(e));
            }
        });
}
// Note: please ensure the passed in _prePrepareMsg not be modified after addPrePrepareCache
void PBFTCacheProcessor::addPrePrepareCache(PBFTMessageInterface::Ptr _prePrepareMsg)
{
    addCache(m_caches, _prePrepareMsg,
        [](PBFTCache::Ptr _pbftCache, PBFTMessageInterface::Ptr proposal) {
            _pbftCache->addPrePrepareCache(proposal);
        });
    // notify the consensusing proposal index to the sync module
    notifyMaxProposalIndex(_prePrepareMsg->index());
}

void PBFTCacheProcessor::notifyMaxProposalIndex(bcos::protocol::BlockNumber _proposalIndex)
{
    // notify the consensusing proposal index to the sync module
    if (m_maxNotifyIndex < _proposalIndex)
    {
        m_maxNotifyIndex = _proposalIndex;
        notifyCommittedProposalIndex(m_maxNotifyIndex);
    }
}

bool PBFTCacheProcessor::existPrePrepare(PBFTMessageInterface::Ptr _prePrepareMsg)
{
    if (!m_caches.count(_prePrepareMsg->index()))
    {
        return false;
    }
    auto pbftCache = m_caches[_prePrepareMsg->index()];
    return pbftCache->existPrePrepare(_prePrepareMsg);
}

bool PBFTCacheProcessor::tryToFillProposal(PBFTMessageInterface::Ptr _prePrepareMsg)
{
    if (!m_caches.count(_prePrepareMsg->index()))
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

bool PBFTCacheProcessor::conflictWithProcessedReq(PBFTMessageInterface::Ptr _msg)
{
    if (!m_caches.count(_msg->index()))
    {
        return false;
    }
    auto pbftCache = m_caches[_msg->index()];
    return pbftCache->conflictWithProcessedReq(_msg);
}

bool PBFTCacheProcessor::conflictWithPrecommitReq(PBFTMessageInterface::Ptr _prePrepareMsg)
{
    if (!m_caches.count(_prePrepareMsg->index()))
    {
        return false;
    }
    auto pbftCache = m_caches[_prePrepareMsg->index()];
    return pbftCache->conflictWithPrecommitReq(_prePrepareMsg);
}

void PBFTCacheProcessor::addCache(
    PBFTCachesType& _pbftCache, PBFTMessageInterface::Ptr _pbftReq, UpdateCacheHandler _handler)

{
    auto index = _pbftReq->index();
    if (!(_pbftCache.count(index)))
    {
        _pbftCache[index] = m_cacheFactory->createPBFTCache(m_config, index,
            boost::bind(
                &PBFTCacheProcessor::notifyCommittedProposalIndex, this, boost::placeholders::_1));
    }
    _handler(_pbftCache[index], _pbftReq);
}

void PBFTCacheProcessor::checkAndPreCommit()
{
    for (auto const& it : m_caches)
    {
        auto ret = it.second->checkAndPreCommit();
        if (!ret)
        {
            continue;
        }
        updateCommitQueue(it.second->preCommitCache()->consensusProposal());
        // refresh the timer when commit success
        m_config->timer()->restart();
        m_config->resetToView();
    }
    resetTimer();
}

void PBFTCacheProcessor::checkAndCommit()
{
    for (auto const& it : m_caches)
    {
        auto ret = it.second->checkAndCommit();
        if (!ret)
        {
            continue;
        }
        updateCommitQueue(it.second->preCommitCache()->consensusProposal());
        // refresh the timer when commit success
        m_config->timer()->restart();
        m_config->resetToView();
    }
    resetTimer();
}

void PBFTCacheProcessor::resetTimer()
{
    for (auto const& it : m_caches)
    {
        if (!it.second->shouldStopTimer())
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
    if (m_executingProposals.count(_committedProposal->hash()))
    {
        return;
    }
    auto proposalIndex = _committedProposal->index();
    notifyMaxProposalIndex(proposalIndex);
    m_committedQueue.push(_committedProposal);
    m_committedProposalList.insert(proposalIndex);
    m_proposalsToStableConsensus.insert(proposalIndex);
    notifyToSealNextBlock();
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

    if (!(m_caches.count(_index)))
    {
        return nullptr;
    }
    return (m_caches[_index])->checkPointProposal();
}

bool PBFTCacheProcessor::tryToApplyCommitQueue()
{
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
        if (m_executingProposals.count(proposal->hash()))
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
    for (auto const& it : m_proposalsToStableConsensus)
    {
        if (lastIndex + 1 < it)
        {
            break;
        }
        lastIndex = it;
    }
    auto nextProposalIndex = lastIndex + 1;
    m_config->notifySealer(nextProposalIndex);
    PBFT_LOG(INFO) << LOG_DESC("notify to seal next proposal")
                   << LOG_KV("nextProposalIndex", nextProposalIndex);
}

// execute the proposal and broadcast checkpoint message
void PBFTCacheProcessor::applyStateMachine(
    ProposalInterface::ConstPtr _lastAppliedProposal, PBFTProposalInterface::Ptr _proposal)
{
    PBFT_LOG(INFO) << LOG_DESC("applyStateMachine") << LOG_KV("index", _proposal->index())
                   << LOG_KV("hash", _proposal->hash().abridged()) << m_config->printCurrentState();
    auto executedProposal = m_config->pbftMessageFactory()->createPBFTProposal();
    auto self = std::weak_ptr<PBFTCacheProcessor>(shared_from_this());
    auto startT = utcTime();
    m_config->stateMachine()->asyncApply(m_config->timer()->timeout(), _lastAppliedProposal,
        _proposal, executedProposal, [self, startT, _proposal, executedProposal](bool _ret) {
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
                                  << LOG_KV("error", boost::diagnostic_information(e));
            }
        });
}

void PBFTCacheProcessor::setCheckPointProposal(PBFTProposalInterface::Ptr _proposal)
{
    auto index = _proposal->index();
    if (!(m_caches.count(index)))
    {
        // Note: since cache is created and freed frequently, it should be safer to use weak_ptr in
        // the callback
        auto self = std::weak_ptr<PBFTCacheProcessor>(shared_from_this());
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
                                      << LOG_KV("errorInfo", boost::diagnostic_information(e));
                }
            });
    }
    (m_caches[index])->setCheckPointProposal(_proposal);
}

void PBFTCacheProcessor::addCheckPointMsg(PBFTMessageInterface::Ptr _checkPointMsg)
{
    addCache(m_caches, _checkPointMsg,
        [](PBFTCache::Ptr _pbftCache, PBFTMessageInterface::Ptr _checkPointMsg) {
            _pbftCache->addCheckPointMsg(_checkPointMsg);
        });
}

void PBFTCacheProcessor::addViewChangeReq(ViewChangeMsgInterface::Ptr _viewChange)
{
    auto reqView = _viewChange->view();
    auto fromIdx = _viewChange->generatedFrom();
    if (m_viewChangeCache.count(reqView) && m_viewChangeCache[reqView].count(fromIdx))
    {
        return;
    }

    auto nodeInfo = m_config->getConsensusNodeByIndex(fromIdx);
    if (!nodeInfo)
    {
        return;
    }
    m_viewChangeCache[reqView][fromIdx] = _viewChange;
    if (!m_viewChangeWeight.count(reqView))
    {
        m_viewChangeWeight[reqView] = 0;
    }
    m_viewChangeWeight[reqView] += nodeInfo->weight();
    auto committedIndex = _viewChange->committedProposal()->index();
    if (!m_maxCommittedIndex.count(reqView) || m_maxCommittedIndex[reqView] < committedIndex)
    {
        m_maxCommittedIndex[reqView] = committedIndex;
    }
    // get the max precommitIndex
    for (auto precommit : _viewChange->preparedProposals())
    {
        auto precommitIndex = precommit->index();
        if (!m_maxPrecommitIndex.count(reqView) || m_maxPrecommitIndex[reqView] < precommitIndex)
        {
            m_maxPrecommitIndex[reqView] = precommitIndex;
        }
    }
    // print the prepared proposal info
    std::stringstream preparedProposalInfo;
    preparedProposalInfo << "preparedProposalInfo: ";
    for (auto proposal : _viewChange->preparedProposals())
    {
        preparedProposalInfo << LOG_KV("propIndex", proposal->index())
                             << LOG_KV("propHash", proposal->hash().abridged())
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
    if (m_maxCommittedIndex.count(toView))
    {
        maxCommittedIndex = m_maxCommittedIndex[toView];
    }
    auto maxPrecommitIndex = m_config->progressedIndex();
    if (m_maxPrecommitIndex.count(toView))
    {
        maxPrecommitIndex = m_maxPrecommitIndex[toView];
    }
    // should not handle the proposal future than the system proposal
    if (m_config->waitSealUntil() > committedIndex)
    {
        maxPrecommitIndex = std::min(m_config->waitSealUntil(), maxPrecommitIndex);
    }
    std::map<BlockNumber, PBFTMessageInterface::Ptr> preparedProposals;
    for (auto it : _viewChangeCache)
    {
        auto viewChangeReq = it.second;
        for (auto proposal : viewChangeReq->preparedProposals())
        {
            // invalid precommit proposal
            if (proposal->index() < maxCommittedIndex)
            {
                continue;
            }
            // repeated precommit proposal
            if (preparedProposals.count(proposal->index()))
            {
                auto precommitProposal = preparedProposals[proposal->index()];
                if (precommitProposal->index() != proposal->index() ||
                    precommitProposal->hash() != proposal->hash())
                {
                    // fatal case: two proposals in the same view with different hash
                    if (precommitProposal->view() == proposal->view())
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
            // new precommit proposla
            preparedProposals[proposal->index()] = proposal;
            proposal->setGeneratedFrom(viewChangeReq->generatedFrom());
        }
    }
    // generate prepareMsg from maxCommittedIndex to  maxPrecommitIndex
    PBFTMessageList prePrepareMsgList;
    for (auto i = (maxCommittedIndex + 1); i <= maxPrecommitIndex; i++)
    {
        PBFTProposalInterface::Ptr prePrepareProposal = nullptr;
        auto generatedFrom = m_config->nodeIndex();
        bool empty = false;
        if (preparedProposals.count(i))
        {
            prePrepareProposal = preparedProposals[i]->consensusProposal();
            generatedFrom = preparedProposals[i]->generatedFrom();
        }
        else
        {
            // empty prePrepare
            prePrepareProposal = m_config->validator()->generateEmptyProposal(
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
    if (!m_viewChangeWeight.count(toView))
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
    m_config->frontService()->asyncSendBroadcastMessage(
        bcos::protocol::NodeType::CONSENSUS_NODE, ModuleID::PBFT, ref(*encodedData));
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
                auto nodeInfo = m_config->getConsensusNodeByIndex(fromIdx);
                if (!nodeInfo)
                {
                    continue;
                }
                greaterViewWeight += nodeInfo->weight();
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
    if (m_config->startRecovered() && (_precommitMsg->view() > m_config->view()))
    {
        return false;
    }
    if (!_precommitMsg->consensusProposal())
    {
        return false;
    }
    auto ret = checkPrecommitWeight(_precommitMsg);
    if (ret == true)
    {
        return ret;
    }
    // avoid the failure to verify proposalWeight due to the modification of consensus node list and
    // consensus weight
    if (!m_caches.count(_precommitMsg->index()))
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
        auto nodeInfo = m_config->getConsensusNodeByIndex(proof.first);
        if (!nodeInfo)
        {
            return false;
        }
        // verify the signature
        auto ret = m_config->cryptoSuite()->signatureImpl()->verify(
            nodeInfo->nodeID(), precommitProposal->hash(), proof.second);
        if (!ret)
        {
            return false;
        }
        weight += nodeInfo->weight();
    }
    // check the quorum
    return (weight >= m_config->minRequiredQuorum());
}

ViewChangeMsgInterface::Ptr PBFTCacheProcessor::fetchPrecommitData(
    BlockNumber _index, bcos::crypto::HashType const& _hash)
{
    if (!m_caches.count(_index))
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

void PBFTCacheProcessor::removeConsensusedCache(ViewType _view, BlockNumber _consensusedNumber)
{
    m_proposalsToStableConsensus.erase(_consensusedNumber);
    for (auto pcache = m_caches.begin(); pcache != m_caches.end();)
    {
        // Note: can't remove stabledCommitted cache here for need to fetch
        // lastAppliedProposalCheckPoint when apply the next proposal
        if (pcache->first <= _consensusedNumber)
        {
            pcache = m_caches.erase(pcache);
            continue;
        }
        pcache++;
    }
    removeInvalidViewChange(_view, _consensusedNumber);
    m_newViewGenerated = false;
}


void PBFTCacheProcessor::resetCacheAfterViewChange(
    ViewType _view, BlockNumber _latestCommittedProposal)
{
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
    ViewType _view, BlockNumber _latestCommittedProposal)
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
            auto nodeInfo = m_config->getConsensusNodeByIndex(generatedFrom);
            if (!nodeInfo)
            {
                continue;
            }
            m_viewChangeWeight[view] += nodeInfo->weight();
            auto viewChangeReq = cache.second;
            auto committedIndex = viewChangeReq->committedProposal()->index();
            if (!m_maxCommittedIndex.count(view) || m_maxCommittedIndex[view] < committedIndex)
            {
                m_maxCommittedIndex[view] = committedIndex;
            }
            // get the max precommitIndex
            for (auto precommit : viewChangeReq->preparedProposals())
            {
                auto precommitIndex = precommit->index();
                if (!m_maxPrecommitIndex.count(view) || m_maxPrecommitIndex[view] < precommitIndex)
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
    for (auto const& it : m_caches)
    {
        auto ret = it.second->checkAndCommitStableCheckPoint();
        if (!ret)
        {
            continue;
        }
        stabledCacheList.emplace_back(it.second);
    }
    // Note: since updateStableCheckPointQueue may update m_caches after commitBlock
    // must call it after iterator m_caches
    for (auto cache : stabledCacheList)
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
    // expired checkpoint
    if (checkPointIndex <= m_config->committedProposal()->index() ||
        checkPointIndex <= m_config->syncingHighestNumber())
    {
        return false;
    }
    // hit in the local committedProposalList or already been requested
    if (m_committedProposalList.count(checkPointIndex))
    {
        return false;
    }
    // the local cache already has the checkPointProposal
    if (m_caches.count(checkPointIndex) && m_caches[checkPointIndex]->checkPointProposal())
    {
        return false;
    }
    // request the checkpoint proposal when timeout
    if (m_config->timeout())
    {
        return true;
    }
    // no-timeout
    // has not receive any checkPoint message before, wait for generating local checkPoint
    if (!m_caches.count(checkPointIndex))
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
    if (!m_committedProposalList.count(_index))
    {
        return;
    }
    m_committedProposalList.erase(_index);
}

// Note: Since blockSync and consensus execute the same block at the same time, the hash obtained is
// different, which will cause the parentHash of the subsequent consensus block to be incorrect, so
// future proposals need to be cleared here
void PBFTCacheProcessor::removeFutureProposals()
{
    PBFT_LOG(INFO) << LOG_DESC("removeFutureProposals for receive the sync block")
                   << LOG_KV("committQueueSize", m_committedQueue.size())
                   << LOG_KV("stableCheckPointSize", m_stableCheckPointQueue.size())
                   << LOG_KV("executedBlock", m_config->expectedCheckPoint() - 1)
                   << m_config->printCurrentState();
    auto committedIndex = m_config->committedProposal()->index();
    m_config->setExpectedCheckPoint(committedIndex + 1);

    // clear the commitQueue
    while (!m_committedQueue.empty())
    {
        auto proposal = m_committedQueue.top();
        m_committedQueue.pop();
        // reset the sealed txs to be unsealed
        if (proposal->index() >= m_config->committedProposal()->index())
        {
            continue;
        }
        m_config->validator()->asyncResetTxsFlag(proposal->data(), false);
    }
    m_committedProposalList.clear();

    // clear stable checkpoint queue
    std::priority_queue<PBFTProposalInterface::Ptr, std::vector<PBFTProposalInterface::Ptr>,
        PBFTProposalCmp>
        stableCheckPointQueue;
    m_stableCheckPointQueue = stableCheckPointQueue;

    // remove the executed proposal
    for (auto pcache = m_caches.begin(); pcache != m_caches.end();)
    {
        auto cache = pcache->second;
        // remove the cache of the future proposal
        if (cache->index() >= committedIndex && cache->checkPointProposal())
        {
            auto precommitMsg = cache->preCommitCache();
            if (precommitMsg && precommitMsg->index() < m_config->committedProposal()->index() &&
                precommitMsg->consensusProposal())
            {
                m_config->validator()->asyncResetTxsFlag(
                    precommitMsg->consensusProposal()->data(), false);
            }
            auto executedProposalIndex = cache->checkPointProposal()->index();
            m_config->storage()->asyncRemoveStabledCheckPoint(executedProposalIndex);
            pcache = m_caches.erase(pcache);
            continue;
        }
        pcache++;
    }
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
    if (m_recoverReqCache.count(view) && m_recoverReqCache[view].count(fromIdx))
    {
        return;
    }
    m_recoverReqCache[view][fromIdx] = _recoverResponse;
    // update the weight
    auto nodeInfo = m_config->getConsensusNodeByIndex(fromIdx);
    if (!nodeInfo)
    {
        return;
    }
    if (!m_recoverCacheWeight.count(view))
    {
        m_recoverCacheWeight[view] = 0;
    }
    m_recoverCacheWeight[view] += nodeInfo->weight();
    PBFT_LOG(INFO) << LOG_DESC("addRecoverReqCache") << LOG_KV("weight", m_recoverCacheWeight[view])
                   << printPBFTMsgInfo(_recoverResponse) << m_config->printCurrentState();
    return;
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
    m_config->resetNewViewState(recoveredView);
    resetCacheAfterViewChange(recoveredView, m_config->committedProposal()->index());
    // clear the recoverReqCache
    m_recoverReqCache.clear();
    m_recoverCacheWeight.clear();
    // try to preCommit/commit after no-timeout
    checkAndPreCommit();
    checkAndCommit();
    PBFT_LOG(INFO) << LOG_DESC("checkAndTryToRecoverView: reachNewView")
                   << LOG_KV("recoveredView", recoveredView) << m_config->printCurrentState();
    return true;
}

PBFTProposalInterface::Ptr PBFTCacheProcessor::fetchPrecommitProposal(
    bcos::protocol::BlockNumber _index)
{
    if (!m_caches.count(_index))
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
            _pbftCache->setPrecommitCache(_precommit);
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