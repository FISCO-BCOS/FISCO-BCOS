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
#include <bcos-framework/interfaces/protocol/Protocol.h>

using namespace bcos;
using namespace bcos::front;
using namespace bcos::protocol;
using namespace bcos::crypto;
using namespace bcos::consensus;

PBFTLogSync::PBFTLogSync(PBFTConfig::Ptr _config, PBFTCacheProcessor::Ptr _pbftCache)
  : m_config(_config),
    m_pbftCache(_pbftCache),
    m_requestThread(std::make_shared<ThreadPool>("pbftLogSync", 1))
{}

void PBFTLogSync::requestCommittedProposals(
    PublicPtr _from, BlockNumber _startIndex, size_t _offset)
{
    auto pbftRequest = m_config->pbftMessageFactory()->populateFrom(
        PacketType::CommittedProposalRequest, _startIndex, _offset);
    requestPBFTData(_from, pbftRequest,
        [this, _startIndex, _offset](Error::Ptr _error, NodeIDPtr _nodeID, bytesConstRef _data,
            std::string const&, SendResponseCallback _sendResponse) {
            return this->onRecvCommittedProposalsResponse(
                _error, _nodeID, _data, _startIndex, _offset, _sendResponse);
        });
}

void PBFTLogSync::requestPrecommitData(bcos::crypto::PublicPtr _from,
    PBFTMessageInterface::Ptr _prePrepareMsg, HandlePrePrepareCallback _prePrepareCallback)
{
    auto pbftRequest = m_config->pbftMessageFactory()->populateFrom(
        PacketType::PreparedProposalRequest, _prePrepareMsg->index(), _prePrepareMsg->hash());
    PBFT_LOG(INFO) << LOG_DESC("request the missed precommit proposal")
                   << LOG_KV("index", _prePrepareMsg->index())
                   << LOG_KV("hash", _prePrepareMsg->hash().abridged());
    requestPBFTData(_from, pbftRequest,
        [this, _prePrepareMsg, _prePrepareCallback](Error::Ptr _error, NodeIDPtr _nodeID,
            bytesConstRef _data, std::string const&, SendResponseCallback _sendResponse) {
            return this->onRecvPrecommitResponse(
                _error, _nodeID, _data, _prePrepareMsg, _prePrepareCallback, _sendResponse);
        });
}

void PBFTLogSync::requestPBFTData(
    PublicPtr _from, PBFTRequestInterface::Ptr _pbftRequest, CallbackFunc _callback)
{
    auto self = std::weak_ptr<PBFTLogSync>(shared_from_this());
    m_requestThread->enqueue([self, _from, _pbftRequest, _callback]() {
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
            // send the request
            config->frontService()->asyncSendMessageByNodeID(ModuleID::PBFT, _from,
                ref(*encodedData), config->networkTimeoutInterval(), _callback);
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
                              << LOG_KV("error", boost::diagnostic_information(e));
        }
    });
}

void PBFTLogSync::onRecvCommittedProposalsResponse(Error::Ptr _error, NodeIDPtr _nodeID,
    bytesConstRef _data, bcos::protocol::BlockNumber _startIndex, size_t _offset,
    SendResponseCallback)
{
    if (_error)
    {
        PBFT_LOG(WARNING) << LOG_DESC("onRecvCommittedProposalResponse error")
                          << LOG_KV("from", _nodeID->shortHex())
                          << LOG_KV("errorCode", _error->errorCode())
                          << LOG_KV("errorMsg", _error->errorMessage());
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
    // TODO: check the proposal to ensure security
    // load the fetched checkpoint proposal into the cache
    auto proposals = proposalResponse->proposals();
    m_pbftCache->initState(proposals, _nodeID);
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
        PBFT_LOG(WARNING) << LOG_DESC("onRecvPrecommitResponse error")
                          << LOG_KV("from", _nodeID->shortHex())
                          << LOG_KV("errorCode", _error->errorCode())
                          << LOG_KV("errorMsg", _error->errorMessage());
    }
    auto response = m_config->codec()->decode(_data);
    if (response->packetType() != PacketType::PreparedProposalResponse)
    {
        return;
    }
    PBFT_LOG(INFO) << LOG_DESC("onRecvPrecommitResponse") << printPBFTMsgInfo(response);
    auto pbftMessage = std::dynamic_pointer_cast<ViewChangeMsgInterface>(response);
    assert(pbftMessage->preparedProposals().size() == 1);
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