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
#include "bcos-network/ASIOInterface.h"
#include "bcos-network/Host.h"
#include "bcos-network/Socket.h"
#include "bcos-gateway/libp2p/Message.h"
#include "bcos-gateway/libp2p/P2PDecoder.h"
#include "bcos-gateway/libp2p/P2PSession.h"
#include "bcos-gateway/libp2p/Service.h"
#include <bcos-task/Wait.h>
#include <bcos-utilities/IOServicePool.h>
#include "bcos-utilities/testutils/TestPromptFixture.h"
#include "unittests/utils/TlsLoopback.h"
#include <queue>
#include <thread>
#include <atomic>
#include <future>
#include <optional>
#include <tuple>
#include <list>
#include <range/v3/view/single.hpp>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace gateway;
using namespace bcos::test;
using namespace bcos::crypto;

BOOST_FIXTURE_TEST_SUITE(FIB70_FIB97_SessionLifecycleTest, TestPromptFixture)

// --- Fake components (mirrors SessionTest.cpp infrastructure) ---

class FakeASIO_FIB : public bcos::network::ASIOInterface
{
public:
    using Packet = std::shared_ptr<std::vector<uint8_t>>;
    using ReadCompletion =
        task::detail::FireCompletion<boost::system::error_code, std::size_t>;

    FakeASIO_FIB()
      : bcos::network::ASIOInterface(std::make_shared<bcos::IOServicePool>(1, "FakeASIO_FIB"), "0.0.0.0", 0),
        m_threadPool(std::make_shared<bcos::IOServicePool>(1, "FakeASIO_FIB"))
    {}
    ~FakeASIO_FIB() noexcept {}

