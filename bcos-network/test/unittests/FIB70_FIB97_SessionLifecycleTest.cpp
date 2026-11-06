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
 * @brief Regression tests for FIB-70 (decode error drops the session) and FIB-97 (socket
 *        shared_ptr capture keeps the socket alive across async teardown), migrated from
 *        bcos-gateway/test/unittests/FIB70_FIB97_SessionLifecycleTest.cpp to bcos-network.
 * @file FIB70_FIB97_SessionLifecycleTest.cpp
 * @date 2026-04-07
 *
 * The request/response correlation cases that used to share this file (the Service pending-
 * response table and the P2PSession write-failure settlement) stayed in the gateway suite:
 * their subject is libp2p policy, not the framed transport. The two cases here exercise the
 * library through the test-local frame format/decoder (utils/TestFraming.h).
 */

#include "unittests/utils/TestAsioInterface.h"
#include "unittests/utils/TestFraming.h"
#include "unittests/utils/TestSessionSocket.h"
#include <bcos-network/ASIOInterface.h>
#include <bcos-network/Host.h>
#include <bcos-network/Session.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/IOServicePool.h>
#include <boost/test/unit_test.hpp>
#include <chrono>
#include <future>
#include <memory>
#include <thread>

using namespace bcos;
using namespace bcos::network;
using namespace bcos::network::test;

BOOST_AUTO_TEST_SUITE(FIB70_FIB97_SessionLifecycleTest)

class FakeHost_FIB : public bcos::network::Host<TestFrameDecoder, TestSessionSocket>
{
public:
    FakeHost_FIB(std::shared_ptr<bcos::network::ASIOInterface> _asioInterface)
      : bcos::network::Host<TestFrameDecoder, TestSessionSocket>(std::move(_asioInterface), nullptr)
    {
        this->m_run = true;
    }
};

using Session_FIB = bcos::network::BasicSession<TestFrameDecoder, TestSessionSocket>;

// A frame a protocol layer would reject for its CONTENT (it mimics a header that promises an
// extended section it does not carry). The library's decoder accepted the length, so the frame
// must be delivered to the consumer — bcos-network never inspects frame content, and protocol-
// level validation of such frames is the consumer's business. In the gateway suite this was the
// V2 truncated-extended-header frame; the shape is kept for continuity.
inline std::shared_ptr<std::vector<uint8_t>> buildTruncatedProtocolHeaderFrame()
{
    std::vector<uint8_t> frame(14, 0);
    TestFrameDecoder::writeLength(frame.data(), static_cast<uint32_t>(frame.size()));
    // "version 2" marker: the protocol layer would expect ttl/src/dst to follow, but the frame
    // ends after the fixed header
    frame[4] = 0x00;
    frame[5] = 0x02;
    return std::make_shared<std::vector<uint8_t>>(std::move(frame));
}

// FIB-70: Verify that a decode error (FrameMeta::Status::ProtocolError from the decoder)
// triggers session drop. Before the fix, the session would remain active as a "zombie" until
// the idle timeout. After the fix, drop(UserReason) is called immediately, setting
// m_active = false. The ProtocolError is produced by an invalid declared length — the test
// decoder's format-intrinsic error trigger (the gateway suite used a bad P2P version field).
BOOST_AUTO_TEST_CASE(DecodeErrorTriggersSessionDrop)
{
    auto fakeSocket = std::make_shared<TestSessionSocket>();

    {
        auto fakeAsio = std::make_shared<TestAsioInterface>();
        auto fakeHost = std::make_shared<FakeHost_FIB>(fakeAsio);

        // 16-byte initial buffer: the read loop must see the 4-byte length prefix before it can
        // make progress on a real frame
        auto session = std::make_shared<Session_FIB>(fakeSocket, *fakeHost, 16, true);

        // Pull-mode consumer: recvMessage() rethrows the NetworkException the recv channel was
        // closed with — on a decode error the read loop closes it with ProtocolError, then drops
        // the session. The consumer's resume is posted to the shared pool through the channel
        // poster, so the outcome is handed back through a promise, never assumed synchronous.
        // The promise is shared so an aborted test (BOOST_REQUIRE timeout) cannot leave the
        // consumer writing into a destroyed promise.
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

        session->startWithPolicy<TestAsioInterface::ReadPolicy>();

        // Send a frame that will trigger a decode error (ProtocolError)
        fakeAsio->asyncAppendRecvPacket(
            std::make_shared<std::vector<uint8_t>>(buildProtocolErrorFrame()));

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

// A frame whose payload the protocol layer would reject (a "truncated extended header") used to
// be rejected inside the session's decode step; with stream splitting limited to length
// validation it is now a complete, consumable frame delivered to the recvMessage() consumer,
// and the session stays up. (Rejection happens at the libp2p boundary — covered by the
// gateway's P2PDecoderTest and Service ProtocolError path.)
BOOST_AUTO_TEST_CASE(TruncatedProtocolHeaderIsDeliveredNotDropped)
{
    auto fakeSocket = std::make_shared<TestSessionSocket>();

    {
        auto fakeAsio = std::make_shared<TestAsioInterface>();
        auto fakeHost = std::make_shared<FakeHost_FIB>(fakeAsio);

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

        session->startWithPolicy<TestAsioInterface::ReadPolicy>();

        fakeAsio->asyncAppendRecvPacket(buildTruncatedProtocolHeaderFrame());

        // Wait for the frame to be delivered to the consumer
        BOOST_REQUIRE(
            receivedFuture.wait_for(std::chrono::seconds(10)) == std::future_status::ready);
        auto receivedBytes = receivedFuture.get();
        auto expected = buildTruncatedProtocolHeaderFrame();
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
    auto fakeSocket = std::make_shared<TestSessionSocket>();

    // Verify socket has expected reference count before session creation
    auto initialRefCount = fakeSocket.use_count();
    BOOST_CHECK_EQUAL(initialRefCount, 1);

    {
        auto fakeAsio = std::make_shared<TestAsioInterface>();
        auto fakeHost = std::make_shared<FakeHost_FIB>(fakeAsio);

        auto session = std::make_shared<Session_FIB>(fakeSocket, *fakeHost, 2, true);

        // After session creation, socket should be held by both fakeSocket and session
        BOOST_CHECK(fakeSocket.use_count() > 1);

        session->setSocket(nullptr);
    }

    // After session destruction, only fakeSocket holds the socket
    BOOST_CHECK_EQUAL(fakeSocket.use_count(), 1);

    fakeSocket->close();
}

BOOST_AUTO_TEST_SUITE_END()
