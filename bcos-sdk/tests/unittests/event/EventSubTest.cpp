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
        // task is running: unsubscribe through a real (not connected) session,
        // which drives the real asyncSendMessage -> error callback path of
        // EventSub::unsubscribeEvent
        auto session = std::make_shared<bcos::cppsdk::test::WsSessionFake>(ioServicePool);
        task->setSession(session->session());

        es->addTask(task);

        // callback error (session disconnected)
        es->unsubscribeEvent(id);

        BOOST_CHECK(!es->getTask(id));
        BOOST_CHECK_EQUAL(es->suspendTasksCount(), 0);
    }

    {
        // response-parsing paths driven directly through the extracted
        // onUnsubscribeResponse: the wrapped real session is not connected, so
        // the success callback cannot be triggered by a network round-trip.
        // These calls exercise the fromJson/status handling of the response.
        auto resp = std::make_shared<bcos::cppsdk::event::EventSubResponse>();
        resp->setId(id);
        resp->setStatus(0);
        auto respJson = resp->generateJson();

        // success path: a valid response parses without throwing
        bcos::boostssl::ws::WsMessage msg;
        msg.setSeq("seq");
        msg.setPayload(bcos::bytes(respJson.begin(), respJson.end()));
        es->onUnsubscribeResponse(id, nullptr, std::move(msg));

        // invalid response path: parse failure must not throw
        std::string invalidJson = "not-a-json";
        bcos::boostssl::ws::WsMessage invalid;
        invalid.setPayload(bcos::bytes(invalidJson.begin(), invalidJson.end()));
        es->onUnsubscribeResponse(id, nullptr, std::move(invalid));

        // error path: a non-null error short-circuits before parsing
        es->onUnsubscribeResponse(
            id, BCOS_ERROR_PTR(-1, "disconnected"), bcos::boostssl::ws::WsMessage());
    }
}

BOOST_AUTO_TEST_SUITE_END()