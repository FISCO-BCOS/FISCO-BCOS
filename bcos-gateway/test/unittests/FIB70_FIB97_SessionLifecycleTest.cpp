/**
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @brief Regression tests for FIB-70 and FIB-97 session lifecycle fixes
 * @file FIB70_FIB97_SessionLifecycleTest.cpp
 * @date 2026-04-07
 */

#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-gateway/libnetwork/ASIOInterface.h"
#include "bcos-gateway/libnetwork/Host.h"
#include "bcos-gateway/libnetwork/Session.h"
#include "bcos-gateway/libnetwork/SessionReadLoop.h"
#include "bcos-gateway/libp2p/P2PMessage.h"
#include <bcos-task/Wait.h>
#include <bcos-utilities/IOServicePool.h>
#include "bcos-utilities/testutils/TestPromptFixture.h"
#include <queue>
#include <thread>
#include <atomic>
#include <optional>
#include <tuple>
#include <list>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace gateway;
using namespace bcos::test;
using namespace bcos::crypto;

BOOST_FIXTURE_TEST_SUITE(FIB70_FIB97_SessionLifecycleTest, TestPromptFixture)

// --- Fake components (mirrors SessionTest.cpp infrastructure) ---

class FakeASIO_FIB : public bcos::gateway::ASIOInterface
{
public:
    using Packet = std::shared_ptr<std::vector<uint8_t>>;
    using ReadCompletion =
        bcos::gateway::detail::AsioCompletion<boost::system::error_code, std::size_t>;

    FakeASIO_FIB()
      : ASIOInterface(std::make_shared<bcos::IOServicePool>(1, "FakeASIO_FIB"), "0.0.0.0", 0),
        m_threadPool(std::make_shared<bcos::IOServicePool>(1, "FakeASIO_FIB"))
    {}
    ~FakeASIO_FIB() noexcept override {}

    // Compile-time read-initiation policy (see ASIOInterface::awaitableReadSome): the read loop
    // is launched with this policy (startWithPolicy<FakeASIO_FIB::ReadPolicy>) so every read
    // parks its completion here instead of arming the real async_read_some.
    struct ReadPolicy
    {
        static void invoke(ASIOInterface* asio, const std::shared_ptr<SocketFace>& /*socket*/,
            ba::mutable_buffer buffers, ReadCompletion completion)
        {
            dynamic_cast<FakeASIO_FIB*>(asio)->parkRead(buffers, std::move(completion));
        }
    };

    // Read-policy target (see FakeASIO_FIB::ReadPolicy): park the read's completion and feed it
    // buffered packets from the fake's own pool thread. The park is posted onto the pool thread
    // so EVERY access to m_pendingReads happens on the single pool thread — the first arm
    // happens on the caller's thread (Session::startWithPolicy -> readLoop), and without the
    // post it would race the pool thread's delivery in multi-session tests that share this fake
    // (see deliverIfPossible).
    void parkRead(ba::mutable_buffer buffers, ReadCompletion completion)
    {
        ++m_readsInFlight;
        m_threadPool->post([this, buffers, completion = std::move(completion)]() mutable {
            m_pendingReads.push_back(PendingRead{buffers, std::move(completion)});
            deliverIfPossible();
        });
    }

    // Test teardown: complete every parked read with operation_aborted so the read loops — and
    // the sessions their frames keep alive — unwind BEFORE the test nulls the socket or
    // destroys this fake. Poll readsInFlight() until 0 afterwards: the counter is decremented
    // only after the fired completion has synchronously unwound the whole read loop, so 0 means
    // the unwind is done and no coroutine touches the session any more.
    void stopReads()
    {
        m_threadPool->post([this] {
            while (!m_pendingReads.empty())
            {
                auto pending = std::move(m_pendingReads.front());
                m_pendingReads.pop_front();
                fireRead(std::move(pending.completion), boost::asio::error::operation_aborted, 0);
            }
        });
    }
    std::size_t readsInFlight() const { return m_readsInFlight.load(); }

    void stop() { m_threadPool.reset(); }

