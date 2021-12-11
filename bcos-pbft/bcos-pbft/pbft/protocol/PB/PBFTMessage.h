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
 * @file PBFTMessage.h
 * @author: yujiechen
 * @date 2021-04-13
 */
#pragma once
#include "../../config/PBFTConfig.h"
#include "../../interfaces/PBFTMessageInterface.h"
#include "PBFTBaseMessage.h"
#include "bcos-pbft/pbft/protocol/proto/PBFT.pb.h"

namespace bcos
{
namespace consensus
{
class PBFTMessage : public PBFTBaseMessage, public PBFTMessageInterface
{
public:
    using Ptr = std::shared_ptr<PBFTMessage>;
    PBFTMessage()
      : PBFTBaseMessage(),
        m_pbftRawMessage(std::make_shared<PBFTRawMessage>()),
        m_proposals(std::make_shared<PBFTProposalList>())
    {}

    explicit PBFTMessage(std::shared_ptr<PBFTRawMessage> _pbftRawMessage) : PBFTBaseMessage()
    {
        m_pbftRawMessage = _pbftRawMessage;
        m_proposals = std::make_shared<PBFTProposalList>();
        PBFTMessage::deserializeToObject();
    }

    PBFTMessage(bcos::crypto::CryptoSuite::Ptr _cryptoSuite, bytesConstRef _data) : PBFTMessage()
    {
        decodeAndSetSignature(_cryptoSuite, _data);
    }

    ~PBFTMessage() override
    {
        // return back the ownership to m_consensusProposal
        if (m_pbftRawMessage->has_consensusproposal())
        {
            m_pbftRawMessage->unsafe_arena_release_consensusproposal();
        }
        // return the ownership of rawProposal to the passed-in proposal
        auto allocatedProposalSize = m_pbftRawMessage->proposals_size();
        for (int i = 0; i < allocatedProposalSize; i++)
        {
            m_pbftRawMessage->mutable_proposals()->UnsafeArenaReleaseLast();
        }
    }

    std::shared_ptr<PBFTRawMessage> pbftRawMessage() { return m_pbftRawMessage; }
    bytesPointer encode(bcos::crypto::CryptoSuite::Ptr _cryptoSuite,
        bcos::crypto::KeyPairInterface::Ptr _keyPair) const override;
    void decode(bytesConstRef _data) override;

    void setProposals(PBFTProposalList const& _proposals) override;
    PBFTProposalList const& proposals() const override { return *m_proposals; }

    void setConsensusProposal(PBFTProposalInterface::Ptr _consensusProposal) override;
    PBFTProposalInterface::Ptr consensusProposal() override { return m_consensusProposal; }

    virtual void decodeAndSetSignature(
        bcos::crypto::CryptoSuite::Ptr _pbftConfig, bytesConstRef _data);

    bool operator==(PBFTMessage const& _pbftMessage);

    bytesConstRef signatureData() override
    {
        auto const& signatureData = m_pbftRawMessage->signaturedata();
        return bytesConstRef((byte const*)signatureData.data(), signatureData.size());
    }

    bcos::crypto::HashType const& signatureDataHash() override { return m_signatureDataHash; }

    void setSignatureDataHash(bcos::crypto::HashType const& _hash) override
    {
        m_signatureDataHash = _hash;
    }

    PBFTMessageInterface::Ptr populateWithoutProposal() override
    {
        auto pbftMessage = std::make_shared<PBFTMessage>();
        auto const& hashFieldData = m_pbftRawMessage->hashfieldsdata();
        pbftMessage->pbftRawMessage()->set_hashfieldsdata(
            hashFieldData.data(), hashFieldData.size());
        pbftMessage->deserializeToObject();
        return pbftMessage;
    }

    void encodeHashFields() const;
    void deserializeToObject() override;

protected:
    virtual bcos::crypto::HashType getHashFieldsDataHash(
        bcos::crypto::CryptoSuite::Ptr _cryptoSuite) const;
    virtual void generateAndSetSignatureData(bcos::crypto::CryptoSuite::Ptr _cryptoSuite,
        bcos::crypto::KeyPairInterface::Ptr _keyPair) const;

private:
    std::shared_ptr<PBFTRawMessage> m_pbftRawMessage;
    PBFTProposalInterface::Ptr m_consensusProposal;
    PBFTProposalListPtr m_proposals;

    mutable bcos::crypto::HashType m_signatureDataHash;
};
}  // namespace consensus
}  // namespace bcos