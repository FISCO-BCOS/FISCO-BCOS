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
 * @brief test for PBBlock
 * @file PBBlockTest.cpp
 * @author: yujiechen
 * @date: 2021-03-23
 */
#pragma once
#include "FakeBlockHeader.h"
#include "FakeTransaction.h"
#include "FakeTransactionReceipt.h"
#include "bcos-tars-protocol/protocol/BlockFactoryImpl.h"
#include "bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h"
#include "bcos-tars-protocol/protocol/BlockHeaderImpl.h"
#include "bcos-tars-protocol/protocol/TransactionFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionMetaDataImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptImpl.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/hash/SM3.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-crypto/signature/sm2/SM2Crypto.h>
#include <bcos-tars-protocol/protocol/BlockImpl.h>
#include <boost/core/ignore_unused.hpp>
#include <boost/test/unit_test.hpp>
using namespace bcos;
using namespace bcos::protocol;
using namespace bcos::crypto;

namespace bcos
{
namespace test
{
inline CryptoSuite::Ptr createNormalCryptoSuite()
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signImpl = std::make_shared<Secp256k1Crypto>();
    return std::make_shared<CryptoSuite>(hashImpl, signImpl, nullptr);
}

inline CryptoSuite::Ptr createSMCryptoSuite()
{
    auto hashImpl = std::make_shared<SM3>();
    auto signImpl = std::make_shared<SM2Crypto>();
    return std::make_shared<CryptoSuite>(hashImpl, signImpl, nullptr);
}

inline BlockFactory::Ptr createBlockFactory(CryptoSuite::Ptr _cryptoSuite)
{
    auto blockHeaderFactory =
        std::make_shared<bcostars::protocol::BlockHeaderFactoryImpl>(_cryptoSuite);
    auto transactionFactory =
        std::make_shared<bcostars::protocol::TransactionFactoryImpl>(_cryptoSuite);
    auto receiptFactory =
        std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(_cryptoSuite);
    return std::make_shared<bcostars::protocol::BlockFactoryImpl>(
        _cryptoSuite, blockHeaderFactory, transactionFactory, receiptFactory);
}

inline void checkBlock(CryptoSuite::Ptr _cryptoSuite, Block::Ptr block, Block::Ptr decodedBlock)
{
    BOOST_CHECK(block->blockType() == decodedBlock->blockType());
    // check BlockHeader
    if (block->blockHeader())
    {
        checkBlockHeader(block->blockHeader(), decodedBlock->blockHeader());
    }
    // check transactions
    BOOST_CHECK_EQUAL(decodedBlock->transactionsSize(), block->transactionsSize());
    for (size_t i = 0; i < block->transactionsSize(); ++i)
    {
        checkTransaction(block->transaction(i), decodedBlock->transaction(i));
    }
    /*
    for (auto transaction : *(block->transactions()))
    {
        checkTransaction(transaction, (*(decodedBlock->transactions()))[index++]);
    }
    */
    // check receipts
    BOOST_CHECK_EQUAL(decodedBlock->receiptsSize(), block->receiptsSize());
    for (size_t i = 0; i < block->receiptsSize(); ++i)
    {
        checkReceipts(_cryptoSuite->hashImpl(), block->receipt(i), decodedBlock->receipt(i));
    }
    /*
    for (auto receipt : *(block->receipts()))
    {
        checkReceipts(_cryptoSuite->hashImpl(), receipt, (*(decodedBlock->receipts()))[index++]);
    }
    */
    for (size_t i = 0; i < decodedBlock->transactionsHashSize(); i++)
    {
        BOOST_CHECK(decodedBlock->transactionHash(i) == block->transactionHash(i));
        BOOST_CHECK(
            decodedBlock->transactionMetaData(i)->hash() == block->transactionMetaData(i)->hash());
        BOOST_CHECK(
            decodedBlock->transactionMetaData(i)->to() == block->transactionMetaData(i)->to());
    }
    // check receiptsRoot
    h256 originHash = h256();
    auto hashImpl = _cryptoSuite->hashImpl();
    if (block->blockHeader())
    {
        block->blockHeader()->calculateHash(*hashImpl);
        originHash = block->blockHeader()->hash();
    }
    BOOST_CHECK(
        block->calculateReceiptRoot(*hashImpl) == decodedBlock->calculateReceiptRoot(*hashImpl));

    if (block->blockHeader())
    {
        BOOST_CHECK_EQUAL(
            block->blockHeader()->receiptsRoot(), block->calculateReceiptRoot(*hashImpl));
        BOOST_CHECK_EQUAL(decodedBlock->blockHeader()->receiptsRoot(),
            decodedBlock->calculateReceiptRoot(*hashImpl));
        decodedBlock->blockHeader()->calculateHash(*hashImpl);

        BOOST_CHECK_EQUAL(decodedBlock->blockHeader()->hash(), originHash);
        originHash = block->blockHeader()->hash();
    }
    // check transactionsRoot
    BOOST_CHECK(block->calculateTransactionRoot(*hashImpl) ==
                decodedBlock->calculateTransactionRoot(*hashImpl));
    if (block->blockHeader())
    {
        BOOST_CHECK_EQUAL(
            block->blockHeader()->txsRoot(), block->calculateTransactionRoot(*hashImpl));
        BOOST_CHECK_EQUAL(
            decodedBlock->blockHeader()->txsRoot(), block->calculateTransactionRoot(*hashImpl));
        BOOST_CHECK_EQUAL(decodedBlock->blockHeader()->hash(), originHash);
        originHash = decodedBlock->blockHeader()->hash();
    }
    // Check idempotence
    auto txsRoot = block->calculateTransactionRoot(*hashImpl);
    auto receiptsRoot = block->calculateReceiptRoot(*hashImpl);
    BOOST_CHECK(txsRoot == block->calculateTransactionRoot(*hashImpl));
    BOOST_CHECK(receiptsRoot == block->calculateReceiptRoot(*hashImpl));
    if (decodedBlock->blockHeader())
    {
        BOOST_CHECK(decodedBlock->blockHeader()->hash() == originHash);
    }
}

inline Block::Ptr fakeAndCheckBlock(CryptoSuite::Ptr _cryptoSuite, BlockFactory::Ptr _blockFactory,
    size_t _txsNum, size_t _receiptsNum, BlockNumber _blockNumber, bool _withHeader = true,
    bool _check = true)
{
    auto block = _blockFactory->createBlock();
    auto blockHeader = testPBBlockHeader(_cryptoSuite, _blockNumber, _check);
    if (_withHeader)
    {
        block->setBlockHeader(blockHeader);
        block->blockHeader()->calculateHash(*_blockFactory->cryptoSuite()->hashImpl());
        block->setBlockType(CompleteBlock);
    }

    // fake transactions
    for (size_t i = 0; i < _txsNum; i++)
    {
        auto tx = fakeTransaction(_cryptoSuite, _check);
        block->appendTransaction(tx);
    }
    auto txRoot = block->calculateTransactionRoot(*_cryptoSuite->hashImpl());
    blockHeader->setTxsRoot(std::move(txRoot));
    // fake receipts
    for (size_t i = 0; i < _receiptsNum; i++)
    {
        auto receipt = testPBTransactionReceipt(_cryptoSuite, _blockNumber, false);
        block->appendReceipt(receipt);
    }
    auto receiptRoot = block->calculateReceiptRoot(*_cryptoSuite->hashImpl());
    blockHeader->setReceiptsRoot(std::move(receiptRoot));
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
        nonceList.emplace_back(std::string("123"));
    }
    block->setNonceList(nonceList);
    block->setBlockHeader(blockHeader);
    if (!_check)
    {
        return block;
    }

