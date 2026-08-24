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
 * @brief Regression tests for FIB-184: cap sessions and bound per-session recv buffer
 * @file FIB184_SessionResourceCapTest.cpp
 * @date 2026-06-15
 */

#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-gateway/libnetwork/ASIOInterface.h"
#include "bcos-gateway/libnetwork/Host.h"
#include "bcos-gateway/libnetwork/Session.h"
#include "bcos-utilities/IOServicePool.h"
#include "bcos-utilities/testutils/TestPromptFixture.h"

#include <boost/test/unit_test.hpp>
using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::test;
using namespace bcos::crypto;

namespace ba = boost::asio;
namespace bi = boost::asio::ip;

BOOST_FIXTURE_TEST_SUITE(FIB184_SessionResourceCapTest, TestPromptFixture)

class FakeASIO_FIB184 : public bcos::gateway::ASIOInterface
{
public:
    // ASIOInterface now owns its IOServicePool and is stopped by ~IOServicePool; the
    // strandPost / stop virtuals this fake used to override no longer exist.
    FakeASIO_FIB184()
      : ASIOInterface(std::make_shared<bcos::IOServicePool>(1, "FakeASIO_FIB184"), "0.0.0.0", 0)
    {}
    ~FakeASIO_FIB184() noexcept override {}
    void asyncReadSome(
        const std::shared_ptr<SocketFace>&, ba::mutable_buffer, ReadWriteHandler) override
    {}
};

class FakeSocket_FIB184 : public SocketFace
{
public:
    FakeSocket_FIB184()
      : m_ioContext(std::make_shared<ba::io_context>()),
        m_sslContext(ba::ssl::context::tlsv12),
        m_sslSocket(std::make_shared<ba::ssl::stream<bi::tcp::socket>>(*m_ioContext, m_sslContext))
    {}
    ~FakeSocket_FIB184() override = default;

    bool isConnected() const override { return m_connected; }
    void close() override { m_connected = false; }
    bi::tcp::endpoint remoteEndpoint(boost::system::error_code) override { return {}; }
    bi::tcp::endpoint localEndpoint(boost::system::error_code) override { return {}; }
    bi::tcp::socket& ref() override { return m_sslSocket->next_layer(); }
    ba::ssl::stream<bi::tcp::socket>& sslref() override { return *m_sslSocket; }
    const NodeIPEndpoint& nodeIPEndpoint() const override { return m_nodeIPEndpoint; }
    void setNodeIPEndpoint(NodeIPEndpoint _nodeIPEndpoint) override
    {
        m_nodeIPEndpoint = std::move(_nodeIPEndpoint);
    }
    ba::io_context& ioService() override { return *m_ioContext; }

    bool m_connected{true};

private:
    std::shared_ptr<ba::io_context> m_ioContext;
    ba::ssl::context m_sslContext;
    std::shared_ptr<ba::ssl::stream<bi::tcp::socket>> m_sslSocket;
    NodeIPEndpoint m_nodeIPEndpoint;
};

// Exposes the protected session-cap helpers for direct testing.
class FakeHost_FIB184 : public bcos::gateway::Host
{
public:
    FakeHost_FIB184(bcos::crypto::Hash::Ptr _hash, std::shared_ptr<ASIOInterface> _asioInterface)
      : Host(_hash, _asioInterface, nullptr, nullptr)
    {
        m_run = true;
    }
    bool callTryAcquireSessionSlot(std::string const& addr) { return tryAcquireSessionSlot(addr); }
    void callReleaseSessionSlot(std::string const& addr) { releaseSessionSlot(addr); }
};

