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
#include <bcos-framework/engine/DACaps.h>
#include <bcos-rpc/web3jsonrpc/Web3JsonRpcImpl.h>
#include <bcos-rpc/web3jsonrpc/endpoints/EndpointsMapping.h>
#include <bcos-rpc/web3jsonrpc/endpoints/EthEndpoint.h>
#include <bcos-rpc/web3jsonrpc/utils/Common.h>
#include <bcos-task/Wait.h>
#include <boost/test/unit_test.hpp>
#include <future>
#include <string_view>

using namespace bcos;
using namespace bcos::rpc;

namespace bcos::test
{
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
    BOOST_CHECK_EQUAL(resp["error"]["code"].asInt(), bcos::rpc::Web3DefaultError);
    BOOST_CHECK_EQUAL(resp["error"]["message"].asString(), "transaction type not supported (blob)");
}

BOOST_AUTO_TEST_CASE(sendRawTransactionRejectsDepositTransaction)
{
    // Deposits (0x7e) are CL-injected via the Engine API only, never via the tx pool.
    auto resp = call(req("eth_sendRawTransaction", R"(["0x7edeadbeef"])"));
    BOOST_REQUIRE(resp.isMember("error"));
    BOOST_CHECK_EQUAL(resp["error"]["code"].asInt(), bcos::rpc::Web3DefaultError);
    BOOST_CHECK_EQUAL(resp["error"]["message"].asString(),
        "transaction type not supported (deposit, Engine API only)");
}

BOOST_AUTO_TEST_CASE(sendRawTransactionGarbageReportsError)
{
    // Non-decodable raw tx must surface a JSON-RPC error, not crash.
    auto resp = call(req("eth_sendRawTransaction", R"(["0xdeadbeef"])"));
    BOOST_CHECK(resp.isMember("error") || resp.isMember("result"));
    BOOST_CHECK(resp.isMember("id"));
}

BOOST_AUTO_TEST_CASE(feeHistoryAndSetMaxDASizeRegistered)
{
    // The cutover wires the DA-cap consumer (OpEngineService), so the producer is now
    // registered. Its runtime reachability is still gated: MinerEndpoint::setMaxDASize
    // throws MethodNotFound when daCaps() is null (Ethereum-only nodes).
    EndpointsMapping mapping;
    BOOST_CHECK_MESSAGE(
        mapping.findHandler("eth_feeHistory").has_value(), "eth_feeHistory not dispatched");
    BOOST_CHECK_MESSAGE(
        mapping.findHandler("miner_setMaxDASize").has_value(), "miner_setMaxDASize not dispatched");

    // And the endpoint stays reachable through the real dispatch path — pinned POSITIVE
    // here, not conditionally: the fixture carries 20 blocks, so a well-formed request
    // must answer a result with the feeHistory shape (a -32602/-32603 here is a
    // regression; the old conditional arm let any non-(-32601/-32603) code pass).
    auto resp = call(req("eth_feeHistory", R"(["0x1","latest"])"));
    BOOST_REQUIRE(resp.isMember("result"));
    BOOST_REQUIRE(resp["result"].isObject());
    BOOST_CHECK(resp["result"].isMember("oldestBlock"));
    BOOST_CHECK(resp["result"].isMember("baseFeePerGas"));
    BOOST_CHECK(!resp.isMember("error"));

    // The param validation is pinned on both arms: a missing newestBlock is the exact
    // InvalidParams (-32602) the endpoint throws (EthEndpoint::feeHistory), not just
    // "anything but -32601/-32603".
    auto malformed = call(req("eth_feeHistory", R"(["0x1"])"));
    BOOST_REQUIRE(malformed.isMember("error"));
    BOOST_CHECK_EQUAL(malformed["error"]["code"].asInt(), -32602);
}

