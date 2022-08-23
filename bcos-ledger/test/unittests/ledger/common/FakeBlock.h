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
 * @file FakeBlock.h
 * @author: kyonRay
 * @date 2021-04-14
 */

#pragma once
#include "FakeBlockHeader.h"
#include "FakeReceipt.h"
#include "FakeTransaction.h"
#include "bcos-framework/protocol/TransactionMetaData.h"
#include "bcos-protocol/protobuf/PBBlock.h"
#include "bcos-protocol/protobuf/PBBlockFactory.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/hash/SM3.h>
#include <boost/test/unit_test.hpp>
using namespace bcos;
using namespace bcos::protocol;
using namespace bcos::crypto;

namespace bcos
{
namespace test
{
inline CryptoSuite::Ptr createCryptoSuite()
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signImpl = std::make_shared<Secp256k1Crypto>();
    return std::make_shared<CryptoSuite>(hashImpl, signImpl, nullptr);
}

inline BlockFactory::Ptr createBlockFactory(CryptoSuite::Ptr _cryptoSuite)
{
    auto blockHeaderFactory = std::make_shared<PBBlockHeaderFactory>(_cryptoSuite);
    auto transactionFactory = std::make_shared<PBTransactionFactory>(_cryptoSuite);
    auto receiptFactory = std::make_shared<PBTransactionReceiptFactory>(_cryptoSuite);
    return std::make_shared<PBBlockFactory>(blockHeaderFactory, transactionFactory, receiptFactory);
}

inline Block::Ptr fakeBlock(CryptoSuite::Ptr _cryptoSuite, BlockFactory::Ptr _blockFactory,
    size_t _txsNum, size_t _receiptsNum, BlockNumber _blockNumber)
{
    auto block = _blockFactory->createBlock();

    auto blockHeader = testPBBlockHeader(_cryptoSuite, _blockNumber);
    block->setBlockHeader(blockHeader);
    block->setBlockType(CompleteBlock);
    // fake transactions
    for (size_t i = 0; i < _txsNum; i++)
    {
        auto tx = fakeTransaction(_cryptoSuite);
        block->appendTransaction(tx);
    }
    // fake receipts
    for (size_t i = 0; i < _receiptsNum; i++)
    {
        auto receipt = testPBTransactionReceipt(_cryptoSuite, _blockNumber);
        block->appendReceipt(receipt);
    }
    // fake txsHash
    for (size_t i = 0; i < _txsNum; i++)
    {
        auto transactionMetaData =
            _blockFactory->createTransactionMetaData(block->transaction(i)->hash(), "/abc");
        block->appendTransactionMetaData(std::move(transactionMetaData));
    }
    NonceList nonceList;
    for (size_t i = 0; i < _txsNum; i++)
    {
        nonceList.emplace_back(u256(123));
    }
    block->setNonceList(nonceList);
    return block;
}

inline Block::Ptr fakeEmptyBlock(
    CryptoSuite::Ptr _cryptoSuite, BlockFactory::Ptr _blockFactory, BlockNumber _blockNumber)
{
    auto block = _blockFactory->createBlock();

    auto blockHeader = testPBBlockHeader(_cryptoSuite, _blockNumber);
    block->setBlockHeader(blockHeader);
    return block;
}

inline BlocksPtr fakeBlocks(CryptoSuite::Ptr _cryptoSuite, BlockFactory::Ptr _blockFactory,
    size_t _txsNumBegin, size_t _receiptsNumBegin, size_t _blockNumber, std::string hash = "")
{
    BlocksPtr blocks = std::make_shared<Blocks>();
    ParentInfo parentInfo;
    parentInfo.blockNumber = 0;
    parentInfo.blockHash = HashType(hash);
    for (size_t i = 0; i < _blockNumber; ++i)
    {
        ParentInfoList parentInfos;
        auto block =
            fakeBlock(_cryptoSuite, _blockFactory, _txsNumBegin + i, _receiptsNumBegin + i, i + 1);
        parentInfos.push_back(parentInfo);
        block->blockHeader()->setNumber(1 + i);
        block->blockHeader()->setParentInfo(parentInfos);
        parentInfo.blockNumber = block->blockHeader()->number();
        parentInfo.blockHash = block->blockHeader()->hash();
        blocks->emplace_back(block);
    }
    return blocks;
}

inline BlocksPtr fakeEmptyBlocks(CryptoSuite::Ptr _cryptoSuite, BlockFactory::Ptr _blockFactory,
    size_t _blockNumber, std::string hash = "")
{
    BlocksPtr blocks = std::make_shared<Blocks>();
    ParentInfo parentInfo;
    parentInfo.blockNumber = 0;
    parentInfo.blockHash = HashType(hash);
    for (size_t i = 0; i < _blockNumber; ++i)
    {
        ParentInfoList parentInfos;
        auto block = fakeEmptyBlock(_cryptoSuite, _blockFactory, i + 1);
        parentInfos.push_back(parentInfo);
        block->blockHeader()->setNumber(1 + i);
        block->blockHeader()->setParentInfo(parentInfos);
        parentInfo.blockNumber = block->blockHeader()->number();
        parentInfo.blockHash = block->blockHeader()->hash();
        blocks->emplace_back(block);
    }
    return blocks;
}


}  // namespace test
}  // namespace bcos
