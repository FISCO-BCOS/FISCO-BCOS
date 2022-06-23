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
 * @file PBFTCacheProcessor.h
 * @author: yujiechen
 * @date 2021-04-21
 */
#pragma once
#include "../cache/PBFTCache.h"
#include "../config/PBFTConfig.h"
#include "../interfaces/PBFTMessageFactory.h"
#include "../interfaces/PBFTMessageInterface.h"
#include "../interfaces/ViewChangeMsgInterface.h"
#include "PBFTCacheFactory.h"
#include <queue>
namespace bcos
{
namespace consensus
{
struct PBFTProposalCmp
{
    bool operator()(PBFTProposalInterface::Ptr _first, PBFTProposalInterface::Ptr _second)
    {
        // increase order
        return _first->index() > _second->index();
    }
};

class PBFTCacheProcessor : public std::enable_shared_from_this<PBFTCacheProcessor>
{
public:
    using Ptr = std::shared_ptr<PBFTCacheProcessor>;
    PBFTCacheProcessor(PBFTCacheFactory::Ptr _cacheFactory, PBFTConfig::Ptr _config)
      : m_cacheFactory(_cacheFactory), m_config(_config)
    {}

    virtual ~PBFTCacheProcessor() {}
    virtual void initState(
        PBFTProposalList const& _committedProposals, bcos::crypto::NodeIDPtr _fromNode);

    virtual void addPrePrepareCache(PBFTMessageInterface::Ptr _prePrepareMsg);
    virtual bool existPrePrepare(PBFTMessageInterface::Ptr _prePrepareMsg);

    virtual bool tryToFillProposal(PBFTMessageInterface::Ptr _prePrepareMsg);

    virtual bool conflictWithProcessedReq(PBFTMessageInterface::Ptr _msg);
    virtual bool conflictWithPrecommitReq(PBFTMessageInterface::Ptr _prePrepareMsg);
    virtual void addPrepareCache(PBFTMessageInterface::Ptr _prepareReq)
    {
        addCache(m_caches, _prepareReq,
            [](PBFTCache::Ptr _pbftCache, PBFTMessageInterface::Ptr _prepareReq) {
                _pbftCache->addPrepareCache(_prepareReq);
            });
    }

    virtual void addCommitReq(PBFTMessageInterface::Ptr _commitReq)
    {
        addCache(m_caches, _commitReq,
            [](PBFTCache::Ptr _pbftCache, PBFTMessageInterface::Ptr _commitReq) {
                _pbftCache->addCommitCache(_commitReq);
            });
    }

    PBFTMessageList preCommitCachesWithData()
    {
        PBFTMessageList precommitCacheList;
        for (auto const& it : m_caches)
        {
            auto precommitCache = it.second->preCommitCache();
            if (precommitCache != nullptr)
            {
                precommitCacheList.push_back(precommitCache);
            }
        }
        return precommitCacheList;
    }

    PBFTMessageList preCommitCachesWithoutData();
    virtual void checkAndPreCommit();
    virtual void checkAndCommit();

    virtual void addViewChangeReq(ViewChangeMsgInterface::Ptr _viewChange);
    virtual NewViewMsgInterface::Ptr checkAndTryIntoNewView();
    virtual ViewType tryToTriggerFastViewChange();

    virtual ViewChangeMsgInterface::Ptr fetchPrecommitData(
        bcos::protocol::BlockNumber _index, bcos::crypto::HashType const& _hash);

    virtual PBFTProposalInterface::Ptr fetchPrecommitProposal(bcos::protocol::BlockNumber _index);
    virtual void updatePrecommit(PBFTProposalInterface::Ptr _proposal);

    virtual bool checkPrecommitMsg(PBFTMessageInterface::Ptr _precommitMsg);

    virtual void removeConsensusedCache(
        ViewType _view, bcos::protocol::BlockNumber _consensusedNumber);
    virtual void resetCacheAfterViewChange(
        ViewType _view, bcos::protocol::BlockNumber _latestCommittedProposal);
    virtual void removeInvalidViewChange(
        ViewType _view, bcos::protocol::BlockNumber _latestCommittedProposal);

    virtual void setCheckPointProposal(PBFTProposalInterface::Ptr _proposal);
    virtual void addCheckPointMsg(PBFTMessageInterface::Ptr _checkPointMsg);
    virtual void checkAndCommitStableCheckPoint();
    virtual void tryToCommitStableCheckPoint();

    virtual void resetTimer();

    virtual bool shouldRequestCheckPoint(PBFTMessageInterface::Ptr _checkPointMsg);

    virtual void registerProposalAppliedHandler(
        std::function<void(bool, PBFTProposalInterface::Ptr, PBFTProposalInterface::Ptr)> _callback)
    {
        m_proposalAppliedHandler = _callback;
    }

    void registerCommittedProposalNotifier(
        std::function<void(bcos::protocol::BlockNumber, std::function<void(Error::Ptr)>)>
            _committedProposalNotifier)
    {
        m_committedProposalNotifier = _committedProposalNotifier;
    }

    bool tryToPreApplyProposal(ProposalInterface::Ptr _proposal);
    bool tryToApplyCommitQueue();

    void removeFutureProposals();
    // notify the consensusing proposal index to the sync module
    void notifyCommittedProposalIndex(bcos::protocol::BlockNumber _index);

    virtual void updateCommitQueue(PBFTProposalInterface::Ptr _committedProposal);

    // TODO: ensure thread-safe here
    virtual void eraseCommittedProposalList(bcos::protocol::BlockNumber _index);

