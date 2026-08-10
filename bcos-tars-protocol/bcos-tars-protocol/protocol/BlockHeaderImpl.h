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
 * @brief implementation for BlockHeader — unified FISCO-BCOS + Ethereum
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
#include <bcos-framework/protocol/Protocol.h>
#include <bcos-framework/protocol/ProtocolTypeDef.h>
#include <bcos-utilities/Error.h>
#include <gsl/span>
#include <memory>
#include <range/v3/view/any_view.hpp>

namespace bcostars::protocol
{
class BlockHeaderImpl : public bcos::protocol::BlockHeader
{
public:
    explicit BlockHeaderImpl(std::shared_ptr<bcostars::BlockHeader> inner);
    BlockHeaderImpl();
    BlockHeaderImpl(const BlockHeaderImpl&) = default;
    BlockHeaderImpl(BlockHeaderImpl&&) noexcept = default;
    BlockHeaderImpl& operator=(const BlockHeaderImpl&) = default;
    BlockHeaderImpl& operator=(BlockHeaderImpl&&) noexcept = default;
    ~BlockHeaderImpl() noexcept override = default;

    void decode(bcos::bytesConstRef _data) override;
    void encode(bcos::bytes& _encodeData) const override;
    bcos::crypto::HashType hash() const override;
    void calculateHash(const bcos::crypto::Hash& hashImpl) override;

    void clear() override;

    uint32_t version() const override;
    bcos::protocol::EthBlockVersion ethBlockVersion() const override;
    void setEthBlockVersion(bcos::protocol::EthBlockVersion _version) override;
    bcos::protocol::ParentInfo parentInfo() const override;

    bcos::crypto::HashType txsRoot() const override;
    bcos::crypto::HashType stateRoot() const override;
    bcos::crypto::HashType receiptsRoot() const override;
    bcos::protocol::BlockNumber number() const override;
    bcos::u256 gasUsed() const override;
    int64_t timestamp() const override;
    int64_t sealer() const override;

    gsl::span<const bcos::bytes> sealerList() const override;
    bcos::bytesConstRef extraData() const override;
    gsl::span<const bcos::protocol::Signature> signatureList() const override;
    gsl::span<const uint64_t> consensusWeights() const override;

    void setVersion(uint32_t _version) override;
    void setParentInfo(bcos::protocol::ParentInfo parentInfo) override;
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
    void setExtraData(bcos::bytes _extraData) override;
    void setSignatureList(
        gsl::span<const bcos::protocol::Signature> const& _signatureList) override;
    void setSignatureList(bcos::protocol::SignatureList&& _signatureList) override;

    // ---- Ethereum-specific header field accessors ----
    bcos::Address coinbase() const override;
    void setCoinbase(bcos::Address _addr) override;

    bcos::bytesConstRef logsBloom() const override;
    void setLogsBloom(bcos::bytesConstRef _bloom) override;

    bcos::u256 gasLimit() const override;
    void setGasLimit(bcos::u256 _limit) override;

    bcos::h256 prevRandao() const override;
    void setPrevRandao(bcos::h256 _digest) override;

    bcos::crypto::HashType uncleHash() const override;
    void setUncleHash(bcos::crypto::HashType _hash) override;
    bcos::u256 difficulty() const override;
    void setDifficulty(bcos::u256 _difficulty) override;
    bcos::h64 nonce() const override;
    void setNonce(bcos::h64 _nonce) override;

    std::optional<bcos::u256> baseFee() const override;
    void setBaseFee(bcos::u256 _fee) override;

    std::optional<bcos::h256> withdrawalsRoot() const override;
    void setWithdrawalsRoot(bcos::h256 _hash) override;

    std::optional<bcos::u256> blobGasUsed() const override;
    void setBlobGasUsed(bcos::u256 _val) override;

    std::optional<bcos::u256> excessBlobGas() const override;
    void setExcessBlobGas(bcos::u256 _val) override;

    std::optional<bcos::h256> parentBeaconBlockRoot() const override;
    void setParentBeaconBlockRoot(bcos::h256 _root) override;

    std::optional<bcos::h256> requestsHash() const override;
    void setRequestsHash(bcos::h256 _hash) override;

    // Inject a pre-computed Ethereum RLP hash (set by the rlp-protocol layer via
    // EthBlockHeader::calculateHash). For Eth headers (ethBlockVersion() != NON_ETH)
    // calculateHash() keeps this value instead of recomputing the FISCO Tars hash.
    void setRLPHash(bcos::crypto::HashType _hash) override;

    const bcostars::BlockHeader& inner() const;
    bcostars::BlockHeader& inner();
    void setInner(bcostars::BlockHeader blockHeader);
    size_t size() const override;

private:
    // Note: When the field in the header used to calculate the hash changes, the dataHash needs to
    // be cleaned up
    void clearDataHash();

    std::shared_ptr<bcostars::BlockHeader> m_inner;
};
}  // namespace bcostars::protocol
