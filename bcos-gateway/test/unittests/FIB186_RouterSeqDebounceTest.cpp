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
 * @brief Regression test for FIB-186 vector B: connection churn amplifying into a router-table
 *        gossip storm that starves PBFT delivery.
 * @file FIB186_RouterSeqDebounceTest.cpp
 * @date 2026-07-21
 *
 * On the pre-fix code every membership change (onNewSession / onEraseSession) and every learned
 * route (joinRouterTable) called broadcastRouterSeq() synchronously. Under a connect/disconnect
 * flood that cascaded a full-mesh seq -> request -> whole-table gossip, and all of it ran on
 * Host::m_asyncGroup -- the same reactor that delivers PBFT messages -- so consensus was starved.
 *
 * The fix coalesces the broadcasts: the first membership change of a burst broadcasts once (leading
 * edge) and the rest only advance the seq, which the 3s router timer flushes. This test is
 * black-box: it drives a burst of route/membership changes through the PUBLIC entry points (the
 * registered RouterTableResponse message handler for route joins, Service::onDisconnect for
 * session loss) and counts the RouterTableSyncSeq frames a real neighbour session receives over a
 * loopback socket -- exactly one for the whole burst, not one-per-change (RED on the pre-fix
 * per-change broadcast, the gossip storm) and not zero (RED on deleting the event-driven
 * broadcast, which would serialize route convergence behind the 3s timer). The isReachable()
 * precondition guarantees the handler reached the router-table update, so the assertion is not
 * vacuously satisfied by an early return.
 */

#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-framework/gateway/GatewayTypeDef.h"
#include "bcos-framework/protocol/ProtocolInfo.h"
#include "bcos-gateway/libnetwork/ASIOInterface.h"
#include "bcos-gateway/libnetwork/Host.h"
#include "bcos-gateway/libnetwork/SessionReadLoop.h"
#include "bcos-gateway/libp2p/Message.h"
#include "bcos-gateway/libp2p/P2PDecoder.h"
#include "bcos-gateway/libp2p/P2PSession.h"
#include "bcos-gateway/libp2p/Service.h"
#include "bcos-gateway/libp2p/router/RouterTableImpl.h"
#include "bcos-utilities/IOServicePool.h"
#include "bcos-utilities/testutils/TestPromptFixture.h"
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::test;

BOOST_FIXTURE_TEST_SUITE(FIB186_RouterSeqDebounceTest, TestPromptFixture)

namespace
{
// Read-parking fake ASIO (same shape as SessionTest's FakeASIO): the service-side session's read
// loop parks its completion here instead of arming the real async_read_some -- the loopback peer
// never sends anything service-side, so reads simply stay parked until stopReads() unwinds them.
class FakeASIO_Debounce : public bcos::gateway::ASIOInterface
{
public:
    using Packet = std::shared_ptr<std::vector<uint8_t>>;
    using ReadCompletion = task::detail::FireCompletion<boost::system::error_code, std::size_t>;

    FakeASIO_Debounce()
      : ASIOInterface(std::make_shared<bcos::IOServicePool>(1, "FakeASIO_Debounce"), "0.0.0.0", 0),
        m_threadPool(std::make_shared<bcos::IOServicePool>(1, "FakeASIO_Debounce"))
    {}
    ~FakeASIO_Debounce() noexcept {};

    struct ReadPolicy
    {
        template <typename SocketT>
        static void invoke(ASIOInterface* asio, const std::shared_ptr<SocketT>& /*socket*/,
            ba::mutable_buffer buffers, ReadCompletion completion)
        {
            static_cast<FakeASIO_Debounce*>(asio)->parkRead(buffers, std::move(completion));
        }
    };

