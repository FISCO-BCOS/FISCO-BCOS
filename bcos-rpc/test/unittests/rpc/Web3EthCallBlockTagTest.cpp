/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * @file Web3EthCallBlockTagTest.cpp
 * @brief eth_call blockTag routing (M13.2): latest-family tags (latest / pending) stay on
 *        SchedulerInterface::call, historical tags — including safe / finalized, which
 *        resolve to latest - depth (configurable) — go through callAtBlock with the
 *        resolved height, and the callAtBlock default implementation keeps schedulers that
 *        only implement call() working.
 */

#include "../common/RPCFixture.h"
#include <bcos-rpc/web3jsonrpc/Web3JsonRpcImpl.h>
#include <boost/test/unit_test.hpp>
#include <future>
#include <string>
#include <string_view>
#include <vector>

using namespace bcos;
using namespace bcos::rpc;

namespace bcos::test
{

/// Records which SchedulerInterface entry point eth_call lands on, and with what height.
class RecordingScheduler : public FakeScheduler
{
public:
    using FakeScheduler::FakeScheduler;
    using Ptr = std::shared_ptr<RecordingScheduler>;

    int m_latestCalls{0};
    std::vector<protocol::BlockNumber> m_historicalCalls;

    void call(protocol::Transaction::Ptr,
        std::function<void(Error::Ptr, protocol::TransactionReceipt::Ptr)> callback) noexcept
        override
    {
        ++m_latestCalls;
        callback({}, std::make_shared<bcostars::protocol::TransactionReceiptImpl>());
    }
    void callAtBlock(protocol::Transaction::Ptr, protocol::BlockNumber blockNumber,
        std::function<void(Error::Ptr, protocol::TransactionReceipt::Ptr)> callback) override
    {
        m_historicalCalls.push_back(blockNumber);
        callback({}, std::make_shared<bcostars::protocol::TransactionReceiptImpl>());
    }
};

class Web3EthCallBlockTagFixture : public RPCFixture
{
public:
    Web3JsonRpcImpl::Ptr buildWeb3Rpc(std::shared_ptr<bcos::scheduler::SchedulerInterface> sched)
    {
        auto service = std::make_shared<rpc::NodeService>(
            m_ledger, std::move(sched), txPool, nullptr, nullptr, m_blockFactory, nullptr);
        rpc = factory->buildLocalRpc(groupInfo, service);
        auto web3 = rpc->web3JsonRpc();
        BOOST_REQUIRE(web3 != nullptr);
        return web3;
    }

    static Json::Value request(Web3JsonRpcImpl::Ptr const& web3, std::string_view blockTag)
    {
        auto payload =
            std::string(
                R"({"jsonrpc":"2.0","id":1,"method":"eth_call","params":[{"to":"0x1234567890123456789012345678901234567890","data":"0x"},)") +
            std::string(blockTag) + "]}";
        std::promise<bcos::bytes> promise;
        web3->onRPCRequest(
            payload, [&promise](bcos::bytes resp, boost::beast::http::status) { promise.set_value(std::move(resp)); });
        auto jsonBytes = promise.get_future().get();
        Json::Value value;
        Json::Reader reader;
        std::string_view json((char*)jsonBytes.data(), jsonBytes.size());
        reader.parse(json.begin(), json.end(), value);
        return value;
    }

    Rpc::Ptr rpc;
};

BOOST_FIXTURE_TEST_SUITE(Web3EthCallBlockTagTest, Web3EthCallBlockTagFixture)

BOOST_AUTO_TEST_CASE(latestFamilyTagsStayOnTheLatestCall)
{
    auto recording = std::make_shared<RecordingScheduler>(m_ledger, m_blockFactory);
    auto web3 = buildWeb3Rpc(recording);

    // "latest" / "pending" are the head. "safe" / "finalized" are NOT: they resolve to
    // committed historical blocks (latest - safeBlockDepth / finalizedBlockDepth) and are
    // covered in historicalTagsRouteThroughCallAtBlock.
    for (auto tag : {R"("latest")", R"("pending")"})
    {
        auto resp = request(web3, tag);
        BOOST_CHECK_MESSAGE(resp.isMember("result"), "tag " + std::string(tag));
    }
    BOOST_CHECK_EQUAL(recording->m_latestCalls, 2);
    BOOST_CHECK(recording->m_historicalCalls.empty());
}

BOOST_AUTO_TEST_CASE(historicalTagsRouteThroughCallAtBlock)
{
    auto recording = std::make_shared<RecordingScheduler>(m_ledger, m_blockFactory);
    auto web3 = buildWeb3Rpc(recording);

    auto resp = request(web3, R"("0x1")");
    BOOST_CHECK(resp.isMember("result"));
    auto respEarliest = request(web3, R"("earliest")");
    BOOST_CHECK(respEarliest.isMember("result"));
    // safe / finalized are historical too: latest(19) - default safeDepth(1) / finalizedDepth(2).
    auto respSafe = request(web3, R"("safe")");
    BOOST_CHECK(respSafe.isMember("result"));
    auto respFinalized = request(web3, R"("finalized")");
    BOOST_CHECK(respFinalized.isMember("result"));

    BOOST_CHECK_EQUAL(recording->m_latestCalls, 0);
    BOOST_REQUIRE_EQUAL(recording->m_historicalCalls.size(), 4U);
    BOOST_CHECK_EQUAL(recording->m_historicalCalls[0], 1);
    BOOST_CHECK_EQUAL(recording->m_historicalCalls[1], 0);
    BOOST_CHECK_EQUAL(recording->m_historicalCalls[2], 18);  // safe = latest(19) - 1
    BOOST_CHECK_EQUAL(recording->m_historicalCalls[3], 17);  // finalized = latest(19) - 2
}

BOOST_AUTO_TEST_CASE(numericTagAtTheTipIsLatest)
{
    auto recording = std::make_shared<RecordingScheduler>(m_ledger, m_blockFactory);
    auto web3 = buildWeb3Rpc(recording);

    // The fake ledger's current height, given as an explicit number, is the latest state.
    protocol::BlockNumber latest = -1;
    m_ledger->asyncGetBlockNumber(
        [&latest](Error::Ptr, protocol::BlockNumber number) { latest = number; });
    BOOST_REQUIRE_GE(latest, 0);

    std::ostringstream quantity;
    quantity << "\"0x" << std::hex << latest << "\"";
    auto resp = request(web3, quantity.str());
    BOOST_CHECK(resp.isMember("result"));
    BOOST_CHECK_EQUAL(recording->m_latestCalls, 1);
    BOOST_CHECK(recording->m_historicalCalls.empty());
}

// A scheduler that never heard of callAtBlock (overrides only call) still serves a
// historical tag through the interface's forwarding default implementation.
BOOST_AUTO_TEST_CASE(defaultImplementationKeepsLegacySchedulersWorking)
{
    auto web3 = buildWeb3Rpc(std::make_shared<FakeScheduler2>(m_ledger, m_blockFactory));

    auto resp = request(web3, R"("0x1")");
    BOOST_CHECK(resp.isMember("result"));
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
