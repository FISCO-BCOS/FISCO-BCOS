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
 * @brief Session read/write/recv-buffer tests for bcos-network.
 * @file SessionTest.cpp
 *
 * Migrated from bcos-gateway/test/unittests/SessionTest.cpp when the libnetwork layer became
 * the standalone bcos-network library. This file keeps the cases whose SUBJECT is the library
 * itself (read loop reassembly, backpressure, the production read entry point, the single-
 * writer write path, the coroutine completion bridge and SessionRecvBuffer). The cases whose
 * subject is gateway policy (P2PSession compression / outbound rate limit / broadcast fan-out
 * stamping) stayed in the gateway suite. Frames are built with the test-local format and
 * decoder (see utils/TestFraming.h): the library is format-agnostic, so its own tests must not
 * pull in the gateway's P2PDecoder.
 */

#include "unittests/utils/TestAsioInterface.h"
#include "unittests/utils/TestFraming.h"
#include "unittests/utils/TestSessionSocket.h"
#include <bcos-network/ASIOInterface.h>
#include <bcos-network/Host.h>
#include <bcos-network/Session.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/IOServicePool.h>
#include <boost/asio/ip/tcp.hpp>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <list>
#include <mutex>
#include <optional>
#include <queue>
#include <range/v3/view/concat.hpp>
#include <range/v3/view/single.hpp>
#include <thread>
#include <tuple>
#include <unordered_set>
#include <vector>

using namespace bcos;
using namespace bcos::network;
using namespace bcos::network::test;

BOOST_AUTO_TEST_SUITE(SessionTest)

using TestSession = BasicSession<TestFrameDecoder, TestSessionSocket>;

// Templated on the socket type: the fake-socket tests instantiate it with TestSessionSocket,
// the plaintext wire test with the production PlainSocket.
template <typename SocketT = TestSessionSocket>
class TestHost : public bcos::network::Host<TestFrameDecoder, SocketT>
{
public:
    TestHost(std::shared_ptr<bcos::network::ASIOInterface> _asioInterface,
        std::shared_ptr<bcos::network::BasicSessionFactory<TestFrameDecoder, SocketT>>
            _sessionFactory)
      : bcos::network::Host<TestFrameDecoder, SocketT>(
            std::move(_asioInterface), std::move(_sessionFactory))
    {
        this->m_run = true;
    }
};

