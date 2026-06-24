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
 * @brief factory for PBFTMessage
 * @file PBFTMessageFactory.h
 * @author: yujiechen
 * @date 2021-04-20
 */
#pragma once
// FIB-121: the single-impl message/proposal interfaces were collapsed into the
// concrete PB types; the factory now creates and returns those directly.
#include "bcos-pbft/pbft/protocol/PB/PBFTMessage.h"
#include "bcos-pbft/pbft/protocol/PB/PBFTNewViewMsg.h"
#include "bcos-pbft/pbft/protocol/PB/PBFTProposal.h"
#include "bcos-pbft/pbft/protocol/PB/PBFTRequest.h"
#include "bcos-pbft/pbft/protocol/PB/PBFTViewChangeMsg.h"
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-framework/protocol/ProtocolTypeDef.h>
namespace bcos
{
namespace consensus
{
class PBFTMessageFactory
{
public:
    using Ptr = std::shared_ptr<PBFTMessageFactory>;
    PBFTMessageFactory() = default;
    virtual ~PBFTMessageFactory() {}

    virtual PBFTMessage::Ptr createPBFTMsg() = 0;
    virtual PBFTMessage::Ptr createPBFTMsg(
        bcos::crypto::CryptoSuite::Ptr _cryptoSuite, bytesConstRef _data) = 0;

    virtual PBFTViewChangeMsg::Ptr createViewChangeMsg() = 0;
    virtual PBFTViewChangeMsg::Ptr createViewChangeMsg(bytesConstRef _data) = 0;

    virtual PBFTNewViewMsg::Ptr createNewViewMsg() = 0;
    virtual PBFTNewViewMsg::Ptr createNewViewMsg(bytesConstRef _data) = 0;
    virtual PBFTProposal::Ptr createPBFTProposal() = 0;
    virtual PBFTProposal::Ptr createPBFTProposal(bytesConstRef _data) = 0;

    virtual PBFTRequest::Ptr createPBFTRequest() = 0;
    virtual PBFTRequest::Ptr createPBFTRequest(bytesConstRef _data) = 0;

    virtual PBFTRequest::Ptr populateFrom(
        PacketType _packetType, bcos::protocol::BlockNumber _startIndex, int64_t _offset)
    {
        auto pbftRequest = createPBFTRequest();
        pbftRequest->setPacketType(_packetType);
        pbftRequest->setIndex(_startIndex);
        pbftRequest->setSize(_offset);
        return pbftRequest;
    }

    virtual PBFTRequest::Ptr populateFrom(PacketType _packetType,
        bcos::protocol::BlockNumber _index, bcos::crypto::HashType const& _hash)
    {
        auto pbftRequest = createPBFTRequest();
        pbftRequest->setPacketType(_packetType);
        pbftRequest->setIndex(_index);
        pbftRequest->setHash(_hash);
        return pbftRequest;
    }

    // _proposal is taken by non-const ref because ProposalInterface::sealerId()
    // (a framework virtual we do not change) is non-const.
    virtual PBFTMessage::Ptr populateFrom(PacketType _packetType, int32_t _version, ViewType _view,
        int64_t _timestamp, IndexType _generatedFrom, PBFTProposal& _proposal,
        bcos::crypto::CryptoSuite::Ptr _cryptoSuite, bcos::crypto::KeyPairInterface::Ptr _keyPair,
        bool _needSign = true)
    {
        auto pbftMessage = createPBFTMsg();
        pbftMessage->setPacketType(_packetType);
        pbftMessage->setVersion(_version);
        pbftMessage->setView(_view);
        pbftMessage->setTimestamp(_timestamp);
        pbftMessage->setGeneratedFrom(_generatedFrom);
        pbftMessage->setHash(_proposal.hash());
        pbftMessage->setIndex(_proposal.index());
        // build the signed consensus proposal in place
        auto* signedProposal = pbftMessage->mutableConsensusProposal();
        signedProposal->setIndex(_proposal.index());
        signedProposal->setHash(_proposal.hash());
        signedProposal->setSealerId(_proposal.sealerId());
        if (_needSign)
        {
            auto signatureData = _cryptoSuite->signatureImpl()->sign(*_keyPair, _proposal.hash());
            signedProposal->setSignature(*signatureData);
        }
        return pbftMessage;
    }

    virtual PBFTMessage::Ptr populateFrom(PacketType _packetType, PBFTProposal& _proposal,
        int32_t _version, ViewType _view, int64_t _timestamp, IndexType _generatedFrom)
    {
        auto pbftMessage = createPBFTMsg();
        pbftMessage->setPacketType(_packetType);
        pbftMessage->setVersion(_version);
        pbftMessage->setView(_view);
        pbftMessage->setTimestamp(_timestamp);
        pbftMessage->setGeneratedFrom(_generatedFrom);
        pbftMessage->setHash(_proposal.hash());
        pbftMessage->setIndex(_proposal.index());
        pbftMessage->setConsensusProposal(_proposal);
        return pbftMessage;
    }

    virtual PBFTProposal::Ptr populateFrom(
        PBFTProposal& _proposal, bool _withData = true, bool _withProof = true)
    {
        auto proposal = createPBFTProposal();
        proposal->setIndex(_proposal.index());
        proposal->setHash(_proposal.hash());
        proposal->setSealerId(_proposal.sealerId());
        if (_withData)
        {
            proposal->setData(_proposal.data());
        }
        // set the signature proof
        if (_withProof)
        {
            auto signatureSize = _proposal.signatureProofSize();
            for (size_t i = 0; i < signatureSize; i++)
            {
                auto proof = _proposal.signatureProof(i);
                proposal->appendSignatureProof(proof.first, proof.second);
            }
        }
        return proposal;
    }
};
}  // namespace consensus
}  // namespace bcos
