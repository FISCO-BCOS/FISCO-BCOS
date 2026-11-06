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
 * @brief Pins the Service-wide seq uniqueness invariant that Service::newSeq() relies on.
 * @file ServiceNewSeqUniquenessTest.cpp
 * @date 2026-09-28
 *
 * Service's pending-request table (libp2p/PendingResponse.h) is keyed by seq and scoped to the
 * whole node — a routed response can arrive on any session of this node — so seqs drawn from one
 * Service must never repeat while a request could still be in flight. These tests go RED if the
 * allocator ever loses its Service-wide monotonicity. (The predecessor of this test pinned the
 * HOST-wide invariant of the old libnetwork callback manager; when correlation moved up to
 * libp2p the allocator moved with it. Two Services sharing one Host no longer share a seq space:
 * each owns its own pending-request table, so cross-service uniqueness is not required.)
 */

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

BOOST_FIXTURE_TEST_SUITE(ServiceNewSeqUniquenessTest, TestPromptFixture)

namespace
{
std::shared_ptr<Service> newService()
{
    P2PInfo selfInfo;
    selfInfo.rawP2pID = "selfRawP2pID";
    selfInfo.p2pID = "selfP2pID";
    return std::make_shared<Service>(selfInfo);
}
}  // namespace

BOOST_AUTO_TEST_CASE(test_sequentialSeqsNeverRepeat)
{
    auto service = newService();
    std::unordered_set<uint32_t> seqs;
    for (int i = 0; i < 1000; ++i)
    {
        BOOST_TEST(seqs.insert(service->newSeq()).second);
    }
}

BOOST_AUTO_TEST_CASE(test_concurrentSeqsNeverCollide)
{
    auto service = newService();
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
                    draws[t].push_back(service->newSeq());
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
}

BOOST_AUTO_TEST_SUITE_END()
