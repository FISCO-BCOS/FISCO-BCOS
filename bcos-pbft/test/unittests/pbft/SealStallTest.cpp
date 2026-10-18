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
 * @brief SealSkipped / SealResumed: one line per reason change of each source
 * @file SealStallTest.cpp
 */
#include "bcos-pbft/pbft/utilities/SealStall.h"
#include <bcos-framework/testutils/LogCapture.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::consensus;

namespace bcos::test
{
BOOST_FIXTURE_TEST_SUITE(SealStallTest, TestPromptFixture)

namespace
{
size_t count(std::string const& _text, std::string const& _needle)
{
    size_t n = 0;
    for (auto pos = _text.find(_needle); pos != std::string::npos;
        pos = _text.find(_needle, pos + 1))
    {
        ++n;
    }
    return n;
}
}  // namespace

// The sealer and PBFT report a stall in turn; each keeps its own last reason, so the interleaving
// prints one line per source instead of one per call.
BOOST_AUTO_TEST_CASE(interleavedSourcesPrintOncePerReason)
{
    noteSealResumed(0);  // the state is per process: start from no stall

    LogCapture capture;
    for (int i = 0; i < 3; ++i)
    {
        noteSealSkipped(SealSkipReason::NoTxs, 10, -1);
        noteSealSkipped(SealSkipReason::NotLeader, 10, -1);
    }
    noteSealResumed(10);
    noteSealResumed(11);
    auto text = capture.text();
    BOOST_CHECK_MESSAGE(count(text, "SealSkipped,reason=no_txs") == 1, text);
    BOOST_CHECK_MESSAGE(count(text, "SealSkipped,reason=not_leader") == 1, text);
    BOOST_CHECK_MESSAGE(count(text, "SealResumed") == 1, text);
}

// A new reason from the same source is a state change and prints again.
BOOST_AUTO_TEST_CASE(reasonChangeWithinASourcePrints)
{
    noteSealResumed(0);

    LogCapture capture;
    noteSealSkipped(SealSkipReason::NotLeader, 10, -1);
    noteSealSkipped(SealSkipReason::WaitReseal, 10, 12);
    noteSealSkipped(SealSkipReason::WaitReseal, 10, 12);
    noteSealResumed(10);
    auto text = capture.text();
    BOOST_CHECK_MESSAGE(count(text, "SealSkipped,reason=not_leader") == 1, text);
    BOOST_CHECK_MESSAGE(count(text, "SealSkipped,reason=wait_reseal") == 1, text);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
