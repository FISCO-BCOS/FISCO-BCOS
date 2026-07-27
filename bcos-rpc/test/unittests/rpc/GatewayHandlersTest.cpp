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
#include <boost/test/unit_test.hpp>
#include <future>

using namespace bcos;
using namespace bcos::rpc;

namespace bcos::test
{
namespace
{
// FakeGateWayWrapper::asyncGetPeers has an empty body (never calls back), which
// would hang getPeers/getGroupPeers. Override it to call back with an error so
// the handlers run their dispatch + error branch without needing valid
// GatewayInfo (which the success path would serialize).
class ErrorGateway : public FakeGateWayWrapper
{
public:
    void asyncGetPeers(
        std::function<void(Error::Ptr, gateway::GatewayInfo::Ptr, gateway::GatewayInfosPtr)>
            _callback) override
    {
        _callback(BCOS_ERROR_PTR(-1, "no peers in test"), nullptr, nullptr);
    }
};

struct AsyncResult
{
    bool fired = false;
    bool hasError = false;
};

template <typename Invoke>
AsyncResult awaitHandler(Invoke&& invoke)
{
    auto shared = std::make_shared<std::promise<AsyncResult>>();
    auto future = shared->get_future();
    std::forward<Invoke>(invoke)([shared](bcos::Error::Ptr error, Json::Value&) {
        shared->set_value(AsyncResult{true, error != nullptr});
    });
    return future.get();
}
}  // namespace

class GatewayHandlersFixture : public RPCFixture
{
public:
    GatewayHandlersFixture()
    {
        auto gw = std::make_shared<ErrorGateway>();
        auto myFactory =
            std::make_shared<bcos::rpc::RpcFactory>(chainId, gw, cryptoSuite->keyFactory());
        myFactory->setNodeConfig(nodeConfig);
        // This fixture builds its own factory (to inject ErrorGateway) instead of using the one
        // RPCFixture sets up, so it must also inject the shared IOServicePool -- RpcFactory now
        // requires it and throws from buildWsService without it. Reuse the fixture's pool.
        myFactory->setIOServicePool(ioServicePool);
        rpc = myFactory->buildLocalRpc(groupInfo, nodeService);
        rpc->groupManager()->updateGroupInfo(groupInfo);
    }
    Rpc::Ptr rpc;
};

BOOST_FIXTURE_TEST_SUITE(GatewayHandlersTest, GatewayHandlersFixture)

BOOST_AUTO_TEST_CASE(getPeersErrorPathFires)
{
    auto* impl = rpc->jsonRpcImpl().get();
    auto result = awaitHandler([&](auto cb) { impl->getPeers(std::move(cb)); });
    BOOST_CHECK(result.fired);
    BOOST_CHECK(result.hasError);  // ErrorGateway reports an error
}

BOOST_AUTO_TEST_CASE(getGroupNodeInfoUsesGroupManager)
{
    // getGroupNodeInfo reads m_groupManager (no gateway) and calls RespFunc
    // synchronously.
    auto* impl = rpc->jsonRpcImpl().get();
    bool fired = false;
    impl->getGroupNodeInfo(groupId, "", [&](bcos::Error::Ptr, Json::Value&) { fired = true; });
    BOOST_CHECK(fired);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
