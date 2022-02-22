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
 * @brief config for PBFT
 * @file PBFTConfig.cpp
 * @author: yujiechen
 * @date 2021-04-12
 */
#include "PBFTConfig.h"

using namespace bcos;
using namespace bcos::consensus;
using namespace bcos::protocol;
using namespace bcos::ledger;

void PBFTConfig::resetConfig(LedgerConfig::Ptr _ledgerConfig, bool _syncedBlock)
{
    bcos::protocol::BlockNumber committedIndex = 0;
    if (m_committedProposal)
    {
        committedIndex = m_committedProposal->index();
    }
    if (_ledgerConfig->blockNumber() <= committedIndex && committedIndex > 0)
    {
        return;
    }
    PBFT_LOG(INFO) << LOG_DESC("resetConfig")
                   << LOG_KV("committedIndex", _ledgerConfig->blockNumber())
                   << LOG_KV("propHash", _ledgerConfig->hash().abridged())
                   << LOG_KV("blockCountLimit", _ledgerConfig->blockTxCountLimit())
                   << LOG_KV("leaderPeriod", _ledgerConfig->leaderSwitchPeriod())
                   << LOG_KV(
                          "consensusNodesSize", _ledgerConfig->mutableConsensusNodeList().size());
    // set committed proposal
    auto committedProposal = m_pbftMessageFactory->createPBFTProposal();
    committedProposal->setIndex(_ledgerConfig->blockNumber());
    committedProposal->setHash(_ledgerConfig->hash());
    setCommittedProposal(committedProposal);
    // set blockTxCountLimit
    setBlockTxCountLimit(_ledgerConfig->blockTxCountLimit());
    // set ConsensusNodeList
    auto& consensusList = _ledgerConfig->mutableConsensusNodeList();
    setConsensusNodeList(consensusList);
    // set leader_period
    setLeaderSwitchPeriod(_ledgerConfig->leaderSwitchPeriod());
    // reset the timer
    resetTimer();

    if (_ledgerConfig->sealerId() == -1)
    {
        PBFT_LOG(INFO) << LOG_DESC("^^^^^^^^Report") << printCurrentState();
    }
    else
    {
        PBFT_LOG(INFO) << LOG_DESC("^^^^^^^^Report") << LOG_KV("sealer", _ledgerConfig->sealerId())
                       << LOG_KV("txs", _ledgerConfig->txsSize()) << printCurrentState();
    }
    // notify the txpool validator to update the consensusNodeList.
    m_validator->updateValidatorConfig(consensusList, _ledgerConfig->observerNodeList());

    // notify the latest block number to the sealer
    if (m_stateNotifier)
    {
        m_stateNotifier(_ledgerConfig->blockNumber());
    }
    // notify the latest block to the sync module
    if (m_newBlockNotifier && !_syncedBlock)
    {
        m_newBlockNotifier(_ledgerConfig, [_ledgerConfig](Error::Ptr _error) {
            if (_error)
            {
                PBFT_LOG(WARNING) << LOG_DESC("asyncNotifyNewBlock to sync module failed")
                                  << LOG_KV("number", _ledgerConfig->blockNumber())
                                  << LOG_KV("hash", _ledgerConfig->hash().abridged())
                                  << LOG_KV("code", _error->errorCode())
                                  << LOG_KV("msg", _error->errorMessage());
            }
        });
    }

    // the node is syncing, reset the timeout state to false for view recovery
    if (m_syncingHighestNumber > _ledgerConfig->blockNumber())
    {
        m_timeoutState = true;
        m_syncingState = true;
        // notify resetSealing(the syncing node should not seal block)
        notifyResetSealing();
        return;
    }
    // after syncing finished, try to reach a new view after syncing completed
    if (m_syncingState)
    {
        m_syncingState = false;
        m_timer->start();
    }
    // try to notify the sealer module to seal proposals
    if (!m_timeoutState)
    {
        if (m_waitResealUntil == _ledgerConfig->blockNumber())
        {
            auto notifyBeginIndex = std::max(sealStartIndex(), m_waitResealUntil + 1);
            PBFT_LOG(INFO) << LOG_DESC("Reach reseal index")
                           << LOG_KV("notifyBeginIndex", notifyBeginIndex) << printCurrentState();
            reNotifySealer(notifyBeginIndex);
        }
        notifySealer(sealStartIndex());
    }
}

