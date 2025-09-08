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
#include "ProtocolTypeDef.h"
#include "bcos-utilities/AnyHolder.h"
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <gsl/span>

namespace bcos::protocol
{
class BlockHeader : public virtual MoveBase<BlockHeader>
{
public:
    using Ptr = std::shared_ptr<BlockHeader>;
    using ConstPtr = std::shared_ptr<const BlockHeader>;
    using BlockHeadersPtr = std::shared_ptr<std::vector<BlockHeader::Ptr> >;
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

    virtual void populateFromParents(const crypto::Hash& hashImpl,
        const std::vector<BlockHeader::Ptr>& _parents, BlockNumber _number)
    {
        // set parentInfo
        ParentInfoList parentInfoList;
        for (const auto& parentHeader : _parents)
        {
            ParentInfo parentInfo;
            parentInfo.blockNumber = parentHeader->number();
            parentInfo.blockHash = parentHeader->hash();
            parentInfoList.emplace_back(parentInfo);
        }
        setParentInfo(parentInfoList);
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
            BOOST_THROW_EXCEPTION(InvalidBlockHeader() << errinfo_comment(
                                      "Invalid blockHeader for the size of sealerList "
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
                BOOST_THROW_EXCEPTION(
                    InvalidSignatureList()
                    << errinfo_comment("Invalid signatureList for verify failed, signatureData:" +
                                       *toHexString(signatureData)));
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

    // version returns the version of the blockHeader
    virtual uint32_t version() const = 0;
    // parentInfo returns the parent information, including (parentBlockNumber, parentHash)
    virtual RANGES::any_view<ParentInfo, RANGES::category::input | RANGES::category::sized>
    parentInfo() const = 0;
    // txsRoot returns the txsRoot of the current block
    virtual bcos::crypto::HashType txsRoot() const = 0;
    // receiptsRoot returns the receiptsRoot of the current block
    virtual bcos::crypto::HashType receiptsRoot() const = 0;
    // stateRoot returns the stateRoot of the current block
    virtual bcos::crypto::HashType stateRoot() const = 0;
    // number returns the number of the current block
    virtual BlockNumber number() const = 0;
    virtual u256 gasUsed() const = 0;
    virtual int64_t timestamp() const = 0;
    // sealer returns the sealer that generate this block
    virtual int64_t sealer() const = 0;
    // sealerList returns the current sealer list
    virtual gsl::span<const bytes> sealerList() const = 0;
    virtual bytesConstRef extraData() const = 0;
    virtual gsl::span<const Signature> signatureList() const = 0;
    virtual gsl::span<const uint64_t> consensusWeights() const = 0;

    virtual void setVersion(uint32_t _version) = 0;
    virtual void setParentInfo(RANGES::any_view<bcos::protocol::ParentInfo> parentInfo) = 0;

    virtual void setTxsRoot(bcos::crypto::HashType _txsRoot) = 0;
    virtual void setReceiptsRoot(bcos::crypto::HashType _receiptsRoot) = 0;
    virtual void setStateRoot(bcos::crypto::HashType _stateRoot) = 0;
    virtual void setNumber(BlockNumber _blockNumber) = 0;
    virtual void setGasUsed(u256 _gasUsed) = 0;
    virtual void setTimestamp(int64_t _timestamp) = 0;
    virtual void setSealer(int64_t _sealerId) = 0;

    virtual void setSealerList(gsl::span<const bytes> const& _sealerList) = 0;
    virtual void setSealerList(std::vector<bytes>&& _sealerList) = 0;

    virtual void setConsensusWeights(gsl::span<const uint64_t> const& _weightList) = 0;
    virtual void setConsensusWeights(std::vector<uint64_t>&& _weightList) = 0;

    virtual void setExtraData(bytes const& _extraData) = 0;
    virtual void setExtraData(bytes&& _extraData) = 0;

    virtual void setSignatureList(gsl::span<const Signature> const& _signatureList) = 0;
    virtual void setSignatureList(SignatureList&& _signatureList) = 0;

    virtual size_t size() const = 0;
};

using AnyBlockHeader = AnyHolder<BlockHeader, 48>;
}  // namespace bcos::protocol