// FIB-184 Fix 2: a forced-size session uses exactly the requested recv buffer size, not the
// 512KB floor that was unconditionally applied before.
BOOST_AUTO_TEST_CASE(ForcedSmallRecvBufferIsHonored)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto fakeSocket = std::make_shared<FakeSocket_FIB184>();
    auto fakeAsio = std::make_shared<FakeASIO_FIB184>();
    auto fakeHost = std::make_shared<FakeHost_FIB184>(hashImpl, fakeAsio);

    auto session = std::make_shared<Session>(fakeSocket, *fakeHost, /*size*/ 2, /*forceSize*/ true);
    BOOST_CHECK_EQUAL(session->recvBuffer().recvBufferSize(), 2u);
    // grow ceiling is still the 512KB minimum
    BOOST_CHECK_EQUAL(session->maxRecvBufferSize(), Session::MIN_SESSION_RECV_BUFFER_SIZE);

    session->setSocket(nullptr);
    fakeSocket->close();
}

// FIB-184 Fix 2: a default-constructed session no longer allocates the 512KB floor up front;
// it starts at the (much smaller) lazy initial size and relies on the grow path.
BOOST_AUTO_TEST_CASE(DefaultRecvBufferStartsSmall)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto fakeSocket = std::make_shared<FakeSocket_FIB184>();
    auto fakeAsio = std::make_shared<FakeASIO_FIB184>();
    auto fakeHost = std::make_shared<FakeHost_FIB184>(hashImpl, fakeAsio);

    auto session = std::make_shared<Session>(fakeSocket, *fakeHost);
    BOOST_CHECK_EQUAL(
        session->recvBuffer().recvBufferSize(), Session::INITIAL_SESSION_RECV_BUFFER_SIZE);
    BOOST_CHECK_LT(
        Session::INITIAL_SESSION_RECV_BUFFER_SIZE, Session::MIN_SESSION_RECV_BUFFER_SIZE);
    // the grow ceiling stays at the 512KB minimum so large messages still fit after growth
    BOOST_CHECK_EQUAL(session->maxRecvBufferSize(), Session::MIN_SESSION_RECV_BUFFER_SIZE);

    session->setSocket(nullptr);
    fakeSocket->close();
}

// FIB-184: production createSession passes the config-validated ceiling (forced to
// 2 * allow_max_msg_size = 64MB), NOT forceSize. The session must allocate only the small initial
// buffer and keep the 64MB as the grow ceiling -- not preallocate 64MB per session (the
// heap-exhaustion crash source). This is the production path the two tests above do not cover.
BOOST_AUTO_TEST_CASE(ProductionLargeRecvBufferDoesNotPreallocate)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto fakeSocket = std::make_shared<FakeSocket_FIB184>();
    auto fakeAsio = std::make_shared<FakeASIO_FIB184>();
    auto fakeHost = std::make_shared<FakeHost_FIB184>(hashImpl, fakeAsio);

    constexpr size_t k64MB =
        64UL * 1024 * 1024;  // == 2 * MAX_MESSAGE_LENGTH, the production ceiling
    auto session =
        std::make_shared<Session>(fakeSocket, *fakeHost, /*ceiling*/ k64MB, /*forceSize*/ false);

    // initial allocation is the lazy 16KB, NOT the 64MB ceiling
    BOOST_CHECK_EQUAL(
        session->recvBuffer().recvBufferSize(), Session::INITIAL_SESSION_RECV_BUFFER_SIZE);
    // the grow ceiling still reflects the requested 64MB so large messages fit after growth
    BOOST_CHECK_EQUAL(session->maxRecvBufferSize(), k64MB);

    session->setSocket(nullptr);
    fakeSocket->close();
}

