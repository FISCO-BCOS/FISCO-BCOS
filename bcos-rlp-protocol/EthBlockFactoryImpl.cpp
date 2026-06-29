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
 * @file EthBlockFactoryImpl.cpp
 * @brief Factory implementation for EthBlockImpl
 * @date 2026/6/24
 */
#include "EthBlockFactoryImpl.h"
#include "EthBlockImpl.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-tars-protocol/protocol/TransactionMetaDataImpl.h>

namespace bcos::protocol
{

EthBlockFactoryImpl::EthBlockFactoryImpl(bcos::crypto::CryptoSuite::Ptr cryptoSuite,
    BlockHeaderFactory::Ptr blockHeaderFactory,
    TransactionFactory::Ptr transactionFactory,
    TransactionReceiptFactory::Ptr receiptFactory)
  : m_cryptoSuite(std::move(cryptoSuite)),
    m_blockHeaderFactory(std::move(blockHeaderFactory)),
    m_transactionFactory(std::move(transactionFactory)),
    m_receiptFactory(std::move(receiptFactory))
{}

Block::Ptr EthBlockFactoryImpl::createBlock()
{
    return std::make_shared<EthBlockImpl>();
}

Block::Ptr EthBlockFactoryImpl::createBlock(
    bytesConstRef _data, bool _calculateHash, bool)
{
    auto block = std::make_shared<EthBlockImpl>();
    block->decode(_data, _calculateHash, false);

    if (_calculateHash && block->headerData().dataHash.empty())
    {
        EthBlockHeaderImpl tmpHdr(std::shared_ptr<EthBlockHeaderData>(
            block, &block->headerData()));
        tmpHdr.calculateHash(*m_cryptoSuite->hashImpl());
    }

    return block;
}

bcos::crypto::CryptoSuite::Ptr EthBlockFactoryImpl::cryptoSuite() { return m_cryptoSuite; }
BlockHeaderFactory::Ptr EthBlockFactoryImpl::blockHeaderFactory() { return m_blockHeaderFactory; }
TransactionFactory::Ptr EthBlockFactoryImpl::transactionFactory() { return m_transactionFactory; }
TransactionReceiptFactory::Ptr EthBlockFactoryImpl::receiptFactory() { return m_receiptFactory; }

TransactionMetaData::Ptr EthBlockFactoryImpl::createTransactionMetaData()
{
    return std::make_shared<bcostars::protocol::TransactionMetaDataImpl>(
        [inner = bcostars::TransactionMetaData()]() mutable { return &inner; });
}

TransactionMetaData::Ptr EthBlockFactoryImpl::createTransactionMetaData(
    crypto::HashType _hash, std::string _to)
{
    auto txMetaData = std::make_shared<bcostars::protocol::TransactionMetaDataImpl>();
    txMetaData->setHash(std::move(_hash));
    txMetaData->setTo(std::move(_to));
    return txMetaData;
}

}  // namespace bcos::protocol
