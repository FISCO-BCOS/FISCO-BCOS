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
 * @brief protobuf implementation of Block
 * @file PBBlock.h
 * @author: yujiechen
 * @date: 2021-03-23
 */
#pragma once
#include "../Common.h"
#include "../ParallelMerkleProof.h"
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-framework/protocol/Block.h>
#include <bcos-framework/protocol/BlockHeaderFactory.h>
#include <bcos-framework/protocol/TransactionMetaData.h>
#include <bcos-protocol/protobuf/proto/Block.pb.h>
namespace bcos
{
namespace protocol
{
class PBBlock : public Block
{
public:
    using Ptr = std::shared_ptr<PBBlock>;
    PBBlock(BlockHeaderFactory::Ptr _blockHeaderFactory,
        TransactionFactory::Ptr _transactionFactory, TransactionReceiptFactory::Ptr _receiptFactory)
      : Block(_transactionFactory, _receiptFactory),
        m_blockHeaderFactory(_blockHeaderFactory),
        m_pbRawBlock(std::make_shared<PBRawBlock>()),
        m_transactions(std::make_shared<Transactions>()),
        m_receipts(std::make_shared<Receipts>()),
        m_transactionMetaDataList(std::make_shared<TransactionMetaDataList>()),
        m_nonceList(std::make_shared<NonceList>())
    {
        assert(m_blockHeaderFactory);
        assert(m_transactionFactory);
        assert(m_receiptFactory);
    }

    PBBlock(BlockHeaderFactory::Ptr _blockHeaderFactory,
        TransactionFactory::Ptr _transactionFactory, TransactionReceiptFactory::Ptr _receiptFactory,
        bytesConstRef _data, bool _calculateHash, bool _checkSig)
      : PBBlock(_blockHeaderFactory, _transactionFactory, _receiptFactory)
    {
        decode(_data, _calculateHash, _checkSig);
    }

    PBBlock(BlockHeaderFactory::Ptr _blockHeaderFactory,
        TransactionFactory::Ptr _transactionFactory, TransactionReceiptFactory::Ptr _receiptFactory,
        bytes const& _data, bool _calculateHash, bool _checkSig)
      : PBBlock(_blockHeaderFactory, _transactionFactory, _receiptFactory, ref(_data),
            _calculateHash, _checkSig)
    {}

    ~PBBlock() override
    {
        // return the ownership of rawProposal to the passed-in proposal
        clearTransactionMetaDataCache();
    }

    void decode(bytesConstRef _data, bool _calculateHash, bool _checkSig) override;
    void encode(bytes& _encodeData) const override;

    // getNonces of the current block
    Transaction::ConstPtr transaction(uint64_t _index) const override;
    TransactionMetaData::ConstPtr transactionMetaData(uint64_t _index) const override;

    TransactionReceipt::ConstPtr receipt(uint64_t _index) const override;

    int32_t version() const override { return m_pbRawBlock->version(); }

    void setVersion(int32_t _version) override { m_pbRawBlock->set_version(_version); }

    BlockType blockType() const override { return (BlockType)m_pbRawBlock->type(); }
    // get blockHeader
    BlockHeader::ConstPtr blockHeaderConst() const override { return m_blockHeader; }
    BlockHeader::Ptr blockHeader() override { return m_blockHeader; }
    // get transactions
    TransactionsConstPtr transactions() const { return m_transactions; }  // removed
    // get receipts
    ReceiptsConstPtr receipts() const { return m_receipts; }  // removed
    // get transaction hash
    TransactionMetaDataList const& transactionsMetaData() const
    {
        return *m_transactionMetaDataList;
    }  // removed

    void setBlockType(BlockType _blockType) override
    {
        m_pbRawBlock->set_type((int32_t)_blockType);
    }
    // set blockHeader
    void setBlockHeader(BlockHeader::Ptr _blockHeader) override { m_blockHeader = _blockHeader; }
    // set transactions
    void setTransactions(TransactionsPtr _transactions)  // removed
    {
        m_transactions = _transactions;
        clearTransactionsCache();
    }
    // Note: the caller must ensure the allocated transactions size
    void setTransaction(uint64_t _index, Transaction::Ptr _transaction) override
    {
        if (m_transactions->size() <= _index)
        {
            m_transactions->resize(_index + 1);
        }
        (*m_transactions)[_index] = _transaction;
        clearTransactionsCache();
    }
    void appendTransaction(Transaction::Ptr _transaction) override
    {
        m_transactions->push_back(_transaction);
        clearTransactionsCache();
    }
    // set receipts
    void setReceipts(ReceiptsPtr _receipts)  // removed
    {
        m_receipts = _receipts;
        // clear the cache
        clearReceiptsCache();
    }
    // Note: the caller must ensure the allocated receipts size
    void setReceipt(uint64_t _index, TransactionReceipt::Ptr _receipt) override
    {
        if (m_receipts->size() <= _index)
        {
            m_receipts->resize(_index + 1);
        }
        (*m_receipts)[_index] = _receipt;
        clearReceiptsCache();
    }

