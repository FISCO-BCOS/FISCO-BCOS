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
 * @brief tars implementation for BlockFactory
 * @file BlockFactoryImpl.h
 * @author: ancelmo
 * @date 2021-04-20
 */
#pragma once

#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"

#include "BlockImpl.h"
#include "bcos-tars-protocol/tars/Block.h"
#include <bcos-framework//protocol/BlockFactory.h>
#include <bcos-framework//protocol/BlockHeaderFactory.h>
#include <bcos-framework//protocol/TransactionFactory.h>
#include <bcos-framework//protocol/TransactionReceiptFactory.h>

namespace bcostars
{
namespace protocol
{
class BlockFactoryImpl : public bcos::protocol::BlockFactory
{
public:
    BlockFactoryImpl(bcos::crypto::CryptoSuite::Ptr cryptoSuite,
        bcos::protocol::BlockHeaderFactory::Ptr blockHeaderFactory,
        bcos::protocol::TransactionFactory::Ptr transactionFactory,
        bcos::protocol::TransactionReceiptFactory::Ptr receiptFactory)
      : m_cryptoSuite(cryptoSuite),
        m_blockHeaderFactory(blockHeaderFactory),
        m_transactionFactory(transactionFactory),
        m_receiptFactory(receiptFactory){};

    ~BlockFactoryImpl() override {}
    bcos::protocol::Block::Ptr createBlock() override
    {
        return std::make_shared<BlockImpl>(m_transactionFactory, m_receiptFactory);
    }

    bcos::protocol::Block::Ptr createBlock(
        bcos::bytes const& _data, bool _calculateHash = true, bool _checkSig = true) override
    {
        return createBlock(bcos::ref(_data), _calculateHash, _checkSig);
    }

    bcos::protocol::Block::Ptr createBlock(
        bcos::bytesConstRef _data, bool _calculateHash = true, bool _checkSig = true) override
    {
        auto block = std::make_shared<BlockImpl>(m_transactionFactory, m_receiptFactory);
        block->decode(_data, _calculateHash, _checkSig);

        return block;
    }

    bcos::crypto::CryptoSuite::Ptr cryptoSuite() override { return m_cryptoSuite; }
    bcos::protocol::BlockHeaderFactory::Ptr blockHeaderFactory() override
    {
        return m_blockHeaderFactory;
    }
    bcos::protocol::TransactionFactory::Ptr transactionFactory() override
    {
        return m_transactionFactory;
    }
    bcos::protocol::TransactionReceiptFactory::Ptr receiptFactory() override
    {
        return m_receiptFactory;
    }

    bcos::protocol::TransactionMetaData::Ptr createTransactionMetaData() override
    {
        return std::make_shared<bcostars::protocol::TransactionMetaDataImpl>(
            [inner = bcostars::TransactionMetaData()]() mutable { return &inner; });
    }

    bcos::protocol::TransactionMetaData::Ptr createTransactionMetaData(
        bcos::crypto::HashType const _hash, std::string const& _to) override
    {
        auto txMetaData = std::make_shared<bcostars::protocol::TransactionMetaDataImpl>();
        txMetaData->setHash(_hash);
        txMetaData->setTo(_to);

        return txMetaData;
    }

private:
    bcos::crypto::CryptoSuite::Ptr m_cryptoSuite;
    bcos::protocol::BlockHeaderFactory::Ptr m_blockHeaderFactory;
    bcos::protocol::TransactionFactory::Ptr m_transactionFactory;
    bcos::protocol::TransactionReceiptFactory::Ptr m_receiptFactory;
};
}  // namespace protocol
}  // namespace bcostars