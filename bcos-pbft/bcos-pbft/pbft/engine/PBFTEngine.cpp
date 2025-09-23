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
 * @file PBFTEngine.cpp
 * @author: yujiechen
 * @date 2021-04-20
 */
#include "PBFTEngine.h"
#include "../cache/PBFTCacheFactory.h"
#include "../cache/PBFTCacheProcessor.h"
#include "bcos-framework/ledger/Ledger.h"
#include "bcos-ledger/LedgerMethods.h"
#include "bcos-task/Wait.h"
#include "bcos-utilities/BoostLog.h"
#include "bcos-utilities/Common.h"
#include <bcos-framework/dispatcher/SchedulerTypeDef.h>
#include <bcos-framework/executor/PrecompiledTypeDef.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/protocol/Protocol.h>
#include <bcos-utilities/ThreadPool.h>
#include <boost/bind/bind.hpp>
#include <utility>

using namespace bcos;
using namespace bcos::consensus;
using namespace bcos::ledger;
using namespace bcos::front;
using namespace bcos::crypto;
using namespace bcos::protocol;

PBFTEngine::PBFTEngine(PBFTConfig::Ptr _config)
  : ConsensusEngine("pbft", 0), m_config(_config), m_ledgerConfig(std::make_shared<LedgerConfig>())
{
    auto cacheFactory = std::make_shared<PBFTCacheFactory>();
    m_cacheProcessor = std::make_shared<PBFTCacheProcessor>(cacheFactory, _config);
    m_logSync = std::make_shared<PBFTLogSync>(m_config, m_cacheProcessor);
    // register the timeout function
    m_config->timer()->registerTimeoutHandler(boost::bind(&PBFTEngine::onTimeout, this));
    m_config->storage()->registerFinalizeHandler(boost::bind(
        &PBFTEngine::finalizeConsensus, this, boost::placeholders::_1, boost::placeholders::_2));

    m_config->storage()->registerOnStableCheckPointCommitFailed(
        [this](Error::Ptr&& _error, PBFTProposalInterface::Ptr _stableProposal) {
            onStableCheckPointCommitFailed(std::move(_error), std::move(_stableProposal));
        });

    m_config->registerFastViewChangeHandler([this]() { triggerTimeout(false); });
    m_cacheProcessor->registerProposalAppliedHandler(boost::bind(&PBFTEngine::onProposalApplied,
        this, boost::placeholders::_1, boost::placeholders::_2, boost::placeholders::_3));

    m_cacheProcessor->registerOnLoadAndVerifyProposalFinish(
        boost::bind(&PBFTEngine::onLoadAndVerifyProposalFinish, this, boost::placeholders::_1,
            boost::placeholders::_2, boost::placeholders::_3));
    initSendResponseHandler();
    // when the node first setup, set timeout to be true for view recovery
    // set timeout to be true to in case of notify-seal before the PBFTEngine
    // started
    m_config->setTimeoutState(true);

    // Timer is used to manage checkpoint timeout
    m_timer =
        std::make_shared<PBFTTimer>(m_config->checkPointTimeoutInterval(), "checkPointResendTimer");
}

void PBFTEngine::initSendResponseHandler()
{
    // set the sendResponse callback
    std::weak_ptr<FrontServiceInterface> weakFrontService = m_config->frontService();
    m_sendResponseHandler = [weakFrontService](std::string const& _id, int _moduleID,
                                NodeIDPtr _dstNode, bytesConstRef _data) {
        try
        {
            auto frontService = weakFrontService.lock();
            if (!frontService)
            {
                return;
            }
            frontService->asyncSendResponse(
                _id, _moduleID, _dstNode, _data, [_id, _moduleID, _dstNode](Error::Ptr _error) {
                    if (_error)
                    {
                        PBFT_LOG(TRACE) << LOG_DESC("sendResponse failed") << LOG_KV("uuid", _id)
                                        << LOG_KV("module", std::to_string(_moduleID))
                                        << LOG_KV("dst", _dstNode->shortHex())
                                        << LOG_KV("code", _error->errorCode())
                                        << LOG_KV("msg", _error->errorMessage());
                    }
                });
        }
        catch (std::exception const& e)
        {
            PBFT_LOG(WARNING) << LOG_DESC("sendResponse exception")
                              << LOG_KV("message", boost::diagnostic_information(e))
                              << LOG_KV("uuid", _id) << LOG_KV("moduleID", _moduleID)
                              << LOG_KV("peer", _dstNode->shortHex());
        }
    };
}

void PBFTEngine::start()
{
    ConsensusEngine::start();
    // when the node setup, start the timer for view recovery
    m_config->timer()->start();
    // register timeout handler
    auto self = weak_from_this();
    m_timer->registerTimeoutHandler([self]() {
        try
        {
            auto engine = self.lock();
            if (!engine)
            {
                return;
            }
            engine->tryToResendCheckPoint();
        }
        catch (std::exception const& e)
        {
            PBFT_LOG(WARNING) << LOG_DESC("tryToResendCheckPoint error")
                              << LOG_KV("message", boost::diagnostic_information(e));
        }
    });
    m_timer->start();
    // trigger fast viewchange to reachNewView
    if (!m_config->startRecovered())
    {
        triggerTimeout(false);
    }
}

void PBFTEngine::tryToResendCheckPoint()
{
    {
        RecursiveGuard l(m_mutex);
        m_cacheProcessor->tryToResendCheckPoint();
    }
    m_timer->restart();
}

void PBFTEngine::restart()
{
    PBFT_LOG(INFO) << LOG_DESC("restart the consensus module");
    m_config->enableAsMasterNode(true);
    triggerTimeout(false);
}

void PBFTEngine::stop()
{
    if (m_stopped.load())
    {
        PBFT_LOG(WARNING) << LOG_DESC("The PBFTEngine already been stopped!");
        return;
    }
    m_stopped.store(true);
    ConsensusEngine::stop();
    if (m_logSync)
    {
        m_logSync->stop();
    }
    if (m_config)
    {
        m_config->stop();
    }
    if (m_timer)
    {
        m_timer->stop();
        m_timer->destroy();
    }
    PBFT_LOG(INFO) << LOG_DESC("stop the PBFTEngine");
}

void PBFTEngine::onLoadAndVerifyProposalFinish(
    bool _verifyResult, Error::Ptr const& _error, PBFTProposalInterface::Ptr const& _proposal)
{
    // loadAndVerify proposal failed
    if (_error || !_verifyResult)
    {
        RecursiveGuard lock(m_mutex);
        m_cacheProcessor->clearCacheAfterRecoverStateFailed();
        return;
    }
    // must add lock here to ensure thread-safe
    RecursiveGuard lock(m_mutex);
    m_cacheProcessor->updateCommitQueue(_proposal);
    // Note:  The node that obtains the consensus proposal by request
    //        proposal from other nodes  will also broadcast the checkPoint message packet,
    //        Therefore, the node may also be requested by other nodes.
    //        Therefore, it must be ensured that the obtained proposal is updated to the
    //        preCommitCache.
    m_cacheProcessor->updatePrecommit(_proposal);
    m_config->timer()->restart();
}

void PBFTEngine::onProposalApplyFailed(int64_t _errorCode, PBFTProposalInterface::Ptr _proposal)
{
    if (!m_config->asMasterNode())
    {
        PBFT_LOG(WARNING) << LOG_DESC(
                                 "onProposalApplyFailed: proposal execute failed, but do nothing "
                                 "for the node-self is not "
                                 "the master node")
                          << printPBFTProposal(_proposal) << m_config->printCurrentState();
        return;
    }
    PBFT_LOG(WARNING) << LOG_DESC("proposal execute failed") << printPBFTProposal(_proposal)
                      << m_config->printCurrentState();
    // Note: must add lock here to ensure thread-safe
    RecursiveGuard l(m_mutex);
    if (_errorCode == bcos::scheduler::SchedulerError::InvalidBlocks)
    {
        PBFT_LOG(INFO) << LOG_DESC("fetchAndUpdateLedgerConfig for InvalidBlocks");
        fetchAndUpdateLedgerConfig();
    }
    // re-push the proposal into the queue
    if (_proposal->index() > m_config->committedProposal()->index() ||
        _proposal->index() > m_config->syncingHighestNumber())
    {
        m_config->timer()->restart();
        // restart checkPoint timer to advoid timeout
        if (m_timer->running())
        {
            m_timer->restart();
        }
        PBFT_LOG(INFO) << LOG_DESC(
                              "proposal execute failed and re-push the proposal "
                              "into the cache")
                       << printPBFTProposal(_proposal);
        // Note: must erase the proposal firstly for updateCommitQueue will not
        // receive the duplicated executing proposal
        m_cacheProcessor->eraseExecutedProposal(_proposal->hash());
        // retry after 20ms
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        m_cacheProcessor->updateCommitQueue(_proposal);
        return;
    }
    m_config->setExpectedCheckPoint(m_config->committedProposal()->index() + 1);
    m_cacheProcessor->eraseExecutedProposal(_proposal->hash());
}

