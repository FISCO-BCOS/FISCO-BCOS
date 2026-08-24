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
 * @brief implementation for PBFTViewChangeMsg
 * @file PBFTViewChangeMsg.cpp
 * @author: yujiechen
 * @date 2021-04-15
 */
#include "PBFTViewChangeMsg.h"
#include "PBFTMessage.h"
#include "PBFTProposal.h"
#include "bcos-pbft/pbft/protocol/proto/PBFT.pb.h"
#include <bcos-protocol/Common.h>

using namespace bcos;
using namespace bcos::consensus;
using namespace bcos::protocol;
using namespace bcos::crypto;
PBFTViewChangeMsg::PBFTViewChangeMsg(std::shared_ptr<RawViewChangeMessage> _rawViewChange)
  // FIB-121: alias the base header onto the viewed protobuf's `message` field so the
  // base wrapper shares _rawViewChange's control block (no owning-over-borrowed pointer).
  : PBFTBaseMessage(std::shared_ptr<BaseMessage>(_rawViewChange, _rawViewChange->mutable_message()))
{
    m_packetType = PacketType::ViewChangePacket;
    m_preparedProposalList = std::make_shared<PBFTMessageList>();
    m_rawViewChange = _rawViewChange;
    PBFTViewChangeMsg::deserializeToObject();
}

bytesPointer PBFTViewChangeMsg::encode(CryptoSuite::Ptr, KeyPairInterface::Ptr) const
{
    return encodePBObject(m_rawViewChange);
}

void PBFTViewChangeMsg::decode(bytesConstRef _data)
{
    decodePBObject(m_rawViewChange, _data);

    // Use an aliasing shared_ptr: m_baseMessage points to the arena-allocated
    // sub-message, but shares ownership with m_rawViewChange.  When the
    // shared_ptr is destroyed it does NOT call delete (the arena owns the
    // memory), avoiding the double-free that a non-aliasing shared_ptr would
    // cause.
    setBaseMessage(
        std::shared_ptr<BaseMessage>(m_rawViewChange, m_rawViewChange->mutable_message()));

    PBFTViewChangeMsg::deserializeToObject();
    m_packetType = PacketType::ViewChangePacket;
}

void PBFTViewChangeMsg::setCommittedProposal(PBFTProposalInterface::Ptr _proposal)
{
    auto pbftProposal = std::dynamic_pointer_cast<PBFTProposal>(_proposal);
    // FIB-121: deep-copy into our protobuf, then alias a wrapper over our own copy.
    m_rawViewChange->mutable_committedproposal()->CopyFrom(*pbftProposal->pbftRawProposal());
    m_committedProposal = std::make_shared<PBFTProposal>(std::shared_ptr<PBFTRawProposal>(
        m_rawViewChange, m_rawViewChange->mutable_committedproposal()));
}

void PBFTViewChangeMsg::setPreparedProposals(PBFTMessageList const& _preparedProposals)
{
    // FIB-121: keep the caller's prePrepare wrappers in the member list (identity +
    // in-memory base fields preserved, as before) and deep-copy each into our protobuf for
    // encode (was AddAllocated borrow + destructor release). A PBFTMessage's base fields
    // round-trip through hashfieldsdata, so rebuilding wrappers from the copy would lose any
    // fields the caller had not yet flushed -- keeping the originals avoids that.
    *m_preparedProposalList = _preparedProposals;
    m_rawViewChange->clear_preparedproposals();
    for (auto const& proposal : _preparedProposals)
    {
        auto pbftMessage = std::dynamic_pointer_cast<PBFTMessage>(proposal);
        m_rawViewChange->add_preparedproposals()->CopyFrom(*pbftMessage->pbftRawMessage());
    }
}

void PBFTViewChangeMsg::deserializeToObject()
{
    PBFTBaseMessage::deserializeToObject();
    m_preparedProposalList->clear();
    // FIB-121 / Issue #3 (DoS mitigation): reject oversized preparedProposals
    // before allocating any wrappers, preventing memory-exhaustion attacks from
    // malicious peers.
    //
    // FIB-121 / Issue #1 structural fix (applied): committedProposal /
    // preparedProposals wrappers below, and the encode-path setters
    // (setCommittedProposal / setPreparedProposals / PBFTNewViewMsg's list setters),
    // now use aliasing shared_ptrs that share m_rawViewChange's control block instead
    // of the old dual-ownership + destructor-release protocol. Every wrapper keeps the
    // owning protobuf alive and deletes nothing, so partial-construct exception
    // unwinding can no longer double-free or leak.
    validateRepeatedSize(
        m_rawViewChange->preparedproposals(), MAX_PBFT_REPEATED_FIELD_SIZE, "preparedProposals");

    // Use aliasing shared_ptrs: sub-messages live in m_rawViewChange's arena,
    // so we share ownership with m_rawViewChange.
    auto* committedRawPtr = m_rawViewChange->mutable_committedproposal();
    std::shared_ptr<PBFTRawProposal> rawCommittedProposal(m_rawViewChange, committedRawPtr);
    m_committedProposal = std::make_shared<PBFTProposal>(rawCommittedProposal);
    for (int i = 0; i < m_rawViewChange->preparedproposals_size(); i++)
    {
        auto* rawPtr = m_rawViewChange->mutable_preparedproposals(i);
        std::shared_ptr<PBFTRawMessage> preparedMsg(m_rawViewChange, rawPtr);
        m_preparedProposalList->push_back(std::make_shared<PBFTMessage>(preparedMsg));
    }
}