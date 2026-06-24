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
    return encodePBObject(m_active);
}

void PBFTMessage::encodeHashFields() const
{
    auto hashFieldsData = PBFTBaseMessage::encode();
    m_active->set_hashfieldsdata(hashFieldsData->data(), hashFieldsData->size());
}

void PBFTMessage::decode(bytesConstRef _data)
{
    decodePBObject(m_active, _data);
    PBFTMessage::deserializeToObject();
}

void PBFTMessage::deserializeToObject()
{
    auto const& hashFieldsData = m_active->hashfieldsdata();
    auto baseMessageData =
        bytesConstRef((byte const*)hashFieldsData.c_str(), hashFieldsData.size());
    PBFTBaseMessage::decode(baseMessageData);

    // FIB-121: re-point the consensusProposal / proposals views at m_active
    // (zero-copy, no shared_ptr borrow, no destructor release dance).
    rebuildProposalViews();
}

void PBFTMessage::decodeAndSetSignature(CryptoSuite::Ptr _cryptoSuite, bytesConstRef _data)
{
    decode(_data);
    m_signatureDataHash = getHashFieldsDataHash(std::move(_cryptoSuite));
}

void PBFTMessage::setConsensusProposal(PBFTProposal const& _consensusProposal)
{
    // FIB-121: adopt by deep copy into our own protobuf (NOT alias/release). The
    // source is caller-owned (often cache-held) and must stay intact; CopyFrom
    // gives us an independent copy, then the view writes through for any proof
    // appended afterwards (PBFTCache::intoPrecommit pattern).
    m_active->mutable_consensusproposal()->CopyFrom(*_consensusProposal.pbftRawProposal());
    m_hasConsensusProposal = true;
    m_consensusProposal = PBFTProposal::asView(m_active->mutable_consensusproposal());
}

PBFTProposal* PBFTMessage::mutableConsensusProposal()
{
    m_hasConsensusProposal = true;
    m_consensusProposal = PBFTProposal::asView(m_active->mutable_consensusproposal());
    return &m_consensusProposal;
}

HashType PBFTMessage::getHashFieldsDataHash(CryptoSuite::Ptr _cryptoSuite) const
{
    auto const& hashFieldsData = m_active->hashfieldsdata();
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
    m_active->set_signaturedata(signature->data(), signature->size());
}

void PBFTMessage::setProposals(PBFTProposalList const& _proposals)
{
    m_active->clear_proposals();
    for (auto const& proposal : _proposals)
    {
        // deep copy each proposal's protobuf into our repeated field
        m_active->add_proposals()->CopyFrom(*proposal.pbftRawProposal());
    }
    // rebuild views only after the repeated field is fully populated (Add() may
    // have reallocated the backing array, invalidating earlier element pointers)
    rebuildProposalViews();
}

bool PBFTMessage::operator==(PBFTMessage const& _pbftMessage) const
{
    if (!PBFTBaseMessage::operator==(_pbftMessage))
    {
        return false;
    }
    // check proposal
    if (m_proposals.size() != _pbftMessage.m_proposals.size())
    {
        return false;
    }
    for (size_t i = 0; i < m_proposals.size(); i++)
    {
        if (m_proposals[i] != _pbftMessage.m_proposals[i])
        {
            return false;
        }
    }
    return true;
}
