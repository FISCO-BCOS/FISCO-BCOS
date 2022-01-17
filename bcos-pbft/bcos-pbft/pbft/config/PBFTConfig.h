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
 * @file PBFTConfig.h
 * @author: yujiechen
 * @date 2021-04-12
 */
#pragma once
#include "bcos-pbft/core/ConsensusConfig.h"
#include "bcos-pbft/framework/StateMachineInterface.h"
#include "bcos-pbft/pbft/engine/PBFTTimer.h"
#include "bcos-pbft/pbft/engine/Validator.h"
#include "bcos-pbft/pbft/interfaces/PBFTCodecInterface.h"
#include "bcos-pbft/pbft/interfaces/PBFTMessageFactory.h"
#include "bcos-pbft/pbft/interfaces/PBFTStorage.h"
#include "bcos-pbft/pbft/utilities/Common.h"
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-framework/interfaces/front/FrontServiceInterface.h>
#include <bcos-framework/interfaces/sync/BlockSyncInterface.h>

namespace bcos
{
namespace consensus
{
class PBFTConfig : public ConsensusConfig, public std::enable_shared_from_this<PBFTConfig>
{
public:
    using Ptr = std::shared_ptr<PBFTConfig>;
    PBFTConfig(bcos::crypto::CryptoSuite::Ptr _cryptoSuite,
        bcos::crypto::KeyPairInterface::Ptr _keyPair,
        std::shared_ptr<PBFTMessageFactory> _pbftMessageFactory,
        std::shared_ptr<PBFTCodecInterface> _codec, std::shared_ptr<ValidatorInterface> _validator,
        std::shared_ptr<bcos::front::FrontServiceInterface> _frontService,
        StateMachineInterface::Ptr _stateMachine, PBFTStorage::Ptr _storage)
      : ConsensusConfig(_keyPair), m_connectedNodeList(std::make_shared<bcos::crypto::NodeIDSet>())
    {
        m_cryptoSuite = _cryptoSuite;
        m_pbftMessageFactory = _pbftMessageFactory;
        m_codec = _codec;
        m_validator = _validator;
        m_frontService = _frontService;
        m_stateMachine = _stateMachine;
        m_storage = _storage;
        m_timer = std::make_shared<PBFTTimer>(consensusTimeout());
    }

    ~PBFTConfig() override {}

    virtual void stop()
    {
        // stop the validator
        if (m_validator)
        {
            m_validator->stop();
        }
        // destroy the timer
        if (m_timer)
        {
            m_timer->destroy();
        }
    }
    virtual void resetConfig(
        bcos::ledger::LedgerConfig::Ptr _ledgerConfig, bool _syncedBlock = false);

    uint64_t minRequiredQuorum() const override;

    virtual ViewType view() const { return m_view; }
    virtual void setView(ViewType _view) { m_view.store(_view); }

    virtual ViewType toView() const { return m_toView; }
    virtual void setToView(ViewType _toView) { m_toView.store(_toView); }
    virtual void incToView(ViewType _increasedValue) { m_toView += _increasedValue; }

    virtual IndexType leaderIndex(bcos::protocol::BlockNumber _proposalIndex);
    virtual bool leaderAfterViewChange();
    IndexType leaderIndexInNewViewPeriod(ViewType _view);
    IndexType leaderIndexInNewViewPeriod(
        bcos::protocol::BlockNumber _proposalIndex, ViewType _view);
    virtual uint64_t leaderSwitchPeriod() const { return m_leaderSwitchPeriod; }
    virtual void setLeaderSwitchPeriod(uint64_t _leaderSwitchPeriod)
    {
        if (_leaderSwitchPeriod == m_leaderSwitchPeriod)
        {
            return;
        }
        m_leaderSwitchPeriod.store(_leaderSwitchPeriod);
        // notify the sealer module to reset sealing
        notifyResetSealing(sealStartIndex());
        PBFT_LOG(INFO) << LOG_DESC(
                              "updateLeaderSwitchPeriod and re-notify the sealer to seal block")
                       << LOG_KV("leader_period", m_leaderSwitchPeriod)
                       << LOG_KV("committedIndex", committedProposal()->index());
    }