    virtual void eraseExecutedProposal(bcos::crypto::HashType const& _hash)
    {
        if (!m_executingProposals.count(_hash))
        {
            return;
        }
        m_executingProposals.erase(_hash);
    }

    virtual size_t executingProposalSize() { return m_executingProposals.size(); }
    virtual void clearExpiredExecutingProposal();
    virtual void registerOnLoadAndVerifyProposalSucc(
        std::function<void(PBFTProposalInterface::Ptr)> _onLoadAndVerifyProposalSucc)
    {
        m_onLoadAndVerifyProposalSucc = _onLoadAndVerifyProposalSucc;
    }

    virtual void addRecoverReqCache(PBFTMessageInterface::Ptr _recoverResponse);
    virtual bool checkAndTryToRecover();

    std::map<bcos::crypto::HashType, bcos::protocol::BlockNumber> const& executingProposals()
    {
        return m_executingProposals;
    }

    bool proposalCommitted(bcos::protocol::BlockNumber _index)
    {
        return m_committedProposalList.count(_index);
    }

    virtual uint64_t getViewChangeWeight(ViewType _view) { return m_viewChangeWeight.at(_view); }

    virtual void clearAllCache()
    {
        m_caches.clear();
        m_viewChangeCache.clear();
        std::priority_queue<PBFTProposalInterface::Ptr, std::vector<PBFTProposalInterface::Ptr>,
            PBFTProposalCmp>
            emptyQueue;
        m_committedQueue.swap(emptyQueue);
        m_executingProposals.clear();
        m_committedProposalList.clear();
        m_proposalsToStableConsensus.clear();

        std::priority_queue<PBFTProposalInterface::Ptr, std::vector<PBFTProposalInterface::Ptr>,
            PBFTProposalCmp>
            emptyStableCheckPointQueue;
        m_stableCheckPointQueue.swap(emptyStableCheckPointQueue);
        m_recoverReqCache.clear();
    }

    void resetUnCommittedCacheState(bcos::protocol::BlockNumber _number);

protected:
    virtual void loadAndVerifyProposal(bcos::crypto::NodeIDPtr _fromNode,
        PBFTProposalInterface::Ptr _proposal, size_t _retryTime = 0);

    virtual bool checkPrecommitWeight(PBFTMessageInterface::Ptr _precommitMsg);
    virtual void applyStateMachine(
        ProposalInterface::ConstPtr _lastAppliedProposal, PBFTProposalInterface::Ptr _proposal);
    virtual void updateStableCheckPointQueue(PBFTProposalInterface::Ptr _stableCheckPoint);

    virtual ProposalInterface::ConstPtr getAppliedCheckPointProposal(
        bcos::protocol::BlockNumber _index);

    virtual void notifyToSealNextBlock();

protected:
    using PBFTCachesType = std::map<bcos::protocol::BlockNumber, PBFTCache::Ptr>;
    using UpdateCacheHandler =
        std::function<void(PBFTCache::Ptr _pbftCache, PBFTMessageInterface::Ptr _pbftMessage)>;
    void addCache(PBFTCachesType& _pbftCache, PBFTMessageInterface::Ptr _pbftReq,
        UpdateCacheHandler _handler);

    PBFTMessageList generatePrePrepareMsg(
        std::map<IndexType, ViewChangeMsgInterface::Ptr> _viewChangeCache);
    void reCalculateViewChangeWeight();
    void removeInvalidRecoverCache(ViewType _view);
    void notifyMaxProposalIndex(bcos::protocol::BlockNumber _proposalIndex);

protected:
    PBFTCacheFactory::Ptr m_cacheFactory;
    PBFTConfig::Ptr m_config;
    PBFTCachesType m_caches;

    // viewchange caches
    using ViewChangeCacheType =
        std::map<ViewType, std::map<IndexType, ViewChangeMsgInterface::Ptr>>;
    ViewChangeCacheType m_viewChangeCache;
    std::map<ViewType, uint64_t> m_viewChangeWeight;
    // only needed for viewchange
    std::map<ViewType, int64_t> m_maxCommittedIndex;
    std::map<ViewType, int64_t> m_maxPrecommitIndex;

    std::atomic_bool m_newViewGenerated = {false};

    std::priority_queue<PBFTProposalInterface::Ptr, std::vector<PBFTProposalInterface::Ptr>,
        PBFTProposalCmp>
        m_committedQueue;
    std::map<bcos::crypto::HashType, bcos::protocol::BlockNumber> m_executingProposals;

    std::set<bcos::protocol::BlockNumber, std::less<bcos::protocol::BlockNumber>>
        m_committedProposalList;

    // ordered by the index
    std::set<bcos::protocol::BlockNumber, std::less<bcos::protocol::BlockNumber>>
        m_proposalsToStableConsensus;

    std::priority_queue<PBFTProposalInterface::Ptr, std::vector<PBFTProposalInterface::Ptr>,
        PBFTProposalCmp>
        m_stableCheckPointQueue;

    std::function<void(bool, PBFTProposalInterface::Ptr, PBFTProposalInterface::Ptr)>
        m_proposalAppliedHandler;
    std::function<void(bcos::protocol::BlockNumber, std::function<void(Error::Ptr)>)>
        m_committedProposalNotifier;
    std::function<void(PBFTProposalInterface::Ptr)> m_onLoadAndVerifyProposalSucc;

    // the recover message cache
    std::map<ViewType, std::map<IndexType, PBFTMessageInterface::Ptr>> m_recoverReqCache;
    std::map<ViewType, uint64_t> m_recoverCacheWeight;

    bcos::protocol::BlockNumber m_maxNotifyIndex = 0;
};
}  // namespace consensus
}  // namespace bcos