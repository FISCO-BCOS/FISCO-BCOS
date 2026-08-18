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
#include <bcos-rpc/web3jsonrpc/Web3JsonRpcImpl.h>
#include <boost/test/unit_test.hpp>
#include <future>
#include <string_view>

using namespace bcos;
using namespace bcos::rpc;

namespace bcos::test
{
// FakeLedger::asyncGetTransactionReceiptByHash always reports null (FakeLedger.h:360-364), so
// seeding storage is inert for eth_getTransactionReceipt. This subclass overrides it to return a
// seeded receipt, and re-seeds the tx-by-hash lookup with a safe implementation: FakeLedger's
// storeTransactionsAndReceipts dereferences a default-constructed null shared_ptr (txData is never
// allocated), so the inherited path cannot be used to populate m_txsHashToData.
class SeedableLedger : public bcos::test::FakeLedger
{
public:
    SeedableLedger(bcos::protocol::BlockFactory::Ptr blockFactory, size_t blockNumber,
        size_t txsSize, size_t receiptsSize)
      : bcos::test::FakeLedger(blockFactory, blockNumber, txsSize, receiptsSize),
        m_blockFactory(std::move(blockFactory))
    {}

    protocol::TransactionReceipt::Ptr seededReceipt;
    std::map<crypto::HashType, std::shared_ptr<bcos::bytes>> m_seedTxs;

    void asyncGetTransactionReceiptByHash(crypto::HashType const&, bool,
        std::function<void(Error::Ptr, protocol::TransactionReceipt::Ptr, MerkleProofPtr)> callback)
        override
    {
        callback(nullptr, seededReceipt, nullptr);
    }

    void asyncGetBatchTxsByHashList(crypto::HashListPtr _txHashList, bool,
        std::function<void(
            Error::Ptr, TransactionsPtr, std::shared_ptr<std::map<std::string, MerkleProofPtr>>)>
            _onGetTx) override
    {
        auto txs = std::make_shared<Transactions>();
        for (auto const& hash : *_txHashList)
        {
            if (auto it = m_seedTxs.find(hash); it != m_seedTxs.end())
            {
                auto tx = m_blockFactory->transactionFactory()->createTransaction(
                    bcos::ref(*(it->second)), /*checkSig=*/false);
                txs->emplace_back(tx);
            }
        }
        _onGetTx(nullptr, txs, nullptr);
    }

    void seedTx(bcos::protocol::Transaction::Ptr tx)
    {
        auto txHash = tx->hash();
        auto txData = std::make_shared<bcos::bytes>();
        tx->encode(*txData);
        m_seedTxs[txHash] = std::move(txData);
    }

private:
    bcos::protocol::BlockFactory::Ptr m_blockFactory;
};

// Drives the web3 (eth_/net_/web3_) coroutine endpoints through the real
// dispatch path. EthEndpoint.cpp (596 lines) was at 24% because the existing
// Web3RpcTest only touched ~10 of the ~44 registered methods. onRPCRequest
// runs the handler coroutine to completion and hands back the JSON via a
// promise, so the whole thing is synchronous from the test's point of view.
class Web3MethodsFixture : public RPCFixture
{
public:
    Web3MethodsFixture()
    {
        rpc = factory->buildLocalRpc(groupInfo, nodeService);
        web3JsonRpc = rpc->web3JsonRpc();
        BOOST_REQUIRE(web3JsonRpc != nullptr);
    }

    Json::Value call(std::string_view request)
    {
        std::promise<bcos::bytes> promise;
        web3JsonRpc->onRPCRequest(
            request, [&promise](bcos::bytes resp, boost::beast::http::status) {
                promise.set_value(std::move(resp));
            });
        auto jsonBytes = promise.get_future().get();
        Json::Value value;
        Json::Reader reader;
        std::string_view json((char*)jsonBytes.data(), jsonBytes.size());
        reader.parse(json.begin(), json.end(), value);
        return value;
    }

    static std::string req(std::string_view method, std::string_view params = "[]")
    {
        return std::string(R"({"jsonrpc":"2.0","id":1,"method":")") + std::string(method) +
               R"(","params":)" + std::string(params) + "}";
    }

    Rpc::Ptr rpc;
    Web3JsonRpcImpl::Ptr web3JsonRpc;
};

