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
#include <bcos-rlp-protocol/BlockHeaderHash.h>
#include <bcos-rlp-protocol/EthBlockHeader.h>
#include <bcos-rlp-protocol/Web3Transaction.h>
#include <bcos-rpc/web3jsonrpc/model/BlockResponse.h>
#include <bcos-rpc/web3jsonrpc/model/ReceiptResponse.h>
#include <bcos-rpc/web3jsonrpc/model/TransactionResponse.h>
#include <bcos-rpc/web3jsonrpc/utils/util.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>
#include <string_view>

#include <fstream>
#include <sstream>

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
    // Native FISCO (NON_ETH) blocks keep the historical Ethereum-compatible mock shape:
    // baseFeePerGas / withdrawals / blob fields are present with fixed values, and gasLimit
    // falls back to the fixed 30000000 (native headers never set it).
    BOOST_CHECK_EQUAL(result["gasLimit"].asString(), "0x1c9c380");  // 30000000
    BOOST_CHECK_EQUAL(result["baseFeePerGas"].asString(), "0x0");
    BOOST_CHECK(result.isMember("withdrawalsRoot"));
    BOOST_CHECK(result.isMember("blobGasUsed"));
    BOOST_CHECK(result.isMember("excessBlobGas"));
    BOOST_CHECK(result.isMember("parentBeaconBlockRoot"));
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

BOOST_AUTO_TEST_CASE(combineBlockResponseEthHeaderReadsFieldsFromHeader)
{
    auto block = m_blockFactory->createBlock();
    auto header = m_blockFactory->blockHeaderFactory()->createBlockHeader();
    // An Eth CANCUN header: all fork-gated fields come from the header, the timestamp is
    // stored in BlockHeader milliseconds and emitted as seconds (/1000).
    header->setNumber(7);
    header->setTimestamp(1700000000 * 1000LL);  // BlockHeader milliseconds == 1700000000 s
    header->setEthBlockVersion(bcos::protocol::EthBlockVersion::CANCUN);
    header->setParentInfo(bcos::protocol::ParentInfo{.blockNumber = 6,
        .blockHash = bcos::crypto::HashType(
            "0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")});
    header->setUncleHash(bcos::crypto::HashType(
        "0x1dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142fd40d49347"));
    header->setCoinbase(bcos::Address("1234567890abcdef1234567890abcdef12345678"));
    header->setDifficulty(bcos::u256(0));
    header->setNonce(bcos::h64(0));
    header->setPrevRandao(
        bcos::h256("1111111111111111111111111111111111111111111111111111111111111111"));
    header->setGasLimit(bcos::u256(30000000));
    header->setGasUsed(bcos::u256(21000));
    // Required non-optional Eth fields so calculateHash can recompute the RLP hash.
    header->setStateRoot(
        bcos::h256("4444444444444444444444444444444444444444444444444444444444444444"));
    header->setTxsRoot(
        bcos::h256("5555555555555555555555555555555555555555555555555555555555555555"));
    header->setReceiptsRoot(
        bcos::h256("6666666666666666666666666666666666666666666666666666666666666666"));
    bcos::Bloom bloom{};
    bloom[0] = 0xab;
    header->setLogsBloom(bcos::bytesConstRef(bloom.data(), bloom.size()));
    header->setBaseFee(bcos::u256(1000000000));
    header->setWithdrawalsRoot(
        bcos::h256("2222222222222222222222222222222222222222222222222222222222222222"));
    header->setBlobGasUsed(bcos::u256(0));
    header->setExcessBlobGas(bcos::u256(0));
    header->setParentBeaconBlockRoot(
        bcos::h256("3333333333333333333333333333333333333333333333333333333333333333"));
    header->calculateHash(*hashImpl);
    block->setBlockHeader(header);

    Json::Value result(Json::objectValue);
    combineBlockResponse(result, *block, /*fullTxs=*/false);

    // Header-derived Eth fields, not mock constants. miner is the EIP-55 checksummed coinbase.
    auto minerAddr = bcos::Address("1234567890abcdef1234567890abcdef12345678").hex();
    auto minerAddrHash = bcos::crypto::keccak256Hash(bcos::bytesConstRef(minerAddr)).hex();
    toChecksumAddress(minerAddr, minerAddrHash);
    BOOST_CHECK_EQUAL(result["miner"].asString(), "0x" + minerAddr);
    BOOST_CHECK_NE(result["miner"].asString(), "0x1234567890abcdef1234567890abcdef12345678");
    BOOST_CHECK_EQUAL(result["sha3Uncles"].asString(),
        "0x1dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142fd40d49347");
    BOOST_CHECK_EQUAL(result["nonce"].asString(), "0x0000000000000000");
    BOOST_CHECK_EQUAL(result["mixHash"].asString(),
        "0x1111111111111111111111111111111111111111111111111111111111111111");
    // Eth timestamp: header milliseconds /1000 = seconds.
    BOOST_CHECK_EQUAL(result["timestamp"].asString(), "0x6553f100");
    // gasLimit/gasUsed come from the header.
    BOOST_CHECK_EQUAL(result["gasLimit"].asString(), "0x1c9c380");  // 30000000
    BOOST_CHECK_EQUAL(result["gasUsed"].asString(), "0x5208");      // 21000
    // Eth blocks take logsBloom from the header (bloom[0] = 0xab), not from the block body.
    BOOST_CHECK(result["logsBloom"].asString().starts_with("0xab"));
    // CANCUN fork-gated fields: present.
    BOOST_CHECK_EQUAL(result["baseFeePerGas"].asString(), "0x3b9aca00");  // 1000000000
    BOOST_CHECK_EQUAL(result["withdrawalsRoot"].asString(),
        "0x2222222222222222222222222222222222222222222222222222222222222222");
    BOOST_CHECK(result.isMember("blobGasUsed"));
    BOOST_CHECK(result.isMember("excessBlobGas"));
    BOOST_CHECK_EQUAL(result["parentBeaconBlockRoot"].asString(),
        "0x3333333333333333333333333333333333333333333333333333333333333333");
    // PRAGUE-only field: not defined for a CANCUN header.
    BOOST_CHECK(!result.isMember("requestsHash"));

    // Independent EIP-55 oracle: the checksum above recomputes via the same
    // toChecksumAddress as the implementation, so pin one official EIP-55 spec vector
    // (the spec's first example) to catch a wrong checksum algorithm.
    std::string specAddr = "5aaeb6053f3e94c9b9a09f33669435e7ef1beaed";
    auto specHash = bcos::crypto::keccak256Hash(bcos::bytesConstRef(specAddr)).hex();
    toChecksumAddress(specAddr, specHash);
    BOOST_CHECK_EQUAL(specAddr, "5aAeb6053F3E94C9b9A09f33669435E7Ef1BeAed");
}