    // encode block
    auto encodedData = std::make_shared<bytes>();
    block->encode(*encodedData);
    // decode block
    auto decodedBlock = _blockFactory->createBlock(*encodedData, true, true);
    checkBlock(_cryptoSuite, block, decodedBlock);
    // check txsHash
    BOOST_CHECK(decodedBlock->transactionsHashSize() == _txsNum);
    BOOST_CHECK(decodedBlock->transactionsMetaDataSize() == _txsNum);
    for (size_t i = 0; i < _txsNum; i++)
    {
        auto hash = block->transaction(i)->hash();
        BOOST_CHECK(decodedBlock->transactionHash(i) == hash);
        BOOST_CHECK(decodedBlock->transactionMetaData(i)->hash() == hash);
    }
    // exception test
    /*(*encodedData)[0] += 1;
    BOOST_CHECK_THROW(
        _blockFactory->createBlock(*encodedData, true, true), PBObjectDecodeException);*/
    return block;
}

inline Block::Ptr fakeEmptyBlock(
    CryptoSuite::Ptr _cryptoSuite, BlockFactory::Ptr _blockFactory, BlockNumber _blockNumber)
{
    auto block = _blockFactory->createBlock();

    auto blockHeader = testPBBlockHeader(_cryptoSuite, _blockNumber);
    blockHeader->calculateHash(*_blockFactory->cryptoSuite()->hashImpl());
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
        auto block = fakeAndCheckBlock(_cryptoSuite, _blockFactory, _txsNumBegin + i,
            _receiptsNumBegin + i, i + 1, true, false);
        parentInfos.push_back(parentInfo);
        block->blockHeader()->setNumber(1 + i);
        block->blockHeader()->setParentInfo(parentInfos);
        block->blockHeader()->calculateHash(*_cryptoSuite->hashImpl());
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
        block->blockHeader()->calculateHash(*_cryptoSuite->hashImpl());
        parentInfo.blockNumber = block->blockHeader()->number();
        parentInfo.blockHash = block->blockHeader()->hash();
        blocks->emplace_back(block);
    }
    return blocks;
}


}  // namespace test
}  // namespace bcos
