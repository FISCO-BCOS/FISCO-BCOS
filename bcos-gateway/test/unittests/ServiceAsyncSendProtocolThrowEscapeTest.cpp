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
 * @brief Regression test for round-3 review finding 1: Service::sendProtocol must not let a
 *        synchronous pre-send rejection (outgoing rate limit / max size) escape task::wait and
 *        abort Service::onConnect's session-registration tail (the session would stay
 *        live-but-unregistered, silently hidden from the routing layer).
 * @file ServiceAsyncSendProtocolThrowEscapeTest.cpp
 * @date 2026-08-25
 *
 * The pre-send checks on the send path (P2PSession::fastSendP2PMessage's rate limit via
 * Service::onBeforeMessage, the session's allowMaxMsgSize / write failures) run synchronously on
 * the caller thread and BOOST_THROW_EXCEPTION. That exception propagates out of
 * task::wait synchronously (the nested co_await chain unwinds inside AsyncTask::start()). If
 * sendProtocol did not catch it, onConnect's lines after the handshake call —
 * updateStaticNodes, m_sessions[p2pID] = p2pSession, callNewSessionHandlers — would all be
 * skipped, leaving a started/live socket that the routing layer cannot see. This test is RED on
 * the pre-fix code (the rejection escapes) and GREEN after (caught inside the coroutine).
 */

#include "bcos-framework/gateway/GatewayTypeDef.h"
#include "bcos-framework/protocol/GlobalConfig.h"
#include "bcos-gateway/libnetwork/ASIOInterface.h"
#include "bcos-gateway/libnetwork/Host.h"
#include "bcos-gateway/libnetwork/Socket.h"
#include "bcos-gateway/libp2p/Message.h"
#include "bcos-gateway/libp2p/P2PDecoder.h"
#include "bcos-gateway/libp2p/P2PSession.h"
#include "bcos-gateway/libp2p/Service.h"
#include "bcos-tars-protocol/protocol/ProtocolInfoCodecImpl.h"
#include "bcos-utilities/IOServicePool.h"
#include "bcos-utilities/testutils/TestPromptFixture.h"
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <chrono>
#include <thread>

using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::test;

namespace ba = boost::asio;
namespace bi = boost::asio::ip;

BOOST_FIXTURE_TEST_SUITE(ServiceSendProtocolThrowEscapeTest, TestPromptFixture)

namespace
{
// Service::sendProtocol is protected; expose it for the test through a subclass (same pattern
// as the FIB-186 lock-order tests).
class ProbeService : public Service
{
public:
    explicit ProbeService(P2PInfo const& _info) : Service(_info) {}
    using Service::sendProtocol;
};

// A Host<P2PDecoder> with the network marked up so Session::active() holds (haveNetwork()).
class TestHost : public Host<P2PDecoder>
{
public:
    TestHost(bcos::crypto::Hash::Ptr _hash, std::shared_ptr<ASIOInterface> _asioInterface)
      : Host<P2PDecoder>(std::move(_hash), std::move(_asioInterface), nullptr)
    {
        m_run = true;
    }
};
}  // namespace

BOOST_AUTO_TEST_CASE(SendProtocolDoesNotEscapeSendRejection)
{
    // sendProtocol encodes the local protocol via g_BCOSConfig's codec — the same global the
    // Service constructor reads. Production initializers set it before building the gateway; the
    // unit-test harness does not, so set it here (idempotent).
    bcos::protocol::g_BCOSConfig.setCodec(
        std::make_shared<bcostars::protocol::ProtocolInfoCodecImpl>());

    P2PInfo selfInfo;
    selfInfo.rawP2pID = "selfRawP2pID";
    selfInfo.p2pID = "selfP2pID";
    auto service = std::make_shared<ProbeService>(selfInfo);

    // The injection point moved with the de-facing refactor: SessionFace is gone, so the
    // synchronous pre-send rejection is driven through the same hook production uses —
    // Service::beforeMessageHandler, invoked from P2PSession::fastSendP2PMessage right before the
    // write — over a REAL active Session. This is strictly closer to the production path than the
    // old SessionFace fake (it exercises the actual fastSendP2PMessage prologue).
    service->setBeforeMessageHandler(
        [](Session&, const Message&, uint32_t) -> std::optional<bcos::Error> {
            return bcos::Error::buildError(
                "", P2PExceptionType::OutBWOverflow, "outgoing bandwidth overflow");
        });

    // Real loopback pair: fastSendP2PMessage gates on session->active(), which needs a connected
    // socket, a running io_context and a live host. The send never reaches the wire — the
    // beforeMessageHandler above rejects it pre-send.
    auto io = std::make_shared<ba::io_context>();
    boost::asio::executor_work_guard<ba::io_context::executor_type> workGuard(io->get_executor());
    std::thread ioThread([io] { io->run(); });

    ba::ip::tcp::acceptor acceptor(*io, ba::ip::tcp::endpoint(ba::ip::tcp::v4(), 0));
    ba::ip::tcp::socket client(*io);
    boost::system::error_code connectError;
    client.connect(acceptor.local_endpoint(), connectError);
    BOOST_REQUIRE(!connectError);
    // Kept open until the session is destroyed so drop()'s ssl async_shutdown completes.
    ba::ip::tcp::socket serverSide(*io);
    acceptor.accept(serverSide);

    auto testHost = std::make_shared<TestHost>(nullptr,
        std::make_shared<ASIOInterface>(
            std::make_shared<bcos::IOServicePool>(1, "sendProtocolTest"), "0.0.0.0", 0));
    // Service::newSeq() delegates to the host-wide seq allocator
    service->setHost(testHost);

    ba::ssl::context sslContext(ba::ssl::context::tlsv12);
    std::atomic<bool> teardownNotified{false};
    {
        auto sessionSocket = std::make_shared<Socket>(io, sslContext, NodeIPEndpoint());
        sessionSocket->ref() = std::move(client);
        auto session = std::make_shared<Session>(sessionSocket, *testHost);
        // Tolerant handler: disconnect()'s teardown notification lands here.
        session->setMessageHandler(
            [&teardownNotified](NetworkException, Session::Ptr, FrameMeta) {
                teardownNotified.store(true);
            });
        session->start();
        BOOST_REQUIRE(session->active());

        auto p2pSession = std::make_shared<P2PSession>();
        p2pSession->setSession(session);
        p2pSession->setService(service);
        p2pSession->setProtocolInfo(
            g_BCOSConfig.protocolInfo(bcos::protocol::ProtocolModuleID::GatewayService));

        // Pre-fix: the handshake rejection escapes task::wait and propagates out of sendProtocol
        // (synchronously aborting onConnect's registration tail). Post-fix: caught inside the
        // coroutine and logged — a failed handshake is a recoverable per-session failure and the
        // session registration in onConnect must proceed.
        BOOST_CHECK_NO_THROW(service->sendProtocol(p2pSession));

        session->disconnect(DisconnectReason::DisconnectRequested);
        // The read loop armed by start() unwinds on the io thread once the socket closes; the
        // teardown notification runs on the host's teardown executor. Wait for it so no coroutine
        // or handler outlives the io_context.
        for (int i = 0; i < 200 && !teardownNotified.load(); ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    workGuard.reset();
    {
        boost::system::error_code ec;
        serverSide.close(ec);
        acceptor.close(ec);
    }
    io->stop();
    ioThread.join();
}

BOOST_AUTO_TEST_SUITE_END()
