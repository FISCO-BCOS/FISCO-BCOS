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
 * @brief the proposal information
 * @file ProposalInterface.h
 * @author: yujiechen
 * @date 2021-04-09
 */
#pragma once
#include <bcos-framework/interfaces/protocol/ProtocolTypeDef.h>
namespace bcos
{
namespace consensus
{
class ProposalInterface
{
public:
    using Ptr = std::shared_ptr<ProposalInterface>;
    using ConstPtr = std::shared_ptr<ProposalInterface const>;
    ProposalInterface() = default;
    virtual ~ProposalInterface() {}

    virtual bytesPointer encode() const = 0;
    virtual void decode(bytesConstRef _data) = 0;

    // the index of the proposal
    virtual bcos::protocol::BlockNumber index() const = 0;
    virtual void setIndex(bcos::protocol::BlockNumber _index) = 0;

    // the hash of the proposal
    virtual bcos::crypto::HashType const& hash() const = 0;
    virtual void setHash(bcos::crypto::HashType const& _hash) = 0;
    // the data of the proposal
    virtual bcos::bytesConstRef data() const = 0;
    virtual void setData(bytes const& _data) = 0;
    virtual void setData(bytes&& _data) = 0;
    virtual void setData(bcos::bytesConstRef _data) = 0;

    // the extra data of the proposal
    virtual bcos::bytesConstRef extraData() const = 0;
    virtual void setExtraData(bytes const& _data) = 0;
    virtual void setExtraData(bytes&& _data) = 0;
    virtual void setExtraData(bcos::bytesConstRef _data) = 0;

    // the sealerId
    virtual void setSealerId(int64_t _sealerId) = 0;
    virtual int64_t sealerId() = 0;

    // the signature to the proposal(optional)
    virtual bytesConstRef signature() const = 0;
    virtual void setSignature(bytes const& _signature) = 0;

    // the proposal type
    virtual bool systemProposal() const = 0;
    virtual void setSystemProposal(bool _systemProposal) = 0;

    virtual void setReExecFlag(bool _reExecFlag) { m_reExecFlag = _reExecFlag; }
    virtual bool reExecFlag() const { return m_reExecFlag; }

private:
    bool m_reExecFlag = false;
};
using ProposalList = std::vector<ProposalInterface::Ptr>;
using ProposalListPtr = std::shared_ptr<ProposalList>;
}  // namespace consensus
}  // namespace bcos
