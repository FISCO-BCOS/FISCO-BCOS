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
 * @brief Pins the host-wide seq uniqueness invariant that Host::newSeq() relies on.
 * @file HostNewSeqUniquenessTest.cpp
 * @date 2026-09-14
 *
 * SessionCallbackManager is shared by every session of one Host, and a routed response can be
 * claimed on a different session than the request went out on, so response-matching seqs must be
 * unique host-wide — a per-session (or per-Service) counter would collide in the shared callback
 * map. These tests go RED if the allocator is ever moved back to a per-session/per-Service scope.
 */

#include "bcos-gateway/libnetwork/Host.h"
#include "bcos-gateway/libp2p/Service.h"
#include "bcos-utilities/testutils/TestPromptFixture.h"
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <thread>
#include <unordered_set>
#include <vector>

using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::test;

BOOST_FIXTURE_TEST_SUITE(HostNewSeqUniquenessTest, TestPromptFixture)

BOOST_AUTO_TEST_CASE(test_sequentialSeqsNeverRepeat)
{
    auto host = std::make_shared<P2PHost>(nullptr, nullptr, nullptr);
    std::unordered_set<uint32_t> seqs;
    for (int i = 0; i < 1000; ++i)
    {
        BOOST_TEST(seqs.insert(host->newSeq()).second);
    }
    host->stop();
}

BOOST_AUTO_TEST_CASE(test_concurrentSeqsNeverCollide)
{
    auto host = std::make_shared<P2PHost>(nullptr, nullptr, nullptr);
    constexpr size_t kThreads = 8;
    constexpr size_t kDrawsPerThread = 500;
    std::atomic<bool> start{false};
    std::vector<std::vector<uint32_t>> draws(kThreads);
    {
        std::vector<std::thread> threads;
        for (size_t t = 0; t < kThreads; ++t)
        {
            threads.emplace_back([&, t] {
                while (!start.load(std::memory_order_acquire))
                {
                }
                draws[t].reserve(kDrawsPerThread);
                for (size_t i = 0; i < kDrawsPerThread; ++i)
                {
                    draws[t].push_back(host->newSeq());
                }
            });
        }
        start.store(true, std::memory_order_release);
        for (auto& thread : threads)
        {
            thread.join();
        }
    }
    std::unordered_set<uint32_t> seqs;
    for (auto const& perThread : draws)
    {
        for (auto seq : perThread)
        {
            BOOST_TEST(seqs.insert(seq).second);
        }
    }
    BOOST_TEST(seqs.size() == kThreads * kDrawsPerThread);
    host->stop();
}

// Two Services sharing one Host (each Service serves its own sessions) must still draw from the
// same host-wide sequence — this is the topology a routed response relies on.
BOOST_AUTO_TEST_CASE(test_seqUniqueAcrossServicesSharingOneHost)
{
    auto host = std::make_shared<P2PHost>(nullptr, nullptr, nullptr);
    P2PInfo selfInfo;
    selfInfo.rawP2pID = "selfRawP2pID";
    selfInfo.p2pID = "selfP2pID";
    auto service1 = std::make_shared<Service>(selfInfo);
    auto service2 = std::make_shared<Service>(selfInfo);
    service1->setHost(host);
    service2->setHost(host);

    std::unordered_set<uint32_t> seqs;
    for (int i = 0; i < 100; ++i)
    {
        BOOST_TEST(seqs.insert(service1->newSeq()).second);
        BOOST_TEST(seqs.insert(service2->newSeq()).second);
    }
    host->stop();
}

BOOST_AUTO_TEST_SUITE_END()
