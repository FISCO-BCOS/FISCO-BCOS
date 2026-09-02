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
 * @brief test for gateway
 * @file SessionTest.cpp
 * @author: octopus
 * @date 2023-02-23
 */
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-framework/protocol/ProtocolInfo.h"
#include "bcos-gateway/libnetwork/ASIOInterface.h"
#include "bcos-gateway/libnetwork/Host.h"
#include "bcos-gateway/libnetwork/Session.h"
#include "bcos-gateway/libnetwork/SessionReadLoop.h"
#include "bcos-gateway/libp2p/P2PMessage.h"
#include "bcos-gateway/libp2p/P2PMessageV2.h"
#include "bcos-gateway/libp2p/P2PSession.h"
#include "bcos-gateway/libp2p/Service.h"
#include <bcos-framework/protocol/Protocol.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/IOServicePool.h>
#include "bcos-utilities/testutils/TestPromptFixture.h"
#include <boost/asio/ip/tcp.hpp>
#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <queue>
#include <list>
#include <range/v3/view/single.hpp>
#include <thread>
#include <tuple>
#include <unordered_set>
#include <vector>

using namespace bcos;
using namespace gateway;
using namespace bcos::test;
using namespace bcos::crypto;

BOOST_FIXTURE_TEST_SUITE(SessionTest, TestPromptFixture)


class FakeASIO : public bcos::gateway::ASIOInterface
{
public:
    using Packet = std::shared_ptr<std::vector<uint8_t>>;
    using ReadCompletion =
        bcos::gateway::detail::AsioCompletion<boost::system::error_code, std::size_t>;

    FakeASIO()
      : ASIOInterface(std::make_shared<bcos::IOServicePool>(1, "FakeASIO"), "0.0.0.0", 0),
        m_threadPool(std::make_shared<bcos::IOServicePool>(1, "FakeASIO"))
    {}
    virtual ~FakeASIO() noexcept override {};

    // Compile-time read-initiation policy (see ASIOInterface::awaitableReadSome): the read loop
    // is launched with this policy (startWithPolicy<FakeASIO::ReadPolicy>) so every read parks
    // its completion here instead of arming the real async_read_some.
    struct ReadPolicy
    {
        static void invoke(ASIOInterface* asio, const std::shared_ptr<SocketFace>& /*socket*/,
            ba::mutable_buffer buffers, ReadCompletion completion)
        {
            dynamic_cast<FakeASIO*>(asio)->parkRead(buffers, std::move(completion));
        }
    };

    // Read-policy target (see FakeASIO::ReadPolicy): park the read's completion and feed it
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

