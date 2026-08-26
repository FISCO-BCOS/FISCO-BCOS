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
 * @brief implementation for PBFT log syncing
 * @file PBFTLogSync.cpp
 * @author: yujiechen
 * @date 2021-04-28
 */
#include "PBFTLogSync.h"
#include <bcos-framework/protocol/CommonError.h>
#include <bcos-framework/protocol/Protocol.h>
#include <bcos-task/Wait.h>
#include <utility>

using namespace bcos;
using namespace bcos::front;
using namespace bcos::protocol;
using namespace bcos::crypto;
using namespace bcos::consensus;

PBFTLogSync::PBFTLogSync(PBFTConfig::Ptr _config, PBFTCacheProcessor::Ptr _pbftCache,
    bcos::IOServicePool::Ptr _ioServicePool)
  : m_config(std::move(_config)),
    m_pbftCache(std::move(_pbftCache)),
    m_strand(std::move(_ioServicePool))
{}

void PBFTLogSync::requestCommittedProposals(
    PublicPtr _from, bcos::protocol::BlockNumber _startIndex, size_t _offset)
{
    auto pbftRequest = m_config->pbftMessageFactory()->populateFrom(
        PacketType::CommittedProposalRequest, _startIndex, _offset);
    auto self = weak_from_this();
    requestPBFTData(std::move(_from), pbftRequest,
        [self, _startIndex, _offset](Error::Ptr _error, NodeIDPtr _nodeID, bytesConstRef _data,
            std::string const&, SendResponseCallback _sendResponse) {
            auto logSync = self.lock();
            if (!logSync)
            {
                return;
            }
            return logSync->onRecvCommittedProposalsResponse(std::move(_error), std::move(_nodeID),
                _data, _startIndex, _offset, std::move(_sendResponse));
        });
}

// new view
void PBFTLogSync::requestPrecommitData(bcos::crypto::PublicPtr _from,
    PBFTMessageInterface::Ptr _prePrepareMsg, HandlePrePrepareCallback _prePrepareCallback)
{
    auto pbftRequest = m_config->pbftMessageFactory()->populateFrom(
        PacketType::PreparedProposalRequest, _prePrepareMsg->index(), _prePrepareMsg->hash());
    PBFT_LOG(INFO) << LOG_DESC("request the missed precommit proposal")
                   << LOG_KV("index", _prePrepareMsg->index())
                   << LOG_KV("hash", _prePrepareMsg->hash().abridged());
    auto self = weak_from_this();
    requestPBFTData(std::move(_from), pbftRequest,
        [self, _prePrepareMsg = std::move(_prePrepareMsg),
            _prePrepareCallback = std::move(_prePrepareCallback)](Error::Ptr _error,
            NodeIDPtr _nodeID, bytesConstRef _data, std::string const&,
            SendResponseCallback _sendResponse) {
            auto logSync = self.lock();
            if (!logSync)
            {
                return;
            }
            return logSync->onRecvPrecommitResponse(std::move(_error), std::move(_nodeID), _data,
                _prePrepareMsg, _prePrepareCallback, std::move(_sendResponse));
        });
}

void PBFTLogSync::requestPBFTData(
    PublicPtr _from, PBFTRequestInterface::Ptr _pbftRequest, CallbackFunc _callback)
{
    auto self = weak_from_this();
    m_strand.post([self, _from, _pbftRequest, _callback]() {
        try
        {
            auto pbftLogSync = self.lock();
            if (!pbftLogSync)
            {
                return;
            }
            auto config = pbftLogSync->m_config;
            // encode
            auto encodedData =
                config->codec()->encode(_pbftRequest, config->pbftMsgDefaultVersion());
            // owned payload + coroutine fast path -> zero-copy; co_await waits for the module-level
            // response (uuid-matched) and the result is delivered to _callback. All state is passed
            // as coroutine parameters so it is copied into the frame and stays alive for the whole
            // (possibly deferred) send.
            auto front = config->frontService();
            auto networkTimeout = config->networkTimeoutInterval();
            task::wait([](decltype(front) _front, decltype(_from) _from,
                           decltype(encodedData) _encodedData, decltype(networkTimeout) _networkTimeout,
                           decltype(_callback) _callback) mutable -> task::Task<void> {
                try
                {
                    auto result = co_await _front->sendMessageByNodeID(ModuleID::PBFT, _from,
                        ::ranges::views::single(ref(*_encodedData)), _networkTimeout);
                    if (_callback)
                    {
                        // deliver the module-level response through the caller's callback
                        // (result.payload is owned by this frame and valid for the callback)
                        _callback(std::move(result.error), std::move(result.nodeID),
                            bcos::ref(result.payload), result.uuid, std::move(result.respond));
                    }
                }
                catch (std::exception const& e)
                {
                    // restore the base "a send failure still yields a callback" contract: under the
                    // new coroutine shape the front timeout can no longer rescue a callback that
                    // was never registered because the send threw before registration.
                    PBFT_LOG(WARNING) << LOG_DESC("requestPBFTData send exception")
                                      << LOG_KV("to", _from->shortHex())
                                      << LOG_KV("message", boost::diagnostic_information(e));
                    if (_callback)
                    {
                        _callback(BCOS_ERROR_PTR(CommonError::FetchTransactionsFailed,
                                      "requestPBFTData exception: " +
                                          boost::diagnostic_information(e)),
                            _from, bytesConstRef(), std::string(), ResponseFunc());
                    }
                }
            }(front, _from, std::move(encodedData), networkTimeout, _callback));
            PBFT_LOG(INFO) << LOG_DESC("request the missed precommit proposal")
                           << LOG_KV("peer", _from->shortHex())
                           << LOG_KV("index", _pbftRequest->index())
                           << LOG_KV("hash", _pbftRequest->hash().abridged());
        }
        catch (std::exception const& e)
        {
            PBFT_LOG(WARNING) << LOG_DESC("requestCommittedProposals exception")
                              << LOG_KV("to", _from->shortHex())
                              << LOG_KV("startIndex", _pbftRequest->index())
                              << LOG_KV("offset", _pbftRequest->size())
                              << LOG_KV("hash", _pbftRequest->hash().abridged())
                              << LOG_KV("message", boost::diagnostic_information(e));
        }
    });
}

