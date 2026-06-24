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
 * @brief implementation of Proposal
 * @file Proposal.h
 * @author: yujiechen
 * @date 2021-04-09
 */
#pragma once
#include "bcos-framework/consensus/ProposalInterface.h"
#include "bcos-pbft/core/proto/Consensus.pb.h"
#include "bcos-pbft/pbft/utilities/Common.h"
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-protocol/Common.h>
#include <cassert>

namespace bcos::consensus
{
const bcos::protocol::BlockNumber InvalidBlockNumber = -1;
// FIB-121: Proposal no longer owns its protobuf. m_rawProposal is a NON-OWNING
// view into the active PBFTRawProposal's `proposal` submessage, owned by the
// derived PBFTProposal (either its standalone storage or a parent message's
// submessage). The derived class re-points it via setRawProposal() whenever the
// active protobuf changes. This removes the aliasing-shared_ptr / unsafe-arena
// dance that the prior dual-ownership model required.
class Proposal : virtual public ProposalInterface
{
public:
    using Ptr = std::shared_ptr<Proposal>;
    Proposal() = default;
    ~Proposal() override = default;

    // the index of the proposal
    bcos::protocol::BlockNumber index() const override { return m_rawProposal->index(); }
    void setIndex(bcos::protocol::BlockNumber _index) override { m_rawProposal->set_index(_index); }

    // the hash of the proposal
    bcos::crypto::HashType const& hash() const override { return m_hash; }
    void setHash(bcos::crypto::HashType const& _hash) override
    {
        m_hash = _hash;
        m_rawProposal->set_hash(_hash.data(), bcos::crypto::HashType::SIZE);
    }
    // the payload of the proposal
    bcos::bytesConstRef data() const override
    {
        auto const& data = m_rawProposal->data();
        return bcos::bytesConstRef((byte const*)data.c_str(), data.size());
    }
    void setData(bytes const& _data) override
    {
        m_rawProposal->set_data(_data.data(), _data.size());
    }

    void setData(bytes&& _data) override
    {
        auto size = _data.size();
        m_rawProposal->set_data(std::move(_data).data(), size);
    }

    void setData(bcos::bytesConstRef _data) override
    {
        m_rawProposal->set_data(_data.data(), _data.size());
    }

    bytesConstRef extraData() const override
    {
        auto const& extraData = m_rawProposal->extradata();
        return bytesConstRef((byte const*)extraData.data(), extraData.size());
    }
    void setExtraData(bytes const& _data) override
    {
        m_rawProposal->set_extradata(_data.data(), _data.size());
    }

    void setExtraData(bytes&& _data) override
    {
        auto dataSize = _data.size();
        m_rawProposal->set_extradata(std::move(_data).data(), dataSize);
    }
    void setExtraData(bcos::bytesConstRef _data) override
    {
        m_rawProposal->set_extradata(_data.data(), _data.size());
    }

    bytesConstRef signature() const override
    {
        auto const& signature = m_rawProposal->signature();
        return bcos::bytesConstRef((byte const*)signature.c_str(), signature.size());
    }

    void setSignature(bytes const& _data) override
    {
        m_rawProposal->set_signature(_data.data(), _data.size());
    }

    bool operator==(Proposal const _proposal) const
    {
        return _proposal.index() == index() && _proposal.hash() == hash() &&
               _proposal.data().toBytes() == data().toBytes();
    }
    bool operator!=(Proposal const _proposal) const { return !(operator==(_proposal)); }

    RawProposal* rawProposal() { return m_rawProposal; }

    bytesPointer encode() const override
    {
        // FIB-121: a bare Proposal has no backing storage — m_rawProposal is a
        // non-owning view supplied by the derived PBFTProposal, which overrides
        // encode()/decode(). These base versions are only reachable if someone
        // misuses Proposal directly; assert the invariant instead of UB.
        assert(m_rawProposal != nullptr);
        return bcos::protocol::encodePBObject(m_rawProposal);
    }
    void decode(bytesConstRef _data) override
    {
        assert(m_rawProposal != nullptr);
        bcos::protocol::decodePBObject(m_rawProposal, _data);
        deserializeObject();
    }

    void setSealerId(int64_t _sealerId) override { m_rawProposal->set_sealerid(_sealerId); }

    int64_t sealerId() override { return m_rawProposal->sealerid(); }

    bool systemProposal() const override { return m_rawProposal->systemproposal(); }
    void setSystemProposal(bool _systemProposal) override
    {
        m_rawProposal->set_systemproposal(_systemProposal);
    }

protected:
    // Re-point the non-owning view at the derived class's active submessage.
    // _runDeserialize controls whether the cached hash is re-validated/derived
    // (FIB-174): true after a decode (fields just changed), false for a plain
    // re-bind after a move/copy where the cached hash is carried separately.
    void setRawProposal(RawProposal* _rawProposal, bool _runDeserialize = true)
    {
        m_rawProposal = _rawProposal;
        if (_runDeserialize)
        {
            deserializeObject();
        }
    }
    virtual void deserializeObject()
    {
        auto const& hashData = m_rawProposal->hash();
        // FIB-174: drop any stale/default identity before validating, so a
        // failed decode on a reused object cannot retain the prior proposal's
        // hash. An empty hash field (size 0) is the not-yet-set state of a
        // freshly constructed proposal (e.g. PBFTProposal() before setHash());
        // leave m_hash default-constructed and do not treat that as malformed.
        m_hash = bcos::crypto::HashType{};
        if (hashData.empty())
        {
            return;
        }
        // FIB-149 / FIB-174: a present-but-non-canonical hash length (oversize OR
        // undersize) is malformed wire data — reject it at decode instead of
        // silently returning. Pre-fix used `<`, which silently truncated oversize
        // input to the first 32 bytes, and the undersize case returned without
        // surfacing the malformed input as an invalid PBFT message.
        if (hashData.size() != bcos::crypto::HashType::SIZE)
        {
            BOOST_THROW_EXCEPTION(InvalidPBFTMessage() << errinfo_comment(
                                      "Proposal::deserializeObject malformed hash length: " +
                                      std::to_string(hashData.size())));
        }
        m_hash =
            bcos::crypto::HashType((byte const*)hashData.c_str(), bcos::crypto::HashType::SIZE);
    }

protected:
    // non-owning: points into the derived PBFTProposal's active PBFTRawProposal
    RawProposal* m_rawProposal = nullptr;
    bcos::crypto::HashType m_hash;
};
}  // namespace bcos::consensus
