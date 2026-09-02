/**
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @brief test for EventSub
 * @file EventSubTest.cpp
 * @author: octopus
 * @date 2021-09-22
 */
#include "../fake/WsSessionFake.h"
#include <bcos-cpp-sdk/event/EventSub.h>
#include <bcos-cpp-sdk/event/EventSubResponse.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/IOServicePool.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>
#include <chrono>
#include <future>
#include <memory>
#include <thread>

using namespace bcos;
using namespace bcos::cppsdk;
using namespace bcos::test;

BOOST_FIXTURE_TEST_SUITE(EventSubTest, TestPromptFixture)

BOOST_AUTO_TEST_CASE(test_EventSub_suspendTask)
{
    boost::asio::io_context ioContext;
    auto es = std::make_shared<bcos::cppsdk::event::EventSub>(ioContext);
    auto task = std::make_shared<bcos::cppsdk::event::EventSubTask>();
    std::string id = "123";
    task->setId(id);

    auto r = es->addSuspendTask(task);
    BOOST_CHECK(r);
    BOOST_CHECK_EQUAL(es->suspendTasksCount(), 1);
    r = es->addSuspendTask(task);
    BOOST_CHECK(!r);
    BOOST_CHECK_EQUAL(es->suspendTasksCount(), 1);

    r = es->removeSuspendTask(id);
    BOOST_CHECK(r);
    BOOST_CHECK_EQUAL(es->suspendTasksCount(), 0);

    r = es->removeSuspendTask(id);
    BOOST_CHECK(!r);
    BOOST_CHECK_EQUAL(es->suspendTasksCount(), 0);
}

BOOST_AUTO_TEST_CASE(test_EventSub_addTask)
{
    boost::asio::io_context ioContext;
    auto es = std::make_shared<bcos::cppsdk::event::EventSub>(ioContext);
    auto task1 = std::make_shared<bcos::cppsdk::event::EventSubTask>();
    auto task2 = std::make_shared<bcos::cppsdk::event::EventSubTask>();

    std::string id1 = "123";
    std::string id2 = "456";
    task1->setId(id1);
    task2->setId(id2);

    {
        // addTask
        auto r = es->addTask(task1);
        BOOST_CHECK(r);
        r = es->addTask(task1);
        BOOST_CHECK(!r);

        // getAndRemove
        auto task = es->getTask(id1);
        BOOST_CHECK(task);
        task = es->getTaskAndRemove(id1);
        BOOST_CHECK(task);
        task = es->getTask(id1);
        BOOST_CHECK(!task);
        task = es->getTaskAndRemove(id1);
        BOOST_CHECK(!task);
    }

    {
        // addTask
        auto r = es->addTask(task1);
        BOOST_CHECK(r);
        r = es->addTask(task1);
        BOOST_CHECK(!r);

        // getAndRemove
        auto task = es->getTask(id1);
        BOOST_CHECK(task);
        task = es->getTaskAndRemove(id1);
        BOOST_CHECK(task);
        task = es->getTask(id1);
        BOOST_CHECK(!task);
        task = es->getTaskAndRemove(id1);
        BOOST_CHECK(!task);
    }

    {
        auto r = es->addSuspendTask(task2);
        BOOST_CHECK(r);
        r = es->addSuspendTask(task2);
        BOOST_CHECK(!r);
        BOOST_CHECK_EQUAL(es->suspendTasksCount(), 1);

        auto task = es->getTask(id2, false);
        BOOST_CHECK(!task);

        task = es->getTask(id2);
        BOOST_CHECK(task);

        task = es->getTaskAndRemove(id2, false);
        BOOST_CHECK(!task);

        task = es->getTaskAndRemove(id2);
        BOOST_CHECK(task);

        BOOST_CHECK_EQUAL(es->suspendTasksCount(), 0);
    }

    {
        auto r = es->addSuspendTask(task2);
        BOOST_CHECK(r);
        BOOST_CHECK_EQUAL(es->suspendTasksCount(), 1);

        r = es->addTask(task2);
        BOOST_CHECK(r);

        BOOST_CHECK_EQUAL(es->suspendTasksCount(), 0);
    }
}

BOOST_AUTO_TEST_CASE(test_EventSub_unsubscribeEvent)
{
    boost::asio::io_context ioContext;
    auto es = std::make_shared<bcos::cppsdk::event::EventSub>(ioContext);

    auto task = std::make_shared<bcos::cppsdk::event::EventSubTask>();
    std::string id = "123";
    task->setId(id);
    {
        // task is suspend
        es->addSuspendTask(task);
        BOOST_CHECK_EQUAL(es->suspendTasksCount(), 1);
        std::promise<bool> p;
        auto f = p.get_future();

        BOOST_CHECK(!es->getTask(id, false));
        BOOST_CHECK_EQUAL(es->suspendTasksCount(), 1);

        BOOST_CHECK(es->getTask(id));
        BOOST_CHECK_EQUAL(es->suspendTasksCount(), 1);

        es->unsubscribeEvent(id);

        BOOST_CHECK(!es->getTask(id));
        BOOST_CHECK_EQUAL(es->suspendTasksCount(), 0);
    }

    auto ioServicePool = std::make_shared<IOServicePool>(2, "evtSubTest");
    {
        // task is running: unsubscribe through a real (not connected) session. The
        // assertions below only cover the synchronous task removal inside
        // unsubscribeEvent; the disconnected-session error callback fires on the
        // io-pool thread and is not asserted here (the block below covers the
        // response-parsing path via onRecvEventSubMessage).
        auto session = std::make_shared<bcos::cppsdk::test::WsSessionFake>(ioServicePool);
        task->setSession(session->session());

        es->addTask(task);

        // callback error (session disconnected)
        es->unsubscribeEvent(id);

        BOOST_CHECK(!es->getTask(id));
        BOOST_CHECK_EQUAL(es->suspendTasksCount(), 0);
    }

    {
        // response parsing driven through the public onRecvEventSubMessage (the
        // receive-path handler for event-sub responses): exercises the
        // EventSubResponse::fromJson and status dispatch machinery.
        auto session = std::make_shared<bcos::cppsdk::test::WsSessionFake>(ioServicePool);

        // invalid response: fromJson fails, handler returns without side effects
        std::string invalidJson = "not-a-json";
        bcos::boostssl::ws::WsMessage invalid;
        invalid.setPayload(bcos::bytes(invalidJson.begin(), invalidJson.end()));
        es->onRecvEventSubMessage(std::move(invalid), session->session());

        // valid success response: task matched by id and its callback invoked
        auto respTask = std::make_shared<bcos::cppsdk::event::EventSubTask>();
        respTask->setId(id);
        bool called = false;
        respTask->setCallback(
            [&called](bcos::Error::Ptr, const std::string&) { called = true; });
        es->addTask(respTask);

        auto resp = std::make_shared<bcos::cppsdk::event::EventSubResponse>();
        resp->setId(id);
        resp->setStatus(0);
        auto respJson = resp->generateJson();
        bcos::boostssl::ws::WsMessage msg;
        msg.setPayload(bcos::bytes(respJson.begin(), respJson.end()));
        es->onRecvEventSubMessage(std::move(msg), session->session());
        BOOST_CHECK(called);
    }
}

BOOST_AUTO_TEST_SUITE_END()