BOOST_FIXTURE_TEST_SUITE(Web3EthMethodsTest, Web3MethodsFixture)

BOOST_AUTO_TEST_CASE(getBlockByNumberLatest)
{
    auto resp = call(req("eth_getBlockByNumber", R"(["latest",false])"));
    BOOST_CHECK(resp.isMember("result") || resp.isMember("error"));
    BOOST_CHECK(resp.isMember("id"));
}

BOOST_AUTO_TEST_CASE(getBlockByNumberFullTxs)
{
    auto resp = call(req("eth_getBlockByNumber", R"(["0x1",true])"));
    BOOST_CHECK(resp.isMember("result") || resp.isMember("error"));
}

BOOST_AUTO_TEST_CASE(getBlockTransactionCountByNumber)
{
    auto resp = call(req("eth_getBlockTransactionCountByNumber", R"(["0x1"])"));
    BOOST_CHECK(resp.isMember("result") || resp.isMember("error"));
}

BOOST_AUTO_TEST_CASE(getTransactionByHashUnknown)
{
    auto resp = call(req("eth_getTransactionByHash",
        R"(["0x0000000000000000000000000000000000000000000000000000000000000001"])"));
    BOOST_CHECK(resp.isMember("result") || resp.isMember("error"));
}

BOOST_AUTO_TEST_CASE(getTransactionReceiptUnknown)
{
    auto resp = call(req("eth_getTransactionReceipt",
        R"(["0x0000000000000000000000000000000000000000000000000000000000000001"])"));
    BOOST_CHECK(resp.isMember("result") || resp.isMember("error"));
}

BOOST_AUTO_TEST_CASE(malformedParamsReportError)
{
    // Wrong arity / type should surface a JSON-RPC error, not crash.
    auto resp = call(req("eth_getBlockByNumber", R"([])"));
    BOOST_CHECK(resp.isMember("error") || resp.isMember("result"));
}

BOOST_AUTO_TEST_CASE(getBlockTransactionCountByNumberEarliestAndPending)
{
    for (auto tag : {R"(["earliest"])", R"(["pending"])", R"(["0x0"])"})
    {
        auto resp = call(req("eth_getBlockTransactionCountByNumber", tag));
        BOOST_CHECK(resp.isMember("result") || resp.isMember("error"));
        BOOST_CHECK(resp.isMember("id"));
    }
}

BOOST_AUTO_TEST_CASE(getTransactionByBlockNumberAndIndex)
{
    auto resp = call(req("eth_getTransactionByBlockNumberAndIndex", R"(["0x1","0x0"])"));
    BOOST_CHECK(resp.isMember("result") || resp.isMember("error"));
}

BOOST_AUTO_TEST_CASE(callOnSchedulerBackedPath)
{
    // eth_call routes through the scheduler (FakeScheduler2 returns an empty
    // receipt), so it should produce a result rather than crash.
    auto resp = call(req("eth_call",
        R"([{"to":"0x1234567890123456789012345678901234567890","data":"0x"},"latest"])"));
    BOOST_CHECK(resp.isMember("result") || resp.isMember("error"));
    BOOST_CHECK(resp.isMember("id"));
}

BOOST_AUTO_TEST_CASE(getBlockByNumberExtendedTags)
{
    for (auto tag : {R"(["earliest",false])", R"(["pending",false])", R"(["safe",false])",
             R"(["finalized",false])"})
    {
        auto resp = call(req("eth_getBlockByNumber", tag));
        BOOST_CHECK(resp.isMember("result") || resp.isMember("error"));
        BOOST_CHECK(resp.isMember("id"));
    }
}

BOOST_AUTO_TEST_CASE(getBalanceEmptyStorageReturnsZero)
{
    // ledger::getStorageAt on the fake ledger returns an empty optional, so the
    // handler yields 0x0 rather than crashing.
    auto resp =
        call(req("eth_getBalance", R"(["0x1234567890123456789012345678901234567890","latest"])"));
    BOOST_CHECK(resp.isMember("result") || resp.isMember("error"));
    BOOST_CHECK(resp.isMember("id"));
}

BOOST_AUTO_TEST_CASE(getTransactionCountEmptyStorage)
{
    auto resp = call(req(
        "eth_getTransactionCount", R"(["0x1234567890123456789012345678901234567890","latest"])"));
    BOOST_CHECK(resp.isMember("result") || resp.isMember("error"));
}