    void appendRecvPacket(Packet packet) { m_recvPackets.push(packet); }
    void asyncAppendRecvPacket(Packet packet)
    {
        m_threadPool->post([this, packet]() {
            m_recvPackets.push(std::move(packet));
            deliverIfPossible();
        });
    }

protected:
    std::size_t drainPackets(ba::mutable_buffer buffers)
    {
        std::size_t bytesTransferred = 0;
        auto limit = buffers.size();

        while (!m_recvPackets.empty())
        {
            auto packet = m_recvPackets.front();
            if (bytesTransferred + packet->size() > limit)
            {
                auto remaining = limit - bytesTransferred;
                boost::asio::buffer_copy(buffers, boost::asio::buffer(*packet), remaining);
                bytesTransferred += remaining;
                packet->erase(packet->begin(), packet->begin() + remaining);
                break;
            }
            else
            {
                m_recvPackets.pop();
                boost::asio::buffer_copy(buffers, boost::asio::buffer(*packet));
                buffers += packet->size();
                bytesTransferred += packet->size();
            }
        }
        return bytesTransferred;
    }

    // Everything below runs on the pool thread only: every read arm (including the first, via
    // parkRead's post) and every delivery are posted onto the single pool thread, so
    // m_pendingReads is never touched from another thread. Multiple sessions may share this
    // fake, so parked reads form a FIFO list rather than a single slot.
    void deliverIfPossible()
    {
        if (m_pendingReads.empty() || m_recvPackets.empty())
        {
            return;
        }
        auto pending = std::move(m_pendingReads.front());
        m_pendingReads.pop_front();
        fireRead(std::move(pending.completion), boost::system::error_code(),
            drainPackets(pending.buffers));
    }

    // Fire a parked completion. The fire resumes the read loop SYNCHRONOUSLY on this (pool)
    // thread — on success the loop processes the messages and re-arms a fresh read (parking a
    // new completion and re-incrementing the counter), on error it unwinds completely — so the
    // counter is decremented only after the loop has settled.
    void fireRead(ReadCompletion completion, boost::system::error_code ec, std::size_t bytes)
    {
        completion(ec, bytes);
        --m_readsInFlight;
    }

    struct PendingRead
    {
        ba::mutable_buffer buffers;
        ReadCompletion completion;
    };

    std::queue<Packet> m_recvPackets;
    std::list<PendingRead> m_pendingReads;
    std::atomic<std::size_t> m_readsInFlight{0};
    bcos::IOServicePool::Ptr m_threadPool;
};

// A message that always returns MESSAGE_ERROR to simulate decode failure
class DecodeErrorMessage : public P2PMessage
{
public:
    using Ptr = std::shared_ptr<DecodeErrorMessage>;
    int32_t decode(const bytesConstRef& _buffer) override
    {
        if (_buffer.size() == 0)
        {
            return MessageDecodeStatus::MESSAGE_INCOMPLETE;
        }
        // Always return error for any non-empty buffer
        return MessageDecodeStatus::MESSAGE_ERROR;
    }
};

class DecodeErrorMessageFactory : public P2PMessageFactory
{
public:
    Message::Ptr buildMessage() override { return std::make_shared<DecodeErrorMessage>(); }
};

// A message that throws an exception during decode
class DecodeExceptionMessage : public P2PMessage
{
public:
    using Ptr = std::shared_ptr<DecodeExceptionMessage>;
    int32_t decode(const bytesConstRef& _buffer) override
    {
        if (_buffer.size() == 0)
        {
            return MessageDecodeStatus::MESSAGE_INCOMPLETE;
        }
        throw std::runtime_error("Simulated decode exception");
    }
};

class DecodeExceptionMessageFactory : public P2PMessageFactory
{
public:
    Message::Ptr buildMessage() override { return std::make_shared<DecodeExceptionMessage>(); }
};

class FakeHost_FIB : public bcos::gateway::Host
{
public:
    FakeHost_FIB(bcos::crypto::Hash::Ptr _hash, std::shared_ptr<ASIOInterface> _asioInterface,
        std::shared_ptr<SessionFactory> _sessionFactory, MessageFactory::Ptr _messageFactory)
      : Host(_hash, _asioInterface, _sessionFactory, _messageFactory)
    {
        m_run = true;
    }
};

