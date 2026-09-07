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
 * @brief Regression test for round-3 review finding 1: Service::asyncSendProtocol must not let a
 *        synchronous pre-send rejection (outgoing rate limit / max size) escape task::wait and
 *        abort Service::onConnect's session-registration tail (the session would stay
 *        live-but-unregistered, silently hidden from the routing layer).
 * @file ServiceAsyncSendProtocolThrowEscapeTest.cpp
 * @date 2026-08-25
 *
 * The pre-send checks in Session::fastSendMessage (allowMaxMsgSize / beforeMessageHandler) run
 * synchronously on the caller thread and BOOST_THROW_EXCEPTION. That exception propagates out of
 * task::wait synchronously (the nested co_await chain unwinds inside AsyncTask::start()). If
 * asyncSendProtocol did not catch it, onConnect's lines after the handshake call —
 * updateStaticNodes, m_sessions[p2pID] = p2pSession, callNewSessionHandlers — would all be
 * skipped, leaving a started/live socket that the routing layer cannot see. This test is RED on
 * the pre-fix code (the rejection escapes) and GREEN after (caught inside the coroutine).
 */

#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-framework/gateway/GatewayTypeDef.h"
#include "bcos-framework/protocol/GlobalConfig.h"
#include "bcos-gateway/libnetwork/ASIOInterface.h"
#include "bcos-gateway/libnetwork/Host.h"
#include "bcos-gateway/libnetwork/Session.h"
#include "bcos-gateway/libp2p/P2PMessage.h"
#include "bcos-gateway/libp2p/P2PMessageV2.h"
#include "bcos-gateway/libp2p/P2PSession.h"
#include "bcos-gateway/libp2p/Service.h"
#include "bcos-tars-protocol/protocol/ProtocolInfoCodecImpl.h"
#include "bcos-utilities/IOServicePool.h"
#include "bcos-utilities/testutils/TestPromptFixture.h"
#include <boost/test/unit_test.hpp>
#include <chrono>
#include <thread>

using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::test;

BOOST_FIXTURE_TEST_SUITE(ServiceAsyncSendProtocolThrowEscapeTest, TestPromptFixture)

namespace
{
// Service::asyncSendProtocol is protected; expose it for the test through a subclass (same pattern
// as the FIB-186 lock-order tests).
class ProbeService : public Service
{
public:
    explicit ProbeService(P2PInfo const& _info) : Service(_info) {}
    void sendProtocol(P2PSession::Ptr _session) { asyncSendProtocol(std::move(_session)); }
};

// Host with the network marked up, so a Session on it becomes/stays active.
class ProbeHost : public bcos::gateway::Host
{
public:
    ProbeHost(bcos::crypto::Hash::Ptr _hash, std::shared_ptr<ASIOInterface> _asioInterface)
      : Host(std::move(_hash), std::move(_asioInterface), nullptr, nullptr)
    {
        m_run = true;
    }
};
}  // namespace

BOOST_AUTO_TEST_CASE(AsyncSendProtocolDoesNotEscapeSendRejection)
{
    // asyncSendProtocol encodes the local protocol via g_BCOSConfig's codec — the same global the
    // Service constructor reads. Production initializers set it before building the gateway; the
    // unit-test harness does not, so set it here (idempotent).
    bcos::protocol::g_BCOSConfig.setCodec(
        std::make_shared<bcostars::protocol::ProtocolInfoCodecImpl>());

    P2PInfo selfInfo;
    selfInfo.rawP2pID = "selfRawP2pID";
    selfInfo.p2pID = "selfP2pID";
    auto service = std::make_shared<ProbeService>(selfInfo);
    service->setMessageFactory(std::make_shared<P2PMessageFactoryV2>());

    // Session is a concrete class now, so the rejecting session is the REAL Session with a
    // rejecting beforeMessageHandler: its fastSendMessage throws NetworkException synchronously
    // for a rate-limit / oversize rejection before any suspension — the same throw the old
    // RejectingSession fake hardcoded. P2PSession::fastSendP2PMessage therefore throws
    // synchronously out of the co_await, which (pre-fix) escaped task::wait inside
    // Service::asyncSendProtocol.
    auto hashImpl = std::make_shared<bcos::crypto::Keccak256>();
    auto asioInterface = std::make_shared<ASIOInterface>(
        std::make_shared<bcos::IOServicePool>(1, "AsyncSendProtocolThrowEscape"), "0.0.0.0", 0);
    auto host = std::make_shared<ProbeHost>(hashImpl, asioInterface);

    // A real Socket on a connected loopback TCP pair; the acceptor side closes at scope exit, so
    // the read armed by start() completes with an error once the io_context runs (teardown below).
    auto io = std::make_shared<boost::asio::io_context>();
    boost::asio::ssl::context sslContext(boost::asio::ssl::context::tlsv12);
    auto socket = std::make_shared<Socket>(io, sslContext, NodeIPEndpoint());
    {
        boost::asio::ip::tcp::acceptor acceptor(
            *io, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), 0));
        socket->ref().connect(acceptor.local_endpoint());
        boost::asio::ip::tcp::socket serverSocket(*io);
        acceptor.accept(serverSocket);
    }

    auto session = std::make_shared<Session>(socket, *host, 1024, true);
    session->setBeforeMessageHandler(
        [](Session&, const Message&, uint32_t) -> std::optional<bcos::Error> {
            return bcos::Error::buildError("", -1, "outgoing bandwidth overflow");
        });
    session->start();

    auto p2pSession = std::make_shared<P2PSession>();
    p2pSession->setSession(session);
    p2pSession->setService(service);
    p2pSession->setProtocolInfo(
        g_BCOSConfig.protocolInfo(bcos::protocol::ProtocolModuleID::GatewayService));

    // Pre-fix: the handshake rejection escapes task::wait and propagates out of asyncSendProtocol
    // (synchronously aborting onConnect's registration tail). Post-fix: caught inside the
    // coroutine and logged — a failed handshake is a recoverable per-session failure and the
    // session registration in onConnect must proceed.
    BOOST_CHECK_NO_THROW(service->sendProtocol(p2pSession));

    // Teardown: run the socket's io_context so the armed read completes (the peer closed at
    // setup), the read loop drops the session and the deferred closeSocket runs; then stop.
    std::thread ioThread([io] { io->run(); });
    for (int i = 0; i < 200 && session->active(); ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    io->stop();
    ioThread.join();
}

BOOST_AUTO_TEST_SUITE_END()
