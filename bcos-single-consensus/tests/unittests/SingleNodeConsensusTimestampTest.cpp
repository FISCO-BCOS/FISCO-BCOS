// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// Unit tests for the single-node driver's block-timestamp rule (R2-F2) and tick pacing
// (R3-F1):
//   * the wall-clock arm floors to a whole second and advances by whole-second steps
//     (EIP-2 strict monotonicity + EthBlockHeader whole-second requirement), so chain
//     time can never outrun the wall clock;
//   * the fixed-timestamp (EEST) arm derives from fixedTimestamp + headNumber and never
//     consults the wall clock;
//   * a restart seeded from the head header (m_lastTimestamp = head_ts) cannot regress:
//     the next wall-clock timestamp is strictly greater than the stored head;
//   * the tick wait composes the wall-clock floor with [consensus] block_interval: idle
//     ticks pace at the configured interval, a tx-block drain stays bounded at one block
//     per second, and a tick that threw before a timestamp was stamped backs off by the
//     interval instead of spinning hot.

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

// Pacing (R3-F1): an idle wall-clock tick paces at the configured [consensus]
// block_interval — the interval must compose with the 1s wall floor, not be replaced by it.
BOOST_AUTO_TEST_CASE(idle_wall_clock_tick_paces_at_block_interval)
{
    // Wall clock caught up (lastTimestamp + 1000 already in the past) and no tx block
    // sealed: the operator's 5s interval paces the tick, not the 1s floor.
    BOOST_CHECK_EQUAL(SingleNodeConsensus::nextTickWaitMs(
                          /*fixedTimestamp=*/0, /*lastTimestamp=*/5'000, /*blockIntervalMs=*/5'000,
                          /*nowMs=*/9'000'000, /*sealedTxBlock=*/false),
        5'000);
    // Wall floor still pending and the larger bound: the floor wins over the interval.
    BOOST_CHECK_EQUAL(
        SingleNodeConsensus::nextTickWaitMs(
            /*fixedTimestamp=*/0, /*lastTimestamp=*/10'000'000, /*blockIntervalMs=*/300,
            /*nowMs=*/10'000'100, /*sealedTxBlock=*/false),
        900);
}

// Pacing (R3-F1): a tick that sealed a tx block stays bounded by the wall floor alone —
// block_interval never delays a drain that has work.
BOOST_AUTO_TEST_CASE(sealed_tx_block_tick_waits_only_the_wall_floor)
{
    BOOST_CHECK_EQUAL(
        SingleNodeConsensus::nextTickWaitMs(
            /*fixedTimestamp=*/0, /*lastTimestamp=*/10'000'000, /*blockIntervalMs=*/5'000,
            /*nowMs=*/10'000'500, /*sealedTxBlock=*/true),
        500);
    // Floor already reached: the next tick runs immediately (1 block/second upper bound).
    BOOST_CHECK_EQUAL(
        SingleNodeConsensus::nextTickWaitMs(
            /*fixedTimestamp=*/0, /*lastTimestamp=*/10'000'000, /*blockIntervalMs=*/5'000,
            /*nowMs=*/10'002'000, /*sealedTxBlock=*/true),
        0);
}

// Pacing (R3-F1): a tick that threw before produceBlock() could stamp m_lastTimestamp
// (still 0 — e.g. resolveInitialHead() failing while the ledger is not answering) backs
// off by block_interval instead of spinning hot.
BOOST_AUTO_TEST_CASE(thrown_tick_backs_off_by_interval_not_hot_spin)
{
    BOOST_CHECK_EQUAL(SingleNodeConsensus::nextTickWaitMs(
                          /*fixedTimestamp=*/0, /*lastTimestamp=*/0, /*blockIntervalMs=*/5'000,
                          /*nowMs=*/20'000'000, /*sealedTxBlock=*/false),
        5'000);
}

// Pacing (R3-F1): the fixed-timestamp (EEST) arm never consults the wall clock — sealed
// ticks stay unpaced, idle ticks wait exactly block_intervalMs.
BOOST_AUTO_TEST_CASE(fixed_timestamp_arm_keeps_interval_only_pacing)
{
    BOOST_CHECK_EQUAL(SingleNodeConsensus::nextTickWaitMs(
                          /*fixedTimestamp=*/100, /*lastTimestamp=*/0, /*blockIntervalMs=*/3'000,
                          /*nowMs=*/999'999'999, /*sealedTxBlock=*/true),
        0);
    BOOST_CHECK_EQUAL(SingleNodeConsensus::nextTickWaitMs(
                          /*fixedTimestamp=*/100, /*lastTimestamp=*/0, /*blockIntervalMs=*/3'000,
                          /*nowMs=*/999'999'999, /*sealedTxBlock=*/false),
        3'000);
}

BOOST_AUTO_TEST_SUITE_END()
