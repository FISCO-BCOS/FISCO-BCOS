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
#include "PBFTBaseMessage.h"
#include "PBFTProposal.h"
#include "bcos-pbft/pbft/protocol/proto/PBFT.pb.h"
#include <memory>

namespace bcos::consensus
{
/**
 * This class is thread-unsafe, should never be writen in multi-thread.
 *
 * FIB-121: value + non-owning-view message (the single-impl PBFTMessageInterface
 * was collapsed in). STANDALONE owns its PBFTRawMessage on the heap; VIEW windows
 * onto a parent message's submessage (a preparedProposals / prePrepareList
 * element). m_consensusProposal / m_proposals are value PBFTProposal views into
 * m_active, so a proof appended after setConsensusProposal still writes through
 * to the protobuf and survives encode. m_owned is a unique_ptr (stable pointee)
 * so the defaulted moves keep all views valid across vector reallocation.
 */
class PBFTMessage : public PBFTBaseMessage
{
public:
    using Ptr = std::shared_ptr<PBFTMessage>;
    PBFTMessage()
      : PBFTBaseMessage(),
        m_pbftRawMessage(std::make_unique<PBFTRawMessage>()),
        m_active(m_pbftRawMessage.get())
    {
        rebuildProposalViews();
    }

    explicit PBFTMessage(bytesConstRef _data)
      : PBFTBaseMessage(),
        m_pbftRawMessage(std::make_unique<PBFTRawMessage>()),
        m_active(m_pbftRawMessage.get())
    {
        decode(_data);
    }

    PBFTMessage(bcos::crypto::CryptoSuite::Ptr _cryptoSuite, bytesConstRef _data) : PBFTMessage()
    {
        decodeAndSetSignature(std::move(_cryptoSuite), _data);
    }

    // Non-owning view onto a parent-owned submessage (preparedProposals /
    // prePrepareList element). The parent protobuf must outlive this view.
    static PBFTMessage asView(PBFTRawMessage* _sub) { return PBFTMessage(ViewTag{}, _sub); }

    // Copy deep-copies into a fresh STANDALONE; move keeps the views valid (the
    // unique_ptr pointee is stable, so m_active and sub-views do not dangle).
    PBFTMessage(PBFTMessage const& _other)
      : PBFTBaseMessage(_other),
        m_pbftRawMessage(std::make_unique<PBFTRawMessage>(*_other.m_active)),
        m_active(m_pbftRawMessage.get()),
        m_signatureDataHash(_other.m_signatureDataHash)
    {
        rebuildProposalViews();
    }
    PBFTMessage& operator=(PBFTMessage const& _other)
    {
        PBFTMessage tmp(_other);
        *this = std::move(tmp);
        return *this;
    }
    PBFTMessage(PBFTMessage&&) noexcept = default;
    PBFTMessage& operator=(PBFTMessage&&) noexcept = default;

    ~PBFTMessage() override = default;

    PBFTRawMessage* pbftRawMessage() { return m_active; }
    PBFTRawMessage const* pbftRawMessage() const { return m_active; }

    bytesPointer encode(bcos::crypto::CryptoSuite::Ptr _cryptoSuite,
        bcos::crypto::KeyPairInterface::Ptr _keyPair) const override;
    void decode(bytesConstRef _data) override;

    void setProposals(PBFTProposalList const& _proposals);
    std::vector<PBFTProposal> const& proposals() const { return m_proposals; }

    void setConsensusProposal(PBFTProposal const& _consensusProposal);
    // Build-in-place: returns a writable view; the caller fills it through the
    // returned pointer (used by PBFTMessageFactory::populateFrom).
    PBFTProposal* mutableConsensusProposal();
    // Nullable observer (returns nullptr when the field is absent).
    // LIFETIME: the returned pointer aliases a value member that views into this
    // message's protobuf — valid only while this PBFTMessage is alive. Do NOT
    // store it past the owning message's lifetime; for an owning handle (e.g.
    // across an async hop or in a queue) copy it:
    // std::make_shared<PBFTProposal>(*msg->consensusProposal()).
    PBFTProposal* consensusProposal()
    {
        return m_hasConsensusProposal ? &m_consensusProposal : nullptr;
    }
    PBFTProposal const* consensusProposal() const
    {
        return m_hasConsensusProposal ? &m_consensusProposal : nullptr;
    }

