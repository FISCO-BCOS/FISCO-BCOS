/**
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @brief Regression test for FIB-135: rPBFT round-robin rotation amplifies slow-
 *        leader stalls because the view-change timeout grows unboundedly via
 *        exponential backoff. The fix caps the max view-change cycle multiplier
 *        so that backoff stays within a bounded multiple of consensus_timeout.
 * @file FIB135_SlowValidatorThroughputTest.cpp
 * @author: claude
 * @date 2026-05-07
 */
#include "bcos-pbft/pbft/engine/PBFTTimer.h"
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>
#include <cmath>

using namespace bcos;
using namespace bcos::consensus;

namespace bcos::test
{

BOOST_FIXTURE_TEST_SUITE(FIB135Test, TestPromptFixture)

// Test that PBFTTimer's view-change exponential backoff is bounded.
//
// FIB-135: in rPBFT, when a slow/isolated leader is repeatedly scheduled,
// the view-change timeout doubles each cycle. An overly large c_maxChangeCycle
// causes cumulative backoff to grow to c_maxChangeCycle * consensus_timeout * base^N,
// which cascades into very long stalls (>100s) when healthy nodes become leader.
//
// After fix: c_maxChangeCycle is reduced so that the max view-change backoff
// stays within a reasonable bound (< c_maxBackoffMultiple * base_timeout).
BOOST_AUTO_TEST_CASE(view_change_backoff_is_bounded)
{
    const int64_t baseTimeout = 3000;  // ms - typical consensus_timeout
    auto pbftTimer = std::make_shared<PBFTTimer>(baseTimeout, "testPBFTTimer");

    // Advance to the maximum change cycle
    // The timer internally clamps to c_maxChangeCycle
    for (int i = 0; i < 100; i++)
    {
        pbftTimer->incChangeCycle(1);
    }

    uint64_t maxCycle = pbftTimer->changeCycle();

    // FIB-135: the max cycle must be bounded to keep stalls manageable.
    // With base=1.5, maxCycle <= 3 gives at most 3.375x base timeout (~10s for 3s base).
    // Even allowing up to 4 gives 5.0625x (~15s). Anything > 5 risks seconds-long stalls.
    // The fix reduces c_maxChangeCycle from 10 (which gives 57.7x) to <= 4.
    BOOST_CHECK_MESSAGE(maxCycle <= 4,
        "FIB-135: PBFTTimer c_maxChangeCycle must be bounded to prevent excessive "
        "view-change backoff; maxCycle=" +
            std::to_string(maxCycle) + " (expected <= 4)");

    // Also verify the absolute timeout remains bounded
    // Max adjusted timeout = baseTimeout * pow(1.5, maxCycle)
    double maxMultiplier = std::pow(1.5, static_cast<double>(maxCycle));
    double maxTimeoutMs = static_cast<double>(baseTimeout) * maxMultiplier;

    // Should be < 10x the base timeout (i.e. < 30 seconds for 3s base)
    BOOST_CHECK_MESSAGE(maxTimeoutMs < baseTimeout * 10.0,
        "FIB-135: max view-change timeout must be < 10x base consensus_timeout; "
        "max=" +
            std::to_string(static_cast<int64_t>(maxTimeoutMs)) +
            "ms, "
            "base=" +
            std::to_string(baseTimeout) + "ms");
}

// Test that resetting the change cycle correctly resets the backoff
BOOST_AUTO_TEST_CASE(change_cycle_resets_to_zero)
{
    auto pbftTimer = std::make_shared<PBFTTimer>(3000, "testReset");
    pbftTimer->incChangeCycle(5);
    BOOST_CHECK_GT(pbftTimer->changeCycle(), 0U);
    pbftTimer->resetChangeCycle();
    BOOST_CHECK_EQUAL(pbftTimer->changeCycle(), 0U);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
