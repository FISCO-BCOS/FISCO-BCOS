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
#include "../..//interfaces/ViewChangeMsgInterface.h"
#include "PBFTBaseMessage.h"
namespace bcos::consensus
{
class PBFTViewChangeMsg : public ViewChangeMsgInterface, public PBFTBaseMessage
{
public:
    using Ptr = std::shared_ptr<PBFTViewChangeMsg>;
    PBFTViewChangeMsg() : PBFTBaseMessage()
    {
        m_preparedProposalList = std::make_shared<PBFTMessageList>();
        m_rawViewChange = std::make_shared<RawViewChangeMessage>();
        m_rawViewChange->set_allocated_message(PBFTBaseMessage::baseMessage().get());
        m_packetType = PacketType::ViewChangePacket;
    }

    explicit PBFTViewChangeMsg(std::shared_ptr<RawViewChangeMessage> _rawViewChange);
    explicit PBFTViewChangeMsg(bytesConstRef _data) : PBFTBaseMessage()
    {
        m_preparedProposalList = std::make_shared<PBFTMessageList>();
        m_rawViewChange = std::make_shared<RawViewChangeMessage>();
        decode(_data);
    }

    ~PBFTViewChangeMsg() override
    {
        // return back the ownership of message to PBFTBaseMessage
        m_rawViewChange->unsafe_arena_release_message();
        // return back the ownership to m_committedProposal
        if (m_rawViewChange->has_committedproposal())
        {
            m_rawViewChange->unsafe_arena_release_committedproposal();
        }
        // return back the ownership to m_preparedProposalList
        auto preparedProposalSize = m_rawViewChange->preparedproposals_size();
        for (auto i = 0; i < preparedProposalSize; i++)
        {
            m_rawViewChange->mutable_preparedproposals()->UnsafeArenaReleaseLast();
        }
    }

    std::shared_ptr<RawViewChangeMessage> rawViewChange() { return m_rawViewChange; }

    PBFTProposalInterface::Ptr committedProposal() override { return m_committedProposal; }
    PBFTMessageList const& preparedProposals() override { return *m_preparedProposalList; }

    void setCommittedProposal(PBFTProposalInterface::Ptr _proposal) override;
    void setPreparedProposals(PBFTMessageList const& _preparedProposals) override;

    bytesPointer encode(
        bcos::crypto::CryptoSuite::Ptr, bcos::crypto::KeyPairInterface::Ptr) const override;
    void decode(bytesConstRef _data) override;

    std::string toDebugString() const override
    {
        std::stringstream stringstream;
        stringstream << LOG_KV("type", m_packetType)
                     << LOG_KV("fromNode", m_from ? m_from->shortHex() : "null")
                     << LOG_KV("commitProposal",
                            m_committedProposal ? printPBFTProposal(m_committedProposal) : "null")
                     << LOG_KV("prePreSize",
                            m_preparedProposalList ? m_preparedProposalList->size() : 0);
        if (m_preparedProposalList)
        {
            size_t i = 0;
            for (auto const& prePrepare : *m_preparedProposalList)
            {
                stringstream << "prePrepare" << i++ << printPBFTMsgInfo(prePrepare);
            }
        }
        return stringstream.str();
    }

protected:
    // deserialize RawViewChangeMessage to Object
    void deserializeToObject() override;

private:
    std::shared_ptr<RawViewChangeMessage> m_rawViewChange;
    // required and need to be verified
    PBFTProposalInterface::Ptr m_committedProposal;
    // optional
    PBFTMessageListPtr m_preparedProposalList;
};
using PBFTViewChangeMsgList = std::vector<PBFTViewChangeMsg::Ptr>;
using PBFTViewChangeMsgListPtr = std::shared_ptr<PBFTViewChangeMsg>;
}  // namespace bcos::consensus
