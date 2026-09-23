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
#include "bcos-gateway/libp2p/Message.h"
#include "bcos-gateway/libp2p/P2PDecoder.h"
#include "bcos-utilities/testutils/TestPromptFixture.h"
#include <bcos-utilities/IOServicePool.h>
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
    FakeASIO_FIB97new()
      : ASIOInterface(std::make_shared<bcos::IOServicePool>(1, "FakeASIO_FIB97new"), "0.0.0.0", 0)
    {}
    ~FakeASIO_FIB97new() noexcept = default;
};

class FakeSocket_FIB97new
{
public:
    FakeSocket_FIB97new()
      : m_ioContext(std::make_shared<boost::asio::io_context>()),
        m_sslContext(boost::asio::ssl::context::tlsv12),
        m_sslSocket(std::make_shared<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>(
            *m_ioContext, m_sslContext))
    {}
    ~FakeSocket_FIB97new() = default;

    bool isConnected() const { return m_connected.load(); }
    void close()
    {
        m_connected.store(false);
        ++m_closeCount;
    }
    boost::asio::ip::tcp::endpoint remoteEndpoint(boost::system::error_code /*ec*/ = {})
    {
        return {};
    }
    boost::asio::ip::tcp::endpoint localEndpoint(boost::system::error_code /*ec*/ = {})
    {
        return {};
    }
    boost::asio::ip::tcp::socket& ref() { return m_sslSocket->next_layer(); }
    boost::asio::ssl::stream<boost::asio::ip::tcp::socket>& sslref()
    {
        return *m_sslSocket;
    }
    const NodeIPEndpoint& nodeIPEndpoint() const { return m_nodeIPEndpoint; }
    void setNodeIPEndpoint(NodeIPEndpoint /*unused*/) {}
    boost::asio::io_context& ioService() { return *m_ioContext; }

    // Counts to detect double-teardown
    std::atomic<int> m_closeCount{0};
    std::atomic<bool> m_connected{true};

private:
    std::shared_ptr<boost::asio::io_context> m_ioContext;
    boost::asio::ssl::context m_sslContext;
    std::shared_ptr<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>> m_sslSocket;
    NodeIPEndpoint m_nodeIPEndpoint;
};

class FakeHost_FIB97new : public bcos::gateway::Host<P2PDecoder, FakeSocket_FIB97new>
{
public:
    FakeHost_FIB97new(std::shared_ptr<ASIOInterface> _asioInterface,
        std::shared_ptr<BasicSessionFactory<P2PDecoder, FakeSocket_FIB97new>> _sessionFactory)
      : Host<P2PDecoder, FakeSocket_FIB97new>(
            std::move(_asioInterface), std::move(_sessionFactory))
    {
        this->m_run = true;
    }
};

using Session_FIB97new = BasicSession<P2PDecoder, FakeSocket_FIB97new>;

// Session owns a reference_wrapper<Host> — the Host must outlive the session.
// Return both from the helper so tests keep the host alive.
struct SessionBundle_FIB97new
{
    std::shared_ptr<FakeHost_FIB97new> host;
    std::shared_ptr<FakeSocket_FIB97new> socket;
    std::shared_ptr<Session_FIB97new> session;
};

inline SessionBundle_FIB97new makeSessionFib97new()
{
    auto fakeSocket = std::make_shared<FakeSocket_FIB97new>();
    auto fakeAsio = std::make_shared<FakeASIO_FIB97new>();
    auto fakeHost = std::make_shared<FakeHost_FIB97new>(fakeAsio, nullptr);

    auto session = std::make_shared<Session_FIB97new>(fakeSocket, *fakeHost, 2, true);
    session->setMessageHandler(
        [](NetworkException /*e*/, Session_FIB97new::Ptr /*s*/, FrameMeta /*m*/) {});

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

    // First drop: wins the m_dropped CAS and posts the (single) socket teardown.
    bundle.session->drop(DisconnectReason::TCPError);
    // Second drop: must be a no-op (CAS fails), so it posts no second teardown.
    bundle.session->drop(DisconnectReason::TCPError);

    // drop() marks the session inactive synchronously.
    BOOST_CHECK(!bundle.session->active());

    // FIB-184: the actual socket close/shutdown is deferred onto the socket's own io_context so it
    // can never race an in-flight async_read_some/async_write. Drain that io_context to run the
    // deferred teardown; only the CAS winner posted one, so close() must run exactly once.
    bundle.socket->ioService().poll();

    // The socket must have been closed exactly once — a second drop re-invoking socket close is a
    // sign of double-teardown. close() in FakeSocket increments m_closeCount.
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
    // FIB-184: run the deferred teardown posted by the single CAS winner (see sequential case).
    bundle.socket->ioService().poll();
    BOOST_CHECK_EQUAL(bundle.socket->m_closeCount.load(), 1);
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
    // FIB-184: run the deferred teardown posted by the single CAS winner (see sequential case).
    bundle.socket->ioService().poll();
    BOOST_CHECK_EQUAL(bundle.socket->m_closeCount.load(), 1);
}

BOOST_AUTO_TEST_SUITE_END()