// FIB-184 Fix 1: per-IP cap rejects slot acquisition once the limit is hit; releasing a slot
// makes room again.
BOOST_AUTO_TEST_CASE(PerIPSessionCapIsEnforced)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto fakeAsio = std::make_shared<FakeASIO_FIB184>();
    auto fakeHost = std::make_shared<FakeHost_FIB184>(hashImpl, fakeAsio);

    fakeHost->setMaxSessionsPerIP(3);
    fakeHost->setMaxConcurrentSessions(100);

    const std::string ip = "1.2.3.4";
    BOOST_CHECK(fakeHost->callTryAcquireSessionSlot(ip));
    BOOST_CHECK(fakeHost->callTryAcquireSessionSlot(ip));
    BOOST_CHECK(fakeHost->callTryAcquireSessionSlot(ip));
    // 4th from the same IP is rejected
    BOOST_CHECK(!fakeHost->callTryAcquireSessionSlot(ip));
    BOOST_CHECK_EQUAL(fakeHost->currentSessionCount(), 3u);

    // a different IP is unaffected
    BOOST_CHECK(fakeHost->callTryAcquireSessionSlot("5.6.7.8"));
    BOOST_CHECK_EQUAL(fakeHost->currentSessionCount(), 4u);

    // releasing one slot for the capped IP makes room for exactly one more
    fakeHost->callReleaseSessionSlot(ip);
    BOOST_CHECK_EQUAL(fakeHost->currentSessionCount(), 3u);
    BOOST_CHECK(fakeHost->callTryAcquireSessionSlot(ip));
    BOOST_CHECK(!fakeHost->callTryAcquireSessionSlot(ip));
}

// FIB-184 Fix 1: global concurrent-session cap rejects once the total limit is hit, even across
// distinct source IPs.
BOOST_AUTO_TEST_CASE(GlobalSessionCapIsEnforced)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto fakeAsio = std::make_shared<FakeASIO_FIB184>();
    auto fakeHost = std::make_shared<FakeHost_FIB184>(hashImpl, fakeAsio);

    fakeHost->setMaxConcurrentSessions(2);
    fakeHost->setMaxSessionsPerIP(100);

    BOOST_CHECK(fakeHost->callTryAcquireSessionSlot("10.0.0.1"));
    BOOST_CHECK(fakeHost->callTryAcquireSessionSlot("10.0.0.2"));
    // global cap reached
    BOOST_CHECK(!fakeHost->callTryAcquireSessionSlot("10.0.0.3"));
    BOOST_CHECK_EQUAL(fakeHost->currentSessionCount(), 2u);

    fakeHost->callReleaseSessionSlot("10.0.0.1");
    BOOST_CHECK(fakeHost->callTryAcquireSessionSlot("10.0.0.3"));
    BOOST_CHECK_EQUAL(fakeHost->currentSessionCount(), 2u);
}

// FIB-184 Fix 1: the lifetime guard releases the slot exactly when the session is destroyed.
BOOST_AUTO_TEST_CASE(LifetimeGuardReleasesSlotOnSessionDestruction)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto fakeSocket = std::make_shared<FakeSocket_FIB184>();
    auto fakeAsio = std::make_shared<FakeASIO_FIB184>();
    auto fakeHost = std::make_shared<FakeHost_FIB184>(hashImpl, fakeAsio);

    const std::string ip = "9.9.9.9";
    BOOST_CHECK(fakeHost->callTryAcquireSessionSlot(ip));
    BOOST_CHECK_EQUAL(fakeHost->currentSessionCount(), 1u);

    {
        auto session = std::make_shared<Session>(fakeSocket, *fakeHost, 2, true);
        // Attach a guard that releases the slot when the session is destroyed, mirroring
        // Host::startPeerSession. Capture the host by weak_ptr so the lambda guard is safe.
        std::weak_ptr<Host> weakHost = fakeHost;
        session->setLifetimeGuard(std::shared_ptr<void>(nullptr, [weakHost, ip](void*) {
            if (auto h = weakHost.lock())
            {
                // FakeHost exposes the protected release helper.
                std::static_pointer_cast<FakeHost_FIB184>(h)->callReleaseSessionSlot(ip);
            }
        }));
        BOOST_CHECK_EQUAL(fakeHost->currentSessionCount(), 1u);
        session->setSocket(nullptr);
    }
    // After the session (and its guard) is destroyed, the slot is released.
    BOOST_CHECK_EQUAL(fakeHost->currentSessionCount(), 0u);

    fakeSocket->close();
}

BOOST_AUTO_TEST_SUITE_END()
