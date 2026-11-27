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
#include "bcos-gateway/libnetwork/Socket.h"
#include "bcos-gateway/libp2p/P2PMessage.h"
#include "bcos-task/Wait.h"
#include "bcos-utilities/testutils/TestPromptFixture.h"
#include <boost/asio/error.hpp>
#include <boost/test/unit_test.hpp>
#include <thread>

using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::test;
using namespace bcos::crypto;

namespace ba = boost::asio;
namespace bi = boost::asio::ip;

BOOST_FIXTURE_TEST_SUITE(FIB184_SessionAsyncLifetimeTest, TestPromptFixture)

// A fake ASIO that captures — but does not invoke — the async read handler and the strandPost
// callback, so a test can hold a read "in flight" and fire it deterministically.
class FakeASIO_Lifetime : public bcos::gateway::ASIOInterface
{
public:
    FakeASIO_Lifetime() { m_timerIOService = std::make_shared<ba::io_context>(); }
    ~FakeASIO_Lifetime() noexcept override = default;

    void asyncWrite(const std::shared_ptr<SocketFace>&,
        const std::vector<ba::const_buffer>& buffers, ReadWriteHandler handler) override
    {
        if (throwOnWrite)
        {
            throw boost::system::system_error(ba::error::operation_aborted);
        }
        writeSizes.push_back(ba::buffer_size(buffers));
        writeBuffers = buffers;
        writeHandler = std::move(handler);
    }

    std::vector<size_t> writeSizes;
    std::vector<ba::const_buffer> writeBuffers;
    ReadWriteHandler writeHandler;
    bool throwOnWrite = false;

    void completeWrite(boost::system::error_code error = {})
    {
        auto handler = std::exchange(writeHandler, {});
        BOOST_REQUIRE(handler);
        writeBuffers.clear();
        handler(error, error ? 0 : writeSizes.back());
    }

    void runTimers() { m_timerIOService->run(); }

    void asyncReadSome(
        const std::shared_ptr<SocketFace>&, ba::mutable_buffer, ReadWriteHandler handler) override
    {
        m_readHandler = std::move(handler);
    }

    void strandPost(Base_Handler handler) override { m_strandHandler = std::move(handler); }
    void stop() override {}

    // Run the deferred first-doRead posted by Session::start(). Moving the boost::function out
    // leaves m_strandHandler empty, so a re-entrant doRead re-arms into a fresh slot.
    void runStrandPost()
    {
        auto handler = std::move(m_strandHandler);
        if (handler)
        {
            handler();
        }
    }

    bool hasReadHandler() const { return static_cast<bool>(m_readHandler); }

    // Complete the outstanding read, releasing the reference it holds on the session. Moving the
    // handler out empties m_readHandler first, so a re-armed read lands in the fresh slot.
    void fireReadHandler(boost::system::error_code error, std::size_t bytes)
    {
        auto handler = std::move(m_readHandler);
        if (handler)
        {
            handler(error, bytes);
        }
    }

private:
    ReadWriteHandler m_readHandler;
    Base_Handler m_strandHandler;
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

    using Host::startPeerSession;
    void waitForDelivery() { m_asyncGroup.wait(); }
    void waitForConnection() { m_taskArena.execute([this] { m_asyncGroup.wait(); }); }
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

        session->start();           // posts the first doRead via strandPost
        fakeAsio->runStrandPost();  // runs doRead -> asyncReadSome captures the read handler