void PBFTEngine::onProposalApplySuccess(
    PBFTProposalInterface::Ptr _proposal, PBFTProposalInterface::Ptr _executedProposal)
{
    // commit the proposal when execute success
    m_config->storage()->asyncCommitProposal(_proposal);

    // broadcast checkpoint message
    auto checkPointMsg = m_config->pbftMessageFactory()->populateFrom(PacketType::CheckPoint,
        m_config->pbftMsgDefaultVersion(), m_config->view(), utcTime(), m_config->nodeIndex(),
        _executedProposal, m_config->cryptoSuite(), m_config->keyPair(), true);

    auto encodedData = m_config->codec()->encode(checkPointMsg);
    // only broadcast message to the consensus nodes
    m_config->frontService()->asyncSendBroadcastMessage(
        bcos::protocol::NodeType::CONSENSUS_NODE, ModuleID::PBFT, ref(*encodedData));
    auto startT = utcTime();
    auto recordT = utcTime();
    // Note: must lock here to ensure thread safe
    RecursiveGuard l(m_mutex);
    auto lockT = (utcTime() - startT);
    // restart the timer when proposal execute finished to in case of timeout
    if (m_config->timer()->running())
    {
        m_config->timer()->restart();
    }
    // restart checkPoint timer to advoid timeout
    if (m_timer->running())
    {
        m_timer->restart();
    }
    m_cacheProcessor->addCheckPointMsg(checkPointMsg);
    m_cacheProcessor->setCheckPointProposal(_executedProposal);
    auto currentExpectedCheckPoint = m_config->expectedCheckPoint();
    if (currentExpectedCheckPoint < _executedProposal->index())
    {
        PBFT_LOG(WARNING) << LOG_DESC(
                                 "onProposalApplySuccess: maybe the consensus state has been "
                                 "changed, not reset the expectedCheckPoint")
                          << LOG_KV("expectedCheckPoint", currentExpectedCheckPoint)
                          << LOG_KV("index", checkPointMsg->index())
                          << LOG_KV("hash", checkPointMsg->hash().abridged());
    }
    else
    {
        m_config->setExpectedCheckPoint(_executedProposal->index() + 1);
    }
    m_cacheProcessor->checkAndCommitStableCheckPoint();
    m_cacheProcessor->tryToApplyCommitQueue();
    m_cacheProcessor->eraseExecutedProposal(_proposal->hash());
    PBFT_LOG(INFO) << LOG_DESC("onProposalApplySuccess") << LOG_KV("index", checkPointMsg->index())
                   << LOG_KV("hash", checkPointMsg->hash().abridged()) << LOG_KV("lockT", lockT)
                   << LOG_KV("timecost", (utcTime() - recordT));
}

// called after proposal executed successfully
void PBFTEngine::onProposalApplied(int64_t _errorCode, PBFTProposalInterface::Ptr _proposal,
    PBFTProposalInterface::Ptr _executedProposal)
{
    try
    {
        if (_errorCode != 0)
        {
            onProposalApplyFailed(_errorCode, _proposal);
            return;
        }
        onProposalApplySuccess(_proposal, _executedProposal);
    }
    catch (std::exception const& e)
    {
        PBFT_LOG(WARNING) << LOG_DESC("onProposalApplied exception")
                          << printPBFTProposal(_executedProposal)
                          << LOG_KV("message", boost::diagnostic_information(e));
    }
}

void PBFTEngine::asyncSubmitProposal(bool _containSysTxs, const protocol::Block& proposal,
    BlockNumber _proposalIndex, HashType const& _proposalHash,
    std::function<void(Error::Ptr)> _onProposalSubmitted)
{
    if (_onProposalSubmitted)
    {
        _onProposalSubmitted(nullptr);
    }
    onRecvProposal(_containSysTxs, proposal, _proposalIndex, _proposalHash);
}

void PBFTEngine::onRecvProposal(bool _containSysTxs, const protocol::Block& proposal,
    BlockNumber _proposalIndex, HashType const& _proposalHash)
{
    if (!m_config->isConsensusNode())
    {
        return;
    }
    // expired proposal
    auto consProposalIndex = m_config->committedProposal()->index() + 1;
    if (_proposalIndex <= m_config->syncingHighestNumber())
    {
        PBFT_LOG(WARNING) << LOG_DESC("asyncSubmitProposal failed for expired index")
                          << LOG_KV("index", _proposalIndex)
                          << LOG_KV("hash", _proposalHash.abridged())
                          << m_config->printCurrentState()
                          << LOG_KV("syncingHighestNumber", m_config->syncingHighestNumber());
        m_config->notifyResetSealing(consProposalIndex);
        m_config->validator()->asyncResetTxsFlag(proposal, false, true);
        return;
    }
    if (_proposalIndex <= m_config->committedProposal()->index() ||
        _proposalIndex < m_config->expectedCheckPoint() ||
        _proposalIndex < m_config->lowWaterMark())
    {
        PBFT_LOG(WARNING) << LOG_DESC("asyncSubmitProposal failed for invalid index")
                          << LOG_KV("index", _proposalIndex)
                          << LOG_KV("hash", _proposalHash.abridged())
                          << m_config->printCurrentState()
                          << LOG_KV("lowWaterMark", m_config->lowWaterMark());
        return;
    }
    auto leaderIndex = m_config->leaderIndex(_proposalIndex);
    if (leaderIndex != m_config->nodeIndex())
    {
        PBFT_LOG(WARNING) << LOG_DESC(
                                 "asyncSubmitProposal failed for the node-self is not the leader")
                          << LOG_KV("expectedLeader", leaderIndex)
                          << LOG_KV("index", _proposalIndex)
                          << LOG_KV("hash", _proposalHash.abridged())
                          << m_config->printCurrentState();
        m_config->notifyResetSealing(consProposalIndex);
        m_config->validator()->asyncResetTxsFlag(proposal, false, true);
        return;
    }
    PBFT_LOG(INFO) << LOG_DESC("asyncSubmitProposal") << LOG_KV("index", _proposalIndex)
                   << LOG_KV("hash", _proposalHash.abridged()) << m_config->printCurrentState();
    // generate the pre-prepare packet
    auto pbftProposal = m_config->pbftMessageFactory()->createPBFTProposal();

    bytes blockData;
    proposal.encode(blockData);

    pbftProposal->setData(blockData);
    pbftProposal->setIndex(_proposalIndex);
    pbftProposal->setHash(_proposalHash);
    pbftProposal->setSealerId(m_config->nodeIndex());
    pbftProposal->setSystemProposal(_containSysTxs);

    auto pbftMessage =
        m_config->pbftMessageFactory()->populateFrom(PacketType::PrePreparePacket, pbftProposal,
            m_config->pbftMsgDefaultVersion(), m_config->view(), utcTime(), m_config->nodeIndex());
    PBFT_LOG(INFO) << LOG_DESC("++++++++++++++++ Generating seal on")
                   << LOG_KV("index", pbftMessage->index()) << LOG_KV("Idx", m_config->nodeIndex())
                   << LOG_KV("hash", pbftMessage->hash().abridged())
                   << LOG_KV("sysProposal", pbftProposal->systemProposal());

    // NOTE: must ensure thread safe, should not write any filed while
    // encoding broadcast the pre-prepare packet
    auto encodeStart = utcTime();
    auto encodedData = m_config->codec()->encode(pbftMessage);
    auto encodeEnd = utcTime();

    // only broadcast pbft message to the consensus nodes
    task::wait([](decltype(m_config) config, decltype(encodedData) encoded) -> task::Task<void> {
        co_await config->frontService()->broadcastMessage(bcos::protocol::NodeType::CONSENSUS_NODE,
            ModuleID::PBFT, ::ranges::views::single(ref(std::as_const(*encoded))));
    }(m_config, encodedData));
    PBFT_LOG(INFO) << LOG_DESC("broadcast pre-prepare packet")
                   << LOG_KV("packetSize", encodedData->size())
                   << LOG_KV("index", pbftMessage->index())
                   << LOG_KV("encode(ms)", encodeEnd - encodeStart)
                   << LOG_KV("asyncSend(ms)", utcTime() - encodeEnd);

    // handle the pre-prepare packet
    RecursiveGuard lock(m_mutex);
    auto beginHandleT = utcSteadyTime();
    auto ret = handlePrePrepareMsg(pbftMessage, false, false, false);
    // only broadcast the prePrepareMsg when local handlePrePrepareMsg success
    if (ret) [[likely]]
    {
        PBFT_LOG(INFO) << LOG_DESC("handlePrePrepareMsg success")
                       << LOG_KV("index", pbftMessage->index())
                       << LOG_KV("costT(ms)", utcSteadyTime() - beginHandleT);
    }
    else
    {
        PBFT_LOG(INFO) << LOG_DESC("handlePrePrepareMsg failed") << printPBFTProposal(pbftMessage)
                       << LOG_KV("costT(ms)", utcSteadyTime() - beginHandleT);
        resetSealedTxs(pbftMessage, proposal);
    }
}

void PBFTEngine::resetSealedTxs(
    std::shared_ptr<PBFTMessageInterface> const& _prePrepareMsg, const protocol::Block& block)
{
    if (_prePrepareMsg->generatedFrom() != m_config->nodeIndex()) [[unlikely]]
    {
        return;
    }
    m_config->notifyResetSealing();
    m_config->validator()->asyncResetTxsFlag(block, false);
}

// receive the new block notification from the sync module
void PBFTEngine::asyncNotifyNewBlock(
    LedgerConfig::Ptr _ledgerConfig, std::function<void(Error::Ptr)> _onRecv)
{
    if (_onRecv)
    {
        _onRecv(nullptr);
    }
    if (m_config->shouldResetConfig(_ledgerConfig->blockNumber()))
    {
        PBFT_LOG(INFO) << LOG_DESC("The sync module notify the latestBlock")
                       << LOG_KV("index", _ledgerConfig->blockNumber())
                       << LOG_KV("hash", _ledgerConfig->hash().abridged());
        finalizeConsensus(_ledgerConfig, true);
    }
}

