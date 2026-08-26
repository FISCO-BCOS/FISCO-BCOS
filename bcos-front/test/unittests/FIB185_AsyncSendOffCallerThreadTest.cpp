/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief FIB-185: FrontService's owned-payload async entry points must dispatch the gateway send
 *        off the caller thread. The gateway send acquires the gateway session lock synchronously
 *        and, under TLS-churn, that lock is contended by session connect/disconnect; running the
 *        send on the caller thread (e.g. the PBFT consensus worker holding m_mutex) couples
 *        consensus to gateway lock contention and halts consensus. Both the broadcast
 *        (asyncBroadcastMessageByOwnedPayload, used by PBFT's prepare/commit/view-change
 *        broadcasts) and the point-to-point send (asyncSendMessageByNodeIDByOwnedPayload, used by
 *        sendViewChange/sendRecoverResponse) hand the owned payload to a serial send queue and
 *        return immediately. These tests pin that: with a gateway whose send blocks, the caller
 *        returns promptly and the send runs on another thread.
 * @file FIB185_AsyncSendOffCallerThreadTest.cpp
 */

#include "FakeGateway.h"
#include <chrono>
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-front/FrontService.h>
#include <bcos-front/FrontServiceFactory.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/IOServicePool.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <future>
#include <thread>

using namespace bcos;
using namespace bcos::front;
using namespace bcos::front::test;

namespace bcos::test
{
namespace
{
bcos::crypto::NodeIDPtr makeNodeID(std::string const& _id)
{
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    return keyFactory->createKey(bytesConstRef((bcos::byte*)_id.data(), _id.size()));
}

// A gateway whose broadcast and point-to-point send each block for a fixed duration on whichever
// thread runs them, recording that thread. If FrontService ran the gateway send on the caller
// thread (the pre-fix coupling), the caller would block ~c_blockMs; the owned-payload async paths
// return immediately and run the send on another thread.
class BlockingGateway : public bcos::front::test::FakeGateway
{
public:
    static constexpr int c_blockMs = 400;
    std::promise<std::thread::id> m_bcastRanOn;
    std::atomic_bool m_bcastCaptured{false};
    std::promise<std::thread::id> m_p2pRanOn;
    std::atomic_bool m_p2pCaptured{false};

    task::Task<void> broadcastMessage(uint16_t, std::string_view, int, const bcos::crypto::NodeID&,
        ::ranges::any_view<bytesConstRef, ::ranges::category::forward>) override
    {
        if (!m_bcastCaptured.exchange(true))
        {
            m_bcastRanOn.set_value(std::this_thread::get_id());
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(c_blockMs));
        co_return;
    }

    task::Task<Error::Ptr> sendMessageByNodeID(const std::string&, int, bcos::crypto::NodeIDPtr,
        bcos::crypto::NodeIDPtr,
        ::ranges::any_view<bytesConstRef, ::ranges::category::forward>) override
    {
        if (!m_p2pCaptured.exchange(true))
        {
            m_p2pRanOn.set_value(std::this_thread::get_id());
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(c_blockMs));
        co_return nullptr;
    }
};

std::shared_ptr<FrontService> buildFrontServiceWith(std::shared_ptr<BlockingGateway> _gateway)
{
    auto factory = std::make_shared<FrontServiceFactory>();
    factory->setGatewayInterface(std::move(_gateway));
    // FrontServiceFactory now requires the shared IOServicePool to be injected before
    // buildFrontService (it used to create its own threads); it is also what enqueueSend's
    // drainer runs on, which is exactly what this test exercises.
    factory->setIOServicePool(std::make_shared<bcos::IOServicePool>(2, "fib185Test"));
    auto front = factory->buildFrontService("fib185.group", makeNodeID("fib185.src.nodeid"));
    front->start();
    return front;
}
}  // namespace

BOOST_FIXTURE_TEST_SUITE(FIB185AsyncSendOffCallerThread, TestPromptFixture)

BOOST_AUTO_TEST_CASE(asyncBroadcastByOwnedPayload_returnsWithoutBlockingOnGatewaySend)
{
    auto gateway = std::make_shared<BlockingGateway>();
    auto front = buildFrontServiceWith(gateway);
    // owned payload (bytesPointer): the async path forwards it by reference, no copy of the body.
    auto payload = std::make_shared<bytes>(64, 'y');

    auto callerThread = std::this_thread::get_id();
    auto startTime = utcSteadyTime();
    front->asyncBroadcastMessageByOwnedPayload(static_cast<uint16_t>(1), /*moduleID*/ 111, payload);
    auto elapsed = utcSteadyTime() - startTime;

    // Deferred to the serial send queue: the caller returns promptly even though the gateway
    // broadcast is blocked. Before the fix this ran the gateway send synchronously and blocked.
    BOOST_CHECK_LT(elapsed, static_cast<uint64_t>(BlockingGateway::c_blockMs / 2));

    // The gateway broadcast eventually runs, on a thread other than the caller.
    auto ranOn = gateway->m_bcastRanOn.get_future();
    BOOST_REQUIRE(ranOn.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    BOOST_CHECK(ranOn.get() != callerThread);

    front->stop();
}

BOOST_AUTO_TEST_CASE(asyncSendByNodeIDByOwnedPayload_returnsWithoutBlockingOnGatewaySend)
{
    auto gateway = std::make_shared<BlockingGateway>();
    auto front = buildFrontServiceWith(gateway);
    auto dstNodeID = makeNodeID("fib185.dst.nodeid");
    auto payload = std::make_shared<bytes>(64, 'z');

    auto callerThread = std::this_thread::get_id();
    auto startTime = utcSteadyTime();
    front->asyncSendMessageByNodeIDByOwnedPayload(/*moduleID*/ 111, dstNodeID, payload);
    auto elapsed = utcSteadyTime() - startTime;

    // Deferred to the serial send queue: the caller (PBFT under m_mutex, via sendViewChange /
    // sendRecoverResponse) returns promptly even though the gateway send is blocked.
    BOOST_CHECK_LT(elapsed, static_cast<uint64_t>(BlockingGateway::c_blockMs / 2));

    auto ranOn = gateway->m_p2pRanOn.get_future();
    BOOST_REQUIRE(ranOn.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    BOOST_CHECK(ranOn.get() != callerThread);

    front->stop();
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
