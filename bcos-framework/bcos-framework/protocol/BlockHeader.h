/*
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
 * @brief interface for BlockHeader
 * @file BlockHeader.h
 * @author: yujiechen
 * @date: 2021-03-22
 */
#pragma once
#include "Exceptions.h"
#include "Protocol.h"
#include "ProtocolTypeDef.h"
#include "bcos-utilities/AnyHolder.h"
#include "bcos-utilities/Bloom.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/Exceptions.h"
#include "bcos-utilities/FixedBytes.h"
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <gsl/span>
#include <range/v3/view/any_view.hpp>

namespace bcos::protocol
{
class BlockHeader
{
public:
    using Ptr = std::shared_ptr<BlockHeader>;
    using ConstPtr = std::shared_ptr<const BlockHeader>;

    BlockHeader() = default;
    BlockHeader(const BlockHeader&) = default;
    BlockHeader(BlockHeader&&) noexcept = default;
    BlockHeader& operator=(const BlockHeader&) = default;
    BlockHeader& operator=(BlockHeader&&) noexcept = default;
    virtual ~BlockHeader() noexcept = default;

    virtual void decode(bytesConstRef _data) = 0;
    virtual void encode(bytes& _encodeData) const = 0;

    virtual bcos::crypto::HashType hash() const = 0;
    virtual void calculateHash(const crypto::Hash& hashImpl) = 0;

    virtual void populateFromParents(
        const crypto::Hash& hashImpl, const BlockHeader& _parent, BlockNumber _number)
    {
        setParentInfo(ParentInfo{.blockNumber = _parent.number(), .blockHash = _parent.hash()});
        setNumber(_number);
    }

    virtual void clear() = 0;

    // verifySignatureList verifys the signatureList
    virtual void verifySignatureList(
        const crypto::Hash& hashImpl, const crypto::SignatureCrypto& signatureImpl) const
    {
        auto signatures = signatureList();
        auto sealers = sealerList();
        if (signatures.size() < sealers.size())
        {
            throwTrace(InvalidBlockHeader()
                       << errinfo_comment("Invalid blockHeader for the size of sealerList "
                                          "is smaller than the size of signatureList"));
        }
        for (const auto& signature : signatures)
        {
            auto sealerIndex = signature.index;
            auto signatureData = signature.signature;
            if (!signatureImpl.verify(
                    std::shared_ptr<const bytes>(&((sealers)[sealerIndex]), [](const bytes*) {}),
                    hash(), bytesConstRef(signatureData.data(), signatureData.size())))
            {
                throwTrace(InvalidSignatureList() << errinfo_comment(
                               "Invalid signatureList for verify failed, signatureData:" +
                               toHex(signatureData)));
            }
        }
    }
    virtual void populateEmptyBlock(
        BlockNumber _number, int64_t _sealerId, int64_t _timestamp = utcTime())
    {
        setNumber(_number);
        setSealer(_sealerId);
        setTimestamp(_timestamp);
    }

    // ---- FISCO-BCOS specific methods ----
    virtual uint32_t version() const = 0;
    // ethBlockVersion marks the Ethereum fork era of this header (see EthBlockVersion in
    // Protocol.h). NON_ETH means the header is a native FISCO-BCOS header; values 1..5 select
    // the mandatory optional fields for the corresponding Ethereum fork.
    virtual bcos::protocol::EthBlockVersion ethBlockVersion() const = 0;
    virtual void setEthBlockVersion(bcos::protocol::EthBlockVersion _version) = 0;
    // Inject a pre-computed Ethereum RLP hash (set by the rlp-protocol layer via
    // EthBlockHeader::calculateRLPHash). For Eth headers (ethBlockVersion() != NON_ETH)
    // calculateHash() keeps this value instead of recomputing the FISCO Tars hash.
    virtual void setRLPHash(bcos::crypto::HashType _hash) = 0;
    // sealer returns the sealer that generate this block
    virtual int64_t sealer() const = 0;
    // sealerList returns the current sealer list
    virtual gsl::span<const bytes> sealerList() const = 0;
    virtual gsl::span<const Signature> signatureList() const = 0;
    virtual gsl::span<const uint64_t> consensusWeights() const = 0;

