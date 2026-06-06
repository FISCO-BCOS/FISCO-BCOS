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
  : PBFTBaseMessage(std::shared_ptr<BaseMessage>(_rawViewChange->mutable_message()))
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
    setBaseMessage(std::shared_ptr<BaseMessage>(m_rawViewChange->mutable_message()));
    PBFTViewChangeMsg::deserializeToObject();
    m_packetType = PacketType::ViewChangePacket;
}

void PBFTViewChangeMsg::setCommittedProposal(PBFTProposalInterface::Ptr _proposal)
{
    m_committedProposal = _proposal;
    auto pbftProposal = std::dynamic_pointer_cast<PBFTProposal>(_proposal);
    // set committed proposal
    if (m_rawViewChange->has_committedproposal())
    {
        m_rawViewChange->unsafe_arena_release_committedproposal();
    }
    m_rawViewChange->unsafe_arena_set_allocated_committedproposal(
        pbftProposal->pbftRawProposal().get());
}

void PBFTViewChangeMsg::setPreparedProposals(PBFTMessageList const& _preparedProposals)
{
    *m_preparedProposalList = _preparedProposals;
    for (auto proposal : *m_preparedProposalList)
    {
        auto pbftMessage = std::dynamic_pointer_cast<PBFTMessage>(proposal);
        m_rawViewChange->mutable_preparedproposals()->AddAllocated(
            pbftMessage->pbftRawMessage().get());
    }
}

void PBFTViewChangeMsg::deserializeToObject()
{
    PBFTBaseMessage::deserializeToObject();
    m_preparedProposalList->clear();
    // FIB-121 / Issue #3 (DoS mitigation): reject oversized preparedProposals
    // before allocating any wrappers, preventing memory-exhaustion attacks from
    // malicious peers and shrinking the bad_alloc surface that Issue #1
    // (partial-construct exception unwinding) depends on.
    //
    // A full structural fix for Issue #1 (release-then-wrap, or aliasing
    // shared_ptrs throughout) requires consistently restructuring the encode
    // path (setCommittedProposal / setPreparedProposals / PBFTNewViewMsg's
    // viewchangemsglist AddAllocated) and the destructors of PBFTViewChangeMsg
    // / PBFTNewViewMsg / PBFTMessage, all of which currently rely on a fragile
    // dual-ownership + destructor-release protocol.  That refactor is deferred
    // because (a) it crosses several files with subtle invariants, and (b) the
    // size cap below reduces the bad_alloc trigger surface to near-zero for the
    // realistic threat model (an attacker sending an oversized message — the
    // primary CertiK concern in Issue #3).
    validateRepeatedSize(
        m_rawViewChange->preparedproposals(), MAX_PBFT_REPEATED_FIELD_SIZE, "preparedProposals");

    std::shared_ptr<PBFTRawProposal> rawCommittedProposal(
        m_rawViewChange->mutable_committedproposal());
    m_committedProposal = std::make_shared<PBFTProposal>(rawCommittedProposal);
    for (int i = 0; i < m_rawViewChange->preparedproposals_size(); i++)
    {
        std::shared_ptr<PBFTRawMessage> preparedMsg(m_rawViewChange->mutable_preparedproposals(i));
        m_preparedProposalList->push_back(std::make_shared<PBFTMessage>(preparedMsg));
    }
}