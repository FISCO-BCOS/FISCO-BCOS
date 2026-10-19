/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include <bcos-boostssl/websocket/WsConfig.h>
#include <bcos-cpp-sdk/SdkFactory.h>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/test/unit_test.hpp>

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(SdkFactoryBuildTest)

// buildSdk(config) is the entry every sample uses; nothing in the suite exercised it before.
// WsInitializer::initWsService throws unless an IOServicePool was handed to it, so this case
// pins that SdkFactory::buildService wires the factory's own pool in.
BOOST_AUTO_TEST_CASE(buildSdkFromPlaintextClientConfigDoesNotThrow)
{
    auto config = std::make_shared<bcos::boostssl::ws::WsConfig>();
    config->setModel(bcos::boostssl::ws::WsModel::Client);
    config->setDisableSsl(true);
    config->setThreadPoolSize(1);
    auto peers = std::make_shared<bcos::boostssl::ws::EndPoints>();
    peers->insert(bcos::boostssl::NodeIPEndpoint("127.0.0.1", 65534));
    config->setConnectPeers(peers);

    bcos::cppsdk::SdkFactory factory;
    try
    {
        auto sdk = factory.buildSdk(config);
        BOOST_CHECK(sdk != nullptr);
    }
    catch (std::exception const& e)
    {
        BOOST_FAIL("buildSdk threw: " + boost::diagnostic_information(e));
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