void PBFTConfig::notifyResetSealing(std::function<void()> _callback)
{
    if (!m_sealerResetNotifier)
    {
        return;
    }
    // only notify the non-leader to reset sealing
    PBFT_LOG(INFO) << LOG_DESC("notifyResetSealing") << printCurrentState();
    auto committedIndex = m_committedProposal->index();
    m_sealerResetNotifier([this, _callback, committedIndex](Error::Ptr _error) {
        if (_error)
        {
            PBFT_LOG(INFO) << LOG_DESC("notifyResetSealing failed")
                           << LOG_KV("code", _error->errorCode())
                           << LOG_KV("msg", _error->errorMessage()) << printCurrentState();
            return;
        }
        if (_callback && m_waitResealUntil <= committedIndex)
        {
            _callback();
        }
        PBFT_LOG(INFO) << LOG_DESC("notifyResetSealing success") << printCurrentState();
    });
    // reset the sealEndIndex and the sealStartIndex
    m_sealEndIndex = (sealStartIndex() - 1);
    m_sealStartIndex = (sealStartIndex() - 1);
}

void PBFTConfig::reNotifySealer(bcos::protocol::BlockNumber _index)
{
    if (_index >= highWaterMark() || _index < m_committedProposal->index())
    {
        PBFT_LOG(INFO) << LOG_DESC("reNotifySealer return for invalid expectedStart")
                       << LOG_KV("expectedStart", _index)
                       << LOG_KV("highWaterMark", highWaterMark()) << printCurrentState();
        return;
    }
    m_sealStartIndex = (_index - 1);
    m_sealEndIndex = (_index - 1);
    notifySealer(_index);
    PBFT_LOG(INFO) << LOG_DESC("reNotifySealer") << LOG_KV("expectedStart", _index)
                   << LOG_KV("highWaterMark", highWaterMark())
                   << LOG_KV("leader", leaderIndex(_index)) << printCurrentState();
}

bool PBFTConfig::canHandleNewProposal()
{
    bcos::protocol::BlockNumber committedIndex = 0;
    if (m_committedProposal)
    {
        committedIndex = m_committedProposal->index();
    }
    if (m_waitSealUntil > committedIndex)
    {
        return false;
    }
    if (m_waitResealUntil > committedIndex)
    {
        return false;
    }
    return true;
}

bool PBFTConfig::canHandleNewProposal(PBFTBaseMessageInterface::Ptr _msg)
{
    if (canHandleNewProposal())
    {
        return true;
    }
    auto committedIndex = m_committedProposal->index();
    if (_msg->index() <= committedIndex || _msg->index() <= m_waitSealUntil ||
        _msg->index() <= m_waitResealUntil)
    {
        return true;
    }
    return false;
}

bool PBFTConfig::tryTriggerFastViewChange(IndexType _leaderIndex)
{
    if (!m_fastViewChangeHandler)
    {
        return false;
    }
    auto nodeList = connectedNodeList();
    // empty connection
    if (nodeList.size() == 0)
    {
        return false;
    }
    // the non-consensus node should not trigger fast viewchange
    if (!isConsensusNode())
    {
        return false;
    }
    // the leader is the current node
    if (_leaderIndex == nodeIndex())
    {
        return false;
    }
    auto leaderNodeInfo = getConsensusNodeByIndex(_leaderIndex);
    if (!leaderNodeInfo)
    {
        return false;
    }
    // Note: must register m_faultyDiscriminator before start the PBFTEngine
    if (nodeList.count(leaderNodeInfo->nodeID()) &&
        !m_faultyDiscriminator(leaderNodeInfo->nodeID()))
    {
        return false;
    }
    PBFT_LOG(INFO) << LOG_DESC("tryTriggerFastViewChange for the leader disconnect")
                   << LOG_KV("leaderIndex", _leaderIndex)
                   << LOG_KV("leader", leaderNodeInfo->nodeID()->shortHex()) << printCurrentState();
    m_fastViewChangeHandler();
    // check the newLeader connection
    auto newLeader = leaderIndexInNewViewPeriod(m_toView);
    return tryTriggerFastViewChange(newLeader);
}

