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
    return encodePBObject(m_rawNewView);
}

void PBFTNewViewMsg::decode(bytesConstRef _data)
{
    decodePBObject(m_rawNewView, _data);
    setBaseMessage(std::shared_ptr<BaseMessage>(m_rawNewView, m_rawNewView->mutable_message()));
    PBFTNewViewMsg::deserializeToObject();
}

void PBFTNewViewMsg::deserializeToObject()
{
    PBFTBaseMessage::deserializeToObject();
    // FIB-121: clear before repopulating so a re-decode does not accumulate duplicate
    // aliasing wrappers (matches PBFTMessage / PBFTViewChangeMsg::deserializeToObject).
    m_viewChangeList->clear();
    m_prePrepareList->clear();
    // aliasing shared_ptrs share m_rawNewView's control block, so every nested
    // viewChange / prePrepare wrapper keeps the NewView protobuf alive and owns nothing.
    for (int i = 0; i < m_rawNewView->viewchangemsglist_size(); i++)
    {
        m_viewChangeList->push_back(
            std::make_shared<PBFTViewChangeMsg>(std::shared_ptr<RawViewChangeMessage>(
                m_rawNewView, m_rawNewView->mutable_viewchangemsglist(i))));
    }
    for (int i = 0; i < m_rawNewView->prepreparelist_size(); i++)
    {
        m_prePrepareList->push_back(std::make_shared<PBFTMessage>(std::shared_ptr<PBFTRawMessage>(
            m_rawNewView, m_rawNewView->mutable_prepreparelist(i))));
    }
}

void PBFTNewViewMsg::setViewChangeMsgList(ViewChangeMsgList const& _viewChangeMsgList)
{
    // FIB-121: keep the caller's viewChange wrappers in the member list (identity +
    // in-memory fields preserved, as before) and deep-copy each into our protobuf for encode
    // (was AddAllocated borrow + destructor release). The in-memory FIB-124 cross-check reads
    // the originals, so their nested preparedProposals stay intact without a hashfieldsdata
    // round-trip.
    *m_viewChangeList = _viewChangeMsgList;
    m_rawNewView->clear_viewchangemsglist();
    for (auto const& viewChangeMsg : _viewChangeMsgList)
    {
        auto pbViewChangeMsg = std::dynamic_pointer_cast<PBFTViewChangeMsg>(viewChangeMsg);
        m_rawNewView->add_viewchangemsglist()->CopyFrom(*pbViewChangeMsg->rawViewChange());
    }
}

void PBFTNewViewMsg::setPrePrepareList(PBFTMessageList const& _prePrepareList)
{
    // FIB-121: keep the caller's prePrepare wrappers in the member list and deep-copy each
    // into our protobuf for encode (was AddAllocated borrow + destructor release).
    *m_prePrepareList = _prePrepareList;
    m_rawNewView->clear_prepreparelist();
    for (auto const& prePrepare : _prePrepareList)
    {
        auto pbPrePrepare = std::dynamic_pointer_cast<PBFTMessage>(prePrepare);
        // flush the inner base header into hashfieldsdata before copying (the prePrepare's
        // base message is serialized into bytes, not embedded as a nested message field).
        pbPrePrepare->encodeHashFields();
        m_rawNewView->add_prepreparelist()->CopyFrom(*pbPrePrepare->pbftRawMessage());
    }
}