void PBFTEngine::onReceivePBFTMessage(
    bcos::Error::Ptr _error, std::string const& _id, NodeIDPtr _nodeID, bytesConstRef _data)
{
    auto self = weak_from_this();
    onReceivePBFTMessage(
        std::move(_error), _nodeID, _data, [_id, _nodeID, self](bytesConstRef _respData) {
            try
            {
                auto engine = self.lock();
                if (!engine)
                {
                    return;
                }
                engine->m_sendResponseHandler(_id, ModuleID::PBFT, _nodeID, _respData);
            }
            catch (std::exception const& e)
            {
                PBFT_LOG(WARNING) << LOG_DESC("onReceivePBFTMessage exception")
                                  << LOG_KV("fromNode", _nodeID->hex()) << LOG_KV("uuid", _id)
                                  << LOG_KV("message", boost::diagnostic_information(e));
            }
        });
}

void PBFTEngine::onReceivePBFTMessage(Error::Ptr _error, NodeIDPtr _fromNode, bytesConstRef _data,
    SendResponseCallback _sendResponseCallback)
{
    try
    {
        if (_error != nullptr)
        {
            return;
        }
        // the node is not the consensusNode
        if (!m_config->isConsensusNode())
        {
            PBFT_LOG(TRACE) << LOG_DESC(
                "onReceivePBFTMessage: reject the message "
                "for the node is not the consensus "
                "node");
            return;
        }
        // decode the message and push the message into the queue
        auto pbftMsg = m_config->codec()->decode(_data);
        pbftMsg->setFrom(_fromNode);
        // the committed proposal request message
        if (pbftMsg->packetType() == PacketType::CommittedProposalRequest)
        {
            try
            {
                onReceiveCommittedProposalRequest(pbftMsg, _sendResponseCallback);
            }
            catch (std::exception const& e)
            {
                PBFT_LOG(WARNING) << LOG_DESC("onReceiveCommittedProposalRequest exception")
                                  << LOG_KV("message", boost::diagnostic_information(e));
            }
            return;
        }
        // the precommitted proposals request message
        if (pbftMsg->packetType() == PacketType::PreparedProposalRequest)
        {
            try
            {
                onReceivePrecommitRequest(pbftMsg, _sendResponseCallback);
            }
            catch (std::exception const& e)
            {
                PBFT_LOG(WARNING) << LOG_DESC("onReceivePrecommitRequest exception")
                                  << LOG_KV("message", boost::diagnostic_information(e));
            }
            return;
        }
        m_msgQueue.push(pbftMsg);
        m_signalled.notify_all();
    }
    catch (std::exception const& _e)
    {
        PBFT_LOG(WARNING) << LOG_DESC("onReceivePBFTMessage exception")
                          << LOG_KV("fromNode", _fromNode->hex())
                          << LOG_KV("Idx", m_config->nodeIndex())
                          << LOG_KV("nodeId", m_config->nodeID()->hex())
                          << LOG_KV("message", boost::diagnostic_information(_e));
    }
}

void PBFTEngine::clearAllCache()
{
    RecursiveGuard l(m_mutex);
    m_cacheProcessor->clearAllCache();
}

void PBFTEngine::executeWorker()
{
    // the node is not the consensusNode
    if (!m_config->isConsensusNode())
    {
        waitSignal();
        return;
    }
    // when the node is syncing, not handle the PBFT message
    if (isSyncingHigher())
    {
        waitSignal();
        return;
    }
    // handle the PBFT message(here will wait when the msgQueue is empty)
    std::shared_ptr<PBFTBaseMessageInterface> messageResult;
    m_msgQueue.try_pop(messageResult);
    auto empty = m_msgQueue.empty();
    if (messageResult)
    {
        const auto& pbftMsg = messageResult;
        auto packetType = pbftMsg->packetType();
        // can't handle the future consensus messages when handling the system
        // proposal
        if ((c_consensusPacket.contains(packetType)) && !m_config->canHandleNewProposal(pbftMsg))
        {
#if 0
            PBFT_LOG(TRACE) << LOG_DESC(
                                   "receive consensus packet, re-push it to the msgQueue for "
                                   "canHandleNewProposal")
                            << LOG_KV("index", pbftMsg->index()) << LOG_KV("type", packetType)
                            << m_config->printCurrentState();
#endif
            m_msgQueue.push(pbftMsg);
            if (empty)
            {
                // only one pbft msg, and cannot handle proposal
                // re-push msg to queue and wait for signal try to re-handle
                waitSignal();
            }
            return;
        }
        handleMsg(pbftMsg);
    }
    else
    {
        waitSignal();
    }
}

void PBFTEngine::handleMsg(std::shared_ptr<PBFTBaseMessageInterface> _msg)
{
    // check the view
    if (_msg->view() > MaxView)
    {
        PBFT_LOG(WARNING) << LOG_DESC("handleMsg: reject msg with invalid view")
                          << printPBFTMsgInfo(_msg);
        return;
    }
    RecursiveGuard lock(m_mutex);
    switch (_msg->packetType())
    {
    case PacketType::PrePreparePacket:
    {
        auto prePrepareMsg = std::dynamic_pointer_cast<PBFTMessageInterface>(_msg);
        handlePrePrepareMsg(prePrepareMsg, true);
        break;
    }
    case PacketType::PreparePacket:
    {
        auto prepareMsg = std::dynamic_pointer_cast<PBFTMessageInterface>(_msg);
        handlePrepareMsg(prepareMsg);
        break;
    }
    case PacketType::CommitPacket:
    {
        auto commitMsg = std::dynamic_pointer_cast<PBFTMessageInterface>(_msg);
        handleCommitMsg(commitMsg);
        break;
    }
    case PacketType::ViewChangePacket:
    {
        auto viewChangeMsg = std::dynamic_pointer_cast<ViewChangeMsgInterface>(_msg);
        handleViewChangeMsg(viewChangeMsg);
        break;
    }
    case PacketType::NewViewPacket:
    {
        auto newViewMsg = std::dynamic_pointer_cast<NewViewMsgInterface>(_msg);
        handleNewViewMsg(newViewMsg);
        break;
    }
    case PacketType::CheckPoint:
    {
        auto checkPointMsg = std::dynamic_pointer_cast<PBFTMessageInterface>(_msg);
        handleCheckPointMsg(checkPointMsg);
        break;
    }
    case PacketType::RecoverRequest:
    {
        auto request = std::dynamic_pointer_cast<PBFTMessageInterface>(_msg);
        handleRecoverRequest(request);
        break;
    }
    case PacketType::RecoverResponse:
    {
        auto recoverResponse = std::dynamic_pointer_cast<PBFTMessageInterface>(_msg);
        handleRecoverResponse(recoverResponse);
        break;
    }
    default:
    {
        PBFT_LOG(WARNING) << LOG_DESC("handleMsg: unknown PBFT message")
                          << LOG_KV("type", std::to_string(_msg->packetType()))
                          << LOG_KV("genIdx", _msg->generatedFrom())
                          << LOG_KV("nodeSelf", m_config->nodeID()->hex());
        return;
    }
    }
    // m_signalled.notify_all();
}

CheckResult PBFTEngine::checkPBFTMsgState(PBFTMessageInterface::Ptr _pbftReq) const
{
    if (!_pbftReq->consensusProposal())
    {
        return CheckResult::INVALID;
    }
    bool proposalCommitted = m_cacheProcessor->proposalCommitted(_pbftReq->index());
    if (_pbftReq->index() < m_config->lowWaterMark() ||
        _pbftReq->index() < m_config->expectedCheckPoint() ||
        _pbftReq->index() <= m_config->syncingHighestNumber() || proposalCommitted)
    {
        PBFT_LOG(DEBUG) << LOG_DESC("checkPBFTMsgState: invalid pbftMsg for invalid index")
                        << LOG_KV("highWaterMark", m_config->highWaterMark())
                        << LOG_KV("lowWaterMark", m_config->lowWaterMark())
                        << printPBFTMsgInfo(_pbftReq) << m_config->printCurrentState()
                        << LOG_KV("syncingNumber", m_config->syncingHighestNumber())
                        << LOG_KV("proposalCommitted", proposalCommitted);
        return CheckResult::INVALID;
    }
    // Note: Accept pbft message with larger view then local view, for other nodes may viewchange to
    // a larger view, and the node-self is not aware of the viewchange.
    // In normal case, it will not happen, node-self will recover the view from the viewchange, and
    // soon will reach to new view.
    // BUT in the Byzantium case, malicious node will send the pre-prepare message with a larger
    // view, to lay down some specific txs.
    // FIXME: to check this logic.
    if (_pbftReq->view() < m_config->view())
    {
        PBFT_LOG(DEBUG) << LOG_DESC("checkPBFTMsgState: invalid pbftMsg for invalid view")
                        << printPBFTMsgInfo(_pbftReq) << m_config->printCurrentState();
        return CheckResult::INVALID;
    }
    return CheckResult::VALID;
}

