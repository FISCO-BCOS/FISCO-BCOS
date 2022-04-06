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
 * @file PBFTCache.h
 * @author: yujiechen
 * @date 2021-04-23
 */
#pragma once
#include "../config/PBFTConfig.h"
#include "../interfaces/PBFTMessageInterface.h"

namespace bcos
{
namespace consensus
{
class PBFTCache : public std::enable_shared_from_this<PBFTCache>
{
public:
    using Ptr = std::shared_ptr<PBFTCache>;
    PBFTCache(PBFTConfig::Ptr _config, bcos::protocol::BlockNumber _index);
    virtual ~PBFTCache() {}
    bool existPrePrepare(PBFTMessageInterface::Ptr _prePrepareMsg);
    bool conflictWithProcessedReq(PBFTMessageInterface::Ptr _msg);
    bool conflictWithPrecommitReq(PBFTMessageInterface::Ptr _prePrepareMsg);

    virtual void addPrepareCache(PBFTMessageInterface::Ptr _prepareProposal)
    {
        addCache(m_prepareCacheList, m_prepareReqWeight, _prepareProposal);
        PBFT_LOG(INFO) << LOG_DESC("addPrepareCache") << printPBFTMsgInfo(_prepareProposal)
                       << m_config->printCurrentState()
                       << LOG_KV("weight", m_prepareReqWeight[_prepareProposal->hash()]);
    }

    virtual void addCommitCache(PBFTMessageInterface::Ptr _commitProposal)
    {
        addCache(m_commitCacheList, m_commitReqWeight, _commitProposal);
        PBFT_LOG(INFO) << LOG_DESC("addCommitCache") << printPBFTMsgInfo(_commitProposal)
                       << m_config->printCurrentState()
                       << LOG_KV("weight", m_commitReqWeight[_commitProposal->hash()]);
    }

    virtual void addPrePrepareCache(PBFTMessageInterface::Ptr _prePrepareMsg)
    {
        if (m_stableCommitted)
        {
            return;
        }
        if (m_checkpointProposal && m_prePrepare &&
            _prePrepareMsg->consensusProposal()->hash() !=
                m_prePrepare->consensusProposal()->hash())
        {
            return;
        }
        m_prePrepare = _prePrepareMsg;
        if (_prePrepareMsg->consStartTime() > 0)
        {
            m_consStartTime = _prePrepareMsg->consStartTime();
        }
        PBFT_LOG(INFO) << LOG_DESC("addPrePrepareCache") << printPBFTMsgInfo(_prePrepareMsg)
                       << LOG_KV("sys", _prePrepareMsg->consensusProposal()->systemProposal())
                       << m_config->printCurrentState();
    }

    bcos::protocol::BlockNumber index() const { return m_index; }

    virtual PBFTMessageInterface::Ptr preCommitCache() { return m_precommit; }
    virtual PBFTMessageInterface::Ptr preCommitWithoutData() { return m_precommitWithoutData; }
    bool resetPrecommitCache(PBFTMessageInterface::Ptr _precommit, bool _needReExec)
    {
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
        m_prePrepare = _precommit;
        intoPrecommit();
        m_precommit->consensusProposal()->setReExecFlag(_needReExec);
        if (_needReExec)
        {
            m_reExecHandler(m_precommit->consensusProposal());
        }
        m_resetted = true;
        return true;
    }
    virtual bool checkAndPreCommit();
    virtual bool checkAndCommit();
    virtual bool shouldStopTimer();
    // reset the cache after viewchange
    virtual void resetCache(ViewType _curView);

    virtual void setCheckPointProposal(PBFTProposalInterface::Ptr _proposal);
    PBFTProposalInterface::Ptr checkPointProposal() const { return m_checkpointProposal; }

    virtual void addCheckPointMsg(PBFTMessageInterface::Ptr _checkPointMsg)
    {
        addCache(m_checkpointCacheList, m_checkpointCacheWeight, _checkPointMsg);
        PBFT_LOG(INFO) << LOG_DESC("addCheckPointMsg") << printPBFTMsgInfo(_checkPointMsg)
                       << LOG_KV("Idx", m_config->nodeIndex())
                       << LOG_KV("weight", m_checkpointCacheWeight[_checkPointMsg->hash()])
                       << LOG_KV("minRequiredWeight", m_config->minRequiredQuorum());
    }

    virtual bool checkAndCommitStableCheckPoint();
    virtual void onCheckPointTimeout();
    bool stableCommitted() const { return m_stableCommitted; }
    bool precommitted() const { return m_precommitted; }