void PBFTLogSync::onRecvCommittedProposalsResponse(Error::Ptr _error, NodeIDPtr _nodeID,
    bytesConstRef _data, bcos::protocol::BlockNumber _startIndex, size_t _offset,
    SendResponseCallback)
{
    if (_error)
    {
        PBFT_LOG(WARNING) << LOG_DESC("onRecvCommittedProposalResponse failed")
                          << LOG_KV("from", _nodeID->shortHex())
                          << LOG_KV("code", _error->errorCode())
                          << LOG_KV("msg", _error->errorMessage());
        for (size_t i = 0; i < _offset; i++)
        {
            m_pbftCache->eraseCommittedProposalList(_startIndex + i);
        }
        return;
    }
    if (_data.size() == 0)
    {
        return;
    }
    auto response = m_config->codec()->decode(_data);
    if (response->packetType() != PacketType::CommittedProposalResponse)
    {
        return;
    }
    auto proposalResponse = std::dynamic_pointer_cast<PBFTMessageInterface>(response);
    // FIB-127: verify signature proofs on each recovered proposal before loading into cache.
    // An untrusted peer could otherwise inject arbitrary committed-proposal data, or repeat
    // the same (sealerIdx, sig) pair to inflate vote weight past minRequiredQuorum().
    // PBFTConfig::verifyProposalQuorumSignatures performs all required checks (non-empty
    // proof list, per-sealer dedup, signature verify, quorum weight).
    auto proposals = proposalResponse->proposals();
    PBFTProposalList validProposals;
    for (const auto& proposal : proposals)
    {
        if (!m_config->verifyProposalQuorumSignatures(proposal))
        {
            PBFT_LOG(WARNING) << LOG_DESC(
                                     "onRecvCommittedProposalsResponse: drop proposal failing "
                                     "quorum-signature verification (FIB-127)")
                              << LOG_KV("from", _nodeID->shortHex())
                              << LOG_KV("index", proposal->index())
                              << LOG_KV("hash", proposal->hash().abridged())
                              << LOG_KV("required", m_config->minRequiredQuorum());
            continue;
        }
        validProposals.push_back(proposal);
    }
    // load the fetched checkpoint proposals into the cache
    m_pbftCache->initState(validProposals, _nodeID);
    PBFT_LOG(INFO) << LOG_DESC("onRecvCommittedProposalsResponse")
                   << LOG_KV("from", _nodeID->shortHex())
                   << LOG_KV("proposalSize", proposals.size());
}

void PBFTLogSync::onRecvPrecommitResponse(Error::Ptr _error, bcos::crypto::NodeIDPtr _nodeID,
    bytesConstRef _data, PBFTMessageInterface::Ptr _prePrepareMsg,
    HandlePrePrepareCallback _prePrepareCallback, SendResponseCallback)
{
    if (_error != nullptr)
    {
        PBFT_LOG(WARNING) << LOG_DESC("onRecvPrecommitResponse failed")
                          << LOG_KV("from", _nodeID->shortHex())
                          << LOG_KV("code", _error->errorCode())
                          << LOG_KV("msg", _error->errorMessage());
    }
    auto response = m_config->codec()->decode(_data);
    if (response->packetType() != PacketType::PreparedProposalResponse)
    {
        return;
    }
    PBFT_LOG(INFO) << LOG_DESC("onRecvPrecommitResponse") << printPBFTMsgInfo(response);
    auto pbftMessage = std::dynamic_pointer_cast<ViewChangeMsgInterface>(response);
    if (pbftMessage->preparedProposals().size() != 1)
    {
        PBFT_LOG(WARNING) << LOG_DESC("onRecvPrecommitResponse: invalid preparedProposals size")
                          << LOG_KV("expected", 1)
                          << LOG_KV("actual", pbftMessage->preparedProposals().size())
                          << LOG_KV("from", _nodeID->shortHex());
        return;
    }
    auto precommitMsg = (pbftMessage->preparedProposals())[0];
    if (!precommitMsg->consensusProposal())
    {
        return;
    }
    if (precommitMsg->consensusProposal()->index() !=
            _prePrepareMsg->consensusProposal()->index() ||
        precommitMsg->consensusProposal()->hash() != _prePrepareMsg->consensusProposal()->hash())
    {
        return;
    }
    if (!m_pbftCache->checkPrecommitMsg(precommitMsg))
    {
        PBFT_LOG(WARNING) << LOG_DESC("Recv invalid precommit response")
                          << printPBFTMsgInfo(precommitMsg);
        return;
    }
    _prePrepareMsg->consensusProposal()->setData(precommitMsg->consensusProposal()->data());
    _prePrepareCallback(_prePrepareMsg);
}