// A FakeSocket backed by a real SSL context and stream so that drop() can safely
// call sslref().async_shutdown() without crashing.
// We create a connected TCP socket-pair (accept → connect) so the underlying TCP
// socket is in a valid ESTABLISHED state. Without this, async_shutdown on an
// unconnected SSL stream triggers a null-pointer dereference in some SSL
// implementations (e.g. Apple's SecureTransport / LibreSSL on macOS).
class FakeSocket_FIB : public SocketFace
{
public:
    FakeSocket_FIB()
      : SocketFace(),
        m_ioContext(std::make_shared<ba::io_context>()),
        m_sslContext(ba::ssl::context::tlsv12)
    {
        // Create a connected TCP socket pair so the SSL stream has a valid transport.
        bi::tcp::acceptor acceptor(*m_ioContext, bi::tcp::endpoint(bi::tcp::v4(), 0));
        auto endpoint = acceptor.local_endpoint();
        bi::tcp::socket clientSocket(*m_ioContext);
        clientSocket.connect(endpoint);
        bi::tcp::socket serverSocket(*m_ioContext);
        acceptor.accept(serverSocket);
        // clientSocket is now in ESTABLISHED state; serverSocket is the
        // acceptor-side and will close when it goes out of scope.

        m_sslSocket = std::make_shared<ba::ssl::stream<bi::tcp::socket>>(
            std::move(clientSocket), m_sslContext);
    }
    ~FakeSocket_FIB() override = default;

    bool isConnected() const override { return m_connected; }
    void close() override { m_connected = false; }
    boost::asio::ip::tcp::endpoint remoteEndpoint(boost::system::error_code ec) override
    {
        return {};
    }
    boost::asio::ip::tcp::endpoint localEndpoint(boost::system::error_code ec) override
    {
        return {};
    }
    bi::tcp::socket& ref() override { return m_sslSocket->next_layer(); }
    ba::ssl::stream<bi::tcp::socket>& sslref() override { return *m_sslSocket; }
    const NodeIPEndpoint& nodeIPEndpoint() const override { return m_nodeIPEndpoint; }
    void setNodeIPEndpoint(NodeIPEndpoint _nodeIPEndpoint) override {}
    ba::io_context& ioService() override { return *m_ioContext; }

    bool m_connected{true};

private:
    std::shared_ptr<ba::io_context> m_ioContext;
    ba::ssl::context m_sslContext;
    std::shared_ptr<ba::ssl::stream<bi::tcp::socket>> m_sslSocket;
    NodeIPEndpoint m_nodeIPEndpoint;
};

// FIB-70: Verify that decode error (negative return from decode()) triggers session drop.
// Before the fix, the session would remain active as a "zombie" until the idle timeout.
// After the fix, drop(UserReason) is called immediately, setting m_active = false.
BOOST_AUTO_TEST_CASE(DecodeErrorTriggersSessionDrop)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto fakeSocket = std::make_shared<FakeSocket_FIB>();
    auto decodeErrorFactory = std::make_shared<DecodeErrorMessageFactory>();

    {
        auto fakeAsio = std::make_shared<FakeASIO_FIB>();
        auto fakeHost =
            std::make_shared<FakeHost_FIB>(hashImpl, fakeAsio, nullptr, decodeErrorFactory);

        auto session = std::make_shared<Session>(fakeSocket, *fakeHost, 2, true);
        session->setMessageFactory(fakeHost->messageFactory());
        session->setMessageHandler(
            [](NetworkException e, SessionFace::Ptr sessionFace, Message::Ptr message) {});

        session->startWithPolicy<FakeASIO_FIB::ReadPolicy>();

        // Send a packet that will trigger a decode error (MESSAGE_ERROR)
        auto badPacket = std::make_shared<std::vector<uint8_t>>(10, 0xAB);
        fakeAsio->asyncAppendRecvPacket(badPacket);

        // Wait for the session to be dropped
        size_t retryCount = 0;
        while (session->active() && retryCount < 200)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            retryCount++;
        }

        // FIB-70 fix: session must be inactive after decode error.
        // drop(UserReason) sets m_active = false as its first action.
        BOOST_CHECK(!session->active());

        session->setSocket(nullptr);
    }

    fakeSocket->close();
}