    // Synchronous helper for fakeClassTest (no session involved).
    template <typename Handler>
    void readSome(std::shared_ptr<SocketFace> /*socket*/, ba::mutable_buffer buffers,
        Handler&& handler)
    {
        handler(boost::system::error_code(), drainPackets(buffers));
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

public:  // for testing
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
                limit = limit - bytesTransferred;
                boost::asio::buffer_copy(buffers, boost::asio::buffer(*packet), limit);
                bytesTransferred += limit;
                packet->erase(packet->begin(), packet->begin() + limit);
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

class FakeP2PMessage : public P2PMessage
{
public:
    using Ptr = std::shared_ptr<FakeP2PMessage>;
    int32_t decode(const bytesConstRef& _buffer) override
    {
        if (_buffer.size() == 0)
        {
            return MessageDecodeStatus::MESSAGE_INCOMPLETE;
        }

        uint8_t length = _buffer[0];
        m_length = length;
        if (_buffer.size() < length)
        {
            return MessageDecodeStatus::MESSAGE_INCOMPLETE;
        }

        // check packet is right
        for (uint8_t i = 1; i < length; ++i)
        {
            if (_buffer[i] != uint8_t(0xff))
            {
                BOOST_CHECK(false);
                return MessageDecodeStatus::MESSAGE_ERROR;
            }
        }

        m_payload.assign(_buffer.begin(), _buffer.begin() + length);
        return length;
    }
};

class FakeMessageFactory : public P2PMessageFactory
{
public:
    Message::Ptr buildMessage() override
    {
        auto message = std::make_shared<FakeP2PMessage>();
        return message;
    }
};

class FakeMessagesBuilder
{
public:
    using Packet = FakeASIO::Packet;
    FakeMessagesBuilder(std::size_t packetNum)
    {
        for (std::size_t i = 0; i < packetNum; ++i)
        {
            uint8_t randPacketSize = rand() % 0xfe + 1;  // 1 ~ 254
            Packet packet = buildPacket(randPacketSize);
            m_sendBuffer.insert(m_sendBuffer.end(), packet->begin(), packet->end());
        }

        // cut sendBuffer randomly and generate sendPacket
        std::size_t offset = 0;
        while (offset < m_sendBuffer.size())
        {
            uint8_t randPacketSize = rand() % 0x7f + 1;  // 1 ~ 127
            std::size_t packetSize =
                std::min(std::size_t(randPacketSize), m_sendBuffer.size() - offset);
            Packet packet = std::make_shared<std::vector<uint8_t>>(
                m_sendBuffer.begin() + offset, m_sendBuffer.begin() + offset + packetSize);
            m_sendPackets.push(packet);
            offset += packetSize;
        }

        std::cout << "Fake message build with : " << m_sendBuffer.size() << " bytes and "
                  << packetNum << " packets" << std::endl;
    }

    Packet nextPacket()
    {
        if (m_sendPackets.empty())
        {
            return nullptr;
        }
        auto packet = m_sendPackets.front();
        m_sendPackets.pop();
        return packet;
    }

    bool isSameSendBuffer(const bytesConstRef& _buffer)
    {
        if (_buffer.size() != m_sendBuffer.size())
        {
            return false;
        }
        for (std::size_t i = 0; i < _buffer.size(); ++i)
        {
            if (_buffer[i] != m_sendBuffer[i])
            {
                return false;
            }
        }
        return true;
    }

    size_t sendBufferSize() { return m_sendBuffer.size(); }

private:
    Packet buildPacket(uint8_t size)
    {
        // [size][0xff][0xff]...[0xff]
        // 1B    size-1B
        auto packet = std::make_shared<std::vector<uint8_t>>(size);
        (*packet)[0] = size;
        for (uint8_t i = 1; i < size; ++i)
        {
            (*packet)[i] = uint8_t(0xff);
        }
        return packet;
    }

    std::vector<uint8_t> m_sendBuffer;
    std::queue<Packet> m_sendPackets;
};

class FakeHost : public bcos::gateway::Host
{
public:
    FakeHost(bcos::crypto::Hash::Ptr _hash, std::shared_ptr<ASIOInterface> _asioInterface,
        std::shared_ptr<SessionFactory> _sessionFactory, MessageFactory::Ptr _messageFactory)
      : Host(_hash, _asioInterface, _sessionFactory, _messageFactory)
    {
        m_run = true;
    }
};

class FakeSocket : public SocketFace
{
public:
    FakeSocket()
      : SocketFace(),
        m_sslContext(ba::ssl::context::tlsv12),
        m_workGuard(boost::asio::make_work_guard(m_ioService))
    {
        // A connected TCP pair backs the SSL stream so drop()/closeSocket() can call
        // sslref().async_shutdown() safely (mirrors FakeSocket_FIB): the acceptor-side socket
        // closes when it goes out of scope, so the shutdown completes with an error instead of
        // dereferencing an unconnected stream.
        bi::tcp::acceptor acceptor(m_ioService, bi::tcp::endpoint(bi::tcp::v4(), 0));
        auto endpoint = acceptor.local_endpoint();
        bi::tcp::socket clientSocket(m_ioService);
        clientSocket.connect(endpoint);
        bi::tcp::socket serverSocket(m_ioService);
        acceptor.accept(serverSocket);
        m_sslSocket = std::make_shared<ba::ssl::stream<bi::tcp::socket>>(
            std::move(clientSocket), m_sslContext);
        m_worker = std::thread([this]() { m_ioService.run(); });
    };
    ~FakeSocket() override
    {
        m_workGuard.reset();
        m_ioService.stop();
        if (m_worker.joinable())
        {
            m_worker.join();
        }
    }
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
    ba::io_context& ioService() override { return m_ioService; }

private:
    // Declaration order matters: members are destroyed in reverse, so the io_context must
    // outlive the SSL stream that deregisters from it in its destructor.
    ba::io_context m_ioService;
    ba::ssl::context m_sslContext;
    std::shared_ptr<ba::ssl::stream<bi::tcp::socket>> m_sslSocket;
    boost::asio::executor_work_guard<ba::io_context::executor_type> m_workGuard;
    std::thread m_worker;
    NodeIPEndpoint m_nodeIPEndpoint;
    bool m_connected{true};
};

BOOST_AUTO_TEST_CASE(fakeClassTest)
{
    auto totalPacketNum = 50;

    FakeMessagesBuilder messageBuilder(totalPacketNum);
    std::vector<uint8_t> recvBuffer;
    auto asio = std::make_shared<FakeASIO>();

    while (auto packet = messageBuilder.nextPacket())
    {
        std::vector<uint8_t> readBuffer(10240);
        asio->appendRecvPacket(packet);
        asio->readSome(nullptr, boost::asio::buffer(readBuffer),
            [&recvBuffer, &readBuffer](
                const boost::system::error_code& ec, std::size_t bytesTransferred) {
                recvBuffer.insert(
                    recvBuffer.end(), readBuffer.begin(), readBuffer.begin() + bytesTransferred);
                std::cout << "receive " << bytesTransferred << " bytes" << std::endl;
            });
    }

    BOOST_CHECK(messageBuilder.isSameSendBuffer(ref(recvBuffer)));
}

BOOST_AUTO_TEST_CASE(doReadTest)
{
    auto totalPacketNum = 500;
    FakeMessagesBuilder messageBuilder(totalPacketNum);
    auto fakeMessageFactory = std::make_shared<FakeMessageFactory>();
    auto hashImpl = std::make_shared<Keccak256>();
    auto fakeSocket = std::make_shared<FakeSocket>();

    std::atomic<size_t> recvPacketCnt = 0;
    std::atomic<size_t> recvBufferSize = 0;
    std::atomic<uint64_t> lastReadTime = utcSteadyTime();
    auto fakeAsio = std::make_shared<FakeASIO>();
    {
        auto fakeHost = std::make_shared<FakeHost>(hashImpl, fakeAsio, nullptr, fakeMessageFactory);

        auto session = std::make_shared<Session>(fakeSocket, *fakeHost, 2, true);
        session->setMessageFactory(fakeHost->messageFactory());

        session->setMessageHandler(
            [&recvPacketCnt, &recvBufferSize, &lastReadTime](
                NetworkException e, SessionFace::Ptr sessionFace, Message::Ptr message) {
                // the read loop calls this function after reading a message
                lastReadTime = utcSteadyTime();
                if (e.errorCode() != P2PExceptionType::Success)
                {
                    std::cout << "error: " << e.errorCode() << " " << e.what() << std::endl;
                }
                {
                    static bcos::SharedMutex x_mutex;
                    bcos::WriteGuard guard(x_mutex);
                    BOOST_CHECK_EQUAL(e.errorCode(), P2PExceptionType::Success);
                    BOOST_CHECK(message);
                    BOOST_CHECK(message->lengthDirect() > 0);
                }

                recvBufferSize += message->lengthDirect();
                recvPacketCnt++;
            });

        session->startWithPolicy<FakeASIO::ReadPolicy>();

        // send packets
        while (auto packet = messageBuilder.nextPacket())
        {
            std::dynamic_pointer_cast<FakeASIO>(fakeHost->asioInterface())
                ->asyncAppendRecvPacket(packet);
        }

        size_t retryTimes = 0;
        while (auto restPacket = totalPacketNum - recvPacketCnt)
        {
            std::cout << "waiting " << restPacket << " packets" << std::endl;
            retryTimes++;
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            BOOST_CHECK(retryTimes < 100);
        }

        BOOST_CHECK_EQUAL(recvPacketCnt, totalPacketNum);
        BOOST_CHECK_EQUAL(recvBufferSize, messageBuilder.sendBufferSize());

        // Teardown: a read is still parked in the fake. Swap in a tolerant message handler
        // first — failing the parked read drops the session, and the teardown notification
        // would otherwise reach the strict handler above with a Disconnect error — then let the
        // fake fail the read and wait for the read loop to unwind completely before nulling
        // the socket.
        session->setMessageHandler([](NetworkException, SessionFace::Ptr, Message::Ptr) {});
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

BOOST_AUTO_TEST_CASE(startUsesDefaultReadPolicy)
{
    // Coverage for the production read entry point: Session::start() is the only instantiation
    // of readLoop<ASIOInterface::DefaultReadPolicy> (every other test enters via
    // startWithPolicy<FakePolicy>). Drive DefaultReadPolicy down its deterministic
    // unexpected-type branch — an m_type that is neither TCP_ONLY nor SSL completes the read
    // with a posted operation_not_supported — and assert the read loop drops the session.
    // (m_type defaults to TCP_ONLY, so "unset" would arm a real async_read_some on the fake's
    // connected socket pair instead; the invalid type is what makes the branch deterministic.)
    auto fakeMessageFactory = std::make_shared<FakeMessageFactory>();
    auto hashImpl = std::make_shared<Keccak256>();
    auto fakeSocket = std::make_shared<FakeSocket>();
    auto fakeAsio = std::make_shared<FakeASIO>();
    fakeAsio->setType(2);
    {
        auto fakeHost =
            std::make_shared<FakeHost>(hashImpl, fakeAsio, nullptr, fakeMessageFactory);

        auto session = std::make_shared<Session>(fakeSocket, *fakeHost, 2, true);
        session->setMessageFactory(fakeHost->messageFactory());
        // Tolerant handler: the read error drops the session, and the drop notifies.
        session->setMessageHandler([](NetworkException, SessionFace::Ptr, Message::Ptr) {});

        session->start();  // virtual production entry — NOT startWithPolicy<>

        size_t retryTimes = 0;
        while (session->active() && retryTimes < 200)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            retryTimes++;
        }
        BOOST_CHECK(!session->active());
        // drop() captured the socket into a local shared_ptr before clearing m_active, so the
        // socket can be nulled as soon as the session is inactive (same teardown as doReadTest).
        session->setSocket(nullptr);
    }

    fakeSocket->close();
}

BOOST_AUTO_TEST_CASE(fastSendMessageOutboundRateLimit)
{
    // The fast path must honour the same pre-send (outgoing rate-limit) check the removed callback
    // path (asyncSendMessage) enforced: a beforeMessageHandler rejection surfaces as a thrown
    // NetworkException (e.g. OutBWOverflow) so coroutine retry loops can stop.
    auto fakeMessageFactory = std::make_shared<FakeMessageFactory>();
    auto hashImpl = std::make_shared<Keccak256>();
    auto fakeSocket = std::make_shared<FakeSocket>();
    auto fakeAsio = std::make_shared<FakeASIO>();
    {
        auto fakeHost = std::make_shared<FakeHost>(hashImpl, fakeAsio, nullptr, fakeMessageFactory);
        auto session = std::make_shared<Session>(fakeSocket, *fakeHost, 2, true);
        session->setMessageFactory(fakeHost->messageFactory());
        session->setBeforeMessageHandler(
            [](SessionFace&, const Message&, uint32_t) -> std::optional<bcos::Error> {
                return bcos::Error::buildError(
                    "", P2PExceptionType::OutBWOverflow, "outgoing bandwidth overflow");
            });
        session->startWithPolicy<FakeASIO::ReadPolicy>();

        P2PMessage message;
        message.setSeq(1);
        bytes payload{1, 2, 3, 4};
        BOOST_CHECK_THROW(
            task::syncWait(session->fastSendMessage(
                message, ::ranges::views::single(bcos::ref(std::as_const(payload))), Options{})),
            NetworkException);

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

// A SocketFace backed by a real connected TCP socket so the ASIOInterface::awaitableWrite path
// actually completes on the wire — the FakeSocket above cannot reach the compression branch
// because its write path is inert.
class RealLoopbackSocket : public SocketFace
{
public:
    RealLoopbackSocket(std::shared_ptr<ba::io_context> _ioContext, bi::tcp::socket _socket)
      : m_ioContext(std::move(_ioContext)),
        m_sslContext(ba::ssl::context::tlsv12),
        m_sslSocket(std::make_shared<ba::ssl::stream<bi::tcp::socket>>(*m_ioContext, m_sslContext))
    {
        m_sslSocket->next_layer() = std::move(_socket);
    }

    bool isConnected() const override { return m_sslSocket->next_layer().is_open(); }
    void close() override
    {
        boost::system::error_code ec;
        m_sslSocket->next_layer().close(ec);
    }
    bi::tcp::endpoint remoteEndpoint(boost::system::error_code) override { return {}; }
    bi::tcp::endpoint localEndpoint(boost::system::error_code) override { return {}; }
    bi::tcp::socket& ref() override { return m_sslSocket->next_layer(); }
    ba::ssl::stream<bi::tcp::socket>& sslref() override { return *m_sslSocket; }
    const NodeIPEndpoint& nodeIPEndpoint() const override { return m_nodeIPEndpoint; }
    void setNodeIPEndpoint(NodeIPEndpoint _nodeIPEndpoint) override
    {
        m_nodeIPEndpoint = std::move(_nodeIPEndpoint);
    }
    ba::io_context& ioService() override { return *m_ioContext; }

private:
    std::shared_ptr<ba::io_context> m_ioContext;
    ba::ssl::context m_sslContext;
    std::shared_ptr<ba::ssl::stream<bi::tcp::socket>> m_sslSocket;
    NodeIPEndpoint m_nodeIPEndpoint;
};

// Service::m_sessions is protected; expose insertion for the fan-out test below.
class FanoutProbeService : public bcos::gateway::Service
{
public:
    explicit FanoutProbeService(P2PInfo const& _info) : Service(_info) {}
    void addSession(P2pID const& _nodeID, P2PSession::Ptr _session)
    {
        std::unique_lock lock(x_sessions);
        m_sessions[_nodeID] = std::move(_session);
    }
};

BOOST_AUTO_TEST_CASE(fastSendMessageCompression)
{
    // The COMPRESS ext flag is stamped only onto the encoded wire header inside fastSendMessage:
    // the caller's message is const and never mutated, so a reused message object (broadcast
    // fan-out / retry loop) that compresses for one peer cannot leak the flag to a later peer that
    // receives an uncompressed frame (which would fail to decompress and drop the connection).
    // Also exercises the compression branch itself, which the FakeSocket-based tests cannot reach.
    auto fakeMessageFactory = std::make_shared<FakeMessageFactory>();
    auto hashImpl = std::make_shared<Keccak256>();
    auto fakeAsio = std::make_shared<FakeASIO>();

    auto io = std::make_shared<ba::io_context>();
    boost::asio::executor_work_guard<ba::io_context::executor_type> workGuard(io->get_executor());
    std::thread ioThread([io] { io->run(); });

    ba::ip::tcp::acceptor acceptor(*io, ba::ip::tcp::endpoint(ba::ip::tcp::v4(), 0));
    auto listenEndpoint = acceptor.local_endpoint();

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
        // Read one chunk: loopback delivers the whole (small, compressed) frame in a single
        // read_some. A single read also avoids blocking this thread forever if the session never
        // closes the socket.
        std::array<uint8_t, 4096> buf;
        std::size_t n = peer.read_some(ba::buffer(buf), ec);
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
        auto fakeHost = std::make_shared<FakeHost>(hashImpl, fakeAsio, nullptr, fakeMessageFactory);
        auto sessionSocket = std::make_shared<RealLoopbackSocket>(io, std::move(client));
        auto session = std::make_shared<Session>(sessionSocket, *fakeHost, 2, true);
        session->setMessageFactory(fakeHost->messageFactory());
        session->startWithPolicy<FakeASIO::ReadPolicy>();

        // V2 wire format + payload well above the 1KB compress threshold -> compression must run
        P2PMessage message;
        message.setVersion((uint16_t)bcos::protocol::ProtocolVersion::V2);
        message.setSeq(1);
        bytes payload(2000, 'x');
        auto originalExt = message.ext();

        task::syncWait(session->fastSendMessage(
            message, ::ranges::views::single(bcos::ref(std::as_const(payload))), Options{}));

        // fastSendMessage takes the message by const ref and never mutates it — the COMPRESS flag
        // only rides on the wire header, so the caller's ext is untouched.
        BOOST_CHECK_EQUAL(message.ext(), originalExt);

        // Clean teardown: disconnect closes the socket and stops the read loop. Do NOT null the
        // socket while the io thread may still run the session's idle timer (checkNetworkStatus
        // would dereference a null m_socket).
        session->disconnect(DisconnectReason::DisconnectRequested);

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
    // [length:4][version:2][packetType:2][seq:4][ext:2] (P2PMessage::MESSAGE_HEADER_LENGTH = 14).
    BOOST_REQUIRE(received.size() >= P2PMessage::MESSAGE_HEADER_LENGTH);
    uint16_t frameExt = (static_cast<uint16_t>(received[12]) << 8) |
                        static_cast<uint16_t>(received[13]);
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
    auto fakeMessageFactory = std::make_shared<FakeMessageFactory>();
    auto hashImpl = std::make_shared<Keccak256>();
    auto fakeAsio = std::make_shared<FakeASIO>();

    auto io = std::make_shared<ba::io_context>();
    boost::asio::executor_work_guard<ba::io_context::executor_type> workGuard(io->get_executor());
    std::thread ioThread([io] { io->run(); });

    ba::ip::tcp::acceptor acceptorV2(*io, ba::ip::tcp::endpoint(ba::ip::tcp::v4(), 0));
    ba::ip::tcp::acceptor acceptorV0(*io, ba::ip::tcp::endpoint(ba::ip::tcp::v4(), 0));
    auto listenV2 = acceptorV2.local_endpoint();
    auto listenV0 = acceptorV0.local_endpoint();

    std::vector<uint8_t> receivedV2;
    std::vector<uint8_t> receivedV0;
    std::mutex recvMutex;
    // Read exactly one wire frame: [length:4][payload...]. The length field counts the whole
    // frame including itself, so after reading the 4 length bytes we read len-4 more.
    auto readExactFrame = [](ba::ip::tcp::socket& _peer) -> std::vector<uint8_t> {
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
        // Service is not run in this test, so P2PSession::start()'s initial heartbeat is skipped
        // (heartBeat only sends when service->active()) — the broadcast frame is the first one.
        auto frame = readExactFrame(peer);
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
        auto frame = readExactFrame(peer);
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
    service->setMessageFactory(fakeMessageFactory);
    // P2PSession::start() -> heartBeat() arms a timer on service->host()->asioInterface().
    service->setHost(std::make_shared<FakeHost>(hashImpl, fakeAsio, nullptr, fakeMessageFactory));

    // The FakeHosts must outlive the sessions (Session holds a reference_wrapper<Host>).
    std::vector<std::shared_ptr<FakeHost>> hosts;
    std::vector<std::shared_ptr<Session>> sessions;
    auto makePeerSession = [&](ba::ip::tcp::socket _client, P2pID _nodeID, uint32_t _version) {
        auto host = std::make_shared<FakeHost>(hashImpl, fakeAsio, nullptr, fakeMessageFactory);
        hosts.push_back(host);
        auto session = std::make_shared<Session>(
            std::make_shared<RealLoopbackSocket>(io, std::move(_client)), *host, 2, true);
        session->setMessageFactory(fakeMessageFactory);
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
    auto message = std::make_shared<P2PMessageV2>();
    message->setPacketType(GatewayMessageType::SyncNodeSeq);
    message->setSeq(0x1234);
    message->setSrcP2PNodeID("srcNodeID");
    message->setDstP2PNodeID("dstNodeID");
    bytes payload(32, 'a');
    task::syncWait(service->broadcastMessageToNeighbors(
        message, ::ranges::views::single(bcos::ref(std::as_const(payload))), Options{}));

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
        session->disconnect(DisconnectReason::DisconnectRequested);
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
    BOOST_REQUIRE(receivedV0.size() >= P2PMessage::MESSAGE_HEADER_LENGTH);
    uint16_t versionV0 = (static_cast<uint16_t>(receivedV0[4]) << 8) | receivedV0[5];
    BOOST_CHECK_EQUAL(versionV0, 0);

    // The V2 frame must decode as a real V2 message carrying the full routing extension. Round-6
    // review: decode the whole frame with P2PMessageV2::decode (instead of hand-reading the
    // version bytes) and assert src/dst/ttl, so "V2 version written but ttl/src/dst missing" (the
    // exact shape of the round-2..5 object-slicing defect) fails here.
    BOOST_REQUIRE(receivedV2.size() >= P2PMessage::MESSAGE_HEADER_LENGTH);
    P2PMessageV2 decodedV2;
    int32_t decodedOffset = decodedV2.decode(bcos::ref(receivedV2));
    BOOST_REQUIRE(decodedOffset > 0);
    BOOST_CHECK_EQUAL(decodedV2.version(), 2);
    BOOST_CHECK_EQUAL(decodedV2.srcP2PNodeID(), "srcNodeID");
    BOOST_CHECK_EQUAL(decodedV2.dstP2PNodeID(), "dstNodeID");
    BOOST_CHECK_EQUAL(decodedV2.ttl(), 10);
}

BOOST_AUTO_TEST_CASE(fastSendConcurrentWriteOrder)
{
    // Regression for the per-session write ordering guarantee: the write path must serialize
    // concurrent producers. N threads call fastSendMessage concurrently and the single-writer
    // write loop (Session::writeLoop, guarded by the m_writingInFlight single-flight flag) must
    // put a complete, non-interleaved frame for every message on the wire — if the serialization
    // were ever broken, frames would be torn or interleaved. Uses a real loopback socket
    // (RealLoopbackSocket) so the full awaitableWrite -> async_write path runs, then parses the
    // accumulated byte stream into frames by their length prefix.
    constexpr size_t threadCount = 8;
    constexpr size_t msgPerThread = 50;
    constexpr size_t totalMsgs = threadCount * msgPerThread;

    auto fakeMessageFactory = std::make_shared<FakeMessageFactory>();
    auto hashImpl = std::make_shared<Keccak256>();
    auto fakeAsio = std::make_shared<FakeASIO>();

    auto io = std::make_shared<ba::io_context>();
    boost::asio::executor_work_guard<ba::io_context::executor_type> workGuard(io->get_executor());
    std::thread ioThread([io] { io->run(); });

    ba::ip::tcp::acceptor acceptor(*io, ba::ip::tcp::endpoint(ba::ip::tcp::v4(), 0));
    auto listenEndpoint = acceptor.local_endpoint();

    std::mutex recvMutex;
    std::vector<uint8_t> received;
    std::atomic<bool> peerDone{false};
    std::thread peerThread([&] {
        ba::ip::tcp::socket peer(*io);
        boost::system::error_code ec;
        acceptor.accept(peer, ec);
        if (ec)
        {
            return;
        }
        std::array<uint8_t, 4096> buf;
        while (!peerDone.load(std::memory_order_relaxed))
        {
            std::size_t n = peer.read_some(ba::buffer(buf), ec);
            if (ec)
            {
                break;
            }
            if (n > 0)
            {
                std::lock_guard<std::mutex> lock(recvMutex);
                received.insert(received.end(), buf.begin(), buf.begin() + n);
            }
        }
    });

    ba::ip::tcp::socket client(*io);
    boost::system::error_code connectError;
    client.connect(listenEndpoint, connectError);
    BOOST_REQUIRE(!connectError);

    // Each frame is the fixed 14-byte base header plus the 64-byte payload (payload is far below
    // the compress threshold, so no compression runs even at V2).
    const size_t frameSize = P2PMessage::MESSAGE_HEADER_LENGTH + 64;
    const size_t expectedBytes = frameSize * totalMsgs;

    {
        auto fakeHost = std::make_shared<FakeHost>(hashImpl, fakeAsio, nullptr, fakeMessageFactory);
        auto sessionSocket = std::make_shared<RealLoopbackSocket>(io, std::move(client));
        auto session = std::make_shared<Session>(sessionSocket, *fakeHost, 2, true);
        session->setMessageFactory(fakeHost->messageFactory());
        session->startWithPolicy<FakeASIO::ReadPolicy>();

        std::vector<std::thread> senders;
        senders.reserve(threadCount);
        for (size_t t = 0; t < threadCount; ++t)
        {
            senders.emplace_back([t, session] {
                for (size_t i = 0; i < msgPerThread; ++i)
                {
                    P2PMessage message;
                    message.setVersion((uint16_t)bcos::protocol::ProtocolVersion::V2);
                    message.setSeq(static_cast<uint32_t>(t * msgPerThread + i));
                    bytes payload(64, static_cast<uint8_t>('a' + t));
                    try
                    {
                        task::syncWait(session->fastSendMessage(message,
                            ::ranges::views::single(bcos::ref(std::as_const(payload))), Options{}));
                    }
                    catch (std::exception const&)
                    {
                        // no write failure is expected on a healthy loopback; a torn/aborted write
                        // would show up as a missing/corrupt frame in the wire assertions below
                    }
                }
            });
        }
        for (auto& th : senders)
        {
            th.join();
        }

        // Every syncWait has returned, but the last async_write may still be completing on the io
        // thread, so poll until the peer has the full byte count.
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        while (true)
        {
            size_t got = 0;
            {
                std::lock_guard<std::mutex> lock(recvMutex);
                got = received.size();
            }
            if (got >= expectedBytes)
            {
                break;
            }
            BOOST_CHECK_MESSAGE(std::chrono::steady_clock::now() < deadline,
                "peer did not receive all " << expectedBytes << " bytes, got " << got);
            if (std::chrono::steady_clock::now() >= deadline)
            {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        peerDone = true;
        // Unblock the peer thread's read (same teardown shape as fastSendMessageCompression).
        session->disconnect(DisconnectReason::DisconnectRequested);

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
    {
        boost::system::error_code ec;
        acceptor.close(ec);
    }
    io->stop();
    ioThread.join();

    // Parse the accumulated stream into frames by the 4-byte length prefix. Header layout:
    // [length:4][version:2][packetType:2][seq:4][ext:2] (P2PMessage::MESSAGE_HEADER_LENGTH = 14);
    // seq sits at bytes [8..12) in network byte order.
    std::vector<uint32_t> seqs;
    std::unordered_set<uint32_t> seenSeqs;
    {
        std::lock_guard<std::mutex> lock(recvMutex);
        BOOST_CHECK_EQUAL(received.size(), expectedBytes);
        size_t pos = 0;
        while (pos < received.size())
        {
            BOOST_REQUIRE(pos + 4 <= received.size());
            uint32_t frameLen = (uint32_t(received[pos]) << 24) |
                                (uint32_t(received[pos + 1]) << 16) |
                                (uint32_t(received[pos + 2]) << 8) | received[pos + 3];
            // a torn/interleaved frame would fail this exact-size assertion
            BOOST_REQUIRE(frameLen == frameSize);
            BOOST_REQUIRE(pos + frameLen <= received.size());
            uint32_t seq = (uint32_t(received[pos + 8]) << 24) |
                           (uint32_t(received[pos + 9]) << 16) |
                           (uint32_t(received[pos + 10]) << 8) | received[pos + 11];
            // payload integrity: each thread sent 64 bytes of ('a' + threadIdx)
            uint8_t expect = static_cast<uint8_t>('a' + seq / msgPerThread);
            for (size_t k = P2PMessage::MESSAGE_HEADER_LENGTH; k < frameLen; ++k)
            {
                BOOST_CHECK_EQUAL(received[pos + k], expect);
            }
            seqs.push_back(seq);
            seenSeqs.insert(seq);
            pos += frameLen;
        }
    }

    BOOST_CHECK_EQUAL(seqs.size(), totalMsgs);
    BOOST_CHECK_EQUAL(seenSeqs.size(), totalMsgs);
}

BOOST_AUTO_TEST_CASE(asioCompletionHandshakeSynchronousInvocation)
{
    // Arm/cancel handshake regression: an initiate that INVOKES the completion synchronously
    // (before returning) must not resume the coroutine from inside its own await_suspend
    // (resuming a not-yet-suspended frame is UB). The completion's operator() claims
    // Init -> Completed and deliberately does not resume; await_suspend observes Completed on
    // its failed CAS and resumes inline by returning false — the result is delivered exactly
    // once.
    auto task = []() -> task::Task<boost::system::error_code> {
        auto [ec] = co_await makeAsioAwaitable<boost::system::error_code>(
            [](auto handler) { handler(boost::system::error_code()); });
        co_return ec;
    }();
    auto ec = task::syncWait(std::move(task));
    BOOST_CHECK(!ec);
}

BOOST_AUTO_TEST_CASE(asioCompletionHandshakeSynchronousDrop)
{
    // Arm/cancel handshake regression: an initiate that DROPS the completion synchronously
    // (destroys it without invocation, no exception) must not destroy the still-running frame
    // from inside its own await_suspend. The destructor records the cancellation instead, and
    // await_suspend resumes inline with operation_aborted so the awaiting coroutine completes
    // with an error rather than suspending forever.
    auto task = []() -> task::Task<boost::system::error_code> {
        auto [ec] = co_await makeAsioAwaitable<boost::system::error_code>(
            [](auto handler) {
                // destroy the armed completion without invoking it: the by-value parameter is
                // destroyed at scope exit
                (void)handler;
            });
        co_return ec;
    }();
    auto ec = task::syncWait(std::move(task));
    BOOST_CHECK_EQUAL(ec, boost::asio::error::operation_aborted);
}

BOOST_AUTO_TEST_CASE(SessionRecvBufferTest)
{
    {
        // 0/r/w                               1024
        // |___________|__________|____________|
        //
        std::size_t recvBufferSize = 1024;
        SessionRecvBuffer recvBuffer(recvBufferSize);

        BOOST_CHECK_EQUAL(recvBuffer.recvBufferSize(), recvBufferSize);

        BOOST_CHECK_EQUAL(recvBuffer.readPos(), 0);
        BOOST_CHECK_EQUAL(recvBuffer.writePos(), 0);
        BOOST_CHECK_EQUAL(recvBuffer.dataSize(), 0);
        BOOST_CHECK_EQUAL(recvBuffer.asReadBuffer().size(), 0);
        BOOST_CHECK_EQUAL(recvBuffer.asWriteBuffer().size(), recvBufferSize);

        // move header operation
        recvBuffer.moveToHeader();
        BOOST_CHECK_EQUAL(recvBuffer.recvBufferSize(), recvBufferSize);

        BOOST_CHECK_EQUAL(recvBuffer.readPos(), 0);
        BOOST_CHECK_EQUAL(recvBuffer.writePos(), 0);
        BOOST_CHECK_EQUAL(recvBuffer.dataSize(), 0);
        BOOST_CHECK_EQUAL(recvBuffer.asReadBuffer().size(), 0);
        BOOST_CHECK_EQUAL(recvBuffer.asWriteBuffer().size(), recvBufferSize);

        // 0/r                   w            1024
        // |_____________________|____________|
        //
        // write 1000Byte to buffer
        int writeDataSize1 = 1000;
        auto result = recvBuffer.onWrite(writeDataSize1);
        // success
        BOOST_CHECK_EQUAL(result, true);
        BOOST_CHECK_EQUAL(recvBuffer.readPos(), 0);
        BOOST_CHECK_EQUAL(recvBuffer.writePos(), writeDataSize1);
        BOOST_CHECK_EQUAL(recvBuffer.dataSize(), writeDataSize1);
        BOOST_CHECK_EQUAL(recvBuffer.asReadBuffer().size(), writeDataSize1);
        BOOST_CHECK_EQUAL(recvBuffer.asWriteBuffer().size(), recvBufferSize - writeDataSize1);

        // write 1000B to buffer again
        int writeDataSize2 = 1000;
        result = recvBuffer.onWrite(writeDataSize2);
        // failure
        BOOST_CHECK_EQUAL(result, false);
        BOOST_CHECK_EQUAL(recvBuffer.readPos(), 0);
        BOOST_CHECK_EQUAL(recvBuffer.writePos(), writeDataSize1);
        BOOST_CHECK_EQUAL(recvBuffer.dataSize(), writeDataSize1);
        BOOST_CHECK_EQUAL(recvBuffer.asReadBuffer().size(), writeDataSize1);
        BOOST_CHECK_EQUAL(recvBuffer.asWriteBuffer().size(), recvBufferSize - writeDataSize1);

        // 0/r                               w/1024
        // |_________________________________|
        //
        // write 24B to buffer again
        int writeDataSize3 = 24;
        result = recvBuffer.onWrite(writeDataSize3);
        // ok
        BOOST_CHECK_EQUAL(result, true);
        BOOST_CHECK_EQUAL(recvBuffer.readPos(), 0);
        BOOST_CHECK_EQUAL(recvBuffer.writePos(), writeDataSize1 + writeDataSize3);
        BOOST_CHECK_EQUAL(recvBuffer.dataSize(), writeDataSize1 + writeDataSize3);
        BOOST_CHECK_EQUAL(recvBuffer.asReadBuffer().size(), writeDataSize1 + writeDataSize3);
        BOOST_CHECK_EQUAL(
            recvBuffer.asWriteBuffer().size(), recvBufferSize - writeDataSize1 - writeDataSize3);


        // 0/r    w/1024                      3072
        // |______|___________________________|
        //
        recvBufferSize *= 3;
        //  resize the buffer
        recvBuffer.resizeBuffer(recvBufferSize);
        BOOST_CHECK_EQUAL(recvBuffer.readPos(), 0);
        BOOST_CHECK_EQUAL(recvBuffer.writePos(), writeDataSize1 + writeDataSize3);
        BOOST_CHECK_EQUAL(recvBuffer.dataSize(), writeDataSize1 + writeDataSize3);
        BOOST_CHECK_EQUAL(recvBuffer.asReadBuffer().size(), writeDataSize1 + writeDataSize3);
        BOOST_CHECK_EQUAL(
            recvBuffer.asWriteBuffer().size(), recvBufferSize - writeDataSize1 - writeDataSize3);


        // 0                    r w            3072
        // |____________________|_|____________|
        //
        // read 999B from buffer
        int readDataSize1 = 999;
        result = recvBuffer.onRead(readDataSize1);
        // success
        BOOST_CHECK_EQUAL(result, true);
        BOOST_CHECK_EQUAL(recvBuffer.readPos(), readDataSize1);
        BOOST_CHECK_EQUAL(recvBuffer.writePos(), writeDataSize1 + writeDataSize3);
        BOOST_CHECK_EQUAL(recvBuffer.dataSize(), writeDataSize1 + writeDataSize3 - readDataSize1);
        BOOST_CHECK_EQUAL(
            recvBuffer.asReadBuffer().size(), writeDataSize1 + writeDataSize3 - readDataSize1);
        BOOST_CHECK_EQUAL(
            recvBuffer.asWriteBuffer().size(), recvBufferSize - writeDataSize1 - writeDataSize3);

        recvBuffer.moveToHeader();
        // move data to header
        // r w                             3072
        // |_|______________________________|

        BOOST_CHECK_EQUAL(recvBuffer.readPos(), 0);
        BOOST_CHECK_EQUAL(recvBuffer.writePos(), writeDataSize1 + writeDataSize3 - readDataSize1);
        BOOST_CHECK_EQUAL(recvBuffer.dataSize(), writeDataSize1 + writeDataSize3 - readDataSize1);
        BOOST_CHECK_EQUAL(
            recvBuffer.asReadBuffer().size(), writeDataSize1 + writeDataSize3 - readDataSize1);
        BOOST_CHECK_EQUAL(recvBuffer.asWriteBuffer().size(),
            recvBufferSize - (writeDataSize1 + writeDataSize3 - readDataSize1));

        // read 3072B from buffer
        int readDataSize2 = 3072;
        result = recvBuffer.onRead(readDataSize2);
        // failure
        BOOST_CHECK_EQUAL(result, false);
        BOOST_CHECK_EQUAL(recvBuffer.readPos(), 0);
        BOOST_CHECK_EQUAL(recvBuffer.writePos(), writeDataSize1 + writeDataSize3 - readDataSize1);
        BOOST_CHECK_EQUAL(recvBuffer.dataSize(), writeDataSize1 + writeDataSize3 - readDataSize1);
        BOOST_CHECK_EQUAL(
            recvBuffer.asReadBuffer().size(), writeDataSize1 + writeDataSize3 - readDataSize1);
        BOOST_CHECK_EQUAL(recvBuffer.asWriteBuffer().size(),
            recvBufferSize - (writeDataSize1 + writeDataSize3 - readDataSize1));
    }
}

BOOST_AUTO_TEST_SUITE_END()