    virtual void decodeAndSetSignature(
        bcos::crypto::CryptoSuite::Ptr _pbftConfig, bytesConstRef _data);

    bool operator==(PBFTMessage const& _pbftMessage) const;

    bytesConstRef signatureData() override
    {
        auto const& signatureData = m_active->signaturedata();
        return bytesConstRef((byte const*)signatureData.data(), signatureData.size());
    }

    bcos::crypto::HashType const& signatureDataHash() override { return m_signatureDataHash; }

    void setSignatureDataHash(bcos::crypto::HashType const& _hash) override
    {
        m_signatureDataHash = _hash;
    }

    PBFTMessage::Ptr populateWithoutProposal()
    {
        auto pbftMessage = std::make_shared<PBFTMessage>();
        encodeHashFields();
        auto const& hashFieldData = m_active->hashfieldsdata();
        pbftMessage->pbftRawMessage()->set_hashfieldsdata(
            hashFieldData.data(), hashFieldData.size());
        pbftMessage->deserializeToObject();
        return pbftMessage;
    }

    void encodeHashFields() const;
    void deserializeToObject() override;

    // FIB-134: re-derive the inner signature digest using the current packetType.
    // Required after PBFTCodec::decode sets the wire packetType, because the digest
    // formula under wire-version >= 1 binds packetType into the hash.
    // Takes CryptoSuite by const& because the codec keeps ownership of m_cryptoSuite;
    // a by-value param would copy the shared_ptr at every call without enabling a move.
    virtual void refreshSignatureDataHash(bcos::crypto::CryptoSuite::Ptr const& _cryptoSuite)
    {
        m_signatureDataHash = getHashFieldsDataHash(_cryptoSuite);
    }

    std::string toDebugString() const override
    {
        std::stringstream stringstream;
        stringstream << LOG_KV("type", m_packetType)
                     << LOG_KV("fromNode", m_from ? m_from->shortHex() : "null")
                     << LOG_KV("rawMsgProposalsSize", m_active ? m_active->proposals_size() : 0)
                     << LOG_KV("consensusProposal", m_hasConsensusProposal ?
                                                        printPBFTProposal(&m_consensusProposal) :
                                                        "null");

        return stringstream.str();
    }

protected:
    virtual bcos::crypto::HashType getHashFieldsDataHash(
        bcos::crypto::CryptoSuite::Ptr _cryptoSuite) const;
    virtual void generateAndSetSignatureData(bcos::crypto::CryptoSuite::Ptr _cryptoSuite,
        bcos::crypto::KeyPairInterface::Ptr _keyPair) const;

private:
    struct ViewTag
    {
    };
    PBFTMessage(ViewTag, PBFTRawMessage* _sub)
      : PBFTBaseMessage(), m_pbftRawMessage(nullptr), m_active(_sub)
    {
        deserializeToObject();
    }

    // Re-create the consensusProposal / proposals views over m_active. Must run
    // AFTER the protobuf's repeated `proposals` field is fully populated, since
    // RepeatedPtrField::Add() may reallocate and invalidate earlier element
    // pointers (FIB-121 view-revalidation invariant).
    void rebuildProposalViews()
    {
        m_hasConsensusProposal = m_active->has_consensusproposal();
        if (m_hasConsensusProposal)
        {
            m_consensusProposal = PBFTProposal::asView(m_active->mutable_consensusproposal());
        }
        m_proposals.clear();
        m_proposals.reserve(m_active->proposals_size());
        for (int i = 0; i < m_active->proposals_size(); i++)
        {
            m_proposals.push_back(PBFTProposal::asView(m_active->mutable_proposals(i)));
        }
    }

    std::unique_ptr<PBFTRawMessage> m_pbftRawMessage;  // non-null iff STANDALONE
    PBFTRawMessage* m_active;                          // owned or parent-owned protobuf
    PBFTProposal m_consensusProposal;                  // VIEW into m_active->consensusproposal()
    bool m_hasConsensusProposal = false;
    std::vector<PBFTProposal> m_proposals;  // VIEWS into m_active->proposals(i)

    mutable bcos::crypto::HashType m_signatureDataHash;
};

// FIB-121: value list (was std::vector<PBFTMessage::Ptr>).
using PBFTMessageList = std::vector<PBFTMessage>;
using PBFTMessageListPtr = std::shared_ptr<PBFTMessageList>;
}  // namespace bcos::consensus
