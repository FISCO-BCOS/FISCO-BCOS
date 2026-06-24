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

bytesPointer PBFTViewChangeMsg::encode(CryptoSuite::Ptr, KeyPairInterface::Ptr) const
{
    // FIB-121: materialize the independently-owned base header into the protobuf
    // before serializing (no set_allocated borrow).
    return encodePBObject(encodedRaw());
}

void PBFTViewChangeMsg::decode(bytesConstRef _data)
{
    decodePBObject(m_active, _data);
    // FIB-121: copy the base header out of the protobuf into our own BaseMessage.
    m_baseMessage->CopyFrom(m_active->message());
    deserializeToObject();
    m_packetType = PacketType::ViewChangePacket;
}

void PBFTViewChangeMsg::deserializeToObject()
{
    PBFTBaseMessage::deserializeToObject();
    // FIB-121 / DoS mitigation: reject oversized preparedProposals before
    // allocating any wrappers.
    validateRepeatedSize(
        m_active->preparedproposals(), MAX_PBFT_REPEATED_FIELD_SIZE, "preparedProposals");
    rebuildViews();
}

void PBFTViewChangeMsg::setCommittedProposal(PBFTProposal const& _proposal)
{
    m_active->mutable_committedproposal()->CopyFrom(*_proposal.pbftRawProposal());
    m_hasCommitted = true;
    m_committedProposal = PBFTProposal::asView(m_active->mutable_committedproposal());
}

void PBFTViewChangeMsg::setPreparedProposals(PBFTMessageList const& _preparedProposals)
{
    m_active->clear_preparedproposals();
    for (auto const& preparedMsg : _preparedProposals)
    {
        // deep copy each prePrepare's protobuf into our repeated field
        m_active->add_preparedproposals()->CopyFrom(*preparedMsg.pbftRawMessage());
    }
    // rebuild views only after the repeated field is fully populated
    rebuildViews();
}
