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
 * @brief test for PBBlock
 * @file PBBlockTest.cpp
 * @author: yujiechen
 * @date: 2021-03-23
 */
#pragma once
#include "bcos-protocol/protobuf/PBBlock.h"
#include "bcos-protocol/protobuf/PBBlockFactory.h"
#include "bcos-protocol/testutils/protocol/FakeBlockHeader.h"
#include "bcos-protocol/testutils/protocol/FakeTransaction.h"
#include "bcos-protocol/testutils/protocol/FakeTransactionReceipt.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/hash/SM3.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-crypto/signature/sm2/SM2Crypto.h>
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
    auto blockHeaderFactory = std::make_shared<PBBlockHeaderFactory>(_cryptoSuite);
    auto transactionFactory = std::make_shared<PBTransactionFactory>(_cryptoSuite);
    auto receiptFactory = std::make_shared<PBTransactionReceiptFactory>(_cryptoSuite);
    return std::make_shared<PBBlockFactory>(blockHeaderFactory, transactionFactory, receiptFactory);
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
    if (block->blockHeader())
    {
        originHash = block->blockHeader()->hash();
    }
    BOOST_CHECK(block->calculateReceiptRoot() == decodedBlock->calculateReceiptRoot());

    if (block->blockHeader())
    {
        BOOST_CHECK_EQUAL(block->blockHeader()->receiptsRoot(), block->calculateReceiptRoot());
        BOOST_CHECK_EQUAL(
            decodedBlock->blockHeader()->receiptsRoot(), decodedBlock->calculateReceiptRoot());
        BOOST_CHECK_EQUAL(decodedBlock->blockHeader()->hash(), originHash);
        originHash = block->blockHeader()->hash();
    }
    // check transactionsRoot
    BOOST_CHECK(block->calculateTransactionRoot() == decodedBlock->calculateTransactionRoot());
    if (block->blockHeader())
    {
        BOOST_CHECK_EQUAL(block->blockHeader()->txsRoot(), block->calculateTransactionRoot());
        BOOST_CHECK_EQUAL(
            decodedBlock->blockHeader()->txsRoot(), block->calculateTransactionRoot());
        BOOST_CHECK_EQUAL(decodedBlock->blockHeader()->hash(), originHash);
        originHash = decodedBlock->blockHeader()->hash();
    }
    // Check idempotence
    auto txsRoot = block->calculateTransactionRoot();
    auto receiptsRoot = block->calculateReceiptRoot();
    BOOST_CHECK(txsRoot == block->calculateTransactionRoot());
    BOOST_CHECK(receiptsRoot == block->calculateReceiptRoot());
    if (decodedBlock->blockHeader())
    {
        BOOST_CHECK(decodedBlock->blockHeader()->hash() == originHash);
    }
}

inline Block::Ptr fakeAndCheckBlock(CryptoSuite::Ptr _cryptoSuite, BlockFactory::Ptr _blockFactory,
    bool _withHeader, size_t _txsNum, size_t _txsHashNum)
{
    auto block = _blockFactory->createBlock();
    if (_withHeader)
    {
        auto blockHeader = testPBBlockHeader(_cryptoSuite);
        block->setBlockHeader(blockHeader);
    }

    block->setBlockType(CompleteBlock);
    // fake transactions
    for (size_t i = 0; i < _txsNum; i++)
    {
        auto tx = fakeTransaction(_cryptoSuite, utcTime() + i);
        block->appendTransaction(tx);

        auto metaData = std::make_shared<PBTransactionMetaData>();
        metaData->setHash(tx->hash());
        metaData->setTo(std::string(tx->to()));

        block->appendTransactionMetaData(metaData);
    }

    _txsHashNum = _txsNum;
    // fake receipts
    for (size_t i = 0; i < _txsNum; i++)
    {
        auto receipt = testPBTransactionReceipt(_cryptoSuite);
        block->setReceipt(i, receipt);
    }
    // fake txsHash
    // for (size_t i = 0; i < _txsHashNum; i++)
    // {
    //     auto content = "transaction: " + std::to_string(i);
    //     auto hash = _cryptoSuite->hashImpl()->hash(content);
    //     boost::ignore_unused(hash);

    //     auto metaData = std::make_shared<PBTransactionMetaData>();
    //     metaData->setHash(hash);
    //     metaData->setTo(*toHexString(hash));
    //     // auto txMetaData = _blockFactory->createTransactionMetaData(hash, *toHexString(hash));
    //     block->appendTransactionMetaData(metaData);
    // }

    if (block->blockHeaderConst())
    {
        block->blockHeader()->setReceiptsRoot(block->calculateReceiptRoot());
        block->blockHeader()->setTxsRoot(block->calculateTransactionRoot());
    }

    // encode block
    auto encodedData = std::make_shared<bytes>();
    block->encode(*encodedData);
    // decode block
    auto decodedBlock = _blockFactory->createBlock(*encodedData, true, true);
    checkBlock(_cryptoSuite, block, decodedBlock);
    // check txsHash
    BOOST_CHECK(decodedBlock->transactionsHashSize() == _txsHashNum);
    BOOST_CHECK(decodedBlock->transactionsMetaDataSize() == _txsHashNum);
    for (size_t i = 0; i < _txsHashNum; i++)
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


}  // namespace test
}  // namespace bcos