// FIB-70: Verify that decode exception triggers session drop.
// Before the fix, an exception in decode() would leave the session as a zombie.
// After the fix, drop(UserReason) is called in the catch block.
BOOST_AUTO_TEST_CASE(DecodeExceptionTriggersSessionDrop)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto fakeSocket = std::make_shared<FakeSocket_FIB>();
    auto decodeExceptionFactory = std::make_shared<DecodeExceptionMessageFactory>();

    {
        auto fakeAsio = std::make_shared<FakeASIO_FIB>();
        auto fakeHost =
            std::make_shared<FakeHost_FIB>(hashImpl, fakeAsio, nullptr, decodeExceptionFactory);

        auto session = std::make_shared<Session>(fakeSocket, *fakeHost, 2, true);
        session->setMessageFactory(fakeHost->messageFactory());
        session->setMessageHandler(
            [](NetworkException e, SessionFace::Ptr sessionFace, Message::Ptr message) {});

        session->startWithPolicy<FakeASIO_FIB::ReadPolicy>();

        // Send a packet that will trigger a decode exception
        auto badPacket = std::make_shared<std::vector<uint8_t>>(10, 0xCD);
        fakeAsio->asyncAppendRecvPacket(badPacket);

        // Wait for the session to be dropped
        size_t retryCount = 0;
        while (session->active() && retryCount < 200)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            retryCount++;
        }

        // FIB-70 fix: session must be inactive after decode exception
        BOOST_CHECK(!session->active());

        session->setSocket(nullptr);
    }

    fakeSocket->close();
}

// FIB-97: Verify socket shared_ptr capture prevents premature destruction.
// The fix captures m_socket as a shared_ptr in the async read handler lambda,
// keeping the socket alive even if Session::drop() is called concurrently.
BOOST_AUTO_TEST_CASE(SocketSharedPtrCaptureInAsyncHandler)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto fakeSocket = std::make_shared<FakeSocket_FIB>();
    auto decodeErrorFactory = std::make_shared<DecodeErrorMessageFactory>();

    // Verify socket has expected reference count before session creation
    auto initialRefCount = fakeSocket.use_count();
    BOOST_CHECK_EQUAL(initialRefCount, 1);

    {
        auto fakeAsio = std::make_shared<FakeASIO_FIB>();
        auto fakeHost =
            std::make_shared<FakeHost_FIB>(hashImpl, fakeAsio, nullptr, decodeErrorFactory);

        auto session = std::make_shared<Session>(fakeSocket, *fakeHost, 2, true);
        session->setMessageFactory(fakeHost->messageFactory());
        session->setMessageHandler(
            [](NetworkException e, SessionFace::Ptr sessionFace, Message::Ptr message) {});

        // After session creation, socket should be held by both fakeSocket and session
        BOOST_CHECK(fakeSocket.use_count() > 1);

        session->setSocket(nullptr);
    }

    // After session destruction, only fakeSocket holds the socket
    BOOST_CHECK_EQUAL(fakeSocket.use_count(), 1);

    fakeSocket->close();
}

// The response-callback manager is shared host-wide (GatewayFactory creates one
// SessionCallbackManagerBucket for the Host and injects it into every session), so drop() must
// fail only the seqs registered through the dropped session — popping the whole manager would
// spuriously fail every in-flight request/response on every other session.
BOOST_AUTO_TEST_CASE(DropFlushesOnlyOwnPendingResponseCallbacks)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto fakeSocketA = std::make_shared<FakeSocket_FIB>();
    auto fakeSocketB = std::make_shared<FakeSocket_FIB>();

    {
        auto fakeAsio = std::make_shared<FakeASIO_FIB>();
        auto fakeHost = std::make_shared<FakeHost_FIB>(
            hashImpl, fakeAsio, nullptr, std::make_shared<P2PMessageFactory>());
        // one manager shared by both sessions, as in production
        auto callbackManager = std::make_shared<SessionCallbackManagerBucket>();

        auto sessionA = std::make_shared<Session>(fakeSocketA, *fakeHost, 2, true);
        sessionA->setMessageFactory(fakeHost->messageFactory());
        sessionA->setSessionCallbackManager(callbackManager);
        auto sessionB = std::make_shared<Session>(fakeSocketB, *fakeHost, 2, true);
        sessionB->setMessageFactory(fakeHost->messageFactory());
        sessionB->setSessionCallbackManager(callbackManager);

        const uint32_t seqA = 1001;
        const uint32_t seqB = 1002;
        std::atomic<int> firedA{0};
        std::atomic<int> firedB{0};
        auto handlerA = std::make_shared<ResponseCallback>();
        handlerA->callback = [&firedA](NetworkException e, Message::Ptr) {
            if (e.errorCode() != 0)
            {
                ++firedA;
            }
        };
        auto handlerB = std::make_shared<ResponseCallback>();
        handlerB->callback = [&firedB](NetworkException e, Message::Ptr) {
            if (e.errorCode() != 0)
            {
                ++firedB;
            }
        };
        callbackManager->addCallback(seqA, handlerA);
        sessionA->addPendingResponseSeq(seqA);
        callbackManager->addCallback(seqB, handlerB);
        sessionB->addPendingResponseSeq(seqB);

        // skip the socket teardown tail; the flush runs before the null-socket check
        sessionA->setSocket(nullptr);
        sessionA->drop(DisconnectReason::UserReason);

        // the flush fires on the posted executor — wait for it
        size_t retryCount = 0;
        while (firedA == 0 && retryCount < 200)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            retryCount++;
        }

        // session A's waiter is failed with an error; session B's is left untouched
        BOOST_CHECK_EQUAL(firedA, 1);
        BOOST_CHECK_EQUAL(firedB, 0);
        BOOST_CHECK(callbackManager->getCallback(seqA, false) == nullptr);
        BOOST_CHECK(callbackManager->getCallback(seqB, false) != nullptr);

        sessionB->setSocket(nullptr);
    }

    fakeSocketA->close();
    fakeSocketB->close();
}

