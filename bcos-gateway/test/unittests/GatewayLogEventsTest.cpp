/**
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
 *
 * @brief gateway log events: the disconnect reason tag table, and a failed reconnect leaves only
 *        Host's own (rate-limited) event at INFO
 * @file GatewayLogEventsTest.cpp
 */
#include "bcos-gateway/libnetwork/Common.h"
#include "bcos-gateway/libp2p/Service.h"
#include <bcos-framework/testutils/LogCapture.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::test;

BOOST_FIXTURE_TEST_SUITE(GatewayLogEventsTest, TestPromptFixture)

BOOST_AUTO_TEST_CASE(disconnectReasonTags)
{
    BOOST_CHECK_EQUAL(std::string(disconnectReasonTag(DuplicatePeer)), "duplicate");
    BOOST_CHECK_EQUAL(std::string(disconnectReasonTag(ClientQuit)), "remote_close");
    BOOST_CHECK_EQUAL(std::string(disconnectReasonTag(DisconnectRequested)), "local_close");
    BOOST_CHECK_EQUAL(std::string(disconnectReasonTag(PingTimeout)), "timeout");
    BOOST_CHECK_EQUAL(std::string(disconnectReasonTag(InBlacklistReason)), "blacklist");
    BOOST_CHECK_EQUAL(std::string(disconnectReasonTag(TCPError)), "error");
}

// the heartbeat feeds every failed reconnect of a down static peer to onConnect; at INFO the
// only trace of it is Host's PeerConnectFailed, which is rate limited per endpoint
BOOST_AUTO_TEST_CASE(failedConnectPrintsNothingAtInfo)
{
    P2PInfo info;
    info.p2pID = "self";
    info.rawP2pID = "self";
    auto service = std::make_shared<Service>(info);
    setModuleLogLevel(LogModule::GATEWAY, LogLevel::INFO);
    LogCapture capture;
    service->onConnect(NetworkException(ConnectError, "Connect failed"), P2PInfo{}, nullptr);
    auto text = capture.text();
    resetModuleLogLevel(LogModule::GATEWAY);
    BOOST_CHECK_MESSAGE(text.find("onConnect") == std::string::npos, text);
}

BOOST_AUTO_TEST_SUITE_END()