    // The park is posted onto the pool thread so EVERY access to m_pendingReads happens on the
    // single pool thread (see SessionTest's FakeASIO for the race this avoids).
    void parkRead(ba::mutable_buffer buffers, ReadCompletion completion)
    {
        ++m_readsInFlight;
        m_threadPool->post([this, buffers, completion = std::move(completion)]() mutable {
            m_pendingReads.push_back(PendingRead{buffers, std::move(completion)});
        });
    }

    // Test teardown: complete every parked read with operation_aborted so the read loops -- and
    // the sessions their frames keep alive -- unwind BEFORE the test destroys this fake. Poll
    // readsInFlight() until 0 afterwards (see SessionTest).
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

protected:
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

    std::list<PendingRead> m_pendingReads;
    std::atomic<std::size_t> m_readsInFlight{0};
    bcos::IOServicePool::Ptr m_threadPool;
};

template <typename SocketT>
class FakeHost_Debounce : public bcos::gateway::Host<P2PDecoder, SocketT>
{
public:
    FakeHost_Debounce(std::shared_ptr<ASIOInterface> _asioInterface,
        std::shared_ptr<BasicSessionFactory<P2PDecoder, SocketT>> _sessionFactory)
      : Host<P2PDecoder, SocketT>(std::move(_asioInterface), std::move(_sessionFactory))
    {
        this->m_run = true;
    }
};

// Wrap a connected TCP socket in the production Socket (see SessionTest's makeLoopbackSocket).
// The ssl::context must outlive the returned Socket (the ssl::stream references it).
std::shared_ptr<Socket> makeLoopbackSocketDebounce(
    std::shared_ptr<ba::io_context> _ioContext, ba::ssl::context& _sslContext,
    bi::tcp::socket _socket)
{
    auto socket = std::make_shared<Socket>(std::move(_ioContext), _sslContext, NodeIPEndpoint());
    socket->ref() = std::move(_socket);
    return socket;
}

// Service::m_sessions is protected; expose insertion for the neighbour/churn sessions below (same
// seam as SessionTest's FanoutProbeService). Nothing router-internal is touched.
class RouterProbeService : public Service
{
public:
    using Service::Service;
    void addSession(P2pID const& _nodeID, P2PSession::Ptr _session)
    {
        std::unique_lock lock(x_sessions);
        m_sessions[_nodeID] = std::move(_session);
    }
};

// Populate p2pInfo directly (mutableP2pInfo avoids setP2PInfo, which dereferences the session's
// socket) so the router entry's dstNode (p2pID) and dstNodeInfo (p2pInfo.rawP2pID) agree.
class FakeSessionVB : public P2PSession
{
public:
    explicit FakeSessionVB(std::string _id) : m_id(std::move(_id))
    {
        auto info = mutableP2pInfo();
        info->rawP2pID = m_id;
        info->p2pID = m_id;
    }
    P2pID p2pID() override { return m_id; }
    std::string printP2pID() override { return m_id; }
    std::string m_id;
};

// Read one length-prefixed frame off the raw loopback peer (see SessionTest's readExactFrame);
// empty on error/EOF.
std::vector<uint8_t> readExactFrame(bi::tcp::socket& _peer)
{
    std::array<uint8_t, 4> lenBuf{};
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
}
}  // namespace

