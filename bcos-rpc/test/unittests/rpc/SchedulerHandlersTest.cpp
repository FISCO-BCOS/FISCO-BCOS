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
// FakeScheduler::getCode/getABI have empty bodies (never call back) → getCode/
// getABI handlers hang. Override them to invoke the callback so the handlers
// run their success path.
class CodeScheduler : public FakeScheduler2
{
public:
    using FakeScheduler2::FakeScheduler2;
    void getCode(std::string_view, std::function<void(Error::Ptr, bcos::bytes)> cb) override
    {
        cb(nullptr, bcos::bytes{0x60, 0x80, 0x60, 0x40});  // a few bytes of "code"
    }
    void getABI(std::string_view, std::function<void(Error::Ptr, std::string)> cb) override
    {
        cb(nullptr, "[]");
    }
};

struct SchedResult
{
    bool fired = false;
    bool hasError = false;
    Json::Value value;
};

template <typename Invoke>
SchedResult awaitSched(Invoke&& invoke)
{
    auto shared = std::make_shared<std::promise<SchedResult>>();
    auto future = shared->get_future();
    std::forward<Invoke>(invoke)([shared](bcos::Error::Ptr error, Json::Value& value) {
        SchedResult result;
        result.fired = true;
        result.hasError = (error != nullptr);
        result.value = value;
        shared->set_value(std::move(result));
    });
    return future.get();
}
}  // namespace

class SchedulerHandlersFixture : public RPCFixture
{
public:
    SchedulerHandlersFixture()
    {
        auto codeScheduler = std::make_shared<CodeScheduler>(m_ledger, m_blockFactory);
        // NodeService gained a trailing AnyEngineService argument; this test does not exercise
        // the engine service, so pass none.
        auto svc = std::make_shared<rpc::NodeService>(
            m_ledger, codeScheduler, txPool, nullptr, nullptr, m_blockFactory, nullptr);
        rpc = factory->buildLocalRpc(groupInfo, svc);
        rpc->groupManager()->updateGroupInfo(groupInfo);
    }
    Rpc::Ptr rpc;
};

BOOST_FIXTURE_TEST_SUITE(SchedulerHandlersTest, SchedulerHandlersFixture)

BOOST_AUTO_TEST_CASE(getCodeReturnsHexFromScheduler)
{
    auto* impl = rpc->jsonRpcImpl().get();
    std::string addr = "0x1234567890123456789012345678901234567890";
    auto result = awaitSched([&](auto cb) { impl->getCode(groupId, "", addr, std::move(cb)); });
    BOOST_CHECK(result.fired);
    BOOST_CHECK(!result.hasError);
}

BOOST_AUTO_TEST_CASE(getABIReturnsFromScheduler)
{
    auto* impl = rpc->jsonRpcImpl().get();
    std::string addr = "0x1234567890123456789012345678901234567890";
    auto result = awaitSched([&](auto cb) { impl->getABI(groupId, "", addr, std::move(cb)); });
    BOOST_CHECK(result.fired);
    BOOST_CHECK(!result.hasError);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