void PBFTConfig::notifySealer(BlockNumber _progressedIndex, bool _enforce)
{
    auto currentLeader = leaderIndex(_progressedIndex);
    if (currentLeader != nodeIndex())
    {
        return;
    }
    if (!canHandleNewProposal())
    {
        PBFT_LOG(INFO) << LOG_DESC(
            "Not notify the sealer to sealing for not reach waitResealUntil/waitToSeal limit");
        return;
    }

    if (_enforce)
    {
        asyncNotifySealProposal(_progressedIndex, _progressedIndex, blockTxCountLimit());
        m_sealEndIndex = std::max(_progressedIndex, m_sealEndIndex.load());
        m_sealStartIndex = std::min(_progressedIndex, m_sealStartIndex.load());
        PBFT_LOG(INFO) << LOG_DESC("notifySealer: enforce notify the leader to seal block")
                       << LOG_KV("idx", nodeIndex()) << LOG_KV("startIndex", _progressedIndex)
                       << LOG_KV("endIndex", _progressedIndex)
                       << LOG_KV("maxTxsToSeal", blockTxCountLimit()) << printCurrentState();
        return;
    }
    int64_t endProposalIndex =
        (_progressedIndex / m_leaderSwitchPeriod + 1) * m_leaderSwitchPeriod - 1;
    // Note: the valid proposal index range should be [max(lowWaterMark, committedIndex+1,
    // expectedCheckpoint), highWaterMark)
    endProposalIndex = std::min(endProposalIndex, (highWaterMark() - 1));
    if (m_sealEndIndex.load() >= endProposalIndex)
    {
        PBFT_LOG(INFO) << LOG_DESC("notifySealer return for invalid seal range")
                       << LOG_KV("currentEndIndex", m_sealEndIndex)
                       << LOG_KV("expectedEndIndex", endProposalIndex) << printCurrentState();
        return;
    }
    auto startSealIndex = std::max(m_sealEndIndex.load() + 1, _progressedIndex);
    if (startSealIndex > endProposalIndex)
    {
        PBFT_LOG(INFO) << LOG_DESC("notifySealer return for invalid seal range")
                       << LOG_KV("expectedStartIndex", startSealIndex)
                       << LOG_KV("expectedEndIndex", endProposalIndex) << printCurrentState();
        return;
    }
    auto committedIndex = m_committedProposal->index();
    if (m_validator->resettingProposalSize() > 0 && (startSealIndex > (committedIndex + 1)))
    {
        PBFT_LOG(INFO) << LOG_DESC(
                              "Not notify the sealer to sealing for txs of some proposals have not "
                              "been resetted success")
                       << LOG_KV("resettingProposalSize", m_validator->resettingProposalSize())
                       << LOG_KV("startSealIndex", startSealIndex) << printCurrentState();
        return;
    }
    asyncNotifySealProposal(startSealIndex, endProposalIndex, blockTxCountLimit());

    m_sealStartIndex = startSealIndex;
    m_sealEndIndex = endProposalIndex;
    PBFT_LOG(INFO) << LOG_DESC("notifySealer: notify the new leader to seal block")
                   << LOG_KV("idx", nodeIndex()) << LOG_KV("startIndex", startSealIndex)
                   << LOG_KV("endIndex", endProposalIndex)
                   << LOG_KV("notifyBeginIndex", _progressedIndex)
                   << LOG_KV("waitSealUntil", m_waitSealUntil)
                   << LOG_KV("waitResealUntil", m_waitResealUntil)
                   << LOG_KV("maxTxsToSeal", blockTxCountLimit()) << printCurrentState();
}