BOOST_AUTO_TEST_CASE(MembershipChurnCoalescesRouterSeqToOneLeadingEdgeBroadcast)
{
    auto fakeAsio = std::make_shared<FakeASIO_Debounce>();

    auto io = std::make_shared<ba::io_context>();
    boost::asio::executor_work_guard<ba::io_context::executor_type> workGuard(io->get_executor());
    std::thread ioThread([io] { io->run(); });

    ba::ip::tcp::acceptor acceptor(*io, ba::ip::tcp::endpoint(ba::ip::tcp::v4(), 0));
    auto listenEndpoint = acceptor.local_endpoint();

    // The loopback peer: records the packetType of every frame the service writes to its
    // neighbour session. The service never start()s here, so P2PSession heartbeats are skipped
    // (they require service->active()) and the ONLY frames on the wire are router-seq broadcasts.
    std::mutex recvMutex;
    std::vector<uint16_t> receivedTypes;
    std::thread peerThread([&] {
        bi::tcp::socket peer(*io);
        boost::system::error_code ec;
        acceptor.accept(peer, ec);
        if (ec)
        {
            return;
        }
        while (true)
        {
            auto frame = readExactFrame(peer);
            if (frame.empty())
            {
                return;
            }
            Message message;
            if (message.decode(bcos::ref(frame)) < 0)
            {
                return;
            }
            std::lock_guard<std::mutex> lock(recvMutex);
            receivedTypes.push_back(message.packetType());
        }
    });

    bi::tcp::socket client(*io);
    {
        boost::system::error_code connectError;
        client.connect(listenEndpoint, connectError);
        BOOST_REQUIRE(!connectError);
    }

    P2PInfo selfInfo;
    selfInfo.rawP2pID = "selfRawP2pID";
    selfInfo.p2pID = "selfP2pID";
    auto factory = std::make_shared<RouterTableFactoryImpl>();
    // The router-seq sync timer is bound to this io_context, which never runs: the 3s flush never
    // fires and the coalescing flag stays set, exactly the unit environment the old
    // subclass-based test created by overriding the broadcast away.
    boost::asio::io_context routerIo;
    auto service = std::make_shared<RouterProbeService>(selfInfo, factory, &routerIo);
    service->setHost(std::make_shared<FakeHost_Debounce<Socket>>(fakeAsio, nullptr));

    // One real neighbour session ("recorder") over the loopback: the leading-edge broadcast must
    // reach it exactly once. The FakeHosts must outlive the sessions (Session holds a
    // reference_wrapper<Host>); the ssl::context must outlive every Socket.
    ba::ssl::context sslContext(ba::ssl::context::tlsv12);
    std::vector<std::shared_ptr<FakeHost_Debounce<Socket>>> hosts;
    std::shared_ptr<Session> recorderSession;
    {
        auto host = std::make_shared<FakeHost_Debounce<Socket>>(fakeAsio, nullptr);
        hosts.push_back(host);
        recorderSession = std::make_shared<Session>(
            makeLoopbackSocketDebounce(io, sslContext, std::move(client)), *host, 2, true);
        recorderSession->startWithPolicy<FakeASIO_Debounce::ReadPolicy>();

        auto p2pSession = std::make_shared<P2PSession>();
        p2pSession->setSession(recorderSession);
        p2pSession->setService(service);
        auto protocolInfo = std::make_shared<bcos::protocol::ProtocolInfo>(
            bcos::protocol::ProtocolModuleID::GatewayService, 0, 2);
        protocolInfo->setVersion(2);
        p2pSession->setProtocolInfo(protocolInfo);
        P2PInfo peerInfo;
        peerInfo.rawP2pID = "recorder";
        peerInfo.p2pID = "recorder";
        p2pSession->setP2PInfo(peerInfo);
        // marks the session active (m_run); the initial heartbeat is skipped because the service
        // is not active
        p2pSession->start();
        service->addSession("recorder", std::move(p2pSession));
    }

    auto countRouterSeq = [&] {
        std::lock_guard<std::mutex> lock(recvMutex);
        return std::count(receivedTypes.begin(), receivedTypes.end(),
            uint16_t(GatewayMessageType::RouterTableSyncSeq));
    };

    // A connect flood drives many route changes in quick succession, injected through the PUBLIC
    // RouterTableResponse message handler (each table teaches the service one new node -- the
    // joinRouterTable -> markRouterSeqChanged path). On the pre-fix code each one broadcast once
    // per change (the gossip storm); the debounced code broadcasts once on the first change
    // (leading edge) and coalesces the rest.
    auto handler = service->getMessageHandlerByMsgType(GatewayMessageType::RouterTableResponse);
    BOOST_REQUIRE(handler);
    constexpr int kChurn = 8;
    for (int i = 0; i < kChurn; ++i)
    {
        auto nodeID = "peer-" + std::to_string(i);
        auto senderID = "sender-" + std::to_string(i);
        auto table = factory->createRouterTable();
        auto entry = factory->createRouterEntry();
        entry->setDstNode(nodeID);
        P2PInfo nodeInfo;
        nodeInfo.rawP2pID = nodeID;
        nodeInfo.p2pID = nodeID;
        entry->setDstNodeInfo(nodeInfo);
        entry->setDistance(1);
        std::set<std::string> unreachableNodes;
        table->update(unreachableNodes, senderID, entry);
        bcos::bytes tableData;
        table->encode(tableData);

        Message message;
        message.setPacketType(GatewayMessageType::RouterTableResponse);
        message.setPayload(std::move(tableData));
        handler(NetworkException{}, std::make_shared<FakeSessionVB>(senderID),
            std::move(message));
    }

    BOOST_REQUIRE_MESSAGE(service->isReachable("peer-0"),
        "precondition: the injected router tables must have recorded the peers in the router "
        "table, else the assertion below would be vacuously satisfied by an early return");

    // The broadcast fan-out is fire-and-forget (task::wait), so wait (bounded) for the single
    // leading-edge RouterTableSyncSeq frame to actually reach the recorder neighbour.
    for (size_t retry = 0; retry < 200 && countRouterSeq() < 1; ++retry)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    BOOST_CHECK_MESSAGE(countRouterSeq() == 1,
        "FIB-186 vector B: a burst of route changes must coalesce to exactly one leading-edge "
        "router-seq broadcast -- not one per change (the gossip storm that starved the PBFT "
        "delivery pool) and not zero (which would serialize route convergence behind the 3s "
        "timer).");

    // Session-loss churn within the same (un-flushed) window stays coalesced: drive the public
    // disconnect path (onDisconnect -> onEraseSession) for each churned peer.
    for (int i = 0; i < kChurn; ++i)
    {
        auto peer = std::make_shared<FakeSessionVB>("peer-" + std::to_string(i));
        // onDisconnect logs the session endpoint and the default log level builds even TRACE
        // streams eagerly, so the fake needs a session object -- a never-started Session over an
        // unconnected Socket is enough (nodeIPEndpoint() reads the stored endpoint member).
        auto socket = std::make_shared<Socket>(io, sslContext, NodeIPEndpoint());
        peer->setSession(std::make_shared<Session>(socket, *hosts.front(), 2, true));
        service->addSession(peer->p2pID(), peer);
        service->onDisconnect(NetworkException{}, std::move(peer));
    }
    // A per-erase broadcast regression would put more RouterTableSyncSeq frames on the wire
    // immediately; give any (bogus) fan-out a grace window to land before asserting.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    BOOST_CHECK_MESSAGE(countRouterSeq() == 1,
        "FIB-186 vector B: erase churn within the same window must also coalesce; the coalesced "
        "seq is flushed once by the router timer, not once per erase.");

    // Teardown (SessionTest order): disconnect the session BEFORE joining the peer thread so a
    // stuck peer read can never hang the test; then fail the parked reads and wait for the read
    // loops to unwind before the fake is destroyed.
    recorderSession->disconnect(DisconnectReason::DisconnectRequested);
    service->stop();
    fakeAsio->stopReads();
    size_t drainRetry = 0;
    while (fakeAsio->readsInFlight() != 0 && drainRetry < 200)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        drainRetry++;
    }
    BOOST_REQUIRE_EQUAL(fakeAsio->readsInFlight(), 0);

    peerThread.join();
    workGuard.reset();
    {
        boost::system::error_code ec;
        acceptor.close(ec);
    }
    io->stop();
    ioThread.join();
    fakeAsio->stop();
}

BOOST_AUTO_TEST_SUITE_END()