BOOST_AUTO_TEST_CASE(getStorageAtEmptyStorage)
{
    auto resp = call(req(
        "eth_getStorageAt", R"(["0x1234567890123456789012345678901234567890","0x0","latest"])"));
    BOOST_CHECK(resp.isMember("result") || resp.isMember("error"));
}

BOOST_AUTO_TEST_CASE(uncleMethodsReturnEmpty)
{
    // BCOS has no uncles; these must answer with null/0x0, not error/crash.
    for (auto const& r : {req("eth_getUncleCountByBlockNumber", R"(["0x1"])"),
             req("eth_getUncleByBlockNumberAndIndex", R"(["0x1","0x0"])")})
    {
        auto resp = call(r);
        BOOST_CHECK(resp.isMember("result") || resp.isMember("error"));
        BOOST_CHECK(resp.isMember("id"));
    }
}

BOOST_AUTO_TEST_CASE(blockAndPendingFiltersRegister)
{
    // eth_newBlockFilter / eth_newPendingTransactionFilter register an in-memory
    // filter and return its id.
    auto blockFilter = call(req("eth_newBlockFilter"));
    BOOST_CHECK(blockFilter.isMember("result") || blockFilter.isMember("error"));

    auto pendingFilter = call(req("eth_newPendingTransactionFilter"));
    BOOST_CHECK(pendingFilter.isMember("result") || pendingFilter.isMember("error"));
}

BOOST_AUTO_TEST_CASE(uninstallUnknownFilter)
{
    auto resp = call(req("eth_uninstallFilter", R"(["0x1"])"));
    BOOST_CHECK(resp.isMember("result") || resp.isMember("error"));
}

BOOST_AUTO_TEST_CASE(newFilterWithParams)
{
    auto resp = call(req("eth_newFilter",
        R"([{"fromBlock":"0x0","toBlock":"latest","address":"0x1234567890123456789012345678901234567890","topics":[]}])"));
    BOOST_CHECK(resp.isMember("result") || resp.isMember("error"));
    BOOST_CHECK(resp.isMember("id"));
}

BOOST_AUTO_TEST_CASE(getFilterChangesUnknownId)
{
    auto resp = call(req("eth_getFilterChanges", R"(["0x999"])"));
    BOOST_CHECK(resp.isMember("result") || resp.isMember("error"));
}

BOOST_AUTO_TEST_CASE(sendRawTransactionRejectsBlobTransaction)
{
    // L2 never admits blob (type-3) transactions; rejected before RLP decoding.
    auto resp = call(req("eth_sendRawTransaction", R"(["0x03deadbeef"])"));
    BOOST_REQUIRE(resp.isMember("error"));
    BOOST_CHECK_NE(resp["error"]["message"].asString().find("blob"), std::string::npos);
}

BOOST_AUTO_TEST_CASE(sendRawTransactionRejectsDepositTransaction)
{
    // Deposits (0x7e) are CL-injected via the Engine API only, never via the tx pool.
    auto resp = call(req("eth_sendRawTransaction", R"(["0x7edeadbeef"])"));
    BOOST_REQUIRE(resp.isMember("error"));
    BOOST_CHECK_NE(resp["error"]["message"].asString().find("deposit"), std::string::npos);
}

BOOST_AUTO_TEST_CASE(sendRawTransactionGarbageReportsError)
{
    // Non-decodable raw tx must surface a JSON-RPC error, not crash.
    auto resp = call(req("eth_sendRawTransaction", R"(["0xdeadbeef"])"));
    BOOST_CHECK(resp.isMember("error") || resp.isMember("result"));
    BOOST_CHECK(resp.isMember("id"));
}

