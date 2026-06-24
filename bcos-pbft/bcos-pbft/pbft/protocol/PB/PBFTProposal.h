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
 * @file PBFTProposal.h
 * @author: yujiechen
 * @date 2021-04-15
 */
#pragma once
#include "bcos-pbft/core/Proposal.h"
#include "bcos-pbft/pbft/protocol/proto/PBFT.pb.h"
#include "bcos-pbft/pbft/utilities/Common.h"
#include <memory>

namespace bcos::consensus
{
// FIB-121: value + non-owning-view type.
//
// A PBFTProposal is one of two modes:
//   STANDALONE: it owns its PBFTRawProposal on the heap (m_owned != nullptr,
//               m_active == m_owned.get()).
//   VIEW:       it is a non-owning window onto a PBFTRawProposal owned by a
//               parent message's protobuf (m_owned == nullptr, m_active points
//               into the parent). Created via PBFTProposal::asView().
//
// All accessors operate through m_active. The Proposal base holds a non-owning
// RawProposal* into m_active->proposal, re-pointed by bindActiveView().
//
// m_owned is a unique_ptr (not by-value) so a STANDALONE's protobuf keeps a
// stable heap address across moves of the wrapper (e.g. std::vector realloc) —
// the defaulted move operations therefore need no pointer fix-up. A VIEW never
// outlives the parent protobuf it points into (enforced by the owning message).
// FIB-121: the single-impl PBFTProposalInterface was collapsed into this class.
// PBFTProposal IS-A framework ProposalInterface (via Proposal) for the bcos-pbft
// boundary; the signature-proof methods below are the PBFT-specific additions.
class PBFTProposal : public Proposal
{
public:
    using Ptr = std::shared_ptr<PBFTProposal>;

    PBFTProposal() : m_owned(std::make_unique<PBFTRawProposal>()), m_active(m_owned.get())
    {
        bindActiveView(true);
    }
    explicit PBFTProposal(bytesConstRef _data)
      : m_owned(std::make_unique<PBFTRawProposal>()), m_active(m_owned.get())
    {
        decode(_data);
    }

    // Non-owning view onto a parent-owned submessage.
    static PBFTProposal asView(PBFTRawProposal* _sub) { return PBFTProposal(ViewTag{}, _sub); }

    // Copy duplicates the viewed/owned data into a fresh STANDALONE (deep copy).
    PBFTProposal(PBFTProposal const& _other)
      : Proposal(_other),
        m_owned(std::make_unique<PBFTRawProposal>(*_other.m_active)),
        m_active(m_owned.get())
    {
        // re-point the base view at our own copy; keep the hash copied by Proposal(_other)
        bindActiveView(false);
    }
    PBFTProposal& operator=(PBFTProposal const& _other)
    {
        PBFTProposal tmp(_other);
        *this = std::move(tmp);
        return *this;
    }
    // Defaulted moves are correct: m_owned is a unique_ptr (stable pointee),
    // m_active and the base RawProposal* remain valid after the wrapper moves.
    PBFTProposal(PBFTProposal&&) noexcept = default;
    PBFTProposal& operator=(PBFTProposal&&) noexcept = default;

    ~PBFTProposal() override = default;

    PBFTRawProposal* pbftRawProposal() { return m_active; }
    PBFTRawProposal const* pbftRawProposal() const { return m_active; }

    size_t signatureProofSize() const { return m_active->signaturelist_size(); }

