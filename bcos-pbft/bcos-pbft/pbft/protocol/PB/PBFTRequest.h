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
 * @brief implementation for PBFT request
 * @file PBFTRequest.h
 * @author: yujiechen
 * @date 2021-04-28
 */
#pragma once
#include "bcos-pbft/pbft/interfaces/PBFTRequestInterface.h"
#include "PBFTBaseMessage.h"
#include "bcos-pbft/pbft/protocol/proto/PBFT.pb.h"
#include <bcos-protocol/Common.h>

namespace bcos
{
namespace consensus
{
class PBFTRequest : virtual public PBFTRequestInterface, public PBFTBaseMessage
{
public:
    PBFTRequest() : PBFTBaseMessage()
    {
        m_pbRequest = std::make_shared<ProposalRequest>();
        m_pbRequest->set_allocated_message(PBFTBaseMessage::baseMessage().get());
    }
    explicit PBFTRequest(bytesConstRef _data) : PBFTBaseMessage()
    {
        m_pbRequest = std::make_shared<ProposalRequest>();
        decode(_data);
    }

    ~PBFTRequest() override { m_pbRequest->unsafe_arena_release_message(); }

    void setSize(int64_t _size) override { m_pbRequest->set_size(_size); }
    int64_t size() const override { return m_pbRequest->size(); }

    bytesPointer encode(
        bcos::crypto::CryptoSuite::Ptr, bcos::crypto::KeyPairInterface::Ptr) const override
    {
        return bcos::protocol::encodePBObject(m_pbRequest);
    }

    void decode(bytesConstRef _data) override
    {
        bcos::protocol::decodePBObject(m_pbRequest, _data);
        setBaseMessage(std::shared_ptr<BaseMessage>(m_pbRequest->mutable_message()));
        PBFTBaseMessage::deserializeToObject();
    }

    bool operator==(PBFTRequest const& _pbftRequest)
    {
        if (!PBFTBaseMessage::operator==(_pbftRequest))
        {
            return false;
        }
        return _pbftRequest.size() == size();
    }

private:
    std::shared_ptr<ProposalRequest> m_pbRequest;
};
}  // namespace consensus
}  // namespace bcos