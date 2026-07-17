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
 * @brief Mechanism confirmation for FIB-186 vector D (persistent bulk-disconnect halts consensus,
 *        never recovers). CertiK re-test of the merged admission-control fix (18f48cc7) found A/B
 *        (connect-close churn) fixed but D still permanently halts consensus.
 * @file FIB186_BulkDisconnectReactorTest.cpp
 * @date 2026-07-14
 *
 * Root cause (from code): every inbound P2P message delivery is
 *   Session::doRead -> Session::onMessage -> Host::asyncTo(...)   (Session.cpp: onMessage, ~L694)
 * and every session teardown notification is
 *   Session::drop -> Host::asyncTo(...)                           (Session.cpp: drop, ~L372)
 * Host::asyncTo is `m_asyncGroup.run(...)` (Host.h) -- a single unbounded TBB task_group on the
 * process-global pool. So message delivery and teardown share ONE reactor. A bulk-disconnect of a
 * large established session pool floods that reactor with teardown work (each drop drives
 * onDisconnect -> onRemoveNodeIDs -> syncLatestNodeIDList), so validator PBFT messages are read off
 * the socket but their delivery task is starved behind teardown -- "validators miss each other's
 * messages", the halt CertiK observed. The accept-side admission control cannot touch this: it
 * gates NEW connections before the handshake, whereas D tears down ALREADY-established sessions.
 *
 * This is a RED (failing) test on the current merged code: it drives a real Session::drop() flood
 * whose teardown work occupies the shared reactor, then submits a validator-delivery task through
 * the same reactor and asserts it is NOT starved. It fails today (delivery cannot run while
 * teardown occupies the reactor) and is the invariant a fix must satisfy: teardown must not be able
 * to starve consensus-message delivery. Because it drives the real drop() path, a fix that moves
 * teardown off m_asyncGroup (a dedicated executor) turns it green, while leaving delivery where it
 * is; a fix that merely throttles teardown on the same reactor does not.
 */

#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-gateway/libnetwork/ASIOInterface.h"
#include "bcos-gateway/libnetwork/Host.h"
#include "bcos-gateway/libnetwork/Session.h"
#include "bcos-gateway/libnetwork/Socket.h"
#include "bcos-gateway/libp2p/P2PMessage.h"
#include "bcos-utilities/testutils/TestPromptFixture.h"
#include <oneapi/tbb/global_control.h>
#include <boost/asio/error.hpp>
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::test;
using namespace bcos::crypto;

namespace ba = boost::asio;
namespace bi = boost::asio::ip;

BOOST_FIXTURE_TEST_SUITE(FIB186_BulkDisconnectReactorTest, TestPromptFixture)

namespace
{
// Minimal ASIO fake: the teardown flood never runs a socket read, so no handler is needed.
class FakeASIO_Reactor : public bcos::gateway::ASIOInterface
{
public:
    ~FakeASIO_Reactor() noexcept override = default;
    void asyncReadSome(
        const std::shared_ptr<SocketFace>&, ba::mutable_buffer, ReadWriteHandler) override
    {}
    void strandPost(Base_Handler) override {}
    void stop() override {}
};

// Host subclass that exposes the real Host::asyncTo (== m_asyncGroup.run) with the network marked
// up, so Session::drop() takes the "post the teardown notification onto m_asyncGroup" path.
class FakeHost_Reactor : public bcos::gateway::Host
{
public:
    FakeHost_Reactor(bcos::crypto::Hash::Ptr hash, std::shared_ptr<ASIOInterface> asioInterface,
        MessageFactory::Ptr messageFactory)
      : Host(std::move(hash), std::move(asioInterface), nullptr, std::move(messageFactory))
    {
        m_run = true;
    }
};

// Socket fake backed by a real SSL stream so drop()/closeSocket() can call sslref(); starts
// disconnected so closeSocket() early-returns (this test exercises only the m_asyncGroup path).
class FakeSocket_Reactor : public SocketFace
{
public:
    FakeSocket_Reactor()
      : m_ioContext(std::make_shared<ba::io_context>()),
        m_sslContext(ba::ssl::context::tlsv12),
        m_sslSocket(std::make_shared<ba::ssl::stream<bi::tcp::socket>>(*m_ioContext, m_sslContext))
    {}
    bool isConnected() const override { return m_connected; }
    void close() override { m_connected = false; }
    bi::tcp::endpoint remoteEndpoint(boost::system::error_code) override { return {}; }
    bi::tcp::endpoint localEndpoint(boost::system::error_code) override { return {}; }
    bi::tcp::socket& ref() override { return m_sslSocket->next_layer(); }
    ba::ssl::stream<bi::tcp::socket>& sslref() override { return *m_sslSocket; }
    const NodeIPEndpoint& nodeIPEndpoint() const override { return m_nodeIPEndpoint; }
    void setNodeIPEndpoint(NodeIPEndpoint) override {}
    ba::io_context& ioService() override { return *m_ioContext; }

