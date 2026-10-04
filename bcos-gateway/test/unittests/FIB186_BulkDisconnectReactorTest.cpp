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
 *        never recovers), re-expressed for the pull-mode receive path. CertiK re-test of the
 *        merged admission-control fix (18f48cc7) found A/B (connect-close churn) fixed but D
 *        still permanently halting consensus.
 * @file FIB186_BulkDisconnectReactorTest.cpp
 * @date 2026-07-14
 *
 * Root cause (from code): in the push-mode receive path every inbound P2P message delivery was
 *   Session readLoop -> registered message handler -> asioInterface()->post(...)
 * and every session teardown notification went to that SAME reactor, so message delivery and
 * teardown shared ONE pool. A bulk-disconnect of a large established session pool flooded that
 * reactor with teardown work (each drop drove onDisconnect -> onRemoveNodeIDs ->
 * syncLatestNodeIDList), so validator PBFT messages were read off the socket but their delivery
 * task starved behind teardown -- "validators miss each other's messages", the halt CertiK
 * observed. The push-mode fix gave teardown its own dedicated executor.
 *
 * The pull-mode receive path removes that coupling structurally; the dedicated teardown executor
 * no longer exists because the work it isolated no longer lands on any reactor:
 *   - consumers pull frames with BasicSession::recvMessage() (the Service receive pump), parked
 *     in the session's recv channel;
 *   - Session::drop() only CLOSES that channel (Session.cpp drop): the parked consumer's wake-up
 *     is handed to the channel poster, which posts it to the shared IO pool while the host is
 *     alive — one cheap post per dropped session, and no per-session teardown task chain is
 *     enqueued anywhere. The disconnect handling itself runs inside the consumer's own
 *     continuation (its catch path), not as reactor tasks queued ahead of message delivery.
 *
 * This test drives a real Session::drop() flood over 8 sessions, each with a consumer coroutine
 * parked in recvMessage(), and asserts the pull-mode equivalent of the vector-D guarantee:
 *   (1) every parked consumer is woken and exits with the Disconnect error the channel was
 *       closed with;
 *   (2) a validator-delivery task submitted to the shared pool while the disconnect wake-ups
 *       are queued or running is NOT starved.
 */

#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-network/ASIOInterface.h"
#include "bcos-network/Host.h"
#include "bcos-gateway/libp2p/Message.h"
#include "bcos-gateway/libp2p/P2PDecoder.h"
#include "bcos-task/Wait.h"
#include "bcos-utilities/IOServicePool.h"
#include "bcos-utilities/testutils/TestPromptFixture.h"
#include <chrono>
#include <boost/test/unit_test.hpp>
#include <atomic>
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
// Minimal ASIO fake: the disconnect flood never runs a socket read, so no handler is needed.
class FakeASIO_Reactor : public bcos::network::ASIOInterface
{
public:
    // Two delivery threads: the disconnect wake-ups and the delivery task share this pool. Wide
    // enough that a single stuck wake-up cannot explain a starved delivery, narrow enough that
    // per-session teardown WORK enqueued here (the push-mode regression) would visibly delay the
    // delivery task.
    FakeASIO_Reactor()
      : bcos::network::ASIOInterface(std::make_shared<bcos::IOServicePool>(2, "FIB186Reactor"), "0.0.0.0", 0)
    {}
    ~FakeASIO_Reactor() noexcept = default;
};

// Socket fake backed by a real SSL stream so drop()/closeSocket() can call sslref(); starts
// disconnected so closeSocket() early-returns (this test exercises only the recv-channel path).
class FakeSocket_Reactor
{
public:
    FakeSocket_Reactor()
      : m_ioContext(std::make_shared<ba::io_context>()),
        m_sslContext(ba::ssl::context::tlsv12),
        m_sslSocket(std::make_shared<ba::ssl::stream<bi::tcp::socket>>(*m_ioContext, m_sslContext))
    {}
    bool isConnected() const { return m_connected; }
    void close() { m_connected = false; }
    bi::tcp::endpoint remoteEndpoint(boost::system::error_code = {}) { return {}; }
    bi::tcp::endpoint localEndpoint(boost::system::error_code = {}) { return {}; }
    bi::tcp::socket& ref() { return m_sslSocket->next_layer(); }
    ba::ssl::stream<bi::tcp::socket>& sslref() { return *m_sslSocket; }
    // ASIOInterface dispatches reads/writes on stream(); the raw TCP socket keeps this fake's
    // IO plaintext (this test never performs real IO — the socket stays disconnected).
    bi::tcp::socket& stream() { return ref(); }
    const NodeIPEndpoint& nodeIPEndpoint() const { return m_nodeIPEndpoint; }
    void setNodeIPEndpoint(NodeIPEndpoint) {}
    ba::io_context& ioService() { return *m_ioContext; }

    bool m_connected{false};

private:
    std::shared_ptr<ba::io_context> m_ioContext;
    ba::ssl::context m_sslContext;
    std::shared_ptr<ba::ssl::stream<bi::tcp::socket>> m_sslSocket;
    NodeIPEndpoint m_nodeIPEndpoint;
};

