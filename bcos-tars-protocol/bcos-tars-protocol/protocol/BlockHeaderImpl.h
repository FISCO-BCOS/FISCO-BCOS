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
// if windows, manual include tup/Tars.h first
#ifdef _WIN32
#include <tup/Tars.h>
#endif
#include "bcos-tars-protocol/tars/Block.h"
#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-framework/protocol/ProtocolTypeDef.h>
#include <gsl/span>

namespace bcostars::protocol
{
class BlockHeaderImpl : public bcos::protocol::BlockHeader
{
public:
    BlockHeaderImpl(std::function<bcostars::BlockHeader*()> inner) : m_inner(std::move(inner)) {}
    ~BlockHeaderImpl() override = default;

    void decode(bcos::bytesConstRef _data) override;
    void encode(bcos::bytes& _encodeData) const override;
    bcos::crypto::HashType hash() const override;
    void calculateHash(const bcos::crypto::Hash& hashImpl) override;

    void clear() override;

    uint32_t version() const override { return m_inner()->data.version; }
    RANGES::any_view<bcos::protocol::ParentInfo, RANGES::category::input | RANGES::category::sized>
    parentInfo() const override;

    bcos::crypto::HashType txsRoot() const override;
    bcos::crypto::HashType stateRoot() const override;
    bcos::crypto::HashType receiptsRoot() const override;
    bcos::protocol::BlockNumber number() const override { return m_inner()->data.blockNumber; }
    bcos::u256 gasUsed() const override;
    int64_t timestamp() const override { return m_inner()->data.timestamp; }
    int64_t sealer() const override { return m_inner()->data.sealer; }

    gsl::span<const bcos::bytes> sealerList() const override;
    bcos::bytesConstRef extraData() const override;
    gsl::span<const bcos::protocol::Signature> signatureList() const override;

    gsl::span<const uint64_t> consensusWeights() const override;

    void setVersion(uint32_t _version) override;

    void setParentInfo(RANGES::any_view<bcos::protocol::ParentInfo> parentInfo) override;

    void setTxsRoot(bcos::crypto::HashType _txsRoot) override;
    void setReceiptsRoot(bcos::crypto::HashType _receiptsRoot) override;
    void setStateRoot(bcos::crypto::HashType _stateRoot) override;
    void setNumber(bcos::protocol::BlockNumber _blockNumber) override;
    void setGasUsed(bcos::u256 _gasUsed) override;
    void setTimestamp(int64_t _timestamp) override;
    void setSealer(int64_t _sealerId) override;
    void setSealerList(gsl::span<const bcos::bytes> const& _sealerList) override;
    void setSealerList(std::vector<bcos::bytes>&& _sealerList) override;

    void setConsensusWeights(gsl::span<const uint64_t> const& _weightList) override;

    void setConsensusWeights(std::vector<uint64_t>&& _weightList) override;

    void setExtraData(bcos::bytes const& _extraData) override;
    void setExtraData(bcos::bytes&& _extraData) override;
    void setSignatureList(
        gsl::span<const bcos::protocol::Signature> const& _signatureList) override;
    void setSignatureList(bcos::protocol::SignatureList&& _signatureList) override;
    const bcostars::BlockHeader& inner() const;

    // Notice: without lock, be aware of thread-safety issues
    bcostars::BlockHeader& mutableInner();
    void setInner(const bcostars::BlockHeader& blockHeader);
    void setInner(bcostars::BlockHeader&& blockHeader);


private:
    // Note: When the field in the header used to calculate the hash changes, the dataHash needs to
    // be cleaned up
    void clearDataHash();

    std::function<bcostars::BlockHeader*()> m_inner;
    mutable bcos::SharedMutex x_inner;
};
}  // namespace bcostars::protocol