    void appendReceipt(TransactionReceipt::Ptr _receipt) override
    {
        m_receipts->push_back(_receipt);
        clearReceiptsCache();
    }
    void appendTransactionMetaData(TransactionMetaData::Ptr _txMetaData) override
    {
        m_transactionMetaDataList->emplace_back(_txMetaData);
    }

    // get transactions size
    uint64_t transactionsSize() const override { return m_transactions->size(); }
    // get receipts size
    uint64_t receiptsSize() const override { return m_receipts->size(); }

    uint64_t transactionsMetaDataSize() const override { return m_transactionMetaDataList->size(); }
    void setNonceList(NonceList const& _nonceList) override
    {
        *m_nonceList = _nonceList;
        m_pbRawBlock->clear_noncelist();
    }

    void setNonceList(NonceList&& _nonceList) override
    {
        *m_nonceList = std::move(_nonceList);
        m_pbRawBlock->clear_noncelist();
    }
    NonceList const& nonceList() const override { return *m_nonceList; }

    bcos::crypto::HashType calculateTransactionRoot() const override
    {
        auto txsRoot = bcos::crypto::HashType();
        // with no transactions
        if (transactionsSize() == 0 && transactionsMetaDataSize() == 0)
        {
            return txsRoot;
        }

        std::vector<bytes> transactionsList;
        if (transactionsSize() > 0)
        {
            transactionsList = bcos::protocol::encodeToCalculateRoot(transactionsSize(),
                [this](uint64_t _index) { return transaction(_index)->hash(); });
        }
        else if (transactionsMetaDataSize() > 0)
        {
            transactionsList = bcos::protocol::encodeToCalculateRoot(transactionsHashSize(),
                [this](uint64_t _index) { return transactionMetaData(_index)->hash(); });
        }

        txsRoot = bcos::protocol::calculateMerkleProofRoot(
            m_transactionFactory->cryptoSuite(), transactionsList);
        return txsRoot;
    }

    bcos::crypto::HashType calculateReceiptRoot() const override
    {
        auto receiptsRoot = bcos::crypto::HashType();
        // with no receipts
        if (receiptsSize() == 0)
        {
            return receiptsRoot;
        }

        auto receiptsList = bcos::protocol::encodeToCalculateRoot(
            receiptsSize(), [this](uint64_t _index) { return receipt(_index)->hash(); });
        receiptsRoot =
            bcos::protocol::calculateMerkleProofRoot(m_receiptFactory->cryptoSuite(), receiptsList);

        return receiptsRoot;
    }

protected:
    virtual void encodeTransactionsMetaData() const;
    virtual void decodeTransactionsMetaData();

    virtual void clearTransactionMetaDataCache() const
    {
        auto allocatedMetaDataSize = m_pbRawBlock->transactionsmetadata_size();
        for (int i = 0; i < allocatedMetaDataSize; i++)
        {
            m_pbRawBlock->mutable_transactionsmetadata()->UnsafeArenaReleaseLast();
        }
        m_pbRawBlock->clear_transactionsmetadata();
    }

private:
    void decodeTransactions(bool _calculateHash, bool _checkSig);
    void decodeReceipts(bool _calculateHash);

    void decodeNonceList();

    void encodeTransactions() const;
    void encodeReceipts() const;

    void encodeNonceList() const;

    void clearTransactionsCache()
    {
        m_pbRawBlock->clear_transactions();
        // WriteGuard l(x_txsRootCache);
        // m_txsRootCache = bcos::crypto::HashType();
    }
    void clearReceiptsCache()
    {
        m_pbRawBlock->clear_receipts();
        // WriteGuard l(x_receiptRootCache);
        // m_receiptRootCache = bcos::crypto::HashType();
    }

private:
    BlockHeaderFactory::Ptr m_blockHeaderFactory;
    std::shared_ptr<PBRawBlock> m_pbRawBlock;
    BlockHeader::Ptr m_blockHeader;
    TransactionsPtr m_transactions;
    ReceiptsPtr m_receipts;
    TransactionMetaDataListPtr m_transactionMetaDataList;
    NonceListPtr m_nonceList;
};
}  // namespace protocol
}  // namespace bcos