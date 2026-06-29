/**
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @file EthBlockFactoryImpl.h
 * @brief Factory for EthBlockImpl — inherits BlockFactory
 * @date 2026/6/24
 */
#pragma once

#include <bcos-framework/protocol/BlockFactory.h>
#include <bcos-framework/protocol/BlockHeaderFactory.h>
#include <bcos-framework/protocol/TransactionFactory.h>
#include <bcos-framework/protocol/TransactionReceiptFactory.h>

namespace bcos::protocol
{

class EthBlockFactoryImpl : public bcos::protocol::BlockFactory
{
public:
    EthBlockFactoryImpl(bcos::crypto::CryptoSuite::Ptr cryptoSuite,
        bcos::protocol::BlockHeaderFactory::Ptr blockHeaderFactory,
        bcos::protocol::TransactionFactory::Ptr transactionFactory,
        bcos::protocol::TransactionReceiptFactory::Ptr receiptFactory);
    EthBlockFactoryImpl(EthBlockFactoryImpl const&) = default;
    EthBlockFactoryImpl(EthBlockFactoryImpl&&) = default;
    EthBlockFactoryImpl& operator=(EthBlockFactoryImpl const&) = default;
    EthBlockFactoryImpl& operator=(EthBlockFactoryImpl&&) = default;
    ~EthBlockFactoryImpl() override = default;

    bcos::protocol::Block::Ptr createBlock() override;
    bcos::protocol::Block::Ptr createBlock(
        bcos::bytesConstRef _data, bool _calculateHash, bool _checkSig) override;

    bcos::crypto::CryptoSuite::Ptr cryptoSuite() override;
    bcos::protocol::BlockHeaderFactory::Ptr blockHeaderFactory() override;
    bcos::protocol::TransactionFactory::Ptr transactionFactory() override;
    bcos::protocol::TransactionReceiptFactory::Ptr receiptFactory() override;

    bcos::protocol::TransactionMetaData::Ptr createTransactionMetaData() override;
    bcos::protocol::TransactionMetaData::Ptr createTransactionMetaData(
        bcos::crypto::HashType _hash, std::string _to) override;

private:
    bcos::crypto::CryptoSuite::Ptr m_cryptoSuite;
    bcos::protocol::BlockHeaderFactory::Ptr m_blockHeaderFactory;
    bcos::protocol::TransactionFactory::Ptr m_transactionFactory;
    bcos::protocol::TransactionReceiptFactory::Ptr m_receiptFactory;
};

}  // namespace bcos::protocol
