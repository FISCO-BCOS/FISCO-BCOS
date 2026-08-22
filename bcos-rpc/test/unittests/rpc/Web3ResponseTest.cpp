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
#include <bcos-crypto/ChecksumAddress.h>
#include <bcos-rpc/web3jsonrpc/model/BlockResponse.h>
#include <bcos-rpc/web3jsonrpc/model/ReceiptResponse.h>
#include <bcos-rpc/web3jsonrpc/model/TransactionResponse.h>
#include <bcos-rpc/web3jsonrpc/model/Web3Transaction.h>
#include <bcos-utilities/DataConvertUtility.h>

#include <boost/test/unit_test.hpp>
using namespace bcos;
using namespace bcos::rpc;

namespace bcos::test
{
namespace
{
// Local factories — Web3ResponseTest.cpp has no makeReceipt/makeWeb3Tx helpers; these mirror the
// construction in combineReceiptResponseShapesReceipt so the OP-field / deposit / 4844 tests below
// can stay compact.
bcos::protocol::TransactionReceipt::Ptr makeReceipt(bcos::protocol::BlockFactory::Ptr blockFactory)
{
    auto receiptFactory = blockFactory->receiptFactory();
    std::vector<bcos::protocol::LogEntry> logs;
    auto receipt = receiptFactory->createReceipt(bcos::u256(21000),
        "0x1234567890123456789012345678901234567890", logs, /*status=*/0, bcos::bytesConstRef{},
        /*blockNumber=*/12);
    receipt->setTransactionIndex(0);
    return receipt;
}

bcos::protocol::Transaction::Ptr makeWeb3Tx(bcos::protocol::BlockFactory::Ptr blockFactory,
    std::string const& chainId, std::string const& groupId)
{
    auto txFactory = blockFactory->transactionFactory();
    return txFactory->createTransaction(0, "0x1234567890123456789012345678901234567890",
        bcos::bytes{0x0a}, "0x2", 100, chainId, groupId, 0);
}

// calculateHash routes headers with ethBlockVersion != NON_ETH through
// EthBlockHeader::validate, which demands every mandatory and fork-gated PRAGUE field —
// fill them all so the header hashes. Distinctively named for unity-build safety.
void fillWeb3ResponseTestPragueFields(bcos::protocol::BlockHeader& header)
{
    bcos::crypto::HashType nonZero;
    nonZero[0] = 0x42;
    header.setUncleHash(nonZero);
    header.setStateRoot(nonZero);
    header.setTxsRoot(nonZero);
    header.setReceiptsRoot(nonZero);
    bcos::bytes bloom(256, 0x00);
    header.setLogsBloom(bcos::ref(bloom));
    header.setBaseFee(u256(1000000000));
    header.setWithdrawalsRoot(nonZero);
    header.setBlobGasUsed(u256(0));
    header.setExcessBlobGas(u256(0));
    header.setParentBeaconBlockRoot(nonZero);
    header.setRequestsHash(nonZero);
    header.setEthBlockVersion(bcos::protocol::EthBlockVersion::PRAGUE);
}
}  // namespace

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

// D2 U1/U2: gasLimit / parentBeaconBlockRoot read from the header with PBFT-compat fallbacks
BOOST_AUTO_TEST_CASE(combineBlockResponseGasLimitAndParentBeaconBlockRoot)
{
    // Unset header fields (PBFT-style: no production path writes them) -> legacy fallbacks
    {
        auto block = m_blockFactory->createBlock();
        auto header = m_blockFactory->blockHeaderFactory()->createBlockHeader();
        header->setNumber(1);
        header->setGasUsed(u256(0));
        header->setTimestamp(1000);
        header->calculateHash(*hashImpl);
        block->setBlockHeader(header);

        Json::Value result(Json::objectValue);
        combineBlockResponse(result, *block, /*fullTxs=*/false);
        BOOST_CHECK_EQUAL(result["gasLimit"].asString(), "0x1c9c380");  // 30M fallback
        BOOST_CHECK_EQUAL(result["parentBeaconBlockRoot"].asString(),
            "0x0000000000000000000000000000000000000000000000000000000000000000");
        BOOST_CHECK(!result.isMember("requestsHash"));  // PBFT/unset -> absent (op-geth omitempty)
    }
    // Set header fields (engine-built block: rebuildOpEthHeader / applyEthGenesisHeader)
    // -> exact round-trip
    {
        auto block = m_blockFactory->createBlock();
        auto header = m_blockFactory->blockHeaderFactory()->createBlockHeader();
        header->setNumber(2);
        header->setGasUsed(u256(21000));
        header->setTimestamp(2000);
        header->setGasLimit(u256(3000000000));
        header->setParentBeaconBlockRoot(
            crypto::HashType("0x1111111111111111111111111111111111111111111111111111111111111111"));
        header->setRequestsHash(
            crypto::HashType("0x2222222222222222222222222222222222222222222222222222222222222222"));
        header->calculateHash(*hashImpl);
        block->setBlockHeader(header);

        Json::Value result(Json::objectValue);
        combineBlockResponse(result, *block, /*fullTxs=*/false);
        BOOST_CHECK_EQUAL(result["gasLimit"].asString(), "0xb2d05e00");  // 3e9 round-trip
        BOOST_CHECK_EQUAL(result["parentBeaconBlockRoot"].asString(),
            "0x1111111111111111111111111111111111111111111111111111111111111111");
        BOOST_CHECK_EQUAL(result["requestsHash"].asString(),
            "0x2222222222222222222222222222222222222222222222222222222222222222");
    }
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

BOOST_AUTO_TEST_CASE(combineBlockResponseEthGenesisTimestampSeconds)
{
    // The eth-genesis B0 stores its timestamp in the internal MILLISECOND domain
    // (Ledger::applyEthGenesisHeader multiplies the artifact's seconds by 1000),
    // exactly like every other block, so the web3 view's uniform ms->s division
    // reports the artifact value — no B0 special case.
    auto block = m_blockFactory->createBlock();
    auto header = m_blockFactory->blockHeaderFactory()->createBlockHeader();
    header->setNumber(0);
    header->setGasUsed(u256(0));
    header->setTimestamp(1755143168000);  // ms: artifact seconds x 1000
    fillWeb3ResponseTestPragueFields(*header);
    header->calculateHash(*hashImpl);
    block->setBlockHeader(header);

    Json::Value result(Json::objectValue);
    combineBlockResponse(result, *block, /*fullTxs=*/false);

    // 1755143168 == 0x689d5c00: the artifact's seconds value.
    BOOST_CHECK_EQUAL(result["timestamp"].asString(), "0x689d5c00");
}

BOOST_AUTO_TEST_CASE(combineBlockResponseEthNonGenesisTimestampStillDivided)
{
    // Every block >= 1 keeps the internal milliseconds domain and the seconds
    // view keeps dividing.
    auto block = m_blockFactory->createBlock();
    auto header = m_blockFactory->blockHeaderFactory()->createBlockHeader();
    header->setNumber(1);
    header->setGasUsed(u256(0));
    header->setTimestamp(1700000000000);  // ms
    fillWeb3ResponseTestPragueFields(*header);
    bcos::crypto::HashType parentHash;
    parentHash[0] = 0x24;
    header->setParentInfo(bcos::protocol::ParentInfo{.blockNumber = 0, .blockHash = parentHash});
    header->calculateHash(*hashImpl);
    block->setBlockHeader(header);

    Json::Value result(Json::objectValue);
    combineBlockResponse(result, *block, /*fullTxs=*/false);

    BOOST_CHECK_EQUAL(result["timestamp"].asString(), "0x6553f100");  // 1700000000
}

BOOST_AUTO_TEST_CASE(combineBlockResponseLegacyGenesisTimestampStillDivided)
{
    // A legacy FISCO genesis header (NON_ETH) stores milliseconds like every
    // other header; its B0 gets the same milliseconds-to-seconds division.
    auto block = m_blockFactory->createBlock();
    auto header = m_blockFactory->blockHeaderFactory()->createBlockHeader();
    header->setNumber(0);
    header->setGasUsed(u256(0));
    header->setTimestamp(2000);  // ms
    header->calculateHash(*hashImpl);
    block->setBlockHeader(header);

    Json::Value result(Json::objectValue);
    combineBlockResponse(result, *block, /*fullTxs=*/false);

    BOOST_CHECK_EQUAL(result["timestamp"].asString(), "0x2");  // 2000 ms -> 2 s
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

BOOST_AUTO_TEST_CASE(combineTxResponseDepositNonceFromReceiptMeta)
{
    // MJ-5: a deposit tx's JSON nonce must reflect the receipt's depositNonce
    // (Regolith+ semantics — deposits carry no nonce field of their own; the value
    // lives in the receipt and drives contract-address derivation). Without a meta
    // the 0x0 default from combineDepositTxResponse stands.
    auto txFactory = m_blockFactory->transactionFactory();

    // Build a deposit tx whose extraTransactionBytes starts with the 0x7e type byte.
    bcos::rpc::Web3Transaction web3Deposit;
    web3Deposit.type = bcos::rpc::TransactionType::Deposit;
    web3Deposit.from = bcos::Address("0xdead000000000000000000000000000000000011");
    web3Deposit.sourceHash =
        bcos::h256("6ab967dfdd3aa359031bef6965cca32ed9a21ea969f7aeee2e58817142a645d7");
    web3Deposit.mint = bcos::u256("0x16345785d8a0000");
    web3Deposit.nonce = 0;
    web3Deposit.isSystemTx = false;
    auto tarsTx = web3Deposit.takeToTarsTransaction();
    bcos::h256 arbitraryHash("0303030303030303030303030303030303030303030303030303030303030303");
    tarsTx.extraTransactionHash.assign(arbitraryHash.begin(), arbitraryHash.end());
    auto tx = std::make_shared<bcostars::protocol::TransactionImpl>(
        [tarsTx = std::move(tarsTx)]() mutable { return &tarsTx; });

    auto receipt = makeReceipt(m_blockFactory);
    protocol::OpStackReceiptMeta meta;
    meta.deposit_nonce = 18;
    meta.deposit_receipt_version = 1;
    receipt->setOpStackMeta(std::move(meta));

    bcos::crypto::HashType blockHash;
    blockHash[0] = 0x77;

    Json::Value result(Json::objectValue);
    combineTxResponse(result, *tx, *receipt, blockHash);
    BOOST_CHECK_EQUAL(result["type"].asString(), "0x7e");
    BOOST_CHECK_EQUAL(result["nonce"].asString(), "0x12");  // 18 = depositNonce
    // MN-6: the tx JSON surfaces depositReceiptVersion from the meta like op-geth does
    // (internal/ethapi/api.go:1210-1213); 1 == Regolith receipt version.
    BOOST_CHECK_EQUAL(result["depositReceiptVersion"].asString(), "0x1");

    // Without a meta (or without deposit_nonce) the 0x0 default stands.
    auto receipt2 = makeReceipt(m_blockFactory);
    Json::Value result2(Json::objectValue);
    combineTxResponse(result2, *tx, *receipt2, blockHash);
    BOOST_CHECK_EQUAL(result2["nonce"].asString(), "0x0");
    BOOST_CHECK(!result2.isMember("depositReceiptVersion"));

    // The no-receipt overload keeps the 0x0 default too.
    Json::Value result3(Json::objectValue);
    combineTxResponse(result3, *tx, /*transactionIndex=*/0, /*blockNumber=*/12, blockHash);
    BOOST_CHECK_EQUAL(result3["nonce"].asString(), "0x0");
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

BOOST_AUTO_TEST_CASE(combineReceiptResponseEmitsOpExtensionFieldsFromMeta)
{
    auto tx = makeWeb3Tx(m_blockFactory, chainId, groupId);
    BOOST_REQUIRE(tx);
    // D8 (review ①): non-deposit from must be the checksum of the RAW sender bytes. Install a raw
    // 20-byte sender; if the read side re-encoded a hex-string (double-encoded) sender this
    // assertion would fail — that is exactly the bug review ① caught on the write side. The
    // address carries mixed-case hex letters (0x5aAe...BeAed) so EIP-55 checksum casing is
    // actually observable — an all-digit address makes toChecksumAddress a no-op and could never
    // regress-catch.
    tx->forceSender(bcos::fromHex("5aAeb6053F3E94C9b9A09f33669435E7Ef1BeAed"));

    auto receipt = makeReceipt(m_blockFactory);
    BOOST_REQUIRE(receipt);
    protocol::OpStackReceiptMeta meta;
    // D2 (P4-1): ALL 13 mappings positively asserted — a wrong JSON key or value on any field
    // would otherwise pass (empty-meta test only proves absence-when-empty). Distinct values so
    // cross-field mixups are caught too.
    meta.l1_gas_price = bcos::u256(5);
    meta.l1_gas_used = 6;
    meta.l1_fee = bcos::u256(10);
    meta.l1_blob_base_fee = bcos::u256(11);
    meta.l1_base_fee_scalar = 12;
    meta.l1_blob_base_fee_scalar = 13;
    meta.operator_fee_scalar = 14;
    meta.operator_fee_constant = 15;
    meta.da_footprint_gas_scalar = 16;
    meta.da_footprint = 17;
    meta.deposit_nonce = 18;
    meta.deposit_receipt_version = 19;
    meta.operator_fee = bcos::u256(20);
    receipt->setOpStackMeta(std::move(meta));

    bcos::crypto::HashType blockHash;
    blockHash[0] = 0x99;

    Json::Value result = Json::objectValue;
    combineReceiptResponse(result, *receipt, *tx, blockHash);

    BOOST_CHECK_EQUAL(result["l1GasPrice"].asString(), "0x5");
    BOOST_CHECK_EQUAL(result["l1GasUsed"].asString(), "0x6");
    BOOST_CHECK_EQUAL(result["l1Fee"].asString(), "0xa");
    BOOST_CHECK_EQUAL(result["l1BlobBaseFee"].asString(), "0xb");
    BOOST_CHECK_EQUAL(result["l1BaseFeeScalar"].asString(), "0xc");
    BOOST_CHECK_EQUAL(result["l1BlobBaseFeeScalar"].asString(), "0xd");
    BOOST_CHECK_EQUAL(result["operatorFeeScalar"].asString(), "0xe");
    BOOST_CHECK_EQUAL(result["operatorFeeConstant"].asString(), "0xf");
    BOOST_CHECK_EQUAL(result["daFootprintGasScalar"].asString(), "0x10");
    BOOST_CHECK_EQUAL(result["blobGasUsed"].asString(), "0x11");  // ← da_footprint (Jovian 复用)
    BOOST_CHECK_EQUAL(result["depositNonce"].asString(), "0x12");
    BOOST_CHECK_EQUAL(result["depositReceiptVersion"].asString(), "0x13");
    BOOST_CHECK_EQUAL(result["operatorFee"].asString(), "0x14");  // FISCO 扩展
    // from = checksum of the raw sender bytes (review ①). Pinned against the independently-known
    // EIP-55 vector 0x5aAeb6053F3E94C9b9A09f33669435E7Ef1BeAed (deliberately NOT derived via the
    // same toChecksumAddress under test, so a checksum-casing regression is actually caught).
    BOOST_CHECK_EQUAL(result["from"].asString(), "0x5aAeb6053F3E94C9b9A09f33669435E7Ef1BeAed");
}

// Important #1 (whole-branch review): receipt `type` for a Web3 tx must equal the EIP-2718 kind
// byte. The write side stores encodeForSign() (RLP WITHOUT the type byte) in extraTransactionBytes
// for typed non-deposit txs, so byte-sniffing extraTransactionBytes collapsed EIP-2930/1559/4844
// receipts into Legacy 0x0 while eth_getTransactionByHash reported the correct kind.
// combineReceiptResponse now reads the `web3TypedTxKind` tars slot (populated by
// takeToTarsTransaction for every Web3 kind).
BOOST_AUTO_TEST_CASE(combineReceiptResponseTypedTxKindType)
{
    auto makeWeb3TarsTx = [&](bcos::rpc::Web3Transaction web3Tx) {
        auto tarsTx = web3Tx.takeToTarsTransaction();
        // D4: manual extraTransactionHash so combineReceiptResponse:19 tx.hash() does not throw.
        bcos::h256 arbitraryHash(
            "0303030303030303030303030303030303030303030303030303030303030303");
        tarsTx.extraTransactionHash.assign(arbitraryHash.begin(), arbitraryHash.end());
        return std::make_shared<bcostars::protocol::TransactionImpl>(
            [tarsTx = std::move(tarsTx)]() mutable { return &tarsTx; });
    };

    // EIP-1559 (0x02): previously misreported as 0x0 Legacy by the byte-sniff.
    {
        bcos::rpc::Web3Transaction web3Tx;
        web3Tx.type = bcos::rpc::TransactionType::EIP1559;
        web3Tx.chainId = 1;
        web3Tx.nonce = 0;
        web3Tx.maxPriorityFeePerGas = bcos::u256(1);
        web3Tx.maxFeePerGas = bcos::u256(2);
        web3Tx.gasLimit = 21000;
        web3Tx.to.emplace(bcos::Address("0x1234567890123456789012345678901234567890"));
        web3Tx.value = bcos::u256(0);
        web3Tx.signatureR = bcos::bytes(32, 0x11);
        web3Tx.signatureS = bcos::bytes(32, 0x22);
        web3Tx.signatureV = 0;
        auto tx = makeWeb3TarsTx(std::move(web3Tx));
        auto receipt = makeReceipt(m_blockFactory);
        Json::Value result = Json::objectValue;
        combineReceiptResponse(result, *receipt, *tx, bcos::crypto::HashType{});
        BOOST_CHECK_EQUAL(result["type"].asString(), "0x2");
    }
    // Deposit (0x7e): full envelope stored, kind slot set to Deposit.
    {
        bcos::rpc::Web3Transaction web3Deposit;
        web3Deposit.type = bcos::rpc::TransactionType::Deposit;
        web3Deposit.from = bcos::Address("0xdead000000000000000000000000000000000011");
        web3Deposit.sourceHash =
            bcos::h256("6ab967dfdd3aa359031bef6965cca32ed9a21ea969f7aeee2e58817142a645d7");
        web3Deposit.mint = bcos::u256("0x16345785d8a0000");
        web3Deposit.nonce = 0;
        web3Deposit.isSystemTx = true;
        auto tx = makeWeb3TarsTx(std::move(web3Deposit));
        auto receipt = makeReceipt(m_blockFactory);
        Json::Value result = Json::objectValue;
        combineReceiptResponse(result, *receipt, *tx, bcos::crypto::HashType{});
        BOOST_CHECK_EQUAL(result["type"].asString(), "0x7e");
    }
    // Legacy BCOSTransaction: web3TypedTxKind() is 0 == Legacy → 0x0.
    {
        auto tx = makeWeb3Tx(m_blockFactory, chainId, groupId);
        auto receipt = makeReceipt(m_blockFactory);
        Json::Value result = Json::objectValue;
        combineReceiptResponse(result, *receipt, *tx, bcos::crypto::HashType{});
        BOOST_CHECK_EQUAL(result["type"].asString(), "0x0");
    }
}

BOOST_AUTO_TEST_CASE(combineReceiptResponseOmitsOpFieldsWhenMetaEmpty)
{
    auto tx = makeWeb3Tx(m_blockFactory, chainId, groupId);
    BOOST_REQUIRE(tx);
    auto receipt = makeReceipt(m_blockFactory);
    BOOST_REQUIRE(receipt);

    bcos::crypto::HashType blockHash;
    Json::Value result = Json::objectValue;
    combineReceiptResponse(result, *receipt, *tx, blockHash);

    // Empty opStackMeta → NO OP fields at all (no zero/default placeholders).
    BOOST_CHECK(!result.isMember("l1GasPrice"));
    BOOST_CHECK(!result.isMember("l1Fee"));
    BOOST_CHECK(!result.isMember("l1GasUsed"));
    BOOST_CHECK(!result.isMember("l1BlobBaseFee"));
    BOOST_CHECK(!result.isMember("l1BaseFeeScalar"));
    BOOST_CHECK(!result.isMember("l1BlobBaseFeeScalar"));
    BOOST_CHECK(!result.isMember("operatorFeeScalar"));
    BOOST_CHECK(!result.isMember("operatorFeeConstant"));
    BOOST_CHECK(!result.isMember("daFootprintGasScalar"));
    BOOST_CHECK(!result.isMember("blobGasUsed"));
    BOOST_CHECK(!result.isMember("depositNonce"));
    BOOST_CHECK(!result.isMember("depositReceiptVersion"));
    BOOST_CHECK(!result.isMember("operatorFee"));
}

// Read-side deposit (0x7e) transaction shape. takeToTarsTransaction() does NOT fill
// extraTransactionHash (review ③/D4), and combineTxResponse:44 calls tx.hash() which throws
// EmptyTransactionHash on an empty one — so the test must install arbitrary 32 bytes.
BOOST_AUTO_TEST_CASE(combineTxResponseDepositMinimalFields)
{
    bcos::rpc::Web3Transaction web3Deposit;
    web3Deposit.type = bcos::rpc::TransactionType::Deposit;
    web3Deposit.from = bcos::Address("0xdead000000000000000000000000000000000011");
    web3Deposit.sourceHash =
        bcos::h256("6ab967dfdd3aa359031bef6965cca32ed9a21ea969f7aeee2e58817142a645d7");
    web3Deposit.mint = bcos::u256("0x16345785d8a0000");
    web3Deposit.nonce = 0;
    web3Deposit.isSystemTx = true;

    auto tarsTx = web3Deposit.takeToTarsTransaction();
    // D4 (review ③): manual extraTransactionHash so combineTxResponse:44 does not throw.
    bcos::h256 arbitraryHash("0101010101010101010101010101010101010101010101010101010101010101");
    tarsTx.extraTransactionHash.assign(arbitraryHash.begin(), arbitraryHash.end());
    bcostars::protocol::TransactionImpl txImpl(
        [tarsTx = std::move(tarsTx)]() mutable { return &tarsTx; });

    Json::Value result = Json::objectValue;
    combineTxResponse(
        result, txImpl, /*transactionIndex=*/3u, /*blockNumber=*/12, bcos::crypto::HashType{});

    BOOST_CHECK(result.isMember("nonce"));  // deposit nonce=0
    BOOST_CHECK_EQUAL(result["type"].asString(), "0x7e");
    // Deposit (0x7e) is numerically larger than every EIP type, so the explicit range checks at
    // TransactionResponse.cpp:68/:86 exclude it from accessList / blob fields.
    BOOST_CHECK(!result.isMember("accessList"));
    BOOST_CHECK(!result.isMember("blobVersionedHashes"));
}

// Read-side EIP-4844 blob transaction: blobVersionedHashes / maxFeePerBlobGas branches.
BOOST_AUTO_TEST_CASE(combineTxResponseBlob4844)
{
    bcos::rpc::Web3Transaction web3Tx;
    web3Tx.type = bcos::rpc::TransactionType::EIP4844;
    web3Tx.chainId = 1;
    web3Tx.nonce = 0;
    web3Tx.maxPriorityFeePerGas = bcos::u256(1);
    web3Tx.maxFeePerGas = bcos::u256(2);
    web3Tx.gasLimit = 21000;
    web3Tx.to.emplace(bcos::Address("0x1234567890123456789012345678901234567890"));
    web3Tx.value = bcos::u256(0);
    web3Tx.maxFeePerBlobGas = bcos::u256(3);
    web3Tx.blobVersionedHashes = {
        bcos::h256("c6bdd1de713471bd6cfa62dd8b5a5b42969ed09e26212d3377f3f8426d8ec210")};
    web3Tx.signatureR = bcos::bytes(32, 0x11);
    web3Tx.signatureS = bcos::bytes(32, 0x22);
    web3Tx.signatureV = 0;

    auto tarsTx = web3Tx.takeToTarsTransaction();
    // D4 (review ③): manual extraTransactionHash so combineTxResponse:44 does not throw.
    bcos::h256 arbitraryHash("0202020202020202020202020202020202020202020202020202020202020202");
    tarsTx.extraTransactionHash.assign(arbitraryHash.begin(), arbitraryHash.end());
    bcostars::protocol::TransactionImpl txImpl(
        [tarsTx = std::move(tarsTx)]() mutable { return &tarsTx; });

    Json::Value result = Json::objectValue;
    combineTxResponse(
        result, txImpl, /*transactionIndex=*/3u, /*blockNumber=*/12, bcos::crypto::HashType{});

    BOOST_CHECK(result.isMember("blobVersionedHashes"));
    BOOST_CHECK(result.isMember("maxFeePerBlobGas"));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
