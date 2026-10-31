/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * @brief Regression test for issue #5433: FrontService::stop() must drop the registered module
 *        dispatchers, otherwise the FrontService -> dispatcher lambda -> BlockSync/PBFT ->
 *        FrontService reference cycle outlives stop() and those modules never destruct.
 */
#include "FakeGateway.h"
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-front/FrontService.h>
#include <bcos-front/FrontServiceFactory.h>
#include <bcos-utilities/IOServicePool.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>
#include <memory>

using namespace bcos;
using namespace bcos::front;

namespace bcos::test
{
BOOST_FIXTURE_TEST_SUITE(Issue5433StopReleasesDispatchers, TestPromptFixture)

BOOST_AUTO_TEST_CASE(stopReleasesModuleDispatchers)
{
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    auto nodeID = keyFactory->createKey(bytesConstRef((bcos::byte*)"n", 1));

    auto factory = std::make_shared<FrontServiceFactory>();
    factory->setGatewayInterface(std::make_shared<bcos::front::test::FakeGateway>());
    factory->setIOServicePool(std::make_shared<bcos::IOServicePool>(1, "issue5433"));
    auto front = factory->buildFrontService("group", nodeID);

    // Stand-in for BlockSync/PBFT: the dispatcher lambda holds it strongly, exactly like
    // libinitializer/FrontServiceInitializer.cpp captures `_blockSync` by value.
    auto module = std::make_shared<int>(42);
    std::weak_ptr<int> moduleAlive = module;
    front->registerModuleMessageDispatcher(bcos::protocol::ModuleID::BlockSync,
        [module](bcos::crypto::NodeIDPtr, const std::string&, bytesConstRef) { (void)*module; });
    module.reset();
    BOOST_CHECK(!moduleAlive.expired());  // held by the dispatcher

    // Second table: FrontServiceInitializer::registerGroupNodeInfoNotification captures
    // txpool/blockSync/pbft by value the same way.
    auto notified = std::make_shared<int>(7);
    std::weak_ptr<int> notifiedAlive = notified;
    front->registerGroupNodeInfoNotification(
        bcos::protocol::ModuleID::PBFT, [notified](bcos::gateway::GroupNodeInfo::Ptr,
                                            bcos::front::ReceiveMsgFunc) { (void)*notified; });
    notified.reset();
    BOOST_CHECK(!notifiedAlive.expired());

    front->start();
    front->stop();

    BOOST_CHECK(moduleAlive.expired());  // stop() dropped the dispatcher, cycle broken
    BOOST_CHECK(front->moduleID2MessageDispatcher().empty());
    BOOST_CHECK(notifiedAlive.expired());
    BOOST_CHECK(front->module2GroupNodeInfoNotifier().empty());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