BOOST_AUTO_TEST_CASE(fakeClassTest)
{
    // Sanity check of the shared test harness itself: TestFrameBuilder concatenates frames and
    // chops them into random packets; TestAsioInterface::readSome() must reassemble the byte
    // stream exactly.
    auto totalPacketNum = 50;

    TestFrameBuilder messageBuilder(totalPacketNum);
    std::vector<uint8_t> recvBuffer;
    auto asio = std::make_shared<TestAsioInterface>();

    while (auto packet = messageBuilder.nextPacket())
    {
        std::vector<uint8_t> readBuffer(10240);
        asio->appendRecvPacket(packet);
        asio->readSome(boost::asio::buffer(readBuffer),
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
    TestFrameBuilder messageBuilder(totalPacketNum);
    // runIo: the session's drop() posts its socket teardown onto the socket's own io_context —
    // keep the original SessionTest behaviour where that queue is actually serviced.
    auto fakeSocket = std::make_shared<TestSessionSocket>(/*connected*/ true, /*runIo*/ true);

    std::atomic<size_t> recvPacketCnt = 0;
    std::atomic<size_t> recvBufferSize = 0;
    std::atomic<uint64_t> lastReadTime = utcSteadyTime();
    // Decode failures observed by the consumer coroutine: it resumes on the fake's feed-pool
    // thread, where Boost.Test assertions are not thread-safe, so it records failures here and
    // the test thread asserts after the coroutine exits.
    std::atomic<size_t> decodeFailures = 0;
    // Fulfilled when the consumer coroutine exits on ANY path (every frame received, channel
    // closed early, unexpected exception): the test thread waits on the future so the
    // coroutine's captured references cannot outlive this scope.
    std::promise<void> consumerDone;
    auto consumerFinished = consumerDone.get_future();
    auto fakeAsio = std::make_shared<TestAsioInterface>();
    {
        auto fakeHost = std::make_shared<TestHost<TestSessionSocket>>(fakeAsio, nullptr);

        // 16-byte initial buffer: a real frame needs its 4-byte length prefix decoded before
        // the read loop can learn the frame length and grow the buffer, so the growth path is
        // exercised from prefix-size upward (frames run up to 244 bytes)
        auto session = std::make_shared<TestSession>(fakeSocket, *fakeHost, 16, true);

        // Pull-mode consumer: drains exactly totalPacketNum frames out of the session's
        // recvMessage() channel, checking each frame's declared length and 0xff payload (the
        // test decoder's frame content is opaque to the library, so no protocol decode is
        // involved). Exits on its own after the last frame; if the read loop drops the session
        // first, the channel close rethrows the teardown NetworkException and the catch exits
        // the coroutine with the shortfall visible in recvPacketCnt.
        task::wait([](TestSession::Ptr _session, std::size_t _expected,
                       std::atomic<size_t>& _recvPacketCnt, std::atomic<size_t>& _recvBufferSize,
                       std::atomic<uint64_t>& _lastReadTime, std::atomic<size_t>& _decodeFailures,
                       std::promise<void>& _done) -> task::Task<void> {
            // Fulfil the promise on every exit path: an escaping exception would throw onto the
            // resuming pool thread's stack (detached task::wait), and an unset promise would
            // hang the test thread.
            struct DoneGuard
            {
                std::promise<void>& m_done;
                ~DoneGuard()
                {
                    try
                    {
                        m_done.set_value();
                    }
                    catch (...)
                    {}
                }
            } const guard{_done};
            try
            {
                for (std::size_t i = 0; i < _expected; ++i)
                {
                    auto meta = co_await _session->recvMessage();
                    _lastReadTime = utcSteadyTime();
                    auto frameData = meta.frameData();
                    if (frameData.size() < TestFrameDecoder::FRAME_HEADER_SIZE ||
                        TestFrameDecoder::readLength(frameData.data()) != frameData.size())
                    {
                        ++_decodeFailures;
                        continue;
                    }
                    // every payload byte of the reassembled frame must be 0xff
                    if (!std::all_of(frameData.begin() + TestFrameDecoder::FRAME_HEADER_SIZE,
                            frameData.end(), [](auto b) { return b == 0xff; }))
                    {
                        ++_decodeFailures;
                    }
                    _recvBufferSize += frameData.size();
                    ++_recvPacketCnt;
                }
            }
            catch (bcos::network::NetworkException& e)
            {
                // channel closed (session dropped) before every frame arrived — the count
                // assertions on the test thread report the shortfall
                std::cout << "consumer exit: " << bcos::network::errorCodeOf(e) << " " << e.what()
                          << std::endl;
            }
            catch (...)
            {}
        }(session, totalPacketNum, recvPacketCnt, recvBufferSize, lastReadTime, decodeFailures,
                                                      consumerDone));

        session->startWithPolicy<TestAsioInterface::ReadPolicy>();

        // send packets
        while (auto packet = messageBuilder.nextPacket())
        {
            fakeAsio->asyncAppendRecvPacket(packet);
        }

        // Wait for the consumer coroutine itself, not just the frame count: its wake-ups are
        // posted onto the fake's feed-pool thread (the channel poster), so completion is
        // asynchronous and the count can lead the coroutine's exit by one resume.
        size_t retryTimes = 0;
        while (consumerFinished.wait_for(std::chrono::milliseconds(500)) !=
                   std::future_status::ready &&
               retryTimes < 100)
        {
            std::cout << "waiting " << (totalPacketNum - static_cast<int>(recvPacketCnt.load()))
                      << " packets" << std::endl;
            retryTimes++;
        }
        BOOST_CHECK(
            consumerFinished.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready);

        BOOST_CHECK_EQUAL(recvPacketCnt, totalPacketNum);
        BOOST_CHECK_EQUAL(decodeFailures, 0);
        BOOST_CHECK_EQUAL(recvBufferSize, messageBuilder.sendBufferSize());

        // Teardown: a read is still parked in the fake. Failing it drops the session and closes
        // the (already drained) recv channel; in pull mode there is no handler to swap out — the
        // consumer has normally exited already, and on the shortfall path it exits now with the
        // teardown NetworkException. Wait for the read loop to unwind completely before nulling
        // the socket.
        fakeAsio->stopReads();
        size_t drainRetry = 0;
        while (fakeAsio->readsInFlight() != 0 && drainRetry < 200)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            drainRetry++;
        }
        BOOST_REQUIRE_EQUAL(fakeAsio->readsInFlight(), 0);

        // Shortfall path: the consumer only exits once the drop above closes the channel — wait
        // (bounded) so its captured references cannot outlive this scope.
        if (consumerFinished.wait_for(std::chrono::seconds(10)) != std::future_status::ready)
        {
            BOOST_FAIL("consumer coroutine did not exit");
        }
        session->setSocket(nullptr);
    }

    fakeSocket->close();
}

BOOST_AUTO_TEST_CASE(recvQueueFullParksReadLoopInsteadOfDropping)
{
    // Backpressure: with no consumer draining, the read loop decodes frames until the recv
    // queue hits MAX_RECV_QUEUE_FRAMES, then PARKS in waitWritable() — a slow consumer is not a
    // dead peer, so the session must stay active (the v1 policy dropped it with
    // drop(UserReason)). Once the consumer starts draining, delivery resumes and every queued
    // plus still-undelivered frame arrives.
    constexpr std::size_t totalPacketNum = TestSession::MAX_RECV_QUEUE_FRAMES + 76;
    TestFrameBuilder messageBuilder(totalPacketNum);
    auto fakeSocket = std::make_shared<TestSessionSocket>(/*connected*/ true, /*runIo*/ true);
    auto fakeAsio = std::make_shared<TestAsioInterface>();
    {
        auto fakeHost = std::make_shared<TestHost<TestSessionSocket>>(fakeAsio, nullptr);

        // Same arrangement as doReadTest: a forced-tiny initial buffer so the growth path runs.
        auto session = std::make_shared<TestSession>(fakeSocket, *fakeHost, 16, true);

        session->startWithPolicy<TestAsioInterface::ReadPolicy>();

        // Feed every packet up front; no consumer is running yet.
        while (auto packet = messageBuilder.nextPacket())
        {
            fakeAsio->asyncAppendRecvPacket(packet);
        }

        // The park is observable as readsInFlight() == 0: a read parked in the fake holds the
        // counter at 1, and the re-arm happens before the completing read unwinds, so only a
        // waitWritable() park (or a dead loop) leaves it at 0 — with packets still undelivered
        // in the fake, a dead loop would fail the active() check below.
        size_t parkRetry = 0;
        while (fakeAsio->readsInFlight() != 0 && parkRetry < 1000)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            parkRetry++;
        }
        BOOST_REQUIRE_EQUAL(fakeAsio->readsInFlight(), 0);

        // The key regression assertion: the session survives a full recv queue.
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        BOOST_CHECK(session->active());

        // Pull-mode consumer, same pattern as doReadTest: drains exactly totalPacketNum frames
        // and exits; the frame-count assertions run on the test thread afterwards.
        std::atomic<size_t> recvPacketCnt = 0;
        std::atomic<size_t> recvBufferSize = 0;
        std::atomic<size_t> decodeFailures = 0;
        std::promise<void> consumerDone;
        auto consumerFinished = consumerDone.get_future();
        task::wait([](TestSession::Ptr _session, std::size_t _expected,
                       std::atomic<size_t>& _recvPacketCnt, std::atomic<size_t>& _recvBufferSize,
                       std::atomic<size_t>& _decodeFailures,
                       std::promise<void>& _done) -> task::Task<void> {
            struct DoneGuard
            {
                std::promise<void>& m_done;
                ~DoneGuard()
                {
                    try
                    {
                        m_done.set_value();
                    }
                    catch (...)
                    {}
                }
            } const guard{_done};
            try
            {
                for (std::size_t i = 0; i < _expected; ++i)
                {
                    auto meta = co_await _session->recvMessage();
                    auto frameData = meta.frameData();
                    if (frameData.size() < TestFrameDecoder::FRAME_HEADER_SIZE ||
                        TestFrameDecoder::readLength(frameData.data()) != frameData.size())
                    {
                        ++_decodeFailures;
                        continue;
                    }
                    _recvBufferSize += frameData.size();
                    ++_recvPacketCnt;
                }
            }
            catch (bcos::network::NetworkException& e)
            {
                // channel closed (session dropped) before every frame arrived
                std::cout << "consumer exit: " << bcos::network::errorCodeOf(e) << " " << e.what()
                          << std::endl;
            }
            catch (...)
            {}
        }(session, totalPacketNum, recvPacketCnt, recvBufferSize, decodeFailures, consumerDone));

        size_t retryTimes = 0;
        while (consumerFinished.wait_for(std::chrono::milliseconds(500)) !=
                   std::future_status::ready &&
               retryTimes < 100)
        {
            retryTimes++;
        }
        BOOST_REQUIRE(
            consumerFinished.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready);

        // Delivery resumed out of the park and nothing was lost: every frame arrived and the
        // session is still alive after the whole stall.
        BOOST_CHECK_EQUAL(recvPacketCnt, totalPacketNum);
        BOOST_CHECK_EQUAL(decodeFailures, 0);
        BOOST_CHECK_EQUAL(recvBufferSize, messageBuilder.sendBufferSize());
        BOOST_CHECK(session->active());

        // Teardown, same as doReadTest: fail the parked read so the loop unwinds before the
        // socket is nulled.
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
    // startWithPolicy<FakePolicy>). DefaultReadPolicy dispatches async_read_some on the fake's
    // stream() — an UN-HANDSHAKEN ssl::stream over a connected TCP pair — which completes
    // promptly with an SSL error; the read loop must then drop the session.
    auto fakeSocket = std::make_shared<TestSessionSocket>(/*connected*/ true, /*runIo*/ true);
    auto fakeAsio = std::make_shared<TestAsioInterface>();
    {
        auto fakeHost = std::make_shared<TestHost<TestSessionSocket>>(fakeAsio, nullptr);

        auto session = std::make_shared<TestSession>(fakeSocket, *fakeHost, 2, true);

        // Pull-mode consumer: the read error drops the session, and the drop closes the recv
        // channel, so the parked recvMessage() rethrows the teardown NetworkException. The
        // observed error code goes back through the promise — the coroutine resumes on the
        // fake's pool thread, where Boost.Test assertions are not thread-safe.
        std::promise<int64_t> recvError;
        auto recvErrorCode = recvError.get_future();
        task::wait(
            [](TestSession::Ptr _session, std::promise<int64_t>& _recvError) -> task::Task<void> {
                try
                {
                    (void)co_await _session->recvMessage();
                    // a decoded frame instead of the teardown error: sentinel, fails the assert
                    _recvError.set_value(bcos::network::P2PExceptionType::Success);
                }
                catch (bcos::network::NetworkException& e)
                {
                    _recvError.set_value(bcos::network::errorCodeOf(e));
                }
                catch (...)
                {
                    _recvError.set_value(-1);
                }
            }(session, recvError));

        session->start();  // production entry — NOT startWithPolicy<>

        size_t retryTimes = 0;
        while (session->active() && retryTimes < 200)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            retryTimes++;
        }
        BOOST_CHECK(!session->active());

        // The consumer's resume is posted to the fake's pool, so the error code arrives
        // asynchronously — wait (bounded) for it before asserting.
        BOOST_REQUIRE(
            recvErrorCode.wait_for(std::chrono::seconds(10)) == std::future_status::ready);
        BOOST_CHECK_EQUAL(recvErrorCode.get(), bcos::network::P2PExceptionType::Disconnect);
        // drop() captured the socket into a local shared_ptr before clearing m_active, so the
        // socket can be nulled as soon as the session is inactive (same teardown as doReadTest).
        session->setSocket(nullptr);
    }

    fakeSocket->close();
}

