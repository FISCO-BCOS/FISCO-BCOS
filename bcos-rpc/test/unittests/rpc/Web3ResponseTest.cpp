/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include "../common/RPCFixture.h"
#include <bcos-rpc/web3jsonrpc/model/BlockResponse.h>
#include <bcos-rpc/web3jsonrpc/model/ReceiptResponse.h>
#include <bcos-rpc/web3jsonrpc/model/TransactionResponse.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::rpc;

namespace bcos::test
{
BOOST_FIXTURE_TEST_SUITE(Web3ResponseTest, RPCFixture)

BOOST_AUTO_TEST_CASE(combineBlockResponseGenesisBlock)
{
    auto block = m_blockFactory->createBlock();
    auto header = m_blockFactory->blockHeaderFactory()->createBlockHeader();
    header->setNumber(0);
    header->setGasUsed(u256(0));
    header->setTimestamp(1000);
    header->calculateHash(*hashImpl);
    block->setBlockHeader(header);

    Json::Value result(Json::objectValue);
    combineBlockResponse(result, *block, /*fullTxs=*/false);

    // Genesis block gets the all-zero miner and parentHash special-cases.
    BOOST_CHECK_EQUAL(result["number"].asString(), "0x0");
    BOOST_CHECK_EQUAL(result["miner"].asString(), "0x0000000000000000000000000000000000000000");
    BOOST_CHECK_EQUAL(result["nonce"].asString(), "0x0000000000000000");
    BOOST_CHECK_EQUAL(result["difficulty"].asString(), "0x0");
    BOOST_CHECK_EQUAL(result["totalDifficulty"].asString(), "0x0");
    BOOST_CHECK(result["uncles"].isArray());
    BOOST_CHECK_EQUAL(result["uncles"].size(), 0U);
    // fullTxs=false → transactions is an array of hashes (empty here).
    BOOST_CHECK(result["transactions"].isArray());
    BOOST_CHECK_EQUAL(result["baseFeePerGas"].asString(), "0x0");
    BOOST_CHECK(result.isMember("withdrawalsRoot"));
    BOOST_CHECK(result.isMember("logsBloom"));
}

BOOST_AUTO_TEST_CASE(combineBlockResponseNonGenesisComputesMiner)
{
    auto block = m_blockFactory->createBlock();
    auto header = m_blockFactory->blockHeaderFactory()->createBlockHeader();
    header->setNumber(42);
    header->setGasUsed(u256(21000));
    header->setTimestamp(1700000000000);  // ms
    header->setSealer(0);
    // A sealer public key so the miner-address derivation branch runs.
    bytes pk(64, 0x11);
    std::vector<bytes> sealerList{pk};
    header->setSealerList(std::move(sealerList));
    header->calculateHash(*hashImpl);
    block->setBlockHeader(header);

    Json::Value result(Json::objectValue);
    combineBlockResponse(result, *block, /*fullTxs=*/false);

    BOOST_CHECK_EQUAL(result["number"].asString(), "0x2a");  // 42
    // miner is derived from the sealer pubkey → 20-byte 0x-prefixed address.
    BOOST_REQUIRE(result.isMember("miner"));
    BOOST_CHECK_EQUAL(result["miner"].asString().substr(0, 2), "0x");
    BOOST_CHECK_EQUAL(result["miner"].asString().size(), 42U);  // 0x + 40 hex
    BOOST_CHECK_EQUAL(result["gasUsed"].asString(), "0x5208");  // 21000
    // timestamp is emitted in seconds (ms / 1000): 1700000000 == 0x6553f100.
    BOOST_CHECK_EQUAL(result["timestamp"].asString(), "0x6553f100");
}

BOOST_AUTO_TEST_CASE(combineBlockResponseFullTxsEmptyList)
{
    auto block = m_blockFactory->createBlock();
    auto header = m_blockFactory->blockHeaderFactory()->createBlockHeader();
    header->setNumber(1);
    header->setGasUsed(u256(0));
    header->calculateHash(*hashImpl);
    block->setBlockHeader(header);

    Json::Value result(Json::objectValue);
    combineBlockResponse(result, *block, /*fullTxs=*/true);

    // fullTxs=true with no transactions → still an (empty) array.
    BOOST_REQUIRE(result["transactions"].isArray());
    BOOST_CHECK_EQUAL(result["transactions"].size(), 0U);
}

BOOST_AUTO_TEST_CASE(combineTxResponseShapesTransaction)
{
    auto txFactory = m_blockFactory->transactionFactory();
    auto tx = txFactory->createTransaction(0, "0x1234567890123456789012345678901234567890",
        bcos::bytes{0x01, 0x02}, "0x1", 100, chainId, groupId, 0);
    BOOST_REQUIRE(tx);

    bcos::crypto::HashType blockHash;
    blockHash[0] = 0x77;

    Json::Value result(Json::objectValue);
    combineTxResponse(result, *tx, /*transactionIndex=*/3, /*blockNumber=*/12, blockHash);

    BOOST_CHECK_EQUAL(result["blockHash"].asString(), blockHash.hexPrefixed());
    BOOST_CHECK_EQUAL(result["blockNumber"].asString(), "0xc");  // 12
    BOOST_CHECK_EQUAL(result["transactionIndex"].asString(), "0x3");
    BOOST_CHECK_EQUAL(result["to"].asString().substr(0, 2), "0x");
    BOOST_CHECK(result.isMember("from"));
    BOOST_CHECK(result.isMember("input"));
}

BOOST_AUTO_TEST_CASE(combineReceiptResponseShapesReceipt)
{
    auto txFactory = m_blockFactory->transactionFactory();
    auto tx = txFactory->createTransaction(0, "0x1234567890123456789012345678901234567890",
        bcos::bytes{0x0a}, "0x2", 100, chainId, groupId, 0);
    BOOST_REQUIRE(tx);

    auto receiptFactory = m_blockFactory->receiptFactory();
    std::vector<bcos::protocol::LogEntry> logs;
    auto receipt = receiptFactory->createReceipt(bcos::u256(21000),
        "0x1234567890123456789012345678901234567890", logs, /*status=*/0, bcos::bytesConstRef{},
        /*blockNumber=*/12);
    BOOST_REQUIRE(receipt);
    receipt->setTransactionIndex(0);

    bcos::crypto::HashType blockHash;
    blockHash[0] = 0x88;

    Json::Value result(Json::objectValue);
    combineReceiptResponse(result, *receipt, *tx, blockHash);

    BOOST_CHECK_EQUAL(result["blockHash"].asString(), blockHash.hexPrefixed());
    BOOST_CHECK_EQUAL(result["blockNumber"].asString(), "0xc");  // 12
    BOOST_CHECK(result.isMember("from"));
    BOOST_CHECK(result.isMember("cumulativeGasUsed"));
    BOOST_CHECK(result.isMember("gasUsed"));
    BOOST_CHECK(result.isMember("logs"));
    BOOST_CHECK(result["logs"].isArray());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