CheckResult PBFTEngine::checkPrePrepareMsg(std::shared_ptr<PBFTMessageInterface> _prePrepareMsg)
{
    if (checkPBFTMsgState(_prePrepareMsg) == CheckResult::INVALID)
    {
        return CheckResult::INVALID;
    }
    // check the existence of the msg
    if (m_cacheProcessor->existPrePrepare(_prePrepareMsg))
    {
        PBFT_LOG(DEBUG) << LOG_DESC("handlePrePrepareMsg: duplicated")
                        << LOG_KV("committedIndex", m_config->committedProposal()->index())
                        << LOG_KV("recvIndex", _prePrepareMsg->index())
                        << LOG_KV("hash", _prePrepareMsg->hash().abridged())
                        << m_config->printCurrentState();
        return CheckResult::INVALID;
    }
    // already has prePrepare msg
    if (m_cacheProcessor->conflictWithProcessedReq(_prePrepareMsg))
    {
        PBFT_LOG(DEBUG) << LOG_DESC("handlePrePrepareMsg: already has prePrepare msg")
                        << LOG_KV("committedIndex", m_config->committedProposal()->index())
                        << LOG_KV("recvIndex", _prePrepareMsg->index())
                        << LOG_KV("hash", _prePrepareMsg->hash().abridged())
                        << m_config->printCurrentState();
        return CheckResult::INVALID;
    }
    // check conflict
    if (m_cacheProcessor->conflictWithPrecommitReq(_prePrepareMsg))
    {
        PBFT_LOG(DEBUG) << LOG_DESC("handlePrePrepareMsg: conflictWithPrecommitReq")
                        << LOG_KV("committedIndex", m_config->committedProposal()->index())
                        << LOG_KV("recvIndex", _prePrepareMsg->index())
                        << LOG_KV("hash", _prePrepareMsg->hash().abridged())
                        << m_config->printCurrentState();
        return CheckResult::INVALID;
    }
    // TODO: to check this fix is proper
    // refuse pre-prepare massage from too large view node
    if (m_config->view() + m_config->waterMarkLimit() < _prePrepareMsg->view())
    {
        PBFT_LOG(INFO) << LOG_DESC(
                              "handlePrePrepareMsg: refuse pre-prepare massage from too large view")
                       << LOG_KV("committedIndex", m_config->committedProposal()->index())
                       << LOG_KV("recvIndex", _prePrepareMsg->index())
                       << LOG_KV("recvView", _prePrepareMsg->view())
                       << LOG_KV("hash", _prePrepareMsg->hash().abridged())
                       << m_config->printCurrentState();
        return CheckResult::INVALID;
    }
    return CheckResult::VALID;
}

CheckResult PBFTEngine::checkSignature(PBFTBaseMessageInterface::Ptr _req)
{
    // check the signature
    auto* nodeInfo = m_config->getConsensusNodeByIndex(_req->generatedFrom());
    if (!nodeInfo)
    {
        PBFT_LOG(WARNING) << LOG_DESC("checkSignature failed for the node is not a consensus node")
                          << printPBFTMsgInfo(_req);
        return CheckResult::INVALID;
    }
    auto publicKey = nodeInfo->nodeID;
    if (!_req->verifySignature(m_config->cryptoSuite(), publicKey))
    {
        PBFT_LOG(WARNING) << LOG_DESC("checkSignature failed for invalid signature")
                          << printPBFTMsgInfo(_req);
        return CheckResult::INVALID;
    }
    return CheckResult::VALID;
}

bool PBFTEngine::checkProposalSignature(
    IndexType _generatedFrom, PBFTProposalInterface::Ptr _proposal)
{
    if (!_proposal || _proposal->signature().size() == 0)
    {
        return false;
    }
    auto* nodeInfo = m_config->getConsensusNodeByIndex(_generatedFrom);
    if (nodeInfo == nullptr)
    {
        PBFT_LOG(WARNING) << LOG_DESC(
                                 "checkProposalSignature failed for the node "
                                 "is not a consensus node")
                          << printPBFTProposal(_proposal);
        return false;
    }

    return m_config->cryptoSuite()->signatureImpl()->verify(
        nodeInfo->nodeID, _proposal->hash(), _proposal->signature());
}

bool PBFTEngine::checkRotateTransactionValid(PBFTMessageInterface::Ptr const& _proposal,
    ConsensusNode const& _leaderInfo, bool needCheckSign)
{
    if (m_config->consensusType() == ConsensusType::PBFT_TYPE &&
        m_config->rpbftConfigTools() == nullptr) [[likely]]
    {
        return true;
    }

    // Note: if the block contains rotatingTx when m_shouldRotateSealers is false
    //       the rotatingTx will be reverted by the ordinary node when executing
    if (!shouldRotateSealers(_proposal->index())) [[unlikely]]
    {
        return true;
    }

    auto block = m_config->blockFactory().createBlock(_proposal->consensusProposal()->data());

    PBFT_LOG(INFO) << LOG_DESC("checkRotateTransactionValid")
                   << LOG_KV("reqIndex", _proposal->index())
                   << LOG_KV("reqHash", _proposal->hash().abridged())
                   << LOG_KV("leaderIdx", _proposal->generatedFrom())
                   << LOG_KV("nodeIdx", m_config->nodeIndex())
                   << LOG_KV("txSize", block->transactionsMetaDataSize())
                   << LOG_KV("needCheckSign", needCheckSign);

    auto rotatingTx = block->transactionMetaData(0);
    if (rotatingTx->to() != bcos::precompiled::CONSENSUS_ADDRESS &&
        rotatingTx->to() != bcos::precompiled::CONSENSUS_TABLE_NAME) [[unlikely]]
    {
        PBFT_LOG(WARNING) << LOG_DESC("checkRotateTransactionValid failed for wrong tx")
                          << LOG_KV("reqIndex", _proposal->index())
                          << LOG_KV("reqHash", _proposal->hash().abridged())
                          << LOG_KV("fromIdx", _proposal->generatedFrom())
                          << LOG_KV("currentTo", rotatingTx->to());
        return false;
    }

    if (!needCheckSign)
    {
        return true;
    }

    // FIXME: should not use source
    auto leaderAddress = right160(m_config->cryptoSuite()->hash(_leaderInfo.nodeID));
    if (rotatingTx->source() == leaderAddress.hex())
    {
        return true;
    }

    PBFT_LOG(WARNING) << LOG_DESC("checkRotateTransactionValid failed for wrong leader")
                      << LOG_KV("reqIndex", _proposal->index())
                      << LOG_KV("reqHash", _proposal->hash().abridged())
                      << LOG_KV("fromIdx", _proposal->generatedFrom())
                      << LOG_KV("leader", _leaderInfo.nodeID->hex())
                      << LOG_KV("leaderAddress", leaderAddress.hex())
                      << LOG_KV("sender", rotatingTx->source());
    return false;
}

bool PBFTEngine::isSyncingHigher()
{
    auto committedIndex = m_config->committedProposal()->index();
    auto syncNumber = m_config->syncingHighestNumber();
    return syncNumber >= (committedIndex + m_config->waterMarkLimit());
}

