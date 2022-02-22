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
 * @file BlockFactory.h
 * @author: yujiechen
 * @date: 2021-03-23
 */
#pragma once
#include "../../interfaces/crypto/CryptoSuite.h"
#include "Block.h"
#include "BlockHeaderFactory.h"
#include "TransactionFactory.h"
#include "TransactionMetaData.h"
#include "TransactionReceiptFactory.h"
namespace bcos
{
namespace protocol
{
class BlockFactory
{
public:
    using Ptr = std::shared_ptr<BlockFactory>;
    BlockFactory() = default;
    virtual ~BlockFactory() {}
    virtual Block::Ptr createBlock() = 0;
    virtual Block::Ptr createBlock(
        bytes const& _data, bool _calculateHash = true, bool _checkSig = true) = 0;
    virtual Block::Ptr createBlock(
        bytesConstRef _data, bool _calculateHash = true, bool _checkSig = true) = 0;

    virtual TransactionMetaData::Ptr createTransactionMetaData() = 0;
    virtual TransactionMetaData::Ptr createTransactionMetaData(
        bcos::crypto::HashType const _hash, std::string const& _to) = 0;

    virtual bcos::crypto::CryptoSuite::Ptr cryptoSuite() = 0;
    virtual BlockHeaderFactory::Ptr blockHeaderFactory() = 0;
    virtual TransactionFactory::Ptr transactionFactory() = 0;
    virtual TransactionReceiptFactory::Ptr receiptFactory() = 0;
};
}  // namespace protocol
}  // namespace bcos