// The with-response send must fail exactly once when the async write itself fails, claiming the
// response callback back (the claimOnWriteError branch) or via the teardown flush — the write
// loop drops the session on a write error, so the drop flush legitimately races the write
// callback
// and either the raw asio write error or NetworkTimeout is a valid outcome; what must hold is
// exactly-once completion, no leftover callback, and no hang. The fake socket's SSL stream sits
// on a TCP pair whose peer closed at construction, so the write (and its implicit handshake)
// fails deterministically once the socket's io_context runs.
BOOST_AUTO_TEST_CASE(WriteFailureFailsWithResponseWaiterExactlyOnce)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto fakeSocket = std::make_shared<FakeSocket_FIB>();

    std::atomic<int> completions{0};
    std::atomic<int64_t> errorCode{0};
    const uint32_t seq = 4321;
    {
        auto fakeAsio = std::make_shared<FakeASIO_FIB>();
        auto fakeHost = std::make_shared<FakeHost_FIB>(
            hashImpl, fakeAsio, nullptr, std::make_shared<P2PMessageFactory>());
        auto callbackManager = std::make_shared<SessionCallbackManagerBucket>();

        auto session = std::make_shared<Session>(fakeSocket, *fakeHost, 2, true);
        session->setMessageFactory(fakeHost->messageFactory());
        session->setSessionCallbackManager(callbackManager);
        session->setMessageHandler(
            [](NetworkException e, SessionFace::Ptr sessionFace, Message::Ptr message) {});
        session->startWithPolicy<FakeASIO_FIB::ReadPolicy>();

        // the socket's io_context is never run by the fixture: drive it so the posted
        // async_write actually executes (and fails against the closed peer)
        std::thread ioThread([&]() { fakeSocket->ioService().run(); });

        auto message = std::static_pointer_cast<P2PMessage>(fakeHost->messageFactory()->buildMessage());
        message->setPacketType(1);
        message->setSeq(seq);
        bcos::bytes payload = {'x'};
        task::wait([](std::shared_ptr<Session> _session, std::shared_ptr<P2PMessage> _message,
                       bcos::bytes _payload, std::atomic<int>& _completions,
                       std::atomic<int64_t>& _errorCode) -> task::Task<void> {
            try
            {
                co_await _session->fastSendMessage(*_message,
                    ::ranges::views::single(bcos::ref(_payload)), Options{2000, true});
                ++_completions;
            }
            catch (NetworkException const& e)
            {
                _errorCode.store(e.errorCode());
                ++_completions;
            }
        }(session, message, payload, completions, errorCode));

        // task::wait detaches: the coroutine completes on the io threads once the write fails
        // (or the 2s response timer fires as backstop) — poll for the completion
        size_t retryCount = 0;
        while (completions.load() == 0 && retryCount < 500)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            retryCount++;
        }

        fakeSocket->ioService().stop();
        ioThread.join();

        // exactly one completion, always a failure, and the response callback is gone
        BOOST_CHECK_EQUAL(completions.load(), 1);
        BOOST_CHECK(errorCode.load() != 0);
        BOOST_CHECK(callbackManager->getCallback(seq, false) == nullptr);

        // drain the parked read so the read loop unwinds before the socket is nulled
        fakeAsio->stopReads();
        size_t drainRetry = 0;
        while (fakeAsio->readsInFlight() != 0 && drainRetry < 200)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            drainRetry++;
        }
        BOOST_REQUIRE_EQUAL(fakeAsio->readsInFlight(), 0);
        session->setSocket(nullptr);
    }

    fakeSocket->close();
}

BOOST_AUTO_TEST_SUITE_END()
