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
 *
 * Migrated from bcos-gateway/test/unittests/FIB97_new_SessionDropIdempotencyTest.cpp: the
 * subject is the network session's teardown CAS, not gateway policy.
 */

#include "unittests/utils/TestAsioInterface.h"
#include "unittests/utils/TestFraming.h"
#include "unittests/utils/TestSessionSocket.h"
#include <bcos-network/ASIOInterface.h>
#include <bcos-network/Host.h>
#include <bcos-network/Session.h>
#include <bcos-utilities/IOServicePool.h>
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <memory>
#include <thread>
#include <vector>

using namespace bcos;
using namespace bcos::network;
using namespace bcos::network::test;

namespace bcos::network::test
{
namespace
{
class FakeHost_FIB97new : public bcos::network::Host<TestFrameDecoder, TestSessionSocket>
{
public:
    FakeHost_FIB97new(std::shared_ptr<bcos::network::ASIOInterface> _asioInterface,
        std::shared_ptr<bcos::network::BasicSessionFactory<TestFrameDecoder, TestSessionSocket>>
            _sessionFactory)
      : bcos::network::Host<TestFrameDecoder, TestSessionSocket>(
            std::move(_asioInterface), std::move(_sessionFactory))
    {
        this->m_run = true;
    }
};

using Session_FIB97new = bcos::network::BasicSession<TestFrameDecoder, TestSessionSocket>;

// Session owns a reference_wrapper<Host> — the Host must outlive the session.
// Return every piece from the helper so tests keep them alive.
struct SessionBundle_FIB97new
{
    std::shared_ptr<FakeHost_FIB97new> host;
    std::shared_ptr<TestSessionSocket> socket;
    std::shared_ptr<Session_FIB97new> session;
};

inline SessionBundle_FIB97new makeSessionFib97new()
{
    auto fakeSocket = std::make_shared<TestSessionSocket>();
    auto fakeAsio = std::make_shared<TestAsioInterface>();
    auto fakeHost = std::make_shared<FakeHost_FIB97new>(fakeAsio, nullptr);

    auto session = std::make_shared<Session_FIB97new>(fakeSocket, *fakeHost, 2, true);

    return {fakeHost, fakeSocket, session};
}
}  // namespace
}  // namespace bcos::network::test

BOOST_AUTO_TEST_SUITE(FIB97newSessionDropIdempotencyTest)

// FIB-97-new: Calling drop() twice sequentially must not double-teardown.
// Without the atomic idempotency guard, the second drop() re-enters teardown,
// resulting in a second close()+async_shutdown() on an already-torn-down socket.
BOOST_AUTO_TEST_CASE(drop_twice_sequential_no_double_teardown)
{
    auto bundle = makeSessionFib97new();
    BOOST_REQUIRE(bundle.session);
    BOOST_REQUIRE(bundle.socket);

    // First drop: wins the m_dropped CAS and posts the (single) socket teardown.
    bundle.session->drop(bcos::network::DisconnectReason::TCPError);
    // Second drop: must be a no-op (CAS fails), so it posts no second teardown.
    bundle.session->drop(bcos::network::DisconnectReason::TCPError);

    // drop() marks the session inactive synchronously.
    BOOST_CHECK(!bundle.session->active());

    // FIB-184: the actual socket close/shutdown is deferred onto the socket's own io_context so it
    // can never race an in-flight async_read_some/async_write. Drain that io_context to run the
    // deferred teardown; only the CAS winner posted one, so close() must run exactly once.
    bundle.socket->ioService().poll();

    // The socket must have been closed exactly once — a second drop re-invoking socket close is a
    // sign of double-teardown.
    BOOST_CHECK_EQUAL(bundle.socket->closeCount(), 1);
}

// FIB-97-new: Calling drop() concurrently from two threads must not data-race.
// Under TSan, without the atomic guard, both threads enter the teardown body
// simultaneously and produce a write-write race on m_active and the socket state.
BOOST_AUTO_TEST_CASE(drop_concurrent_two_threads_no_race)
{
    auto bundle = makeSessionFib97new();
    BOOST_REQUIRE(bundle.session);

    std::thread t1([&] { bundle.session->drop(bcos::network::DisconnectReason::TCPError); });
    std::thread t2([&] { bundle.session->drop(bcos::network::DisconnectReason::TCPError); });
    t1.join();
    t2.join();

    // No crash, no UAF, no TSan report.
    BOOST_CHECK(!bundle.session->active());
    // FIB-184: run the deferred teardown posted by the single CAS winner (see sequential case).
    bundle.socket->ioService().poll();
    BOOST_CHECK_EQUAL(bundle.socket->closeCount(), 1);
}

// FIB-97-new: Eight threads all calling drop() concurrently — stress the CAS path.
BOOST_AUTO_TEST_CASE(drop_many_threads_stress)
{
    auto bundle = makeSessionFib97new();
    BOOST_REQUIRE(bundle.session);

    constexpr int kThreads = 8;
    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i)
    {
        threads.emplace_back(
            [&] { bundle.session->drop(bcos::network::DisconnectReason::TCPError); });
    }
    for (auto& t : threads)
    {
        t.join();
    }

    BOOST_CHECK(!bundle.session->active());
    // FIB-184: run the deferred teardown posted by the single CAS winner (see sequential case).
    bundle.socket->ioService().poll();
    BOOST_CHECK_EQUAL(bundle.socket->closeCount(), 1);
}

BOOST_AUTO_TEST_SUITE_END()