void PBFTConfig::asyncNotifySealProposal(
    size_t _proposalIndex, size_t _proposalEndIndex, size_t _maxTxsToSeal, size_t _retryTime)
{
    if (!m_sealProposalNotifier)
    {
        return;
    }
    if (_retryTime > 3)
    {
        return;
    }
    auto self = std::weak_ptr<PBFTConfig>(shared_from_this());
    m_sealProposalNotifier(_proposalIndex, _proposalEndIndex, _maxTxsToSeal,
        [_proposalIndex, _proposalEndIndex, _maxTxsToSeal, self, _retryTime](Error::Ptr _error) {
            if (_error == nullptr)
            {
                PBFT_LOG(INFO) << LOG_DESC("asyncNotifySealProposal success")
                               << LOG_KV("startIndex", _proposalIndex)
                               << LOG_KV("endIndex", _proposalEndIndex)
                               << LOG_KV("maxTxsToSeal", _maxTxsToSeal);
                return;
            }
            try
            {
                auto pbftConfig = self.lock();
                if (!pbftConfig)
                {
                    return;
                }
                // retry after 1 seconds
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                // retry when send failed
                pbftConfig->asyncNotifySealProposal(
                    _proposalIndex, _proposalEndIndex, _maxTxsToSeal, _retryTime + 1);
            }
            catch (std::exception const& e)
            {
                PBFT_LOG(WARNING) << LOG_DESC("asyncNotifySealProposal exception")
                                  << LOG_KV("error", boost::diagnostic_information(e));
            }
        });
}

uint64_t PBFTConfig::minRequiredQuorum() const
{
    return m_minRequiredQuorum;
}

void PBFTConfig::updateQuorum()
{
    m_totalQuorum.store(0);
    ReadGuard l(x_consensusNodeList);
    for (auto consensusNode : *m_consensusNodeList)
    {
        m_totalQuorum += consensusNode->weight();
    }
    // get m_maxFaultyQuorum
    m_maxFaultyQuorum = (m_totalQuorum - 1) / 3;
    m_minRequiredQuorum = m_totalQuorum - m_maxFaultyQuorum;
}

IndexType PBFTConfig::leaderIndex(BlockNumber _proposalIndex)
{
    return (_proposalIndex / m_leaderSwitchPeriod + m_view) % m_consensusNodeNum;
}

bool PBFTConfig::leaderAfterViewChange()
{
    auto expectedLeader = leaderIndexInNewViewPeriod(m_toView);
    return (m_nodeIndex == expectedLeader);
}

IndexType PBFTConfig::leaderIndexInNewViewPeriod(ViewType _view)
{
    return leaderIndexInNewViewPeriod(m_progressedIndex, _view);
}

IndexType PBFTConfig::leaderIndexInNewViewPeriod(
    bcos::protocol::BlockNumber _proposalIndex, ViewType _view)
{
    return (_proposalIndex / m_leaderSwitchPeriod + _view) % m_consensusNodeNum;
}

PBFTProposalInterface::Ptr PBFTConfig::populateCommittedProposal()
{
    ReadGuard l(x_committedProposal);
    if (!m_committedProposal)
    {
        return nullptr;
    }
    return m_pbftMessageFactory->populateFrom(
        std::dynamic_pointer_cast<PBFTProposalInterface>(m_committedProposal));
}

std::string PBFTConfig::printCurrentState()
{
    std::ostringstream stringstream;
    if (!committedProposal())
    {
        stringstream << LOG_DESC("The storage has not been init.");
        return stringstream.str();
    }
    stringstream << LOG_KV("committedIndex", committedProposal()->index())
                 << LOG_KV("consNum", progressedIndex())
                 << LOG_KV("committedHash", committedProposal()->hash().abridged())
                 << LOG_KV("view", view()) << LOG_KV("toView", toView())
                 << LOG_KV("changeCycle", m_timer->changeCycle())
                 << LOG_KV("expectedCheckPoint", m_expectedCheckPoint) << LOG_KV("Idx", nodeIndex())
                 << LOG_KV("unsealedTxs", m_unsealedTxsSize.load())
                 << LOG_KV("sealUntil", m_waitSealUntil)
                 << LOG_KV("waitResealUntil", m_waitResealUntil)
                 << LOG_KV("nodeId", nodeID()->shortHex());
    return stringstream.str();
}