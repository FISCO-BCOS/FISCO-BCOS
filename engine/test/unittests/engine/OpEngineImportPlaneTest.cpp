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
 * @file OpEngineImportPlaneTest.cpp
 * @brief Artifact-level pins for the import plane and the merge target.
 */

// Two artifact-level pins, both about the substrate the engine's import/canonicalize
// path hands around rather than about service-level status codes:
//   - the flat materialized for a block must contain that block's own writes, i.e. it is
//     the block's post-state, not "the committed layers plus the ancestor seeds"
//     (review F1; the service-level consequence is already pinned by
//     OpEngineImportFcuTest/ChainedImportMatchesCanonicalParentState);
//   - mergeToBackends must commit the layer it is GIVEN even while a pending view sits
//     at the FIFO front -- mergeView would have committed that pending layer instead,
//     so a tip-advance row written through the wrong primitive lands on the wrong plane
//     (review F6). Nothing else in the tree drives pushView together with mergeToBackends.
#include "support/OpEngineKarstTestHarness.h"

#include <boost/test/unit_test.hpp>

#include <string>

using namespace bcos;
using namespace bcos::engine;
using namespace op_engine_parity_test;

namespace
{
struct ImportArtifacts
{
    bcos::protocol::BlockHeader::Ptr header;
    std::shared_ptr<void> delta;
    std::shared_ptr<void> flat;
};

/// Drive one import through the real OpScheduler; @p plane is the parent's materialized
/// flat (nullptr = the parent is the canonical tip).
ImportArtifacts runImport(
    ImportSchedulerFixture& f, bcos::protocol::Block::Ptr block, std::shared_ptr<void> const& plane)
{
    ImportArtifacts out;
    f.scheduler->importExecute(block, {}, plane,
        [&](Error::Ptr error, bcos::protocol::BlockHeader::Ptr header, std::shared_ptr<void> delta,
            std::shared_ptr<void> flat) {
            if (error)
            {
                BOOST_FAIL(std::string("import failed: ") + error->errorMessage());
            }
            out.header = std::move(header);
            out.delta = std::move(delta);
            out.flat = std::move(flat);
        });
    return out;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OpEngineImportPlaneTest)

/// The deposit executes, so the depositor's account rows are written into the view's
/// fresh mutable layer. Both artifacts are asked for exactly those rows: the delta is
/// where the execution wrote them, and the flat -- materialized for the NEXT block to
/// execute against -- must carry them too, otherwise a chained import would execute on a
/// plane missing its parent's post-state.
BOOST_AUTO_TEST_CASE(FlatCapturesTheBlocksOwnWrites)
{
    ImportSchedulerFixture f;
    auto block1 = f.depositBlock(1, bcos::h256{}, 1'000'000, "plane-b1");
    auto artifacts = runImport(f, block1, nullptr);
    BOOST_REQUIRE(artifacts.delta != nullptr);
    BOOST_REQUIRE(artifacts.flat != nullptr);

    auto& delta = *std::static_pointer_cast<MutableStorage>(artifacts.delta);
    auto& flat = *std::static_pointer_cast<MutableStorage>(artifacts.flat);

    // The deposit's from-address (kDepositFrom, 0xdead..0001) as the executor's state
    // keys render it: the account address without the 0x prefix under the /apps/ table.
    auto accountKey = [](std::string_view field) {
        return StateKey{
            std::string("/apps/") + std::string("deaddeaddeaddeaddeaddeaddeaddeaddead0001"),
            std::string(field)};
    };

    BOOST_CHECK_MESSAGE(
        bcos::task::syncWait(bcos::storage2::readOne(delta, accountKey("nonce"))).has_value(),
        "the executed block did not write the depositor's nonce into its delta");
    BOOST_CHECK_MESSAGE(
        bcos::task::syncWait(bcos::storage2::readOne(delta, accountKey("balance"))).has_value(),
        "the executed block did not write the depositor's balance into its delta");
    BOOST_CHECK_MESSAGE(
        bcos::task::syncWait(bcos::storage2::readOne(flat, accountKey("nonce"))).has_value(),
        "the materialized flat is missing the block's own nonce write");
    BOOST_CHECK_MESSAGE(
        bcos::task::syncWait(bcos::storage2::readOne(flat, accountKey("balance"))).has_value(),
        "the materialized flat is missing the block's own balance write");
}

/// mergeToBackends takes the layer it is handed; the pending layer queued via pushView
/// stays pending. This is the FIFO-immunity that separates it from mergeView, which pops
/// and commits the deque front instead.
BOOST_AUTO_TEST_CASE(MergeToBackendsIsFifoImmune)
{
    ImportSchedulerFixture f;
    auto const pendingKey = StateKey{"s_import_plane_marker", std::string{"pending"}};
    auto const givenKey = StateKey{"s_import_plane_marker", std::string{"given"}};

    {
        auto view = f.storage.fork();
        view.newMutable();
        bcos::storage::Entry entry;
        entry.set("pending");
        bcos::task::syncWait(bcos::storage2::writeOne(view, pendingKey, std::move(entry)));
        f.storage.pushView(std::move(view));  // queued at the FIFO front, must stay pending
    }
    {
        auto layer = std::make_shared<MutableStorage>();
        bcos::storage::Entry entry;
        entry.set("given");
        bcos::task::syncWait(bcos::storage2::writeOne(*layer, givenKey, std::move(entry)));
        bcos::task::syncWait(f.storage.mergeToBackends(*layer));
    }

    auto committed = f.storage.forkCommitted();
    BOOST_CHECK_MESSAGE(
        !bcos::task::syncWait(bcos::storage2::readOne(committed, pendingKey)).has_value(),
        "mergeToBackends committed the pending layer instead of the layer it was given");
    BOOST_CHECK_MESSAGE(
        bcos::task::syncWait(bcos::storage2::readOne(committed, givenKey)).has_value(),
        "mergeToBackends did not commit the layer it was given");
}

BOOST_AUTO_TEST_SUITE_END()