BOOST_AUTO_TEST_CASE(fastSendConcurrentWriteOrder)
{
    // Regression for the per-session write ordering guarantee: the write path must serialize
    // concurrent producers. N threads call sendMessage concurrently and the single-writer
    // write loop (Session::writeLoop, guarded by the m_writingInFlight single-flight flag) must
    // put a complete, non-interleaved frame for every message on the wire — if the serialization
    // were ever broken, frames would be torn or interleaved. Uses a real loopback connection
    // with the plaintext PlainSocket so the full awaitableWrite -> async_write path runs, then
    // parses the accumulated byte stream into frames by their length prefix.
    constexpr size_t threadCount = 8;
    constexpr size_t msgPerThread = 50;
    constexpr size_t totalMsgs = threadCount * msgPerThread;

    auto fakeAsio = std::make_shared<TestAsioInterface>();

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

    // Each frame is the test format's [length:4][seq:4] header plus a 64-byte payload. The
    // header is pre-encoded by the caller (in production the protocol layer does this), and the
    // session must write header+payload atomically as one frame.
    const size_t frameHeaderLen = 8;
    const size_t payloadLen = 64;
    const size_t frameSize = frameHeaderLen + payloadLen;
    const size_t expectedBytes = frameSize * totalMsgs;

    // PlainSocket flavour: this test drives the session directly (no protocol wrapper), so it
    // can exercise the compile-time plaintext stream end to end — the peer reads raw frames.
    {
        auto fakeHost = std::make_shared<TestHost<PlainSocket>>(fakeAsio, nullptr);
        auto sessionSocket = std::make_shared<PlainSocket>(io, nullptr, NodeIPEndpoint());
        sessionSocket->ref() = std::move(client);
        auto session = std::make_shared<BasicSession<TestFrameDecoder, PlainSocket>>(
            sessionSocket, *fakeHost, 2, true);
        session->startWithPolicy<TestAsioInterface::ReadPolicy>();

        std::vector<std::thread> senders;
        senders.reserve(threadCount);
        for (size_t t = 0; t < threadCount; ++t)
        {
            senders.emplace_back([t, session] {
                for (size_t i = 0; i < msgPerThread; ++i)
                {
                    // The session sends pure bytes: encode the [length:4][seq:4] frame header at
                    // the caller (the protocol layer's business)
                    uint32_t seq = static_cast<uint32_t>(t * msgPerThread + i);
                    bytes headerBuffer(frameHeaderLen);
                    TestFrameDecoder::writeLength(
                        headerBuffer.data(), static_cast<uint32_t>(frameSize));
                    headerBuffer[4] = byte(seq >> 24);
                    headerBuffer[5] = byte(seq >> 16);
                    headerBuffer[6] = byte(seq >> 8);
                    headerBuffer[7] = byte(seq);
                    bytes payload(payloadLen, static_cast<uint8_t>('a' + t));
                    try
                    {
                        task::syncWait(session->sendMessage(::ranges::views::concat(
                            ::ranges::views::single(bcos::ref(headerBuffer)),
                            ::ranges::views::single(bcos::ref(std::as_const(payload))))));
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
        // Unblock the peer thread's read.
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
    {
        boost::system::error_code ec;
        acceptor.close(ec);
    }
    io->stop();
    ioThread.join();

    // Parse the accumulated stream into frames by the 4-byte length prefix; seq sits at bytes
    // [4..8) in network byte order, payload at byte 8.
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
            uint32_t seq = (uint32_t(received[pos + 4]) << 24) |
                           (uint32_t(received[pos + 5]) << 16) |
                           (uint32_t(received[pos + 6]) << 8) | received[pos + 7];
            // payload integrity: each thread sent 64 bytes of ('a' + threadIdx)
            uint8_t expect = static_cast<uint8_t>('a' + seq / msgPerThread);
            for (size_t k = frameHeaderLen; k < frameLen; ++k)
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

BOOST_AUTO_TEST_CASE(readTimeoutDoesNotAbortInflightWrite)
{
    // Regression for the read-timeout cancellation scope: the deadline must cancel ONLY the
    // pending read (per-operation cancellation slot), not the whole socket. The old handler
    // called socket->ref().cancel(), which also aborted a concurrent in-flight async_write;
    // writeLoop treats a write error as fatal and dropped the session — a healthy idle-peer
    // timeout became a spurious disconnect (devp2p runs exactly this shape: a 15s read timeout
    // plus gossip writes issued from unrelated threads). Here a large write is kept in flight
    // across a read-timeout boundary: the write must complete intact and the session must stay
    // alive, with the timeout surfaced to the consumer as Timeout frames.
    auto fakeAsio = std::make_shared<TestAsioInterface>();

    auto io = std::make_shared<ba::io_context>();
    boost::asio::executor_work_guard<ba::io_context::executor_type> workGuard(io->get_executor());
    std::thread ioThread([io] { io->run(); });

    ba::ip::tcp::acceptor acceptor(*io, ba::ip::tcp::endpoint(ba::ip::tcp::v4(), 0));
    auto listenEndpoint = acceptor.local_endpoint();

    // 16MB: far beyond the loopback send buffers, so the async_write is still pending when the
    // 200ms read-timeout deadline fires (the peer deliberately does not read until afterwards).
    const size_t frameHeaderLen = TestFrameDecoder::FRAME_HEADER_SIZE;
    const size_t frameSize = frameHeaderLen + 16 * 1024 * 1024;

    std::atomic<bool> peerError{false};
    std::atomic<size_t> peerReceived{0};
    std::atomic<bool> peerDone{false};
    std::atomic<bool> releasePeer{false};
    std::thread peerThread([&] {
        ba::ip::tcp::socket peer(*io);
        boost::system::error_code ec;
        acceptor.accept(peer, ec);
        if (ec)
        {
            peerError = true;
            return;
        }
        // Stay silent (nothing for the session to read) and keep the write blocked past the
        // timeout boundary.
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
        std::vector<uint8_t> buf(64 * 1024);
        size_t total = 0;
        while (total < frameSize)
        {
            std::size_t n = peer.read_some(ba::buffer(buf), ec);
            if (ec)
            {
                peerError = true;
                break;
            }
            total += n;
        }
        peerReceived = total;
        peerDone = true;
        // Hold the connection open until the test's assertions are done: closing here would
        // deliver an EOF to the read loop and drop the session before the checks run.
        while (!releasePeer.load(std::memory_order_relaxed))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });

    ba::ip::tcp::socket client(*io);
    boost::system::error_code connectError;
    client.connect(listenEndpoint, connectError);
    BOOST_REQUIRE(!connectError);

    {
        auto fakeHost = std::make_shared<TestHost<PlainSocket>>(fakeAsio, nullptr);
        auto sessionSocket = std::make_shared<PlainSocket>(io, nullptr, NodeIPEndpoint());
        sessionSocket->ref() = std::move(client);
        auto session = std::make_shared<BasicSession<TestFrameDecoder, PlainSocket>>(
            sessionSocket, *fakeHost, 2, true);
        session->setReadTimeoutMs(200);
        // DefaultReadPolicy: the read loop arms a REAL async_read_some on the loopback socket,
        // so the deadline's cancellation actually goes through the socket.
        session->start();

        // Consumer: the read side sees silence, so Timeout frames must arrive WITHOUT the
        // channel closing (a close here means the session was dropped — the old failure mode).
        std::atomic<size_t> timeoutFrames{0};
        std::atomic<bool> consumerDropped{false};
        std::atomic<bool> stopConsumer{false};
        std::promise<void> consumerDone;
        auto consumerFinished = consumerDone.get_future();
        task::wait(
            [](BasicSession<TestFrameDecoder, PlainSocket>::Ptr _session,
                std::atomic<size_t>& _timeoutFrames, std::atomic<bool>& _consumerDropped,
                std::atomic<bool>& _stopConsumer, std::promise<void>& _done) -> task::Task<void> {
                // Fulfil the promise on every exit path (see doReadTest's DoneGuard rationale).
                struct DoneGuard
                {
                    std::promise<void>& m_done;
                    ~DoneGuard()
                    {
                        try
                        {
                            m_done.set_value();
                        }
                        catch (...)
                        {}
                    }
                } const guard{_done};
                try
                {
                    while (!_stopConsumer.load(std::memory_order_relaxed))
                    {
                        auto meta = co_await _session->recvMessage();
                        if (meta.status == FrameMeta::Status::Timeout)
                        {
                            ++_timeoutFrames;
                        }
                    }
                }
                catch (bcos::network::NetworkException const&)
                {
                    if (!_stopConsumer.load(std::memory_order_relaxed))
                    {
                        _consumerDropped = true;
                    }
                }
                catch (...)
                {}
            }(session, timeoutFrames, consumerDropped, stopConsumer, consumerDone));

        // Issue the large write shortly before the first deadline fires.
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        bytes frame(frameSize, 0xff);
        TestFrameDecoder::writeLength(frame.data(), static_cast<uint32_t>(frameSize));
        bool sent = task::syncWait(
            session->sendMessage(::ranges::views::single(bcos::ref(std::as_const(frame)))));
        BOOST_CHECK(sent);

        // The peer starts draining at ~800ms; wait for the whole frame to arrive intact.
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        while (!peerDone.load() && !peerError.load() && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        BOOST_CHECK(!peerError.load());
        BOOST_CHECK_EQUAL(peerReceived.load(), frameSize);

        // The deadline fired while the write was in flight: timeouts were delivered, the write
        // completed, and neither the session nor the consumer went down.
        BOOST_CHECK_GT(timeoutFrames.load(), 0);
        BOOST_CHECK(session->active());
        BOOST_CHECK(!consumerDropped.load());

        stopConsumer = true;
        session->disconnect(bcos::network::DisconnectReason::DisconnectRequested);
        releasePeer = true;
        if (consumerFinished.wait_for(std::chrono::seconds(10)) != std::future_status::ready)
        {
            BOOST_FAIL("consumer coroutine did not exit");
        }
    }

    releasePeer = true;
    peerThread.join();
    workGuard.reset();
    {
        boost::system::error_code ec;
        acceptor.close(ec);
    }
    io->stop();
    ioThread.join();
}

namespace
{
// A decoder that violates the FrameDecoder contract: it reports more wire-consumed bytes than
// the buffer holds. The read loop's copy path must treat that as fatal (drop), exactly like the
// take-buffer branch — ignoring onRead()'s false return used to re-decode the identical bytes
// and push copies of the same frame forever.
class OverconsumeDecoder
{
public:
    FrameMeta tryDecode(const bytesConstRef& buffer)
    {
        FrameMeta meta;
        if (buffer.size() < TestFrameDecoder::FRAME_HEADER_SIZE)
        {
            return meta;  // NeedMoreData
        }
        meta.status = FrameMeta::Status::Frame;
        meta.wireConsumed = buffer.size() + 1;  // contract violation: beyond the available bytes
        meta.frameSize = TestFrameDecoder::FRAME_HEADER_SIZE;
        meta.frame = bytes(buffer.data(), buffer.data() + TestFrameDecoder::FRAME_HEADER_SIZE);
        return meta;
    }
};

template <typename SocketT>
class OverconsumeHost : public bcos::network::Host<OverconsumeDecoder, SocketT>
{
public:
    OverconsumeHost(std::shared_ptr<bcos::network::ASIOInterface> _asioInterface,
        std::shared_ptr<bcos::network::BasicSessionFactory<OverconsumeDecoder, SocketT>>
            _sessionFactory)
      : bcos::network::Host<OverconsumeDecoder, SocketT>(
            std::move(_asioInterface), std::move(_sessionFactory))
    {
        this->m_run = true;
    }
};
}  // namespace

BOOST_AUTO_TEST_CASE(decoderOverconsumptionDropsSession)
{
    // Regression: a decoder whose wireConsumed exceeds the buffered bytes must drop the session
    // on the copy path (onRead returns false without advancing). Before the fix the loop pushed
    // the same frame over and over; the consumer below must therefore see the channel CLOSE
    // before any frame is delivered.
    auto fakeSocket = std::make_shared<TestSessionSocket>(/*connected*/ true, /*runIo*/ true);
    auto fakeAsio = std::make_shared<TestAsioInterface>();

    using OverconsumeSession = BasicSession<OverconsumeDecoder, TestSessionSocket>;
    {
        auto fakeHost = std::make_shared<OverconsumeHost<TestSessionSocket>>(fakeAsio, nullptr);
        auto session = std::make_shared<OverconsumeSession>(fakeSocket, *fakeHost, 16, true);

        std::atomic<bool> gotFrame{false};
        std::atomic<bool> channelClosed{false};
        std::promise<void> consumerDone;
        auto consumerFinished = consumerDone.get_future();
        task::wait(
            [](OverconsumeSession::Ptr _session, std::atomic<bool>& _gotFrame,
                std::atomic<bool>& _channelClosed, std::promise<void>& _done) -> task::Task<void> {
                struct DoneGuard
                {
                    std::promise<void>& m_done;
                    ~DoneGuard()
                    {
                        try
                        {
                            m_done.set_value();
                        }
                        catch (...)
                        {}
                    }
                } const guard{_done};
                try
                {
                    auto meta = co_await _session->recvMessage();
                    (void)meta;
                    _gotFrame = true;  // the buggy frame must never be delivered
                }
                catch (bcos::network::NetworkException const&)
                {
                    _channelClosed = true;  // the drop closed the channel under the consumer
                }
                catch (...)
                {}
            }(session, gotFrame, channelClosed, consumerDone));

        session->startWithPolicy<TestAsioInterface::ReadPolicy>();
        fakeAsio->asyncAppendRecvPacket(
            std::make_shared<std::vector<uint8_t>>(buildTestFrame(bytes(8, 0xff))));

        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (
            consumerFinished.wait_for(std::chrono::milliseconds(10)) != std::future_status::ready &&
            std::chrono::steady_clock::now() < deadline)
        {
        }
        BOOST_REQUIRE(
            consumerFinished.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready);
        BOOST_CHECK(channelClosed.load());
        BOOST_CHECK(!gotFrame.load());
        BOOST_CHECK(!session->active());

        // Teardown, same pattern as doReadTest.
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

BOOST_AUTO_TEST_CASE(asioCompletionHandshakeSynchronousInvocation)
{
    // Regression: an initiate that INVOKES the completion synchronously (before returning) must
    // not resume the coroutine from inside its own await_suspend. Symmetric transfer guarantees
    // the awaiting coroutine is suspended before the bridge runs, so resuming it is safe — the
    // result is delivered exactly once.
    auto task = []() -> task::Task<boost::system::error_code> {
        auto [ec] = co_await task::makeFireAwaitable<boost::system::error_code>(
            [](auto handler) { handler(boost::system::error_code()); },
            boost::asio::error::operation_aborted);
        co_return ec;
    }();
    auto ec = task::syncWait(std::move(task));
    BOOST_CHECK(!ec);
}

BOOST_AUTO_TEST_CASE(asioCompletionHandshakeSynchronousDrop)
{
    // Regression: an initiate that DROPS the completion synchronously (destroys it without
    // invocation, no exception) delivers the initial error result (operation_aborted) instead of
    // suspending forever.
    auto task = []() -> task::Task<boost::system::error_code> {
        auto [ec] = co_await task::makeFireAwaitable<boost::system::error_code>(
            [](auto handler) {
                // destroy the armed completion without invoking it: the by-value parameter is
                // destroyed at scope exit
                (void)handler;
            },
            boost::asio::error::operation_aborted);
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
        bcos::network::SessionRecvBuffer recvBuffer(recvBufferSize);

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

BOOST_AUTO_TEST_CASE(SessionRecvBufferTakeStorageTest)
{
    std::size_t recvBufferSize = 1024;
    bcos::network::SessionRecvBuffer recvBuffer(recvBufferSize);

    // layout: [0,100) consumed prefix, [100,700) the frame being taken, [700,800) tail
    constexpr std::size_t prefixLen = 100;
    constexpr std::size_t frameLen = 600;
    constexpr std::size_t tailLen = 100;
    auto writeBuffer = recvBuffer.asWriteBuffer();
    for (std::size_t i = 0; i < prefixLen + frameLen + tailLen; ++i)
    {
        writeBuffer.data()[i] = static_cast<byte>(i);
    }
    BOOST_REQUIRE(recvBuffer.onWrite(prefixLen + frameLen + tailLen));
    BOOST_REQUIRE(recvBuffer.onRead(prefixLen));  // consume the prefix

    bytes frameStorage;
    auto frameStart = recvBuffer.takeStorage(frameStorage, frameLen);

    // the moved storage keeps the dead prefix; the frame starts at the old readPos, and the
    // stolen tail is truncated off the end
    BOOST_CHECK_EQUAL(frameStart, prefixLen);
    BOOST_REQUIRE_EQUAL(frameStorage.size(), prefixLen + frameLen);
    for (std::size_t i = 0; i < prefixLen + frameLen; ++i)
    {
        BOOST_CHECK_EQUAL(frameStorage[i], static_cast<byte>(i));
    }

    // the buffer is reseeded with exactly the tail
    BOOST_CHECK_EQUAL(recvBuffer.readPos(), 0);
    BOOST_CHECK_EQUAL(recvBuffer.writePos(), tailLen);
    BOOST_CHECK_EQUAL(recvBuffer.dataSize(), tailLen);
    auto readBuffer = recvBuffer.asReadBuffer();
    BOOST_REQUIRE_EQUAL(readBuffer.size(), tailLen);
    for (std::size_t i = 0; i < tailLen; ++i)
    {
        BOOST_CHECK_EQUAL(readBuffer[i], static_cast<byte>(prefixLen + frameLen + i));
    }

    // take again with an empty tail: the fresh buffer keeps write space for the next read
    bytes all;
    auto start2 = recvBuffer.takeStorage(all, tailLen);
    BOOST_CHECK_EQUAL(start2, 0);
    BOOST_REQUIRE_EQUAL(all.size(), tailLen);
    for (std::size_t i = 0; i < tailLen; ++i)
    {
        BOOST_CHECK_EQUAL(all[i], static_cast<byte>(prefixLen + frameLen + i));
    }
    BOOST_CHECK_EQUAL(recvBuffer.readPos(), 0);
    BOOST_CHECK_EQUAL(recvBuffer.writePos(), 0);
    BOOST_CHECK_EQUAL(recvBuffer.dataSize(), 0);
    BOOST_CHECK(recvBuffer.asWriteBuffer().size() > 0);
}

// Frames at least FRAME_TAKE_BUFFER_THRESHOLD take the buffer-swap path instead of being
// copied out of the receive buffer (FrameMeta::takeBuffer). [A small][B large][C small] in a
// single read exercises the two hard parts of the swap: B is decoded with a dead prefix
// (frameOffset > 0) and a non-empty tail (C) that must reseed the fresh buffer.
BOOST_AUTO_TEST_CASE(largeFrameTakesReceiveBuffer)
{
    auto fakeSocket = std::make_shared<TestSessionSocket>(/*connected*/ true, /*runIo*/ true);
    auto fakeAsio = std::make_shared<TestAsioInterface>();
    {
        auto fakeHost = std::make_shared<TestHost<TestSessionSocket>>(fakeAsio, nullptr);

        auto frameA = buildTestFrame(bytes(10, 'a'));
        auto frameB = buildTestFrame(bytes(bcos::network::FRAME_TAKE_BUFFER_THRESHOLD, 'b'));
        auto frameC = buildTestFrame(bytes(20, 'c'));
        auto stream = std::make_shared<std::vector<uint8_t>>();
        stream->insert(stream->end(), frameA.begin(), frameA.end());
        stream->insert(stream->end(), frameB.begin(), frameB.end());
        stream->insert(stream->end(), frameC.begin(), frameC.end());

        // forceSize: the whole stream lands in one read (maxReadDataSize lifted above the
        // stream size), so A/B/C decode in one inner-loop pass and B's handover sees the
        // dead prefix (readPos > 0) plus a non-empty tail
        auto session =
            std::make_shared<TestSession>(fakeSocket, *fakeHost, stream->size() + 1024, true);
        session->setMaxReadDataSize(4 * 1024 * 1024);

        struct Received
        {
            bytes frame;
            uint32_t frameOffset;
            std::size_t storageSize;
        };
        // The consumer coroutine hands the collected frames back through the promise: its
        // resumptions are posted onto the fake's feed-pool thread, so completion is asynchronous
        // and the assertions below run on the test thread.
        std::promise<std::vector<Received>> receivedPromise;
        auto receivedFuture = receivedPromise.get_future();

        // Pull-mode consumer: receive exactly the three frames. If the session is dropped first,
        // the channel close rethrows the teardown NetworkException and the coroutine hands back
        // the shortfall — the size assertion below reports it.
        task::wait([](TestSession::Ptr _session, std::size_t _expected,
                       std::promise<std::vector<Received>>& _received) -> task::Task<void> {
            std::vector<Received> received;
            try
            {
                for (std::size_t i = 0; i < _expected; ++i)
                {
                    auto meta = co_await _session->recvMessage();
                    auto data = meta.frameData();
                    received.push_back(Received{
                        bytes(data.begin(), data.end()), meta.frameOffset, meta.frame.size()});
                }
            }
            catch (bcos::network::NetworkException& e)
            {
                std::cout << "consumer exit: " << bcos::network::errorCodeOf(e) << " " << e.what()
                          << std::endl;
            }
            catch (...)
            {}
            try
            {
                _received.set_value(std::move(received));
            }
            catch (...)
            {}
        }(session, 3, receivedPromise));
        session->startWithPolicy<TestAsioInterface::ReadPolicy>();

        fakeAsio->asyncAppendRecvPacket(stream);

        size_t retryTimes = 0;
        while (
            receivedFuture.wait_for(std::chrono::milliseconds(10)) != std::future_status::ready &&
            retryTimes < 500)
        {
            ++retryTimes;
        }
        BOOST_CHECK(
            receivedFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready);

        // Teardown (same as doReadTest): unwind the parked read. Pull mode has no handler to
        // swap out — the consumer has normally exited already; on the shortfall path the drop
        // below closes the channel and the consumer exits with the teardown NetworkException.
        fakeAsio->stopReads();
        size_t drainRetry = 0;
        while (fakeAsio->readsInFlight() != 0 && drainRetry < 200)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            drainRetry++;
        }
        BOOST_REQUIRE_EQUAL(fakeAsio->readsInFlight(), 0);

        // Shortfall path: the consumer only exits once the drop above closes the channel — wait
        // (bounded) so its captured references cannot outlive this scope.
        if (receivedFuture.wait_for(std::chrono::seconds(10)) != std::future_status::ready)
        {
            BOOST_FAIL("consumer coroutine did not exit");
        }
        auto received = receivedFuture.get();
        session->setSocket(nullptr);

        BOOST_REQUIRE_EQUAL(received.size(), 3);
        BOOST_CHECK(received[0].frame == frameA);
        BOOST_CHECK(received[1].frame == frameB);
        BOOST_CHECK(received[2].frame == frameC);
        // A and C took the copy path: storage is exactly the frame
        BOOST_CHECK_EQUAL(received[0].frameOffset, 0);
        BOOST_CHECK_EQUAL(received[0].storageSize, frameA.size());
        BOOST_CHECK_EQUAL(received[2].frameOffset, 0);
        BOOST_CHECK_EQUAL(received[2].storageSize, frameC.size());
        // B took the buffer: storage = A's consumed prefix + B, tail (C) truncated away
        BOOST_CHECK_EQUAL(received[1].frameOffset, frameA.size());
        BOOST_CHECK_EQUAL(received[1].storageSize, frameA.size() + frameB.size());
    }

    fakeSocket->close();
}

// Regression for the FrameMeta two-byte-count contract (FIB: RLPx-style decoders).
//
// A decoder whose format has a framing/encryption layer consumes MORE wire bytes than the
// plaintext it yields (RLPx: 32B header + padded body + 16B MAC). The previous shape had a single
// `consumed` field and frameData() returned {frame.data() + frameOffset, consumed} — i.e. it read
// (consumed - plaintext) bytes past the end of the payload. This checks the split contract holds
// end to end through a real session: the read loop advances by the WIRE count while the consumer
// reads exactly the plaintext, with frameData() never wider than the storage it points into.
namespace
{
// The overhead-framing shape a RLPx-style decoder produces: wire bytes exceed the plaintext.
// Only the byte counts matter here, so the type is just a named constant.
namespace
{
constexpr uint32_t kOverheadFramingBytes = 16;
}  // namespace

}  // namespace

BOOST_AUTO_TEST_CASE(overheadDecoderPlaintextViewStaysInBounds)
{
    // 1. The contract itself, on a hand-built FrameMeta: frameData() must be the plaintext, never
    //    (wireConsumed - plaintext) bytes long.
    bcos::network::FrameMeta meta;
    meta.status = bcos::network::FrameMeta::Status::Frame;
    meta.frame = bytes{1, 2, 3, 4, 5};
    meta.frameSize = 5;
    meta.wireConsumed = 5 + 100;  // a framing layer that consumes far more than it yields
    meta.frameOffset = 0;
    BOOST_REQUIRE_EQUAL(meta.frameData().size(), 5u);
    BOOST_CHECK(meta.frameData().data() == meta.frame.data());
    BOOST_CHECK_EQUAL(meta.frameData().size() <= meta.frame.size(), true);

    // The take-buffer shape too: a payload shorter than the storage it was carried in.
    bcos::network::FrameMeta taken;
    taken.status = bcos::network::FrameMeta::Status::Frame;
    taken.frame = bytes{9, 9, 9, 7, 7, 7, 7};  // dead prefix of 3, plaintext of 4
    taken.frameOffset = 3;
    taken.frameSize = 4;
    taken.wireConsumed = 20;  // wire bytes exceed the storage, as a MAC/padding layer would
    BOOST_REQUIRE_EQUAL(taken.frameData().size(), 4u);
    BOOST_CHECK_EQUAL(*taken.frameData().data(), 7);
    BOOST_CHECK(taken.frameData().data() + taken.frameData().size() <=
                taken.frame.data() + taken.frame.size());

    // 2. The same two shapes as the read loop actually builds them, i.e. AFTER takeStorage has
    //    moved the storage in. `consumed` used to be the only count, so frameData() would have
    //    read past the end in both cases; now the view is bounded by frameSize only.
    {
        // copy path: frame == plaintext, offset 0
        bcos::network::FrameMeta copyPath;
        copyPath.status = bcos::network::FrameMeta::Status::Frame;
        copyPath.frame = bytes(200, 0xAB);
        copyPath.frameSize = 200;
        copyPath.wireConsumed = 200 + kOverheadFramingBytes;
        copyPath.frameOffset = 0;
        auto view = copyPath.frameData();
        BOOST_REQUIRE_EQUAL(view.size(), 200u);
        BOOST_CHECK(view.data() == copyPath.frame.data());
        BOOST_CHECK(view.data() + view.size() <= copyPath.frame.data() + copyPath.frame.size());
    }
    {
        // take-buffer path: storage = the whole read buffer, plaintext at [frameOffset, +frameSize)
        bcos::network::FrameMeta takePath;
        takePath.status = bcos::network::FrameMeta::Status::Frame;
        takePath.frame = bytes(4096, 0xCD);
        takePath.frameOffset = 1024;  // dead prefix, as the moved buffer carries it
        takePath.frameSize = 500;
        takePath.wireConsumed = 4096 - 1024;  // what the read loop advanced by
        auto view = takePath.frameData();
        BOOST_REQUIRE_EQUAL(view.size(), 500u);
        BOOST_CHECK(view.data() == takePath.frame.data() + takePath.frameOffset);
        BOOST_CHECK(view.data() + view.size() <= takePath.frame.data() + takePath.frame.size());
    }
}

BOOST_AUTO_TEST_SUITE_END()
