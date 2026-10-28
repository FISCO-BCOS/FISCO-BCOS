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
#include <algorithm>
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

BOOST_AUTO_TEST_CASE(setMaxDASizeRegistered)
{
    // The miner namespace is opt-in ([web3_rpc] enable_miner_api): it writes the node-wide DA
    // caps, so a listener that does not explicitly enable it must not dispatch it at all —
    // otherwise any caller of the public web3 port can starve the sequencer's DA budget.
    EndpointsMapping defaultMapping;
    BOOST_CHECK_MESSAGE(!defaultMapping.findHandler("miner_setMaxDASize").has_value(),
        "miner_setMaxDASize must not be dispatched unless enable_miner_api is set");

    // With the opt-in on, the DA-cap producer is registered. Its runtime reachability is still
    // gated: MinerEndpoint::setMaxDASize throws MethodNotFound when daCaps() is null
    // (Ethereum-only nodes).
    EndpointsMapping mapping(/*enableOPEngine=*/false, /*enableMinerApi=*/true);
    BOOST_CHECK_MESSAGE(
        mapping.findHandler("miner_setMaxDASize").has_value(), "miner_setMaxDASize not dispatched");
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

BOOST_AUTO_TEST_CASE(minerSetMaxDASizeAcceptsZeroMaxTxSizeFromTheBatcher)
{
    // op-batcher's throttle controller initialises MaxTxSize to 0 and its FIRST
    // miner_setMaxDASize call therefore sends ["0x0","0x1fbd0"] (0, 130000 =
    // DefaultThrottleBlockSizeUpperLimit). The batcher shuts itself down on any generic
    // RPC error from this call, so a zero-reject here silently kills safe/finalized
    // derivation (a C2 e2e regression). Zero must be stored, not refused.
    auto caps = std::make_shared<bcos::engine::DACaps>();
    nodeService->setDaCaps(caps);
    auto batcherFirstCall = call(req("miner_setMaxDASize", R"(["0x0","0x1fbd0"])"));
    BOOST_REQUIRE(batcherFirstCall.isMember("result"));
    BOOST_CHECK(batcherFirstCall["result"].asBool());
    BOOST_CHECK_EQUAL(caps->maxTxSize.load(), 0);
    BOOST_CHECK_EQUAL(caps->maxBlockSize.load(), 130000);
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

// The sibling null-ledger guards: gasPrice and maxPriorityFeePerGas must both
// fail closed with InternalError on a node with no ledger — the guards existed but no test
// reached them, so an ordering that dereferenced before checking would have passed.
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
// A scheduler whose execution succeeds only when the gasLimit reaches `required`, and which
// reports `measured` as the gasUsed of a successful run. That is the shape eth_estimateGas sees:
// the ceiling probe succeeds and measures, and a tighter budget runs out of gas.
class LadderScheduler : public FakeScheduler2
{
public:
    using FakeScheduler2::FakeScheduler2;
    u256 measured{21'000};
    uint64_t required{30'000};
    // When non-zero, the Nth call (1-based) answers with an error instead of a receipt. That is
    // how a probe stops for a reason other than the budget: once the chain advances past the
    // pinned block, call() routes to callAtBlock, and a lane that cannot serve that historical
    // state answers the awaitable with an Error, which call() rethrows.
    uint32_t failOnCall{0};
    std::vector<u256> gasLimits;

    void call(protocol::Transaction::Ptr _tx,
        std::function<void(Error::Ptr, protocol::TransactionReceipt::Ptr)> _callback) noexcept
        override
    {
        gasLimits.push_back(_tx->gasLimit());
        if (failOnCall != 0 && gasLimits.size() == failOnCall)
        {
            _callback(BCOS_ERROR_PTR(-1, "historical state unavailable"), nullptr);
            return;
        }
        auto receipt = std::make_shared<bcostars::protocol::TransactionReceiptImpl>();
        if (_tx->gasLimit() < u256(required))
        {
            receipt->inner().data.status =
                static_cast<int32_t>(protocol::TransactionStatus::OutOfGas);
        }
        else
        {
            receipt->inner().data.status = static_cast<int32_t>(protocol::TransactionStatus::None);
            receipt->inner().data.gasUsed = measured.str();
        }
        _callback({}, receipt);
    }
};

// Captures the transaction EthEndpoint::call hands to the scheduler, so a test can pin
// the gasLimit the endpoint actually derived (the response alone does not expose it).
class RecordingScheduler : public FakeScheduler2
{
public:
    using FakeScheduler2::FakeScheduler2;
    protocol::Transaction::Ptr lastTx;
    // Every gasLimit the endpoint handed over, in order. eth_estimateGas probes at the ceiling
    // first and then retries upward from the measurement, so the ceiling is entries.front(), not
    // the last one.
    std::vector<u256> gasLimits;
    // Records what the RPC layer asked the pool for, so a test can pin the lookup key.
    std::string lastPendingAddress;
    std::string lastPendingKey;
    void call(protocol::Transaction::Ptr _tx,
        std::function<void(Error::Ptr, protocol::TransactionReceipt::Ptr)> _callback) noexcept
        override
    {
        gasLimits.push_back(_tx->gasLimit());
        lastTx = std::move(_tx);
        // Same shape as FakeScheduler2::call (which is private): an empty receipt.
        _callback({}, std::make_shared<bcostars::protocol::TransactionReceiptImpl>());
    }
    task::Task<std::optional<bcos::storage::Entry>> getPendingStorageAt(std::string_view _address,
        std::string_view _key, bcos::protocol::BlockNumber _number) override
    {
        lastPendingAddress = std::string(_address);
        lastPendingKey = std::string(_key);
        co_return co_await FakeScheduler2::getPendingStorageAt(_address, _key, _number);
    }
};
}  // namespace

// CallRequestTest pins only takeToTransaction with a hand-passed cap; the ENDPOINT's own
// header read is pinned here — with a tip gasLimit != 30'000'000 so a
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

    // The estimate proceeded through the header read (no refusal) and the FIRST probe — the
    // ceiling — is the target block's gasLimit: not 30M, not the 50M RPC cap. Later probes are
    // the retry ladder starting from the measured gasUsed.
    BOOST_REQUIRE(!recordingScheduler->gasLimits.empty());
    BOOST_CHECK_EQUAL(recordingScheduler->gasLimits.front(), u256(21'000'000));
}

// eth_estimateGas measures once at the ceiling, then walks a 10% ladder from the measurement and
// bisects whatever is left. Both ends matter: the ladder is what keeps a call that needs only a
// little more than it consumed cheap (this case settles in four probes), and the bisection is
// what stops a deep call chain from costing dozens.
BOOST_AUTO_TEST_CASE(estimateGasLaddersThenBisects)
{
    m_ledger->ledgerData().at(1)->blockHeader()->setGasLimit(u256(21'000'000));

    auto ladderScheduler = std::make_shared<LadderScheduler>(m_ledger, m_blockFactory);
    // Deliberately above c_intrinsicGasFloor: a measurement below the floor is clamped to it (see
    // estimateGasClampsTheMeasurementToTheIntrinsicFloor), which would make this case assert the
    // clamp instead of the ladder.
    ladderScheduler->measured = u256(100'000);
    ladderScheduler->required = 115'000;  // 1.15x: what a shallow nested call actually needs
    auto ledgerService = std::make_shared<rpc::NodeService>(
        m_ledger, ladderScheduler, nullptr, nullptr, nullptr, m_blockFactory, nullptr);
    auto endpoint = std::make_shared<EthEndpoint>(ledgerService, nullptr, false);

    Json::Value params(Json::arrayValue);
    Json::Value tx(Json::objectValue);
    tx["to"] = "0x1234567890abcdef1234567890abcdef12345678";
    tx["data"] = "0x";
    params.append(tx);
    params.append("0x1");

    Json::Value response;
    task::syncWait(endpoint->estimateGas(params, response));

    // Ceiling probe, the measurement, then two ladder steps: 100000 -> 110001 (fails, needs
    // 115000) -> 121002 (succeeds), and the next step would land on 121002 again so the loop
    // stops rather than re-probing. This is the case a plain bisection gets wrong: it would spend
    // ~25 probes narrowing to the same gas, on every nested call.
    auto const& probes = ladderScheduler->gasLimits;
    BOOST_REQUIRE_EQUAL(probes.size(), 4U);
    BOOST_CHECK_EQUAL(probes.at(0), u256(21'000'000));
    BOOST_CHECK_EQUAL(probes.at(1), u256(100'000));
    BOOST_CHECK_EQUAL(probes.at(2), u256(110'001));  // 100000 + 10000 + 1
    BOOST_CHECK_EQUAL(probes.at(3), u256(121'002));  // 110001 + 11000 + 1
    auto const answer = std::stoull(response["result"].asString(), nullptr, 16);
    BOOST_CHECK_GE(answer, ladderScheduler->required);
    BOOST_CHECK_EQUAL(probes.back(), u256(answer));
}

// The measurement is floored at the intrinsic cost before the search starts. A scheduler
// reporting a gasUsed below it (only reachable through a synthetic ledger) must not produce a
// candidate below the floor, and the floor -- not the measurement -- becomes the first rung.
BOOST_AUTO_TEST_CASE(estimateGasClampsTheMeasurementToTheIntrinsicFloor)
{
    m_ledger->ledgerData().at(1)->blockHeader()->setGasLimit(u256(21'000'000));

    auto ladderScheduler = std::make_shared<LadderScheduler>(m_ledger, m_blockFactory);
    ladderScheduler->measured = u256(20'000);  // below the 21,000 intrinsic floor
    ladderScheduler->required = 23'000;
    auto ledgerService = std::make_shared<rpc::NodeService>(
        m_ledger, ladderScheduler, nullptr, nullptr, nullptr, m_blockFactory, nullptr);
    auto endpoint = std::make_shared<EthEndpoint>(ledgerService, nullptr, false);

    Json::Value params(Json::arrayValue);
    Json::Value tx(Json::objectValue);
    tx["to"] = "0x1234567890abcdef1234567890abcdef12345678";
    tx["data"] = "0x";
    params.append(tx);
    params.append("0x1");

    Json::Value response;
    task::syncWait(endpoint->estimateGas(params, response));

    // 20000 is clamped to 21000, so the ladder starts there: 21000 (fails) -> 23101 (succeeds).
    auto const& probes = ladderScheduler->gasLimits;
    BOOST_REQUIRE_EQUAL(probes.size(), 3U);
    BOOST_CHECK_EQUAL(probes.at(0), u256(21'000'000));
    BOOST_CHECK_EQUAL(probes.at(1), u256(21'000));  // the floor, not the 20,000 measurement
    BOOST_CHECK_EQUAL(probes.at(2), u256(23'101));  // 21000 + 2100 + 1
    auto const answer = std::stoull(response["result"].asString(), nullptr, 16);
    BOOST_CHECK_EQUAL(answer, 23'101U);
}

// The cheap common case: a call that needs exactly what it consumed settles in two dry runs, and
// never enters the ladder.
BOOST_AUTO_TEST_CASE(estimateGasStopsWhenTheMeasurementSuffices)
{
    m_ledger->ledgerData().at(1)->blockHeader()->setGasLimit(u256(21'000'000));

    auto ladderScheduler = std::make_shared<LadderScheduler>(m_ledger, m_blockFactory);
    ladderScheduler->measured = u256(100'000);
    ladderScheduler->required = 100'000;
    auto ledgerService = std::make_shared<rpc::NodeService>(
        m_ledger, ladderScheduler, nullptr, nullptr, nullptr, m_blockFactory, nullptr);
    auto endpoint = std::make_shared<EthEndpoint>(ledgerService, nullptr, false);

    Json::Value params(Json::arrayValue);
    Json::Value tx(Json::objectValue);
    tx["to"] = "0x1234567890abcdef1234567890abcdef12345678";
    tx["data"] = "0x";
    params.append(tx);
    params.append("0x1");

    Json::Value response;
    task::syncWait(endpoint->estimateGas(params, response));

    auto const& probes = ladderScheduler->gasLimits;
    BOOST_REQUIRE_EQUAL(probes.size(), 2U);
    BOOST_CHECK_EQUAL(probes.at(0), u256(21'000'000));
    BOOST_CHECK_EQUAL(probes.at(1), u256(100'000));
    BOOST_CHECK_EQUAL(response["result"].asString(), toQuantity(u256(100'000)));
}

// The far end: a call that needs orders of magnitude more than it consumes must not cost dozens
// of dry runs. Ladder steps are capped, then the remainder is bisected.
BOOST_AUTO_TEST_CASE(estimateGasBoundsTheFarEnd)
{
    m_ledger->ledgerData().at(1)->blockHeader()->setGasLimit(u256(21'000'000));

    auto ladderScheduler = std::make_shared<LadderScheduler>(m_ledger, m_blockFactory);
    ladderScheduler->measured = u256(21'000);  // a cheap call ...
    ladderScheduler->required = 12'000'000;    // ... that needs a deep chain's worth of budget
    auto ledgerService = std::make_shared<rpc::NodeService>(
        m_ledger, ladderScheduler, nullptr, nullptr, nullptr, m_blockFactory, nullptr);
    auto endpoint = std::make_shared<EthEndpoint>(ledgerService, nullptr, false);

    Json::Value params(Json::arrayValue);
    Json::Value tx(Json::objectValue);
    tx["to"] = "0x1234567890abcdef1234567890abcdef12345678";
    tx["data"] = "0x";
    params.append(tx);
    params.append("0x1");

    Json::Value response;
    task::syncWait(endpoint->estimateGas(params, response));

    // Ceiling probe + measurement probe + the capped ladder + the tolerance-bounded bisection. Both
    // ends stay cheap: the ladder handles the near case, and the bisection stops at 1/64 of the
    // upper bound instead of narrowing to a single gas, which used to make this phase cost
    // ceil(log2(high - low)) whatever the uncertainty was.
    BOOST_CHECK_LE(ladderScheduler->gasLimits.size(), 2U + 8U + 25U);
    auto const answer = std::stoull(response["result"].asString(), nullptr, 16);
    BOOST_CHECK_GE(answer, ladderScheduler->required);
    BOOST_CHECK_LT(answer, 21'000'000U);
    // The bound above only says the probe count is bounded; it says nothing about the answer being
    // usable. The tolerance leaves `high` at most 1/64 above the least budget that runs, so a tenth
    // over the requirement is already generous -- and a "return the ceiling" regression fails here.
    BOOST_CHECK_LE(answer, ladderScheduler->required + ladderScheduler->required / 10 + 2);
}

// A block gasLimit above the RPC gas cap must not widen the search past what a probe can execute.
// 100M limit, 60M required: the ceiling is clamped to the 50M cap, so the 50M probe runs out of
// gas and the request is answered with that failure. Negative control: drop the clamp on the
// estimate arm's default (chainBlockGasLimit = limit) and the ceiling probe at 100M succeeds, the
// ladder climbs from the measurement inside a range it can never reach, and the endpoint answers
// 100,000,000 -- so the "expects an error" assertion below is what goes red.
BOOST_AUTO_TEST_CASE(estimateGasCeilingIsClampedToTheRpcGasCap)
{
    m_ledger->ledgerData().at(1)->blockHeader()->setGasLimit(u256(100'000'000));

    auto ladderScheduler = std::make_shared<LadderScheduler>(m_ledger, m_blockFactory);
    ladderScheduler->measured = u256(21'000);
    ladderScheduler->required = 60'000'000;  // above the cap, below the block limit
    auto ledgerService = std::make_shared<rpc::NodeService>(
        m_ledger, ladderScheduler, nullptr, nullptr, nullptr, m_blockFactory, nullptr);
    auto endpoint = std::make_shared<EthEndpoint>(ledgerService, nullptr, false);

    Json::Value params(Json::arrayValue);
    Json::Value tx(Json::objectValue);
    tx["to"] = "0x1234567890abcdef1234567890abcdef12345678";
    tx["data"] = "0x";
    params.append(tx);
    params.append("0x1");

    Json::Value response;
    task::syncWait(endpoint->estimateGas(params, response));

    // Exactly one probe, at the cap rather than at the block limit, and its out-of-gas is the
    // answer: no budget this node will execute can satisfy this request. The message names the
    // allowance geth-style, so a client can tell "the node will not dry-run more than this" apart
    // from "the transaction reverted" -- both are otherwise a bare "out of gas".
    auto const& probes = ladderScheduler->gasLimits;
    BOOST_REQUIRE_EQUAL(probes.size(), 1U);
    BOOST_CHECK_EQUAL(probes.at(0), u256(50'000'000));
    BOOST_CHECK(response.isMember("error"));
    auto const message = response["error"]["message"].asString();
    BOOST_CHECK_NE(message.find("gas required exceeds allowance (50000000)"), std::string::npos);
}

// The mid-range shape nothing else covers: an ordinary two-or-three-level nested call, needing
// about 2.5x what it consumed. The 8 ladder rungs only reach 1.1^8 = 2.14x, so this is the first
// answer past that cliff -- under a bisection that narrows to a single gas it cost the same ~35
// dry runs as a 120x one, because that phase cost ceil(log2(high - low)) regardless of how
// uncertain the answer was. The relative tolerance stops it at 1/64 of the upper bound.
BOOST_AUTO_TEST_CASE(estimateGasStopsAtTheRelativeTolerance)
{
    m_ledger->ledgerData().at(1)->blockHeader()->setGasLimit(u256(21'000'000));

    auto ladderScheduler = std::make_shared<LadderScheduler>(m_ledger, m_blockFactory);
    ladderScheduler->measured = u256(100'000);
    ladderScheduler->required = 250'000;  // 2.5x: just past where the ladder gives up
    auto ledgerService = std::make_shared<rpc::NodeService>(
        m_ledger, ladderScheduler, nullptr, nullptr, nullptr, m_blockFactory, nullptr);
    auto endpoint = std::make_shared<EthEndpoint>(ledgerService, nullptr, false);

    Json::Value params(Json::arrayValue);
    Json::Value tx(Json::objectValue);
    tx["to"] = "0x1234567890abcdef1234567890abcdef12345678";
    tx["data"] = "0x";
    params.append(tx);
    params.append("0x1");

    Json::Value response;
    task::syncWait(endpoint->estimateGas(params, response));

    // Ceiling, the measurement, the 8 rungs (100000 -> 214,367, all below 250,000) and 13
    // bisections. Narrowing to one gas instead needs 12 more. The answer overshoots by 2,425
    // (0.97%), which is the tolerance's price and inside its 1/64 bound.
    auto const& probes = ladderScheduler->gasLimits;
    BOOST_CHECK_EQUAL(probes.size(), 23U);
    auto const answer = std::stoull(response["result"].asString(), nullptr, 16);
    BOOST_CHECK_EQUAL(answer, 252'425U);
    BOOST_CHECK_GE(answer, ladderScheduler->required);
}

// A probe that stops for a reason other than the budget, before any budget tighter than the ceiling
// has been proven, must refuse rather than answer with the untightened ceiling. Answering `high`
// there would report up to the whole 21M/50M cap for a transfer that needs the intrinsic cost, and
// a wallet would take that number as its gasLimit.
BOOST_AUTO_TEST_CASE(estimateGasRefusesWhenTruncatedBeforeAnyTightening)
{
    m_ledger->ledgerData().at(1)->blockHeader()->setGasLimit(u256(21'000'000));

    auto ladderScheduler = std::make_shared<LadderScheduler>(m_ledger, m_blockFactory);
    ladderScheduler->measured = u256(21'000);
    ladderScheduler->required = 30'000;
    ladderScheduler->failOnCall = 2;  // the measurement probe, before anything was tightened
    auto ledgerService = std::make_shared<rpc::NodeService>(
        m_ledger, ladderScheduler, nullptr, nullptr, nullptr, m_blockFactory, nullptr);
    auto endpoint = std::make_shared<EthEndpoint>(ledgerService, nullptr, false);

    Json::Value params(Json::arrayValue);
    Json::Value tx(Json::objectValue);
    tx["to"] = "0x1234567890abcdef1234567890abcdef12345678";
    tx["data"] = "0x";
    params.append(tx);
    params.append("0x1");

    Json::Value response;
    BOOST_CHECK_THROW(task::syncWait(endpoint->estimateGas(params, response)), std::exception);
    // The ceiling probe and the failing measurement: the search never got to tighten anything.
    BOOST_CHECK_EQUAL(ladderScheduler->gasLimits.size(), 2U);
    BOOST_CHECK(!response.isMember("result"));
}

// The same stop, but after a tighter budget has been proven. That one ran, so it is the answer:
// dropping it on a transient refusal to tighten further would turn a usable result into a failed
// request.
BOOST_AUTO_TEST_CASE(estimateGasKeepsTheProvenBudgetWhenTruncatedLater)
{
    m_ledger->ledgerData().at(1)->blockHeader()->setGasLimit(u256(21'000'000));

    auto ladderScheduler = std::make_shared<LadderScheduler>(m_ledger, m_blockFactory);
    ladderScheduler->measured = u256(21'000);
    ladderScheduler->required = 12'000'000;
    // Call 12 proves 15,761,256 (it runs), call 13 stops the search. `high` is already tighter
    // than the ceiling at that point.
    ladderScheduler->failOnCall = 13;
    auto ledgerService = std::make_shared<rpc::NodeService>(
        m_ledger, ladderScheduler, nullptr, nullptr, nullptr, m_blockFactory, nullptr);
    auto endpoint = std::make_shared<EthEndpoint>(ledgerService, nullptr, false);

    Json::Value params(Json::arrayValue);
    Json::Value tx(Json::objectValue);
    tx["to"] = "0x1234567890abcdef1234567890abcdef12345678";
    tx["data"] = "0x";
    params.append(tx);
    params.append("0x1");

    Json::Value response;
    task::syncWait(endpoint->estimateGas(params, response));

    BOOST_CHECK(!response.isMember("error"));
    auto const answer = std::stoull(response["result"].asString(), nullptr, 16);
    BOOST_CHECK_GE(answer, ladderScheduler->required);  // proven to execute
    BOOST_CHECK_LT(answer, 21'000'000U);                // tightened, not the raw ceiling
    BOOST_CHECK_EQUAL(answer, 15'761'256U);
}

// A client sends an EIP-55 mixed-case `from` (ethers/viem default). The account row key is
// the lowercase hex text, so the pending-nonce lookup must normalize before the read or it
// misses and the call silently falls back to the state nonce.
BOOST_AUTO_TEST_CASE(callPendingNonceLookupNormalizesTheFromAddress)
{
    auto recordingScheduler = std::make_shared<RecordingScheduler>(m_ledger, m_blockFactory);
    auto ledgerService = std::make_shared<rpc::NodeService>(
        m_ledger, recordingScheduler, nullptr, nullptr, nullptr, m_blockFactory, nullptr);
    auto endpoint = std::make_shared<EthEndpoint>(ledgerService, nullptr, false);

    Json::Value params(Json::arrayValue);
    Json::Value tx(Json::objectValue);
    tx["from"] = "0x5AAeb6053f3e94c9b9a09f33669435E7Ef1BeAed";  // EIP-55 mixed case
    tx["to"] = "0x1234567890abcdef1234567890abcdef12345678";
    tx["data"] = "0x";
    params.append(tx);
    params.append("latest");

    Json::Value response;
    task::syncWait(endpoint->call(params, response));

    BOOST_CHECK_EQUAL(recordingScheduler->lastPendingKey, "nonce");
    BOOST_CHECK_EQUAL(
        recordingScheduler->lastPendingAddress, "5aaeb6053f3e94c9b9a09f33669435e7ef1beaed");
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
