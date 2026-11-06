/*
 *  Copyright (C) 2026 FISCO BCOS.
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
 */

#include "bcos-gateway/libnetwork/Host.h"
#include "bcos-task/Wait.h"
#include "bcos-utilities/testutils/TestPromptFixture.h"
#include <boost/test/unit_test.hpp>
#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <utility>

using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::test;

namespace
{
class AcceptTestHost : public Host
{
public:
    AcceptTestHost() : Host(nullptr, nullptr, nullptr) { m_run = true; }

    bool enqueue(P2PInfo p2pInfo)
    {
        return m_acceptChannel.push(
            AcceptedConnection{std::move(p2pInfo), std::shared_ptr<SessionFace>()});
    }
};
}  // namespace

BOOST_FIXTURE_TEST_SUITE(HostAcceptChannelTest, TestPromptFixture)

BOOST_AUTO_TEST_CASE(queuedConnectionCanBeAccepted)
{
    auto host = std::make_shared<AcceptTestHost>();
    P2PInfo p2pInfo;
    p2pInfo.rawP2pID = "peer";
    BOOST_CHECK(host->enqueue(std::move(p2pInfo)));

    auto connection = task::syncWait(host->acceptSession());
    BOOST_CHECK_EQUAL(connection.p2pInfo.rawP2pID, "peer");
    BOOST_CHECK(!connection.session);
    host->stop();
}

BOOST_AUTO_TEST_CASE(stopWakesParkedAcceptSession)
{
    auto host = std::make_shared<AcceptTestHost>();
    std::promise<std::string> result;
    auto future = result.get_future();

    task::wait(
        [](std::shared_ptr<Host> host, std::promise<std::string>& result) -> task::Task<void> {
            try
            {
                co_await host->acceptSession();
                result.set_value("no exception");
            }
            catch (NetworkException const& e)
            {
                result.set_value(e.what());
            }
        }(host, result));

    host->stop();
    BOOST_REQUIRE(future.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    BOOST_CHECK_EQUAL(future.get(), "HostStopped");
}

BOOST_AUTO_TEST_SUITE_END()
