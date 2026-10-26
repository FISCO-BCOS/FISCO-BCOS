/**
 *  Copyright (C) 2023 FISCO BCOS.
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
 * @brief P2PSession/Service wire-level tests that used to share SessionTest.cpp with the raw
 *        session read/write/buffer cases; those moved to bcos-network's own suite
 *        (bcos-network/test/unittests/SessionTest.cpp) when libnetwork became a standalone
 *        library. What remains here needs the gateway wire format (P2PDecoder/Message) and
 *        libp2p policy (Service hooks, P2PSession compression/version stamping).
 * @file SessionTest.cpp
 * @author: octopus
 * @date 2023-02-23
 */
#include "bcos-framework/protocol/ProtocolInfo.h"
#include "bcos-network/ASIOInterface.h"
#include "bcos-network/Host.h"
#include "bcos-gateway/libp2p/Message.h"
#include "bcos-gateway/libp2p/P2PDecoder.h"
#include "bcos-gateway/libp2p/P2PSession.h"
#include "bcos-gateway/libp2p/Service.h"
#include "bcos-utilities/testutils/TestPromptFixture.h"
#include <bcos-framework/protocol/Protocol.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/IOServicePool.h>
#include "unittests/utils/TlsLoopback.h"
#include <boost/asio/ip/tcp.hpp>
#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <list>
#include <mutex>
#include <optional>
#include <range/v3/view/single.hpp>
#include <thread>
#include <vector>

using namespace bcos;
using namespace gateway;
using namespace bcos::test;
using namespace bcos::crypto;

BOOST_FIXTURE_TEST_SUITE(SessionTest, TestPromptFixture)


class FakeASIO : public bcos::network::ASIOInterface
{
public:
    using ReadCompletion = task::detail::FireCompletion<boost::system::error_code, std::size_t>;

    FakeASIO()
      : bcos::network::ASIOInterface(std::make_shared<bcos::IOServicePool>(1, "FakeASIO"), "0.0.0.0", 0),
        m_threadPool(std::make_shared<bcos::IOServicePool>(1, "FakeASIO"))
    {}
    ~FakeASIO() noexcept {};

    // Compile-time read-initiation policy (see ASIOInterface::awaitableReadSome): the read loop
    // is launched with this policy (startWithPolicy<FakeASIO::ReadPolicy>) so every read parks
    // its completion here instead of arming the real async_read_some. Generic over the socket
    // type — the tests drive it with the production Socket.
    struct ReadPolicy
    {
        template <typename SocketT>
        static void invoke(bcos::network::ASIOInterface* asio, const std::shared_ptr<SocketT>& /*socket*/,
            ba::mutable_buffer /*buffers*/, ReadCompletion completion)
        {
            static_cast<FakeASIO*>(asio)->parkRead(std::move(completion));
        }
    };

    // Read-policy target (see FakeASIO::ReadPolicy): park the read's completion on the fake's
    // own pool thread. The remaining tests in this file never deliver bytes — they park reads
    // only to keep the read loop quiet and drain it at teardown (stopReads) — so no packet
    // plumbing is needed here. (The read-delivery cases moved to bcos-network's suite together
    // with its packet-feeding test ASIO fake.)
    void parkRead(ReadCompletion completion)
    {
        ++m_readsInFlight;
        m_threadPool->post([this, completion = std::move(completion)]() mutable {
            m_pendingReads.push_back(std::move(completion));
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
                auto completion = std::move(m_pendingReads.front());
                m_pendingReads.pop_front();
                fireRead(std::move(completion), boost::asio::error::operation_aborted, 0);
            }
        });
    }
    std::size_t readsInFlight() const { return m_readsInFlight.load(); }

    void stop() { m_threadPool.reset(); }

protected:
    // Fire a parked completion. The fire resumes the read loop SYNCHRONOUSLY on this (pool)
    // thread — on error it unwinds completely — so the counter is decremented only after the
    // loop has settled.
    void fireRead(ReadCompletion completion, boost::system::error_code ec, std::size_t bytes)
    {
        completion(ec, bytes);
        --m_readsInFlight;
    }

    std::list<ReadCompletion> m_pendingReads;
    std::atomic<std::size_t> m_readsInFlight{0};
    bcos::IOServicePool::Ptr m_threadPool;
};

