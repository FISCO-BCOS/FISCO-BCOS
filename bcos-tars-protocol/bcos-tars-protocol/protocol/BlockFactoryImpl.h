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

// if windows, manual include tup/Tars.h first
#ifdef _WIN32
#include <tup/Tars.h>
#endif
#include "BlockImpl.h"
#include "bcos-tars-protocol/tars/Block.h"
#include <bcos-framework/protocol/BlockFactory.h>
#include <bcos-framework/protocol/BlockHeaderFactory.h>
#include <bcos-framework/protocol/TransactionFactory.h>
#include <bcos-framework/protocol/TransactionReceiptFactory.h>

namespace bcostars::protocol
{
class BlockFactoryImpl : public bcos::protocol::BlockFactory
{
public:
    BlockFactoryImpl(bcos::crypto::CryptoSuite::Ptr cryptoSuite,
        bcos::protocol::BlockHeaderFactory::Ptr blockHeaderFactory,
        bcos::protocol::TransactionFactory::Ptr transactionFactory,
        bcos::protocol::TransactionReceiptFactory::Ptr receiptFactory);
    BlockFactoryImpl(BlockFactoryImpl const&) = default;
    BlockFactoryImpl(BlockFactoryImpl&&) = default;
    BlockFactoryImpl& operator=(BlockFactoryImpl const&) = default;
    BlockFactoryImpl& operator=(BlockFactoryImpl&&) = default;
    ~BlockFactoryImpl() override = default;

    bcos::protocol::Block::Ptr createBlock() override;

    bcos::protocol::Block::Ptr createBlock(
        bcos::bytesConstRef _data, bool _calculateHash = true, bool _checkSig = true) override;

    bcos::crypto::CryptoSuite::Ptr cryptoSuite() override;
    bcos::protocol::BlockHeaderFactory::Ptr blockHeaderFactory() override;
    bcos::protocol::TransactionFactory::Ptr transactionFactory() override;
    bcos::protocol::TransactionReceiptFactory::Ptr receiptFactory() override;

    bcos::protocol::TransactionMetaData::Ptr createTransactionMetaData() override;

    bcos::protocol::TransactionMetaData::Ptr createTransactionMetaData(
        bcos::crypto::HashType const _hash, std::string const& _to) override;

private:
    bcos::crypto::CryptoSuite::Ptr m_cryptoSuite;
    bcos::protocol::BlockHeaderFactory::Ptr m_blockHeaderFactory;
    bcos::protocol::TransactionFactory::Ptr m_transactionFactory;
    bcos::protocol::TransactionReceiptFactory::Ptr m_receiptFactory;
};
}  // namespace bcostars::protocol