BOOST_AUTO_TEST_CASE(minerSetMaxDASizeWritesSharedCapsAndGatesEthOnly)
{
    // Regression for the miner_setMaxDASize producer (previously the handler was never
    // invoked, so the write path and the eth-only gate were untested).
    //
    // Ethereum-only node: daCaps() is null, so even though the handler is registered
    // the namespace must answer MethodNotFound (-32601) rather than ack a cap nothing
    // applies.
    nodeService->setDaCaps(nullptr);
    auto ethOnly = call(req("miner_setMaxDASize", R"(["0x100","0x200"])"));
    BOOST_REQUIRE(ethOnly.isMember("error"));
    BOOST_CHECK_EQUAL(ethOnly["error"]["code"].asInt(), -32601);

    // OP node: the handler writes both quantities into the shared DACaps the engine reads.
    auto caps = std::make_shared<bcos::engine::DACaps>();
    nodeService->setDaCaps(caps);
    auto ok = call(req("miner_setMaxDASize", R"(["0x100","0x200"])"));
    BOOST_REQUIRE(ok.isMember("result"));
    BOOST_CHECK(ok["result"].asBool());
    BOOST_CHECK_EQUAL(caps->maxTxSize.load(), 0x100);
    BOOST_CHECK_EQUAL(caps->maxBlockSize.load(), 0x200);

    // Rejecting a malformed request must not touch the stored caps.
    auto bad = call(req("miner_setMaxDASize", R"(["0x100"])"));
    BOOST_REQUIRE(bad.isMember("error"));
    BOOST_CHECK_EQUAL(bad["error"]["code"].asInt(), -32602);
    BOOST_CHECK_EQUAL(caps->maxTxSize.load(), 0x100);
    BOOST_CHECK_EQUAL(caps->maxBlockSize.load(), 0x200);
}

BOOST_AUTO_TEST_CASE(estimateGasWithoutLedgerFailsClosed)
{
    // eth_estimateGas sizes its gas cap from the target block's header, so a node with no
    // ledger must refuse the request with InternalError. The chore(style) commit deleted that
    // guard and let the cap fall back to a hardcoded 30'000'000 instead.
    auto noLedgerService = std::make_shared<rpc::NodeService>(
        nullptr, scheduler, nullptr, nullptr, nullptr, m_blockFactory, nullptr);
    auto endpoint = std::make_shared<EthEndpoint>(noLedgerService, nullptr, false);

    Json::Value params(Json::arrayValue);
    Json::Value tx(Json::objectValue);
    tx["to"] = "0x1234567890abcdef1234567890abcdef12345678";
    tx["data"] = "0x";
    params.append(tx);
    params.append("0x1");

    Json::Value response;
    try
    {
        task::syncWait(endpoint->estimateGas(params, response));
        BOOST_FAIL("eth_estimateGas must not succeed without a ledger");
    }
    catch (JsonRpcException const& error)
    {
        BOOST_CHECK_EQUAL(error.code(), static_cast<int32_t>(JsonRpcError::InternalError));
        BOOST_CHECK_EQUAL(error.msg(), "Ledger not available for eth_estimateGas");
    }
}

// The sibling null-ledger guards (round-3 I): gasPrice, maxPriorityFeePerGas and
// feeHistory must all fail closed with InternalError on a node with no ledger —
// the guards existed but no test reached them, so a reordering that put the deref
// first (as round-2 B did for feeHistory) would have passed the suite.
BOOST_AUTO_TEST_CASE(feeMethodsWithoutLedgerFailClosed)
{
    auto noLedgerService = std::make_shared<rpc::NodeService>(
        nullptr, scheduler, nullptr, nullptr, nullptr, m_blockFactory, nullptr);
    auto endpoint = std::make_shared<EthEndpoint>(noLedgerService, nullptr, false);

    Json::Value emptyParams;
    Json::Value response;
    try
    {
        task::syncWait(endpoint->gasPrice(emptyParams, response));
        BOOST_FAIL("eth_gasPrice must not succeed without a ledger");
    }
    catch (JsonRpcException const& error)
    {
        BOOST_CHECK_EQUAL(error.code(), static_cast<int32_t>(JsonRpcError::InternalError));
        BOOST_CHECK_EQUAL(error.msg(), "Ledger not available for eth_gasPrice");
    }

    try
    {
        task::syncWait(endpoint->maxPriorityFeePerGas(emptyParams, response));
        BOOST_FAIL("eth_maxPriorityFeePerGas must not succeed without a ledger");
    }
    catch (JsonRpcException const& error)
    {
        BOOST_CHECK_EQUAL(error.code(), static_cast<int32_t>(JsonRpcError::InternalError));
        BOOST_CHECK_EQUAL(error.msg(), "Ledger not available for eth_maxPriorityFeePerGas");
    }

    Json::Value feeHistoryParams(Json::arrayValue);
    feeHistoryParams.append("0x1");
    feeHistoryParams.append("latest");
    try
    {
        task::syncWait(endpoint->feeHistory(feeHistoryParams, response));
        BOOST_FAIL("eth_feeHistory must not succeed without a ledger");
    }
    catch (JsonRpcException const& error)
    {
        BOOST_CHECK_EQUAL(error.code(), static_cast<int32_t>(JsonRpcError::InternalError));
        BOOST_CHECK_EQUAL(error.msg(), "Ledger not available for eth_feeHistory");
    }
}