    bcos::crypto::CryptoSuite::Ptr cryptoSuite() { return m_cryptoSuite; }
    std::shared_ptr<PBFTMessageFactory> pbftMessageFactory() { return m_pbftMessageFactory; }
    std::shared_ptr<bcos::front::FrontServiceInterface> frontService() { return m_frontService; }
    std::shared_ptr<PBFTCodecInterface> codec() { return m_codec; }

    PBFTProposalInterface::Ptr populateCommittedProposal();
    unsigned pbftMsgDefaultVersion() const { return c_pbftMsgDefaultVersion; }
    unsigned networkTimeoutInterval() const { return c_networkTimeoutInterval; }
    std::shared_ptr<ValidatorInterface> validator() { return m_validator; }
    PBFTStorage::Ptr storage() { return m_storage; }

    std::string printCurrentState();
    int64_t highWaterMark() { return m_progressedIndex + m_warterMarkLimit; }
    int64_t lowWaterMark() { return m_lowWaterMark; }
    void setLowWaterMark(bcos::protocol::BlockNumber _index) { m_lowWaterMark = _index; }

    PBFTTimer::Ptr timer() { return m_timer; }

    void setConsensusTimeout(uint64_t _consensusTimeout) override
    {
        ConsensusConfig::setConsensusTimeout(_consensusTimeout);
        m_timer->reset(_consensusTimeout);
    }

    void setCommittedProposal(ProposalInterface::Ptr _committedProposal) override
    {
        ConsensusConfig::setCommittedProposal(_committedProposal);
        auto progressedIndex = _committedProposal->index() + 1;
        if (progressedIndex > m_expectedCheckPoint)
        {
            PBFT_LOG(INFO) << LOG_DESC("PBFTConfig: resetExpectedCheckPoint") << printCurrentState()
                           << LOG_KV("expectedCheckPoint", m_expectedCheckPoint);
            m_expectedCheckPoint = _committedProposal->index() + 1;
        }
        if (progressedIndex > m_lowWaterMark)
        {
            setLowWaterMark(progressedIndex);
        }
    }

    int64_t expectedCheckPoint() { return m_expectedCheckPoint; }
    void setExpectedCheckPoint(bcos::protocol::BlockNumber _expectedCheckPoint)
    {
        m_expectedCheckPoint = std::max(committedProposal()->index() + 1, _expectedCheckPoint);
        PBFT_LOG(INFO) << LOG_DESC("PBFTConfig: setExpectedCheckPoint") << printCurrentState()
                       << LOG_KV("expectedCheckPoint", m_expectedCheckPoint);
    }

    StateMachineInterface::Ptr stateMachine() { return m_stateMachine; }

    int64_t warterMarkLimit() const { return m_warterMarkLimit; }
    void setWarterMarkLimit(int64_t _warterMarkLimit) { m_warterMarkLimit = _warterMarkLimit; }

    int64_t checkPointTimeoutInterval() const { return m_checkPointTimeoutInterval; }
    void setCheckPointTimeoutInterval(int64_t _timeoutInterval)
    {
        m_checkPointTimeoutInterval = _timeoutInterval;
    }

    void resetToView()
    {
        m_toView.store(m_view);
        m_timer->resetChangeCycle();
    }

    uint64_t maxFaultyQuorum() const { return m_maxFaultyQuorum; }

    virtual void notifySealer(bcos::protocol::BlockNumber _progressedIndex, bool _enforce = false);
    virtual void reNotifySealer(bcos::protocol::BlockNumber _index);
    virtual bool shouldResetConfig(bcos::protocol::BlockNumber _index)
    {
        ReadGuard l(x_committedProposal);
        if (!m_committedProposal)
        {
            return false;
        }
        return m_committedProposal->index() < _index;
    }

    virtual void setTimeoutState(bool _timeoutState) { m_timeoutState = _timeoutState; }
    virtual bool timeout() { return m_timeoutState; }

