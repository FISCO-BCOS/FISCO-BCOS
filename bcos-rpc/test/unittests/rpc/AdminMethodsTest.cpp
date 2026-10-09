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
#include <bcos-rpc/jsonrpc/AdminMethods.h>
#include <bcos-utilities/BoostLog.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::rpc;

namespace bcos::test
{
namespace
{
struct LevelGuard
{
    LogLevel saved{c_fileLogLevel};
    ~LevelGuard()
    {
        setFileLogLevel(saved);
        for (size_t i = 0; i < static_cast<size_t>(LogModule::COUNT); ++i)
        {
            resetModuleLogLevel(static_cast<LogModule>(i));
        }
    }
};

Json::Value parse(bcos::bytes const& _body)
{
    Json::Value value;
    Json::CharReaderBuilder builder;
    std::string errors;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    auto const* begin = reinterpret_cast<char const*>(_body.data());
    BOOST_REQUIRE(reader->parse(begin, begin + _body.size(), &value, &errors));
    return value;
}

Json::Value viaIpc(JsonRpcInterface& _rpc, std::string const& _request)
{
    Json::Value captured;
    _rpc.onIpcRequest(_request,
        [&captured](bcos::bytes _body, boost::beast::http::status) { captured = parse(_body); });
    return captured;
}

Json::Value viaTcp(JsonRpcInterface& _rpc, std::string const& _request)
{
    Json::Value captured;
    _rpc.onRPCRequest(_request,
        [&captured](bcos::bytes _body, boost::beast::http::status) { captured = parse(_body); });
    return captured;
}
}  // namespace

BOOST_FIXTURE_TEST_SUITE(AdminMethodsTest, RPCFixture)

BOOST_AUTO_TEST_CASE(setModuleLevelThroughIpc)
{
    LevelGuard guard;
    setFileLogLevel(LogLevel::INFO);
    auto rpc = factory->buildLocalRpc(groupInfo, nodeService);
    auto jsonRpc = rpc->jsonRpcImpl();
    registerAdminMethods(*jsonRpc);

    auto before =
        viaIpc(*jsonRpc, R"({"jsonrpc":"2.0","id":1,"method":"admin_getLogLevel","params":[]})");
    BOOST_CHECK_EQUAL(before["result"]["global"].asString(), "info");
    BOOST_CHECK_EQUAL(before["result"]["modules"].size(), 0U);

    auto set = viaIpc(*jsonRpc,
        R"({"jsonrpc":"2.0","id":2,"method":"admin_setLogLevel","params":["debug","PBFT"]})");
    BOOST_CHECK(!set.isMember("error") || set["error"].isNull());
    BOOST_CHECK(moduleLogEnabled(LogModule::PBFT, LogLevel::DEBUG));
    BOOST_CHECK(!moduleLogEnabled(LogModule::TXPOOL, LogLevel::DEBUG));
    BOOST_CHECK_EQUAL(set["result"]["modules"]["PBFT"].asString(), "debug");

    auto inherit = viaIpc(*jsonRpc,
        R"({"jsonrpc":"2.0","id":3,"method":"admin_setLogLevel","params":["inherit","pbft"]})");
    BOOST_CHECK_EQUAL(inherit["result"]["modules"].size(), 0U);
    BOOST_CHECK(!moduleLogEnabled(LogModule::PBFT, LogLevel::DEBUG));

    auto global = viaIpc(
        *jsonRpc, R"({"jsonrpc":"2.0","id":4,"method":"admin_setLogLevel","params":["trace"]})");
    BOOST_CHECK_EQUAL(global["result"]["global"].asString(), "trace");
    BOOST_CHECK(moduleLogEnabled(LogModule::TXPOOL, LogLevel::TRACE));
}

BOOST_AUTO_TEST_CASE(adminMethodsAreNotOnTcp)
{
    LevelGuard guard;
    auto rpc = factory->buildLocalRpc(groupInfo, nodeService);
    auto jsonRpc = rpc->jsonRpcImpl();
    registerAdminMethods(*jsonRpc);
    auto tcp =
        viaTcp(*jsonRpc, R"({"jsonrpc":"2.0","id":5,"method":"admin_getLogLevel","params":[]})");
    BOOST_CHECK_EQUAL(tcp["error"]["code"].asInt(), -32601);
    // the public table is still served on the socket
    auto ipc = viaIpc(*jsonRpc, R"({"jsonrpc":"2.0","id":6,"method":"getGroupList","params":[]})");
    BOOST_CHECK(ipc["result"]["groupList"].isArray());
}

BOOST_AUTO_TEST_CASE(invalidLevelOrModuleIsInvalidParams)
{
    LevelGuard guard;
    auto rpc = factory->buildLocalRpc(groupInfo, nodeService);
    auto jsonRpc = rpc->jsonRpcImpl();
    registerAdminMethods(*jsonRpc);
    auto badLevel = viaIpc(
        *jsonRpc, R"({"jsonrpc":"2.0","id":7,"method":"admin_setLogLevel","params":["nope"]})");
    BOOST_CHECK_EQUAL(badLevel["error"]["code"].asInt(), -32602);
    auto badModule = viaIpc(*jsonRpc,
        R"({"jsonrpc":"2.0","id":8,"method":"admin_setLogLevel","params":["debug","NOPE"]})");
    BOOST_CHECK_EQUAL(badModule["error"]["code"].asInt(), -32602);
    auto noParams =
        viaIpc(*jsonRpc, R"({"jsonrpc":"2.0","id":9,"method":"admin_setLogLevel","params":[]})");
    BOOST_CHECK_EQUAL(noParams["error"]["code"].asInt(), -32602);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
