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
 * @brief PB implementation for PBFT Message
 * @file PBFTMessage.cpp
 * @author: yujiechen
 * @date 2021-04-13
 */
#include "PBFTMessage.h"
#include "PBFTProposal.h"
#include "bcos-pbft/core/Proposal.h"

using namespace bcos;
using namespace bcos::consensus;
using namespace bcos::crypto;
using namespace bcos::protocol;

bytesPointer PBFTMessage::encode(
    CryptoSuite::Ptr _cryptoSuite, KeyPairInterface::Ptr _keyPair) const
{
    // encode the PBFTBaseMessage
    encodeHashFields();
    generateAndSetSignatureData(_cryptoSuite, _keyPair);
    return encodePBObject(m_pbftRawMessage);
}

void PBFTMessage::encodeHashFields() const
{
    auto hashFieldsData = PBFTBaseMessage::encode();
    m_pbftRawMessage->set_hashfieldsdata(hashFieldsData->data(), hashFieldsData->size());
}

void PBFTMessage::decode(bytesConstRef _data)
{
    decodePBObject(m_pbftRawMessage, _data);
    PBFTMessage::deserializeToObject();
}

void PBFTMessage::deserializeToObject()
{
    auto const& hashFieldsData = m_pbftRawMessage->hashfieldsdata();
    auto baseMessageData =
        bytesConstRef((byte const*)hashFieldsData.c_str(), hashFieldsData.size());
    PBFTBaseMessage::decode(baseMessageData);

    // decode the proposals
    m_proposals->clear();
    if (m_pbftRawMessage->has_consensusproposal())
    {
        auto consensusProposal = m_pbftRawMessage->mutable_consensusproposal();
        std::shared_ptr<PBFTRawProposal> rawConsensusProposal(consensusProposal);
        m_consensusProposal = std::make_shared<PBFTProposal>(rawConsensusProposal);
    }
    for (int i = 0; i < m_pbftRawMessage->proposals_size(); i++)
    {
        std::shared_ptr<PBFTRawProposal> rawProposal(m_pbftRawMessage->mutable_proposals(i));
        m_proposals->push_back(std::make_shared<PBFTProposal>(rawProposal));
    }
}

void PBFTMessage::decodeAndSetSignature(CryptoSuite::Ptr _cryptoSuite, bytesConstRef _data)
{
    decode(_data);
    m_signatureDataHash = getHashFieldsDataHash(_cryptoSuite);
}

void PBFTMessage::setConsensusProposal(PBFTProposalInterface::Ptr _consensusProposal)
{
    m_consensusProposal = _consensusProposal;
    auto pbftProposal = std::dynamic_pointer_cast<PBFTProposal>(_consensusProposal);
    // set committed proposal
    if (m_pbftRawMessage->has_consensusproposal())
    {
        m_pbftRawMessage->unsafe_arena_release_consensusproposal();
    }
    m_pbftRawMessage->unsafe_arena_set_allocated_consensusproposal(
        pbftProposal->pbftRawProposal().get());
}

HashType PBFTMessage::getHashFieldsDataHash(CryptoSuite::Ptr _cryptoSuite) const
{
    auto const& hashFieldsData = m_pbftRawMessage->hashfieldsdata();
    auto hashFieldsDataRef =
        bytesConstRef((byte const*)hashFieldsData.data(), hashFieldsData.size());
    return _cryptoSuite->hash(hashFieldsDataRef);
}

void PBFTMessage::generateAndSetSignatureData(
    CryptoSuite::Ptr _cryptoSuite, KeyPairInterface::Ptr _keyPair) const
{
    m_signatureDataHash = getHashFieldsDataHash(_cryptoSuite);
    auto signature = _cryptoSuite->signatureImpl()->sign(*_keyPair, m_signatureDataHash, false);
    // set the signature data
    m_pbftRawMessage->set_signaturedata(signature->data(), signature->size());
}

void PBFTMessage::setProposals(PBFTProposalList const& _proposals)
{
    *m_proposals = _proposals;
    m_pbftRawMessage->clear_proposals();
    for (auto proposal : _proposals)
    {
        auto proposalImpl = std::dynamic_pointer_cast<PBFTProposal>(proposal);
        assert(proposalImpl);
        m_pbftRawMessage->mutable_proposals()->UnsafeArenaAddAllocated(
            proposalImpl->pbftRawProposal().get());
    }
}

bool PBFTMessage::operator==(PBFTMessage const& _pbftMessage)
{
    if (!PBFTBaseMessage::operator==(_pbftMessage))
    {
        return false;
    }
    // check proposal
    for (size_t i = 0; i < _pbftMessage.proposals().size(); i++)
    {
        auto proposal = std::dynamic_pointer_cast<PBFTProposal>((*m_proposals)[i]);
        auto comparedProposal =
            std::dynamic_pointer_cast<PBFTProposal>((_pbftMessage.proposals())[i]);
        if (*proposal != *comparedProposal)
        {
            return false;
        }
    }
    return true;
}