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
 * @brief Regression test for FIB-184: async read/write handlers must own the session
 * @file FIB184_SessionAsyncLifetimeTest.cpp
 * @date 2026-07-03
 *
 * CertiK re-review of commit 00ec1c8b1 found the earlier hardening (session caps, lazy recv
 * buffer, log-sink suppression) did not address the underlying use-after-free: the async read
 * handler captured only a weak_ptr<Session>, yet the buffer handed to async_read_some points into
 * Session::m_recvBuffer and the stream is Session::m_socket. A concurrent teardown on another
 * thread could destroy the Session — freeing the recv buffer — while the read was still in flight,
 * so ASIO wrote decrypted bytes into freed memory (SIGSEGV). The fix captures shared_from_this()
 * so an outstanding operation keeps the Session (and its buffer/socket) alive until it completes.
 *
 * This test drives a Session to the point where exactly one async read is in flight, drops every
 * external strong reference, and asserts the Session is still alive — i.e. the in-flight read owns
 * it. With the pre-fix weak_ptr capture the Session would already be destroyed here.
 *
 * Migrated from bcos-gateway/test/unittests/FIB184_SessionAsyncLifetimeTest.cpp: the subject is
 * the network session's lifetime contract. The read loop is parked through the shared test
 * ASIO fake's synchronous mode (utils/TestAsioInterface.h), so the test never touches a real
 * wire format.
 */

#include "unittests/utils/TestAsioInterface.h"
#include "unittests/utils/TestFraming.h"
#include "unittests/utils/TestSessionSocket.h"
#include <bcos-network/ASIOInterface.h>
#include <bcos-network/Host.h>
#include <bcos-network/Session.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/IOServicePool.h>
#include <boost/asio/error.hpp>
#include <boost/test/unit_test.hpp>
#include <chrono>
#include <future>
#include <optional>
#include <tuple>

using namespace bcos;
using namespace bcos::network;
using namespace bcos::network::test;

BOOST_AUTO_TEST_SUITE(FIB184_SessionAsyncLifetimeTest)

class FakeHost_Lifetime : public bcos::network::Host<TestFrameDecoder, TestSessionSocket>
{
public:
    FakeHost_Lifetime(std::shared_ptr<bcos::network::ASIOInterface> asioInterface,
        std::shared_ptr<bcos::network::BasicSessionFactory<TestFrameDecoder, TestSessionSocket>>
            sessionFactory)
      : bcos::network::Host<TestFrameDecoder, TestSessionSocket>(
            std::move(asioInterface), std::move(sessionFactory))
    {
        this->m_run = true;
    }

    // Simulate Host::stop() having already run, so haveNetwork() returns false and no
    // io_context is left to service posted handlers.
    void stopNetwork() { this->m_run = false; }
};

using Session_Lifetime = bcos::network::BasicSession<TestFrameDecoder, TestSessionSocket>;

// The regression: an in-flight async read must keep the Session alive after every external strong
// reference is dropped. Pre-fix (weak_ptr capture) the Session would be destroyed here, leaving
// async_read_some writing into a freed recv buffer.
BOOST_AUTO_TEST_CASE(InFlightReadKeepsSessionAlive)
{
    auto fakeSocket = std::make_shared<TestSessionSocket>();
    // Synchronous-park mode: startWithPolicy() parks exactly one read inline, and only
    // fireNextRead() completes it — the deterministic "hold a read in flight" harness.
    auto fakeAsio = std::make_shared<TestAsioInterface>(/*poolSize*/ 1, "FakeAsioLifetime",
        /*synchronousPark*/ true);
    auto fakeHost = std::make_shared<FakeHost_Lifetime>(fakeAsio, nullptr);

    // Filled by the consumer coroutine below when drop() closes the recv channel: the push-era
    // teardown handler is now the close error observed by a parked recvMessage().
    auto disconnectError = std::make_shared<std::promise<int64_t>>();
    auto disconnectResult = disconnectError->get_future();

    std::weak_ptr<Session_Lifetime> weakSession;
    {
        auto session = std::make_shared<Session_Lifetime>(fakeSocket, *fakeHost, 1024, true);
        weakSession = session;

        // Pull-mode consumer, parked on recvMessage() until the read loop delivers a frame or
        // drop() closes the channel with the teardown error. task::wait starts the coroutine
        // synchronously, so it is parked in the channel's waiter slot before this statement
        // returns. The session is captured as a RAW POINTER on purpose: a strong capture would
        // defeat the weakSession assertions below, and the channel's lifetime contract (close()
        // wakes every parked recv() before the channel may be destroyed, and the wake touches
        // only the consumer's coroutine frame -- see Channel.h) makes the raw pointer safe. The
        // promise travels by shared_ptr so a timed-out wait below cannot leave the resumed
        // coroutine writing into a dead stack local.
        task::wait([](Session_Lifetime* sessionPtr,
                       std::shared_ptr<std::promise<int64_t>> result) -> task::Task<void> {
            try
            {
                while (true)
                {
                    (void)co_await sessionPtr->recvMessage();
                }
            }
            catch (bcos::network::NetworkException const& e)
            {
                result->set_value(bcos::network::errorCodeOf(e));
            }
            catch (...)
            {
                result->set_value(-1);
            }
        }(session.get(), disconnectError));

        // startWithPolicy() arms the first read synchronously.
        session->startWithPolicy<TestAsioInterface::ReadPolicy>();

        BOOST_REQUIRE_MESSAGE(fakeAsio->hasPendingRead(),
            "Session::startWithPolicy() must arm exactly one async read");
        // `session` goes out of scope here: the in-flight read is now the only owner (the
        // consumer coroutine above holds only a raw pointer).
    }

    BOOST_CHECK_MESSAGE(weakSession.lock() != nullptr,
        "FIB-184: an in-flight async read must keep the Session (and its recv buffer/socket) "
        "alive; pre-fix weak_ptr capture would have destroyed it here");

    // Completing the read with EOF drives drop(): no re-arm, so the read loop releases its
    // frame's reference. drop() then hands out TWO pieces of deferred work, on two different
    // executors:
    //   1. closeSocket(), posted to the socket's own io_context -- drained by poll() below.
    //      Mark the socket disconnected first so closeSocket() early-returns (no real SSL
    //      shutdown here).
    //   2. the teardown notification: closing the recv channel wakes the parked consumer through
    //      the channel poster, which posts the resume to the shared IO pool (the host is still
    //      up) -- awaited via the promise below.
    fakeSocket->setConnected(false);
    fakeAsio->fireNextRead(boost::asio::error::eof, 0);
    fakeSocket->ioService().poll();

    // Piece 2 is why the release cannot be asserted the instant fireNextRead returns: the
    // consumer's resume is posted to the shared IO pool, so it runs asynchronously on a pool
    // thread. Wait on the promise instead of sleeping on it: once it is fulfilled, the consumer
    // coroutine has observed the channel close error and run to completion.
    BOOST_REQUIRE_MESSAGE(
        disconnectResult.wait_for(std::chrono::seconds(5)) == std::future_status::ready,
        "the parked recvMessage() was never woken -- drop() did not close the recv channel");
    BOOST_CHECK_EQUAL(
        disconnectResult.get(), static_cast<int64_t>(bcos::network::P2PExceptionType::Disconnect));

    BOOST_CHECK_MESSAGE(weakSession.expired(),
        "FIB-184: once the outstanding read and the deferred teardown complete, the Session must "
        "be released (no leak / self-reference cycle)");
}

