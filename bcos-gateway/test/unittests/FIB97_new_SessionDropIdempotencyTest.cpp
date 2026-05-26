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
 * @brief FIB-97-new: Session::drop() must be idempotent — safe to call more than once,
 *        sequentially or concurrently, without double-teardown or data races.
 * @file FIB97_new_SessionDropIdempotencyTest.cpp
 * @date 2026-05-08
 */

#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-gateway/libnetwork/ASIOInterface.h"
#include "bcos-gateway/libnetwork/Host.h"
#include "bcos-gateway/libnetwork/Session.h"
#include "bcos-gateway/libnetwork/Socket.h"
#include "bcos-gateway/libp2p/P2PMessage.h"
#include "bcos-utilities/ThreadPool.h"
#include "bcos-utilities/testutils/TestPromptFixture.h"
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <thread>

using namespace bcos;
using namespace gateway;
using namespace bcos::test;
using namespace bcos::crypto;

namespace bcos::test
{
namespace
{

// FIB-97-new: All helper names are suffixed _FIB97new to avoid ODR collisions under UNITY_BUILD.

class FakeASIO_FIB97new : public bcos::gateway::ASIOInterface
{
public:
    FakeASIO_FIB97new() = default;
    ~FakeASIO_FIB97new() noexcept override = default;

    // Never issues actual async reads — tests that call drop() don't need reads.
    void asyncReadSome(const std::shared_ptr<SocketFace>& /*socket*/,
        boost::asio::mutable_buffer /*buffers*/, ReadWriteHandler /*handler*/) override
    {}

    void strandPost(Base_Handler /*handler*/) override {}
    void stop() override {}
};

class FakeSocket_FIB97new : public SocketFace
{
public:
    FakeSocket_FIB97new()
      : SocketFace(),
        m_ioContext(std::make_shared<boost::asio::io_context>()),
        m_sslContext(boost::asio::ssl::context::tlsv12),
        m_sslSocket(std::make_shared<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>(
            *m_ioContext, m_sslContext))
    {}
    ~FakeSocket_FIB97new() override = default;

    bool isConnected() const override { return m_connected.load(); }
    void close() override
    {
        m_connected.store(false);
        ++m_closeCount;
    }
    boost::asio::ip::tcp::endpoint remoteEndpoint(boost::system::error_code /*ec*/) override
    {
        return {};
    }
    boost::asio::ip::tcp::endpoint localEndpoint(boost::system::error_code /*ec*/) override
    {
        return {};
    }
    boost::asio::ip::tcp::socket& ref() override { return m_sslSocket->next_layer(); }
    boost::asio::ssl::stream<boost::asio::ip::tcp::socket>& sslref() override
    {
        return *m_sslSocket;
    }
    const NodeIPEndpoint& nodeIPEndpoint() const override { return m_nodeIPEndpoint; }
    void setNodeIPEndpoint(NodeIPEndpoint /*unused*/) override {}
    boost::asio::io_context& ioService() override { return *m_ioContext; }

    // Counts to detect double-teardown
    std::atomic<int> m_closeCount{0};
    std::atomic<bool> m_connected{true};

private:
    std::shared_ptr<boost::asio::io_context> m_ioContext;
    boost::asio::ssl::context m_sslContext;
    std::shared_ptr<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>> m_sslSocket;
    NodeIPEndpoint m_nodeIPEndpoint;
};

class FakeHost_FIB97new : public bcos::gateway::Host
{
public:
    FakeHost_FIB97new(bcos::crypto::Hash::Ptr _hash, std::shared_ptr<ASIOInterface> _asioInterface,
        std::shared_ptr<SessionFactory> _sessionFactory, MessageFactory::Ptr _messageFactory)
      : Host(_hash, _asioInterface, _sessionFactory, _messageFactory)
    {
        m_run = true;
    }
};

// Session owns a reference_wrapper<Host> — the Host must outlive the session.
// Return both from the helper so tests keep the host alive.
struct SessionBundle_FIB97new
{
    std::shared_ptr<FakeHost_FIB97new> host;
    std::shared_ptr<FakeSocket_FIB97new> socket;
    std::shared_ptr<bcos::gateway::Session> session;
};

inline SessionBundle_FIB97new makeSessionFib97new()
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto fakeSocket = std::make_shared<FakeSocket_FIB97new>();
    auto fakeAsio = std::make_shared<FakeASIO_FIB97new>();
    auto msgFactory = std::make_shared<P2PMessageFactory>();
    auto fakeHost = std::make_shared<FakeHost_FIB97new>(hashImpl, fakeAsio, nullptr, msgFactory);

