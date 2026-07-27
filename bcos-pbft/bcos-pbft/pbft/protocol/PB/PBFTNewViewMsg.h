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
 * @brief implementation for PBFTNewViewMsg
 * @file PBFTNewViewMsg.h
 * @author: yujiechen
 * @date 2021-04-16
 */
#pragma once
#include "../../interfaces/NewViewMsgInterface.h"
#include "../../interfaces/ViewChangeMsgInterface.h"
#include "PBFTBaseMessage.h"

namespace bcos
{
namespace consensus
{
class PBFTNewViewMsg : public NewViewMsgInterface, public PBFTBaseMessage
{
public:
    using Ptr = std::shared_ptr<PBFTNewViewMsg>;
    PBFTNewViewMsg() : PBFTBaseMessage()
    {
        m_rawNewView = std::make_shared<RawNewViewMessage>();
        // FIB-121: alias the base header onto the protobuf's `message` field (was
        // set_allocated_message + destructor release).
        setBaseMessage(std::shared_ptr<BaseMessage>(m_rawNewView, m_rawNewView->mutable_message()));
        m_viewChangeList = std::make_shared<ViewChangeMsgList>();
        m_prePrepareList = std::make_shared<PBFTMessageList>();
        m_packetType = PacketType::NewViewPacket;
    }
    explicit PBFTNewViewMsg(bytesConstRef _data) : PBFTBaseMessage()
    {
        m_rawNewView = std::make_shared<RawNewViewMessage>();
        m_viewChangeList = std::make_shared<ViewChangeMsgList>();
        m_prePrepareList = std::make_shared<PBFTMessageList>();
        m_packetType = PacketType::NewViewPacket;
        decode(_data);
    }

    // FIB-121: base header / viewChangeMsgList / prePrepareList wrappers hold aliasing
    // shared_ptrs that share this object's control block; nothing to release here.
    ~PBFTNewViewMsg() override = default;

    bytesPointer encode(bcos::crypto::CryptoSuite::Ptr _cryptoSuite,
        bcos::crypto::KeyPairInterface::Ptr _keyPair) const override;
    void decode(bytesConstRef _data) override;

    void setViewChangeMsgList(ViewChangeMsgList const& _viewChangeMsgList) override;
    ViewChangeMsgList const& viewChangeMsgList() const override { return *m_viewChangeList; }

    PBFTMessageList const& prePrepareList() override { return *m_prePrepareList; }
    void setPrePrepareList(PBFTMessageList const& _preparedProposal) override;

    std::string toDebugString() const override
    {
        std::stringstream stringstream;
        stringstream << LOG_KV("type", m_packetType)
                     << LOG_KV("fromNode", m_from ? m_from->shortHex() : "null")
                     << LOG_KV("vcMsgSize", m_viewChangeList ? m_viewChangeList->size() : 0)
                     << LOG_KV("prePreSize", m_prePrepareList ? m_prePrepareList->size() : 0);
        if (c_fileLogLevel == TRACE) [[unlikely]]
        {
            if (m_prePrepareList)
            {
                size_t i = 0;
                for (auto const& prePrepare : *m_prePrepareList)
                {
                    stringstream << "prePrepare" << i++ << printPBFTMsgInfo(prePrepare);
                }
            }
            if (m_viewChangeList)
            {
                size_t i = 0;
                for (auto const& viewChange : *m_viewChangeList)
                {
                    stringstream << "viewChange" << i++ << printPBFTMsgInfo(viewChange);
                }
            }
        }
        return stringstream.str();
    }

protected:
    void deserializeToObject() override;

private:
    std::shared_ptr<RawNewViewMessage> m_rawNewView;
    // required and need to be verified
    ViewChangeMsgListPtr m_viewChangeList;
    PBFTMessageListPtr m_prePrepareList;
};
}  // namespace consensus
}  // namespace bcos