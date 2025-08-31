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
 * @brief interface for Block
 * @file Block.h
 * @author: yujiechen
 * @date: 2021-03-23
 */
#pragma once
#include "BlockHeader.h"
#include "Transaction.h"
#include "TransactionMetaData.h"
#include "TransactionReceipt.h"
#include "bcos-crypto/interfaces/crypto/CommonType.h"
#include <range/v3/view/any_view.hpp>

namespace bcos::protocol
{
using HashList = std::vector<bcos::crypto::HashType>;
using HashListPtr = std::shared_ptr<HashList>;
using HashListConstPtr = std::shared_ptr<const HashList>;

enum BlockType : int32_t
{
    CompleteBlock = 1,
    WithTransactionsHash = 2,
};

template <class T>
using ViewResult = ::ranges::any_view<T, ::ranges::category::mask | ::ranges::category::sized>;

class Block
{
public:
    using Ptr = std::shared_ptr<Block>;
    using ConstPtr = std::shared_ptr<Block const>;
    Block() = default;
    Block(const Block&) = default;
    Block(Block&&) noexcept = default;
    Block& operator=(const Block&) = default;
    Block& operator=(Block&&) noexcept = default;
    virtual ~Block() noexcept = default;

    virtual void decode(bytesConstRef _data, bool _calculateHash, bool _checkSig) = 0;
    virtual void encode(bytes& _encodeData) const = 0;

    virtual bcos::crypto::HashType calculateTransactionRoot(const crypto::Hash& hashImpl) const = 0;
    virtual bcos::crypto::HashType calculateReceiptRoot(const crypto::Hash& hashImpl) const = 0;

    virtual int32_t version() const = 0;
    virtual void setVersion(int32_t _version) = 0;
    virtual BlockType blockType() const = 0;
    // blockHeader gets blockHeader
    virtual BlockHeader::ConstPtr blockHeader() const = 0;
    virtual BlockHeader::Ptr blockHeader() = 0;
    // get transactions
    virtual Transaction::ConstPtr transaction(uint64_t _index) const = 0;
    // get receipts
    virtual TransactionReceipt::ConstPtr receipt(uint64_t _index) const = 0;
    // get transaction metaData
    virtual TransactionMetaData::ConstPtr transactionMetaData(uint64_t _index) const = 0;
    // get transaction hash
    virtual bcos::crypto::HashType transactionHash(uint64_t _index) const = 0;

    virtual void setBlockType(BlockType _blockType) = 0;
    // setBlockHeader sets blockHeader
    virtual void setBlockHeader(BlockHeader::Ptr _blockHeader) = 0;
    // set transactions
    virtual void setTransaction(uint64_t _index, Transaction::Ptr _transaction) = 0;
    // FIXME: appendTransaction will create Transaction, the parameter should be object not pointer
    virtual void appendTransaction(Transaction::Ptr _transaction) = 0;
    // set receipts
    virtual void setReceipt(uint64_t _index, TransactionReceipt::Ptr _receipt) = 0;
    virtual void appendReceipt(TransactionReceipt::Ptr _receipt) = 0;
    // set transaction metaData
    // FIXME: appendTransactionMetaData will create, parameter should be object instead of pointer
    virtual void appendTransactionMetaData(TransactionMetaData::Ptr _txMetaData) = 0;

    // get transactions size
    virtual uint64_t transactionsSize() const = 0;
    virtual uint64_t transactionsMetaDataSize() const = 0;
    virtual uint64_t transactionsHashSize() const { return transactionsMetaDataSize(); }

    // get receipts size
    virtual uint64_t receiptsSize() const = 0;

    // for nonceList
    virtual void setNonceList(::ranges::any_view<std::string> nonces) = 0;
    virtual ::ranges::any_view<std::string> nonceList() const = 0;

    virtual NonceListPtr nonces() const
    {
        return std::make_shared<NonceList>(
            ::ranges::iota_view<size_t, size_t>(0LU, transactionsSize()) |
            ::ranges::views::transform([this](uint64_t index) {
                auto transaction = this->transaction(index);
                return transaction->nonce();
            }) |
            ::ranges::to<NonceList>());
    }

    virtual ViewResult<crypto::HashType> transactionHashes() const = 0;
    virtual ViewResult<AnyTransactionMetaData> transactionMetaDatas() const = 0;
    virtual ViewResult<AnyTransaction> transactions() const = 0;
    virtual ViewResult<AnyTransactionReceipt> receipts() const = 0;
    bool operator<(const Block& block) const
    {
        return blockHeader()->number() < block.blockHeader()->number();
    }
    virtual size_t size() const = 0;
};
using Blocks = std::vector<Block::Ptr>;
using BlocksPtr = std::shared_ptr<Blocks>;

}  // namespace bcos::protocol
