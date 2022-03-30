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
 * @brief factory of PBBlock
 * @file PBBlockFactory.h
 * @author: yujiechen
 * @date: 2021-03-23
 */
#pragma once
#include "./PBTransactionMetaData.h"
#include "PBBlock.h"
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-framework/interfaces/protocol/BlockFactory.h>
#include <bcos-framework/interfaces/protocol/BlockHeaderFactory.h>
#include <bcos-framework/interfaces/protocol/TransactionFactory.h>
#include <bcos-framework/interfaces/protocol/TransactionReceiptFactory.h>
namespace bcos
{
namespace protocol
{
class PBBlockFactory : public BlockFactory
{
public:
    using Ptr = std::shared_ptr<PBBlockFactory>;
    PBBlockFactory(BlockHeaderFactory::Ptr _blockHeaderFactory,
        TransactionFactory::Ptr _transactionFactory, TransactionReceiptFactory::Ptr _receiptFactory)
      : m_blockHeaderFactory(_blockHeaderFactory),
        m_transactionFactory(_transactionFactory),
        m_receiptFactory(_receiptFactory)
    {}
    ~PBBlockFactory() override {}

    Block::Ptr createBlock() override
    {
        return std::make_shared<PBBlock>(
            m_blockHeaderFactory, m_transactionFactory, m_receiptFactory);
    }

    Block::Ptr createBlock(
        bytes const& _data, bool _calculateHash = true, bool _checkSig = true) override
    {
        return std::make_shared<PBBlock>(m_blockHeaderFactory, m_transactionFactory,
            m_receiptFactory, _data, _calculateHash, _checkSig);
    }

    Block::Ptr createBlock(
        bytesConstRef _data, bool _calculateHash = true, bool _checkSig = true) override
    {
        return std::make_shared<PBBlock>(m_blockHeaderFactory, m_transactionFactory,
            m_receiptFactory, _data, _calculateHash, _checkSig);
    }

    TransactionMetaData::Ptr createTransactionMetaData() override
    {
        return std::make_shared<PBTransactionMetaData>();
    }
    TransactionMetaData::Ptr createTransactionMetaData(
        bcos::crypto::HashType const _hash, std::string const& _to) override
    {
        return std::make_shared<PBTransactionMetaData>(_hash, _to);
    }

    bcos::crypto::CryptoSuite::Ptr cryptoSuite() override
    {
        return m_transactionFactory->cryptoSuite();
    }

    BlockHeaderFactory::Ptr blockHeaderFactory() override { return m_blockHeaderFactory; }
    TransactionFactory::Ptr transactionFactory() override { return m_transactionFactory; }
    TransactionReceiptFactory::Ptr receiptFactory() override { return m_receiptFactory; }

private:
    BlockHeaderFactory::Ptr m_blockHeaderFactory;
    TransactionFactory::Ptr m_transactionFactory;
    TransactionReceiptFactory::Ptr m_receiptFactory;
};
}  // namespace protocol
}  // namespace bcos