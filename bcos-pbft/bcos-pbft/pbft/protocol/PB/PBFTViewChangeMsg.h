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
 * @brief implementation for ViewChangeMsg
 * @file PBFTViewChangeMsg.h
 * @author: yujiechen
 * @date 2021-04-15
 */
#pragma once
#include "PBFTBaseMessage.h"
#include "PBFTMessage.h"
#include "PBFTProposal.h"
#include "bcos-pbft/pbft/protocol/proto/PBFT.pb.h"
#include <memory>

namespace bcos::consensus
{
// FIB-121: value + non-owning-view message (single-impl ViewChangeMsgInterface
// collapsed in). STANDALONE owns its RawViewChangeMessage; VIEW windows onto a
// parent NewView's viewChangeMsgList element. The base header lives in the
// independently-owned PBFTBaseMessage and is copied in/out of the protobuf's
// `message` field at encode/decode (no set_allocated borrow + destructor
// release). committedProposal / preparedProposals are value views into m_active.
class PBFTViewChangeMsg : public PBFTBaseMessage
{
public:
    using Ptr = std::shared_ptr<PBFTViewChangeMsg>;
    PBFTViewChangeMsg()
      : PBFTBaseMessage(),
        m_rawViewChange(std::make_unique<RawViewChangeMessage>()),
        m_active(m_rawViewChange.get())
    {
        m_packetType = PacketType::ViewChangePacket;
        rebuildViews();
    }

    explicit PBFTViewChangeMsg(bytesConstRef _data)
      : PBFTBaseMessage(),
        m_rawViewChange(std::make_unique<RawViewChangeMessage>()),
        m_active(m_rawViewChange.get())
    {
        decode(_data);
    }

    // Non-owning view onto a parent NewView's viewChangeMsgList element.
    static PBFTViewChangeMsg asView(RawViewChangeMessage* _sub)
    {
        return PBFTViewChangeMsg(ViewTag{}, _sub);
    }

    PBFTViewChangeMsg(PBFTViewChangeMsg const& _other)
      : PBFTBaseMessage(_other),
        m_rawViewChange(std::make_unique<RawViewChangeMessage>(*_other.m_active)),
        m_active(m_rawViewChange.get())
    {
        rebuildViews();
    }
    PBFTViewChangeMsg& operator=(PBFTViewChangeMsg const& _other)
    {
        PBFTViewChangeMsg tmp(_other);
        *this = std::move(tmp);
        return *this;
    }
    PBFTViewChangeMsg(PBFTViewChangeMsg&&) noexcept = default;
    PBFTViewChangeMsg& operator=(PBFTViewChangeMsg&&) noexcept = default;

    ~PBFTViewChangeMsg() override = default;

    RawViewChangeMessage* rawViewChange() { return m_active; }
    RawViewChangeMessage const* rawViewChange() const { return m_active; }

    // Materialize the independently-owned base header into the protobuf's
    // `message` field and return it, so the protobuf is self-contained for
    // serialization or for embedding into a NewView (replaces the old
    // set_allocated_message aliasing). Logically const: the base header in
    // m_baseMessage is the source of truth; this only mirrors it into m_active.
    RawViewChangeMessage const* encodedRaw() const
    {
        m_active->mutable_message()->CopyFrom(*m_baseMessage);
        return m_active;
    }

    // Nullable observer (returns nullptr when the field is absent).
    // LIFETIME: the returned pointer aliases a value member that views into this
    // message's protobuf — valid only while this PBFTViewChangeMsg is alive. Do
    // NOT store it past the owning message's lifetime; for an owning handle copy
    // it: std::make_shared<PBFTProposal>(*vc->committedProposal()).
    PBFTProposal* committedProposal() { return m_hasCommitted ? &m_committedProposal : nullptr; }
    PBFTProposal const* committedProposal() const
    {
        return m_hasCommitted ? &m_committedProposal : nullptr;
    }
    std::vector<PBFTMessage> const& preparedProposals() const { return m_preparedProposalList; }

    void setCommittedProposal(PBFTProposal const& _proposal);
    void setPreparedProposals(PBFTMessageList const& _preparedProposals);

    bytesPointer encode(
        bcos::crypto::CryptoSuite::Ptr, bcos::crypto::KeyPairInterface::Ptr) const override;
    void decode(bytesConstRef _data) override;

    std::string toDebugString() const override
    {
        std::stringstream stringstream;
        stringstream << LOG_KV("type", m_packetType)
                     << LOG_KV("fromNode", m_from ? m_from->shortHex() : "null")
                     << LOG_KV("commitProposal",
                            m_hasCommitted ? printPBFTProposal(&m_committedProposal) : "null")
                     << LOG_KV("prePreSize", m_preparedProposalList.size());
        return stringstream.str();
    }

protected:
    void deserializeToObject() override;

private:
    struct ViewTag
    {
    };
    PBFTViewChangeMsg(ViewTag, RawViewChangeMessage* _sub)
      : PBFTBaseMessage(), m_rawViewChange(nullptr), m_active(_sub)
    {
        // copy the base header out of the viewed protobuf, then build views
        m_baseMessage->CopyFrom(m_active->message());
        deserializeToObject();
        m_packetType = PacketType::ViewChangePacket;
    }

    void rebuildViews()
    {
        m_hasCommitted = m_active->has_committedproposal();
        if (m_hasCommitted)
        {
            m_committedProposal = PBFTProposal::asView(m_active->mutable_committedproposal());
        }
        m_preparedProposalList.clear();
        m_preparedProposalList.reserve(m_active->preparedproposals_size());
        for (int i = 0; i < m_active->preparedproposals_size(); i++)
        {
            m_preparedProposalList.push_back(
                PBFTMessage::asView(m_active->mutable_preparedproposals(i)));
        }
    }

    std::unique_ptr<RawViewChangeMessage> m_rawViewChange;  // non-null iff STANDALONE
    RawViewChangeMessage* m_active;                         // owned or parent-owned protobuf
    PBFTProposal m_committedProposal;  // VIEW into m_active->committedproposal()
    bool m_hasCommitted = false;
    std::vector<PBFTMessage> m_preparedProposalList;  // VIEWS into m_active->preparedproposals(i)
};
// FIB-121: value list (was std::vector<PBFTViewChangeMsg::Ptr>).
using ViewChangeMsgList = std::vector<PBFTViewChangeMsg>;
using ViewChangeMsgListPtr = std::shared_ptr<ViewChangeMsgList>;
}  // namespace bcos::consensus