// Templated on the socket type: the remaining wire-level tests (which wrap the session in a
// P2PSession and therefore need the production Session = BasicSession<P2PDecoder>) instantiate
// it with the production Socket.
template <typename SocketT>
class FakeHost : public bcos::network::Host<P2PDecoder, SocketT>
{
public:
    FakeHost(std::shared_ptr<bcos::network::ASIOInterface> _asioInterface,
        std::shared_ptr<bcos::network::BasicSessionFactory<P2PDecoder, SocketT>> _sessionFactory)
      : bcos::network::Host<P2PDecoder, SocketT>(std::move(_asioInterface), std::move(_sessionFactory))
    {
        this->m_run = true;
    }
};

// Wrap a connected TCP socket in the production Socket: tests that wrap the session in a
// P2PSession must drive the production Session = BasicSession<P2PDecoder> (SocketT = Socket), and
// the wire-level tests additionally need ASIOInterface::awaitableWrite to actually complete on
// the wire — a fake socket cannot reach those branches because its write path is inert.
// The ssl::context must outlive the returned Socket (the ssl::stream references it).
inline std::shared_ptr<bcos::network::Socket> makeLoopbackSocket(
    std::shared_ptr<ba::io_context> _ioContext, ba::ssl::context& _sslContext,
    bi::tcp::socket _socket)
{
    auto socket = std::make_shared<bcos::network::Socket>(std::move(_ioContext), &_sslContext, NodeIPEndpoint());
    socket->ref() = std::move(_socket);
    return socket;
}