    virtual void resetTimeoutState(bool _incTimeout = true)
    {
        m_timeoutState.store(true);
        // update toView
        incToView(1);
        if (_incTimeout)
        {
            // increase the changeCycle
            timer()->incChangeCycle(1);
        }
        // start the timer again(the timer here must be restarted)
        timer()->restart();
    }

    virtual void resetNewViewState(ViewType _view)
    {
        if (m_view > _view)
        {
            return;
        }
        if (m_startRecovered.load() == false)
        {
            m_startRecovered.store(true);
        }
        // reset the timer when reach a new-view
        resetTimer();
        // update the changeCycle
        timer()->resetChangeCycle();
        setView(_view);
        setToView(_view);
        m_timeoutState.store(false);
    }
    virtual void setUnSealedTxsSize(size_t _unsealedTxsSize)
    {
        m_unsealedTxsSize = _unsealedTxsSize;
        if (m_unsealedTxsSize > 0 && !m_timer->running())
        {
            m_timer->start();
        }
    }

    virtual void resetTimer()
    {
        // reset the timeout state to false
        m_timeoutState.store(false);
        freshTimer();
    }

    virtual void freshTimer()
    {
        if (m_unsealedTxsSize > 0)
        {
            m_timer->restart();
        }
        else
        {
            m_timer->stop();
        }
    }

    void registerSealProposalNotifier(
        std::function<void(size_t, size_t, size_t, std::function<void(Error::Ptr)>)>
            _sealProposalNotifier)
    {
        m_sealProposalNotifier = _sealProposalNotifier;
    }

    void registerStateNotifier(std::function<void(bcos::protocol::BlockNumber)> _stateNotifier)
    {
        m_stateNotifier = _stateNotifier;
    }

    void registerNewBlockNotifier(
        std::function<void(bcos::ledger::LedgerConfig::Ptr, std::function<void(Error::Ptr)>)>
            _newBlockNotifier)
    {
        m_newBlockNotifier = _newBlockNotifier;
    }

    void registerSealerResetNotifier(
        std::function<void(std::function<void(Error::Ptr)>)> _sealerResetNotifier)
    {
        m_sealerResetNotifier = _sealerResetNotifier;
    }

    void registerFaultyDiscriminator(
        std::function<bool(bcos::crypto::NodeIDPtr)> _faultyDiscriminator)
    {
        m_faultyDiscriminator = _faultyDiscriminator;
    }

    virtual void notifyResetSealing(bcos::protocol::BlockNumber _consIndex)
    {
        notifyResetSealing([this, _consIndex]() {
            // notify the sealer to reseal
            reNotifySealer(_consIndex);
        });
    }

    virtual void notifyResetSealing(std::function<void()> _callback = nullptr);

    virtual void setWaitResealUntil(bcos::protocol::BlockNumber _waitResealUntil)
    {
        m_waitResealUntil = _waitResealUntil;
    }

    virtual void setWaitSealUntil(bcos::protocol::BlockNumber _waitSealUntil)
    {
        m_waitSealUntil = std::max(m_waitSealUntil.load(), _waitSealUntil);
    }

    void setConsensusNodeList(ConsensusNodeList& _consensusNodeList) override
    {
        ConsensusConfig::setConsensusNodeList(_consensusNodeList);
        if (!m_nodeUpdated)
        {
            return;
        }
        if (committedProposal())
        {
            notifyResetSealing(sealStartIndex());
        }
    }

    bcos::protocol::BlockNumber sealStartIndex()
    {
        auto sealStartIndex = expectedCheckPoint();
        if (committedProposal())
        {
            sealStartIndex = std::max(sealStartIndex, committedProposal()->index() + 1);
        }
        return sealStartIndex;
    }

    bool canHandleNewProposal();
    bool canHandleNewProposal(PBFTBaseMessageInterface::Ptr _msg);

    void registerFastViewChangeHandler(std::function<void()> _fastViewChangeHandler)
    {
        m_fastViewChangeHandler = _fastViewChangeHandler;
    }

