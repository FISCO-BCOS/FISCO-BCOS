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
#include "bcos-gateway/libp2p/Message.h"
#include "bcos-gateway/libp2p/P2PDecoder.h"
#include "bcos-gateway/libnetwork/SessionReadLoop.h"
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
        task::detail::FireCompletion<boost::system::error_code, std::size_t>;

    FakeASIO_FIB()
      : ASIOInterface(std::make_shared<bcos::IOServicePool>(1, "FakeASIO_FIB"), "0.0.0.0", 0),
        m_threadPool(std::make_shared<bcos::IOServicePool>(1, "FakeASIO_FIB"))
    {}
    ~FakeASIO_FIB() noexcept {}

    // Compile-time read-initiation policy (see ASIOInterface::awaitableReadSome): the read loop
    // is launched with this policy (startWithPolicy<FakeASIO_FIB::ReadPolicy>) so every read
    // parks its completion here instead of arming the real async_read_some.
    struct ReadPolicy
    {
        template <typename SocketT>
        static void invoke(ASIOInterface* asio, const std::shared_ptr<SocketT>& /*socket*/,
            ba::mutable_buffer buffers, ReadCompletion completion)
        {
            static_cast<FakeASIO_FIB*>(asio)->parkRead(buffers, std::move(completion));
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

// A frame that decodes to MESSAGE_ERROR: the base-header length is valid (14) but the version
// is out of the supported range, so the FIB-66 version check in decodeHeader rejects it.
inline std::shared_ptr<std::vector<uint8_t>> buildDecodeErrorFrame()
{
    auto frame = std::make_shared<std::vector<uint8_t>>(Message::MESSAGE_HEADER_LENGTH, 0);
    uint32_t frameLen = boost::asio::detail::socket_ops::host_to_network_long(
        static_cast<uint32_t>(Message::MESSAGE_HEADER_LENGTH));
    std::memcpy(frame->data(), &frameLen, sizeof(frameLen));
    uint16_t badVersion = boost::asio::detail::socket_ops::host_to_network_short(0xFFFF);
    std::memcpy(frame->data() + 4, &badVersion, sizeof(badVersion));
    return frame;
}

// A frame that throws during decode: version V2 means the extended header (ttl/src/dst) must
// follow the 14-byte base header, but the frame ends there — checkOffset throws out_of_range.
inline std::shared_ptr<std::vector<uint8_t>> buildDecodeExceptionFrame()
{
    auto frame = buildDecodeErrorFrame();
    uint16_t v2 = boost::asio::detail::socket_ops::host_to_network_short(
        static_cast<uint16_t>(bcos::protocol::ProtocolVersion::V2));
    std::memcpy(frame->data() + 4, &v2, sizeof(v2));
    return frame;
}

// A FakeSocket backed by a real SSL context and stream so that drop() can safely
// call sslref().async_shutdown() without crashing.
// We create a connected TCP socket-pair (accept → connect) so the underlying TCP
// socket is in a valid ESTABLISHED state. Without this, async_shutdown on an
// unconnected SSL stream triggers a null-pointer dereference in some SSL
// implementations (e.g. Apple's SecureTransport / LibreSSL on macOS).
class FakeSocket_FIB
{
public:
    FakeSocket_FIB()
      : m_ioContext(std::make_shared<ba::io_context>()),
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
    ~FakeSocket_FIB() = default;

    bool isConnected() const { return m_connected; }
    void close() { m_connected = false; }
    boost::asio::ip::tcp::endpoint remoteEndpoint(boost::system::error_code ec = {})
    {
        return {};
    }
    boost::asio::ip::tcp::endpoint localEndpoint(boost::system::error_code ec = {})
    {
        return {};
    }
    bi::tcp::socket& ref() { return m_sslSocket->next_layer(); }
    ba::ssl::stream<bi::tcp::socket>& sslref() { return *m_sslSocket; }
    // ASIOInterface dispatches reads/writes on stream(); the raw TCP socket keeps this fake's
    // IO plaintext (the read-loop tests inject completions via the fake read policy anyway).
    bi::tcp::socket& stream() { return ref(); }
    const NodeIPEndpoint& nodeIPEndpoint() const { return m_nodeIPEndpoint; }
    void setNodeIPEndpoint(NodeIPEndpoint _nodeIPEndpoint) {}
    ba::io_context& ioService() { return *m_ioContext; }

    bool m_connected{true};

private:
    std::shared_ptr<ba::io_context> m_ioContext;
    ba::ssl::context m_sslContext;
    std::shared_ptr<ba::ssl::stream<bi::tcp::socket>> m_sslSocket;
    NodeIPEndpoint m_nodeIPEndpoint;
};

class FakeHost_FIB : public bcos::gateway::Host<P2PDecoder, FakeSocket_FIB>
{
public:
    FakeHost_FIB(std::shared_ptr<ASIOInterface> _asioInterface,
        std::shared_ptr<BasicSessionFactory<P2PDecoder, FakeSocket_FIB>> _sessionFactory)
      : Host<P2PDecoder, FakeSocket_FIB>(std::move(_asioInterface), std::move(_sessionFactory))
    {
        this->m_run = true;
    }
};

using Session_FIB = BasicSession<P2PDecoder, FakeSocket_FIB>;

// FIB-70: Verify that decode error (negative return from decode()) triggers session drop.
// Before the fix, the session would remain active as a "zombie" until the idle timeout.
// After the fix, drop(UserReason) is called immediately, setting m_active = false.
BOOST_AUTO_TEST_CASE(DecodeErrorTriggersSessionDrop)
{
    auto fakeSocket = std::make_shared<FakeSocket_FIB>();

    {
        auto fakeAsio = std::make_shared<FakeASIO_FIB>();
        auto fakeHost = std::make_shared<FakeHost_FIB>(fakeAsio, nullptr);

        // 16-byte initial buffer: the read loop must see the 14-byte fixed header before it can
        // make progress on a real frame
        auto session = std::make_shared<Session_FIB>(fakeSocket, *fakeHost, 16, true);
        session->setMessageHandler(
            [](NetworkException e, Session_FIB::Ptr sessionFace, FrameMeta meta) {});

        session->startWithPolicy<FakeASIO_FIB::ReadPolicy>();

        // Send a frame that will trigger a decode error (MESSAGE_ERROR)
        fakeAsio->asyncAppendRecvPacket(buildDecodeErrorFrame());

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
    auto fakeSocket = std::make_shared<FakeSocket_FIB>();

    {
        auto fakeAsio = std::make_shared<FakeASIO_FIB>();
        auto fakeHost = std::make_shared<FakeHost_FIB>(fakeAsio, nullptr);

        auto session = std::make_shared<Session_FIB>(fakeSocket, *fakeHost, 16, true);
        session->setMessageHandler(
            [](NetworkException e, Session_FIB::Ptr sessionFace, FrameMeta meta) {});

        session->startWithPolicy<FakeASIO_FIB::ReadPolicy>();

        // Send a frame that will trigger a decode exception
        fakeAsio->asyncAppendRecvPacket(buildDecodeExceptionFrame());

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
    auto fakeSocket = std::make_shared<FakeSocket_FIB>();

    // Verify socket has expected reference count before session creation
    auto initialRefCount = fakeSocket.use_count();
    BOOST_CHECK_EQUAL(initialRefCount, 1);

    {
        auto fakeAsio = std::make_shared<FakeASIO_FIB>();
        auto fakeHost = std::make_shared<FakeHost_FIB>(fakeAsio, nullptr);

        auto session = std::make_shared<Session_FIB>(fakeSocket, *fakeHost, 2, true);
        session->setMessageHandler(
            [](NetworkException e, Session_FIB::Ptr sessionFace, FrameMeta meta) {});

        // After session creation, socket should be held by both fakeSocket and session
        BOOST_CHECK(fakeSocket.use_count() > 1);

        session->setSocket(nullptr);
    }

    // After session destruction, only fakeSocket holds the socket
    BOOST_CHECK_EQUAL(fakeSocket.use_count(), 1);

    fakeSocket->close();
}

// The response-callback manager is shared host-wide (the Host owns one SessionCallbackManager
// and hands it to every session it creates), so drop() must
// fail only the seqs registered through the dropped session — popping the whole manager would
// spuriously fail every in-flight request/response on every other session.
BOOST_AUTO_TEST_CASE(DropFlushesOnlyOwnPendingResponseCallbacks)
{
    auto fakeSocketA = std::make_shared<FakeSocket_FIB>();
    auto fakeSocketB = std::make_shared<FakeSocket_FIB>();

    {
        auto fakeAsio = std::make_shared<FakeASIO_FIB>();
        auto fakeHost = std::make_shared<FakeHost_FIB>(fakeAsio, nullptr);
        // both sessions share their host's callback manager, as in production
        auto& callbackManager = fakeHost->sessionCallbackManager();

        auto sessionA = std::make_shared<Session_FIB>(fakeSocketA, *fakeHost, 2, true);
        auto sessionB = std::make_shared<Session_FIB>(fakeSocketB, *fakeHost, 2, true);

        const uint32_t seqA = 1001;
        const uint32_t seqB = 1002;
        std::atomic<int> firedA{0};
        std::atomic<int> firedB{0};
        auto handlerA = std::make_shared<ResponseCallback<Session_FIB>>();
        handlerA->callback = [&firedA](NetworkException e, std::optional<FrameMeta>) {
            if (errorCodeOf(e) != 0)
            {
                ++firedA;
            }
        };
        auto handlerB = std::make_shared<ResponseCallback<Session_FIB>>();
        handlerB->callback = [&firedB](NetworkException e, std::optional<FrameMeta>) {
            if (errorCodeOf(e) != 0)
            {
                ++firedB;
            }
        };
        callbackManager.addCallback(seqA, handlerA);
        sessionA->addPendingResponseSeq(seqA);
        callbackManager.addCallback(seqB, handlerB);
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
        BOOST_CHECK(callbackManager.getCallback(seqA, false) == nullptr);
        BOOST_CHECK(callbackManager.getCallback(seqB, false) != nullptr);

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
    auto fakeSocket = std::make_shared<FakeSocket_FIB>();

    std::atomic<int> completions{0};
    std::atomic<int64_t> errorCode{0};
    const uint32_t seq = 4321;
    {
        auto fakeAsio = std::make_shared<FakeASIO_FIB>();
        auto fakeHost = std::make_shared<FakeHost_FIB>(fakeAsio, nullptr);
        auto& callbackManager = fakeHost->sessionCallbackManager();

        auto session = std::make_shared<Session_FIB>(fakeSocket, *fakeHost, 2, true);
        session->setMessageHandler(
            [](NetworkException e, Session_FIB::Ptr sessionFace, FrameMeta meta) {});
        session->startWithPolicy<FakeASIO_FIB::ReadPolicy>();

        // the socket's io_context is never run by the fixture: drive it so the posted
        // async_write actually executes (and fails against the closed peer)
        std::thread ioThread([&]() { fakeSocket->ioService().run(); });

        // the session sends pure bytes now: encode the P2P header at the caller and hand over
        // header + payload views (packetType 0 carries no options, so the encode cannot fail)
        Message message;
        message.setSeq(seq);
        bcos::bytes payload = {'x'};
        bcos::bytes headerBuffer;
        message.encodeHeader(headerBuffer);
        Message::stampLength(headerBuffer,
            static_cast<uint32_t>(headerBuffer.size() + payload.size()));
        task::wait([](std::shared_ptr<Session_FIB> _session, bcos::bytes _header,
                       bcos::bytes _payload, uint32_t _seq, std::atomic<int>& _completions,
                       std::atomic<int64_t>& _errorCode) -> task::Task<void> {
            try
            {
                co_await _session->fastSendMessage(bcos::ref(_header),
                    ::ranges::views::single(bcos::ref(_payload)), _seq, Options{2000, true});
                ++_completions;
            }
            catch (NetworkException const& e)
            {
                _errorCode.store(errorCodeOf(e));
                ++_completions;
            }
        }(session, std::move(headerBuffer), payload, seq, completions, errorCode));

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
        BOOST_CHECK(callbackManager.getCallback(seq, false) == nullptr);

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