BOOST_AUTO_TEST_CASE(sendMessageOutboundRateLimit)
{
    // The fast path must honour the same pre-send (outgoing rate-limit) check the removed callback
    // path (asyncSendMessage) enforced: a beforeMessageHandler rejection surfaces as a thrown
    // NetworkException (e.g. OutBWOverflow) so coroutine retry loops can stop. The hook moved with
    // the wire-format work: it now lives on the Service (invoked from
    // P2PSession::fastSendP2PMessage), not on the session.
    auto fakeAsio = std::make_shared<FakeASIO>();

    // Real loopback pair: the session must be the production Session (BasicSession<P2PDecoder>,
    // SocketT = Socket) so it can be wrapped in a P2PSession. The send never reaches the wire —
    // the beforeMessageHandler rejects it pre-send — but active() requires a connected socket.
    auto io = std::make_shared<ba::io_context>();
    boost::asio::executor_work_guard<ba::io_context::executor_type> workGuard(io->get_executor());
    std::thread ioThread([io] { io->run(); });

    ba::ip::tcp::acceptor acceptor(*io, ba::ip::tcp::endpoint(ba::ip::tcp::v4(), 0));
    ba::ip::tcp::socket client(*io);
    boost::system::error_code connectError;
    client.connect(acceptor.local_endpoint(), connectError);
    BOOST_REQUIRE(!connectError);
    // Kept open until the session is destroyed so drop()'s ssl async_shutdown completes.
    ba::ip::tcp::socket serverSide(*io);
    acceptor.accept(serverSide);

    ba::ssl::context sslContext(ba::ssl::context::tlsv12);
    {
        auto fakeHost = std::make_shared<FakeHost<bcos::network::Socket>>(fakeAsio, nullptr);
        auto sessionSocket = makeLoopbackSocket(io, sslContext, std::move(client));
        auto session = std::make_shared<Session>(sessionSocket, *fakeHost, 2, true);
        session->startWithPolicy<FakeASIO::ReadPolicy>();

        P2PInfo selfInfo;
        selfInfo.rawP2pID = "selfRawP2pID";
        selfInfo.p2pID = "selfP2pID";
        auto service = std::make_shared<Service>(selfInfo);
        service->setBeforeMessageHandler(
            [](Session&, const Message&, uint32_t) -> std::optional<bcos::Error> {
                return bcos::Error::buildError(
                    "", bcos::network::P2PExceptionType::OutBWOverflow, "outgoing bandwidth overflow");
            });

        auto p2pSession = std::make_shared<P2PSession>();
        p2pSession->setSession(session);
        p2pSession->setService(service);

        Message message;
        message.setSeq(1);
        bytes payload{1, 2, 3, 4};
        BOOST_CHECK_THROW(
            task::syncWait(p2pSession->fastSendP2PMessage(
                message, ::ranges::views::single(bcos::ref(std::as_const(payload))), bcos::network::Options{})),
            bcos::network::NetworkException);

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

    workGuard.reset();
    {
        boost::system::error_code ec;
        serverSide.close(ec);
        acceptor.close(ec);
    }
    io->stop();
    ioThread.join();
}

// Service::m_sessions is protected; expose insertion for the fan-out test below.
class FanoutProbeService : public bcos::gateway::Service
{
public:
    explicit FanoutProbeService(P2PInfo const& _info) : Service(_info) {}
    void addSession(bcos::network::P2pID const& _nodeID, P2PSession::Ptr _session)
    {
        std::unique_lock lock(x_sessions);
        m_sessions[_nodeID] = std::move(_session);
    }
};

BOOST_AUTO_TEST_CASE(sendMessageCompression)
{
    // The COMPRESS ext flag is stamped only onto the encoded wire header inside
    // P2PSession::fastSendP2PMessage: the caller's message is never mutated, so a reused message
    // object (broadcast fan-out / retry loop) that compresses for one peer cannot leak the flag
    // to a later peer that receives an uncompressed frame (which would fail to decompress and
    // drop the connection). Also exercises the compression branch itself, which the
    // inert fake-socket tests cannot reach.
    auto fakeAsio = std::make_shared<FakeASIO>();

    auto io = std::make_shared<ba::io_context>();
    boost::asio::executor_work_guard<ba::io_context::executor_type> workGuard(io->get_executor());
    std::thread ioThread([io] { io->run(); });

    ba::ip::tcp::acceptor acceptor(*io, ba::ip::tcp::endpoint(ba::ip::tcp::v4(), 0));
    auto listenEndpoint = acceptor.local_endpoint();

    // TLS contexts for the loopback: the session's reads/writes dispatch on its ssl::stream at
    // compile time, so the wire speaks real TLS (see unittests/utils/TlsLoopback.h).
    auto serverCtx = testutil::makeTlsServerContext();
    auto clientCtx = testutil::makeTlsClientContext();

    std::vector<uint8_t> received;
    std::mutex recvMutex;
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
        if (ec)
        {
            return;
        }
        // Read one chunk: loopback delivers the whole (small, compressed) frame in a single TLS
        // record, hence in a single read_some. A single read also avoids blocking this thread
        // forever if the session never closes the socket.
        std::array<uint8_t, 4096> buf;
        std::size_t n = tlsPeer.read_some(ba::buffer(buf), ec);
        if (!ec && n > 0)
        {
            std::lock_guard<std::mutex> lock(recvMutex);
            received.assign(buf.begin(), buf.begin() + n);
        }
    });

    ba::ip::tcp::socket client(*io);
    boost::system::error_code connectError;
    client.connect(listenEndpoint, connectError);
    BOOST_REQUIRE(!connectError);

    {
        auto fakeHost = std::make_shared<FakeHost<bcos::network::Socket>>(fakeAsio, nullptr);
        auto sessionSocket = testutil::makeTlsSessionSocket(io, clientCtx, std::move(client));
        auto session = std::make_shared<Session>(sessionSocket, *fakeHost, 2, true);
        session->startWithPolicy<FakeASIO::ReadPolicy>();

        P2PInfo selfInfo;
        selfInfo.rawP2pID = "selfRawP2pID";
        selfInfo.p2pID = "selfP2pID";
        auto service = std::make_shared<Service>(selfInfo);
        service->setEnableCompress(true);

        auto p2pSession = std::make_shared<P2PSession>();
        p2pSession->setSession(session);
        p2pSession->setService(service);
        // V2 wire format + payload well above the 1KB compress threshold -> compression must run
        // (fastSendP2PMessage stamps the session-negotiated version onto the message)
        auto protocolInfo = std::make_shared<bcos::protocol::ProtocolInfo>(
            bcos::protocol::ProtocolModuleID::GatewayService, 0, 2);
        protocolInfo->setVersion((uint16_t)bcos::protocol::ProtocolVersion::V2);
        p2pSession->setProtocolInfo(protocolInfo);

        Message message;
        message.setSeq(1);
        bytes payload(2000, 'x');
        auto originalExt = message.ext();

        task::syncWait(p2pSession->fastSendP2PMessage(
            message, ::ranges::views::single(bcos::ref(std::as_const(payload))), bcos::network::Options{}));

        // fastSendP2PMessage never mutates the caller's message — the COMPRESS flag only rides on
        // the wire header, so the caller's ext is untouched.
        BOOST_CHECK_EQUAL(message.ext(), originalExt);

        // Clean teardown: disconnect closes the socket and stops the read loop. Do NOT null the
        // socket while the io thread may still run the session's idle timer (checkNetworkStatus
        // would dereference a null m_socket).
        session->disconnect(bcos::network::DisconnectReason::DisconnectRequested);

        // The session's read is parked in the fake ASIO (not on the real socket), so the
        // disconnect does not unwind it — fail it explicitly and wait for the read loop to
        // finish before the fake is destroyed.
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
    // Stop the io explicitly: drop()'s ssl-shutdown path can leave the session socket open (the
    // failed SSL shutdown cancels the force-close timer), and an open socket/acceptor would keep
    // io_context::run() from returning.
    {
        boost::system::error_code ec;
        acceptor.close(ec);
    }
    io->stop();
    ioThread.join();

    // The wire frame must actually be compressed: parse the header
    // [length:4][version:2][packetType:2][seq:4][ext:2] (Message::MESSAGE_HEADER_LENGTH = 14).
    BOOST_REQUIRE(received.size() >= Message::MESSAGE_HEADER_LENGTH);
    uint16_t frameExt =
        (static_cast<uint16_t>(received[12]) << 8) | static_cast<uint16_t>(received[13]);
    BOOST_CHECK(frameExt & bcos::protocol::MessageExtFieldFlag::COMPRESS);
}

