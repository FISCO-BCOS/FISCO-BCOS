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
#include <bcos-rpc/jsonrpc/JsonRpcImpl_2_0.h>
#include <future>

using namespace bcos;
using namespace bcos::rpc;

namespace bcos::test
{
// Drives the JSON-RPC 2.0 handler surface against the faked node stack from
// RPCFixture (FakeLedger has 20 blocks). JsonRpcImpl_2_0.cpp was at ~2% line
// coverage; each handler cascades through request dispatch + response shaping.
BOOST_FIXTURE_TEST_SUITE(RpcHandlersTest, RPCFixture)

namespace
{
struct Captured
{
    bool called = false;
    bool hasError = false;
    Json::Value value;
};

auto capturing(Captured& out)
{
    return [&out](bcos::Error::Ptr error, Json::Value& value) {
        out.called = true;
        out.hasError = (error != nullptr);
        out.value = value;
    };
}

// For handlers whose callback fires off-thread: block on a promise so the
// lambda never outlives the test frame and we observe the result.
struct AsyncResult
{
    bool hasError = false;
    Json::Value value;
};

template <typename Invoke>
AsyncResult awaitHandler(Invoke&& invoke)
{
    auto shared = std::make_shared<std::promise<AsyncResult>>();
    auto future = shared->get_future();
    std::forward<Invoke>(invoke)([shared](bcos::Error::Ptr error, Json::Value& value) {
        AsyncResult result;
        result.hasError = (error != nullptr);
        result.value = value;
        shared->set_value(std::move(result));
    });
    return future.get();
}
}  // namespace

BOOST_AUTO_TEST_CASE(getBlockNumberReturnsPositive)
{
    auto rpc = factory->buildLocalRpc(groupInfo, nodeService);
    rpc->groupManager()->updateGroupInfo(groupInfo);

    Captured cap;
    rpc->jsonRpcImpl()->getBlockNumber(groupId, "", capturing(cap));
    BOOST_REQUIRE(cap.called);
    BOOST_CHECK(!cap.hasError);
    BOOST_CHECK_GT(cap.value.asInt64(), 0);
}

BOOST_AUTO_TEST_CASE(getBlockByNumberHeaderOnly)
{
    auto rpc = factory->buildLocalRpc(groupInfo, nodeService);
    rpc->groupManager()->updateGroupInfo(groupInfo);

    Captured cap;
    rpc->jsonRpcImpl()->getBlockByNumber(
        groupId, "", 1, /*onlyHeader=*/true, /*onlyTxHash=*/false, capturing(cap));
    BOOST_REQUIRE(cap.called);
    BOOST_CHECK(!cap.hasError);
}

BOOST_AUTO_TEST_CASE(getBlockByNumberFullAndTxHashOnly)
{
    auto rpc = factory->buildLocalRpc(groupInfo, nodeService);
    rpc->groupManager()->updateGroupInfo(groupInfo);

    Captured full;
    rpc->jsonRpcImpl()->getBlockByNumber(groupId, "", 1, false, false, capturing(full));
    BOOST_REQUIRE(full.called);

    Captured hashOnly;
    rpc->jsonRpcImpl()->getBlockByNumber(groupId, "", 1, false, true, capturing(hashOnly));
    BOOST_REQUIRE(hashOnly.called);
}

BOOST_AUTO_TEST_CASE(getBlockHashByNumberFires)
{
    auto rpc = factory->buildLocalRpc(groupInfo, nodeService);
    rpc->groupManager()->updateGroupInfo(groupInfo);

    Captured hashCap;
    rpc->jsonRpcImpl()->getBlockHashByNumber(groupId, "", 1, capturing(hashCap));
    BOOST_REQUIRE(hashCap.called);
}

BOOST_AUTO_TEST_CASE(getSystemConfigByKeyKnownKey)
{
    auto rpc = factory->buildLocalRpc(groupInfo, nodeService);
    rpc->groupManager()->updateGroupInfo(groupInfo);

    Captured cap;
    rpc->jsonRpcImpl()->getSystemConfigByKey(
        groupId, "", ledger::SYSTEM_KEY_TX_COUNT_LIMIT, capturing(cap));
    BOOST_REQUIRE(cap.called);
}

BOOST_AUTO_TEST_CASE(groupQueryHandlersFire)
{
    auto rpc = factory->buildLocalRpc(groupInfo, nodeService);
    rpc->groupManager()->updateGroupInfo(groupInfo);
    auto* impl = rpc->jsonRpcImpl().get();

    Captured list;
    impl->getGroupList(capturing(list));
    BOOST_CHECK(list.called);

    Captured infoList;
    impl->getGroupInfoList(capturing(infoList));
    BOOST_CHECK(infoList.called);

    Captured groupInfoCap;
    impl->getGroupInfo(groupId, capturing(groupInfoCap));
    BOOST_CHECK(groupInfoCap.called);

    Captured groupBlockNumber;
    impl->getGroupBlockNumber(capturing(groupBlockNumber));
    BOOST_CHECK(groupBlockNumber.called);
}

BOOST_AUTO_TEST_CASE(nodeListHandlersFire)
{
    auto rpc = factory->buildLocalRpc(groupInfo, nodeService);
    rpc->groupManager()->updateGroupInfo(groupInfo);
    auto* impl = rpc->jsonRpcImpl().get();

    Captured sealers;
    impl->getSealerList(groupId, "", capturing(sealers));
    BOOST_CHECK(sealers.called);

    Captured observers;
    impl->getObserverList(groupId, "", capturing(observers));
    BOOST_CHECK(observers.called);
}

BOOST_AUTO_TEST_CASE(totalTransactionCountFires)
{
    auto rpc = factory->buildLocalRpc(groupInfo, nodeService);
    rpc->groupManager()->updateGroupInfo(groupInfo);

    Captured cap;
    rpc->jsonRpcImpl()->getTotalTransactionCount(groupId, "", capturing(cap));
    BOOST_CHECK(cap.called);
}

BOOST_AUTO_TEST_CASE(pendingTxSizeFires)
{
    auto rpc = factory->buildLocalRpc(groupInfo, nodeService);
    rpc->groupManager()->updateGroupInfo(groupInfo);

    Captured cap;
    rpc->jsonRpcImpl()->getPendingTxSize(groupId, "", capturing(cap));
    BOOST_CHECK(cap.called);
}

BOOST_AUTO_TEST_CASE(callViaAsyncWait)
{
    auto rpc = factory->buildLocalRpc(groupInfo, nodeService);
    rpc->groupManager()->updateGroupInfo(groupInfo);
    auto* impl = rpc->jsonRpcImpl().get();

    // call routes through FakeScheduler2 which returns an empty receipt.
    std::string to = "0x1234567890123456789012345678901234567890";
    auto result = awaitHandler([&](auto cb) { impl->call(groupId, "", to, "0x", std::move(cb)); });
    BOOST_CHECK(!result.value.isNull() || result.hasError);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