    bool m_connected{false};

private:
    std::shared_ptr<ba::io_context> m_ioContext;
    ba::ssl::context m_sslContext;
    std::shared_ptr<ba::ssl::stream<bi::tcp::socket>> m_sslSocket;
    NodeIPEndpoint m_nodeIPEndpoint;
};

// Shared state, held by shared_ptr so a task that outlives the test body never dangles.
struct ReactorProbe
{
    std::atomic<int> teardownRunning{0};  // teardown tasks currently occupying a reactor worker
    std::atomic<bool> release{false};     // gate that frees the occupying teardown tasks
    std::atomic<bool> delivered{false};   // set when the validator-delivery task runs
    std::atomic<int> done{0};             // total tasks completed (drain barrier)
};
}  // namespace

// Vector D as a shared-reactor starvation: teardown of established sessions and PBFT message
// delivery both run on Host::m_asyncGroup, so a teardown flood starves delivery.
BOOST_AUTO_TEST_CASE(TeardownFloodMustNotStarveMessageDelivery)
{
    // Pin the shared TBB pool to a small, known width so "every reactor worker is occupied by
    // teardown" is deterministic instead of depending on the host core count.
    tbb::global_control gc(tbb::global_control::max_allowed_parallelism, 2);

    auto hashImpl = std::make_shared<Keccak256>();
    auto messageFactory = std::make_shared<P2PMessageFactory>();
    auto fakeAsio = std::make_shared<FakeASIO_Reactor>();
    auto fakeHost = std::make_shared<FakeHost_Reactor>(hashImpl, fakeAsio, messageFactory);

    auto probe = std::make_shared<ReactorProbe>();

    // Each dropped session's teardown notification models the per-disconnect work a real
    // bulk-disconnect produces (onDisconnect -> onRemoveNodeIDs -> syncLatestNodeIDList): it
    // occupies a reactor worker until released. More sessions than possible workers guarantees
    // every worker ends up in teardown.
    constexpr int floodCount = 8;
    std::vector<Session::Ptr> sessions;
    sessions.reserve(floodCount);
    for (int i = 0; i < floodCount; ++i)
    {
        auto socket = std::make_shared<FakeSocket_Reactor>();
        auto session = std::make_shared<Session>(socket, *fakeHost, 1024, true);
        session->setMessageFactory(messageFactory);
        session->setMessageHandler([probe](NetworkException, SessionFace::Ptr, Message::Ptr) {
            probe->teardownRunning.fetch_add(1);
            while (!probe->release.load())
            {  // hold the reactor worker, as a batch of real teardowns would
            }
            probe->done.fetch_add(1);
        });
        sessions.push_back(std::move(session));
    }

    // Bulk-disconnect: each drop() posts its teardown notification onto m_asyncGroup (Session.cpp
    // drop, ~L372) -- the same reactor message delivery uses.
    for (auto& session : sessions)
    {
        session->drop(DisconnectReason::TCPError);
    }

    // Wait until teardown occupies the reactor, then let any second worker also pick up a teardown
    // task (there are more teardown tasks than workers), so no worker is left idle.
    while (probe->teardownRunning.load() < 1)
    {
        std::this_thread::yield();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // A validator PBFT message delivery goes through the same reactor (Session.cpp onMessage,
    // ~L694). Submit it now, with teardown occupying the reactor.
    fakeHost->asyncTo([probe]() {
        probe->delivered.store(true);
        probe->done.fetch_add(1);
    });

    // Grace window: a healthy node must deliver consensus messages far inside a PBFT round. If the
    // reactor is shared, teardown holds every worker and delivery cannot run within the window.
    for (int i = 0; i < 50 && !probe->delivered.load(); ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    bool deliveredDuringFlood = probe->delivered.load();

    // Release the flood and drain every task so no lambda outlives this scope.
    probe->release.store(true);
    for (int i = 0; i < 5000 && probe->done.load() < floodCount + 1; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    BOOST_CHECK_MESSAGE(deliveredDuringFlood,
        "FIB-186 vector D: a PBFT message delivery submitted during a session-teardown flood must "
        "not be starved. It is today because Session::drop() and Session::onMessage() share the "
        "unbounded m_asyncGroup reactor -- teardown occupies every worker and delivery never runs, "
        "halting consensus. A fix must route teardown off the delivery reactor.");
    BOOST_CHECK_EQUAL(probe->done.load(), floodCount + 1);
}

BOOST_AUTO_TEST_SUITE_END()
