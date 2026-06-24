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
 * @file PBFTNewViewMsg.cpp
 * @author: yujiechen
 * @date 2021-04-16
 */

#include "PBFTNewViewMsg.h"
#include "PBFTMessage.h"
#include "PBFTViewChangeMsg.h"
#include <bcos-protocol/Common.h>

using namespace bcos;
using namespace bcos::consensus;
using namespace bcos::protocol;
using namespace bcos::crypto;

bytesPointer PBFTNewViewMsg::encode(CryptoSuite::Ptr, KeyPairInterface::Ptr) const
{
    // FIB-121: materialize the base header into the protobuf before serializing.
    m_rawNewView->mutable_message()->CopyFrom(*m_baseMessage);
    return encodePBObject(m_rawNewView.get());
}

void PBFTNewViewMsg::decode(bytesConstRef _data)
{
    decodePBObject(m_rawNewView.get(), _data);
    // FIB-121: copy the base header out of the protobuf into our own BaseMessage.
    m_baseMessage->CopyFrom(m_rawNewView->message());
    deserializeToObject();
    m_packetType = PacketType::NewViewPacket;
}

void PBFTNewViewMsg::deserializeToObject()
{
    PBFTBaseMessage::deserializeToObject();
    rebuildViews();
}

void PBFTNewViewMsg::setViewChangeMsgList(ViewChangeMsgList const& _viewChangeMsgList)
{
    m_rawNewView->clear_viewchangemsglist();
    for (auto const& viewChange : _viewChangeMsgList)
    {
        // encodedRaw() materializes the viewChange's base header into its protobuf
        // (it had no set_allocated alias), then we deep-copy it in.
        m_rawNewView->add_viewchangemsglist()->CopyFrom(*viewChange.encodedRaw());
    }
    rebuildViews();
}

void PBFTNewViewMsg::setPrePrepareList(PBFTMessageList const& _prePrepareList)
{
    m_rawNewView->clear_prepreparelist();
    for (auto const& prePrepare : _prePrepareList)
    {
        // serialize the prePrepare's base fields into its hashFieldsData first,
        // then deep-copy the whole message in.
        prePrepare.encodeHashFields();
        m_rawNewView->add_prepreparelist()->CopyFrom(*prePrepare.pbftRawMessage());
    }
    rebuildViews();
}
