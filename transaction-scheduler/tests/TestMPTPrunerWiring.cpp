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
 * @file TestMPTPrunerWiring.cpp
 * @brief End-to-end wiring of the MPT pruner into the BaselineScheduler commit path, over the
 *        PRODUCTION persistence stack (FullChainFixture: real RocksDB, real Ledger, real
 *        prewrite/merge). A live MPTPruner (window N=2) replaces the probe observer via
 *        setMPTCommitObserver and blocks are driven through executeBlock+commitBlock:
 *          (a) the pruner keeps ALL state in memory — no /sys/mpt_prune_* metadata row ever
 *              lands on the backend;
 *          (b) past the window the committed "/mpt/" node-row count plateaus (bounded);
 *          (c) roots inside [head-N, head] keep their nodes, older roots are deleted;
 *          (d) a fresh pruner over the same backend (the restart path) REBUILDS the in-memory
 *              counts from the window's state roots at init and keeps deleting on new commits;
 *          (e) enabling pruning on a chain whose MPT built blocks WITHOUT a pruner needs no
 *              guard or seeding — init rebuilds over whatever history is on disk.
 *        Deletions land synchronously inside commitBlock (coPreparePruneRows' batch), so every
 *        assertion below runs against the committed state with no worker to drain.
 */
#include "FullChainFixture.h"
#include "bcos-ledger/GenesisStateRoot.h"
#include "bcos-ledger/mpt/MPTPruner.h"

#include <boost/test/unit_test.hpp>
#include <utility>
namespace
{
using namespace bcos;
using namespace bcos::test::fullchain;
namespace mpt = bcos::ledger::mpt;

constexpr std::string_view c_mptFlagName = "feature_mpt_state_root";
constexpr int64_t c_pruneWindow = 2;

/// The committed-state backend type: MultiLayerStorage::latestBackend() is the checkpoint
/// storage's OPENED handle (RocksDBStorage2) — what the pruner and the production
/// initializer (decltype over the same expression) are parameterized on.
using FCBackend = std::remove_cvref_t<decltype(std::declval<FCMultiLayerStorage&>().latestBackend())>;
using FCPruner = mpt::MPTPruner<FCBackend>;

/// The production init lookup (Initializer.cpp): the committed header's stateRoot, nullopt
/// when the block is not on chain.
FCPruner::StateRootLookup stateRootLookup(std::shared_ptr<bcos::ledger::Ledger> const& ledger)
{
    return [ledger](protocol::BlockNumber number) -> task::Task<std::optional<h256>> {
        auto block = co_await ledger::getBlockData(*ledger, number, ledger::HEADER);
        co_return block ? std::optional<h256>{block->blockHeader()->stateRoot()} : std::nullopt;
    };
}

bool nodeRowInBackend(FCBackend& backend, h256 const& hash)
{
    return task::syncWait(storage2::existsOne(backend, bcos::ledger::mptNodeStateKey(hash)));
}

/// Rows under any "/sys/mpt_prune_*" table — the in-memory pruner must never write one.
size_t pruneMetadataRowCount(FCBackend& backend)
{
    return task::syncWait([](FCBackend& backend) -> task::Task<size_t> {
        size_t count = 0;
        auto iterator = co_await storage2::range(backend);
        while (auto item = co_await iterator.next())
        {
            if (executor_v1::StateKeyView{std::get<0>(*item)}.m_table.find("mpt_prune") !=
                std::string_view::npos)
            {
                ++count;
            }
        }
        co_return count;
    }(backend));
}

BOOST_AUTO_TEST_SUITE(MPTPrunerWiringSuite)

BOOST_AUTO_TEST_CASE(prunerWiredIntoCommitPath)
{
    FullChainFixture fixture{"mpt_pruner_wiring"};
    fixture.buildGenesis(FullChainFixture::baseGenesis());
    // Activation at block 1: block 1 itself stays on the legacy XOR root (strictly-greater
    // rule), blocks >= 2 are MPT blocks and fire the pruner.
    fixture.enableFeatureFromBlock(c_mptFlagName, 1);

    auto& backend = fixture.m_multiLayerStorage.latestBackend();
    auto pruner = std::make_shared<FCPruner>(backend, c_pruneWindow);
    // Fresh chain at the genesis block: MPT is not active yet, so init starts empty — the
    // first MPT block's full build seeds the counts through the ordinary delta path.
    task::syncWait(pruner->init(0, stateRootLookup(fixture.m_ledger), /*sweepGarbage=*/false));
    BOOST_CHECK_EQUAL(pruner->trackedCount(), 0U);
    fixture.m_baselineScheduler.setMPTCommitObserver(pruner);

    // Every block changes account A's balance, so every MPT block produces a fresh state root
    // and obsoletes the previous one — a new trie version per block.
    auto const addressA = FullChainFixture::makeAddress(0xA5);
    constexpr protocol::BlockNumber c_head = 8;
    std::map<protocol::BlockNumber, h256> roots;
    std::map<protocol::BlockNumber, size_t> nodeCounts;
    for (protocol::BlockNumber number = 1; number <= c_head; ++number)
    {
        fixture.planBlock(
            number, {FullChainFixture::balanceRow(addressA, std::to_string(number * 100))});
        auto header = fixture.executeOneBlock(number);
        fixture.commitOneBlock(header);

        if (number >= 2)  // MPT blocks only; block 1 is XOR and fires no observer
        {
            // (a) The in-memory count of the just-committed root: exactly one reference, no
            // deadline — and no metadata row may have landed with any block.
            BOOST_CHECK(pruner->countOf(header->stateRoot()) == std::optional<uint64_t>{1});
            BOOST_CHECK(!pruner->deadlineOf(header->stateRoot()).has_value());
            BOOST_CHECK_EQUAL(pruneMetadataRowCount(backend), 0U);
        }
        roots[number] = header->stateRoot();
        nodeCounts[number] = fixture.backendNodeCount();
    }

    // (b) Bounded, converged node count: deletions land at the commit of block 2+N+1 = 5;
    // from then on each block adds one trie version and deletes the one that fell out of the
    // window, so the count plateaus.
    BOOST_REQUIRE_EQUAL(nodeCounts[5], nodeCounts[6]);
    BOOST_REQUIRE_EQUAL(nodeCounts[6], nodeCounts[7]);
    BOOST_REQUIRE_EQUAL(nodeCounts[7], nodeCounts[8]);
    BOOST_CHECK_LE(nodeCounts[8], nodeCounts[4]);  // never exceeds the pre-deletion level
    BOOST_CHECK_GT(nodeCounts[8], 0);

    // (c) Window guarantee with N=2 at head 8: roots of blocks 6..8 (head-N .. head) keep
    // their nodes; the root of block r is deleted when block r+1+N commits, so roots 2..5
    // are all gone.
    for (protocol::BlockNumber number = 6; number <= 8; ++number)
    {
        BOOST_CHECK_MESSAGE(nodeRowInBackend(backend, roots[number]),
            "in-window root of block " + std::to_string(number) + " was pruned");
    }
    for (protocol::BlockNumber number = 2; number <= 5; ++number)
    {
        BOOST_CHECK_MESSAGE(!nodeRowInBackend(backend, roots[number]),
            "out-of-window root of block " + std::to_string(number) + " still on disk");
    }
    // The root of block 2 was obsoleted at block 3 and deleted at block 5 — its in-memory
    // entry is erased with the deletion; a still-live root reads count 1, no deadline.
    BOOST_CHECK(!pruner->countOf(roots[2]).has_value());
    BOOST_CHECK(pruner->countOf(roots[8]) == std::optional<uint64_t>{1});

    // (d) Restart path: a fresh pruner over the same backend rebuilds the counts from the
    // window's state roots — no guard, no replay: every deletion already landed with its
    // block's commit, and the rebuilt state matches the running pruner's exactly.
    auto pruner2 = std::make_shared<FCPruner>(backend, c_pruneWindow);
    task::syncWait(pruner2->init(c_head, stateRootLookup(fixture.m_ledger), /*sweepGarbage=*/false));
    BOOST_CHECK_EQUAL(pruner2->watermark(), c_head);
    BOOST_CHECK_EQUAL(pruner2->trackedCount(), pruner->trackedCount());
    BOOST_CHECK_EQUAL(pruner2->pendingCount(), pruner->pendingCount());
    BOOST_CHECK_EQUAL(fixture.backendNodeCount(), nodeCounts[8]);

    fixture.m_baselineScheduler.setMPTCommitObserver(pruner2);
    constexpr protocol::BlockNumber c_next = c_head + 1;
    fixture.planBlock(
        c_next, {FullChainFixture::balanceRow(addressA, std::to_string(c_next * 100))});
    auto header9 = fixture.executeOneBlock(c_next);
    fixture.commitOneBlock(header9);

    roots[c_next] = header9->stateRoot();
    // Block 9 shifts the window to [7, 9]: the root of block 6 (deleted at 6+1+N = 9) falls.
    BOOST_CHECK(!nodeRowInBackend(backend, roots[6]));
    for (protocol::BlockNumber number = 7; number <= c_next; ++number)
    {
        BOOST_CHECK_MESSAGE(nodeRowInBackend(backend, roots[number]),
            "in-window root of block " + std::to_string(number) + " was pruned after restart");
    }
    BOOST_CHECK_EQUAL(fixture.backendNodeCount(), nodeCounts[8]);  // plateau held
    BOOST_CHECK_EQUAL(pruneMetadataRowCount(backend), 0U);
}

BOOST_AUTO_TEST_CASE(midChainEnableRebuildsFromStateRoots)
{
    // Blocks committed while NO pruner was wired are no obstacle anymore: init rebuilds the
    // counts from the state roots on disk — no seeding, no guard. The chain then prunes
    // exactly as if it had run with a pruner from the start.
    FullChainFixture fixture{"mpt_pruner_midchain_rebuild"};
    fixture.buildGenesis(FullChainFixture::baseGenesis());
    fixture.enableFeatureFromBlock(c_mptFlagName, 1);

    auto const addressA = FullChainFixture::makeAddress(0xA6);
    std::map<protocol::BlockNumber, h256> roots;
    for (protocol::BlockNumber number = 1; number <= 3; ++number)
    {
        fixture.planBlock(
            number, {FullChainFixture::balanceRow(addressA, std::to_string(number * 100))});
        roots[number] = fixture.runBlock(number)->stateRoot();
    }

    auto& backend = fixture.m_multiLayerStorage.latestBackend();
    // feature_mpt_state_root activated at block 1 and the head is 3: init walks the roots of
    // blocks 2..3 (the whole post-activation history) and adopts every node on disk.
    auto pruner = std::make_shared<FCPruner>(backend, c_pruneWindow);
    BOOST_CHECK_NO_THROW(task::syncWait(pruner->init(3, stateRootLookup(fixture.m_ledger), /*sweepGarbage=*/false)));
    BOOST_CHECK_EQUAL(pruner->trackedCount(), fixture.backendNodeCount());
    fixture.m_baselineScheduler.setMPTCommitObserver(pruner);

    for (protocol::BlockNumber number = 4; number <= 7; ++number)
    {
        fixture.planBlock(
            number, {FullChainFixture::balanceRow(addressA, std::to_string(number * 100))});
        roots[number] = fixture.runBlock(number)->stateRoot();
    }

    // At head 7 with N=2 the window is [5, 7]; the root of block r is deleted when block
    // r+1+N commits — roots 2..4 gone (5, 6, 7 committed), roots 5..7 intact.
    for (protocol::BlockNumber number = 5; number <= 7; ++number)
    {
        BOOST_CHECK_MESSAGE(nodeRowInBackend(backend, roots[number]),
            "in-window root of block " + std::to_string(number) + " was pruned");
    }
    for (protocol::BlockNumber number = 2; number <= 4; ++number)
    {
        BOOST_CHECK_MESSAGE(!nodeRowInBackend(backend, roots[number]),
            "out-of-window root of block " + std::to_string(number) + " still on disk");
    }
    BOOST_CHECK_EQUAL(pruneMetadataRowCount(backend), 0U);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace
