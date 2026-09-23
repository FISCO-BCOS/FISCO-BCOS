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
 * @file ClSyncCoordinationTest.cpp
 * @brief Tests for the CL-driven coordination state shared between the Engine API
 *        service and the EL-mode devp2p sync loop
 */

#include "engine/bcos-engine/ClSyncCoordination.h"

#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::engine;

BOOST_AUTO_TEST_SUITE(ClSyncCoordinationTest)

// The latch is the guard against a CL disconnect reviving autonomous devp2p advance:
// once any forkchoiceUpdated was served, CL-driven mode never clears.
BOOST_AUTO_TEST_CASE(cl_latch_never_clears)
{
    engine_common::ClSyncCoordination coordination;
    BOOST_CHECK(!coordination.clDriving());
    coordination.noteForkchoiceServed();
    BOOST_CHECK(coordination.clDriving());
    // Idempotent, and nothing un-latches it.
    coordination.noteForkchoiceServed();
    BOOST_CHECK(coordination.clDriving());
}

BOOST_AUTO_TEST_CASE(backfill_target_latest_wins)
{
    engine_common::ClSyncCoordination coordination;
    BOOST_CHECK(!coordination.backfillTarget().has_value());

    coordination.requestBackfill(h256(1));
    BOOST_REQUIRE(coordination.backfillTarget().has_value());
    BOOST_CHECK_EQUAL(*coordination.backfillTarget(), h256(1));

    // A newer SYNCING answer supersedes the stale target.
    coordination.requestBackfill(h256(2));
    BOOST_REQUIRE(coordination.backfillTarget().has_value());
    BOOST_CHECK_EQUAL(*coordination.backfillTarget(), h256(2));
}

// clearBackfillTarget is conditional on the target still being the served hash: a
// backfill that completed must not clear a newer target the CL posted mid-download.
BOOST_AUTO_TEST_CASE(backfill_clear_is_conditional)
{
    engine_common::ClSyncCoordination coordination;
    coordination.requestBackfill(h256(1));
    coordination.requestBackfill(h256(2));

    coordination.clearBackfillTarget(h256(1));  // stale: must not drop the newer target
    BOOST_REQUIRE(coordination.backfillTarget().has_value());
    BOOST_CHECK_EQUAL(*coordination.backfillTarget(), h256(2));

    coordination.clearBackfillTarget(h256(2));
    BOOST_CHECK(!coordination.backfillTarget().has_value());
}

BOOST_AUTO_TEST_SUITE_END()