    auto session = std::make_shared<Session>(fakeSocket, *fakeHost, 2, true);
    session->setMessageFactory(msgFactory);
    session->setMessageHandler(
        [](NetworkException /*e*/, SessionFace::Ptr /*s*/, Message::Ptr /*m*/) {});

    return {fakeHost, fakeSocket, session};
}

}  // namespace
}  // namespace bcos::test

BOOST_FIXTURE_TEST_SUITE(FIB97newSessionDropIdempotencyTest, TestPromptFixture)

// FIB-97-new: Calling drop() twice sequentially must not double-teardown.
// Without the atomic idempotency guard, the second drop() re-enters teardown,
// resulting in a second close()+async_shutdown() on an already-torn-down socket.
BOOST_AUTO_TEST_CASE(drop_twice_sequential_no_double_teardown)
{
    auto bundle = bcos::test::makeSessionFib97new();
    BOOST_REQUIRE(bundle.session);
    BOOST_REQUIRE(bundle.socket);

    // First drop: should perform the full teardown.
    bundle.session->drop(DisconnectReason::TCPError);
    // Second drop: must be a no-op (not re-enter teardown).
    bundle.session->drop(DisconnectReason::TCPError);

    // The socket must have been closed exactly once — a second drop re-invoking
    // socket close is a sign of double-teardown.
    // NOTE: close() in FakeSocket sets m_connected = false and increments m_closeCount.
    // After the first drop the socket is already disconnected, so the second drop's
    // `if (m_socket->isConnected())` guard may also prevent re-entry even without the
    // new m_dropped guard — but the isConnected() check is not thread-safe and a race
    // between two concurrent callers can slip through.  The atomic CAS fix is the
    // correct guard.  The sequential check here still demonstrates the invariant.
    BOOST_CHECK(!bundle.session->active());
    BOOST_CHECK_EQUAL(bundle.socket->m_closeCount.load(), 1);
}

// FIB-97-new: Calling drop() concurrently from two threads must not data-race.
// Under TSan, without the atomic guard, both threads enter the teardown body
// simultaneously and produce a write-write race on m_active and the socket state.
BOOST_AUTO_TEST_CASE(drop_concurrent_two_threads_no_race)
{
    auto bundle = bcos::test::makeSessionFib97new();
    BOOST_REQUIRE(bundle.session);

    std::thread t1([&] { bundle.session->drop(DisconnectReason::TCPError); });
    std::thread t2([&] { bundle.session->drop(DisconnectReason::TCPError); });
    t1.join();
    t2.join();

    // No crash, no UAF, no TSan report.
    BOOST_CHECK(!bundle.session->active());
    BOOST_CHECK_LE(bundle.socket->m_closeCount.load(), 1);
}

// FIB-97-new: Eight threads all calling drop() concurrently — stress the CAS path.
BOOST_AUTO_TEST_CASE(drop_many_threads_stress)
{
    auto bundle = bcos::test::makeSessionFib97new();
    BOOST_REQUIRE(bundle.session);

    constexpr int kThreads = 8;
    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i)
    {
        threads.emplace_back([&] { bundle.session->drop(DisconnectReason::TCPError); });
    }
    for (auto& t : threads)
    {
        t.join();
    }

    BOOST_CHECK(!bundle.session->active());
    BOOST_CHECK_LE(bundle.socket->m_closeCount.load(), 1);
}

BOOST_AUTO_TEST_SUITE_END()