bool PBFTEngine::handlePrePrepareMsg(PBFTMessageInterface::Ptr _prePrepareMsg,
    bool _needVerifyProposal, bool _generatedFromNewView, bool _needCheckSignature)
{
    ittapi::Report report(
        ittapi::ITT_DOMAINS::instance().PBFT, ittapi::ITT_DOMAINS::instance().PRE_PREPARE_MSG);
    if (isSyncingHigher())
    {
        PBFT_LOG(INFO) << LOG_DESC(
                              "handlePrePrepareMsg: reject the prePrepareMsg "
                              "for the node is syncing")
                       << LOG_KV("committedIndex", m_config->committedProposal()->index())
                       << LOG_KV("recvIndex", _prePrepareMsg->index())
                       << LOG_KV("hash", _prePrepareMsg->hash().abridged())
                       << LOG_KV("syncingNum", m_config->syncingHighestNumber())
                       << m_config->printCurrentState();
        return false;
    }
    if (m_cacheProcessor->executingProposals().contains(_prePrepareMsg->hash()))
    {
        PBFT_LOG(DEBUG) << LOG_DESC(
                               "handlePrePrepareMsg: reject the prePrepareMsg "
                               "for the proposal is executing")
                        << LOG_KV("committedIndex", m_config->committedProposal()->index())
                        << LOG_KV("recvIndex", _prePrepareMsg->index())
                        << LOG_KV("hash", _prePrepareMsg->hash().abridged())
                        << m_config->printCurrentState();
        return false;
    }
    PBFT_LOG(INFO) << LOG_DESC("handlePrePrepareMsg") << printPBFTMsgInfo(_prePrepareMsg)
                   << m_config->printCurrentState() << LOG_KV("utc", utcSteadyTime());

    auto result = checkPrePrepareMsg(_prePrepareMsg);
    if (result == CheckResult::INVALID)
    {
        PBFT_LOG(INFO) << LOG_DESC("handlePrePrepareMsg checkPrePrepareMsg failed")
                       << printPBFTMsgInfo(_prePrepareMsg) << m_config->printCurrentState()
                       << LOG_KV("utc", utcSteadyTime());
        return false;
    }
    if (!_generatedFromNewView)
    {
        // packet can be processed in this round of consensus
        // check the proposal is generated from the leader
        auto expectedLeader = m_config->leaderIndex(_prePrepareMsg->index());
        if (expectedLeader != _prePrepareMsg->generatedFrom())
        {
            PBFT_LOG(TRACE) << LOG_DESC(
                                   "handlePrePrepareMsg: invalid packet for not from the leader")
                            << printPBFTMsgInfo(_prePrepareMsg) << m_config->printCurrentState()
                            << LOG_KV("expectedLeader", expectedLeader);
            return false;
        }
        if (_needCheckSignature)
        {
            // check the signature
            result = checkSignature(_prePrepareMsg);
            if (result == CheckResult::INVALID)
            {
                m_config->notifySealer(_prePrepareMsg->index(), true);
                return false;
            }
        }
    }
    auto block = m_config->blockFactory().createBlock(
        _prePrepareMsg->consensusProposal()->data(), false, false);
    // add the prePrepareReq to the cache
    if (!_needVerifyProposal)
    {
        // Note: must reset the txs to be sealed no matter verify success or failed
        // because some nodes may verify failed for timeout, while other nodes may
        // verify success
        m_config->validator()->asyncResetTxsFlag(*block, true);
        // add the pre-prepare packet into the cache
        m_cacheProcessor->addPrePrepareCache(_prePrepareMsg);
        m_config->timer()->restart();
        // broadcast PrepareMsg the packet
        broadcastPrepareMsg(_prePrepareMsg);
        PBFT_LOG(INFO) << LOG_DESC("handlePrePrepareMsg and broadcast prepare packet")
                       << printPBFTMsgInfo(_prePrepareMsg) << m_config->printCurrentState();
        m_cacheProcessor->checkAndPreCommit();
        return true;
    }
    // verify the proposal
    auto self = weak_from_this();
    auto* leaderNodeInfo = m_config->getConsensusNodeByIndex(_prePrepareMsg->generatedFrom());
    if (leaderNodeInfo == nullptr)
    {
        return false;
    }
    m_config->validator()->verifyProposal(leaderNodeInfo->nodeID,
        _prePrepareMsg->consensusProposal(),
        [self, _prePrepareMsg, _generatedFromNewView, leaderNodeInfo, _needCheckSignature,
            block = std::move(block)](auto&& _error, bool _verifyResult) {
            try
            {
                auto pbftEngine = self.lock();
                if (!pbftEngine)
                {
                    return;
                }
                auto committedIndex = pbftEngine->m_config->committedProposal()->index();
                if (committedIndex >= _prePrepareMsg->index())
                {
                    return;
                }
                // Note: must reset the txs to be sealed no matter verify success or
                // failed because some nodes may verify failed for timeout,  while
                // other nodes may verify success
                pbftEngine->m_config->validator()->asyncResetTxsFlag(*block, true);

                // verify exceptioned
                if (_error != nullptr)
                {
                    PBFT_LOG(WARNING)
                        << LOG_DESC("verify proposal exceptioned")
                        << printPBFTMsgInfo(_prePrepareMsg) << LOG_KV("code", _error->errorCode())
                        << LOG_KV("msg", _error->errorMessage());
                    pbftEngine->m_config->notifySealer(_prePrepareMsg->index(), true);
                    RecursiveGuard lock(pbftEngine->m_mutex);
                    pbftEngine->m_cacheProcessor->addExceptionCache(_prePrepareMsg);
                    return;
                }
                auto rotateResult = pbftEngine->checkRotateTransactionValid(
                    _prePrepareMsg, *leaderNodeInfo, _needCheckSignature);
                // verify failed
                if (!_verifyResult || !rotateResult)
                {
                    PBFT_LOG(WARNING)
                        << LOG_DESC("verify proposal failed") << printPBFTMsgInfo(_prePrepareMsg);
                    pbftEngine->m_config->notifySealer(_prePrepareMsg->index(), true);
                    RecursiveGuard lock(pbftEngine->m_mutex);
                    pbftEngine->m_cacheProcessor->addExceptionCache(_prePrepareMsg);
                    return;
                }
                // verify success
                RecursiveGuard lock(pbftEngine->m_mutex);
                auto ret = pbftEngine->handlePrePrepareMsg(
                    _prePrepareMsg, false, _generatedFromNewView, false);
                if (!ret)
                {
                    pbftEngine->m_cacheProcessor->addExceptionCache(_prePrepareMsg);
                }
            }
            catch (std::exception const& _e)
            {
                PBFT_LOG(WARNING) << LOG_DESC("exception when calls onVerifyFinishedHandler")
                                  << printPBFTMsgInfo(_prePrepareMsg)
                                  << LOG_KV("message", boost::diagnostic_information(_e));
            }
        });
    return true;
}

void PBFTEngine::broadcastPrepareMsg(PBFTMessageInterface::Ptr const& _prePrepareMsg)
{
    auto prepareMsg = m_config->pbftMessageFactory()->populateFrom(PacketType::PreparePacket,
        m_config->pbftMsgDefaultVersion(), m_config->view(), utcTime(), m_config->nodeIndex(),
        _prePrepareMsg->consensusProposal(), m_config->cryptoSuite(), m_config->keyPair());
    prepareMsg->setIndex(_prePrepareMsg->index());
    // add the message to local cache
    m_cacheProcessor->addPrepareCache(prepareMsg);

    auto encodedData = m_config->codec()->encode(prepareMsg, m_config->pbftMsgDefaultVersion());

    PBFT_LOG(INFO) << LOG_DESC("broadcast prepare packet")
                   << LOG_KV("packetSize", encodedData->size())
                   << LOG_KV("index", _prePrepareMsg->index());
    // only broadcast to the consensus nodes
    m_config->frontService()->asyncSendBroadcastMessage(
        bcos::protocol::NodeType::CONSENSUS_NODE, ModuleID::PBFT, ref(*encodedData));
    // try to precommit the message
    m_cacheProcessor->checkAndPreCommit();
}

CheckResult PBFTEngine::checkPBFTMsg(PBFTMessageInterface::Ptr const& _prepareMsg)
{
    auto result = checkPBFTMsgState(_prepareMsg);
    if (result == CheckResult::INVALID)
    {
        return result;
    }
    if (_prepareMsg->generatedFrom() == m_config->nodeIndex())
    {
        PBFT_LOG(TRACE) << LOG_DESC("checkPrepareMsg: Recv own req")
                        << printPBFTMsgInfo(_prepareMsg);
        return CheckResult::INVALID;
    }
    // check the existence of Pre-Prepare request
    if (m_cacheProcessor->existPrePrepare(_prepareMsg))
    {
        // compare with the local pre-prepare cache
        if (m_cacheProcessor->conflictWithProcessedReq(_prepareMsg))
        {
            return CheckResult::INVALID;
        }
    }
    return checkSignature(_prepareMsg);
}

bool PBFTEngine::handlePrepareMsg(PBFTMessageInterface::Ptr const& _prepareMsg)
{
    ittapi::Report report(
        ittapi::ITT_DOMAINS::instance().PBFT, ittapi::ITT_DOMAINS::instance().PREPARE_MSG);
    PBFT_LOG(TRACE) << LOG_DESC("handlePrepareMsg") << printPBFTMsgInfo(_prepareMsg)
                    << m_config->printCurrentState();
    auto result = checkPBFTMsg(_prepareMsg);
    if (result == CheckResult::INVALID)
    {
        return false;
    }
    if (!checkProposalSignature(_prepareMsg->generatedFrom(), _prepareMsg->consensusProposal()))
    {
        return false;
    }
    m_cacheProcessor->addPrepareCache(_prepareMsg);
    m_cacheProcessor->checkAndPreCommit();
    return true;
}

bool PBFTEngine::handleCommitMsg(PBFTMessageInterface::Ptr const& _commitMsg)
{
    ittapi::Report report(
        ittapi::ITT_DOMAINS::instance().PBFT, ittapi::ITT_DOMAINS::instance().COMMIT_MSG);
    PBFT_LOG(TRACE) << LOG_DESC("handleCommitMsg") << printPBFTMsgInfo(_commitMsg)
                    << m_config->printCurrentState();
    auto result = checkPBFTMsg(_commitMsg);
    if (result == CheckResult::INVALID)
    {
        return false;
    }
    m_cacheProcessor->addCommitReq(_commitMsg);
    m_cacheProcessor->checkAndCommit();
    return true;
}

void PBFTEngine::onTimeout()
{
    // only restart the timer if the node is not exist in the group
    if (!m_config->isConsensusNode())
    {
        m_config->timer()->restart();
        return;
    }
    auto startT = utcTime();
    auto recordT = utcTime();
    RecursiveGuard lock(m_mutex);
    auto lockT = utcTime() - startT;
    if (m_cacheProcessor->tryToApplyCommitQueue())
    {
        PBFT_LOG(INFO) << LOG_DESC("onTimeout: apply proposal to state-machine, restart the timer")
                       << m_config->printCurrentState();
        m_config->timer()->restart();
        return;
    }
    m_cacheProcessor->clearExpiredExecutingProposal();
    // when some proposals are executing, not trigger timeout
    auto executingProposalSize = m_cacheProcessor->executingProposalSize();
    if (executingProposalSize > 0)
    {
        PBFT_LOG(INFO) << LOG_DESC("onTimeout: Proposal is executing, restart the timer")
                       << LOG_KV("executingProposalSize", executingProposalSize)
                       << m_config->printCurrentState();
        m_config->timer()->restart();
        return;
    }
    triggerTimeout(true);
    PBFT_LOG(WARNING) << LOG_DESC("After onTimeout") << m_config->printCurrentState()
                      << LOG_KV("lockT", lockT) << LOG_KV("timecost", (utcTime() - recordT));
}