// Host subclass with the network marked up (m_run = true), so Session::drop() takes the
// live-network path: the recv-channel poster posts each parked consumer's wake-up to the shared
// pool instead of running it inline (haveNetwork() == false would run the wake inline on the
// dropping thread).
class FakeHost_Reactor : public bcos::network::Host<P2PDecoder, FakeSocket_Reactor>
{
public:
    FakeHost_Reactor(std::shared_ptr<bcos::network::ASIOInterface> asioInterface)
      : bcos::network::Host<P2PDecoder, FakeSocket_Reactor>(std::move(asioInterface), nullptr)
    {
        this->m_run = true;
    }
};

using Session_Reactor = bcos::network::BasicSession<P2PDecoder, FakeSocket_Reactor>;

// Shared state, held by shared_ptr so a coroutine or task that outlives the test body never
// dangles.
struct ReactorProbe
{
    std::atomic<int> parked{0};          // consumers parked in recvMessage()
    std::atomic<int> disconnects{0};     // consumers woken by drop() with a Disconnect error
    std::atomic<bool> delivered{false};  // set when the validator-delivery task runs
    std::atomic<int> done{0};            // total completions (drain barrier)
};
}  // namespace

// Vector D in pull mode: a bulk disconnect must wake every consumer parked in recvMessage() with
// the Disconnect error, and the flood of consumer wake-ups posted to the shared pool must not
// starve consensus message delivery on that same pool.
BOOST_AUTO_TEST_CASE(TeardownFloodMustNotStarveMessageDelivery)
{
    // The pool width is pinned by FakeASIO_Reactor's own IOServicePool(2) rather than by a
    // process-global control, so "the disconnect wake-ups and the delivery task share this pool"
    // is deterministic and independent of the host core count.
    auto fakeAsio = std::make_shared<FakeASIO_Reactor>();
    auto fakeHost = std::make_shared<FakeHost_Reactor>(fakeAsio);

    auto probe = std::make_shared<ReactorProbe>();

    // One consumer coroutine per session, parked in recvMessage() exactly like the Service
    // receive pump (Service::receiveLoop). task::wait starts the coroutine synchronously, so the
    // consumer is parked in the session's recv channel before task::wait returns.
    constexpr int floodCount = 8;
    std::vector<Session_Reactor::Ptr> sessions;
    sessions.reserve(floodCount);
    for (int i = 0; i < floodCount; ++i)
    {
        auto socket = std::make_shared<FakeSocket_Reactor>();
        auto session = std::make_shared<Session_Reactor>(socket, *fakeHost, 1024, true);
        task::wait([](Session_Reactor::Ptr _session,
                       std::shared_ptr<ReactorProbe> _probe) -> task::Task<void> {
            _probe->parked.fetch_add(1);
            try
            {
                while (true)
                {
                    // This test never delivers a frame; only the teardown wake matters.
                    [[maybe_unused]] auto meta = co_await _session->recvMessage();
                }
            }
            catch (bcos::network::NetworkException& e)
            {
                // The pull-mode teardown notification: drop() closed the recv channel with the
                // disconnect error. This catch is mandatory — an exception escaping a
                // task::wait'd coroutine is rethrown on the resuming pool thread.
                if (bcos::network::errorCodeOf(e) == bcos::network::P2PExceptionType::Disconnect)
                {
                    _probe->disconnects.fetch_add(1);
                }
                _probe->done.fetch_add(1);
                co_return;
            }
        }(session, probe));
        sessions.push_back(std::move(session));
    }

    // Every consumer parked before any drop: each drop below then takes the channel's
    // posted-wake path — the one a real bulk disconnect exercises.
    BOOST_REQUIRE_EQUAL(probe->parked.load(), floodCount);

    // Bulk-disconnect: each drop() closes the session's recv channel, and the channel poster
    // hands the parked consumer's wake-up to the shared pool (one post per session). No
    // per-session teardown task chain is enqueued anywhere.
    for (auto& session : sessions)
    {
        session->drop(bcos::network::DisconnectReason::TCPError);
    }

    // A validator PBFT message delivery is posted to the same shared pool (the Session message
    // delivery path posts there). Submit it while the disconnect wake-ups are queued or running.
    fakeHost->asioInterface()->post([probe]() {
        probe->delivered.store(true);
        probe->done.fetch_add(1);
    });

    // Grace window: a healthy node must deliver consensus messages far inside a PBFT round. A
    // disconnect flood that enqueued per-session teardown WORK on this pool ahead of delivery
    // (the push-mode regression) would delay the delivery task beyond the window.
    for (int i = 0; i < 50 && !probe->delivered.load(); ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    bool deliveredDuringFlood = probe->delivered.load();

    // Drain barrier: every parked consumer must have been woken with Disconnect, plus the
    // delivery task, so no coroutine or lambda outlives this scope.
    for (int i = 0; i < 5000 && probe->done.load() < floodCount + 1; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    BOOST_CHECK_MESSAGE(deliveredDuringFlood,
        "FIB-186 vector D (pull mode): a PBFT message delivery submitted during a "
        "bulk-disconnect flood must not be starved. The wake-ups drop() posts for parked "
        "recvMessage() consumers share the delivery pool; if this fails, per-session teardown "
        "work is back on the delivery pool ahead of consensus messages.");
    BOOST_CHECK_MESSAGE(probe->disconnects.load() == floodCount,
        "every consumer parked in recvMessage() must be woken by drop() with the Disconnect "
        "error the recv channel was closed with");
    BOOST_CHECK_EQUAL(probe->done.load(), floodCount + 1);
}

BOOST_AUTO_TEST_SUITE_END()
