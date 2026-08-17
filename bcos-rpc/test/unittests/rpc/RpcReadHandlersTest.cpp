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

#include <boost/test/unit_test.hpp>
using namespace bcos;
using namespace bcos::rpc;

namespace bcos::test
{
BOOST_FIXTURE_TEST_SUITE(RpcReadHandlersTest, RPCFixture)

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
}  // namespace

// Node-list style queries resolve through the ledger and fire synchronously.
BOOST_AUTO_TEST_CASE(nodeListQueriesFire)
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

    Captured byType;
    impl->getNodeListByType(groupId, "", "sealer", capturing(byType));
    BOOST_CHECK(byType.called);
}

// Group-node info query fires.
BOOST_AUTO_TEST_CASE(groupNodeInfoQueryFires)
{
    auto rpc = factory->buildLocalRpc(groupInfo, nodeService);
    rpc->groupManager()->updateGroupInfo(groupInfo);
    auto* impl = rpc->jsonRpcImpl().get();

    Captured nodeInfo;
    impl->getGroupNodeInfo(groupId, "node1", capturing(nodeInfo));
    BOOST_CHECK(nodeInfo.called);
}

// Filter creation returns an id / fires the callback.
BOOST_AUTO_TEST_CASE(filterCreationFires)
{
    auto rpc = factory->buildLocalRpc(groupInfo, nodeService);
    rpc->groupManager()->updateGroupInfo(groupInfo);
    auto* impl = rpc->jsonRpcImpl().get();

    Captured blockFilter;
    impl->newBlockFilter(groupId, capturing(blockFilter));
    BOOST_CHECK(blockFilter.called);

    Captured pendingFilter;
    impl->newPendingTransactionFilter(groupId, capturing(pendingFilter));
    BOOST_CHECK(pendingFilter.called);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
