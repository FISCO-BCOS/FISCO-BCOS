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
 * @brief extend for PreparedProof
 * @file PreparedPoof.h
 * @author: yujiechen
 * @date 2021-04-15
 */
#pragma once
#include "bcos-pbft/core/Proposal.h"
#include "bcos-pbft/pbft/protocol/proto/PBFT.pb.h"
namespace bcos
{
namespace consensus
{
class PBFTProposal : public Proposal, virtual public PBFTProposalInterface
{
public:
    using Ptr = std::shared_ptr<PBFTProposal>;
    PBFTProposal() : Proposal()
    {
        m_pbftRawProposal = std::make_shared<PBFTRawProposal>();
        m_pbftRawProposal->set_allocated_proposal(rawProposal().get());
    }
    explicit PBFTProposal(bytesConstRef _data) : Proposal()
    {
        m_pbftRawProposal = std::make_shared<PBFTRawProposal>();
        decode(_data);
    }
    explicit PBFTProposal(std::shared_ptr<PBFTRawProposal> _pbftRawProposal)
      : Proposal(std::shared_ptr<RawProposal>(_pbftRawProposal->mutable_proposal()))
    {
        m_pbftRawProposal = _pbftRawProposal;
    }

    ~PBFTProposal() override { m_pbftRawProposal->unsafe_arena_release_proposal(); }

    std::shared_ptr<PBFTRawProposal> pbftRawProposal() { return m_pbftRawProposal; }

    size_t signatureProofSize() const override { return m_pbftRawProposal->signaturelist_size(); }

    std::pair<int64_t, bytesConstRef> signatureProof(size_t _index) const override
    {
        auto const& signatureData = m_pbftRawProposal->signaturelist(_index);
        auto signatureDataRef =
            bytesConstRef((byte const*)signatureData.c_str(), signatureData.size());
        return std::make_pair(m_pbftRawProposal->nodelist(_index), signatureDataRef);
    }

    void appendSignatureProof(int64_t _nodeIdx, bytesConstRef _signatureData) override
    {
        m_pbftRawProposal->add_nodelist(_nodeIdx);
        m_pbftRawProposal->add_signaturelist(_signatureData.data(), _signatureData.size());
    }

    void clearSignatureProof() override
    {
        m_pbftRawProposal->clear_nodelist();
        m_pbftRawProposal->clear_signaturelist();
    }

    bool operator==(PBFTProposal const& _proposal) const
    {
        if (!Proposal::operator==(_proposal))
        {
            return false;
        }
        // check the signatureProof
        if (_proposal.signatureProofSize() != signatureProofSize())
        {
            return false;
        }
        size_t proofSize = signatureProofSize();
        for (size_t i = 0; i < proofSize; i++)
        {
            auto proof = _proposal.signatureProof(i);
            auto comparedProof = signatureProof(i);
            if (proof.first != comparedProof.first ||
                proof.second.toBytes() != comparedProof.second.toBytes())
            {
                return false;
            }
        }
        return true;
    }

    bool operator!=(PBFTProposal const& _proposal) const { return !(operator==(_proposal)); }

    bytesPointer encode() const override
    {
        return bcos::protocol::encodePBObject(m_pbftRawProposal);
    }
    void decode(bytesConstRef _data) override
    {
        bcos::protocol::decodePBObject(m_pbftRawProposal, _data);
        setRawProposal(std::shared_ptr<RawProposal>(m_pbftRawProposal->mutable_proposal()));
    }

private:
    std::shared_ptr<PBFTRawProposal> m_pbftRawProposal;
};
}  // namespace consensus
}  // namespace bcos