BOOST_AUTO_TEST_CASE(fastSendBroadcastFanoutMixedVersion)
{
    // Round-4 review finding 2: the per-peer fan-out invariant — "each task's per-session
    // src/dst/version stamping runs synchronously before its first suspension, so the tasks never
    // race on the shared header" — is only documented in comments. This drives the real
    // Service::broadcastMessageToNeighbors over two real loopback sockets whose sessions
    // negotiated different protocol versions (V2 and V0) and asserts each peer receives a frame
    // carrying its OWN negotiated version: the shared message is stamped per-peer before each
    // header encode and the parallel fan-out tasks never cross-contaminate each other's wire
    // header.
    auto fakeAsio = std::make_shared<FakeASIO>();

    auto io = std::make_shared<ba::io_context>();
    boost::asio::executor_work_guard<ba::io_context::executor_type> workGuard(io->get_executor());
    std::thread ioThread([io] { io->run(); });

    ba::ip::tcp::acceptor acceptorV2(*io, ba::ip::tcp::endpoint(ba::ip::tcp::v4(), 0));
    ba::ip::tcp::acceptor acceptorV0(*io, ba::ip::tcp::endpoint(ba::ip::tcp::v4(), 0));
    auto listenV2 = acceptorV2.local_endpoint();
    auto listenV0 = acceptorV0.local_endpoint();

    // TLS contexts for the loopbacks: the sessions' reads/writes dispatch on their ssl::stream at
    // compile time, so the wires speak real TLS (see unittests/utils/TlsLoopback.h). The contexts
    // must outlive every Socket/peer stream (an ssl::stream references its context).
    auto serverCtx = testutil::makeTlsServerContext();
    auto clientCtx = testutil::makeTlsClientContext();

    std::vector<uint8_t> receivedV2;
    std::vector<uint8_t> receivedV0;
    std::mutex recvMutex;
    // Read exactly one wire frame: [length:4][payload...]. The length field counts the whole
    // frame including itself, so after reading the 4 length bytes we read len-4 more. Works on
    // the TLS peer stream — boost::asio::read decrypts transparently.
    auto readExactFrame = [](auto& _peer) -> std::vector<uint8_t> {
        std::array<uint8_t, 4> lenBuf;
        boost::system::error_code ec;
        std::size_t n = boost::asio::read(_peer, ba::buffer(lenBuf), ec);
        if (ec || n != lenBuf.size())
        {
            return {};
        }
        uint32_t len = (uint32_t(lenBuf[0]) << 24) | (uint32_t(lenBuf[1]) << 16) |
                       (uint32_t(lenBuf[2]) << 8) | lenBuf[3];
        if (len < lenBuf.size() || len > 4096)
        {
            return {};
        }
        std::vector<uint8_t> frame(len);
        std::copy(lenBuf.begin(), lenBuf.end(), frame.begin());
        n = boost::asio::read(_peer, ba::buffer(frame.data() + 4, len - 4), ec);
        if (ec || n != len - 4)
        {
            return {};
        }
        return frame;
    };
    std::thread peerV2([&] {
        ba::ip::tcp::socket peer(*io);
        boost::system::error_code ec;
        acceptorV2.accept(peer, ec);
        if (ec)
        {
            return;
        }
        testutil::PeerSslStream tlsPeer(std::move(peer), serverCtx);
        tlsPeer.handshake(ba::ssl::stream_base::server, ec);
        if (ec)
        {
            return;
        }
        // Service is not run in this test, so P2PSession::start()'s initial heartbeat is skipped
        // (heartBeat only sends when service->active()) — the broadcast frame is the first one.
        auto frame = readExactFrame(tlsPeer);
        std::lock_guard<std::mutex> lock(recvMutex);
        receivedV2 = std::move(frame);
    });
    std::thread peerV0([&] {
        ba::ip::tcp::socket peer(*io);
        boost::system::error_code ec;
        acceptorV0.accept(peer, ec);
        if (ec)
        {
            return;
        }
        testutil::PeerSslStream tlsPeer(std::move(peer), serverCtx);
        tlsPeer.handshake(ba::ssl::stream_base::server, ec);
        if (ec)
        {
            return;
        }
        auto frame = readExactFrame(tlsPeer);
        std::lock_guard<std::mutex> lock(recvMutex);
        receivedV0 = std::move(frame);
    });

    ba::ip::tcp::socket clientV2(*io);
    ba::ip::tcp::socket clientV0(*io);
    {
        boost::system::error_code connectError;
        clientV2.connect(listenV2, connectError);
        BOOST_REQUIRE(!connectError);
        clientV0.connect(listenV0, connectError);
        BOOST_REQUIRE(!connectError);
    }

    P2PInfo selfInfo;
    selfInfo.rawP2pID = "selfRawP2pID";
    selfInfo.p2pID = "selfP2pID";
    auto service = std::make_shared<FanoutProbeService>(selfInfo);
    // P2PSession::start() -> heartBeat() arms a timer on service->host()->asioInterface().
    service->setHost(std::make_shared<FakeHost<bcos::network::Socket>>(fakeAsio, nullptr));

    // The FakeHosts must outlive the sessions (Session holds a reference_wrapper<Host>).
    std::vector<std::shared_ptr<FakeHost<bcos::network::Socket>>> hosts;
    std::vector<std::shared_ptr<Session>> sessions;
    auto makePeerSession = [&](ba::ip::tcp::socket _client, bcos::network::P2pID _nodeID, uint32_t _version) {
        auto host = std::make_shared<FakeHost<bcos::network::Socket>>(fakeAsio, nullptr);
        hosts.push_back(host);
        auto session = std::make_shared<Session>(
            testutil::makeTlsSessionSocket(io, clientCtx, std::move(_client)), *host, 2, true);
        session->startWithPolicy<FakeASIO::ReadPolicy>();
        sessions.push_back(session);

        auto p2pSession = std::make_shared<P2PSession>();
        p2pSession->setSession(session);
        p2pSession->setService(service);
        auto protocolInfo = std::make_shared<bcos::protocol::ProtocolInfo>(
            bcos::protocol::ProtocolModuleID::GatewayService, 0, 2);
        protocolInfo->setVersion(_version);
        p2pSession->setProtocolInfo(protocolInfo);
        P2PInfo peerInfo;
        peerInfo.rawP2pID = _nodeID;
        peerInfo.p2pID = _nodeID;
        p2pSession->setP2PInfo(peerInfo);
        // marks the session active (m_run) and sends an initial heartbeat (discarded by the peer)
        p2pSession->start();
        service->addSession(_nodeID, std::move(p2pSession));
    };
    makePeerSession(std::move(clientV2), "peerV2", 2);
    makePeerSession(std::move(clientV0), "peerV0", 0);

    // One shared message broadcast to both sessions: each per-peer fan-out task stamps the
    // negotiated version synchronously before its own header encode. Round-6 review: stamp
    // non-empty routing fields as well, so the V2 wire frame carries a real ttl/src/dst extension
    // that the V2-peer decode below asserts — a regression that encodes a V2 frame through the
    // base-class header (e.g. an object-sliced message, which writes no ttl/src/dst extension)
    // would fail that decode immediately.
    auto message = std::make_shared<Message>();
    message->setPacketType(GatewayMessageType::SyncNodeSeq);
    message->setSeq(0x1234);
    message->setSrcP2PNodeID("srcNodeID");
    message->setDstP2PNodeID("dstNodeID");
    bytes payload(32, 'a');
    task::syncWait(service->broadcastMessageToNeighbors(
        message, ::ranges::views::single(bcos::ref(std::as_const(payload))), bcos::network::Options{}));

    // Session::write() posts the write-loop launch to the socket's io thread, so the broadcast
    // returning only means each per-peer payload is QUEUED (the fan-out tasks are fire-and-
    // forget). Wait (bounded) until both peers have actually received their frame before
    // disconnecting below — otherwise the disconnect races the posted launch and drop() fails
    // the queued payload instead of writing it. A genuinely lost frame still fails the size
    // assertions at the end; the bound only keeps the peer reads from hanging forever first.
    for (size_t frameRetry = 0; frameRetry < 500; ++frameRetry)
    {
        {
            std::lock_guard<std::mutex> lock(recvMutex);
            if (!receivedV2.empty() && !receivedV0.empty())
            {
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Round-7 review: disconnect the sessions BEFORE joining the peer threads. Each peer thread
    // blocks on a synchronous asio read; if a broadcast regression ever skipped a peer, that
    // thread would block forever and a disconnect placed after join() would never run — surfacing
    // as a CI hang instead of a failure. Closing the client sockets first unblocks any stuck peer
    // read so a missed peer fails the test.
    for (auto& session : sessions)
    {
        session->disconnect(bcos::network::DisconnectReason::DisconnectRequested);
    }

    // The sessions' reads are parked in the fake ASIO (not on the real sockets), so the
    // disconnects do not unwind them — fail them explicitly and wait for the read loops to
    // finish before the fake is destroyed.
    fakeAsio->stopReads();
    size_t drainRetry = 0;
    while (fakeAsio->readsInFlight() != 0 && drainRetry < 200)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        drainRetry++;
    }
    BOOST_REQUIRE_EQUAL(fakeAsio->readsInFlight(), 0);

    peerV2.join();
    peerV0.join();

    workGuard.reset();
    {
        boost::system::error_code ec;
        acceptorV2.close(ec);
        acceptorV0.close(ec);
    }
    io->stop();
    ioThread.join();

    // The V0 frame must carry the negotiated V0 version (base header only): the version field is
    // bytes [4..6) of the fixed base header [length:4][version:2][packetType:2][seq:4][ext:2].
    BOOST_REQUIRE(receivedV0.size() >= Message::MESSAGE_HEADER_LENGTH);
    uint16_t versionV0 = (static_cast<uint16_t>(receivedV0[4]) << 8) | receivedV0[5];
    BOOST_CHECK_EQUAL(versionV0, 0);

    // The V2 frame must decode as a real V2 message carrying the full routing extension. Round-6
    // review: decode the whole frame with Message::decode (instead of hand-reading the
    // version bytes) and assert src/dst/ttl, so "V2 version written but ttl/src/dst missing" (the
    // exact shape of the round-2..5 object-slicing defect) fails here.
    BOOST_REQUIRE(receivedV2.size() >= Message::MESSAGE_HEADER_LENGTH);
    Message decodedV2;
    int32_t decodedOffset = decodedV2.decode(bcos::ref(receivedV2));
    BOOST_REQUIRE(decodedOffset > 0);
    BOOST_CHECK_EQUAL(decodedV2.version(), 2);
    BOOST_CHECK_EQUAL(decodedV2.srcP2PNodeID(), "srcNodeID");
    BOOST_CHECK_EQUAL(decodedV2.dstP2PNodeID(), "dstNodeID");
    BOOST_CHECK_EQUAL(decodedV2.ttl(), 10);
}

BOOST_AUTO_TEST_SUITE_END()
