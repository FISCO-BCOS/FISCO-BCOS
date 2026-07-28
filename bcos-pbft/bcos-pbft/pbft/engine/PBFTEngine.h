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
 * @brief implementation for PBFTEngine
 * @file PBFTEngine.h
 * @author: yujiechen
 * @date 2021-04-20
 */
#pragma once
#include "PBFTLogSync.h"
#include "bcos-framework/ledger/LedgerInterface.h"
#include "bcos-pbft/core/ConsensusEngine.h"
#include "bcos-pbft/pbft/utilities/PBFTPipeline.h"
#include <bcos-utilities/Error.h>
#include <bcos-utilities/IOServicePool.h>
#include <bcos-utilities/Timer.h>
#include <oneapi/tbb/concurrent_queue.h>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace bcos
{
namespace ledger
{
class LedgerConfig;
}
namespace consensus
{
class PBFTBaseMessageInterface;
class PBFTMessageInterface;
class ViewChangeMsgInterface;
class NewViewMsgInterface;
class PBFTConfig;
class PBFTCacheProcessor;
class PBFTProposalInterface;

enum CheckResult
{
    VALID = 0,
    INVALID = 1,
};

class PBFTEngine : public ConsensusEngine, public std::enable_shared_from_this<PBFTEngine>
{
public:
    using Ptr = std::shared_ptr<PBFTEngine>;
    using SendResponseCallback = std::function<void(bytesConstRef)>;
    explicit PBFTEngine(std::shared_ptr<PBFTConfig> _config, boost::asio::io_context& _ioContext,
        bcos::IOServicePool::Ptr _ioServicePool);
    ~PBFTEngine() override { stop(); }

    void start() override;
    void stop() override;

    virtual void asyncSubmitProposal(bool _containSysTxs, const protocol::Block& proposal,
        bcos::protocol::BlockNumber _proposalIndex, bcos::crypto::HashType const& _proposalHash,
        std::function<void(Error::Ptr)> _onProposalSubmitted);

    std::shared_ptr<PBFTConfig> pbftConfig() { return m_config; }

    // FIB-146 follow-up: exposed so the initializer can wire reset() to
    // sealer-set rotation. Not for use on the hot path — admit() / consumed()
    // are called via the engine itself.
    PBFTPipeline& pipeline() { return m_pipeline; }

    // Receive PBFT message package from frontService
    virtual void onReceivePBFTMessage(bcos::Error::Ptr _error, std::string const& _id,
        bcos::crypto::NodeIDPtr _nodeID, bytesConstRef _data);

    virtual void initState(PBFTProposalList const& _proposals, bcos::crypto::NodeIDPtr _fromNode)
    {
        m_cacheProcessor->initState(_proposals, std::move(_fromNode));
    }

    virtual void asyncNotifyNewBlock(
        bcos::ledger::LedgerConfig::Ptr _ledgerConfig, std::function<void(Error::Ptr)> _onRecv);

    virtual std::shared_ptr<PBFTCacheProcessor> cacheProcessor() { return m_cacheProcessor; }
    virtual bool isTimeout() { return m_config->timeout(); }
    void registerCommittedProposalNotifier(
        std::function<void(bcos::protocol::BlockNumber, std::function<void(Error::Ptr)>)>
            _committedProposalNotifier)
    {
        m_cacheProcessor->registerCommittedProposalNotifier(std::move(_committedProposalNotifier));
    }

    virtual void restart();

    virtual void clearExceptionProposalState(bcos::protocol::BlockNumber _number);

    void clearAllCache();
    // FIB-137: drain m_msgQueue. Called by PBFTImpl::enableAsMasterNode on demotion
    // (discard stale messages routed under a previous master role) and on promotion
    // before resuming processing (discard messages enqueued during demotion).
    void clearMsgQueue();
    void recoverState();

    void fetchAndUpdateLedgerConfig();
    bool shouldRotateSealers(protocol::BlockNumber _number) const;
    void setLedger(ledger::LedgerInterface::Ptr ledger);

protected:
    virtual void initSendResponseHandler();
    virtual void tryToResendCheckPoint();
    // FIB-185: watchdog that recovers a stalled consensus timer. If the node is a running
    // consensus node sitting in the timeout state but the view-change timer is not armed,
    // re-arm it. Only re-arms the timer (the actual view-change fires later from onTimeout),
    // so it cannot itself emit a view-change. Invoked from the periodic checkpoint timer path.
    virtual void checkConsensusTimerWatchdog();
    virtual void onReceivePBFTMessage(bcos::Error::Ptr _error, bcos::crypto::NodeIDPtr _nodeID,
        bytesConstRef _data, SendResponseCallback _sendResponse);

    virtual void onRecvProposal(bool _containSysTxs, const protocol::Block& proposal,
        bcos::protocol::BlockNumber _proposalIndex, bcos::crypto::HashType const& _proposalHash);

    // PBFT main processing function
    void executeWorker() override;

    // General entry for message processing
    virtual void handleMsg(std::shared_ptr<PBFTBaseMessageInterface> _msg);

    // Process Pre-prepare type message packets
    virtual bool handlePrePrepareMsg(std::shared_ptr<PBFTMessageInterface> _prePrepareMsg,
        bool _needVerifyProposal, bool _generatedFromNewView = false,
        bool _needCheckSignature = true);
    // When handlePrePrepareMsg return false, then reset sealed txs
    virtual void resetSealedTxs(
        std::shared_ptr<PBFTMessageInterface> const& _prePrepareMsg, const protocol::Block& block);

    // To check pre-prepare msg valid
    virtual CheckResult checkPrePrepareMsg(std::shared_ptr<PBFTMessageInterface> _prePrepareMsg);
    // To check pbft msg sign valid
    virtual CheckResult checkSignature(std::shared_ptr<PBFTBaseMessageInterface> _req);
    virtual bool checkProposalSignature(
        IndexType _generatedFrom, PBFTProposalInterface::Ptr _proposal);

    virtual CheckResult checkPBFTMsgState(std::shared_ptr<PBFTMessageInterface> _pbftReq) const;
    virtual bool checkRotateTransactionValid(PBFTMessageInterface::Ptr const& _proposal,
        ConsensusNode const& _leaderInfo, bool needCheckSign);

    // When pre-prepare proposal seems ok, then broadcast prepare msg
    virtual void broadcastPrepareMsg(std::shared_ptr<PBFTMessageInterface> const& _prePrepareMsg);

    // Process the Prepare type message packet
    virtual bool handlePrepareMsg(PBFTMessageInterface::Ptr const& _prepareMsg);
    // To check 'Prepare' or 'Commit' type proposal
    virtual CheckResult checkPBFTMsg(PBFTMessageInterface::Ptr const& _prepareMsg);

    virtual bool handleCommitMsg(PBFTMessageInterface::Ptr const& _commitMsg);

    virtual void onTimeout();
    virtual ViewChangeMsgInterface::Ptr generateViewChange();
    virtual void broadcastViewChangeReq();
    virtual void sendViewChange(bcos::crypto::NodeIDPtr _dstNode);

    virtual bool handleViewChangeMsg(std::shared_ptr<ViewChangeMsgInterface> _viewChangeMsg);
    virtual bool isValidViewChangeMsg(bcos::crypto::NodeIDPtr _fromNode,
        std::shared_ptr<ViewChangeMsgInterface> _viewChangeMsg, bool _shouldCheckSig = true);

    virtual bool handleNewViewMsg(std::shared_ptr<NewViewMsgInterface> _newViewMsg);
    virtual void reHandlePrePrepareProposals(std::shared_ptr<NewViewMsgInterface> _newViewMsg);
    virtual bool isValidNewViewMsg(std::shared_ptr<NewViewMsgInterface> _newViewMsg);
    // FIB-124: verify every prePrepareList item carried by a NewView is exactly justified
    // by the bundled viewChange evidence (mirrors PBFTCacheProcessor::generatePrePrepareMsg).
    // Called from isValidNewViewMsg after viewChange signatures and quorum weight pass.
    virtual bool isValidNewViewPrePrepareList(std::shared_ptr<NewViewMsgInterface> _newViewMsg);
    virtual void reachNewView(ViewType _view);

    // handle the checkpoint message
    virtual bool handleCheckPointMsg(std::shared_ptr<PBFTMessageInterface> _checkPointMsg);

    // function called after reaching a consensus
    virtual void finalizeConsensus(
        std::shared_ptr<bcos::ledger::LedgerConfig> _ledgerConfig, bool _syncedBlock = false);


    virtual void onProposalApplied(int64_t _errorCode, PBFTProposalInterface::Ptr _proposal,
        PBFTProposalInterface::Ptr _executedProposal);
    virtual void onProposalApplySuccess(
        PBFTProposalInterface::Ptr _proposal, PBFTProposalInterface::Ptr _executedProposal);
    virtual void onProposalApplyFailed(int64_t _errorCode, PBFTProposalInterface::Ptr _proposal);
    virtual void onLoadAndVerifyProposalFinish(
        bool _verifyResult, Error::Ptr const& _error, PBFTProposalInterface::Ptr const& _proposal);
    virtual void triggerTimeout(bool _incTimeout = true);

    void handleRecoverResponse(PBFTMessageInterface::Ptr _recoverResponse);
    void handleRecoverRequest(PBFTMessageInterface::Ptr _request);
    void sendRecoverResponse(bcos::crypto::NodeIDPtr _dstNode);
    bool isSyncingHigher();
    /**
     * @brief Receive proposal requests from other nodes and reply to corresponding proposals
     *
     * @param _pbftMsg the proposal request
     * @param _sendResponse callback used to send the requested-proposals back to the node
     */
    virtual void onReceiveCommittedProposalRequest(
        PBFTBaseMessageInterface::Ptr _pbftMsg, SendResponseCallback _sendResponse);

    /**
     * @brief Receive precommit requests from other nodes and reply to the corresponding precommit
     * data
     *
     * @param _pbftMessage the precommit request
     * @param _sendResponse callback used to send the requested-proposals back to the node
     */
    virtual void onReceivePrecommitRequest(
        std::shared_ptr<PBFTBaseMessageInterface> _pbftMessage, SendResponseCallback _sendResponse);
    void sendCommittedProposalResponse(
        PBFTProposalList const& _proposalList, SendResponseCallback _sendResponse);

    virtual void onStableCheckPointCommitFailed(
        Error::Ptr&& _error, PBFTProposalInterface::Ptr _stableProposal);

    // FIB-132: helpers for in-flight proposal deduplication
    static std::string inFlightKey(std::shared_ptr<PBFTBaseMessageInterface> const& _msg);
    void eraseInFlightProposal(std::shared_ptr<PBFTBaseMessageInterface> const& _msg);

private:
    // utility functions
    void switchToRPBFT(const ledger::LedgerConfig::Ptr& _ledgerConfig);

protected:
    // PBFT configuration class
    // mainly maintains the node information, consensus configuration information
    // such as consensus node list, consensus weight, etc.
    std::shared_ptr<PBFTConfig> m_config;

    // PBFT message cache queue
    tbb::concurrent_queue<std::shared_ptr<PBFTBaseMessageInterface>> m_msgQueue;
    std::shared_ptr<PBFTCacheProcessor> m_cacheProcessor;
    // for log syncing
    PBFTLogSync::Ptr m_logSync;

    std::function<void(std::string const&, int, bcos::crypto::NodeIDPtr, bytesConstRef)>
        m_sendResponseHandler;

    mutable RecursiveMutex m_mutex;

    const unsigned c_PopWaitSeconds = 5;
    const std::set<PacketType> c_consensusPacket = {PrePreparePacket, PreparePacket, CommitPacket};

    // FIB-126: Maximum allowed clock skew (ms) between the proposed block timestamp and the
    // local wall-clock time.  Proposals more than this far in the future are rejected.
    // 15 seconds matches common blockchain practice for BFT networks.
    static constexpr int64_t c_maxAllowedFutureTimestampMs = 15'000;

    std::atomic_bool m_stopped = {false};

    // the timer used to resend checkPointProposal
    std::shared_ptr<bcos::Timer> m_timer;

    ledger::LedgerInterface::Ptr m_ledger;
    ledger::LedgerConfig::Ptr m_ledgerConfig;

public:
    // FIB-132: hard cap on in-flight verifications. Sized well above the
    // typical worker-pool concurrency so legitimate load never hits it;
    // reaches the cap only under a flood of distinct invalid PrePrepares
    // whose verify callbacks lag.
    static constexpr size_t c_maxInFlightProposals = 1024;

protected:
    // FIB-132: in-flight verification set keyed by "<index>:<hash>:<view>".
    // Prevents the same PrePrepare proposal from being re-submitted to
    // verifyProposal() while a prior verification is still pending.
    // Bounded by c_maxInFlightProposals; new entries are rejected when full.
    std::unordered_set<std::string> m_inFlightProposals;

    // FIB-145 / FIB-146: 3-stage admission pipeline applied before m_msgQueue.push().
    PBFTPipeline m_pipeline;
    // FIB-131: per-peer consecutive invalid-pre-prepare counter used to suppress reseal storms.
    // Key: node index (IndexType). Protected by m_mutex.
    // Resets on any successful pre-prepare from the same peer.
    static constexpr uint32_t c_maxInvalidPrePreparePerPeer = 3;
    std::unordered_map<IndexType, uint32_t> m_invalidPrePrepareCount;
};
}  // namespace consensus
}  // namespace bcos