void PBFTEngine::triggerTimeout(bool _incTimeout)
{
    m_config->resetTimeoutState(_incTimeout);
    // clear the viewchange cache
    m_cacheProcessor->removeInvalidViewChange(
        m_config->view(), m_config->committedProposal()->index());
    // notify the latest proposal index to the sync module when timeout to enable
    // syncing
    m_cacheProcessor->notifyCommittedProposalIndex(m_config->committedProposal()->index());
    // broadcast viewchange and try to the new-view phase
    broadcastViewChangeReq();
}

ViewChangeMsgInterface::Ptr PBFTEngine::generateViewChange()
{
    // broadcast the viewChangeReq
    auto committedProposal = m_config->populateCommittedProposal();
    if (committedProposal == nullptr)
    {
        PBFT_LOG(WARNING) << LOG_DESC(
            "broadcastViewChangeReq failed for the "
            "latest storage state has not been loaded.");
    }
    auto viewChangeReq = m_config->pbftMessageFactory()->createViewChangeMsg();
    viewChangeReq->setHash(m_config->committedProposal()->hash());
    viewChangeReq->setIndex(m_config->committedProposal()->index());
    viewChangeReq->setPacketType(PacketType::ViewChangePacket);
    viewChangeReq->setVersion(m_config->pbftMsgDefaultVersion());
    viewChangeReq->setView(m_config->toView());
    viewChangeReq->setTimestamp(utcTime());
    viewChangeReq->setGeneratedFrom(m_config->nodeIndex());
    // set the committed proposal
    viewChangeReq->setCommittedProposal(committedProposal);
    // set prepared proposals
    viewChangeReq->setPreparedProposals(m_cacheProcessor->preCommitCachesWithoutData());
    return viewChangeReq;
}

void PBFTEngine::sendViewChange(bcos::crypto::NodeIDPtr _dstNode)
{
    auto viewChangeReq = generateViewChange();
    // encode and broadcast the viewchangeReq
    auto encodedData = m_config->codec()->encode(viewChangeReq);
    // only broadcast to the consensus nodes
    m_config->frontService()->asyncSendMessageByNodeID(
        ModuleID::PBFT, std::move(_dstNode), ref(*encodedData), 0, nullptr);
    // collect the viewchangeReq
    m_cacheProcessor->addViewChangeReq(viewChangeReq);
    auto newViewMsg = m_cacheProcessor->checkAndTryIntoNewView();
    if (newViewMsg)
    {
        reHandlePrePrepareProposals(newViewMsg);
    }
}

void PBFTEngine::sendRecoverResponse(bcos::crypto::NodeIDPtr _dstNode)
{
    auto response = m_config->pbftMessageFactory()->createPBFTMsg();
    response->setPacketType(PacketType::RecoverResponse);
    response->setGeneratedFrom(m_config->nodeIndex());
    response->setView(m_config->view());
    response->setTimestamp(utcTime());
    response->setIndex(m_config->committedProposal()->index());
    auto encodedData = m_config->codec()->encode(response);
    m_config->frontService()->asyncSendMessageByNodeID(
        ModuleID::PBFT, _dstNode, ref(*encodedData), 0, nullptr);
    PBFT_LOG(DEBUG) << LOG_DESC("sendRecoverResponse") << LOG_KV("peer", _dstNode->shortHex())
                    << m_config->printCurrentState();
}

void PBFTEngine::broadcastViewChangeReq()
{
    auto viewChangeReq = generateViewChange();
    // encode and broadcast the viewchangeReq
    auto encodedData = m_config->codec()->encode(viewChangeReq);
    // only broadcast to the consensus nodes
    m_config->frontService()->asyncSendBroadcastMessage(
        bcos::protocol::NodeType::CONSENSUS_NODE, ModuleID::PBFT, ref(*encodedData));
    PBFT_LOG(INFO) << LOG_DESC("broadcastViewChangeReq") << printPBFTMsgInfo(viewChangeReq)
                   << LOG_KV("packetSize", encodedData->size());
    // collect the viewchangeReq
    m_cacheProcessor->addViewChangeReq(viewChangeReq);
    auto newViewMsg = m_cacheProcessor->checkAndTryIntoNewView();
    if (newViewMsg)
    {
        reHandlePrePrepareProposals(newViewMsg);
    }
}

bool PBFTEngine::isValidViewChangeMsg(bcos::crypto::NodeIDPtr _fromNode,
    std::shared_ptr<ViewChangeMsgInterface> _viewChangeMsg, bool _shouldCheckSig)
{
    // check the committed-proposal index
    if (_viewChangeMsg->committedProposal()->index() < m_config->committedProposal()->index())
    {
        PBFT_LOG(INFO) << LOG_DESC("InvalidViewChangeReq: invalid index")
                       << printPBFTMsgInfo(_viewChangeMsg) << m_config->printCurrentState();
        return false;
    }
    if (_fromNode && !isTimeout())
    {
        PBFT_LOG(INFO) << LOG_DESC("sendRecoverResponse to the node try to trigger viewchange")
                       << LOG_KV("dst", _fromNode->shortHex()) << printPBFTMsgInfo(_viewChangeMsg)
                       << m_config->printCurrentState();
        sendRecoverResponse(_fromNode);
    }
    // check the view
    if ((_viewChangeMsg->view() < m_config->view()) ||
        (_viewChangeMsg->view() + 1 < m_config->toView()))
    {
        if (_fromNode && isTimeout())
        {
            PBFT_LOG(INFO) << LOG_DESC("send viewchange to the node whose view falling behind")
                           << LOG_KV("dst", _fromNode->shortHex())
                           << printPBFTMsgInfo(_viewChangeMsg) << m_config->printCurrentState();
            sendViewChange(_fromNode);
        }
    }
    if (_viewChangeMsg->view() < m_config->view())
    {
        return false;
    }
    // check the committed proposal hash
    if (_viewChangeMsg->committedProposal()->index() == m_config->committedProposal()->index() &&
        _viewChangeMsg->committedProposal()->hash() != m_config->committedProposal()->hash())
    {
        PBFT_LOG(WARNING) << LOG_DESC("InvalidViewChangeReq: conflict with local committedProposal")
                          << LOG_DESC(", received proposal: ")
                          << printPBFTProposal(_viewChangeMsg->committedProposal())
                          << LOG_DESC(", local committedProposal:")
                          << printPBFTProposal(m_config->committedProposal());
        return false;
    }
    // check the precommitted proposals
    for (const auto& precommitMsg : _viewChangeMsg->preparedProposals())
    {
        if (precommitMsg->view() > _viewChangeMsg->view())
        {
            PBFT_LOG(INFO) << LOG_DESC("InvalidViewChangeReq for invalid view")
                           << printPBFTMsgInfo(precommitMsg) << printPBFTMsgInfo(_viewChangeMsg)
                           << m_config->printCurrentState();
            return false;
        }
        if (!m_cacheProcessor->checkPrecommitMsg(precommitMsg))
        {
            PBFT_LOG(INFO) << LOG_DESC("InvalidViewChangeReq for invalid proposal")
                           << LOG_KV("viewChangeFrom", _viewChangeMsg->generatedFrom())
                           << printPBFTMsgInfo(precommitMsg) << m_config->printCurrentState();
            return false;
        }
    }
    if (!_shouldCheckSig)
    {
        return true;
    }
    auto ret = checkSignature(_viewChangeMsg);
    if (ret == CheckResult::INVALID)
    {
        PBFT_LOG(INFO) << LOG_DESC("InvalidViewChangeReq: invalid signature")
                       << printPBFTMsgInfo(_viewChangeMsg) << m_config->printCurrentState();
        return false;
    }
    return true;
}

bool PBFTEngine::handleViewChangeMsg(ViewChangeMsgInterface::Ptr _viewChangeMsg)
{
    if (!isValidViewChangeMsg(_viewChangeMsg->from(), _viewChangeMsg))
    {
        return false;
    }
    if (!m_config->timeout() && _viewChangeMsg->from())
    {
        sendRecoverResponse(_viewChangeMsg->from());
    }
    m_cacheProcessor->addViewChangeReq(_viewChangeMsg);
    // try to trigger fast view change if receive more than (f+1) valid view
    // change messages whose view is greater than the current view: sends a
    // view-change message for the smallest view in the set, even if its timer has
    // not expired
    auto leaderIndex =
        m_config->leaderIndexInNewViewPeriod(_viewChangeMsg->index() + 1, _viewChangeMsg->index());
    if (_viewChangeMsg->generatedFrom() == leaderIndex ||
        (m_cacheProcessor->getViewChangeWeight(_viewChangeMsg->view()) >
            m_config->maxFaultyQuorum()))
    {
        auto view = m_cacheProcessor->tryToTriggerFastViewChange();
        if (view > 0)
        {
            // trigger timeout to reach fast view change
            triggerTimeout(false);
        }
    }
    auto newViewMsg = m_cacheProcessor->checkAndTryIntoNewView();
    if (newViewMsg)
    {
        reHandlePrePrepareProposals(newViewMsg);
        return true;
    }
    return true;
}