    virtual void setConnectedNodeList(bcos::crypto::NodeIDSet&& _connectedNodeList)
    {
        WriteGuard l(x_connectedNodeList);
        *m_connectedNodeList = std::move(_connectedNodeList);
    }
    virtual void setConnectedNodeList(bcos::crypto::NodeIDSet const& _connectedNodeList)
    {
        WriteGuard l(x_connectedNodeList);
        *m_connectedNodeList = _connectedNodeList;
    }

    virtual bcos::crypto::NodeIDSet connectedNodeList()
    {
        ReadGuard l(x_connectedNodeList);
        return *m_connectedNodeList;
    }

    IndexType getLeader() { return leaderIndex(sealStartIndex()); }

    virtual bool tryTriggerFastViewChange(IndexType _leaderIndex);

    virtual bool startRecovered() const { return m_startRecovered; }

protected:
    void updateQuorum() override;
    virtual void asyncNotifySealProposal(size_t _proposalIndex, size_t _proposalEndIndex,
        size_t _maxTxsToSeal, size_t _retryTime = 0);


protected:
    bcos::crypto::CryptoSuite::Ptr m_cryptoSuite;
    // Factory for creating PBFT message package
    std::shared_ptr<PBFTMessageFactory> m_pbftMessageFactory;
    // Codec for serialization/deserialization of PBFT message packets
    std::shared_ptr<PBFTCodecInterface> m_codec;
    // Proposal validator
    std::shared_ptr<ValidatorInterface> m_validator;
    // FrontService, used to send/receive P2P message packages
    std::shared_ptr<bcos::front::FrontServiceInterface> m_frontService;
    StateMachineInterface::Ptr m_stateMachine;
    PBFTStorage::Ptr m_storage;
    // Timer
    PBFTTimer::Ptr m_timer;
    // notify the sealer seal Proposal
    std::function<void(size_t, size_t, size_t, std::function<void(Error::Ptr)>)>
        m_sealProposalNotifier;
    // notify the sealer to reset sealing
    std::function<void(std::function<void(Error::Ptr)>)> m_sealerResetNotifier;

    // notify the sealer the latest blockNumber
    std::function<void(bcos::protocol::BlockNumber)> m_stateNotifier;
    // the sync module notify the consensus module the new block
    std::function<void(bcos::ledger::LedgerConfig::Ptr, std::function<void(Error::Ptr)>)>
        m_newBlockNotifier;
    std::atomic<uint64_t> m_maxFaultyQuorum = {0};
    std::atomic<uint64_t> m_totalQuorum = {0};
    std::atomic<uint64_t> m_minRequiredQuorum = {0};
    std::atomic<ViewType> m_view = {0};
    std::atomic<ViewType> m_toView = {0};

    std::atomic<bcos::protocol::BlockNumber> m_expectedCheckPoint = {0};
    std::atomic<bcos::protocol::BlockNumber> m_lowWaterMark = {0};

    std::atomic<bcos::protocol::BlockNumber> m_sealStartIndex = {0};
    std::atomic<bcos::protocol::BlockNumber> m_sealEndIndex = {0};

    int64_t m_warterMarkLimit = 10;
    std::atomic<int64_t> m_checkPointTimeoutInterval = {3000};

    std::atomic<uint64_t> m_leaderSwitchPeriod = {1};
    const unsigned c_pbftMsgDefaultVersion = 0;
    const unsigned c_networkTimeoutInterval = 1000;
    // state variable that identifies whether has timed out
    std::atomic_bool m_timeoutState = {false};

    std::atomic<size_t> m_unsealedTxsSize = {0};
    // notify the sealer to reseal new block until m_waitResealUntil stable committed
    std::atomic<bcos::protocol::BlockNumber> m_waitResealUntil = {0};
    // notify the ealer to seal new block until m_waitSealUntil committed
    std::atomic<bcos::protocol::BlockNumber> m_waitSealUntil = {0};

    bcos::crypto::NodeIDSetPtr m_connectedNodeList;
    SharedMutex x_connectedNodeList;

    std::function<void()> m_fastViewChangeHandler;

    std::atomic_bool m_startRecovered = {false};

    std::function<bool(bcos::crypto::NodeIDPtr)> m_faultyDiscriminator;
};
}  // namespace consensus
}  // namespace bcos
