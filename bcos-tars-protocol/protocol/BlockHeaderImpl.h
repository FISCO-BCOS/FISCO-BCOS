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
 * @brief implementation for BlockHeader
 * @file BlockHeaderImpl.h
 * @author: ancelmo
 * @date 2021-04-20
 */

#pragma once
#include "bcos-tars-protocol/Common.h"
#include "bcos-tars-protocol/tars/Block.h"
#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-framework/interfaces/protocol/BlockHeader.h>
#include <bcos-framework/interfaces/protocol/ProtocolTypeDef.h>
#include <gsl/span>

namespace bcostars
{
namespace protocol
{
class BlockHeaderImpl : public bcos::protocol::BlockHeader
{
public:
    virtual ~BlockHeaderImpl() {}

    BlockHeaderImpl() = delete;

    BlockHeaderImpl(
        bcos::crypto::CryptoSuite::Ptr cryptoSuite, std::function<bcostars::BlockHeader*()> inner)
      : bcos::protocol::BlockHeader(cryptoSuite), m_inner(inner)
    {}

    void decode(bcos::bytesConstRef _data) override;
    void encode(bcos::bytes& _encodeData) const override;
    bcos::crypto::HashType hash() const override;

    void clear() override;

    int32_t version() const override { return m_inner()->data.version; }
    gsl::span<const bcos::protocol::ParentInfo> parentInfo() const override;

    bcos::crypto::HashType txsRoot() const override;
    bcos::crypto::HashType stateRoot() const override;
    bcos::crypto::HashType receiptsRoot() const override;
    bcos::protocol::BlockNumber number() const override { return m_inner()->data.blockNumber; }
    bcos::u256 gasUsed() const override;
    int64_t timestamp() const override { return m_inner()->data.timestamp; }
    int64_t sealer() const override { return m_inner()->data.sealer; }

    gsl::span<const bcos::bytes> sealerList() const override
    {
        return gsl::span(reinterpret_cast<const bcos::bytes*>(m_inner()->data.sealerList.data()),
            m_inner()->data.sealerList.size());
    }
    bcos::bytesConstRef extraData() const override
    {
        return bcos::bytesConstRef(
            reinterpret_cast<const bcos::byte*>(m_inner()->data.extraData.data()),
            m_inner()->data.extraData.size());
    }
    gsl::span<const bcos::protocol::Signature> signatureList() const override
    {
        return gsl::span(
            reinterpret_cast<const bcos::protocol::Signature*>(m_inner()->signatureList.data()),
            m_inner()->signatureList.size());
    }

    gsl::span<const uint64_t> consensusWeights() const override
    {
        return gsl::span(reinterpret_cast<const uint64_t*>(m_inner()->data.consensusWeights.data()),
            m_inner()->data.consensusWeights.size());
    }

    void setVersion(int32_t _version) override { m_inner()->data.version = _version; }

    void setParentInfo(gsl::span<const bcos::protocol::ParentInfo> const& _parentInfo) override;

    void setParentInfo(bcos::protocol::ParentInfoList&& _parentInfo) override
    {
        setParentInfo(gsl::span(_parentInfo.data(), _parentInfo.size()));
    }

    void setTxsRoot(bcos::crypto::HashType _txsRoot) override
    {
        m_inner()->data.txsRoot.assign(_txsRoot.begin(), _txsRoot.end());
    }
    void setReceiptsRoot(bcos::crypto::HashType _receiptsRoot) override
    {
        m_inner()->data.receiptRoot.assign(_receiptsRoot.begin(), _receiptsRoot.end());
    }
    void setStateRoot(bcos::crypto::HashType _stateRoot) override
    {
        m_inner()->data.stateRoot.assign(_stateRoot.begin(), _stateRoot.end());
    }
    void setNumber(bcos::protocol::BlockNumber _blockNumber) override
    {
        m_inner()->data.blockNumber = _blockNumber;
    }
    void setGasUsed(bcos::u256 _gasUsed) override
    {
        m_inner()->data.gasUsed = boost::lexical_cast<std::string>(_gasUsed);
    }
    void setTimestamp(int64_t _timestamp) override { m_inner()->data.timestamp = _timestamp; }
    void setSealer(int64_t _sealerId) override { m_inner()->data.sealer = _sealerId; }
    void setSealerList(gsl::span<const bcos::bytes> const& _sealerList) override;
    void setSealerList(std::vector<bcos::bytes>&& _sealerList) override
    {
        setSealerList(gsl::span(_sealerList.data(), _sealerList.size()));
    }

    void setConsensusWeights(gsl::span<const uint64_t> const& _weightList) override
    {
        m_inner()->data.consensusWeights.assign(_weightList.begin(), _weightList.end());
    }

    void setConsensusWeights(std::vector<uint64_t>&& _weightList) override
    {
        setConsensusWeights(gsl::span(_weightList.data(), _weightList.size()));
    }

    void setExtraData(bcos::bytes const& _extraData) override
    {
        m_inner()->data.extraData.assign(_extraData.begin(), _extraData.end());
    }
    void setExtraData(bcos::bytes&& _extraData) override
    {
        m_inner()->data.extraData.assign(_extraData.begin(), _extraData.end());
    }
    void setSignatureList(
        gsl::span<const bcos::protocol::Signature> const& _signatureList) override;

    void setSignatureList(bcos::protocol::SignatureList&& _signatureList) override
    {
        setSignatureList(gsl::span(_signatureList.data(), _signatureList.size()));
    }

    const bcostars::BlockHeader& inner() const { return *m_inner(); }

    void setInner(const bcostars::BlockHeader& blockHeader) { *m_inner() = blockHeader; }
    void setInner(bcostars::BlockHeader&& blockHeader) { *m_inner() = std::move(blockHeader); }

private:
    std::function<bcostars::BlockHeader*()> m_inner;
    mutable std::vector<bcos::protocol::ParentInfo> m_parentInfo;
};
}  // namespace protocol
}  // namespace bcostars