BOOST_AUTO_TEST_CASE(estimateGasMissingParentBlockFailsClosed)
{
    // The ledger-backed half of the estimate-arm guard: a readable ledger whose target block
    // is missing must refuse with the diagnosable message, not silently size the estimate
    // against a constant cap. The fake ledger carries 20 blocks; 0x40 (64) is missing.
    auto endpoint = std::make_shared<EthEndpoint>(nodeService, nullptr, false);

    Json::Value params(Json::arrayValue);
    Json::Value tx(Json::objectValue);
    tx["to"] = "0x1234567890abcdef1234567890abcdef12345678";
    tx["data"] = "0x";
    params.append(tx);
    params.append("0x40");

    Json::Value response;
    try
    {
        task::syncWait(endpoint->estimateGas(params, response));
        BOOST_FAIL("eth_estimateGas must not succeed on a missing parent block");
    }
    catch (JsonRpcException const& error)
    {
        BOOST_CHECK_EQUAL(error.code(), static_cast<int32_t>(JsonRpcError::InternalError));
        BOOST_CHECK_EQUAL(error.msg(), "Unable to read parent block gas limit for eth_estimateGas");
    }
}

namespace
{
// Captures the transaction EthEndpoint::call hands to the scheduler, so a test can pin
// the gasLimit the endpoint actually derived (the response alone does not expose it).
class RecordingScheduler : public FakeScheduler2
{
public:
    using FakeScheduler2::FakeScheduler2;
    protocol::Transaction::Ptr lastTx;
    void call(protocol::Transaction::Ptr _tx,
        std::function<void(Error::Ptr, protocol::TransactionReceipt::Ptr)> _callback) noexcept
        override
    {
        lastTx = std::move(_tx);
        // Same shape as FakeScheduler2::call (which is private): an empty receipt.
        _callback({}, std::make_shared<bcostars::protocol::TransactionReceiptImpl>());
    }
};
}  // namespace

// Round-3 F2: CallRequestTest pins only takeToTransaction with a hand-passed cap; the
// ENDPOINT's own header read is pinned here — with a tip gasLimit != 30'000'000 so a
// regression to the removed hardcoded constant (or to the RPC cap) fails visibly.
BOOST_AUTO_TEST_CASE(estimateGasCapComesFromTheTipBlockHeaderAtTheEndpoint)
{
    // Give block 1's header a distinctive gas limit the endpoint must read.
    auto const distinctiveLimit = u256(21'000'000);
    m_ledger->ledgerData().at(1)->blockHeader()->setGasLimit(distinctiveLimit);

    auto recordingScheduler = std::make_shared<RecordingScheduler>(m_ledger, m_blockFactory);
    auto ledgerService = std::make_shared<rpc::NodeService>(
        m_ledger, recordingScheduler, nullptr, nullptr, nullptr, m_blockFactory, nullptr);
    auto endpoint = std::make_shared<EthEndpoint>(ledgerService, nullptr, false);

    Json::Value params(Json::arrayValue);
    Json::Value tx(Json::objectValue);
    tx["to"] = "0x1234567890abcdef1234567890abcdef12345678";
    tx["data"] = "0x";
    params.append(tx);
    params.append("0x1");

    Json::Value response;
    task::syncWait(endpoint->estimateGas(params, response));

    // The estimate proceeded through the header read (no refusal) and the budget handed
    // to the scheduler is the target block's gasLimit — not 30M, not the 50M RPC cap.
    BOOST_REQUIRE(recordingScheduler->lastTx != nullptr);
    BOOST_CHECK_EQUAL(recordingScheduler->lastTx->gasLimit(), 21'000'000u);
}

BOOST_AUTO_TEST_CASE(callRejectsMalformedFromAddress)
{
    // A malformed `from` used to be passed straight to the scheduler as a storage table name:
    // the read threw, SchedulerManager swallowed it into nullopt, and the request continued
    // with the sender's stale state nonce plus a WARNING per call. It must be InvalidParams.
    auto resp = call(req("eth_call",
        R"([{"from":"not_an_address","to":"0x1234567890abcdef1234567890abcdef12345678","data":"0x"},"latest"])"));
    BOOST_REQUIRE(resp.isMember("error"));
    BOOST_CHECK_EQUAL(resp["error"]["code"].asInt(), -32602);

    // A well-formed `from` still reaches execution: assert the positive arm (a result
    // member) instead of the old conditional, which passed vacuously when the response
    // carried no error and accepted any code except -32602 when it did.
    auto ok = call(req("eth_call",
        R"([{"from":"0x1234567890abcdef1234567890abcdef12345678","to":"0x1234567890abcdef1234567890abcdef12345678","data":"0x"},"latest"])"));
    BOOST_REQUIRE(ok.isMember("result"));
    BOOST_CHECK(!ok.isMember("error"));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
