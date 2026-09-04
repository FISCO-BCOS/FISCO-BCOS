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
 *          (a) pruning metadata rows (watermark / refcount) are readable from the backend
 *              immediately after commitBlock returns — same WriteBatch as the block data;
 *          (b) past the window the committed "/mpt/" node-row count plateaus (bounded);
 *          (c) roots inside [head-N, head] keep their nodes, older roots are deleted;
 *          (d) a fresh pruner over the same backend (the restart path) passes the startup
 *              guard only when the persisted watermark matches the head, and keeps deleting
 *              on new commits;
 *          (e) enabling pruning on a chain whose MPT built blocks WITHOUT a pruner (no
 *              watermark) is refused by the startup guard.
 *        Deletions land synchronously inside commitBlock (coPreparePruneRows' batch), so every
 *        assertion below runs against the committed state with no worker to drain.
 */
#include "FullChainFixture.h"
#include "bcos-ledger/GenesisStateRoot.h"
#include "bcos-ledger/mpt/MPTPruner.h"
#include "bcos-ledger/mpt/PruneMetadata.h"
#include "bcos-tool/Exceptions.h"

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

std::optional<uint64_t> watermarkInBackend(FCBackend& backend)
{
    auto entry = task::syncWait(storage2::readOne(backend, mpt::watermarkKey()));
    if (!entry)
    {
        return std::nullopt;
    }
    auto raw = entry->get();
    return mpt::decodeWatermark(
        bcos::bytesConstRef(reinterpret_cast<bcos::byte const*>(raw.data()), raw.size()));
}

std::optional<mpt::PruneRefCount> refCountInBackend(
    FCBackend& backend, h256 const& hash)
{
    auto entry = task::syncWait(storage2::readOne(backend, mpt::pruneRefKey(hash)));
    if (!entry)
    {
        return std::nullopt;
    }
    auto raw = entry->get();
    return mpt::decodeRefCount(
        bcos::bytesConstRef(reinterpret_cast<bcos::byte const*>(raw.data()), raw.size()));
}

bool nodeRowInBackend(FCBackend& backend, h256 const& hash)
{
    return task::syncWait(
        storage2::existsOne(backend, bcos::ledger::mptNodeStateKey(hash)));
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
    auto pruner = std::make_shared<mpt::MPTPruner<FCBackend>>(backend, c_pruneWindow);
    // Fresh chain at the genesis block: no watermark yet, the guard lets a non-L2 chain start.
    task::syncWait(pruner->init(0));
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

        // (a) The watermark row must be readable from the backend IMMEDIATELY after commit —
        // it landed in the block's own WriteBatch (coPreparePruneRows -> prewriteStorage ->
        // mergeBackStorage), together with that commit's node deletions.
        fixture.commitOneBlock(header);
        if (number >= 2)  // MPT blocks only; block 1 is XOR and fires no observer
        {
            auto const watermark = watermarkInBackend(backend);
            BOOST_REQUIRE_MESSAGE(watermark.has_value(),
                "watermark row missing right after commit of block " + std::to_string(number));
            BOOST_CHECK_EQUAL(*watermark, static_cast<uint64_t>(number));
        }
        else
        {
            BOOST_CHECK(!watermarkInBackend(backend).has_value());
        }

        roots[number] = header->stateRoot();
        nodeCounts[number] = fixture.backendNodeCount();
    }

    // (a2) The refcount row of the first MPT block's root: exactly one reference, not queued.
    auto const rootRef = refCountInBackend(backend, roots[2]);
    // The root of block 2 was obsoleted at block 3 and deleted at block 5 — its metadata rows
    // are consumed by the deletion, so the row is GONE by now. Pin that, then check a
    // still-live root below instead.
    BOOST_CHECK(!rootRef.has_value());

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
    // A still-live root carries a refcount row with exactly one reference and no pending
    // deletion.
    auto const liveRef = refCountInBackend(backend, roots[8]);
    BOOST_REQUIRE(liveRef.has_value());
    BOOST_CHECK_EQUAL(liveRef->count, 1);
    BOOST_CHECK(!liveRef->pendingDeleteAt.has_value());

    // (d) Restart path: a fresh pruner over the same backend passes the startup guard because
    // the persisted watermark equals the current head — no replay, no catch-up: every deletion
    // already landed with its block's commit.
    auto pruner2 = std::make_shared<mpt::MPTPruner<FCBackend>>(backend, c_pruneWindow);
    task::syncWait(pruner2->init(c_head));
    BOOST_CHECK_EQUAL(pruner2->watermark(), c_head);
    BOOST_CHECK_EQUAL(fixture.backendNodeCount(), nodeCounts[8]);

    // A watermark/head mismatch — pruning disabled for a while, then re-enabled — is refused
    // loudly instead of pruning over the uncounted gap.
    auto prunerStale = std::make_shared<mpt::MPTPruner<FCBackend>>(backend, c_pruneWindow);
    BOOST_CHECK_THROW(
        task::syncWait(prunerStale->init(c_head + 5)), bcos::tool::InvalidConfig);

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
    auto const watermark = watermarkInBackend(backend);
    BOOST_REQUIRE(watermark.has_value());
    BOOST_CHECK_EQUAL(*watermark, static_cast<uint64_t>(c_next));
    BOOST_CHECK_EQUAL(fixture.backendNodeCount(), nodeCounts[8]);  // plateau held
}

BOOST_AUTO_TEST_CASE(midChainEnableIsRefusedByStartupGuard)
{
    // Blocks committed while NO pruner was wired leave no watermark and uncounted node deltas;
    // enabling pruning afterwards must be refused instead of pruning live state.
    FullChainFixture fixture{"mpt_pruner_midchain_guard"};
    fixture.buildGenesis(FullChainFixture::baseGenesis());
    fixture.enableFeatureFromBlock(c_mptFlagName, 1);

    auto const addressA = FullChainFixture::makeAddress(0xA6);
    for (protocol::BlockNumber number = 1; number <= 3; ++number)
    {
        fixture.planBlock(
            number, {FullChainFixture::balanceRow(addressA, std::to_string(number * 100))});
        fixture.runBlock(number);
    }

    auto& backend = fixture.m_multiLayerStorage.latestBackend();
    // feature_mpt_state_root activated at block 1 and the head is 3 — past the safe point.
    auto pruner = std::make_shared<mpt::MPTPruner<FCBackend>>(backend, c_pruneWindow);
    BOOST_CHECK_THROW(task::syncWait(pruner->init(3)), bcos::tool::InvalidConfig);

    // At the activation block itself it would still have been safe: block 1 keeps the legacy
    // XOR root (strictly-greater rule), so no MPT block had committed yet.
    auto early = std::make_shared<mpt::MPTPruner<FCBackend>>(backend, c_pruneWindow);
    BOOST_CHECK_NO_THROW(task::syncWait(early->init(1)));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace
