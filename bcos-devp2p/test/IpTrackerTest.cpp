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
 * @file IpTrackerTest.cpp
 * @brief External-address prediction: one vote per reporter, minimum agreement, sliding
 *        window, and no state reset under a flood of distinct statements.
 * @date 2026/11/3
 */
#include <bcos-devp2p/disc/IpTracker.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::devp2p::disc;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(IpTrackerTest)

namespace
{
IpTracker::Address reporter(uint64_t _n)
{
    return {100, static_cast<uint8_t>((_n >> 16) & 0xff), static_cast<uint8_t>((_n >> 8) & 0xff),
        static_cast<uint8_t>(_n & 0xff)};
}
}  // namespace

BOOST_AUTO_TEST_CASE(minimumAgreementAndOneVotePerReporter)
{
    IpTracker tracker(10, std::chrono::minutes(5));
    auto const now = IpTracker::Clock::now();
    IpTracker::Address const a{203, 0, 113, 7};
    // One reporter repeating itself never counts more than once.
    for (int i = 0; i < 50; ++i)
    {
        tracker.addStatement(reporter(1), a, now);
    }
    BOOST_CHECK(!tracker.predict(now).has_value());
    BOOST_CHECK_EQUAL(tracker.size(), 1u);
    // Nine distinct reporters: still below the threshold; the tenth predicts.
    for (uint64_t i = 2; i <= 9; ++i)
    {
        tracker.addStatement(reporter(i), a, now);
    }
    BOOST_CHECK(!tracker.predict(now).has_value());
    tracker.addStatement(reporter(10), a, now);
    BOOST_REQUIRE(tracker.predict(now).has_value());
    BOOST_CHECK(*tracker.predict(now) == a);
}

BOOST_AUTO_TEST_CASE(oneHostCannotOutvoteTheNetwork)
{
    IpTracker tracker(10, std::chrono::minutes(5));
    auto const now = IpTracker::Clock::now();
    IpTracker::Address const forged{198, 51, 100, 9};
    // 200 minted node ids behind one public /24 are one vote.
    for (int i = 0; i < 200; ++i)
    {
        tracker.addStatement(
            IpTracker::reporterKey({203, 0, 113, static_cast<uint8_t>(i)}), forged, now);
    }
    BOOST_CHECK_EQUAL(tracker.size(), 1u);
    BOOST_CHECK(!tracker.predict(now).has_value());
    // A private network legitimately sits in one /24: its hosts keep separate votes.
    BOOST_CHECK(
        IpTracker::reporterKey({192, 168, 1, 7}) != IpTracker::reporterKey({192, 168, 1, 8}));
    BOOST_CHECK(IpTracker::reporterKey({127, 0, 0, 1}) != IpTracker::reporterKey({127, 0, 0, 2}));
}

BOOST_AUTO_TEST_CASE(floodOfDistinctAddressesDoesNotResetOrFlip)
{
    IpTracker tracker(10, std::chrono::minutes(5));
    auto const now = IpTracker::Clock::now();
    IpTracker::Address const honest{203, 0, 113, 7};
    for (uint64_t i = 1; i <= 10; ++i)
    {
        tracker.addStatement(reporter(i), honest, now);
    }
    BOOST_CHECK(*tracker.predict(now) == honest);
    // 200 reporters each claiming a different address (the old "clear on overflow"
    // path would have wiped the honest votes): nothing reaches 10, the prediction holds.
    for (uint64_t i = 100; i < 300; ++i)
    {
        IpTracker::Address other{198, 51, 100, static_cast<uint8_t>(i & 0xff)};
        other[2] = static_cast<uint8_t>(100 + (i >> 8));
        tracker.addStatement(reporter(i), other, now);
    }
    BOOST_REQUIRE(tracker.predict(now).has_value());
    BOOST_CHECK(*tracker.predict(now) == honest);
    // A tie between two addresses at the threshold predicts nothing.
    IpTracker tie(2, std::chrono::minutes(5));
    tie.addStatement(reporter(1), honest, now);
    tie.addStatement(reporter(2), honest, now);
    tie.addStatement(reporter(3), {198, 51, 100, 1}, now);
    tie.addStatement(reporter(4), {198, 51, 100, 1}, now);
    BOOST_CHECK(!tie.predict(now).has_value());
}

BOOST_AUTO_TEST_CASE(windowEvictionAndCap)
{
    IpTracker tracker(3, std::chrono::minutes(5), 5);
    auto const start = IpTracker::Clock::now();
    IpTracker::Address const a{203, 0, 113, 7};
    tracker.addStatement(reporter(1), a, start);
    tracker.addStatement(reporter(2), a, start);
    tracker.addStatement(reporter(3), a, start + std::chrono::minutes(4));
    BOOST_CHECK(tracker.predict(start + std::chrono::minutes(4)).has_value());
    // Six minutes in, the first two statements expired: only one remains.
    BOOST_CHECK(!tracker.predict(start + std::chrono::minutes(6)).has_value());
    BOOST_CHECK_EQUAL(tracker.size(), 1u);
    // Capacity 5: the sixth statement evicts the oldest one, not the table.
    auto const later = start + std::chrono::minutes(7);
    for (uint64_t i = 10; i < 16; ++i)
    {
        tracker.addStatement(reporter(i), a, later + std::chrono::seconds(i));
    }
    BOOST_CHECK_EQUAL(tracker.size(), 5u);
    BOOST_REQUIRE(tracker.predict(later + std::chrono::seconds(20)).has_value());
    BOOST_CHECK(*tracker.predict(later + std::chrono::seconds(20)) == a);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
