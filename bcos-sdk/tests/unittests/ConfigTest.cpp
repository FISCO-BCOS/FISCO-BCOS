/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include <bcos-cpp-sdk/config/Config.h>
#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>
#include <fstream>

using namespace bcos::cppsdk::config;

namespace bcos::test
{
namespace
{
std::string writeIni(const std::string& content)
{
    auto dir =
        boost::filesystem::temp_directory_path() / boost::filesystem::unique_path("sdkcfg-%%%%");
    boost::filesystem::create_directories(dir);
    auto path = (dir / "sdk.ini").string();
    std::ofstream out(path);
    out << content;
    out.close();
    return path;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(SdkConfigTest)

BOOST_AUTO_TEST_CASE(sendRpcFlagRoundTrip)
{
    Config cfg;
    cfg.setSendRpcRequestToHighestBlockNode(false);
    BOOST_CHECK(!cfg.sendRpcRequestToHighestBlockNode());
    cfg.setSendRpcRequestToHighestBlockNode(true);
    BOOST_CHECK(cfg.sendRpcRequestToHighestBlockNode());
}

BOOST_AUTO_TEST_CASE(loadConfigCommonAndPeersNoSsl)
{
    // disable_ssl=true so loadConfig skips loadCert (no cert files needed).
    auto path = writeIni(
        "[common]\n"
        "disable_ssl=true\n"
        "thread_pool_size=4\n"
        "message_timeout_ms=5000\n"
        "send_rpc_request_to_highest_block_node=false\n"
        "[peers]\n"
        "node.0=127.0.0.1:20200\n"
        "node.1=127.0.0.1:20201\n"
        "node.2=not-a-valid-endpoint\n");  // invalid → skipped, not fatal

    Config cfg;
    auto wsConfig = cfg.loadConfig(path);
    BOOST_REQUIRE(wsConfig);
    BOOST_CHECK(wsConfig->disableSsl());
    BOOST_CHECK_EQUAL(wsConfig->threadPoolSize(), 4U);
    BOOST_REQUIRE(wsConfig->connectPeers());
    // The two valid endpoints are added; the invalid one is skipped.
    BOOST_CHECK_EQUAL(wsConfig->connectPeers()->size(), 2U);
}

BOOST_AUTO_TEST_CASE(loadConfigSinglePeer)
{
    auto path = writeIni("[common]\ndisable_ssl=true\n[peers]\nnode.0=127.0.0.1:20200\n");
    Config cfg;
    auto wsConfig = cfg.loadConfig(path);
    BOOST_REQUIRE(wsConfig);
    BOOST_CHECK(wsConfig->disableSsl());
    BOOST_REQUIRE(wsConfig->connectPeers());
    BOOST_CHECK_EQUAL(wsConfig->connectPeers()->size(), 1U);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