bool PBFTEngine::isValidNewViewMsg(std::shared_ptr<NewViewMsgInterface> _newViewMsg)
{
    if (_newViewMsg->view() <= m_config->view())
    {
        PBFT_LOG(INFO) << LOG_DESC("InvalidNewViewMsg for invalid view")
                       << printPBFTMsgInfo(_newViewMsg) << m_config->printCurrentState();
        return false;
    }
    // check the viewchange
    uint64_t weight = 0;
    auto viewChangeList = _newViewMsg->viewChangeMsgList();
    for (const auto& viewChangeReq : viewChangeList)
    {
        if (!isValidViewChangeMsg(_newViewMsg->from(), viewChangeReq))
        {
            PBFT_LOG(WARNING) << LOG_DESC("InvalidNewViewMsg for viewChange check failed")
                              << printPBFTMsgInfo(viewChangeReq);
            return false;
        }
        auto* nodeInfo = m_config->getConsensusNodeByIndex(viewChangeReq->generatedFrom());
        if (!nodeInfo)
        {
            continue;
        }
        weight += nodeInfo->voteWeight;
    }
    // TODO: need to ensure the accuracy of local weight parameters
    if (weight < m_config->minRequiredQuorum())
    {
        PBFT_LOG(WARNING) << LOG_DESC("InvalidNewViewMsg for unenough weight")
                          << LOG_KV("weight", weight)
                          << LOG_KV("minRequiredQuorum", m_config->minRequiredQuorum());
        return false;
    }
    // TODO: check the prePrepared message
    auto ret = checkSignature(_newViewMsg);
    return ret != CheckResult::INVALID;
}

bool PBFTEngine::handleNewViewMsg(NewViewMsgInterface::Ptr _newViewMsg)
{
    PBFT_LOG(INFO) << LOG_DESC("handleNewViewMsg: receive newViewChangeMsg")
                   << printPBFTMsgInfo(_newViewMsg) << m_config->printCurrentState();
    if (!isValidNewViewMsg(_newViewMsg))
    {
        return false;
    }
    PBFT_LOG(INFO) << LOG_DESC("handleNewViewMsg success") << printPBFTMsgInfo(_newViewMsg)
                   << m_config->printCurrentState();
    reHandlePrePrepareProposals(_newViewMsg);
    return true;
}

void PBFTEngine::reachNewView(ViewType _view)
{
    m_config->resetNewViewState(_view);
    m_cacheProcessor->resetCacheAfterViewChange(
        m_config->view(), m_config->committedProposal()->index());
    // try to preCommit/commit after no-timeout
    m_cacheProcessor->checkAndPreCommit();
    m_cacheProcessor->checkAndCommit();
    PBFT_LOG(INFO) << LOG_DESC("reachNewView") << m_config->printCurrentState();
    m_cacheProcessor->tryToApplyCommitQueue();
    m_cacheProcessor->tryToCommitStableCheckPoint();
}

void PBFTEngine::reHandlePrePrepareProposals(NewViewMsgInterface::Ptr _newViewReq)
{
    reachNewView(_newViewReq->view());
    // note the sealer to reset after viewchange
    m_config->notifyResetSealing();
    auto const& prePrepareList = _newViewReq->prePrepareList();
    auto maxProposalIndex = m_config->committedProposal()->index();
    auto self = weak_from_this();
    for (const auto& prePrepare : prePrepareList)
    {
        // empty block proposal
        if (!prePrepare->consensusProposal()->data().empty())
        {
            PBFT_LOG(INFO) << LOG_DESC("reHandlePrePrepareProposals: hit the proposal")
                           << printPBFTMsgInfo(prePrepare) << m_config->printCurrentState();
            handlePrePrepareMsg(prePrepare, true, true, false);
            continue;
        }
        if (prePrepare->index() > maxProposalIndex)
        {
            maxProposalIndex = prePrepare->index();
        }
        // hit the cache
        if (m_cacheProcessor->tryToFillProposal(prePrepare))
        {
            PBFT_LOG(INFO) << LOG_DESC(
                                  "reHandlePrePrepareProposals: hit the cache, "
                                  "into prepare phase directly")
                           << printPBFTMsgInfo(prePrepare) << m_config->printCurrentState();
            handlePrePrepareMsg(prePrepare, true, true, false);
            continue;
        }
        // miss the cache, request to from node
        auto* from = m_config->getConsensusNodeByIndex(prePrepare->generatedFrom());
        m_logSync->requestPrecommitData(
            from->nodeID, prePrepare, [self](PBFTMessageInterface::Ptr _prePrepare) {
                auto engine = self.lock();
                if (!engine)
                {
                    return;
                }
                PBFT_LOG(INFO) << LOG_DESC(
                                      "reHandlePrePrepareProposals: get the "
                                      "missed proposal and handle now")
                               << printPBFTMsgInfo(_prePrepare)
                               << engine->m_config->printCurrentState();
                RecursiveGuard lock(engine->m_mutex);
                engine->handlePrePrepareMsg(_prePrepare, true, true, false);
            });
    }
    if (!prePrepareList.empty())
    {
        // Note: in case of the reHandled proposals have system transactions, must
        // wait to reseal until all reHandled proposal committed
        m_config->setWaitResealUntil(maxProposalIndex);
        PBFT_LOG(INFO) << LOG_DESC("reHandlePrePrepareProposals and wait to reseal new proposal")
                       << LOG_KV("waitResealUntil", maxProposalIndex)
                       << m_config->printCurrentState();
    }
    else
    {
        m_config->reNotifySealer(maxProposalIndex + 1);
    }
}

void PBFTEngine::finalizeConsensus(LedgerConfig::Ptr _ledgerConfig, bool _syncedBlock)
{
    RecursiveGuard lock(m_mutex);
    // try to switch rpbft
    switchToRPBFT(_ledgerConfig);
    // resetConfig after submit the block to ledger
    m_config->resetConfig(_ledgerConfig, _syncedBlock);
    m_cacheProcessor->checkAndCommitStableCheckPoint();
    m_cacheProcessor->tryToApplyCommitQueue();
    // tried to commit the stable checkpoint
    m_cacheProcessor->removeConsensusedCache(m_config->view(), _ledgerConfig->blockNumber());
    m_cacheProcessor->tryToCommitStableCheckPoint();
    // Note: only the consensus-triggered finalize should resetTimer
    // resetTimer will try to trigger-fast-viewchange when the leader disconnected or
    // falling-far-behind
    if (!_syncedBlock)
    {
        m_cacheProcessor->resetTimer();
        m_timer->restart();
    }
}

bool PBFTEngine::handleCheckPointMsg(std::shared_ptr<PBFTMessageInterface> _checkPointMsg)
{
    // check index
    if (_checkPointMsg->index() <= m_config->committedProposal()->index())
    {
        PBFT_LOG(TRACE) << LOG_DESC("handleCheckPointMsg: Invalid expired checkpoint msg")
                        << LOG_KV("committedIndex", m_config->committedProposal()->index())
                        << LOG_KV("recvIndex", _checkPointMsg->index())
                        << LOG_KV("hash", _checkPointMsg->hash().abridged())
                        << m_config->printCurrentState();
        return false;
    }
    if (isSyncingHigher())
    {
        PBFT_LOG(INFO) << LOG_DESC(
                              "handleCheckPointMsg: reject the checkPoint for the node is "
                              "syncing higher block")
                       << LOG_KV("committedIndex", m_config->committedProposal()->index())
                       << LOG_KV("recvIndex", _checkPointMsg->index())
                       << LOG_KV("hash", _checkPointMsg->hash().abridged())
                       << LOG_KV("syncingNum", m_config->syncingHighestNumber())
                       << m_config->printCurrentState();
        return false;
    }
    // check signature
    auto result = checkSignature(_checkPointMsg);
    if (result == CheckResult::INVALID)
    {
        PBFT_LOG(WARNING) << LOG_DESC("handleCheckPointMsg: invalid signature")
                          << printPBFTMsgInfo(_checkPointMsg);
        return false;
    }
    // check the proposal signature
    if (!checkProposalSignature(
            _checkPointMsg->generatedFrom(), _checkPointMsg->consensusProposal()))
    {
        PBFT_LOG(WARNING) << LOG_DESC("handleCheckPointMsg: invalid  proposal signature")
                          << printPBFTMsgInfo(_checkPointMsg);
        return false;
    }
    PBFT_LOG(INFO) << LOG_DESC(
                          "handleCheckPointMsg: try to add the checkpoint "
                          "message into the cache")
                   << printPBFTMsgInfo(_checkPointMsg) << m_config->printCurrentState();
    m_cacheProcessor->addCheckPointMsg(_checkPointMsg);
    m_cacheProcessor->tryToApplyCommitQueue();
    m_cacheProcessor->checkAndCommitStableCheckPoint();
    if (m_cacheProcessor->shouldRequestCheckPoint(_checkPointMsg))
    {
        PBFT_LOG(INFO) << LOG_DESC("request checkPoint proposal")
                       << LOG_KV("checkPointIndex", _checkPointMsg->index())
                       << LOG_KV("checkPointHash", _checkPointMsg->hash().abridged())
                       << m_config->printCurrentState();
        m_logSync->requestCommittedProposals(_checkPointMsg->from(), _checkPointMsg->index(), 1);
    }
    return true;
}

void PBFTEngine::handleRecoverResponse(PBFTMessageInterface::Ptr _recoverResponse)
{
    if (checkSignature(_recoverResponse) == CheckResult::INVALID)
    {
        return;
    }
    m_cacheProcessor->addRecoverReqCache(_recoverResponse);
    m_cacheProcessor->checkAndTryToRecover();
}