    // Compile-time read-initiation policy (see ASIOInterface::awaitableReadSome): the read loop
    // is launched with this policy (startWithPolicy<FakeASIO_FIB::ReadPolicy>) so every read
    // parks its completion here instead of arming the real async_read_some.
    struct ReadPolicy
    {
        template <typename SocketT>
        static void invoke(bcos::network::ASIOInterface* asio, const std::shared_ptr<SocketT>& /*socket*/,
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

// A frame whose V2 header promises the extended header (ttl/src/dst) but ends after the
// 14-byte base header. Stream splitting no longer inspects the extended header, so the
// session delivers it; Message::decode rejects it at the libp2p boundary (checkOffset throws
// out_of_range) — see P2PDecoderTest.truncatedExtendedHeaderRejectedAtLibp2pBoundary.
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

// Templated on the socket type: the FakeSocket_FIB-based read-loop tests instantiate it with the
// default, the correlation tests (which wrap the session in a P2PSession / register it as a
// pending-request owner) with the production Socket.
template <typename SocketT = FakeSocket_FIB>
class FakeHost_FIB : public bcos::network::Host<P2PDecoder, SocketT>
{
public:
    FakeHost_FIB(std::shared_ptr<bcos::network::ASIOInterface> _asioInterface,
        std::shared_ptr<bcos::network::BasicSessionFactory<P2PDecoder, SocketT>> _sessionFactory)
      : bcos::network::Host<P2PDecoder, SocketT>(std::move(_asioInterface), std::move(_sessionFactory))
    {
        this->m_run = true;
    }
};

using Session_FIB = bcos::network::BasicSession<P2PDecoder, FakeSocket_FIB>;

// FIB-70: Verify that decode error (negative return from decode()) triggers session drop.
// Before the fix, the session would remain active as a "zombie" until the idle timeout.
// After the fix, drop(UserReason) is called immediately, setting m_active = false.
BOOST_AUTO_TEST_CASE(DecodeErrorTriggersSessionDrop)
{
    auto fakeSocket = std::make_shared<FakeSocket_FIB>();

    {
        auto fakeAsio = std::make_shared<FakeASIO_FIB>();
        auto fakeHost = std::make_shared<FakeHost_FIB<>>(fakeAsio, nullptr);

        // 16-byte initial buffer: the read loop must see the 14-byte fixed header before it can
        // make progress on a real frame
        auto session = std::make_shared<Session_FIB>(fakeSocket, *fakeHost, 16, true);

        // Pull-mode consumer: recvMessage() rethrows the NetworkException the recv channel was
        // closed with — on a decode error the read loop closes it with ProtocolError, then drops
        // the session. The consumer's resume is posted to the shared pool through the channel
        // poster, so the outcome is handed back through a promise, never assumed synchronous. The
        // promise is shared so an aborted test (BOOST_REQUIRE timeout) cannot leave the consumer
        // writing into a destroyed promise.
        auto exitCode = std::make_shared<std::promise<int64_t>>();
        auto exitCodeFuture = exitCode->get_future();
        task::wait([](Session_FIB::Ptr _session,
                       std::shared_ptr<std::promise<int64_t>> _exitCode) -> task::Task<void> {
            try
            {
                (void)co_await _session->recvMessage();
                // a frame delivery instead of the teardown error means no drop happened
                _exitCode->set_value(bcos::network::P2PExceptionType::Success);
            }
            catch (bcos::network::NetworkException const& e)
            {
                _exitCode->set_value(bcos::network::errorCodeOf(e));
            }
            catch (...)
            {
                _exitCode->set_value(-1);
            }
        }(session, exitCode));

        session->startWithPolicy<FakeASIO_FIB::ReadPolicy>();

        // Send a frame that will trigger a decode error (MESSAGE_ERROR)
        fakeAsio->asyncAppendRecvPacket(buildDecodeErrorFrame());

        // Wait for the consumer to observe the channel close (ProtocolError)
        BOOST_REQUIRE(
            exitCodeFuture.wait_for(std::chrono::seconds(10)) == std::future_status::ready);
        BOOST_CHECK_EQUAL(exitCodeFuture.get(), bcos::network::P2PExceptionType::ProtocolError);

        // FIB-70 fix: session must be inactive after decode error.
        // drop(UserReason) sets m_active = false as its first action; it runs on the read loop
        // right after the channel close and races the consumer's posted resume, so poll for it.
        size_t retryCount = 0;
        while (session->active() && retryCount < 200)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            retryCount++;
        }
        BOOST_CHECK(!session->active());

        session->setSocket(nullptr);
    }

    fakeSocket->close();
}

// The V2-truncated-extended-header frame used to be rejected inside the session's decode
// step; with stream splitting limited to length/version validation it is now a complete,
// consumable frame delivered to the recvMessage() consumer, and the session stays up. (Rejection
// happens at the libp2p boundary, covered by P2PDecoderTest and Service's ProtocolError path.)
BOOST_AUTO_TEST_CASE(TruncatedExtendedHeaderIsDeliveredNotDropped)
{
    auto fakeSocket = std::make_shared<FakeSocket_FIB>();

    {
        auto fakeAsio = std::make_shared<FakeASIO_FIB>();
        auto fakeHost = std::make_shared<FakeHost_FIB<>>(fakeAsio, nullptr);

        auto session = std::make_shared<Session_FIB>(fakeSocket, *fakeHost, 16, true);

        // Pull-mode consumer: recvMessage() delivers the decoded FrameMeta. The resume is posted
        // to the shared pool through the channel poster, so the received bytes are handed back
        // through a promise — never assume a synchronous delivery. The promise is shared so an
        // aborted test cannot leave the consumer writing into a destroyed promise.
        auto received = std::make_shared<std::promise<bcos::bytes>>();
        auto receivedFuture = received->get_future();
        task::wait([](Session_FIB::Ptr _session,
                       std::shared_ptr<std::promise<bcos::bytes>> _received) -> task::Task<void> {
            try
            {
                auto meta = co_await _session->recvMessage();
                auto frameData = meta.frameData();
                _received->set_value(bcos::bytes(frameData.begin(), frameData.end()));
            }
            catch (...)
            {
                // a channel close (session teardown) surfaces as a NetworkException here; let it
                // fail the test through the future instead of faking a delivery
                _received->set_exception(std::current_exception());
            }
        }(session, received));

        session->startWithPolicy<FakeASIO_FIB::ReadPolicy>();

        fakeAsio->asyncAppendRecvPacket(buildDecodeExceptionFrame());

        // Wait for the frame to be delivered to the consumer
        BOOST_REQUIRE(
            receivedFuture.wait_for(std::chrono::seconds(10)) == std::future_status::ready);
        auto receivedBytes = receivedFuture.get();
        auto expected = buildDecodeExceptionFrame();
        BOOST_REQUIRE_EQUAL(receivedBytes.size(), expected->size());
        BOOST_CHECK_EQUAL_COLLECTIONS(
            receivedBytes.begin(), receivedBytes.end(), expected->begin(), expected->end());

        // the session is NOT dropped: stream splitting succeeded on this frame
        BOOST_CHECK(session->active());

        session->drop(bcos::network::UserReason);
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
        auto fakeHost = std::make_shared<FakeHost_FIB<>>(fakeAsio, nullptr);

        auto session = std::make_shared<Session_FIB>(fakeSocket, *fakeHost, 2, true);

        // After session creation, socket should be held by both fakeSocket and session
        BOOST_CHECK(fakeSocket.use_count() > 1);

        session->setSocket(nullptr);
    }

    // After session destruction, only fakeSocket holds the socket
    BOOST_CHECK_EQUAL(fakeSocket.use_count(), 1);

    fakeSocket->close();
}

// Request/response correlation lives in the node-level pending-request table owned by the
// Service (libp2p/PendingResponse.h): a routed response can arrive on ANY session of this node,
// so the table is keyed by seq alone. A disconnect must fail only the requests whose OUTBOUND
// session is the dropped one (each entry records its owner session) — the old fail-fast-on-drop
// semantics moved up from libnetwork's per-session seq lists with the table. (This is the
// successor of the per-session callback-manager flush test.)
BOOST_AUTO_TEST_CASE(DisconnectFlushesOnlyOwnPendingResponses)
{
    auto fakeAsio = std::make_shared<FakeASIO_FIB>();
    auto host = std::make_shared<FakeHost_FIB<bcos::network::Socket>>(fakeAsio, nullptr);

    // The sessions never start and never touch the wire: they exist only as owner identities for
    // the pending table, so a bare Socket over an idle io_context is enough.
    auto io = std::make_shared<ba::io_context>();
    ba::ssl::context sslContext(ba::ssl::context::tlsv12);
    auto makeSession = [&]() {
        return std::make_shared<Session>(
            std::make_shared<bcos::network::Socket>(io, &sslContext, NodeIPEndpoint()), *host, 16, true);
    };
    auto sessionA = makeSession();
    auto sessionB = makeSession();

    P2PInfo selfInfo;
    selfInfo.rawP2pID = "selfRawP2pID";
    selfInfo.p2pID = "selfP2pID";
    // no host on the service: settlements run inline instead of being posted to the pool
    auto service = std::make_shared<Service>(selfInfo);

    const uint32_t seqA = 1001;
    const uint32_t seqB = 1002;
    std::atomic<int> firedA{0};
    std::atomic<int> firedB{0};
    auto pendingA = std::make_shared<PendingResponse>();
    pendingA->callback = [&firedA](bcos::network::NetworkException e, std::optional<bcos::network::FrameMeta>) {
        if (bcos::network::errorCodeOf(e) != 0)
        {
            ++firedA;
        }
    };
    pendingA->owner = sessionA;
    auto pendingB = std::make_shared<PendingResponse>();
    pendingB->callback = [&firedB](bcos::network::NetworkException e, std::optional<bcos::network::FrameMeta>) {
        if (bcos::network::errorCodeOf(e) != 0)
        {
            ++firedB;
        }
    };
    pendingB->owner = sessionB;
    BOOST_REQUIRE(service->registerPendingResponse(seqA, pendingA));
    BOOST_REQUIRE(service->registerPendingResponse(seqB, pendingB));

    service->failPendingResponsesOf(sessionA,
        bcos::network::makeNetworkException(bcos::network::P2PExceptionType::NetworkTimeout, "NetworkTimeout"));

    // session A's waiter is failed with an error; session B's is left untouched
    BOOST_CHECK_EQUAL(firedA, 1);
    BOOST_CHECK_EQUAL(firedB, 0);
    BOOST_CHECK(service->claimPendingResponse(seqA) == nullptr);
    BOOST_CHECK(service->claimPendingResponse(seqB) != nullptr);
}

// A timeout settles the waiter exactly once: onResponseTimeout claims the entry atomically, so a
// later ack (or a second timeout fire) finds nothing and the callback runs exactly once.
BOOST_AUTO_TEST_CASE(ResponseTimeoutSettlesWaiterExactlyOnce)
{
    P2PInfo selfInfo;
    selfInfo.rawP2pID = "selfRawP2pID";
    selfInfo.p2pID = "selfP2pID";
    auto service = std::make_shared<Service>(selfInfo);

    const uint32_t seq = 777;
    std::atomic<int> fired{0};
    std::atomic<int64_t> errorCode{0};
    auto pending = std::make_shared<PendingResponse>();
    pending->callback = [&fired, &errorCode](bcos::network::NetworkException e, std::optional<bcos::network::FrameMeta> meta) {
        errorCode.store(bcos::network::errorCodeOf(e));
        BOOST_CHECK(!meta);
        ++fired;
    };
    BOOST_REQUIRE(service->registerPendingResponse(seq, pending));

    service->onResponseTimeout(seq);
    // a second settlement attempt (late ack / repeated timer fire) is a no-op
    service->onResponseTimeout(seq);

    BOOST_CHECK_EQUAL(fired, 1);
    BOOST_CHECK_EQUAL(errorCode.load(), bcos::network::P2PExceptionType::NetworkTimeout);
    BOOST_CHECK(service->claimPendingResponse(seq) == nullptr);
}

// The with-response send must fail exactly once when the async write itself fails:
// P2PSession::fastSendP2PMessage reclaims the registration in its write-failure catch — unless a
// concurrent settlement (the response timeout is the only other path armed here) already claimed
// it. Either the raw asio write error or NetworkTimeout is a valid outcome; what must hold is
// exactly-once completion and no leftover entry. The loopback peer closes right after the TLS
// handshake, so the client's write fails deterministically once it reaches the wire.
BOOST_AUTO_TEST_CASE(WriteFailureFailsWithResponseWaiterExactlyOnce)
{
    auto fakeAsio = std::make_shared<FakeASIO_FIB>();

    auto io = std::make_shared<ba::io_context>();
    boost::asio::executor_work_guard<ba::io_context::executor_type> workGuard(io->get_executor());
    std::thread ioThread([io] { io->run(); });

    ba::ip::tcp::acceptor acceptor(*io, ba::ip::tcp::endpoint(ba::ip::tcp::v4(), 0));
    auto listenEndpoint = acceptor.local_endpoint();

    // TLS contexts for the loopback: the session's writes dispatch on its ssl::stream at compile
    // time, so the wire speaks real TLS (see unittests/utils/TlsLoopback.h).
    auto serverCtx = testutil::makeTlsServerContext();
    auto clientCtx = testutil::makeTlsClientContext();

    std::thread peerThread([&] {
        ba::ip::tcp::socket peer(*io);
        boost::system::error_code ec;
        acceptor.accept(peer, ec);
        if (ec)
        {
            return;
        }
        testutil::PeerSslStream tlsPeer(std::move(peer), serverCtx);
        tlsPeer.handshake(ba::ssl::stream_base::server, ec);
        // close immediately (tlsPeer destructs here): the client's write then fails on the
        // broken wire — or, if it lands before the FIN is processed, the 2s response timer
        // fires as backstop; both are valid failure outcomes for the assertion below
    });

    ba::ip::tcp::socket client(*io);
    boost::system::error_code connectError;
    client.connect(listenEndpoint, connectError);
    BOOST_REQUIRE(!connectError);

    std::atomic<int> completions{0};
    std::atomic<int64_t> errorCode{0};
    const uint32_t seq = 4321;
    {
        auto fakeHost = std::make_shared<FakeHost_FIB<bcos::network::Socket>>(fakeAsio, nullptr);
        auto sessionSocket = testutil::makeTlsSessionSocket(io, clientCtx, std::move(client));
        auto session = std::make_shared<Session>(sessionSocket, *fakeHost, 2, true);
        session->startWithPolicy<FakeASIO_FIB::ReadPolicy>();

        P2PInfo selfInfo;
        selfInfo.rawP2pID = "selfRawP2pID";
        selfInfo.p2pID = "selfP2pID";
        auto service = std::make_shared<Service>(selfInfo);

        auto p2pSession = std::make_shared<P2PSession>();
        p2pSession->setSession(session);
        p2pSession->setService(service);

        bcos::bytes payload = {'x'};
        task::wait([](P2PSession::Ptr _p2pSession, bcos::bytes _payload, uint32_t _seq,
                       std::atomic<int>& _completions,
                       std::atomic<int64_t>& _errorCode) -> task::Task<void> {
            Message message;
            message.setSeq(_seq);
            try
            {
                co_await _p2pSession->fastSendP2PMessage(message,
                    ::ranges::views::single(bcos::ref(std::as_const(_payload))),
                    bcos::network::Options{2000, true});
                ++_completions;
            }
            catch (bcos::network::NetworkException const& e)
            {
                _errorCode.store(bcos::network::errorCodeOf(e));
                ++_completions;
            }
        }(p2pSession, std::move(payload), seq, completions, errorCode));

        // task::wait detaches: the coroutine completes on the io threads once the write fails
        // (or the 2s response timer fires as backstop) — poll for the completion
        size_t retryCount = 0;
        while (completions.load() == 0 && retryCount < 500)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            retryCount++;
        }

        // exactly one completion, always a failure, and the pending entry is reclaimed
        BOOST_CHECK_EQUAL(completions.load(), 1);
        BOOST_CHECK(errorCode.load() != 0);
        BOOST_CHECK(service->claimPendingResponse(seq) == nullptr);

        session->disconnect(bcos::network::DisconnectReason::DisconnectRequested);

        // drain the parked read so the read loop unwinds before the fake is destroyed
        fakeAsio->stopReads();
        size_t drainRetry = 0;
        while (fakeAsio->readsInFlight() != 0 && drainRetry < 200)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            drainRetry++;
        }
        BOOST_REQUIRE_EQUAL(fakeAsio->readsInFlight(), 0);
    }

    peerThread.join();
    workGuard.reset();
    {
        boost::system::error_code ec;
        acceptor.close(ec);
    }
    io->stop();
    ioThread.join();
}

BOOST_AUTO_TEST_SUITE_END()