    virtual void setVersion(uint32_t _version) = 0;
    virtual void setSealer(int64_t _sealerId) = 0;
    virtual void setSealerList(gsl::span<const bytes> const& _sealerList) = 0;
    virtual void setSealerList(std::vector<bytes>&& _sealerList) = 0;
    virtual void setConsensusWeights(gsl::span<const uint64_t> const& _weightList) = 0;
    virtual void setConsensusWeights(std::vector<uint64_t>&& _weightList) = 0;
    virtual void setSignatureList(gsl::span<const Signature> const& _signatureList) = 0;
    virtual void setSignatureList(SignatureList&& _signatureList) = 0;

    // ---- Shared methods (FISCO ↔ Eth) ----
    virtual ParentInfo parentInfo() const = 0;
    virtual bcos::crypto::HashType txsRoot() const = 0;
    virtual bcos::crypto::HashType receiptsRoot() const = 0;
    virtual bcos::crypto::HashType stateRoot() const = 0;
    virtual BlockNumber number() const = 0;
    virtual u256 gasUsed() const = 0;
    virtual int64_t timestamp() const = 0;
    virtual bytesConstRef extraData() const = 0;

    virtual void setParentInfo(bcos::protocol::ParentInfo parentInfo) = 0;
    virtual void setTxsRoot(bcos::crypto::HashType _txsRoot) = 0;
    virtual void setReceiptsRoot(bcos::crypto::HashType _receiptsRoot) = 0;
    virtual void setStateRoot(bcos::crypto::HashType _stateRoot) = 0;
    virtual void setNumber(BlockNumber _blockNumber) = 0;
    virtual void setGasUsed(u256 _gasUsed) = 0;
    virtual void setTimestamp(int64_t _timestamp) = 0;

    virtual void setExtraData(bytes _extraData) = 0;

    virtual size_t size() const = 0;

    // ---- Ethereum-specific header field accessors ----
    virtual bcos::Address coinbase() const = 0;
    virtual void setCoinbase(bcos::Address _addr) = 0;

    virtual bcos::bytesConstRef logsBloom() const = 0;
    virtual void setLogsBloom(bcos::bytesConstRef _bloom) = 0;

    virtual u256 gasLimit() const = 0;
    virtual void setGasLimit(u256 _limit) = 0;

    virtual bcos::h256 prevRandao() const = 0;
    virtual void setPrevRandao(bcos::h256 _digest) = 0;

    virtual bcos::crypto::HashType uncleHash() const = 0;
    virtual void setUncleHash(bcos::crypto::HashType _hash) = 0;
    virtual bcos::u256 difficulty() const = 0;
    virtual void setDifficulty(bcos::u256 _difficulty) = 0;
    virtual bcos::h64 nonce() const = 0;
    virtual void setNonce(bcos::h64 _nonce) = 0;

    // Eth optional fields
    virtual std::optional<u256> baseFee() const = 0;
    virtual void setBaseFee(u256 _fee) = 0;

    virtual std::optional<bcos::h256> withdrawalsRoot() const = 0;
    virtual void setWithdrawalsRoot(bcos::h256 _hash) = 0;

    virtual std::optional<u256> blobGasUsed() const = 0;
    virtual void setBlobGasUsed(u256 _val) = 0;

    virtual std::optional<u256> excessBlobGas() const = 0;
    virtual void setExcessBlobGas(u256 _val) = 0;

    virtual std::optional<bcos::h256> parentBeaconBlockRoot() const = 0;
    virtual void setParentBeaconBlockRoot(bcos::h256 _root) = 0;

    virtual std::optional<bcos::h256> requestsHash() const = 0;
    virtual void setRequestsHash(bcos::h256 _hash) = 0;
};

using AnyBlockHeader = AnyHolder<BlockHeader, 72>;  // 多平台BlockHeaderImpl的最大尺寸 (Maximum size
                                                    // of BlockHeaderImpl across platforms)

}  // namespace bcos::protocol