// Build an Eth header with the given fork marker plus the fork-gated fields that fork
// would carry, so the RPC response shape can be asserted per fork.
static std::shared_ptr<bcos::protocol::Block> makeEthHeaderBlock(
    std::shared_ptr<bcos::protocol::BlockFactory> const& blockFactory,
    std::shared_ptr<bcos::crypto::Hash> const& hashImpl, bcos::protocol::EthBlockVersion version,
    std::optional<bcos::u256> baseFee, std::optional<bcos::h256> withdrawalsRoot,
    std::optional<bcos::u256> blobGasUsed, std::optional<bcos::u256> excessBlobGas,
    std::optional<bcos::h256> parentBeaconBlockRoot, std::optional<bcos::h256> requestsHash)
{
    auto block = blockFactory->createBlock();
    auto header = blockFactory->blockHeaderFactory()->createBlockHeader();
    header->setNumber(7);
    header->setTimestamp(1700000000 * 1000LL);  // BlockHeader milliseconds == 1700000000 s
    header->setEthBlockVersion(version);
    header->setParentInfo(bcos::protocol::ParentInfo{.blockNumber = 6,
        .blockHash = bcos::crypto::HashType(
            "0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")});
    header->setUncleHash(bcos::crypto::HashType(
        "0x1dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142fd40d49347"));
    header->setCoinbase(bcos::Address("1234567890abcdef1234567890abcdef12345678"));
    header->setDifficulty(bcos::u256(0));
    header->setNonce(bcos::h64(0));
    header->setPrevRandao(
        bcos::h256("1111111111111111111111111111111111111111111111111111111111111111"));
    header->setGasLimit(bcos::u256(30000000));
    header->setGasUsed(bcos::u256(21000));
    header->setStateRoot(
        bcos::h256("4444444444444444444444444444444444444444444444444444444444444444"));
    header->setTxsRoot(
        bcos::h256("5555555555555555555555555555555555555555555555555555555555555555"));
    header->setReceiptsRoot(
        bcos::h256("6666666666666666666666666666666666666666666666666666666666666666"));
    bcos::Bloom bloom{};
    bloom[0] = 0xab;
    header->setLogsBloom(bcos::bytesConstRef(bloom.data(), bloom.size()));
    if (baseFee)
    {
        header->setBaseFee(*baseFee);
    }
    if (withdrawalsRoot)
    {
        header->setWithdrawalsRoot(*withdrawalsRoot);
    }
    if (blobGasUsed)
    {
        header->setBlobGasUsed(*blobGasUsed);
    }
    if (excessBlobGas)
    {
        header->setExcessBlobGas(*excessBlobGas);
    }
    if (parentBeaconBlockRoot)
    {
        header->setParentBeaconBlockRoot(*parentBeaconBlockRoot);
    }
    if (requestsHash)
    {
        header->setRequestsHash(*requestsHash);
    }
    header->calculateHash(*hashImpl);
    block->setBlockHeader(header);
    return block;
}

