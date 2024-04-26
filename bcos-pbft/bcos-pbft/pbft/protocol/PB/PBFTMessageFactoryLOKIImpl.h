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
 * @file PBFTMessageFactoryLOKIImpl.h
 * @author: fuchenma
 * @date 2024-04-22
 */
#pragma once
#include "../../interfaces/PBFTMessageFactory.h"
#include "PBFTMessage.h"
#include "PBFTNewViewMsg.h"
#include "PBFTProposal.h"
#include "PBFTRequest.h"
#include "PBFTViewChangeMsg.h"
#include "../../libloki/FuzzEngine.h"

namespace bcos
{
namespace consensus
{
class PBFTMessageFactoryLOKIImpl : public PBFTMessageFactory
{
public:
    using Ptr = std::shared_ptr<PBFTMessageFactoryLOKIImpl>;
    PBFTMessageFactoryLOKIImpl(){}
    ~PBFTMessageFactoryLOKIImpl() override {}

    PBFTMessageInterface::Ptr createPBFTMsg() override { return std::make_shared<PBFTMessage>(); }

    ViewChangeMsgInterface::Ptr createViewChangeMsg() override
    {
        auto passive_fuzzer_engine = std::make_shared<loki::fuzzer::Fuzzer>();
        auto pbft_view_change_msg = std::make_shared<PBFTViewChangeMsg>();
        return passive_fuzzer_engine->mutateViewChange(pbft_view_change_msg);
    }

    NewViewMsgInterface::Ptr createNewViewMsg() override
    {
        auto passive_fuzzer_engine = std::make_shared<loki::fuzzer::Fuzzer>();
        auto pbft_new_view_msg = std::make_shared<PBFTNewViewMsg>();
        return passive_fuzzer_engine->mutateNewViewMsg(pbft_new_view_msg);
    }

    PBFTMessageInterface::Ptr createPBFTMsg(
        bcos::crypto::CryptoSuite::Ptr _cryptoSuite, bytesConstRef _data) override
    {
        auto passive_fuzzer_engine = std::make_shared<loki::fuzzer::Fuzzer>();
        auto pbft_msg = std::make_shared<PBFTMessage>(_cryptoSuite, _data);
        return passive_fuzzer_engine->mutatePbftMsg(pbft_msg);
    }

    ViewChangeMsgInterface::Ptr createViewChangeMsg(bytesConstRef _data) override
    {
        auto passive_fuzzer_engine = std::make_shared<loki::fuzzer::Fuzzer>();
        auto pbft_view_change_msg = std::make_shared<PBFTViewChangeMsg>(_data);
        return passive_fuzzer_engine->mutateViewChange(pbft_view_change_msg);
    }

    NewViewMsgInterface::Ptr createNewViewMsg(bytesConstRef _data) override
    {
        return std::make_shared<PBFTNewViewMsg>(_data);
    }

    PBFTProposalInterface::Ptr createPBFTProposal() override
    {
        return std::make_shared<PBFTProposal>();
    }

    PBFTProposalInterface::Ptr createPBFTProposal(bytesConstRef _data) override
    {
        return std::make_shared<PBFTProposal>(_data);
    }

    PBFTRequestInterface::Ptr createPBFTRequest() override
    {
        return std::make_shared<PBFTRequest>();
    }

    PBFTRequestInterface::Ptr createPBFTRequest(bytesConstRef _data) override
    {
        return std::make_shared<PBFTRequest>(_data);
    }
};
}  // namespace consensus
}  // namespace bcos