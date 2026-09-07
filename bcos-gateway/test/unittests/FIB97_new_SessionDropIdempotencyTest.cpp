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
#include "bcos-gateway/libp2p/P2PMessage.h"
#include "bcos-utilities/testutils/TestPromptFixture.h"
#include <bcos-utilities/IOServicePool.h>
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <chrono>
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
};

// Socket is a concrete class now, so the tests drive the real thing: a Socket whose SSL stream
// sits on a connected loopback TCP pair (the acceptor side closes at scope exit). A real close()
// really disconnects the socket, so a double-teardown's second closeSocket() early-returns on
// isConnected() — the "close exactly once" property is instead asserted through the teardown
// notification counter in SessionBundle_FIB97new (the notification is posted once iff drop()'s
// CAS admitted exactly one teardown).
struct FakeSocket_FIB97new
{
    std::shared_ptr<boost::asio::io_context> ioContext =
        std::make_shared<boost::asio::io_context>();
    boost::asio::ssl::context sslContext{boost::asio::ssl::context::tlsv12};
    std::shared_ptr<Socket> socket =
        std::make_shared<Socket>(ioContext, sslContext, NodeIPEndpoint());

    FakeSocket_FIB97new()
    {
        boost::asio::ip::tcp::acceptor acceptor(
            *ioContext, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), 0));
        socket->ref().connect(acceptor.local_endpoint());
        boost::asio::ip::tcp::socket serverSocket(*ioContext);
        acceptor.accept(serverSocket);
    }
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
    // teardown-notification count: drop() posts exactly one notification iff its CAS admitted
    // exactly one teardown — the observable proxy for "socket teardown ran exactly once" now
    // that the socket is a real Socket whose close() cannot be counted from outside.
    std::shared_ptr<std::atomic<int>> notifyCount;
};

inline SessionBundle_FIB97new makeSessionFib97new()
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto fakeSocket = std::make_shared<FakeSocket_FIB97new>();
    auto fakeAsio = std::make_shared<FakeASIO_FIB97new>();
    auto msgFactory = std::make_shared<P2PMessageFactory>();
    auto fakeHost = std::make_shared<FakeHost_FIB97new>(hashImpl, fakeAsio, nullptr, msgFactory);

    auto notifyCount = std::make_shared<std::atomic<int>>(0);
    auto session = std::make_shared<Session>(fakeSocket->socket, *fakeHost, 2, true);
    session->setMessageFactory(msgFactory);
    session->setMessageHandler(
        [notifyCount](NetworkException e, Session::Ptr /*s*/, Message::Ptr /*m*/) {
            if (e.errorCode() != 0)
            {
                ++(*notifyCount);
            }
        });

    return {fakeHost, fakeSocket, session, notifyCount};
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
    // deferred teardown; only the CAS winner posted one.
    bundle.socket->ioContext->poll();

    // The teardown notification must have fired exactly once — a second drop re-entering the
    // teardown body would post a second one. It runs on the host's dedicated teardown executor,
    // so wait (bounded) until it lands.
    for (int i = 0; i < 200 && bundle.notifyCount->load() == 0; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    BOOST_CHECK_EQUAL(bundle.notifyCount->load(), 1);
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
    bundle.socket->ioContext->poll();
    for (int i = 0; i < 200 && bundle.notifyCount->load() == 0; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    BOOST_CHECK_EQUAL(bundle.notifyCount->load(), 1);
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
    bundle.socket->ioContext->poll();
    for (int i = 0; i < 200 && bundle.notifyCount->load() == 0; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    BOOST_CHECK_EQUAL(bundle.notifyCount->load(), 1);
}

BOOST_AUTO_TEST_SUITE_END()