        BOOST_REQUIRE_MESSAGE(fakeAsio->hasReadHandler(),
            "Session::start()/doRead() must arm exactly one async read");
        // `session` goes out of scope here: the in-flight read is now the only owner.
    }

    BOOST_CHECK_MESSAGE(weakSession.lock() != nullptr,
        "FIB-184: an in-flight async read must keep the Session (and its recv buffer/socket) "
        "alive; pre-fix weak_ptr capture would have destroyed it here");

    // Completing the read with EOF drives drop(): no re-arm, so the read handler releases its
    // reference. drop() defers the socket teardown onto the socket's io_context; mark the socket
    // disconnected first so closeSocket() early-returns (no real SSL shutdown in the test), then
    // drain that io_context so the deferred handler releases its reference too.
    fakeSocket->m_connected = false;
    fakeAsio->fireReadHandler(boost::asio::error::eof, 0);
    fakeSocket->ioService().poll();

    BOOST_CHECK_MESSAGE(weakSession.lock() == nullptr,
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

BOOST_AUTO_TEST_CASE(QueuedWritesContinueAfterCompletionOnAnotherThread)
{
    auto socket = std::make_shared<FakeSocket_Lifetime>();
    auto asio = std::make_shared<FakeASIO_Lifetime>();
    auto host = std::make_shared<FakeHost_Lifetime>(
        std::make_shared<Keccak256>(), asio, nullptr, std::make_shared<P2PMessageFactory>());
    auto session = std::make_shared<Session>(socket, *host, 1024, true);
    session->m_active = true;
    auto enqueue = [&](size_t size) {
        EncodedMessage encoded;
        encoded.payload.resize(size);
        session->m_writeQueue.push(Payload{std::move(encoded), {}});
        session->write();
    };
    enqueue(3);
    enqueue(5);
    enqueue(7);
    BOOST_REQUIRE_EQUAL(asio->writeSizes.size(), 1);
    BOOST_CHECK_EQUAL(asio->writeSizes[0], 3);

    auto first = std::exchange(asio->writeHandler, {});
    std::thread worker([first = std::move(first)] { first({}, 3); });
    worker.join();
    BOOST_REQUIRE_EQUAL(asio->writeSizes.size(), 2);
    BOOST_CHECK_EQUAL(asio->writeSizes[1], 12);
    auto last = std::exchange(asio->writeHandler, {});
    last({}, 12);
    BOOST_CHECK(!asio->writeHandler);
}

BOOST_AUTO_TEST_CASE(OnlyMatchingResponseConsumesPendingRequest)
{
    auto asio = std::make_shared<FakeASIO_Lifetime>();
    auto host = std::make_shared<FakeHost_Lifetime>(
        std::make_shared<Keccak256>(), asio, nullptr, std::make_shared<P2PMessageFactory>());
    auto socket = std::make_shared<FakeSocket_Lifetime>();
    auto outbound = std::make_shared<Session>(socket, *host, 1024, true);
    auto other = std::make_shared<Session>(socket, *host, 1024, true);
    auto manager = std::make_shared<SessionCallbackManagerBucket>();
    outbound->setSessionCallbackManager(manager);
    outbound->m_peerID = "relay";

    P2PMessage request;
    std::string destination(128, 'a');
    auto canonical = host->canonicalPeerID(destination);
    request.setDstP2PNodeID(destination);
    request.setSeq(1);
    request.setPacketType(GatewayMessageType::AMOPMessageType);
    auto pending = outbound->registerResponse(request, 0, [](NetworkException, Message::Ptr) {});
    BOOST_REQUIRE(pending);
    BOOST_CHECK(pending->timeoutHandler.has_value());
    auto take = [&](Session& session, std::string_view peer, std::string_view source,
                    uint16_t type) {
        return manager->getCallback(1, true, [&](const ResponseCallback& callback) {
            return callback.accepts(session, peer, source, type);
        });
    };
    BOOST_CHECK(!take(*other, "other", canonical, GatewayMessageType::WSMessageType));
    BOOST_CHECK(!take(*outbound, "relay", "other", GatewayMessageType::AMOPMessageType));
    BOOST_CHECK(!take(*outbound, "relay", canonical, GatewayMessageType::PeerToPeerMessage));
    BOOST_CHECK(take(*outbound, "relay", canonical, GatewayMessageType::WSMessageType) == pending);
    BOOST_CHECK(!take(*outbound, "relay", canonical, GatewayMessageType::WSMessageType));
    BOOST_CHECK(
        pending->accepts(*other, canonical, canonical, GatewayMessageType::AMOPMessageType));

    P2PMessage direct;
    direct.setSeq(1);
    direct.setPacketType(GatewayMessageType::PeerToPeerMessage);
    auto ack = outbound->registerResponse(direct, 0, [](NetworkException, Message::Ptr) {});
    BOOST_REQUIRE(ack);
    BOOST_CHECK(!ack->timeoutHandler.has_value());
    BOOST_CHECK(!manager->getCallback(
        1, true, [&](const ResponseCallback& item) { return &item == pending.get(); }));
    BOOST_CHECK(take(*outbound, "relay", {}, 0) == ack);
}

struct SessionWriteFixture
{
    std::shared_ptr<FakeASIO_Lifetime> asio = std::make_shared<FakeASIO_Lifetime>();
    std::shared_ptr<FakeSocket_Lifetime> socket = std::make_shared<FakeSocket_Lifetime>();
    std::shared_ptr<SessionCallbackManagerBucket> callbacks =
        std::make_shared<SessionCallbackManagerBucket>();
    std::shared_ptr<FakeHost_Lifetime> host =
        std::make_shared<FakeHost_Lifetime>(std::make_shared<Keccak256>(), asio,
            std::make_shared<SessionFactory>(P2PInfo{}, 1024, 4096, 1024, 4096, 4, false),
            std::make_shared<P2PMessageFactory>());
    std::shared_ptr<Session> session;

    SessionWriteFixture()
    {
        P2PInfo peer;
        peer.p2pID = "peer";
        host->setSessionCallbackManager(callbacks);
        host->setConnectionHandler([this](NetworkException, const P2PInfo&, SessionFace::Ptr s) {
            session = std::dynamic_pointer_cast<Session>(s);
        });
        host->startPeerSession(peer, socket, {});
        host->waitForConnection();
        BOOST_REQUIRE(session);
        session->m_active = true;
    }

    static std::shared_ptr<P2PMessage> message(uint32_t seq)
    {
        auto msg = std::make_shared<P2PMessage>();
        msg->setSeq(seq);
        msg->setPacketType(GatewayMessageType::AMOPMessageType);
        return msg;
    }
};
BOOST_AUTO_TEST_SUITE_END()

BOOST_FIXTURE_TEST_SUITE(SessionWriteTest, FIB184_SessionAsyncLifetimeTest::SessionWriteFixture)

BOOST_AUTO_TEST_CASE(ReplyCompletesOriginalSendOnce)
{
    auto request = message(7);
    auto reply = message(7);
    reply->setPacketType(GatewayMessageType::WSMessageType);
    reply->setSrcP2PNodeID("peer");
    reply->setRespPacket();
    std::vector<std::pair<int, Message::Ptr>> responses;
    int duplicateError = 0;
    session->asyncSendMessage(
        request, {.timeout = 100}, [&](NetworkException ec, Message::Ptr response) {
            responses.emplace_back(ec.errorCode(), std::move(response));
        });
    session->asyncSendMessage(
        request, {}, [&](NetworkException ec, Message::Ptr) { duplicateError = ec.errorCode(); });
    asio->completeWrite();
    host->waitForDelivery();
    BOOST_CHECK(responses.empty());
    BOOST_CHECK_EQUAL(duplicateError, ProtocolError);
    BOOST_CHECK_EQUAL(asio->writeSizes.size(), 1);
    BOOST_REQUIRE(callbacks->getCallback(7, false));

    session->onMessage({}, reply);
    host->waitForDelivery();
    session->onMessage({}, reply);
    host->waitForDelivery();
    asio->runTimers();
    host->waitForDelivery();
    BOOST_REQUIRE_EQUAL(responses.size(), 1);
    BOOST_CHECK_EQUAL(responses.front().first, Success);
    BOOST_CHECK(responses.front().second == reply);
    BOOST_CHECK(!callbacks->getCallback(7, false));
}

BOOST_AUTO_TEST_CASE(EncodingFailureDoesNotReserveSequenceOrWrite)
{
    struct UnencodableMessage : P2PMessage
    {
        bool encode(EncodedMessage&) const override { return false; }
    };
    auto request = std::make_shared<UnencodableMessage>();
    request->setSeq(8);
    int result = 0;
    session->asyncSendMessage(
        request, {}, [&](NetworkException ec, Message::Ptr) { result = ec.errorCode(); });
    host->waitForDelivery();
    BOOST_CHECK_EQUAL(result, ProtocolError);
    BOOST_CHECK(!callbacks->getCallback(8, false));
    BOOST_CHECK(asio->writeSizes.empty());
}

BOOST_AUTO_TEST_CASE(WriteFailureCompletesActiveAndQueuedSends)
{
    int activeError = 0, queuedError = 0;
    session->asyncSendMessage(
        message(1), {}, [&](NetworkException ec, Message::Ptr) { activeError = ec.errorCode(); });
    session->asyncSendMessage(
        message(2), {}, [&](NetworkException ec, Message::Ptr) { queuedError = ec.errorCode(); });
    socket->m_connected = false;
    asio->completeWrite(ba::error::connection_reset);
    host->waitForDelivery();
    socket->ioService().poll();
    BOOST_CHECK_EQUAL(activeError, static_cast<int>(ba::error::connection_reset));
    BOOST_CHECK_EQUAL(queuedError, static_cast<int>(ba::error::not_connected));
    BOOST_CHECK(!callbacks->getCallback(1, false));
    BOOST_CHECK(!callbacks->getCallback(2, false));
    BOOST_CHECK_EQUAL(asio->writeSizes.size(), 1);
}

BOOST_AUTO_TEST_CASE(TimeoutAllowsSequenceReuseBeforeWriteCompletes)
{
    auto request = message(11);
    std::vector<int> firstResult;
    session->asyncSendMessage(request, {.timeout = 1},
        [&](NetworkException ec, Message::Ptr) { firstResult.push_back(ec.errorCode()); });
    asio->runTimers();
    host->waitForDelivery();
    BOOST_REQUIRE_EQUAL(firstResult.size(), 1);
    BOOST_CHECK_EQUAL(firstResult.front(), NetworkTimeout);
    BOOST_CHECK(!callbacks->getCallback(11, false));

    int secondError = 0;
    session->asyncSendMessage(
        request, {}, [&](NetworkException ec, Message::Ptr) { secondError = ec.errorCode(); });
    socket->m_connected = false;
    asio->completeWrite(ba::error::connection_reset);
    host->waitForDelivery();
    socket->ioService().poll();
    BOOST_CHECK_EQUAL(firstResult.size(), 1);
    BOOST_CHECK_EQUAL(secondError, static_cast<int>(ba::error::not_connected));
    BOOST_CHECK(!callbacks->getCallback(11, false));
}

BOOST_AUTO_TEST_CASE(WriteInitiationFailureReleasesPendingRequest)
{
    asio->throwOnWrite = true;
    std::vector<int> results;
    session->asyncSendMessage(message(5), {},
        [&](NetworkException ec, Message::Ptr) { results.push_back(ec.errorCode()); });
    host->waitForDelivery();
    socket->m_connected = false;
    socket->ioService().poll();
    BOOST_REQUIRE_EQUAL(results.size(), 1);
    BOOST_CHECK_EQUAL(results.front(), static_cast<int>(ba::error::operation_aborted));
    BOOST_CHECK(!callbacks->getCallback(5, false));
}

BOOST_AUTO_TEST_CASE(FastSendKeepsPayloadUntilWriteCompletes)
{
    auto request = message(19);
    bytes payload{1, 2, 3};
    request->setPayload(payload);
    bytes expected;
    BOOST_REQUIRE(request->encode(expected));
    std::vector<bytesConstRef> parts{ref(std::as_const(payload))};
    Message::Ptr received;
    auto send = [&]() -> task::Task<void> {
        received = co_await session->fastSendMessage(*request, parts, {.response = true});
    };
    task::wait(send());
    BOOST_REQUIRE(asio->writeHandler);
    BOOST_REQUIRE(callbacks->getCallback(19, false));
    std::optional<int> duplicateError;
    auto duplicate = [&]() -> task::Task<void> {
        try
        {
            co_await session->fastSendMessage(*request, parts, {.response = true});
            duplicateError = 0;
        }
        catch (const NetworkException& e)
        {
            duplicateError = e.errorCode();
        }
    };
    task::wait(duplicate());
    BOOST_REQUIRE(duplicateError);
    BOOST_CHECK_EQUAL(*duplicateError, ProtocolError);

    std::fill(payload.begin(), payload.end(), 0);
    bytes written(ba::buffer_size(asio->writeBuffers));
    ba::buffer_copy(ba::buffer(written), asio->writeBuffers);
    BOOST_CHECK(written == expected);

    auto reply = message(19);
    reply->setRespPacket();
    session->onMessage({}, reply);
    host->waitForDelivery();
    BOOST_CHECK(received == reply);
    BOOST_CHECK(!callbacks->getCallback(19, false));
    BOOST_CHECK_EQUAL(asio->writeSizes.size(), 1);
    asio->completeWrite();
}

BOOST_AUTO_TEST_SUITE_END()