BOOST_AUTO_TEST_CASE(combineBlockResponseOpNonEthUsesRlpIdentityHashAndPrevRandao)
{
    auto block = m_blockFactory->createBlock();
    auto header = m_blockFactory->blockHeaderFactory()->createBlockHeader();
    header->setNumber(1);
    header->setTimestamp(1700000000 * 1000LL);
    header->setParentInfo(bcos::protocol::ParentInfo{.blockNumber = 0,
        .blockHash = bcos::crypto::HashType(
            "0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")});
    header->setCoinbase(bcos::Address("4200000000000000000000000000000000000011"));
    header->setUncleHash(
        bcos::h256("0x1dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142fd40d49347"));
    header->setDifficulty(bcos::u256(0));
    header->setNonce(bcos::h64(0));
    header->setPrevRandao(
        bcos::h256("62293916ac98bc02b90472638bd2beb1b531a914395c34239abe6fc011b9011a"));
    header->setGasLimit(bcos::u256(30000000));
    header->setGasUsed(bcos::u256(21000));
    header->setStateRoot(
        bcos::h256("4444444444444444444444444444444444444444444444444444444444444444"));
    header->setTxsRoot(
        bcos::h256("5555555555555555555555555555555555555555555555555555555555555555"));
    header->setReceiptsRoot(
        bcos::h256("6666666666666666666666666666666666666666666666666666666666666666"));
    // Value-initialised: bcos::Bloom is a std::array with no default initialisation, so
    // `Bloom bloom;` would leave 255 bytes indeterminate and make the header hash vary run
    // to run (which is why this case could only ever compare the code to itself).
    bcos::Bloom bloom{};
    bloom[0] = 0xcd;
    header->setLogsBloom(bcos::bytesConstRef(bloom.data(), bloom.size()));
    header->setExtraData(bcos::bytes{0x01, 0x00, 0x00, 0x00, 0xfa, 0x00, 0x00, 0x00, 0x06, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
    header->setBaseFee(bcos::u256(1000000000));
    header->setWithdrawalsRoot(
        bcos::h256("2222222222222222222222222222222222222222222222222222222222222222"));
    header->setBlobGasUsed(bcos::u256(0));
    header->setExcessBlobGas(bcos::u256(0));
    header->setParentBeaconBlockRoot(
        bcos::h256("3333333333333333333333333333333333333333333333333333333333333333"));
    header->setRequestsHash(
        bcos::h256("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    header->calculateHash(*hashImpl);
    auto const tarsHash = header->hash();
    auto const rlpHash = bcos::protocol::EthBlockHeader::computeHash(*header);
    BOOST_CHECK(tarsHash != rlpHash);
    block->setBlockHeader(header);

    Json::Value result(Json::objectValue);
    combineBlockResponse(result, *block, /*fullTxs=*/false);

    // Golden literal, independent of the code under test: the header is fully deterministic
    // now that the bloom is value-initialised, so a change to the RLP identity hash or to
    // any hashed field fails here instead of comparing the production function to itself.
    BOOST_CHECK_EQUAL(result["hash"].asString(),
        "0x2aa80e9130ed69be2160c354c483adf10e39d7ed2d899ba7dd22c39d4136f8b4");
    BOOST_CHECK_NE(result["hash"].asString(), tarsHash.hexPrefixed());
    BOOST_CHECK_EQUAL(result["mixHash"].asString(),
        "0x62293916ac98bc02b90472638bd2beb1b531a914395c34239abe6fc011b9011a");
    BOOST_CHECK_EQUAL(result["baseFeePerGas"].asString(), "0x3b9aca00");
    BOOST_CHECK(result.isMember("withdrawalsRoot"));
    BOOST_CHECK(result.isMember("requestsHash"));
}

// The fork-gated key matrix must match geth's eth_getBlock* shape exactly: LONDON has only
// baseFeePerGas; SHANGHAI adds withdrawals/withdrawalsRoot; CANCUN adds the blob trio;
// PRAGUE adds requestsHash. A wrong presence/absence on any fork must fail here.
BOOST_AUTO_TEST_CASE(combineBlockResponseEthForkShapesGateKeys)
{
    using bcos::protocol::EthBlockVersion;

    struct Case
    {
        EthBlockVersion version;
        bool expectBaseFee;
        bool expectWithdrawals;
        bool expectBlobTrio;
        bool expectRequestsHash;
    };
    std::vector<Case> const cases{
        {EthBlockVersion::LONDON, true, false, false, false},
        {EthBlockVersion::SHANGHAI, true, true, false, false},
        {EthBlockVersion::CANCUN, true, true, true, false},
        {EthBlockVersion::PRAGUE, true, true, true, true},
    };

    for (auto const& c : cases)
    {
        // The header must carry exactly the fields its fork permits: validateHeader
        // forbids a field the version does not know (e.g. withdrawalsRoot on LONDON).
        auto baseFee = bcos::u256(1000000000);
        std::optional<bcos::h256> withdrawalsRoot;
        std::optional<bcos::u256> blobGasUsed;
        std::optional<bcos::u256> excessBlobGas;
        std::optional<bcos::h256> parentBeaconBlockRoot;
        std::optional<bcos::h256> requestsHash;
        if (c.expectWithdrawals)
        {
            withdrawalsRoot =
                bcos::h256("2222222222222222222222222222222222222222222222222222222222222222");
        }
        if (c.expectBlobTrio)
        {
            blobGasUsed = bcos::u256(0);
            excessBlobGas = bcos::u256(0);
            parentBeaconBlockRoot =
                bcos::h256("3333333333333333333333333333333333333333333333333333333333333333");
        }
        if (c.expectRequestsHash)
        {
            requestsHash =
                bcos::h256("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
        }
        auto block = makeEthHeaderBlock(m_blockFactory, hashImpl, c.version, baseFee,
            withdrawalsRoot, blobGasUsed, excessBlobGas, parentBeaconBlockRoot, requestsHash);

        Json::Value result(Json::objectValue);
        combineBlockResponse(result, *block, /*fullTxs=*/false);

        BOOST_CHECK_EQUAL(result.isMember("baseFeePerGas"), c.expectBaseFee);
        BOOST_CHECK_EQUAL(result.isMember("withdrawals"), c.expectWithdrawals);
        BOOST_CHECK_EQUAL(result.isMember("withdrawalsRoot"), c.expectWithdrawals);
        BOOST_CHECK_EQUAL(result.isMember("blobGasUsed"), c.expectBlobTrio);
        BOOST_CHECK_EQUAL(result.isMember("excessBlobGas"), c.expectBlobTrio);
        BOOST_CHECK_EQUAL(result.isMember("parentBeaconBlockRoot"), c.expectBlobTrio);
        BOOST_CHECK_EQUAL(result.isMember("requestsHash"), c.expectRequestsHash);
    }
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

/// The per-log branch needs a NON-EMPTY logs vector: log.address must be the EIP-55
/// checksum of the raw 20 bytes and log.transactionIndex a hex quantity. The base emitted
/// toQuantity(transactionIndex) on an already-hex string ("0x307833") and the raw-byte
/// address, so this case is the regression pin for both fixes.
BOOST_AUTO_TEST_CASE(combineReceiptResponseEmitsLogAddressAndIndex)
{
    auto txFactory = m_blockFactory->transactionFactory();
    auto tx = txFactory->createTransaction(0, "0x1234567890123456789012345678901234567890",
        bcos::bytes{0x0a}, "0x2", 100, chainId, groupId, 0);
    BOOST_REQUIRE(tx);

    // EIP-55 test vector: 0x5aaeb6053f3e94c9b9a09f33669435e7ef1beaed must checksum to the
    // mixed-case form below.
    bcos::bytes const logAddress = bcos::fromHex("5aaeb6053f3e94c9b9a09f33669435e7ef1beaed");
    bcos::h256s const topics{
        bcos::h256("0000000000000000000000000000000000000000000000000000000000000001")};
    std::vector<bcos::protocol::LogEntry> logs;
    logs.emplace_back(logAddress, topics, bcos::bytes{0x01, 0x02});

    auto receiptFactory = m_blockFactory->receiptFactory();
    auto receipt = receiptFactory->createReceipt(bcos::u256(21000),
        "0x1234567890123456789012345678901234567890", logs, /*status=*/0, bcos::bytesConstRef{},
        /*blockNumber=*/12);
    BOOST_REQUIRE(receipt);
    receipt->setTransactionIndex(3);

    bcos::crypto::HashType blockHash;
    Json::Value result(Json::objectValue);
    combineReceiptResponse(result, *receipt, *tx, blockHash);

    BOOST_REQUIRE_EQUAL(result["logs"].size(), 1U);
    auto const& log = result["logs"][0U];
    BOOST_CHECK_EQUAL(log["address"].asString(), "0x5aAeb6053F3E94C9b9A09f33669435E7Ef1BeAed");
    BOOST_CHECK_EQUAL(log["transactionIndex"].asString(), "0x3");
    BOOST_CHECK_EQUAL(log["logIndex"].asString(), "0x0");
    BOOST_REQUIRE_EQUAL(log["topics"].size(), 1U);
    BOOST_CHECK_EQUAL(log["topics"][0U].asString(), topics[0].hexPrefixed());
    BOOST_CHECK_EQUAL(log["data"].asString(), "0x0102");
    BOOST_CHECK_EQUAL(log["removed"].asBool(), false);
}

BOOST_AUTO_TEST_CASE(canonicalBlockHashUsesTheRlpHashForOpHeaders)
{
    // The RPC reports this hash through every producer of a blockHash field (the block
    // response, the transaction-by-block-number response, eth_getLogs log entries), so a
    // client can always match a tx/log back to the block it read.
    auto header = m_blockFactory->blockHeaderFactory()->createBlockHeader();
    BOOST_REQUIRE(header);
    header->setEthBlockVersion(bcos::protocol::EthBlockVersion::NON_ETH);
    header->setWithdrawalsRoot(bcos::h256(1U));
    header->setBaseFee(bcos::u256(1));
    BOOST_REQUIRE(bcos::protocol::isOpEthereumBlock(*header));

    // Golden pinned externally (keccak256 over the RLP encoding of exactly this header's
    // field set), not derived from canonicalBlockHash — the OP branch of the predicate
    // delegates to EthBlockHeader::computeHash, so comparing the two calls against each
    // other can never fail, and a broken hash would pass unnoticed.
    constexpr std::string_view c_opHeaderGoldenHash =
        "fb8ad653db984845f2d6e8271069d37e99da6454e798740654bacdebae7655ec";
    BOOST_CHECK_EQUAL(bcos::protocol::EthBlockHeader::computeHash(*header).hex(),
        std::string(c_opHeaderGoldenHash));
    BOOST_CHECK_EQUAL(
        bcos::protocol::canonicalBlockHash(*header).hex(), std::string(c_opHeaderGoldenHash));
}

BOOST_AUTO_TEST_CASE(combineReceiptResponseAcceptsTheFiscoLaneAddressForm)
{
    // The FISCO / eth-mode VM stores a log's address as its ASCII hex TEXT
    // (bcos-executor HostContext::log passes myAddress()), while the OP lane stores the raw
    // 20 bytes (OpTransition's mapOpLogAddress). The encoder must publish the same field for
    // both: hex-encoding the text form yields hex-of-ASCII and ethers rejects the whole
    // receipt, which is what the Air integration harness caught. The case above pins the
    // byte form; this one pins the text form.
    auto txFactory = m_blockFactory->transactionFactory();
    auto tx = txFactory->createTransaction(0, "0x1234567890123456789012345678901234567890",
        bcos::bytes{0x0a}, "0x2", 100, chainId, groupId, 0);
    BOOST_REQUIRE(tx);

    std::string const textAddress = "800df9e6e4f7146932d97e89b66ca6ae55b0c9de";
    bcos::bytes const logAddress(textAddress.begin(), textAddress.end());
    bcos::h256s const topics{bcos::h256(1U)};
    std::vector<bcos::protocol::LogEntry> logs;
    logs.emplace_back(logAddress, topics, bcos::bytes{});

    auto receiptFactory = m_blockFactory->receiptFactory();
    auto receipt = receiptFactory->createReceipt(bcos::u256(21000),
        "0x1234567890123456789012345678901234567890", logs, /*status=*/0, bcos::bytesConstRef{},
        /*blockNumber=*/12);
    BOOST_REQUIRE(receipt);

    Json::Value result(Json::objectValue);
    combineReceiptResponse(result, *receipt, *tx, bcos::crypto::HashType{});
    BOOST_REQUIRE_EQUAL(result["logs"].size(), 1U);
    auto const& out = result["logs"][0U]["address"].asString();
    BOOST_REQUIRE_EQUAL(out.size(), 42U);
    BOOST_CHECK_EQUAL(boost::algorithm::to_lower_copy(out.substr(2)), textAddress);
    // The regression shape, stated independently of the checksum implementation: never the
    // hex-of-ASCII form.
    BOOST_CHECK_NE(out, "0x" + bcos::toHex(textAddress));
}

BOOST_AUTO_TEST_CASE(combineReceiptResponseEmitsOpExtensionFieldsFromMeta)
{
    auto tx = makeWeb3Tx(m_blockFactory, chainId, groupId);
    BOOST_REQUIRE(tx);
    // Non-deposit from must be the checksum of the RAW sender bytes: install a raw 20-byte
    // sender; a read side that re-encoded a hex-string (double-encoded) sender would fail here.
    // The address carries mixed-case hex letters (0x5aAe...BeAed) so EIP-55 checksum casing is
    // actually observable — an all-digit address makes toChecksumAddress a no-op and could never
    // regress-catch.
    tx->forceSender(bcos::fromHex("5aAeb6053F3E94C9b9A09f33669435E7Ef1BeAed"));

    auto receipt = makeReceipt(m_blockFactory);
    BOOST_REQUIRE(receipt);
    protocol::OpStackReceiptMeta meta;
    // ALL 14 mappings positively asserted — a wrong JSON key or value on any field would
    // otherwise pass (empty-meta test only proves absence-when-empty). Distinct values so
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
    meta.l1_fee_scalar = bcos::u256(1'000'000);  // raw slot-6; upstream FeeScalar = /1e6
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
    // l1FeeScalar: raw 1e6 -> upstream scaled FeeScalar "1" (op-geth intToScaledFloat decimal
    // text; whole units render without a fractional part).
    BOOST_CHECK_EQUAL(result["l1FeeScalar"].asString(), "1");
    // from = checksum of the raw sender bytes. Pinned against the independently-known
    // EIP-55 vector 0x5aAeb6053F3E94C9b9A09f33669435E7Ef1BeAed (deliberately NOT derived via the
    // same toChecksumAddress under test, so a checksum-casing regression is actually caught).
    BOOST_CHECK_EQUAL(result["from"].asString(), "0x5aAeb6053F3E94C9b9A09f33669435E7Ef1BeAed");
}

// Receipt `type` for a Web3 tx must equal the EIP-2718 kind byte: the write side stores
// encodeForSign() (RLP WITHOUT the type byte) in extraTransactionBytes for typed non-deposit
// txs, so byte-sniffing extraTransactionBytes collapsed EIP-2930/1559/4844 receipts into
// Legacy 0x0 while eth_getTransactionByHash reported the correct kind.
// combineReceiptResponse now reads the `web3TypedTxKind` tars slot (populated by
// takeToTarsTransaction for every Web3 kind).
BOOST_AUTO_TEST_CASE(combineReceiptResponseTypedTxKindType)
{
    auto makeWeb3TarsTx = [&](bcos::rpc::Web3Transaction web3Tx) {
        auto tarsTx = web3Tx.takeToTarsTransaction();
        // Manual extraTransactionHash so combineReceiptResponse:19 tx.hash() does not throw.
        bcos::h256 arbitraryHash(
            "0303030303030303030303030303030303030303030303030303030303030303");
        tarsTx.extraTransactionHash.assign(arbitraryHash.begin(), arbitraryHash.end());
        return std::make_shared<bcostars::protocol::TransactionImpl>(
            [tarsTx = std::move(tarsTx)]() mutable { return &tarsTx; });
    };

    // EIP-1559 (0x02): must report 0x02, not fall through to 0x0 Legacy.
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
    // Divergence pin: a legacy-shaped envelope (first byte is the RLP list header — a byte-sniff
    // implementation would report Legacy) whose web3TypedTxKind slot is forged to EIP-1559. The
    // response must follow the kind slot (0x2), not the envelope's first byte (0x0). The three
    // cases above cannot distinguish the two implementations — typed/deposit envelopes keep their
    // type byte, so byte-sniffing renders identical output.
    {
        bcos::rpc::Web3Transaction legacyTx;
        legacyTx.type = bcos::rpc::TransactionType::Legacy;
        legacyTx.chainId = 1;
        legacyTx.nonce = 0;
        legacyTx.maxPriorityFeePerGas = bcos::u256(1);
        legacyTx.maxFeePerGas = bcos::u256(2);
        legacyTx.gasLimit = 21000;
        legacyTx.to.emplace(bcos::Address("0x1234567890123456789012345678901234567890"));
        legacyTx.value = bcos::u256(0);
        legacyTx.signatureR = bcos::bytes(32, 0x11);
        legacyTx.signatureS = bcos::bytes(32, 0x22);
        legacyTx.signatureV = 0;
        auto tarsTx = legacyTx.takeToTarsTransaction();
        tarsTx.web3TypedTxKind = static_cast<tars::Char>(bcos::rpc::TransactionType::EIP1559);
        bcos::h256 arbitraryHash(
            "0404040404040404040404040404040404040404040404040404040404040404");
        tarsTx.extraTransactionHash.assign(arbitraryHash.begin(), arbitraryHash.end());
        auto tx = std::make_shared<bcostars::protocol::TransactionImpl>(
            [tarsTx = std::move(tarsTx)]() mutable { return &tarsTx; });
        auto receipt = makeReceipt(m_blockFactory);
        Json::Value result = Json::objectValue;
        combineReceiptResponse(result, *receipt, *tx, bcos::crypto::HashType{});
        BOOST_CHECK_EQUAL(result["type"].asString(), "0x2");
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
    BOOST_CHECK(!result.isMember("l1FeeScalar"));
}

// F4 presence rule: upstream emits l1FeeScalar on pre-Ecotone receipts and the new scalar
// fields instead from Ecotone on (op-geth gen_receipt_json.go:40 / receipt.go:91-93; op-reth
// crates/rpc/src/eth/receipt.rs:184-193). The FISCO emitter must not invent the field when
// the meta lacks it, nor drop it when the Bedrock shape is the one populated.
BOOST_AUTO_TEST_CASE(combineReceiptResponseL1FeeScalarFollowsMetaPresence)
{
    auto tx = makeWeb3Tx(m_blockFactory, chainId, groupId);
    BOOST_REQUIRE(tx);
    bcos::crypto::HashType blockHash;

    {  // Bedrock shape: l1_fee_scalar present, Ecotone scalars absent
        auto receipt = makeReceipt(m_blockFactory);
        protocol::OpStackReceiptMeta meta;
        meta.l1_gas_price = bcos::u256(1);
        meta.l1_gas_used = 2;
        meta.l1_fee = bcos::u256(3);
        meta.l1_fee_scalar = bcos::u256(2'000'000);  // raw 2e6 -> scaled FeeScalar "2"
        receipt->setOpStackMeta(std::move(meta));

        Json::Value result = Json::objectValue;
        combineReceiptResponse(result, *receipt, *tx, blockHash);
        BOOST_CHECK(result.isMember("l1FeeScalar"));
        BOOST_CHECK_EQUAL(result["l1FeeScalar"].asString(), "2");
        BOOST_CHECK(!result.isMember("l1BaseFeeScalar"));
        BOOST_CHECK(!result.isMember("l1BlobBaseFeeScalar"));
    }

    {  // Ecotone shape: Ecotone scalars present, l1_fee_scalar absent
        auto receipt = makeReceipt(m_blockFactory);
        protocol::OpStackReceiptMeta meta;
        meta.l1_gas_price = bcos::u256(1);
        meta.l1_gas_used = 2;
        meta.l1_fee = bcos::u256(3);
        meta.l1_base_fee_scalar = 4;
        meta.l1_blob_base_fee_scalar = 5;
        receipt->setOpStackMeta(std::move(meta));

        Json::Value result = Json::objectValue;
        combineReceiptResponse(result, *receipt, *tx, blockHash);
        BOOST_CHECK(!result.isMember("l1FeeScalar"));
        BOOST_CHECK_EQUAL(result["l1BaseFeeScalar"].asString(), "0x4");
        BOOST_CHECK_EQUAL(result["l1BlobBaseFeeScalar"].asString(), "0x5");
    }
}

// Read-side deposit (0x7e) transaction shape. takeToTarsTransaction() does NOT fill
// extraTransactionHash, and combineTxResponse:44 calls tx.hash() which throws
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
    // Manual extraTransactionHash so combineTxResponse:44 does not throw.
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
    // Manual extraTransactionHash so combineTxResponse:44 does not throw.
    bcos::h256 arbitraryHash("0202020202020202020202020202020202020202020202020202020202020202");
    tarsTx.extraTransactionHash.assign(arbitraryHash.begin(), arbitraryHash.end());
    bcostars::protocol::TransactionImpl txImpl(
        [tarsTx = std::move(tarsTx)]() mutable { return &tarsTx; });

    Json::Value result = Json::objectValue;
    combineTxResponse(
        result, txImpl, /*transactionIndex=*/3u, /*blockNumber=*/12, bcos::crypto::HashType{});

    // Length- and value-pinned: a reintroduced resize()+append() on blobVersionedHashes (the
    // 2N-array bug class) or reverting maxFeePerBlobGas to decimal .str() fails these.
    BOOST_REQUIRE(result.isMember("blobVersionedHashes"));
    BOOST_REQUIRE_EQUAL(result["blobVersionedHashes"].size(), 1u);
    BOOST_CHECK_EQUAL(result["blobVersionedHashes"][0].asString(),
        "0xc6bdd1de713471bd6cfa62dd8b5a5b42969ed09e26212d3377f3f8426d8ec210");
    BOOST_REQUIRE(result.isMember("maxFeePerBlobGas"));
    BOOST_CHECK_EQUAL(result["maxFeePerBlobGas"].asString(), "0x3");
}

// A legacy (type-0) tx must expose the EIP-155 `v` = chainId*2 + 35 + yParity (geth's
// RawSignatureValues semantics), NOT the raw yParity 0/1. FISCO stores the internal signature
// as r||s||yParity, so the response used to emit `v` = 0/1 for a legacy tx; go-ethereum then
// fails sender recovery with "invalid transaction v, r, s values", which stalls op-node (or any
// geth-based CL) the moment a block contains a legacy transaction.
BOOST_AUTO_TEST_CASE(combineTxResponseLegacyUsesEip155V)
{
    constexpr uint64_t c_chainId = 914901;  // 0xdf5d5
    constexpr uint8_t c_parity = 1;
    constexpr uint64_t c_eip155V = c_chainId * 2 + 35 + c_parity;  // 0x1bebce

    bcos::rpc::Web3Transaction legacy;
    legacy.type = bcos::rpc::TransactionType::Legacy;
    legacy.chainId = c_chainId;
    legacy.nonce = 0;
    legacy.maxPriorityFeePerGas = bcos::u256(2000000000);  // gasPrice for a legacy tx
    legacy.gasLimit = 21000;
    legacy.to.emplace(bcos::Address("0xdeaddeaddeaddeaddeaddeaddeaddeaddead0000"));
    legacy.value = bcos::u256(81);
    legacy.signatureR = bcos::bytes(32, 0x11);
    legacy.signatureS = bcos::bytes(32, 0x22);
    // yParity; encode() derives the wire v = chainId*2 + 35 + yParity for the full envelope.
    legacy.signatureV = c_parity;

    // Full EIP-2718 envelope (the ledger's read path stores the full envelope, not the preimage).
    auto const fullEnvelope = legacy.encode();
    // takeToTarsTransaction stores the internal signature as r||s||yParity — the value the
    // buggy code read as `v`.
    auto tarsTx = legacy.takeToTarsTransaction();
    tarsTx.extraTransactionBytes.assign(fullEnvelope.begin(), fullEnvelope.end());
    bcos::h256 arbitraryHash("0303030303030303030303030303030303030303030303030303030303030303");
    tarsTx.extraTransactionHash.assign(arbitraryHash.begin(), arbitraryHash.end());
    bcostars::protocol::TransactionImpl txImpl(
        [tarsTx = std::move(tarsTx)]() mutable { return &tarsTx; });

    Json::Value result = Json::objectValue;
    combineTxResponse(
        result, txImpl, /*transactionIndex=*/0u, /*blockNumber=*/12, bcos::crypto::HashType{});

    BOOST_CHECK_EQUAL(result["type"].asString(), "0x0");
    BOOST_CHECK_EQUAL(result["chainId"].asString(), toQuantity(c_chainId));
    BOOST_CHECK_EQUAL(result["v"].asString(), toQuantity(c_eip155V));
}

// Positive serialization test for the EIP-2930 accessList and EIP-7702 authorizationList
// branches of combineTxResponse (TransactionResponse.cpp:82-148). A wrong key name or a
// hex-prefix/quantity drift on either list would pass a coverage-by-absence test — these
// assertions pin the exact output shape the write side (takeToTarsTransaction) mirrors.
BOOST_AUTO_TEST_CASE(combineTxResponseAccessAndAuthLists)
{
    // EIP-2930: accessList {address, storageKeys} with hex-prefixed keys.
    {
        bcos::rpc::Web3Transaction web3Tx;
        web3Tx.type = bcos::rpc::TransactionType::EIP2930;
        web3Tx.chainId = 1;
        web3Tx.nonce = 0;
        web3Tx.maxPriorityFeePerGas = bcos::u256(1);
        web3Tx.maxFeePerGas = bcos::u256(2);
        web3Tx.gasLimit = 21000;
        web3Tx.to.emplace(bcos::Address("0x1234567890123456789012345678901234567890"));
        web3Tx.value = bcos::u256(0);
        web3Tx.accessList = {{bcos::Address("0xde0b295669a9fd93d5f28d9ec85e40f4cb697bae"),
            {bcos::crypto::HashType(
                "0x0000000000000000000000000000000000000000000000000000000000000003")}}};
        web3Tx.signatureR = bcos::bytes(32, 0x11);
        web3Tx.signatureS = bcos::bytes(32, 0x22);
        web3Tx.signatureV = 0;

        auto tarsTx = web3Tx.takeToTarsTransaction();
        bcos::h256 arbitraryHash(
            "0303030303030303030303030303030303030303030303030303030303030303");
        tarsTx.extraTransactionHash.assign(arbitraryHash.begin(), arbitraryHash.end());
        bcostars::protocol::TransactionImpl txImpl(
            [tarsTx = std::move(tarsTx)]() mutable { return &tarsTx; });

        Json::Value result = Json::objectValue;
        combineTxResponse(
            result, txImpl, /*transactionIndex=*/3u, /*blockNumber=*/12, bcos::crypto::HashType{});

        BOOST_REQUIRE(result.isMember("accessList"));
        BOOST_REQUIRE(result["accessList"].isArray());
        BOOST_REQUIRE_EQUAL(result["accessList"].size(), 1u);
        BOOST_CHECK_EQUAL(result["accessList"][0]["address"].asString(),
            "0xde0b295669a9fd93d5f28d9ec85e40f4cb697bae");
        BOOST_REQUIRE(result["accessList"][0]["storageKeys"].isArray());
        BOOST_REQUIRE_EQUAL(result["accessList"][0]["storageKeys"].size(), 1u);
        BOOST_CHECK_EQUAL(result["accessList"][0]["storageKeys"][0].asString(),
            "0x0000000000000000000000000000000000000000000000000000000000000003");
    }

    // EIP-7702: authorizationList {chainId, address, nonce, yParity, r, s} — hex-quantity
    // scalars, hex-prefixed address.
    {
        bcos::rpc::Web3Transaction web3Tx;
        web3Tx.type = bcos::rpc::TransactionType::EIP7702;
        web3Tx.chainId = 1;
        web3Tx.nonce = 0;
        web3Tx.maxPriorityFeePerGas = bcos::u256(1);
        web3Tx.maxFeePerGas = bcos::u256(2);
        web3Tx.gasLimit = 21000;
        web3Tx.to.emplace(bcos::Address("0x1234567890123456789012345678901234567890"));
        web3Tx.value = bcos::u256(0);
        web3Tx.authorizationList = {
            {bcos::u256(0x1234), bcos::Address("0xaaaa0000000000000000000000000000000000bb"),
                /*nonce=*/7, /*yParity=*/1, bcos::u256(0x101), bcos::u256(0x202)}};
        web3Tx.signatureR = bcos::bytes(32, 0x11);
        web3Tx.signatureS = bcos::bytes(32, 0x22);
        web3Tx.signatureV = 0;

        auto tarsTx = web3Tx.takeToTarsTransaction();
        bcos::h256 arbitraryHash(
            "0404040404040404040404040404040404040404040404040404040404040404");
        tarsTx.extraTransactionHash.assign(arbitraryHash.begin(), arbitraryHash.end());
        bcostars::protocol::TransactionImpl txImpl(
            [tarsTx = std::move(tarsTx)]() mutable { return &tarsTx; });

        Json::Value result = Json::objectValue;
        combineTxResponse(
            result, txImpl, /*transactionIndex=*/3u, /*blockNumber=*/12, bcos::crypto::HashType{});

        BOOST_REQUIRE(result.isMember("authorizationList"));
        BOOST_REQUIRE(result["authorizationList"].isArray());
        BOOST_REQUIRE_EQUAL(result["authorizationList"].size(), 1u);
        auto const& auth = result["authorizationList"][0];
        BOOST_CHECK_EQUAL(auth["chainId"].asString(), "0x1234");
        BOOST_CHECK_EQUAL(auth["address"].asString(), "0xaaaa0000000000000000000000000000000000bb");
        BOOST_CHECK_EQUAL(auth["nonce"].asString(), "0x7");
        BOOST_CHECK_EQUAL(auth["yParity"].asString(), "0x1");
        BOOST_CHECK_EQUAL(auth["r"].asString(), "0x101");
        BOOST_CHECK_EQUAL(auth["s"].asString(), "0x202");
        // Blob fields are EIP-4844-only (== EIP4844 narrowing, TransactionResponse.cpp:136): a
        // regression to >= EIP4844 would leak maxFeePerBlobGas/blobVersionedHashes onto 7702.
        BOOST_CHECK(!result.isMember("maxFeePerBlobGas"));
        BOOST_CHECK(!result.isMember("blobVersionedHashes"));
    }
}

// ---------------------------------------------------------------------------
// WI-E2: pin the deposit (0x7e) receipt RPC shape against REAL op-geth output.
//
// The goldens under rpc/golden/op-geth-deposit-receipt/ are verbatim
// eth_getTransactionReceipt / eth_getTransactionByHash responses captured from a live
// op-geth devnet (geth built from e8800cffe53d459cde8a07c8e8f1de9d86e79e07, corpus
// opdevnet.sh, L2 RPC :9545; capture recipe: corpus tools/devnet/README.md §WI-E2).
// This closes the "FISCO writes its own fixtures and passes its own tests" gap: the
// replica receipt/tx are BUILT FROM the golden JSON, driven through FISCO's production
// serializer (combineReceiptResponse), and compared field-by-field — field names
// (spelling included), values, and the full field-set both directions.
//
// Captured cases:
//   user-deposit                        OptimismPortal.depositTransaction(isCreation=false),
//                                       first deposit of that sender → depositNonce 0x0
//   user-deposit-creation               isCreation=true → to=null + contractAddress set,
//                                       depositNonce 0x1
//   attributes-deposit-canyon-onward    per-block L1 attributes deposit (post-Canyon):
//                                       depositNonce + depositReceiptVersion 0x1
//   attributes-deposit-pre-canyon       pre-Canyon: depositReceiptVersion ABSENT
//
// op-geth emission conditions (internal/ethapi/api.go MarshalReceipt + core/state_processor.go
// MakeReceipt), confirmed at the pinned commit:
//   - depositNonce: every deposit receipt post-Regolith, = the executing sender account's
//     EVM nonce at execution time (statedb.GetNonce(msg.From)) — NOT a global deposit
//     counter; MarshalReceipt gates on `receipt.DepositNonce != nil`.
//   - depositReceiptVersion: additionally from Canyon (CanyonDepositReceiptVersion = 1).
//   - l1GasPrice/l1Fee/... are NEVER emitted on deposit receipts (gated on !IsDepositTx).
//   - contractAddress: emitted iff a contract was created (non-zero address), else null.
//
// Pinned representation divergence (corpus DIVERGENCES.md §WI-E2, D1):
//   op-geth emits from/to/contractAddress as all-lowercase hex; FISCO emits EIP-55
//   checksummed. Same 20 bytes — tools comparing raw JSON (not parsing addresses) see a
//   difference. Compared case-insensitively here; the casing itself is pinned by literal
//   assertions in the concrete cases below. Do NOT "fix" FISCO to lowercase silently:
//   that changes the wire representation and must re-open the divergence entry.
// ---------------------------------------------------------------------------

#ifndef WEB3_RPC_OPGETH_DEPOSIT_GOLDEN_DIR
#define WEB3_RPC_OPGETH_DEPOSIT_GOLDEN_DIR ""
#endif

namespace
{
Json::Value wiE2LoadGoldenJson(std::string const& fileName)
{
    auto const path = std::string(WEB3_RPC_OPGETH_DEPOSIT_GOLDEN_DIR) + "/" + fileName;
    std::ifstream in(path);
    BOOST_REQUIRE_MESSAGE(in, "WI-E2 golden fixture not found: " << path);
    std::stringstream buffer;
    buffer << in.rdbuf();
    Json::Value root;
    Json::Reader reader;
    BOOST_REQUIRE_MESSAGE(
        reader.parse(buffer.str(), root), "WI-E2 golden JSON parse failed: " << path);
    return root;
}

std::string wiE2Without0x(std::string const& hex)
{
    return hex.starts_with("0x") ? hex.substr(2) : hex;
}

// Goldens carry 0x-prefixed quantities (op-geth hexutil). boost cpp_int parses the 0x
// prefix itself (same form already used by the deposit tx test above).
bcos::u256 wiE2GoldenU256(std::string const& hexQuantity)
{
    return bcos::u256(hexQuantity);
}

struct WiE2Replica
{
    std::shared_ptr<bcostars::protocol::TransactionImpl> tx;
    bcos::protocol::TransactionReceipt::Ptr receipt;
    bcos::crypto::HashType blockHash;
};

// Build the FISCO-side equivalent of a captured deposit purely from the golden JSON:
// the tx envelope via the production Web3Transaction → tars bridge, the receipt via the
// production receipt factory + OpStackReceiptMeta. Nothing is hard-coded per-case.
WiE2Replica wiE2BuildReplica(bcos::protocol::BlockFactory::Ptr const& blockFactory,
    Json::Value const& txGolden, Json::Value const& receiptGolden)
{
    bcos::rpc::Web3Transaction web3Tx;
    web3Tx.type = bcos::rpc::TransactionType::Deposit;
    web3Tx.from = bcos::Address(txGolden["from"].asString());
    if (txGolden["to"].isString())
    {
        web3Tx.to = bcos::Address(txGolden["to"].asString());
    }
    web3Tx.gasLimit = wiE2GoldenU256(txGolden["gas"].asString()).convert_to<unsigned long long>();
    web3Tx.value = wiE2GoldenU256(txGolden["value"].asString());
    web3Tx.data = bcos::fromHex(wiE2Without0x(txGolden["input"].asString()));
    // nonce is NOT part of the deposit RLP envelope (op-geth DepositTx has no nonce
    // field); the golden's `nonce` is the sender account nonce mirrored by the RPC.
    web3Tx.mint = wiE2GoldenU256(txGolden["mint"].asString());
    // Regolith+ deposits: isSystemTx false on all captured goldens (field absent).
    web3Tx.isSystemTx = false;
    web3Tx.sourceHash = bcos::crypto::HashType(wiE2Without0x(txGolden["sourceHash"].asString()));

    auto tarsTx = web3Tx.takeToTarsTransaction();
    // takeToTarsTransaction fills extraTransactionHash = keccak(full 0x7E envelope) itself;
    // the case below pins it against the golden transactionHash.
    auto tx = std::make_shared<bcostars::protocol::TransactionImpl>(
        [tarsTx = std::move(tarsTx)]() mutable { return &tarsTx; });

    std::vector<bcos::protocol::LogEntry> logs;
    auto receipt = blockFactory->receiptFactory()->createReceipt(
        wiE2GoldenU256(receiptGolden["gasUsed"].asString()),
        receiptGolden["contractAddress"].isString() ? receiptGolden["contractAddress"].asString() :
                                                      std::string(),
        logs,
        /*status=*/0,  // FISCO internal 0 == success → serializer emits 0x1 like the golden
        bcos::bytesConstRef{},
        static_cast<bcos::protocol::BlockNumber>(
            wiE2GoldenU256(receiptGolden["blockNumber"].asString()).convert_to<long long>()));
    receipt->setTransactionIndex(
        static_cast<size_t>(wiE2GoldenU256(receiptGolden["transactionIndex"].asString())
                                .convert_to<unsigned long long>()));
    // cumulativeGasUsed is a DECIMAL string on the FISCO side (TransactionReceipt.h note).
    receipt->setCumulativeGasUsed(
        wiE2GoldenU256(receiptGolden["cumulativeGasUsed"].asString()).str());
    // op-geth receipts always carry the 256-byte bloom (all-zero for these no-log
    // deposits); the tars receipt leaves it empty unless set explicitly.
    bcos::bytes zeroBloom(256, 0x00);
    receipt->setLogsBloom(bcos::ref(zeroBloom));

    bcos::protocol::OpStackReceiptMeta meta;
    if (receiptGolden.isMember("depositNonce"))
    {
        meta.deposit_nonce =
            wiE2GoldenU256(receiptGolden["depositNonce"].asString()).convert_to<uint64_t>();
    }
    if (receiptGolden.isMember("depositReceiptVersion"))
    {
        meta.deposit_receipt_version =
            wiE2GoldenU256(receiptGolden["depositReceiptVersion"].asString())
                .convert_to<uint64_t>();
    }
    receipt->setOpStackMeta(std::move(meta));

    return {.tx = std::move(tx),
        .receipt = std::move(receipt),
        .blockHash = bcos::crypto::HashType(wiE2Without0x(receiptGolden["blockHash"].asString()))};
}

// Full field-set diff (both directions) + per-field value comparison. Address fields are
// compared case-insensitively (pinned divergence D1, see block comment); every other
// field must match byte-for-byte. Any failure is a REAL representation-layer divergence
// — fix the code or re-capture the golden; never loosen this comparator.
void wiE2CompareGoldenReceipt(
    Json::Value const& golden, Json::Value const& actual, std::string const& label)
{
    std::vector<std::string> missingInFisco;
    std::vector<std::string> extraInFisco;
    for (auto const& key : golden.getMemberNames())
    {
        if (!actual.isMember(key))
        {
            missingInFisco.push_back(key);
        }
    }
    for (auto const& key : actual.getMemberNames())
    {
        if (!golden.isMember(key))
        {
            extraInFisco.push_back(key);
        }
    }
    std::string diffMessage = label + ": op-geth-only fields (FISCO does not emit):";
    for (auto const& key : missingInFisco)
    {
        diffMessage += " " + key;
    }
    diffMessage += "; FISCO-only fields (op-geth does not emit):";
    for (auto const& key : extraInFisco)
    {
        diffMessage += " " + key;
    }
    BOOST_CHECK_MESSAGE(
        missingInFisco.empty() && extraInFisco.empty(), "field-set divergence: " << diffMessage);

    for (auto const& key : golden.getMemberNames())
    {
        if (!actual.isMember(key))
        {
            continue;  // already reported above
        }
        if (key == "from" || key == "to" || key == "contractAddress")
        {
            BOOST_CHECK_MESSAGE(golden[key].isNull() == actual[key].isNull(),
                label << "." << key << ": nullness differs");
            if (!golden[key].isNull() && actual[key].isString())
            {
                BOOST_CHECK_MESSAGE(boost::algorithm::to_lower_copy(actual[key].asString()) ==
                                        boost::algorithm::to_lower_copy(golden[key].asString()),
                    label << "." << key << " address bytes differ: actual="
                          << actual[key].asString() << " golden=" << golden[key].asString());
            }
            continue;
        }
        if (golden[key].isString())
        {
            BOOST_CHECK_MESSAGE(actual[key].isString(),
                label << "." << key << ": type differs (golden string, actual " << actual[key]
                      << ")");
            BOOST_CHECK_EQUAL(actual[key].asString(), golden[key].asString());
        }
        else
        {
            BOOST_CHECK_MESSAGE(actual[key] == golden[key],
                label << "." << key << ": value differs (actual " << actual[key] << " golden "
                      << golden[key] << ")");
        }
    }
}
}  // namespace

// First captured user deposit (call-type): the canonical no-mint-surprise shape —
// depositNonce 0x0 must be EMITTED (op-geth emits the field for the nil-vs-zero
// distinction; a falsy presence check would drop it).
BOOST_AUTO_TEST_CASE(opgethGoldenUserDepositReceipt)
{
    auto const receiptGolden = wiE2LoadGoldenJson("user-deposit.receipt.json");
    auto const txGolden = wiE2LoadGoldenJson("user-deposit.tx.json");
    auto parts = wiE2BuildReplica(m_blockFactory, txGolden, receiptGolden);

    // Envelope equivalence pin: FISCO's deposit tx hash (keccak of the full 0x7E envelope
    // assembled from the same golden fields) must reproduce op-geth's transactionHash.
    BOOST_CHECK_EQUAL(parts.tx->hash().hexPrefixed(), receiptGolden["transactionHash"].asString());

    Json::Value result = Json::objectValue;
    combineReceiptResponse(result, *parts.receipt, *parts.tx, parts.blockHash);
    wiE2CompareGoldenReceipt(receiptGolden, result, "user-deposit");

    // D1 concrete pin: FISCO emits the EIP-55 CHECKSUMMED sender for op-geth's lowercase
    // 0xf39fd6e51aad88f6f4ce6ab8827279cfffb92266 (same 20 bytes).
    BOOST_CHECK_EQUAL(result["from"].asString(), "0xf39Fd6e51aad88F6F4ce6aB8827279cffFb92266");
    // The zero depositNonce survives serialization and renders as "0x0".
    BOOST_REQUIRE(result.isMember("depositNonce"));
    BOOST_CHECK_EQUAL(result["depositNonce"].asString(), "0x0");
}

// Creation-type user deposit (isCreation=true): the only capture with a non-null
// contractAddress, and to=null on both sides.
BOOST_AUTO_TEST_CASE(opgethGoldenUserDepositCreationReceipt)
{
    auto const receiptGolden = wiE2LoadGoldenJson("user-deposit-creation.receipt.json");
    auto const txGolden = wiE2LoadGoldenJson("user-deposit-creation.tx.json");
    auto parts = wiE2BuildReplica(m_blockFactory, txGolden, receiptGolden);

    BOOST_CHECK_EQUAL(parts.tx->hash().hexPrefixed(), receiptGolden["transactionHash"].asString());

    Json::Value result = Json::objectValue;
    combineReceiptResponse(result, *parts.receipt, *parts.tx, parts.blockHash);
    wiE2CompareGoldenReceipt(receiptGolden, result, "user-deposit-creation");

    BOOST_CHECK(result["to"].isNull());
    // D1 concrete pin for contractAddress: FISCO checksummed vs op-geth lowercase
    // 0xe7f1725e7734ce288f8367e1bb143e90bb3f0512.
    BOOST_CHECK_EQUAL(
        result["contractAddress"].asString(), "0xe7f1725E7734CE288F8367e1Bb143E90bb3F0512");
    BOOST_CHECK_EQUAL(result["depositNonce"].asString(), "0x1");
}

// Per-block L1 attributes deposit post-Canyon: both OP fields present; the real
// l1info calldata exercises the envelope round-trip beyond a minimal stub.
BOOST_AUTO_TEST_CASE(opgethGoldenAttributesDepositReceipt)
{
    auto const receiptGolden = wiE2LoadGoldenJson("attributes-deposit-canyon-onward.receipt.json");
    auto const txGolden = wiE2LoadGoldenJson("attributes-deposit-canyon-onward.tx.json");
    auto parts = wiE2BuildReplica(m_blockFactory, txGolden, receiptGolden);

    BOOST_CHECK_EQUAL(parts.tx->hash().hexPrefixed(), receiptGolden["transactionHash"].asString());

    Json::Value result = Json::objectValue;
    combineReceiptResponse(result, *parts.receipt, *parts.tx, parts.blockHash);
    wiE2CompareGoldenReceipt(receiptGolden, result, "attributes-deposit-canyon-onward");

    // depositNonce = L1 attributes depositor's account nonce (block 3000 → 0xbb9);
    // depositReceiptVersion = 1 from Canyon on.
    BOOST_CHECK_EQUAL(result["depositNonce"].asString(), "0xbb9");
    BOOST_CHECK_EQUAL(result["depositReceiptVersion"].asString(), "0x1");
}

// Pre-Canyon attributes deposit: op-geth OMITS depositReceiptVersion (field absent, not
// null) — FISCO must omit it too. Pins the absence semantics on both sides.
BOOST_AUTO_TEST_CASE(opgethGoldenAttributesDepositPreCanyonReceipt)
{
    auto const receiptGolden = wiE2LoadGoldenJson("attributes-deposit-pre-canyon.receipt.json");
    auto const txGolden = wiE2LoadGoldenJson("attributes-deposit-pre-canyon.tx.json");
    auto parts = wiE2BuildReplica(m_blockFactory, txGolden, receiptGolden);

    BOOST_CHECK_EQUAL(parts.tx->hash().hexPrefixed(), receiptGolden["transactionHash"].asString());

    // The fixture itself predates Canyon: no depositReceiptVersion anywhere.
    BOOST_CHECK(!receiptGolden.isMember("depositReceiptVersion"));
    BOOST_REQUIRE(parts.receipt->opStackMeta().has_value());
    BOOST_CHECK(!parts.receipt->opStackMeta()->deposit_receipt_version.has_value());

    Json::Value result = Json::objectValue;
    combineReceiptResponse(result, *parts.receipt, *parts.tx, parts.blockHash);
    wiE2CompareGoldenReceipt(receiptGolden, result, "attributes-deposit-pre-canyon");

    BOOST_CHECK(!result.isMember("depositReceiptVersion"));
    BOOST_CHECK_EQUAL(result["depositNonce"].asString(), "0x63");
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
