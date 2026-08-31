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
 * @brief Regression tests for FIB-186: cap concurrent in-flight TLS handshakes so connection churn
 *        cannot starve the shared I/O pool that also delivers consensus messages.
 * @file FIB186_HandshakeAdmissionTest.cpp
 * @date 2026-07-07
 */

#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-gateway/libnetwork/ASIOInterface.h"
#include "bcos-gateway/libnetwork/Host.h"
#include "bcos-utilities/IOServicePool.h"
#include "bcos-utilities/testutils/TestPromptFixture.h"

#include <boost/test/unit_test.hpp>
using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::test;
using namespace bcos::crypto;

namespace ba = boost::asio;

BOOST_FIXTURE_TEST_SUITE(FIB186_HandshakeAdmissionTest, TestPromptFixture)

class FakeASIO_FIB186 : public bcos::gateway::ASIOInterface
{
public:
    // ASIOInterface now owns its IOServicePool and is stopped by ~IOServicePool; the
    // strandPost / stop virtuals this fake used to override no longer exist.
    FakeASIO_FIB186()
      : ASIOInterface(std::make_shared<bcos::IOServicePool>(1, "FakeASIO_FIB186"), "0.0.0.0", 0)
    {}
    ~FakeASIO_FIB186() noexcept override {}
};

// Exposes the protected handshake-admission helpers for direct testing, mirroring the FIB-184
// session-cap test harness.
class FakeHost_FIB186 : public bcos::gateway::Host
{
public:
    FakeHost_FIB186(bcos::crypto::Hash::Ptr _hash, std::shared_ptr<ASIOInterface> _asioInterface)
      : Host(_hash, _asioInterface, nullptr, nullptr)
    {
        m_run = true;
    }
    bool callTryAcquireHandshakeSlot() { return tryAcquireHandshakeSlot(); }
    void callReleaseHandshakeSlot() { releaseHandshakeSlot(); }
    std::shared_ptr<void> callAcquireHandshakeSlotGuard() { return acquireHandshakeSlotGuard(); }
};

// FIB-186: the global concurrent-handshake cap rejects once the total limit is hit. It is global,
// not per-IP: a churn attacker rotates source addresses, so a per-IP cap only raises the bar from
// one IP to a handful while risking false rejections of legitimate peers behind a shared egress IP.
BOOST_AUTO_TEST_CASE(GlobalHandshakeCapIsEnforced)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto fakeAsio = std::make_shared<FakeASIO_FIB186>();
    auto fakeHost = std::make_shared<FakeHost_FIB186>(hashImpl, fakeAsio);

    fakeHost->setMaxPendingHandshakes(2);

    BOOST_CHECK(fakeHost->callTryAcquireHandshakeSlot());
    BOOST_CHECK(fakeHost->callTryAcquireHandshakeSlot());
    // global cap reached, the next handshake is rejected
    BOOST_CHECK(!fakeHost->callTryAcquireHandshakeSlot());
    BOOST_CHECK_EQUAL(fakeHost->currentPendingHandshakes(), 2u);

    fakeHost->callReleaseHandshakeSlot();
    BOOST_CHECK(fakeHost->callTryAcquireHandshakeSlot());
    BOOST_CHECK_EQUAL(fakeHost->currentPendingHandshakes(), 2u);
}

// FIB-186: the REAL HandshakeSlotGuard the accept loop hands into serverHandshake releases the
// slot exactly once when the frame unwinds (success, failure, or abort — all three are the same
// frame destruction, which is what this drives).
BOOST_AUTO_TEST_CASE(GuardReleasesSlotExactlyOnce)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto fakeAsio = std::make_shared<FakeASIO_FIB186>();
    auto fakeHost = std::make_shared<FakeHost_FIB186>(hashImpl, fakeAsio);

    {
        // Acquiring through the real factory reserves the slot and binds it to the guard.
        auto guard = fakeHost->callAcquireHandshakeSlotGuard();
        BOOST_REQUIRE(guard);
        BOOST_CHECK_EQUAL(fakeHost->currentPendingHandshakes(), 1u);
        // Copying the guard (the coroutine frame may hold several references to it) must not
        // double-release: the slot is released on final destruction, not per copy.
        auto guardCopy = guard;
        BOOST_CHECK_EQUAL(fakeHost->currentPendingHandshakes(), 1u);
    }
    // After the last copy is destroyed — i.e. the serverHandshake frame unwound — the slot is
    // released exactly once.
    BOOST_CHECK_EQUAL(fakeHost->currentPendingHandshakes(), 0u);

    // Cap reached: the real factory returns nullptr and reserves nothing.
    fakeHost->setMaxPendingHandshakes(1);
    auto guard = fakeHost->callAcquireHandshakeSlotGuard();
    BOOST_REQUIRE(guard);
    BOOST_CHECK(!fakeHost->callAcquireHandshakeSlotGuard());
    BOOST_CHECK_EQUAL(fakeHost->currentPendingHandshakes(), 1u);
}

// FIB-186: releasing an unknown / already-drained address never underflows the global counter.
BOOST_AUTO_TEST_CASE(ReleaseNeverUnderflows)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto fakeAsio = std::make_shared<FakeASIO_FIB186>();
    auto fakeHost = std::make_shared<FakeHost_FIB186>(hashImpl, fakeAsio);

    fakeHost->callReleaseHandshakeSlot();  // release with no slot held
    BOOST_CHECK_EQUAL(fakeHost->currentPendingHandshakes(), 0u);

    BOOST_CHECK(fakeHost->callTryAcquireHandshakeSlot());
    fakeHost->callReleaseHandshakeSlot();
    fakeHost->callReleaseHandshakeSlot();  // spurious extra release
    BOOST_CHECK_EQUAL(fakeHost->currentPendingHandshakes(), 0u);
}

// FIB-186: the connection accept-rate token bucket bounds accepted connections per second. With a
// rate cap, a tight burst of accepts is throttled once the bucket drains; with the cap disabled
// (0) every accept passes. This bounds handshake CPU under churn, which the concurrency caps do
// not.
BOOST_AUTO_TEST_CASE(ConnectionRateLimitBoundsAcceptBurst)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto fakeAsio = std::make_shared<FakeASIO_FIB186>();
    auto fakeHost = std::make_shared<FakeHost_FIB186>(hashImpl, fakeAsio);

    // unlimited: every accept passes
    fakeHost->setMaxConnectionsPerSecond(0);
    for (int i = 0; i < 1000; ++i)
    {
        BOOST_CHECK(fakeHost->tryAcquireConnectionToken());
    }

    // bounded: a tight burst of 1000 accepts is throttled to roughly the bucket capacity, so far
    // fewer than 1000 succeed (loose bounds keep this independent of wall-clock timing).
    fakeHost->setMaxConnectionsPerSecond(10);
    int granted = 0;
    for (int i = 0; i < 1000; ++i)
    {
        if (fakeHost->tryAcquireConnectionToken())
        {
            ++granted;
        }
    }
    BOOST_CHECK_GE(granted, 1);
    BOOST_CHECK_LT(granted, 1000);
}

BOOST_AUTO_TEST_SUITE_END()
