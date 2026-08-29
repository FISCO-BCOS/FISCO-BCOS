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
 */

#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-gateway/libnetwork/ASIOInterface.h"
#include "bcos-gateway/libnetwork/Host.h"
#include "bcos-gateway/libnetwork/Session.h"
#include "bcos-gateway/libnetwork/SessionReadLoop.h"
#include "bcos-gateway/libp2p/P2PMessage.h"
#include "bcos-utilities/IOServicePool.h"
#include "bcos-utilities/testutils/TestPromptFixture.h"
#include <boost/asio/error.hpp>

#include <boost/test/unit_test.hpp>
#include <chrono>
#include <future>
#include <optional>
#include <tuple>
using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::test;
using namespace bcos::crypto;

namespace ba = boost::asio;
namespace bi = boost::asio::ip;

BOOST_FIXTURE_TEST_SUITE(FIB184_SessionAsyncLifetimeTest, TestPromptFixture)

// A fake ASIO that parks the read-loop's coroutine at a manually-fired completion, so a test can
// hold a read "in flight" and complete it deterministically.
class FakeASIO_Lifetime : public bcos::gateway::ASIOInterface
{
public:
    using ReadCompletion =
        bcos::gateway::detail::AsioCompletion<boost::system::error_code, std::size_t>;

    FakeASIO_Lifetime()
      : ASIOInterface(std::make_shared<bcos::IOServicePool>(1, "FakeASIO_Lifetime"), "0.0.0.0", 0)
    {}
    ~FakeASIO_Lifetime() noexcept override = default;

    // Compile-time read-initiation policy (see ASIOInterface::awaitableReadSome): the read loop
    // is launched with this policy (startWithPolicy<FakeASIO_Lifetime::ReadPolicy>) so every
    // read parks its completion in a manually-fired slot.
    struct ReadPolicy
    {
        static void invoke(ASIOInterface* asio, const std::shared_ptr<SocketFace>& /*socket*/,
            ba::mutable_buffer /*buffers*/, ReadCompletion completion)
        {
            dynamic_cast<FakeASIO_Lifetime*>(asio)->parkRead(std::move(completion));
        }
    };

    // Read-policy target (see FakeASIO_Lifetime::ReadPolicy): park the read's completion in a
    // manually-fired slot so a test can hold a read "in flight" and complete it deterministically.
    void parkRead(ReadCompletion completion)
    {
        m_readHandler.emplace(std::move(completion));
    }

    bool hasReadHandler() const { return m_readHandler.has_value(); }

    // Complete the outstanding read, unwinding the read loop suspended on it. The slot is
    // cleared EXPLICITLY before firing: firing resumes the read loop, which re-arms the next
    // read into a fresh slot, and the moved-from optional would otherwise be destroyed only
    // after the new completion was already parked in it.
    void fireReadHandler(boost::system::error_code error, std::size_t bytes)
    {
        auto handler = std::move(m_readHandler);
        m_readHandler.reset();
        if (handler)
        {
            (*handler)(error, bytes);
        }
    }

private:
    std::optional<bcos::gateway::detail::AsioCompletion<boost::system::error_code, std::size_t>>
        m_readHandler;
};

class FakeHost_Lifetime : public bcos::gateway::Host
{
public:
    FakeHost_Lifetime(bcos::crypto::Hash::Ptr hash, std::shared_ptr<ASIOInterface> asioInterface,
        std::shared_ptr<SessionFactory> sessionFactory, MessageFactory::Ptr messageFactory)
      : Host(std::move(hash), std::move(asioInterface), std::move(sessionFactory),
            std::move(messageFactory))
    {
        m_run = true;
    }

    // Simulate Host::stop() having already run (IOServicePool::stop() joins the io_context
    // threads), so haveNetwork() returns false and no io_context is left to service posted
    // handlers.
    void stopNetwork() { m_run = false; }
};

// A socket backed by a real SSL stream so drop()/closeSocket() can call sslref() safely; close()
// only flips the connected flag (the underlying TCP socket is never opened).
class FakeSocket_Lifetime : public SocketFace
{
public:
    FakeSocket_Lifetime()
      : m_ioContext(std::make_shared<ba::io_context>()),
        m_sslContext(ba::ssl::context::tlsv12),
        m_sslSocket(std::make_shared<ba::ssl::stream<bi::tcp::socket>>(*m_ioContext, m_sslContext))
    {}
    ~FakeSocket_Lifetime() override = default;

    bool isConnected() const override { return m_connected; }
    void close() override { m_connected = false; }
    bi::tcp::endpoint remoteEndpoint(boost::system::error_code) override { return {}; }
    bi::tcp::endpoint localEndpoint(boost::system::error_code) override { return {}; }
    bi::tcp::socket& ref() override { return m_sslSocket->next_layer(); }
    ba::ssl::stream<bi::tcp::socket>& sslref() override { return *m_sslSocket; }
    const NodeIPEndpoint& nodeIPEndpoint() const override { return m_nodeIPEndpoint; }
    void setNodeIPEndpoint(NodeIPEndpoint) override {}
    ba::io_context& ioService() override { return *m_ioContext; }

    bool m_connected{true};

private:
    std::shared_ptr<ba::io_context> m_ioContext;
    ba::ssl::context m_sslContext;
    std::shared_ptr<ba::ssl::stream<bi::tcp::socket>> m_sslSocket;
    NodeIPEndpoint m_nodeIPEndpoint;
};

