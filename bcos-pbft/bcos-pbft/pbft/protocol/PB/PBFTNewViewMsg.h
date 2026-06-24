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
#include "PBFTBaseMessage.h"
#include "PBFTMessage.h"
#include "PBFTViewChangeMsg.h"
#include "bcos-pbft/pbft/protocol/proto/PBFT.pb.h"
#include <memory>

namespace bcos::consensus
{
// FIB-121: standalone-only message (single-impl NewViewMsgInterface collapsed
// in). NewView is never embedded in another message, so it owns its protobuf
// and is non-copyable; viewChangeMsgList / prePrepareList are value views into
// it. The base header is copied in/out of the protobuf's `message` field at
// encode/decode (no set_allocated borrow + destructor release).
class PBFTNewViewMsg : public PBFTBaseMessage
{
public:
    using Ptr = std::shared_ptr<PBFTNewViewMsg>;
    PBFTNewViewMsg() : PBFTBaseMessage(), m_rawNewView(std::make_unique<RawNewViewMessage>())
    {
        m_packetType = PacketType::NewViewPacket;
        rebuildViews();
    }
    explicit PBFTNewViewMsg(bytesConstRef _data)
      : PBFTBaseMessage(), m_rawNewView(std::make_unique<RawNewViewMessage>())
    {
        m_packetType = PacketType::NewViewPacket;
        decode(_data);
    }

    ~PBFTNewViewMsg() override = default;

    bytesPointer encode(bcos::crypto::CryptoSuite::Ptr _cryptoSuite,
        bcos::crypto::KeyPairInterface::Ptr _keyPair) const override;
    void decode(bytesConstRef _data) override;

    void setViewChangeMsgList(ViewChangeMsgList const& _viewChangeMsgList);
    std::vector<PBFTViewChangeMsg> const& viewChangeMsgList() const { return m_viewChangeList; }

    std::vector<PBFTMessage> const& prePrepareList() const { return m_prePrepareList; }
    void setPrePrepareList(PBFTMessageList const& _prePrepareList);

    std::string toDebugString() const override
    {
        std::stringstream stringstream;
        stringstream << LOG_KV("type", m_packetType)
                     << LOG_KV("fromNode", m_from ? m_from->shortHex() : "null")
                     << LOG_KV("vcMsgSize", m_viewChangeList.size())
                     << LOG_KV("prePreSize", m_prePrepareList.size());
        if (c_fileLogLevel == TRACE) [[unlikely]]
        {
            size_t i = 0;
            for (auto const& prePrepare : m_prePrepareList)
            {
                stringstream << "prePrepare" << i++ << printPBFTMsgInfo(&prePrepare);
            }
            i = 0;
            for (auto const& viewChange : m_viewChangeList)
            {
                stringstream << "viewChange" << i++ << printPBFTMsgInfo(&viewChange);
            }
        }
        return stringstream.str();
    }

protected:
    void deserializeToObject() override;

private:
    void rebuildViews()
    {
        m_viewChangeList.clear();
        m_viewChangeList.reserve(m_rawNewView->viewchangemsglist_size());
        for (int i = 0; i < m_rawNewView->viewchangemsglist_size(); i++)
        {
            m_viewChangeList.push_back(
                PBFTViewChangeMsg::asView(m_rawNewView->mutable_viewchangemsglist(i)));
        }
        m_prePrepareList.clear();
        m_prePrepareList.reserve(m_rawNewView->prepreparelist_size());
        for (int i = 0; i < m_rawNewView->prepreparelist_size(); i++)
        {
            m_prePrepareList.push_back(
                PBFTMessage::asView(m_rawNewView->mutable_prepreparelist(i)));
        }
    }

    std::unique_ptr<RawNewViewMessage> m_rawNewView;  // sole owner (NewView is never nested)
    std::vector<PBFTViewChangeMsg> m_viewChangeList;  // VIEWS into
                                                      // m_rawNewView->viewchangemsglist(i)
    std::vector<PBFTMessage> m_prePrepareList;        // VIEWS into m_rawNewView->prepreparelist(i)
};
}  // namespace bcos::consensus
