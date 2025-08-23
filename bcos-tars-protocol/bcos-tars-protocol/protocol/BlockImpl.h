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
 * @brief tars implementation for Block
 * @file BlockImpl.h
 * @author: ancelmo
 * @date 2021-04-20
 */
#pragma once
// if windows, manual include tup/Tars.h first
#ifdef _WIN32
#include <tup/Tars.h>
#endif
#include "TransactionMetaDataImpl.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-tars-protocol/tars/Block.h"
#include <bcos-crypto/hasher/AnyHasher.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/merkle/Merkle.h>
#include <bcos-framework/protocol/Block.h>
#include <bcos-framework/protocol/BlockHeader.h>
#include <gsl/span>
#include <memory>

namespace bcostars::protocol
{
class BlockImpl : public bcos::protocol::Block, public std::enable_shared_from_this<BlockImpl>
{
public:
    BlockImpl() = default;
    BlockImpl(const BlockImpl&) = delete;
    BlockImpl(BlockImpl&&) = delete;
    BlockImpl& operator=(const BlockImpl&) = delete;
    BlockImpl& operator=(BlockImpl&&) = delete;
    explicit BlockImpl(bcostars::Block _block) : BlockImpl() { m_inner = std::move(_block); }
    ~BlockImpl() noexcept override = default;

    void decode(bcos::bytesConstRef _data, bool _calculateHash, bool _checkSig) override;
    void encode(bcos::bytes& _encodeData) const override;

    int32_t version() const override { return m_inner.blockHeader.data.version; }
    void setVersion(int32_t _version) override { m_inner.blockHeader.data.version = _version; }

    bcos::protocol::BlockType blockType() const override;
    // FIXME: this will cause the same blockHeader calculate hash multiple times
    bcos::protocol::BlockHeader::Ptr blockHeader() override;
    bcos::protocol::BlockHeader::ConstPtr blockHeaderConst() const override;

    bcos::protocol::Transaction::ConstPtr transaction(uint64_t _index) const override;
    bcos::protocol::TransactionReceipt::ConstPtr receipt(uint64_t _index) const override;

    // get transaction metaData
    bcos::protocol::TransactionMetaData::ConstPtr transactionMetaData(
        uint64_t _index) const override;
    TransactionMetaDataImpl transactionMetaDataImpl(uint64_t _index) const;
    bcos::crypto::HashType transactionHash(uint64_t _index) const override;

    void setBlockType(bcos::protocol::BlockType _blockType) override;

    // set blockHeader
    void setBlockHeader(bcos::protocol::BlockHeader::Ptr _blockHeader) override;

    void setTransaction(uint64_t _index, bcos::protocol::Transaction::Ptr _transaction) override;
    void appendTransaction(bcos::protocol::Transaction::Ptr _transaction) override;

    void setReceipt(uint64_t _index, bcos::protocol::TransactionReceipt::Ptr _receipt) override;
    void appendReceipt(bcos::protocol::TransactionReceipt::Ptr _receipt) override;

    void appendTransactionMetaData(bcos::protocol::TransactionMetaData::Ptr _txMetaData) override;

    // get transactions size
    uint64_t transactionsSize() const override;
    uint64_t transactionsMetaDataSize() const override;
    // get receipts size
    uint64_t receiptsSize() const override;

    void setNonceList(RANGES::any_view<std::string> nonces) override;
    RANGES::any_view<std::string> nonceList() const override;

    const bcostars::Block& inner() const;
    bcostars::Block& inner();
    void setInner(bcostars::Block inner);

    bcos::crypto::HashType calculateTransactionRoot(
        const bcos::crypto::Hash& hashImpl) const override;

    bcos::crypto::HashType calculateReceiptRoot(const bcos::crypto::Hash& hashImpl) const override;

    bcos::protocol::ViewResult<bcos::crypto::HashType> transactionHashes() const override;
    bcos::protocol::ViewResult<bcos::protocol::AnyTransactionMetaData> transactionMetaDatas()
        const override;
    bcos::protocol::ViewResult<bcos::protocol::AnyTransaction> transactions() const override;
    bcos::protocol::ViewResult<bcos::protocol::AnyTransactionReceipt> receipts() const override;
    size_t size() const override;

private:
    mutable bcostars::Block m_inner;
};
}  // namespace bcostars::protocol
