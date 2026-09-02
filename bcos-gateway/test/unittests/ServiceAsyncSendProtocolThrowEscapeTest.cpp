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

#include "bcos-framework/gateway/GatewayTypeDef.h"
#include "bcos-framework/protocol/GlobalConfig.h"
#include "bcos-gateway/libnetwork/SessionFace.h"
#include "bcos-gateway/libnetwork/SocketFace.h"
#include "bcos-gateway/libp2p/P2PMessage.h"
#include "bcos-gateway/libp2p/P2PMessageV2.h"
#include "bcos-gateway/libp2p/P2PSession.h"
#include "bcos-gateway/libp2p/Service.h"
#include "bcos-tars-protocol/protocol/ProtocolInfoCodecImpl.h"
#include "bcos-utilities/testutils/TestPromptFixture.h"
#include <boost/test/unit_test.hpp>

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

// A SessionFace whose fastSendMessage rejects synchronously — the same way Session::
// fastSendMessage throws NetworkException for a rate-limit / oversize rejection before any
// suspension. P2PSession::fastSendP2PMessage therefore throws synchronously out of the co_await,
// which (pre-fix) escaped task::wait inside Service::asyncSendProtocol.
class RejectingSession : public SessionFace
{
public:
    void start() override {}
    void disconnect(DisconnectReason) override {}
    task::Task<Message::Ptr> fastSendMessage(const Message& /*header*/,
        ::ranges::any_view<bytesConstRef> /*payloads*/, Options /*options*/) override
    {
        BOOST_THROW_EXCEPTION(NetworkException(-1, "outgoing bandwidth overflow"));
        co_return nullptr;
    }
    std::shared_ptr<SocketFace> socket() override { return nullptr; }
    void setMessageHandler(
        std::function<void(NetworkException, SessionFace::Ptr, Message::Ptr)>) override
    {}
    void setBeforeMessageHandler(std::function<std::optional<bcos::Error>(
        SessionFace&, const Message&, uint32_t)>) override
    {}
    NodeIPEndpoint nodeIPEndpoint() const override { return {}; }
    bool active() const override { return true; }
    std::size_t writeQueueSize() override { return 0; }
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

    auto p2pSession = std::make_shared<P2PSession>();
    p2pSession->setSession(std::make_shared<RejectingSession>());
    p2pSession->setService(service);
    p2pSession->setProtocolInfo(
        g_BCOSConfig.protocolInfo(bcos::protocol::ProtocolModuleID::GatewayService));

    // Pre-fix: the handshake rejection escapes task::wait and propagates out of asyncSendProtocol
    // (synchronously aborting onConnect's registration tail). Post-fix: caught inside the
    // coroutine and logged — a failed handshake is a recoverable per-session failure and the
    // session registration in onConnect must proceed.
    BOOST_CHECK_NO_THROW(service->sendProtocol(p2pSession));
}

BOOST_AUTO_TEST_SUITE_END()