    std::pair<int64_t, bytesConstRef> signatureProof(size_t _index) const
    {
        // FIB-120: defensive bounds check. decode() already enforces
        // signatureList.size() == nodeList.size(), so this should never fire
        // for objects built via decode(); it catches programming errors when
        // a PBFTProposal is constructed by other means.
        if (_index >= static_cast<size_t>(m_active->signaturelist_size()) ||
            _index >= static_cast<size_t>(m_active->nodelist_size()))
        {
            BOOST_THROW_EXCEPTION(InvalidPBFTMessage() << errinfo_comment(
                                      "PBFTProposal::signatureProof index out of bounds: index=" +
                                      std::to_string(_index) + " signatureList=" +
                                      std::to_string(m_active->signaturelist_size()) +
                                      " nodeList=" + std::to_string(m_active->nodelist_size())));
        }
        auto const& signatureData = m_active->signaturelist(_index);
        auto signatureDataRef =
            bytesConstRef((byte const*)signatureData.c_str(), signatureData.size());
        return std::make_pair(m_active->nodelist(_index), signatureDataRef);
    }

    void appendSignatureProof(int64_t _nodeIdx, bytesConstRef _signatureData)
    {
        m_active->add_nodelist(_nodeIdx);
        m_active->add_signaturelist(_signatureData.data(), _signatureData.size());
    }

    void clearSignatureProof()
    {
        m_active->clear_nodelist();
        m_active->clear_signaturelist();
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

    bytesPointer encode() const override { return bcos::protocol::encodePBObject(m_active); }
    void decode(bytesConstRef _data) override
    {
        bcos::protocol::decodePBObject(m_active, _data);
        // FIB-123: reject excessive signatureList / nodeList to prevent memory exhaustion
        validateRepeatedSize(
            m_active->signaturelist(), MAX_PBFT_REPEATED_FIELD_SIZE, "signatureList");
        validateRepeatedSize(m_active->nodelist(), MAX_PBFT_REPEATED_FIELD_SIZE, "nodeList");
        // FIB-120: signatureProof(i) indexes signaturelist(i) AND nodelist(i) in lockstep;
        // a malicious peer can send size(signatureList) != size(nodeList) to drive OOB
        // reads at downstream call sites (PBFTCacheProcessor::checkPrecommitWeight,
        // LedgerStorage::asyncCommitStableCheckPoint, PBFTMessageFactory::populateFrom).
        // Require strict 1:1 correspondence before exposing this object.
        if (m_active->signaturelist_size() != m_active->nodelist_size())
        {
            BOOST_THROW_EXCEPTION(
                InvalidPBFTMessage() << errinfo_comment(
                    "PBFTProposal::decode signatureList/nodeList size mismatch: signatureList=" +
                    std::to_string(m_active->signaturelist_size()) +
                    " nodeList=" + std::to_string(m_active->nodelist_size())));
        }
        // re-point the base view: ParseFromArray may have (re)allocated the inner
        // proposal submessage, and re-validate/cache the hash (FIB-174).
        bindActiveView(true);
    }

private:
    struct ViewTag
    {
    };
    PBFTProposal(ViewTag, PBFTRawProposal* _sub) : m_owned(nullptr), m_active(_sub)
    {
        bindActiveView(true);
    }

    // Re-point Proposal::m_rawProposal at m_active's inner proposal submessage.
    void bindActiveView(bool _runDeserialize)
    {
        setRawProposal(m_active->mutable_proposal(), _runDeserialize);
    }

    std::unique_ptr<PBFTRawProposal> m_owned;  // non-null iff STANDALONE
    PBFTRawProposal* m_active;                 // active protobuf (owned or parent-owned)
};

// FIB-121: value list (was std::vector<PBFTProposalInterface::Ptr>).
using PBFTProposalList = std::vector<PBFTProposal>;
using PBFTProposalListPtr = std::shared_ptr<PBFTProposalList>;

// Accepts any pointer-like proposal handle (PBFTProposal*, shared_ptr, framework
// ProposalInterface::Ptr); call sites with a value pass its address.
template <typename T>
inline std::string printPBFTProposal(T const& _proposal)
{
    std::ostringstream stringstream;
    stringstream << LOG_KV("propHash", _proposal->hash().abridged())
                 << LOG_KV("propIndex", _proposal->index());
    return stringstream.str();
}
}  // namespace bcos::consensus