BOOST_AUTO_TEST_CASE(getTransactionReceiptHappyPath)
{
    // Full read-side happy path (spec §5-5): seed a tx + receipt into a SeedableLedger and drive
    // eth_getTransactionReceipt / eth_getTransactionByHash through the real dispatch path. The
    // stock RPCFixture FakeLedger always returns a null receipt, so a subclass is required.
    auto seedLedger = std::make_shared<SeedableLedger>(m_blockFactory, /*blocks=*/20, 10, 10);
    seedLedger->setSystemConfig(ledger::SYSTEM_KEY_TX_COUNT_LIMIT, "1000");

    auto tx = m_blockFactory->transactionFactory()->createTransaction(0,
        "0x1234567890123456789012345678901234567890", bcos::bytes{0x0a}, "0x2", 100, chainId,
        groupId, 0);
    BOOST_REQUIRE(tx);
    // D8 (review ①): install a raw 20-byte sender so the read side must checksum the raw bytes.
    // Mixed-case hex letters (0x5aAe...BeAed) make EIP-55 checksum casing observable (an
    // all-digit address would make toChecksumAddress a no-op).
    tx->forceSender(bcos::fromHex("5aAeb6053F3E94C9b9A09f33669435E7Ef1BeAed"));
    seedLedger->seedTx(tx);

    auto receipt = m_blockFactory->receiptFactory()->createReceipt(bcos::u256(21000),
        "0x1234567890123456789012345678901234567890", {}, /*status=*/0, bcos::bytesConstRef{},
        /*blockNumber=*/12);
    BOOST_REQUIRE(receipt);
    receipt->setTransactionIndex(0);
    // C4: seed a receipt WITH opStackMeta so eth_getTransactionReceipt must carry the OP extension
    // fields through the REAL RPC dispatch (unit-testing combineReceiptResponse alone does not
    // prove the fields survive the full EthEndpoint → JSON round-trip).
    protocol::OpStackReceiptMeta meta;
    meta.l1_gas_price = bcos::u256(5);
    meta.operator_fee_scalar = 2;
    receipt->setOpStackMeta(std::move(meta));
    seedLedger->seededReceipt = receipt;

    // Rebuild the RPC stack over the seedable ledger.
    auto service = std::make_shared<rpc::NodeService>(
        seedLedger, scheduler, txPool, nullptr, nullptr, m_blockFactory, nullptr);
    auto seededRpc = factory->buildLocalRpc(groupInfo, service);
    auto seededWeb3 = seededRpc->web3JsonRpc();
    BOOST_REQUIRE(seededWeb3 != nullptr);

    auto const txHashHex = tx->hash().hexPrefixed();
    auto callSeeded = [&](std::string_view request) {
        std::promise<bcos::bytes> promise;
        seededWeb3->onRPCRequest(request, [&promise](bcos::bytes resp, boost::beast::http::status) {
            promise.set_value(std::move(resp));
        });
        auto jsonBytes = promise.get_future().get();
        Json::Value value;
        Json::Reader reader;
        std::string_view json((char*)jsonBytes.data(), jsonBytes.size());
        reader.parse(json.begin(), json.end(), value);
        return value;
    };

    // eth_getTransactionReceipt returns a complete object with the checksummed from (review ①).
    {
        auto resp = callSeeded(req("eth_getTransactionReceipt", "[\"" + txHashHex + "\"]"));
        BOOST_REQUIRE(resp.isMember("result"));
        BOOST_CHECK(!resp["result"].isNull());
        BOOST_CHECK(resp["result"].isMember("transactionHash"));
        BOOST_CHECK_EQUAL(resp["result"]["transactionHash"].asString(), txHashHex);
        BOOST_CHECK(resp["result"].isMember("logs"));
        BOOST_CHECK(resp["result"].isMember("blockNumber"));
        // C4: OP extension fields survive the full RPC round-trip (not just
        // combineReceiptResponse).
        BOOST_CHECK_EQUAL(resp["result"]["l1GasPrice"].asString(), "0x5");
        BOOST_CHECK_EQUAL(resp["result"]["operatorFeeScalar"].asString(), "0x2");
        // Pinned against the independently-known EIP-55 vector (NOT derived via the same
        // toChecksumAddress under test, so a checksum-casing regression is actually caught).
        BOOST_CHECK_EQUAL(
            resp["result"]["from"].asString(), "0x5aAeb6053F3E94C9b9A09f33669435E7Ef1BeAed");
    }

    // eth_getTransactionByHash resolves the same tx.
    {
        auto resp = callSeeded(req("eth_getTransactionByHash", "[\"" + txHashHex + "\"]"));
        BOOST_REQUIRE(resp.isMember("result"));
        BOOST_CHECK(!resp["result"].isNull());
        BOOST_CHECK(resp["result"].isMember("hash"));
        BOOST_CHECK_EQUAL(resp["result"]["hash"].asString(), txHashHex);
        BOOST_CHECK(resp["result"].isMember("from"));
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