// The regression: an in-flight async read must keep the Session alive after every external strong
// reference is dropped. Pre-fix (weak_ptr capture) the Session would be destroyed here, leaving
// async_read_some writing into a freed recv buffer.
BOOST_AUTO_TEST_CASE(InFlightReadKeepsSessionAlive)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto fakeSocket = std::make_shared<FakeSocket_Lifetime>();
    auto messageFactory = std::make_shared<P2PMessageFactory>();
    auto fakeAsio = std::make_shared<FakeASIO_Lifetime>();
    auto fakeHost =
        std::make_shared<FakeHost_Lifetime>(hashImpl, fakeAsio, nullptr, messageFactory);

    std::weak_ptr<Session> weakSession;
    {
        auto session = std::make_shared<Session>(fakeSocket, *fakeHost, 1024, true);
        session->setMessageFactory(messageFactory);
        session->setMessageHandler([](NetworkException, SessionFace::Ptr, Message::Ptr) {});
        weakSession = session;

        // startWithPolicy() arms the first read synchronously (the old code used to defer the
        // first read through ASIOInterface::strandPost, which no longer exists).
        session->startWithPolicy<FakeASIO_Lifetime::ReadPolicy>();

        BOOST_REQUIRE_MESSAGE(fakeAsio->hasReadHandler(),
            "Session::startWithPolicy() must arm exactly one async read");
        // `session` goes out of scope here: the in-flight read is now the only owner.
    }

    BOOST_CHECK_MESSAGE(weakSession.lock() != nullptr,
        "FIB-184: an in-flight async read must keep the Session (and its recv buffer/socket) "
        "alive; pre-fix weak_ptr capture would have destroyed it here");

    // Completing the read with EOF drives drop(): no re-arm, so the read handler releases its
    // reference. drop() then hands out TWO deferred pieces of work, on two different executors:
    //   1. closeSocket(), posted to the socket's own io_context -- drained by poll() below.
    //      Mark the socket disconnected first so closeSocket() early-returns (no real SSL
    //      shutdown here).
    //   2. the teardown notification, posted to Host::m_teardownPool -- a dedicated executor this
    //      test drains with a sentinel below.
    fakeSocket->m_connected = false;
    fakeAsio->fireReadHandler(boost::asio::error::eof, 0);
    fakeSocket->ioService().poll();

    // Piece 2 is why the release cannot be asserted the instant fireReadHandler returns: the
    // notification lambda captures weak_from_this() but calls lock() on the teardown thread, so
    // while it runs it holds a strong reference (plus the copy it passes to m_messageHandler).
    // Asserting straight away raced that thread and made this case fail randomly in CI.
    //
    // Drain it instead of sleeping on it. m_teardownPool is a ONE-worker IOServicePool -- a single
    // io_context serviced by a single thread (Host.cpp: IOServicePool(1, "p2pTeardown")) -- so
    // posted handlers run strictly FIFO, and drop() ran synchronously inside fireReadHandler above,
    // which means the notification is already queued. A sentinel posted now therefore runs after
    // it, by which point notifyDisconnect has returned and both of its strong references are gone.
    // If the pool ever gains a second worker this barrier stops holding and must be revisited.
    //
    // The promise is captured by value through a shared_ptr on purpose: should the wait below time
    // out and BOOST_REQUIRE unwind the stack, the sentinel may still be sitting in the teardown
    // queue, and a by-reference capture of a dead local would be exactly the use-after-free this
    // file exists to prevent.
    auto teardownDone = std::make_shared<std::promise<void>>();
    auto teardownDrained = teardownDone->get_future();
    fakeHost->postTeardown([teardownDone]() { teardownDone->set_value(); });
    BOOST_REQUIRE_MESSAGE(
        teardownDrained.wait_for(std::chrono::seconds(5)) == std::future_status::ready,
        "the teardown executor never drained -- the notification was never dispatched");

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
    auto hashImpl = std::make_shared<Keccak256>();
    auto fakeSocket = std::make_shared<FakeSocket_Lifetime>();
    auto messageFactory = std::make_shared<P2PMessageFactory>();
    auto fakeAsio = std::make_shared<FakeASIO_Lifetime>();
    auto fakeHost =
        std::make_shared<FakeHost_Lifetime>(hashImpl, fakeAsio, nullptr, messageFactory);

    auto session = std::make_shared<Session>(fakeSocket, *fakeHost, 1024, true);
    session->setMessageFactory(messageFactory);
    session->setMessageHandler([](NetworkException, SessionFace::Ptr, Message::Ptr) {});
    BOOST_REQUIRE(fakeSocket->isConnected());

    // Host::stop() has already joined the io_context threads: the socket's io_context will never
    // run again, so a posted teardown would be dead code.
    fakeHost->stopNetwork();

    // The socket's io_context is deliberately never run in this test.
    session->drop(DisconnectReason::ClientQuit);

    BOOST_CHECK_MESSAGE(!fakeSocket->isConnected(),
        "FIB-184: with the network down (io_context threads joined), drop() must close the socket "
        "inline; a teardown posted to the dead io_context would never run");

    // Drain the shutdown handlers closeSocket() queued (they hold the socket, not the session) so
    // the fake io_context tears down cleanly.
    fakeSocket->ioService().poll();
}

BOOST_AUTO_TEST_SUITE_END()
