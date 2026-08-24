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
#include "bcos-pbft/pbft/utilities/PacketTypeDigest.h"
#include <utility>

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
    // Use aliasing shared_ptrs: sub-messages live in m_pbftRawMessage's arena,
    // so we share ownership with m_pbftRawMessage rather than taking ownership
    // from the arena (which would lead to a double-free).
    m_proposals->clear();
    if (m_pbftRawMessage->has_consensusproposal())
    {
        // FIB-121: the aliasing shared_ptr shares m_pbftRawMessage's control block, so the
        // child wrapper keeps the parent protobuf alive and never deletes the submessage
        // itself -- no dual-ownership, no destructor release dance.
        auto* rawPtr = m_pbftRawMessage->mutable_consensusproposal();
        std::shared_ptr<PBFTRawProposal> rawConsensusProposal(m_pbftRawMessage, rawPtr);
        m_consensusProposal = std::make_shared<PBFTProposal>(rawConsensusProposal);
    }
    for (int i = 0; i < m_pbftRawMessage->proposals_size(); i++)
    {
        auto* rawPtr = m_pbftRawMessage->mutable_proposals(i);
        std::shared_ptr<PBFTRawProposal> rawProposal(m_pbftRawMessage, rawPtr);
        m_proposals->push_back(std::make_shared<PBFTProposal>(rawProposal));
    }
}

void PBFTMessage::decodeAndSetSignature(CryptoSuite::Ptr _cryptoSuite, bytesConstRef _data)
{
    decode(_data);
    m_signatureDataHash = getHashFieldsDataHash(std::move(_cryptoSuite));
}

void PBFTMessage::setConsensusProposal(PBFTProposalInterface::Ptr _consensusProposal)
{
    auto pbftProposal = std::dynamic_pointer_cast<PBFTProposal>(_consensusProposal);
    // FIB-121: deep-copy the caller's proposal into our own protobuf, then expose an
    // aliasing wrapper over that copy. Nothing is shared with the caller, and proofs
    // later appended via consensusProposal() (e.g. PBFTCache::intoPrecommit ->
    // setSignatureList) write through to m_pbftRawMessage and survive encode().
    m_pbftRawMessage->mutable_consensusproposal()->CopyFrom(*pbftProposal->pbftRawProposal());
    m_consensusProposal = std::make_shared<PBFTProposal>(std::shared_ptr<PBFTRawProposal>(
        m_pbftRawMessage, m_pbftRawMessage->mutable_consensusproposal()));
}

HashType PBFTMessage::getHashFieldsDataHash(CryptoSuite::Ptr _cryptoSuite) const
{
    auto const& hashFieldsData = m_pbftRawMessage->hashfieldsdata();
    auto hashFieldsDataRef =
        bytesConstRef((byte const*)hashFieldsData.data(), hashFieldsData.size());
    // FIB-134: dual-mode digest — receiver-side branch on message version.
    return PacketTypeDigest::compute(
        version(), packetType(), hashFieldsDataRef, _cryptoSuite->hashImpl());
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
    // FIB-121: keep the caller's wrappers in the member list (identity + in-memory fields
    // preserved, exactly as before) and deep-copy each into our protobuf for encode. The
    // protobuf owns its copies normally, so there is no borrowed raw pointer to release in
    // the destructor (was UnsafeArenaAddAllocated borrow + ~PBFTMessage release).
    *m_proposals = _proposals;
    m_pbftRawMessage->clear_proposals();
    for (const auto& proposal : _proposals)
    {
        auto proposalImpl = std::dynamic_pointer_cast<PBFTProposal>(proposal);
        assert(proposalImpl);
        m_pbftRawMessage->add_proposals()->CopyFrom(*proposalImpl->pbftRawProposal());
    }
}

bool PBFTMessage::operator==(PBFTMessage const& _pbftMessage) const
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