void PBFTEngine::handleRecoverRequest(PBFTMessageInterface::Ptr _request)
{
    if (checkSignature(_request) == CheckResult::INVALID)
    {
        return;
    }
    sendRecoverResponse(_request->from());
    PBFT_LOG(INFO) << LOG_DESC("handleRecoverRequest and response current state")
                   << LOG_KV("peer", _request->from()->shortHex()) << m_config->printCurrentState();
}

void PBFTEngine::sendCommittedProposalResponse(
    PBFTProposalList const& _proposalList, SendResponseCallback _sendResponse)
{
    auto pbftMessage = m_config->pbftMessageFactory()->createPBFTMsg();
    pbftMessage->setPacketType(PacketType::CommittedProposalResponse);
    pbftMessage->setProposals(_proposalList);
    auto encodedData = m_config->codec()->encode(pbftMessage);
    _sendResponse(ref(*encodedData));
}

void PBFTEngine::onReceiveCommittedProposalRequest(
    PBFTBaseMessageInterface::Ptr _pbftMsg, SendResponseCallback _sendResponse)
{
    RecursiveGuard l(m_mutex);
    auto pbftRequest = std::dynamic_pointer_cast<PBFTRequestInterface>(_pbftMsg);
    PBFT_LOG(INFO) << LOG_DESC("Receive CommittedProposalRequest")
                   << LOG_KV("fromIndex", pbftRequest->index())
                   << LOG_KV("size", pbftRequest->size());
    // hit the local cache
    auto proposal = m_cacheProcessor->fetchPrecommitProposal(pbftRequest->index());
    if (pbftRequest->size() == 1 && proposal)
    {
        PBFTProposalList proposalList;
        proposalList.emplace_back(proposal);
        sendCommittedProposalResponse(proposalList, _sendResponse);
        return;
    }
    auto self = weak_from_this();
    m_config->storage()->asyncGetCommittedProposals(pbftRequest->index(), pbftRequest->size(),
        [self, pbftRequest, _sendResponse](PBFTProposalListPtr _proposalList) {
            auto engine = self.lock();
            if (!engine)
            {
                return;
            }
            // empty case
            if (!_proposalList || _proposalList->size() == 0)
            {
                PBFT_LOG(DEBUG) << LOG_DESC(
                                       "onReceiveCommittedProposalRequest: miss "
                                       "the expected proposal")
                                << LOG_KV("fromIndex", pbftRequest->index())
                                << LOG_KV("size", pbftRequest->size());
                _sendResponse(bytesConstRef());
                return;
            }
            // hit case
            engine->sendCommittedProposalResponse(*_proposalList, _sendResponse);
        });
}

void PBFTEngine::onReceivePrecommitRequest(
    std::shared_ptr<PBFTBaseMessageInterface> _pbftMessage, SendResponseCallback _sendResponse)
{
    RecursiveGuard l(m_mutex);
    // receive the precommitted proposals request message
    auto pbftRequest = std::dynamic_pointer_cast<PBFTRequestInterface>(_pbftMessage);
    // get the local precommitData
    auto precommitMsg =
        m_cacheProcessor->fetchPrecommitData(pbftRequest->index(), pbftRequest->hash());
    if (!precommitMsg)
    {
        PBFT_LOG(INFO) << LOG_DESC("onReceivePrecommitRequest: miss the requested precommit")
                       << LOG_KV("hash", pbftRequest->hash().abridged())
                       << LOG_KV("index", pbftRequest->index());
        return;
    }
    precommitMsg->setPacketType(PacketType::PreparedProposalResponse);
    auto encodedData = m_config->codec()->encode(precommitMsg);
    // response the precommitData
    _sendResponse(ref(*encodedData));
    PBFT_LOG(INFO) << LOG_DESC("Receive precommitRequest and send response")
                   << LOG_KV("hash", pbftRequest->hash().abridged())
                   << LOG_KV("index", pbftRequest->index());
}

void PBFTEngine::onStableCheckPointCommitFailed(
    Error::Ptr&& _error, PBFTProposalInterface::Ptr _stableProposal)
{
    if (!m_config->asMasterNode())
    {
        PBFT_LOG(WARNING) << LOG_DESC(
                                 "onStableCheckPointCommitFailed: but do nothing for the node-self "
                                 "is not a master node")
                          << printPBFTProposal(_stableProposal) << m_config->printCurrentState();
        return;
    }

    if (_stableProposal->index() <= m_config->committedProposal()->index())
    {
        PBFT_LOG(INFO) << LOG_DESC(
                              "onStableCheckPointCommitFailed: drop the expired stable proposal")
                       << m_config->printCurrentState() << printPBFTProposal(_stableProposal);
        return;
    }
    if (_error->errorCode() == bcos::scheduler::SchedulerError::BlockIsCommitting)
    {
        PBFT_LOG(WARNING) << LOG_DESC(
                                 "onStableCheckPointCommitFailed for BlockIsCommitting: "
                                 "retry to commit again")
                          << m_config->printCurrentState() << printPBFTProposal(_stableProposal);
        // retry after 20ms
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        RecursiveGuard l(m_mutex);
        m_cacheProcessor->updateStableCheckPointQueue(_stableProposal);
        return;
    }
    if (_error->errorCode() == bcos::scheduler::SchedulerError::InvalidBlocks)
    {
        PBFT_LOG(WARNING) << LOG_DESC(
                                 "onStableCheckPointCommitFailed for InvalidBlocks: "
                                 "fetchAndUpdateLedgerConfig")
                          << m_config->printCurrentState() << printPBFTProposal(_stableProposal);
        fetchAndUpdateLedgerConfig();
        return;
    }
    clearExceptionProposalState(_stableProposal->index());
}

void PBFTEngine::recoverState()
{
    // Note: only replay the PBFT state when all-modules ready
    PBFT_LOG(INFO) << LOG_DESC("fetch PBFT state");
    auto stateProposals = m_config->storage()->loadState(m_config->committedProposal()->index());
    if (stateProposals && stateProposals->size() > 0)
    {
        initState(*stateProposals, m_config->keyPair()->publicKey());
        auto lowWaterMarkIndex = stateProposals->size() - 1;
        auto lowWaterMark = ((*stateProposals)[lowWaterMarkIndex])->index();
        m_config->setLowWaterMark(lowWaterMark + 1);
        PBFT_LOG(INFO) << LOG_DESC("recoverState")
                       << LOG_KV("stateProposals", stateProposals->size())
                       << LOG_KV("lowWaterMark", lowWaterMark)
                       << LOG_KV("highWaterMark", m_config->highWaterMark());
    }
    m_config->timer()->start();
}

void PBFTEngine::clearExceptionProposalState(bcos::protocol::BlockNumber _number)
{
    try
    {
        if (!m_config->asMasterNode())
        {
            PBFT_LOG(INFO) << LOG_DESC(
                "clearExceptionProposalState return directly for the node is the backup node");
            return;
        }
        RecursiveGuard l(m_mutex);
        if (!m_config->committedProposal())
        {
            PBFT_LOG(WARNING) << LOG_DESC(
                "clearExceptionProposalState return directly for the pbft module has not been "
                "inited");
            return;
        }
        // update the ledgerConfig when switch
        fetchAndUpdateLedgerConfig();
        m_config->timer()->restart();
        m_cacheProcessor->resetUnCommittedCacheState(_number);
        m_config->setExpectedCheckPoint(_number);
        m_cacheProcessor->checkAndPreCommit();
        m_cacheProcessor->checkAndCommit();
        m_cacheProcessor->tryToApplyCommitQueue();
        recoverState();
    }
    catch (std::exception const& e)
    {
        PBFT_LOG(WARNING) << LOG_DESC("clearExceptionProposalState exception")
                          << LOG_KV("number", _number)
                          << LOG_KV("msg", boost::diagnostic_information(e));
    }
}

void PBFTEngine::fetchAndUpdateLedgerConfig()
{
    PBFT_LOG(INFO) << LOG_DESC("fetchAndUpdateLedgerConfig");
    task::syncWait(ledger::getLedgerConfig(*m_ledger, *m_ledgerConfig));
    auto& ledgerConfig = m_ledgerConfig;
    PBFT_LOG(INFO) << LOG_DESC("fetchAndUpdateLedgerConfig success")
                   << LOG_KV("blockNumber", ledgerConfig->blockNumber())
                   << LOG_KV("hash", ledgerConfig->hash().abridged())
                   << LOG_KV("maxTxsPerBlock", ledgerConfig->blockTxCountLimit())
                   << LOG_KV("consensusNodeList", ledgerConfig->consensusNodeList().size());
    m_config->resetConfig(ledgerConfig);
}


void PBFTEngine::switchToRPBFT(const LedgerConfig::Ptr& _ledgerConfig)
{
    // switch to rpbft
    if (_ledgerConfig->features().get(ledger::Features::Flag::feature_rpbft) &&
        this->m_config->consensusType() == ledger::ConsensusType::PBFT_TYPE &&
        this->m_config->rpbftConfigTools() == nullptr) [[unlikely]]
    {
        this->m_config->setRPBFTConfigTools(std::make_shared<RPBFTConfigTools>());
    }
}
bool bcos::consensus::PBFTEngine::shouldRotateSealers(protocol::BlockNumber _number) const
{
    if (m_config->rpbftConfigTools() == nullptr)
    {
        return false;
    }
    return m_config->rpbftConfigTools()->shouldRotateSealers(_number);
}
void bcos::consensus::PBFTEngine::setLedger(ledger::LedgerInterface::Ptr ledger)
{
    m_ledger = std::move(ledger);
}