    void registerCommittedIndexNotify(
        std::function<void(bcos::protocol::BlockNumber)> _committedIndexNotifier)
    {
        m_committedIndexNotifier = _committedIndexNotifier;
    }

    void registerReExecProposal(std::function<void(PBFTProposalInterface::Ptr)> _reExecHandler)
    {
        m_reExecHandler = _reExecHandler;
    }
    uint64_t getCollectedCheckPointWeight(bcos::crypto::HashType const& _hash)
    {
        if (m_checkpointCacheWeight.count(_hash))
        {
            return m_checkpointCacheWeight[_hash];
        }
        return 0;
    }
    void init();

protected:
    bool checkPrePrepareProposalStatus();
    using CollectionCacheType =
        std::map<bcos::crypto::HashType, std::map<IndexType, PBFTMessageInterface::Ptr>>;
    using QuorumRecoderType = std::map<bcos::crypto::HashType, uint64_t>;
    void addCache(CollectionCacheType& _cachedReq, QuorumRecoderType& _weightInfo,
        PBFTMessageInterface::Ptr _proposal);
    bool collectEnoughQuorum(bcos::crypto::HashType const& _hash, QuorumRecoderType& _weightInfo);

    bool collectEnoughPrepareReq();
    bool collectEnoughCommitReq();
    bool collectEnoughCheckpoint();
    virtual void intoPrecommit();
    virtual void setSignatureList(
        PBFTProposalInterface::Ptr _proposal, CollectionCacheType& _cache);

    template <typename T>
    void resetCacheAfterViewChange(T& _caches, ViewType _curView)
    {
        for (auto it = _caches.begin(); it != _caches.end();)
        {
            // Note: must use reference here, in case of erase nothing
            auto& cache = it->second;
            for (auto pcache = cache.begin(); pcache != cache.end();)
            {
                auto pbftMsg = pcache->second;
                if (pbftMsg->view() < _curView)
                {
                    pcache = cache.erase(pcache);
                    continue;
                }
                pcache++;
            }
            if (cache.size() == 0)
            {
                it = _caches.erase(it);
                continue;
            }
            it++;
        }
    }

    template <typename T>
    void recalculateQuorum(QuorumRecoderType& _quorum, T const& _caches)
    {
        _quorum.clear();
        for (auto const& it : _caches)
        {
            auto hash = it.first;
            if (!_quorum.count(hash))
            {
                _quorum[hash] = 0;
            }
            auto const& cache = it.second;
            for (auto pcache : cache)
            {
                auto generatedFrom = pcache.second->generatedFrom();
                auto nodeInfo = m_config->getConsensusNodeByIndex(generatedFrom);
                if (!nodeInfo)
                {
                    continue;
                }
                _quorum[hash] += nodeInfo->weight();
            }
        }
    }

    bool triggerNonDeterministic();
    void onReceiveExecResult(
        Error::Ptr _error, bcos::crypto::NodeIDPtr _nodeID, bytesConstRef _data);

protected:
    PBFTConfig::Ptr m_config;
    // avoid submitting the same committed proposal multiple times
    std::atomic_bool m_submitted = {false};
    // avoid submitting the same stable checkpoint multiple times
    std::atomic_bool m_stableCommitted = {false};
    std::atomic_bool m_precommitted = {false};
    std::atomic<bcos::protocol::BlockNumber> m_index;
    // prepareCacheList
    CollectionCacheType m_prepareCacheList;
    QuorumRecoderType m_prepareReqWeight;

    // commitCache
    CollectionCacheType m_commitCacheList;
    QuorumRecoderType m_commitReqWeight;

    PBFTMessageInterface::Ptr m_prePrepare = nullptr;
    PBFTMessageInterface::Ptr m_precommit = nullptr;
    PBFTMessageInterface::Ptr m_precommitWithoutData = nullptr;

    PBFTProposalInterface::Ptr m_checkpointProposal = nullptr;
    CollectionCacheType m_checkpointCacheList;
    QuorumRecoderType m_checkpointCacheWeight;

    PBFTTimer::Ptr m_timer;

    std::function<void(bcos::protocol::BlockNumber)> m_committedIndexNotifier;
    int64_t m_consStartTime = 0;
    std::map<bcos::crypto::PublicPtr, bool, bcos::crypto::KeyCompare>
        m_nodesWithInconsistentCheckPoint;

    std::function<void(PBFTProposalInterface::Ptr)> m_reExecHandler;
    bcos::protocol::Block::Ptr m_localBlock = nullptr;
    mutable RecursiveMutex m_mutex;

    bool m_resetted = false;
};
}  // namespace consensus
}  // namespace bcos
