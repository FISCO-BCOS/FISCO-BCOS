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
 * @brief Pending-response correlation tests (the successor of FIB-70's per-session callback
 *        manager): Service::registerPendingResponse / claimPendingResponse /
 *        failPendingResponsesOf / onResponseTimeout and the P2PSession write-failure
 *        settlement. The session lifecycle cases this file used to share (decode-error drop,
 *        truncated-header delivery, socket shared_ptr capture) moved to bcos-network's own
 *        suite: bcos-network/test/unittests/FIB70_FIB97_SessionLifecycleTest.cpp.
 * @file FIB70_FIB97_SessionLifecycleTest.cpp
 * @date 2026-04-07
 */

#include "bcos-gateway/libp2p/Message.h"
#include "bcos-gateway/libp2p/P2PDecoder.h"
#include "bcos-gateway/libp2p/P2PSession.h"
#include "bcos-gateway/libp2p/Service.h"
#include "bcos-network/ASIOInterface.h"
#include "bcos-network/Host.h"
#include "bcos-network/Socket.h"
#include "bcos-utilities/testutils/TestPromptFixture.h"
#include "unittests/utils/TlsLoopback.h"
#include <bcos-task/Wait.h>
#include <bcos-utilities/IOServicePool.h>
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <future>
#include <list>
#include <optional>
#include <queue>
#include <range/v3/view/single.hpp>
#include <thread>
#include <tuple>

using namespace bcos;
using namespace gateway;
using namespace bcos::test;
using namespace bcos::crypto;

BOOST_FIXTURE_TEST_SUITE(FIB70_FIB97_SessionLifecycleTest, TestPromptFixture)

// --- Fake components (mirrors SessionTest.cpp infrastructure) ---

class FakeASIO_FIB : public bcos::network::ASIOInterface
{
public:
    using ReadCompletion = task::detail::FireCompletion<boost::system::error_code, std::size_t>;

    FakeASIO_FIB()
      : bcos::network::ASIOInterface(
            std::make_shared<bcos::IOServicePool>(1, "FakeASIO_FIB"), "0.0.0.0", 0),
        m_threadPool(std::make_shared<bcos::IOServicePool>(1, "FakeASIO_FIB"))
    {}
    ~FakeASIO_FIB() noexcept {}

    // Compile-time read-initiation policy (see ASIOInterface::awaitableReadSome): the read loop
    // is launched with this policy (startWithPolicy<FakeASIO_FIB::ReadPolicy>) so every read
    // parks its completion here instead of arming the real async_read_some.
    struct ReadPolicy
    {
        template <typename SocketT>
        static void invoke(bcos::network::ASIOInterface* asio,
            const std::shared_ptr<SocketT>& /*socket*/, ba::mutable_buffer /*buffers*/,
            ReadCompletion completion)
        {
            static_cast<FakeASIO_FIB*>(asio)->parkRead(std::move(completion));
        }
    };

    // Read-policy target (see FakeASIO_FIB::ReadPolicy): park the read's completion on the
    // fake's own pool thread. The remaining tests in this file never deliver bytes — they park
    // reads only to hold the loop quiet and drain it at teardown (stopReads) — so no packet
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

// Templated on the socket type: the correlation tests (which wrap the session in a
// P2PSession / register it as a pending-request owner) instantiate it with the production
// Socket.
template <typename SocketT = bcos::network::Socket>
class FakeHost_FIB : public bcos::network::Host<P2PDecoder, SocketT>
{
public:
    FakeHost_FIB(std::shared_ptr<bcos::network::ASIOInterface> _asioInterface,
        std::shared_ptr<bcos::network::BasicSessionFactory<P2PDecoder, SocketT>> _sessionFactory)
      : bcos::network::Host<P2PDecoder, SocketT>(
            std::move(_asioInterface), std::move(_sessionFactory))
    {
        this->m_run = true;
    }
};

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
            std::make_shared<bcos::network::Socket>(io, &sslContext, NodeIPEndpoint()), *host, 16,
            true);
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
    pendingA->callback = [&firedA](bcos::network::NetworkException e,
                             std::optional<bcos::network::FrameMeta>) {
        if (bcos::network::errorCodeOf(e) != 0)
        {
            ++firedA;
        }
    };
    pendingA->owner = sessionA;
    auto pendingB = std::make_shared<PendingResponse>();
    pendingB->callback = [&firedB](bcos::network::NetworkException e,
                             std::optional<bcos::network::FrameMeta>) {
        if (bcos::network::errorCodeOf(e) != 0)
        {
            ++firedB;
        }
    };
    pendingB->owner = sessionB;
    BOOST_REQUIRE(service->registerPendingResponse(seqA, pendingA));
    BOOST_REQUIRE(service->registerPendingResponse(seqB, pendingB));

    service->failPendingResponsesOf(
        sessionA, bcos::network::makeNetworkException(
                      bcos::network::P2PExceptionType::NetworkTimeout, "NetworkTimeout"));

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
    pending->callback = [&fired, &errorCode](bcos::network::NetworkException e,
                            std::optional<bcos::network::FrameMeta> meta) {
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
