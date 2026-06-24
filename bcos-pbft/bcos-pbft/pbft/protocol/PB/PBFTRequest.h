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
#include "PBFTBaseMessage.h"
#include "bcos-pbft/pbft/protocol/proto/PBFT.pb.h"
#include <bcos-protocol/Common.h>
#include <memory>

namespace bcos::consensus
{
// FIB-121: standalone-only message (single-impl PBFTRequestInterface collapsed
// in). Owns its ProposalRequest; the base header is copied in/out of the
// protobuf's `message` field at encode/decode (no set_allocated borrow +
// destructor release).
class PBFTRequest : public PBFTBaseMessage
{
public:
    using Ptr = std::shared_ptr<PBFTRequest>;
    PBFTRequest() : PBFTBaseMessage(), m_pbRequest(std::make_unique<ProposalRequest>()) {}
    explicit PBFTRequest(bytesConstRef _data)
      : PBFTBaseMessage(), m_pbRequest(std::make_unique<ProposalRequest>())
    {
        decode(_data);
    }

    ~PBFTRequest() override = default;

    void setSize(int64_t _size) { m_pbRequest->set_size(_size); }
    int64_t size() const { return m_pbRequest->size(); }

    bytesPointer encode(
        bcos::crypto::CryptoSuite::Ptr, bcos::crypto::KeyPairInterface::Ptr) const override
    {
        // FIB-121: materialize the base header into the protobuf before serializing.
        m_pbRequest->mutable_message()->CopyFrom(*m_baseMessage);
        return bcos::protocol::encodePBObject(m_pbRequest.get());
    }

    void decode(bytesConstRef _data) override
    {
        bcos::protocol::decodePBObject(m_pbRequest.get(), _data);
        // FIB-121: copy the base header out of the protobuf into our own BaseMessage.
        m_baseMessage->CopyFrom(m_pbRequest->message());
        PBFTBaseMessage::deserializeToObject();
    }

    bool operator==(PBFTRequest const& _pbftRequest) const
    {
        if (!PBFTBaseMessage::operator==(_pbftRequest))
        {
            return false;
        }
        return _pbftRequest.size() == size();
    }

private:
    std::unique_ptr<ProposalRequest> m_pbRequest;  // sole owner (PBFTRequest is never nested)
};
}  // namespace bcos::consensus