// Shutdown-path regression: Service::stop() calls Host::stop() -- which stops and JOINS the
// io_context threads via IOServicePool::stop() -- BEFORE dropping sessions. A drop() that posts its
// teardown to the now-dead io_context would never close the socket and would pin the session in a
// dead queue. When the network is already down, drop() must close the socket inline instead. This
// test deliberately never runs the socket's io_context, mirroring the joined-thread state.
BOOST_AUTO_TEST_CASE(DropClosesSocketInlineWhenNetworkDown)
{
    auto fakeSocket = std::make_shared<TestSessionSocket>();
    auto fakeAsio = std::make_shared<TestAsioInterface>(/*poolSize*/ 1, "FakeAsioLifetime",
        /*synchronousPark*/ true);
    auto fakeHost = std::make_shared<FakeHost_Lifetime>(fakeAsio, nullptr);

    auto session = std::make_shared<Session_Lifetime>(fakeSocket, *fakeHost, 1024, true);
    BOOST_REQUIRE(fakeSocket->isConnected());

    // Park a consumer on recvMessage(): no read loop ever runs in this test, so the channel
    // stays empty and the coroutine suspends in the channel's waiter slot before task::wait
    // returns. The session goes in as a raw pointer (the channel's wake touches only the
    // consumer's coroutine frame -- see Channel.h), the promise by shared_ptr.
    auto disconnectError = std::make_shared<std::promise<int64_t>>();
    auto disconnectResult = disconnectError->get_future();
    task::wait([](Session_Lifetime* sessionPtr,
                   std::shared_ptr<std::promise<int64_t>> result) -> task::Task<void> {
        try
        {
            (void)co_await sessionPtr->recvMessage();
        }
        catch (bcos::network::NetworkException const& e)
        {
            result->set_value(bcos::network::errorCodeOf(e));
        }
        catch (...)
        {
            result->set_value(-1);
        }
    }(session.get(), disconnectError));

    // Host::stop() has already run: the socket's io_context will never run again, so a posted
    // teardown would be dead code.
    fakeHost->stopNetwork();

    // The socket's io_context is deliberately never run in this test. With the network down, the
    // channel poster runs the consumer's wake INLINE (a posted task would never run), so the
    // parked recvMessage() throws on drop()'s own stack and the promise is already fulfilled by
    // the time drop() returns.
    session->drop(bcos::network::DisconnectReason::ClientQuit);

    BOOST_CHECK_MESSAGE(!fakeSocket->isConnected(),
        "FIB-184: with the network down (io_context threads joined), drop() must close the socket "
        "inline; a teardown posted to the dead io_context would never run");

    BOOST_REQUIRE_MESSAGE(
        disconnectResult.wait_for(std::chrono::seconds(0)) == std::future_status::ready,
        "the parked recvMessage() was not woken inline -- with the network down the channel "
        "poster must run the wake synchronously inside drop()");
    BOOST_CHECK_EQUAL(
        disconnectResult.get(), static_cast<int64_t>(bcos::network::P2PExceptionType::Disconnect));

    // Drain the shutdown handlers closeSocket() queued (they hold the socket, not the session) so
    // the fake io_context tears down cleanly.
    fakeSocket->ioService().poll();
}

BOOST_AUTO_TEST_SUITE_END()
