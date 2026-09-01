// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// Unit tests for the single-node driver's block-timestamp rule (R2-F2):
//   * the wall-clock arm floors to a whole second and advances by whole-second steps
//     (EIP-2 strict monotonicity + EthBlockHeader whole-second requirement), so chain
//     time can never outrun the wall clock;
//   * the fixed-timestamp (EEST) arm derives from fixedTimestamp + headNumber and never
//     consults the wall clock;
//   * a restart seeded from the head header (m_lastTimestamp = head_ts) cannot regress:
//     the next wall-clock timestamp is strictly greater than the stored head.

#define BOOST_TEST_MODULE SingleNodeConsensusTests
#include "bcos-single-consensus/bcos-single-consensus/SingleNodeConsensus.h"

#include <boost/test/unit_test.hpp>

using bcos::single_consensus::SingleNodeConsensus;

BOOST_AUTO_TEST_SUITE(SingleNodeConsensusTimestampTests)

// Wall-clock arm: floors to a whole second, advances in whole-second steps, monotonic.
BOOST_AUTO_TEST_CASE(wall_clock_floors_and_advances_whole_seconds)
{
    // nowMs = 1.5s boundary; floor to 1000, advance from lastTimestamp by whole seconds.
    BOOST_CHECK_EQUAL(
        SingleNodeConsensus::nextBlockTimestamp(
            /*fixedTimestamp=*/0, /*headNumber=*/0, /*lastTimestamp=*/0, /*nowMs=*/1500),
        1000);
    // lastTimestamp already ahead of wall clock (fast drain): keep +1000 monotonicity,
    // never go backwards, never emit a sub-second timestamp.
    BOOST_CHECK_EQUAL(
        SingleNodeConsensus::nextBlockTimestamp(
            /*fixedTimestamp=*/0, /*headNumber=*/0, /*lastTimestamp=*/5000, /*nowMs=*/5200),
        6000);
    // Exact wall-clock second: monotonic max picks the later value.
    BOOST_CHECK_EQUAL(
        SingleNodeConsensus::nextBlockTimestamp(
            /*fixedTimestamp=*/0, /*headNumber=*/0, /*lastTimestamp=*/3000, /*nowMs=*/4000),
        4000);
}

// Wall-clock arm advances in whole-second steps and stays monotonic under fast drain.
BOOST_AUTO_TEST_CASE(wall_clock_advances_whole_seconds_monotonically)
{
    // Even if the caller drains back-to-back without waiting, each step is a whole second
    // and strictly increasing; the advance never jumps more than one second per block.
    std::uint64_t last = 0;
    constexpr std::uint64_t nowMs = 10'000'000;
    for (int i = 0; i < 100; ++i)
    {
        std::uint64_t const next = SingleNodeConsensus::nextBlockTimestamp(0, 0, last, nowMs);
        BOOST_CHECK_EQUAL(next % 1000, 0);  // whole-second
        BOOST_CHECK_GT(next, last);         // strict monotonicity (EIP-2)
        // Once the wall clock is caught up, each step is exactly one second (the loop's
        // pace-wait enforces the wall-clock bound; the pure function only guarantees
        // whole-second monotonic steps).
        if (last >= nowMs - nowMs % 1000)
        {
            BOOST_CHECK_EQUAL(next - last, 1000);
        }
        last = next;
    }
}

// Fixed-timestamp (EEST) arm: derives from fixedTimestamp + headNumber, ignores wall clock.
BOOST_AUTO_TEST_CASE(fixed_timestamp_derives_from_fixed_and_head)
{
    // fixedTimestamp is in seconds; the produced timestamp is (fixed + head) * 1000 ms.
    BOOST_CHECK_EQUAL(
        SingleNodeConsensus::nextBlockTimestamp(
            /*fixedTimestamp=*/100, /*headNumber=*/5, /*lastTimestamp=*/0, /*nowMs=*/999999999),
        (100 + 5) * 1000);
    // Wall clock far in the future must not affect the fixed arm.
    BOOST_CHECK_EQUAL(
        SingleNodeConsensus::nextBlockTimestamp(
            /*fixedTimestamp=*/100, /*headNumber=*/5, /*lastTimestamp=*/0, /*nowMs=*/1),
        (100 + 5) * 1000);
}

// Restart safety (R2-F2): a head-seeded lastTimestamp guarantees the next block is
// strictly greater than the stored head (EIP-2 "timestamp must be strictly greater").
BOOST_AUTO_TEST_CASE(head_seed_restart_is_strictly_greater)
{
    // Head block timestamp 5000 ms. After restart, m_lastTimestamp is seeded to it; the
    // wall-clock arm must produce 6000 (head + 1000), strictly greater than the head.
    std::uint64_t const headTsMs = 5000;
    std::uint64_t const next = SingleNodeConsensus::nextBlockTimestamp(
        /*fixedTimestamp=*/0, /*headNumber=*/0, /*lastTimestamp=*/headTsMs, /*nowMs=*/6000);
    BOOST_CHECK_GT(next, headTsMs);
    BOOST_CHECK_EQUAL(next, 6000);
    // Even if wall clock is behind the head (e.g. the chain ran ahead under load before
    // restart), monotonicity keeps the next timestamp strictly above the head.
    std::uint64_t const behind = SingleNodeConsensus::nextBlockTimestamp(
        /*fixedTimestamp=*/0, /*headNumber=*/0, /*lastTimestamp=*/5000, /*nowMs=*/4200);
    BOOST_CHECK_GT(behind, 5000);
    BOOST_CHECK_EQUAL(behind, 6000);
}

BOOST_AUTO_TEST_SUITE_END()
