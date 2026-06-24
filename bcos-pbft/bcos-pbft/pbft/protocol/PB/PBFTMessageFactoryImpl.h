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
 * @brief implementation for PBFTMessageFactory
 * @file PBFTMessageFactoryImpl.h
 * @author: yujiechen
 * @date 2021-04-20
 */
#pragma once
#include "../../interfaces/PBFTMessageFactory.h"
#include "PBFTMessage.h"
#include "PBFTNewViewMsg.h"
#include "PBFTProposal.h"
#include "PBFTRequest.h"
#include "PBFTViewChangeMsg.h"

namespace bcos
{
namespace consensus
{
class PBFTMessageFactoryImpl : public PBFTMessageFactory
{
public:
    using Ptr = std::shared_ptr<PBFTMessageFactoryImpl>;
    PBFTMessageFactoryImpl() = default;
    ~PBFTMessageFactoryImpl() override {}

    PBFTMessage::Ptr createPBFTMsg() override { return std::make_shared<PBFTMessage>(); }

    PBFTViewChangeMsg::Ptr createViewChangeMsg() override
    {
        return std::make_shared<PBFTViewChangeMsg>();
    }

    PBFTNewViewMsg::Ptr createNewViewMsg() override { return std::make_shared<PBFTNewViewMsg>(); }

    PBFTMessage::Ptr createPBFTMsg(
        bcos::crypto::CryptoSuite::Ptr _cryptoSuite, bytesConstRef _data) override
    {
        return std::make_shared<PBFTMessage>(_cryptoSuite, _data);
    }

    PBFTViewChangeMsg::Ptr createViewChangeMsg(bytesConstRef _data) override
    {
        return std::make_shared<PBFTViewChangeMsg>(_data);
    }

    PBFTNewViewMsg::Ptr createNewViewMsg(bytesConstRef _data) override
    {
        return std::make_shared<PBFTNewViewMsg>(_data);
    }

    PBFTProposal::Ptr createPBFTProposal() override { return std::make_shared<PBFTProposal>(); }

    PBFTProposal::Ptr createPBFTProposal(bytesConstRef _data) override
    {
        return std::make_shared<PBFTProposal>(_data);
    }

    PBFTRequest::Ptr createPBFTRequest() override { return std::make_shared<PBFTRequest>(); }

    PBFTRequest::Ptr createPBFTRequest(bytesConstRef _data) override
    {
        return std::make_shared<PBFTRequest>(_data);
    }
};
}  // namespace consensus
}  // namespace bcos