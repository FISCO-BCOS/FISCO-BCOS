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
 * @file MPTPrunerTest.cpp
 * @brief MPTPruner: in-memory refcount transition rules, cross-trie sharing, windowed deletion
 *        invariants over random workloads, the startup rebuild (three phases) and the tombstone
 *        path's manual counting (spec §4.8)
 */
#include "TestHelpers.h"
#include <bcos-framework/ledger/Features.h>
#include <bcos-framework/ledger/FeaturesStorage.h>
#include <bcos-ledger/GenesisStateRoot.h>
#include <bcos-ledger/mpt/CommitObserver.h>
#include <bcos-ledger/mpt/MPTPruner.h>
#include <bcos-ledger/mpt/Proof.h>
#include <bcos-ledger/mpt/Trie.h>
#include <bcos-storage/KeyPrefixes.h>
#include <boost/test/unit_test.hpp>
#include <deque>
#include <map>
#include <optional>
#include <ranges>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace bcos::ledger::mpt::test
{

BOOST_AUTO_TEST_SUITE(MPTPrunerSuite)

namespace
{
/// The committed-state backend: ordered (the Phase-3 sweep range-scans the "/mpt/" table with
/// RANGE_SEEK) and physically deleting (no LOGICAL_DELETION attribute), matching RocksDB.
using PruneBackend = bcos::storage2::memory_storage::MemoryStorage<bcos::executor_v1::StateKey,
    bcos::executor_v1::StateValue, bcos::storage2::memory_storage::ORDERED>;
using Pruner = MPTPruner<PruneBackend>;

/// The (h256 → raw RLP) trie-node facade over the StateKey-keyed backend — the same mapping the
/// production adapters (ViewNodeStorage / MPTNodeReadStorage) apply: StateKey{"/mpt/", digest}.
/// Test-local because the production adapters live in bcos-storage / transaction-scheduler and
/// are read-only or view-bound; the pruner tests need a writable one over a bare backend.
class BackendNodeStorage
{
public:
    using Key = bcos::h256;
    using Value = bcos::bytes;

    explicit BackendNodeStorage(PruneBackend& backend) : m_backend(std::addressof(backend)) {}

    bcos::task::Task<std::optional<bcos::bytes>> readOne(bcos::h256 key)
    {
        auto entry =
            co_await bcos::storage2::readOne(*m_backend, bcos::ledger::mptNodeStateKey(key));
        if (!entry)
        {
            co_return std::nullopt;
        }
        auto raw = entry->get();
        co_return bcos::bytes(raw.begin(), raw.end());
    }

    bcos::task::Task<std::vector<std::optional<bcos::bytes>>> readSome(
        ::ranges::input_range auto keys)
    {
        std::vector<std::optional<bcos::bytes>> values;
        for (auto const& key : keys)
        {
            values.emplace_back(co_await readOne(key));
        }
        co_return values;
    }

    bcos::task::Task<void> writeOne(bcos::h256 key, bcos::bytes value)
    {
        bcos::storage::Entry entry;
        entry.set(std::move(value));
        co_await bcos::storage2::writeOne(
            *m_backend, bcos::ledger::mptNodeStateKey(key), std::move(entry));
    }

    bcos::task::Task<void> writeSome(::ranges::input_range auto keyValues)
    {
        for (auto const& [key, value] : keyValues)
        {
            bcos::storage::Entry entry;
            entry.set(bcos::bytes(value.begin(), value.end()));
            co_await bcos::storage2::writeOne(
                *m_backend, bcos::ledger::mptNodeStateKey(key), std::move(entry));
        }
        co_return;
    }

private:
    PruneBackend* m_backend;
};

/// The hashes of every hash-addressed node reachable from @p root (the live set oracle, same
/// walk as HashBuilderIncrementalTest's reachableHashes). BOOST_REQUIREs on a missing node, so
/// a successful return also proves the trie version is fully resolvable.
std::unordered_set<bcos::h256> liveNodeHashes(BackendNodeStorage& storage, bcos::h256 root)
{
    std::unordered_set<bcos::h256> out;
    if (root == emptyRootHash())
    {
        return out;
    }
    std::vector<bcos::h256> queue{root};
    while (!queue.empty())
    {
        bcos::h256 const hash = queue.back();
        queue.pop_back();
        if (!out.insert(hash).second)
        {
            continue;
        }
        auto raw = bcos::task::syncWait(bcos::storage2::readOne(storage, hash));
        BOOST_REQUIRE_MESSAGE(raw.has_value(), "reachable node missing from storage");
        TrieNode const node = decodeNode(bcos::ref(*raw));
        if (auto const* ext = std::get_if<ExtensionNode>(&node))
        {
            if (ext->child.size() == HASH_REF_ENCODED_SIZE && ext->child[0] == RLP_HASH_REF_PREFIX)
            {
                queue.emplace_back(
                    bcos::bytesConstRef(ext->child.data(), ext->child.size()).getCroppedData(1));
            }
        }
        else if (auto const* branch = std::get_if<BranchNode>(&node))
        {
            for (auto const& child : branch->children)
            {
                if (child.kind() == NodeRef::Kind::Hash)
                {
                    queue.push_back(child.hash());
                }
            }
        }
    }
    return out;
}

/// Number of live (non-tombstone) rows of @p table in @p backend.
size_t countRowsInTable(PruneBackend& backend, std::string_view table)
{
    auto iterator = bcos::task::syncWait(bcos::storage2::range(backend));
    size_t count = 0;
    while (auto item = bcos::task::syncWait(iterator.next()))
    {
        auto const& [key, value] = *item;
        if (bcos::executor_v1::StateKeyView{key}.m_table == table &&
            std::get_if<bcos::storage::Entry>(std::addressof(value)))
        {
            ++count;
        }
    }
    return count;
}

bool nodeRowExists(PruneBackend& backend, bcos::h256 const& hash)
{
    return bcos::task::syncWait(
        bcos::storage2::existsOne(backend, bcos::ledger::mptNodeStateKey(hash)));
}

/// A deterministic junk hash for garbage-row tests: 0xA5-prefixed with the counter in the last
/// 4 bytes (real node hashes are Keccak digests, so collisions are cryptographically absent).
bcos::h256 garbageHash(uint32_t index)
{
    bcos::h256 h{};
    h.data()[0] = 0xA5;
    for (size_t byte = 0; byte < 4; ++byte)
    {
        h.data()[bcos::h256::SIZE - 1 - byte] =
            static_cast<bcos::byte>((index >> (8 * byte)) & 0xFF);
    }
    return h;
}

/// Write @p count unreachable junk rows into the "/mpt/" table (Phase-3 garbage).
void writeGarbageRows(PruneBackend& backend, size_t count)
{
    std::vector<std::pair<bcos::executor_v1::StateKey, bcos::storage::Entry>> rows;
    rows.reserve(count);
    for (uint32_t i = 0; i < count; ++i)
    {
        bcos::storage::Entry entry;
        entry.set(bcos::bytes(4, 0x66));
        rows.emplace_back(bcos::ledger::mptNodeStateKey(garbageHash(i)), std::move(entry));
    }
    bcos::task::syncWait(bcos::storage2::writeSome(backend, std::move(rows)));
}

Account makePruneAccount(uint64_t nonce, uint64_t balance)
{
    Account account;  // storageRoot=emptyRootHash(), codeHash=emptyCodeHash() by default
    account.nonce = nonce;
    account.balance = balance;
    return account;
}

/// The feature row marking the chain's MPT active from @p activation (enableNumber): the first
/// MPT block is activation + 1 (the activation block itself keeps the legacy XOR root). With
/// activation 0 the chain builds MPT roots from block 1 — the shape the pruning tests drive.
void writeMptActivation(PruneBackend& backend, bcos::protocol::BlockNumber activation)
{
    bcos::ledger::Features features;
    features.set(bcos::ledger::Features::Flag::feature_mpt_state_root);
    bcos::task::syncWait(features.writeToStorage(backend, activation));
}

/// A StateRootLookup over a recorded (block → stateRoot) map; absent blocks report nullopt.
Pruner::StateRootLookup rootLookupOf(
    std::map<bcos::protocol::BlockNumber, bcos::h256> const& roots)
{
    return [&roots](bcos::protocol::BlockNumber number)
               -> bcos::task::Task<std::optional<bcos::h256>> {
        auto const it = roots.find(number);
        co_return it == roots.end() ? std::nullopt : std::optional<bcos::h256>{it->second};
    };
}

/// One full commit of block @p blockNumber over @p delta, in the production commit flow's order
/// (BaselineScheduler-tpp.h): prepare the pruning batch → apply its deletions (the stand-in for
/// the block's single WriteBatch onto prewriteStorage) → onCommit. onCommit is what lands the
/// block's staged counting on the pruner's base tables, so tests read post-commit state only
/// after this returns. The in-memory pruner never produces upsert rows — only node deletions.
void commitPruneBlock(PruneBackend& backend, Pruner& pruner,
    bcos::protocol::BlockNumber blockNumber, MPTDeltaLayer const& delta)
{
    auto batch = bcos::task::syncWait(pruner.coPreparePruneRows(blockNumber, delta));
    if (!batch.deletions.empty())
    {
        bcos::task::syncWait(bcos::storage2::removeSome(backend, std::move(batch.deletions)));
    }
    pruner.onCommit(blockNumber, delta);
}

/// Blocks with no trie delta: the delete queue is still consumed up to each block — how a
/// matured deletion or a stale schedule drains without new account churn.
void runEmptyBlocks(PruneBackend& backend, Pruner& pruner, bcos::protocol::BlockNumber from,
    bcos::protocol::BlockNumber to)
{
    for (auto block = from; block <= to; ++block)
    {
        commitPruneBlock(backend, pruner, block, MPTDeltaLayer{});
    }
}

/// One block of the account-trie chain the pruning tests drive, in the production commit order:
/// build the trie delta → flush its nodes → prepare + apply the pruning batch + onCommit (the
/// deletions of expired nodes, the prewriteStorage stand-in). Returns the delta for inspection.
MPTDeltaLayer commitAccountBlock(PruneBackend& backend, BackendNodeStorage& nodes,
    Pruner& pruner, std::map<bcos::Address, Account>& accounts, bcos::h256 priorRoot,
    std::map<bcos::Address, std::optional<Account>> const& accountChanges,
    bcos::protocol::BlockNumber blockNumber)
{
    std::map<bcos::h256, std::optional<bcos::bytes>> trieChanges;
    for (auto const& [address, account] : accountChanges)
    {
        if (account)
        {
            accounts[address] = *account;
            trieChanges[accountKeyHash(address)] = account->encode();
        }
        else
        {
            accounts.erase(address);
            trieChanges[accountKeyHash(address)] = std::nullopt;
        }
    }

    auto result = bcos::task::syncWait(commitTrie(nodes, priorRoot, trieChanges));
    MPTDeltaLayer delta;
    delta.stateRoot = result.root;
    mergeNodeDelta(std::move(result), delta);
    bcos::task::syncWait(flushTrieNodes(nodes, delta.newNodes));
    commitPruneBlock(backend, pruner, blockNumber, delta);
    return delta;
}
}  // namespace

BOOST_AUTO_TEST_CASE(DefaultObserverHookReturnsNoRows)
{
    NoopCommitObserver observer;
    MPTDeltaLayer delta;
    delta.newNodes[makeHash(0x01)] = bcos::bytes{0x01};
    auto batch = bcos::task::syncWait(observer.coPreparePruneRows(3, delta));
    BOOST_CHECK(batch.deletions.empty());
}

BOOST_AUTO_TEST_CASE(UncountedDeltaThrowsFailLoud)
{
    // A delta that changed nodes but carries no refCountDeltas tally (a build run with
    // trackRefCounts=false — production prevents this via needsRefCountDeltas, so it is a
    // wiring bug): the set reading would under-count content-addressed nodes shared across
    // the block's tries (newNodes deduplicates duplicate emissions), and skipping the block
    // is not a pure leak either (nodes born in it stay uncounted forever) — so the pruner
    // fails loud, failing the block's commit on the commit path.
    PruneBackend backend;
    BackendNodeStorage nodes(backend);
    Pruner pruner(backend, /*pruneWindow=*/5);

    auto const h1 = makeHash(0x01);
    auto const h2 = makeHash(0x02);
    MPTDeltaLayer delta;
    delta.newNodes[h1] = bcos::bytes{0x11};
    delta.obsoletedNodes.insert(h2);

    BOOST_CHECK_THROW(
        bcos::task::syncWait(pruner.coPreparePruneRows(7, delta)), MPTInvariantViolation);
    BOOST_CHECK(!pruner.countOf(h1).has_value());
    BOOST_CHECK(!pruner.countOf(h2).has_value());
    BOOST_CHECK_EQUAL(pruner.pendingCount(), 0U);
}

BOOST_AUTO_TEST_CASE(EmptyDeltaPassesSilently)
{
    // A fully empty delta is the normal empty block, not the uncounted-delta case: no ERROR,
    // and the delete queue is still consumed up to this block.
    PruneBackend backend;
    BackendNodeStorage nodes(backend);
    Pruner pruner(backend, /*pruneWindow=*/2);
    auto const h1 = makeHash(0x01);

    // Counted history: created at block 1, obsoleted at block 2 → deadline 4.
    MPTDeltaLayer delta1;
    delta1.refCountDeltas[h1] = 1;
    delta1.newNodes[h1] = bcos::bytes{0x11};
    commitPruneBlock(backend, pruner, 1, delta1);
    bcos::task::syncWait(nodes.writeOne(h1, bcos::bytes{0x11}));
    MPTDeltaLayer delta2;
    delta2.refCountDeltas[h1] = -1;
    delta2.obsoletedNodes.insert(h1);
    commitPruneBlock(backend, pruner, 2, delta2);
    BOOST_REQUIRE_EQUAL(pruner.pendingCount(), 1U);

    // Empty blocks still consume the matured deletion.
    auto batch = bcos::task::syncWait(pruner.coPreparePruneRows(3, MPTDeltaLayer{}));
    BOOST_CHECK(batch.deletions.empty());  // not yet due
    runEmptyBlocks(backend, pruner, 4, 4);
    BOOST_CHECK(!nodeRowExists(backend, h1));
    BOOST_CHECK_EQUAL(pruner.pendingCount(), 0U);
    BOOST_CHECK(!pruner.countOf(h1).has_value());  // entry erased with the deletion
}

BOOST_AUTO_TEST_CASE(SummaryLogIntervalFloorsAtHundred)
{
    // The steady-state INFO summary fires every max(N, 100) blocks: the floor keeps a small
    // prune window from printing one line per block.
    BOOST_CHECK_EQUAL(Pruner::summaryLogInterval(0), Pruner::SUMMARY_LOG_MIN_INTERVAL);
    BOOST_CHECK_EQUAL(Pruner::summaryLogInterval(1), Pruner::SUMMARY_LOG_MIN_INTERVAL);
    BOOST_CHECK_EQUAL(
        Pruner::summaryLogInterval(100), Pruner::SUMMARY_LOG_MIN_INTERVAL);
    BOOST_CHECK_EQUAL(Pruner::summaryLogInterval(101), 101);
    BOOST_CHECK_EQUAL(Pruner::summaryLogInterval(10'000), 10'000);
}

BOOST_AUTO_TEST_CASE(ObsoletionToZeroQueuesDeletion)
{
    PruneBackend backend;
    BackendNodeStorage nodes(backend);
    Pruner pruner(backend, /*pruneWindow=*/5);
    auto const h1 = makeHash(0x01);

    MPTDeltaLayer delta1;
    delta1.refCountDeltas[h1] = 1;
    delta1.newNodes[h1] = bcos::bytes{0x11};
    commitPruneBlock(backend, pruner, 1, delta1);

    MPTDeltaLayer delta2;
    delta2.refCountDeltas[h1] = -1;
    delta2.obsoletedNodes.insert(h1);
    commitPruneBlock(backend, pruner, 2, delta2);

    // Count went 1→0: scheduled at 2+5=7.
    BOOST_CHECK(pruner.countOf(h1) == std::optional<uint64_t>{0});
    BOOST_CHECK(pruner.deadlineOf(h1) == std::optional<uint64_t>{7});
    BOOST_CHECK_EQUAL(pruner.pendingCount(), 1U);
    BOOST_CHECK(nodeRowExists(backend, h1) == false);  // this test never wrote a node row
}

BOOST_AUTO_TEST_CASE(SharedRefCountSurvivesSingleObsoletion)
{
    PruneBackend backend;
    BackendNodeStorage nodes(backend);
    Pruner pruner(backend, /*pruneWindow=*/5);
    auto const h1 = makeHash(0x01);

    // Two referencing tries, one block each: +1 twice.
    for (bcos::protocol::BlockNumber block = 1; block <= 2; ++block)
    {
        MPTDeltaLayer delta;
        delta.refCountDeltas[h1] = 1;
        delta.newNodes[h1] = bcos::bytes{0x11};
        commitPruneBlock(backend, pruner, block, delta);
    }
    BOOST_CHECK(pruner.countOf(h1) == std::optional<uint64_t>{2});

    MPTDeltaLayer delta3;
    delta3.refCountDeltas[h1] = -1;
    delta3.obsoletedNodes.insert(h1);
    commitPruneBlock(backend, pruner, 3, delta3);

    // 2→1: still referenced — no deadline armed.
    BOOST_CHECK(pruner.countOf(h1) == std::optional<uint64_t>{1});
    BOOST_CHECK(!pruner.deadlineOf(h1).has_value());
    BOOST_CHECK_EQUAL(pruner.pendingCount(), 0U);
}

BOOST_AUTO_TEST_CASE(CommitRetryPrepareIsIdempotent)
{
    // The commit flow retries a block whose merge failed by re-running coPreparePruneRows with
    // the SAME delta (BaselineScheduler keeps the pending result; onCommit never ran). The
    // block's effects are staged on an overlay until onCommit, so the retry recomputes from the
    // pre-block state: identical batch, no double-applied refCountDeltas. Regression pinned:
    // applying the deltas directly at prepare time drove a count-2 shared node 2→1→0 across the
    // retry and scheduled the deletion of a node a second trie still referenced.
    PruneBackend backend;
    BackendNodeStorage nodes(backend);
    Pruner pruner(backend, /*pruneWindow=*/5);
    auto const shared = makeHash(0x01);
    auto const expiring = makeHash(0x02);
    bcos::task::syncWait(nodes.writeOne(expiring, bcos::bytes{0x22}));

    // Block 1: the shared node's first reference; the expiring node is created. Block 2: the
    // shared node's second reference (count 2); the expiring node is obsoleted → deadline 7.
    MPTDeltaLayer delta1;
    delta1.refCountDeltas[shared] = 1;
    delta1.newNodes[shared] = bcos::bytes{0x11};
    delta1.refCountDeltas[expiring] = 1;
    delta1.newNodes[expiring] = bcos::bytes{0x22};
    commitPruneBlock(backend, pruner, 1, delta1);
    MPTDeltaLayer delta2;
    delta2.refCountDeltas[shared] = 1;
    delta2.newNodes[shared] = bcos::bytes{0x11};
    delta2.refCountDeltas[expiring] = -1;
    delta2.obsoletedNodes.insert(expiring);
    commitPruneBlock(backend, pruner, 2, delta2);
    BOOST_REQUIRE(pruner.countOf(shared) == std::optional<uint64_t>{2});

    // Block 3 obsoletes ONE of the shared node's two references (2→1 — no schedule may arm).
    // Its commit "fails" after prepare: the identical prepare runs again, with no onCommit in
    // between.
    MPTDeltaLayer delta3;
    delta3.refCountDeltas[shared] = -1;
    delta3.obsoletedNodes.insert(shared);
    auto first = bcos::task::syncWait(pruner.coPreparePruneRows(3, delta3));
    BOOST_CHECK(first.deletions.empty());
    // The base tables are untouched until onCommit: the pre-block count still reads 2.
    BOOST_CHECK(pruner.countOf(shared) == std::optional<uint64_t>{2});

    auto retry = bcos::task::syncWait(pruner.coPreparePruneRows(3, delta3));
    BOOST_CHECK(retry.deletions == first.deletions);
    // The retry armed no schedule: the only pending entry is still the expiring node's.
    BOOST_CHECK(!pruner.deadlineOf(shared).has_value());
    BOOST_CHECK_EQUAL(pruner.pendingCount(), 1U);
    BOOST_CHECK(pruner.countOf(shared) == std::optional<uint64_t>{2});

    // One onCommit for the one successful merge: exactly one application — count 1, not 0.
    pruner.onCommit(3, delta3);
    BOOST_CHECK(pruner.countOf(shared) == std::optional<uint64_t>{1});
    BOOST_CHECK(!pruner.deadlineOf(shared).has_value());
    BOOST_CHECK_EQUAL(pruner.pendingCount(), 1U);  // still only the expiring node

    // The same idempotency at a DELETION block: the expiring node matures at block 7. Two
    // prepares produce the same one-key batch; one onCommit + one apply removes the row once.
    runEmptyBlocks(backend, pruner, 4, 6);
    auto firstAt7 = bcos::task::syncWait(pruner.coPreparePruneRows(7, MPTDeltaLayer{}));
    BOOST_REQUIRE_EQUAL(firstAt7.deletions.size(), 1U);
    auto retryAt7 = bcos::task::syncWait(pruner.coPreparePruneRows(7, MPTDeltaLayer{}));
    BOOST_CHECK(retryAt7.deletions == firstAt7.deletions);
    pruner.onCommit(7, MPTDeltaLayer{});
    bcos::task::syncWait(bcos::storage2::removeSome(backend, std::move(firstAt7.deletions)));
    BOOST_CHECK(!nodeRowExists(backend, expiring));
    BOOST_CHECK(!pruner.countOf(expiring).has_value());
    BOOST_CHECK_EQUAL(pruner.pendingCount(), 0U);
}

BOOST_AUTO_TEST_CASE(RevivalRevokesPendingDelete)
{
    PruneBackend backend;
    BackendNodeStorage nodes(backend);
    Pruner pruner(backend, /*pruneWindow=*/5);
    auto const h1 = makeHash(0x01);

    // Block 1: created. Block 2: obsoleted → queued at 7. The node row sits on disk.
    MPTDeltaLayer delta1;
    delta1.refCountDeltas[h1] = 1;
    delta1.newNodes[h1] = bcos::bytes{0x11};
    commitPruneBlock(backend, pruner, 1, delta1);
    bcos::task::syncWait(nodes.writeOne(h1, bcos::bytes{0x11}));
    MPTDeltaLayer delta2;
    delta2.refCountDeltas[h1] = -1;
    delta2.obsoletedNodes.insert(h1);
    commitPruneBlock(backend, pruner, 2, delta2);
    BOOST_CHECK_EQUAL(pruner.pendingCount(), 1U);

    // Block 3: re-created before its deletion ran → count 1, schedule revoked EAGERLY (the
    // in-memory queue needs no lazy stale-row cleanup).
    MPTDeltaLayer delta3;
    delta3.refCountDeltas[h1] = 1;
    delta3.newNodes[h1] = bcos::bytes{0x11};
    commitPruneBlock(backend, pruner, 3, delta3);
    BOOST_CHECK(pruner.countOf(h1) == std::optional<uint64_t>{1});
    BOOST_CHECK(!pruner.deadlineOf(h1).has_value());
    BOOST_CHECK_EQUAL(pruner.pendingCount(), 0U);

    // Nothing matures afterwards; the revived node rides out its old deadline.
    runEmptyBlocks(backend, pruner, 4, 10);
    BOOST_CHECK(nodeRowExists(backend, h1));
    BOOST_CHECK(pruner.countOf(h1) == std::optional<uint64_t>{1});
}

BOOST_AUTO_TEST_CASE(RevivalAtExpiryBlockIsNotDeleted)
{
    // The F2 race, closed by preparing deletions inside the commit coroutine: a node whose
    // deletion matures at block B and is revived BY block B itself must survive — the
    // consumption re-check reads the count AS UPDATED BY THIS BLOCK, not the pre-block count 0.
    PruneBackend backend;
    BackendNodeStorage nodes(backend);
    Pruner pruner(backend, /*pruneWindow=*/2);
    auto const h1 = makeHash(0x01);

    // Block 1: created (node row on disk). Block 2: obsoleted → queued at 4.
    MPTDeltaLayer delta1;
    delta1.refCountDeltas[h1] = 1;
    delta1.newNodes[h1] = bcos::bytes{0x11};
    commitPruneBlock(backend, pruner, 1, delta1);
    bcos::task::syncWait(nodes.writeOne(h1, bcos::bytes{0x11}));
    MPTDeltaLayer delta2;
    delta2.refCountDeltas[h1] = -1;
    delta2.obsoletedNodes.insert(h1);
    commitPruneBlock(backend, pruner, 2, delta2);
    BOOST_REQUIRE(pruner.countOf(h1) == std::optional<uint64_t>{0});
    BOOST_REQUIRE(pruner.deadlineOf(h1) == std::optional<uint64_t>{4});
    BOOST_REQUIRE_EQUAL(pruner.pendingCount(), 1U);

    // Block 4 — the expiry block itself — re-creates the node (0→1, schedule revoked). The
    // expired deadline IS consumed this block: the re-check must see the revival.
    MPTDeltaLayer delta4;
    delta4.refCountDeltas[h1] = 1;
    delta4.newNodes[h1] = bcos::bytes{0x11};
    commitPruneBlock(backend, pruner, 4, delta4);

    BOOST_CHECK(nodeRowExists(backend, h1));
    BOOST_CHECK(pruner.countOf(h1) == std::optional<uint64_t>{1});
    BOOST_CHECK_EQUAL(pruner.pendingCount(), 0U);
}

BOOST_AUTO_TEST_CASE(UntrackedObsoletionSaturatesAtZeroAndQueues)
{
    // Genesis-prewrite shape: a node row that never passed any delta has no count entry.
    PruneBackend backend;
    BackendNodeStorage nodes(backend);
    Pruner pruner(backend, /*pruneWindow=*/5);
    auto const h1 = makeHash(0x01);
    bcos::task::syncWait(nodes.writeOne(h1, bcos::bytes{0x11}));

    MPTDeltaLayer delta;
    delta.refCountDeltas[h1] = -1;
    delta.obsoletedNodes.insert(h1);
    commitPruneBlock(backend, pruner, 4, delta);

    // Saturating 0→0, still queued at 4+5=9.
    BOOST_CHECK(pruner.countOf(h1) == std::optional<uint64_t>{0});
    BOOST_CHECK(pruner.deadlineOf(h1) == std::optional<uint64_t>{9});
    BOOST_CHECK_EQUAL(pruner.pendingCount(), 1U);

    runEmptyBlocks(backend, pruner, 5, 8);
    BOOST_CHECK(nodeRowExists(backend, h1));  // not yet due
    runEmptyBlocks(backend, pruner, 9, 9);
    BOOST_CHECK(!nodeRowExists(backend, h1));
    BOOST_CHECK(!pruner.countOf(h1).has_value());
    BOOST_CHECK_EQUAL(pruner.pendingCount(), 0U);
}

BOOST_AUTO_TEST_CASE(IntraBlockObsoletionNetsToZero)
{
    PruneBackend backend;
    BackendNodeStorage nodes(backend);
    Pruner pruner(backend, /*pruneWindow=*/5);
    auto const h1 = makeHash(0x01);

    MPTDeltaLayer delta1;
    delta1.refCountDeltas[h1] = 1;
    delta1.newNodes[h1] = bcos::bytes{0x11};
    commitPruneBlock(backend, pruner, 1, delta1);

    // Produced AND obsoleted within block 2, with a prior count of 1: +1 −1 nets to 0 and the
    // node stays counted — it is referenced by the new version.
    MPTDeltaLayer delta2;
    delta2.refCountDeltas[h1] = 0;
    delta2.newNodes[h1] = bcos::bytes{0x11};
    delta2.intraBlockObsoleted.insert(h1);
    commitPruneBlock(backend, pruner, 2, delta2);
    BOOST_CHECK(pruner.countOf(h1) == std::optional<uint64_t>{1});
    BOOST_CHECK(!pruner.deadlineOf(h1).has_value());
    BOOST_CHECK_EQUAL(pruner.pendingCount(), 0U);

    // The same shape with NO prior count (untracked history): the node is on disk (it was in
    // newNodes) and referenced by nothing the counter can see — queue it like any 0→0
    // obsoletion.
    auto const h2 = makeHash(0x02);
    MPTDeltaLayer delta3;
    delta3.refCountDeltas[h2] = 0;
    delta3.newNodes[h2] = bcos::bytes{0x22};
    delta3.intraBlockObsoleted.insert(h2);
    commitPruneBlock(backend, pruner, 3, delta3);
    BOOST_CHECK(pruner.countOf(h2) == std::optional<uint64_t>{0});
    BOOST_CHECK(pruner.deadlineOf(h2) == std::optional<uint64_t>{8});
    BOOST_CHECK_EQUAL(pruner.pendingCount(), 1U);
}

BOOST_AUTO_TEST_CASE(CrossTrieSharingSurvivesUntilLastReferenceDrops)
{
    // Two accounts whose storage tries encode identically resolve to the same node hashes —
    // the content-addressed sharing MPTDeltaLayer warns about. Both builds happen in ONE block,
    // so newNodes deduplicates them and only refCountDeltas still carries the true count of 2.
    PruneBackend backend;
    BackendNodeStorage nodes(backend);
    constexpr int64_t N = 3;
    Pruner pruner(backend, N);

    std::map<bcos::h256, bcos::bytes> const content{
        {makeHash(0x11), bcos::bytes{0x01}}, {makeHash(0x22), bcos::bytes(40, 0x02)},
        {makeHash(0x33), bcos::bytes(20, 0x03)}};
    std::map<bcos::h256, std::optional<bcos::bytes>> const contentChanges{
        content.begin(), content.end()};

    // Block 1: account A's and account B's storage builds emit the byte-identical nodes.
    auto resultA = bcos::task::syncWait(commitTrie(nodes, emptyRootHash(), contentChanges));
    auto resultB = bcos::task::syncWait(commitTrie(nodes, emptyRootHash(), contentChanges));
    auto const sharedRoot = resultA.root;
    BOOST_REQUIRE(resultB.root == sharedRoot);
    MPTDeltaLayer delta1;
    delta1.stateRoot = sharedRoot;
    mergeNodeDelta(std::move(resultA), delta1);
    mergeNodeDelta(std::move(resultB), delta1);
    bcos::task::syncWait(flushTrieNodes(nodes, delta1.newNodes));
    commitPruneBlock(backend, pruner, 1, delta1);

    // Sanity: the delta really did deduplicate the double emission (else the test is vacuous),
    // and every shared node was still counted twice.
    auto const liveAfterBlock1 = liveNodeHashes(nodes, sharedRoot);
    BOOST_REQUIRE(!liveAfterBlock1.empty());
    BOOST_CHECK_EQUAL(delta1.newNodes.size(), liveAfterBlock1.size());
    for (auto const& hash : liveAfterBlock1)
    {
        BOOST_REQUIRE(delta1.refCountDeltas.contains(hash));
        BOOST_CHECK_EQUAL(delta1.refCountDeltas.at(hash), 2);
        BOOST_CHECK(pruner.countOf(hash) == std::optional<uint64_t>{2});
    }

    // Block 2: A's storage trie is rebuilt (one value changed) — every shared node it obsoletes
    // drops 2→1 and must survive past A's own deletion deadline. The two unchanged keys stay in
    // the change-set as NO-OP puts: mergeTrie resolves their paths and re-emits those nodes
    // byte-identically (TrieMergeResult::reemittedNodes), which must net to ZERO reference
    // movement — the same trie referenced them before and after.
    std::map<bcos::h256, std::optional<bcos::bytes>> changesA = contentChanges;
    changesA[makeHash(0x11)] = bcos::bytes{0x7F};
    auto resultA2 = bcos::task::syncWait(commitTrie(nodes, sharedRoot, changesA));
    MPTDeltaLayer delta2;
    delta2.stateRoot = resultA2.root;
    size_t reemitted = resultA2.reemittedNodes.size();
    mergeNodeDelta(std::move(resultA2), delta2);
    BOOST_REQUIRE(!delta2.obsoletedNodes.empty());
    BOOST_REQUIRE(reemitted > 0U);  // else the no-op puts exercised nothing
    bcos::task::syncWait(flushTrieNodes(nodes, delta2.newNodes));
    commitPruneBlock(backend, pruner, 2, delta2);

    for (auto const& hash : delta2.obsoletedNodes)
    {
        BOOST_CHECK(pruner.countOf(hash) == std::optional<uint64_t>{1});
        BOOST_CHECK(!pruner.deadlineOf(hash).has_value());
    }
    // Re-emitted nodes: still in newNodes (flushed, still live), but their refcount is unmoved.
    for (auto const& hash : delta2.newNodes | std::views::keys)
    {
        if (liveAfterBlock1.contains(hash))
        {
            BOOST_CHECK_EQUAL(delta2.refCountDeltas.at(hash), 0);
            BOOST_CHECK(pruner.countOf(hash) == std::optional<uint64_t>{2});
        }
    }
    // Well past 2+N, but B still references those nodes: nothing may be deleted. (Nothing was
    // ever queued — the 2→1 drops arm no deadline — so the node rows are the assertion that
    // matters.)
    runEmptyBlocks(backend, pruner, 3, 2 + N);
    for (auto const& hash : delta2.obsoletedNodes)
    {
        BOOST_CHECK_MESSAGE(nodeRowExists(backend, hash),
            "shared node deleted while a second trie still references it");
    }
    BOOST_CHECK(liveNodeHashes(nodes, sharedRoot).size() == liveAfterBlock1.size());

    // Block 2+N+1: B's trie is rebuilt with the SAME change — the last reference to the old
    // nodes drops (1→0, queued at 2+N+1+N), and B's fresh nodes byte-match A's block-2 emission
    // (counted +1 there), so the re-emission makes them count 2: both new tries reference them.
    auto const blockB = 2 + N + 1;
    auto resultB2 = bcos::task::syncWait(commitTrie(nodes, sharedRoot, changesA));
    BOOST_REQUIRE(resultB2.root == delta2.stateRoot);
    MPTDeltaLayer delta3;
    delta3.stateRoot = resultB2.root;
    mergeNodeDelta(std::move(resultB2), delta3);
    bcos::task::syncWait(flushTrieNodes(nodes, delta3.newNodes));
    commitPruneBlock(backend, pruner, blockB, delta3);

    for (auto const& hash : delta2.obsoletedNodes)
    {
        BOOST_CHECK(pruner.countOf(hash) == std::optional<uint64_t>{0});
        BOOST_CHECK(pruner.deadlineOf(hash) == std::optional<uint64_t>{blockB + N});
    }
    for (auto const& hash : delta3.newNodes | std::views::keys)
    {
        BOOST_CHECK(pruner.countOf(hash) == std::optional<uint64_t>{2});
    }

    // Deadline discipline: present at blockB+N−1, gone at blockB+N.
    runEmptyBlocks(backend, pruner, blockB + 1, blockB + N - 1);
    for (auto const& hash : delta2.obsoletedNodes)
    {
        BOOST_CHECK(nodeRowExists(backend, hash));
    }
    runEmptyBlocks(backend, pruner, blockB + N, blockB + N);
    for (auto const& hash : delta2.obsoletedNodes)
    {
        BOOST_CHECK(!nodeRowExists(backend, hash));
    }
    // The surviving version reads back complete.
    BOOST_CHECK(!liveNodeHashes(nodes, resultB2.root).empty());
}

BOOST_AUTO_TEST_CASE(WindowedRandomWorkloadInvariants)
{
    // N=3, 30 blocks of random account churn. After every block: the last N+1 roots resolve
    // completely and prove, and the on-disk node rows are EXACTLY the union of the window's
    // live node sets (nothing early, nothing leaked).
    constexpr int64_t N = 3;
    constexpr bcos::protocol::BlockNumber BLOCKS = 30;
    auto rng = seededRng(0x5EED42);

    PruneBackend backend;
    BackendNodeStorage nodes(backend);
    Pruner pruner(backend, N);

    std::vector<bcos::Address> pool;
    for (uint8_t i = 1; i <= 40; ++i)
    {
        pool.push_back(makeAddress(i));
    }
    std::map<bcos::Address, Account> accounts;
    std::map<bcos::Address, uint64_t> versions;  // every update changes the leaf — no no-op puts
    struct Version
    {
        bcos::protocol::BlockNumber number;
        bcos::h256 root;
        std::map<bcos::Address, Account> accounts;
    };
    std::deque<Version> history;

    bcos::h256 root = emptyRootHash();
    for (bcos::protocol::BlockNumber block = 1; block <= BLOCKS; ++block)
    {
        std::map<bcos::Address, std::optional<Account>> changes;
        size_t const touched = 1 + rng() % 5;
        for (size_t i = 0; i < touched; ++i)
        {
            auto const& address = pool[rng() % pool.size()];
            if (!accounts.contains(address))
            {
                versions[address] = 0;
                changes[address] = makePruneAccount(0, 1000 + rng() % 1000);
            }
            else if (rng() % 3 == 0 && accounts.size() > 1)
            {
                changes[address] = std::nullopt;  // delete
            }
            else
            {
                changes[address] = makePruneAccount(++versions[address], 1000 + rng() % 1000);
            }
        }
        auto const delta = commitAccountBlock(backend, nodes, pruner, accounts, root, changes, block);
        root = delta.stateRoot;
        history.push_back(Version{.number = block, .root = root, .accounts = accounts});
        while (history.size() > static_cast<size_t>(N) + 1)
        {
            history.pop_front();
        }

        // (a) every root in [head−N, head] is fully resolvable and proves its accounts.
        std::unordered_set<bcos::h256> windowLive;
        for (auto const& version : history)
        {
            auto live = liveNodeHashes(nodes, version.root);
            windowLive.insert(live.begin(), live.end());
            size_t proven = 0;
            for (auto const& [address, account] : version.accounts)
            {
                auto proof = bcos::task::syncWait(generateProof(
                    nodes, version.root, address, std::span<bcos::h256 const>{}));
                BOOST_REQUIRE_MESSAGE(std::holds_alternative<EIP1186Proof>(proof),
                    "window root failed generateProof at block " << version.number);
                ++proven;
                if (proven >= 3)
                {
                    break;  // 3 proofs per version suffice; the walk above covers the rest
                }
            }
        }

        // (b) + (c): the block's prepare consumed every matured deletion, so on-disk rows ==
        // the window's live set.
        auto const onDisk = countRowsInTable(backend, bcos::storage2::kMPTTable);
        BOOST_CHECK_EQUAL(onDisk, windowLive.size());

        // Every armed deadline is future-dated (past-due schedules are consumed above).
        if (auto const earliest = pruner.nextPendingDeadline())
        {
            BOOST_CHECK_MESSAGE(*earliest > static_cast<uint64_t>(block),
                "past-due deletion survived the consumption pass");
        }
    }
}

BOOST_AUTO_TEST_CASE(OnCommitAdvancesWatermarkAfterBatchDeletion)
{
    // The production commit order (commitAccountBlock runs it inline): coPreparePruneRows'
    // deletions land with the block's WriteBatch; onCommit afterwards applies the block's
    // staged counting to the base tables and advances the in-memory watermark. The end state
    // matches what a deletion pass run to the same horizon produces.
    constexpr int64_t N = 2;
    PruneBackend backend;
    BackendNodeStorage nodes(backend);
    Pruner pruner(backend, N);
    std::map<bcos::Address, Account> accounts;

    bcos::h256 root = emptyRootHash();
    std::map<bcos::Address, uint64_t> versions;
    std::deque<std::pair<bcos::protocol::BlockNumber, bcos::h256>> history;
    for (bcos::protocol::BlockNumber block = 1; block <= 5; ++block)
    {
        std::map<bcos::Address, std::optional<Account>> changes;
        auto const address = makeAddress(static_cast<uint8_t>(block));
        versions[address] = 0;
        changes[address] = makePruneAccount(0, 7 * block);
        if (block >= 2)
        {
            auto const older = makeAddress(static_cast<uint8_t>(block - 1));
            changes[older] = makePruneAccount(++versions[older], 7 * block);
        }
        auto const delta = commitAccountBlock(backend, nodes, pruner, accounts, root, changes, block);
        root = delta.stateRoot;
        history.emplace_back(block, root);
    }
    BOOST_CHECK_EQUAL(pruner.watermark(), 5);

    // Deletion caught up to block 5 inline: on-disk nodes are exactly the live set of the
    // window [5−N, 5].
    std::unordered_set<bcos::h256> windowLive;
    for (auto const& [number, versionRoot] : history)
    {
        if (number >= 5 - N)
        {
            auto live = liveNodeHashes(nodes, versionRoot);
            windowLive.insert(live.begin(), live.end());
        }
    }
    BOOST_CHECK_EQUAL(countRowsInTable(backend, bcos::storage2::kMPTTable), windowLive.size());
}

BOOST_AUTO_TEST_CASE(TombstoneObsoletionCountedManually)
{
    // The MPTBuilder tombstone path is the ONE producer that writes refCountDeltas by hand (the
    // prior storage root's −1, MPTBuilder.h's finalizeAccount): drive prepare with exactly that
    // shape and verify the pruner schedules the storage root like any other obsoletion.
    PruneBackend backend;
    BackendNodeStorage nodes(backend);
    Pruner pruner(backend, /*pruneWindow=*/2);
    auto const storageRoot = makeHash(0x77);
    bcos::task::syncWait(nodes.writeOne(storageRoot, bcos::bytes{0xAA}));

    // The storage trie's own build counted its root at creation (+1 tallied by the build).
    MPTDeltaLayer delta1;
    delta1.refCountDeltas[storageRoot] = 1;
    delta1.newNodes[storageRoot] = bcos::bytes{0xAA};
    commitPruneBlock(backend, pruner, 1, delta1);
    BOOST_CHECK(pruner.countOf(storageRoot) == std::optional<uint64_t>{1});

    // Block 2: the account SELFDESTRUCTs — the tombstone's manual −1 with the obsoletion.
    MPTDeltaLayer delta2;
    delta2.refCountDeltas[storageRoot] = -1;
    delta2.obsoletedNodes.insert(storageRoot);
    commitPruneBlock(backend, pruner, 2, delta2);
    BOOST_CHECK(pruner.countOf(storageRoot) == std::optional<uint64_t>{0});
    BOOST_CHECK(pruner.deadlineOf(storageRoot) == std::optional<uint64_t>{4});
    runEmptyBlocks(backend, pruner, 3, 3);
    BOOST_CHECK(nodeRowExists(backend, storageRoot));  // inside the window
    runEmptyBlocks(backend, pruner, 4, 4);
    BOOST_CHECK(!nodeRowExists(backend, storageRoot));
    BOOST_CHECK(!pruner.countOf(storageRoot).has_value());

    // The saturating 0→0 of the same hand-built shape (no counted history at all): still
    // queued, still deleted at the deadline.
    auto const orphan = makeHash(0x78);
    bcos::task::syncWait(nodes.writeOne(orphan, bcos::bytes{0xBB}));
    MPTDeltaLayer delta3;
    delta3.refCountDeltas[orphan] = -1;
    delta3.obsoletedNodes.insert(orphan);
    commitPruneBlock(backend, pruner, 5, delta3);
    BOOST_CHECK(pruner.countOf(orphan) == std::optional<uint64_t>{0});
    BOOST_CHECK(pruner.deadlineOf(orphan) == std::optional<uint64_t>{7});
    runEmptyBlocks(backend, pruner, 6, 7);
    BOOST_CHECK(!nodeRowExists(backend, orphan));
    BOOST_CHECK_EQUAL(pruner.pendingCount(), 0U);
}

BOOST_AUTO_TEST_CASE(RebuildCountsCrossTrieSharing)
{
    // Phase 1 counts EVERY encounter: two accounts with byte-identical storage tries share the
    // same node hashes; the head-state walk descends into each account's storage trie, so every
    // shared node lands at count 2 — exactly what the per-block deltas tallied incrementally.
    PruneBackend backend;
    BackendNodeStorage nodes(backend);
    writeMptActivation(backend, /*activation=*/0);
    constexpr int64_t N = 3;
    Pruner pruner(backend, N);

    std::map<bcos::h256, std::optional<bcos::bytes>> const storageChanges{
        {makeHash(0x11), bcos::bytes{0x01}},
        {makeHash(0x22), bcos::bytes(40, 0x02)},
        {makeHash(0x33), bcos::bytes(20, 0x03)}};
    auto resultA = bcos::task::syncWait(commitTrie(nodes, emptyRootHash(), storageChanges));
    auto resultB = bcos::task::syncWait(commitTrie(nodes, emptyRootHash(), storageChanges));
    auto const storageRoot = resultA.root;
    BOOST_REQUIRE(resultB.root == storageRoot);

    auto accountWithStorage = [](uint64_t balance, bcos::h256 root) {
        Account account;
        account.balance = balance;
        account.storageRoot = root;
        return account;
    };
    auto const addressA = makeAddress(0x0A);
    auto const addressB = makeAddress(0x0B);
    std::map<bcos::h256, std::optional<bcos::bytes>> accountChanges{
        {accountKeyHash(addressA), accountWithStorage(100, storageRoot).encode()},
        {accountKeyHash(addressB), accountWithStorage(200, storageRoot).encode()}};
    auto accountResult = bcos::task::syncWait(commitTrie(nodes, emptyRootHash(), accountChanges));

    // The incremental side: one block carrying all three builds' emissions.
    MPTDeltaLayer delta;
    delta.stateRoot = accountResult.root;
    mergeNodeDelta(std::move(resultA), delta);
    mergeNodeDelta(std::move(resultB), delta);
    mergeNodeDelta(std::move(accountResult), delta);
    bcos::task::syncWait(flushTrieNodes(nodes, delta.newNodes));
    commitPruneBlock(backend, pruner, 1, delta);

    auto const storageLive = liveNodeHashes(nodes, storageRoot);
    auto const accountLive = liveNodeHashes(nodes, delta.stateRoot);
    BOOST_REQUIRE(!storageLive.empty());
    for (auto const& hash : storageLive)
    {
        BOOST_CHECK(pruner.countOf(hash) == std::optional<uint64_t>{2});
    }
    for (auto const& hash : accountLive)
    {
        BOOST_CHECK(pruner.countOf(hash) == std::optional<uint64_t>{1});
    }

    // The rebuild side: a fresh pruner over the same backend (the restart path) must arrive at
    // the same counts from the head root alone.
    Pruner rebuilt(backend, N);
    std::map<bcos::protocol::BlockNumber, bcos::h256> const roots{{1, delta.stateRoot}};
    bcos::task::syncWait(rebuilt.init(1, rootLookupOf(roots), /*sweepGarbage=*/false));
    BOOST_CHECK_EQUAL(rebuilt.trackedCount(), pruner.trackedCount());
    for (auto const& hash : storageLive)
    {
        BOOST_CHECK(rebuilt.countOf(hash) == std::optional<uint64_t>{2});
    }
    for (auto const& hash : accountLive)
    {
        BOOST_CHECK(rebuilt.countOf(hash) == std::optional<uint64_t>{1});
    }
    BOOST_CHECK_EQUAL(rebuilt.pendingCount(), 0U);
}

BOOST_AUTO_TEST_CASE(RebuildAfterRestartMatchesIncrementalState)
{
    // The restart path: commit a random workload with pruner A, then init a fresh pruner B over
    // the same backend — B's rebuilt counts and deadlines must equal A's incremental ones for
    // every tracked hash, and B must keep the window guarantee on subsequent blocks.
    constexpr int64_t N = 3;
    constexpr bcos::protocol::BlockNumber BLOCKS = 15;
    auto rng = seededRng(0xB00B);

    PruneBackend backend;
    BackendNodeStorage nodes(backend);
    writeMptActivation(backend, /*activation=*/0);
    Pruner prunerA(backend, N);

    std::vector<bcos::Address> pool;
    for (uint8_t i = 1; i <= 12; ++i)
    {
        pool.push_back(makeAddress(i));
    }
    std::map<bcos::Address, Account> accounts;
    std::map<bcos::Address, uint64_t> versions;
    std::map<bcos::protocol::BlockNumber, bcos::h256> roots;
    std::map<bcos::protocol::BlockNumber, std::map<bcos::Address, Account>> accountsAt;

    bcos::h256 root = emptyRootHash();
    for (bcos::protocol::BlockNumber block = 1; block <= BLOCKS; ++block)
    {
        std::map<bcos::Address, std::optional<Account>> changes;
        size_t const touched = 1 + rng() % 4;
        for (size_t i = 0; i < touched; ++i)
        {
            auto const& address = pool[rng() % pool.size()];
            if (!accounts.contains(address))
            {
                versions[address] = 0;
                changes[address] = makePruneAccount(0, 100 + rng() % 100);
            }
            else if (rng() % 4 == 0 && accounts.size() > 1)
            {
                changes[address] = std::nullopt;
            }
            else
            {
                changes[address] = makePruneAccount(++versions[address], 100 + rng() % 100);
            }
        }
        auto const delta =
            commitAccountBlock(backend, nodes, prunerA, accounts, root, changes, block);
        root = delta.stateRoot;
        roots[block] = root;
        accountsAt[block] = accounts;
    }

    // Restart: a fresh pruner rebuilds from the same backend and the recorded roots.
    Pruner prunerB(backend, N);
    bcos::task::syncWait(prunerB.init(BLOCKS, rootLookupOf(roots), /*sweepGarbage=*/false));
    BOOST_CHECK_EQUAL(prunerB.watermark(), BLOCKS);

    // Every hash tracked by either pruner lives in the union of the window's live sets
    // (scheduled nodes are still referenced by some in-window root until their deletion runs).
    std::unordered_set<bcos::h256> windowLive;
    for (bcos::protocol::BlockNumber block = BLOCKS - N; block <= BLOCKS; ++block)
    {
        auto live = liveNodeHashes(nodes, roots[block]);
        windowLive.insert(live.begin(), live.end());
    }
    BOOST_CHECK_EQUAL(prunerA.trackedCount(), windowLive.size());
    BOOST_CHECK_EQUAL(prunerB.trackedCount(), windowLive.size());
    for (auto const& hash : windowLive)
    {
        auto const countA = prunerA.countOf(hash);
        auto const countB = prunerB.countOf(hash);
        BOOST_REQUIRE_MESSAGE(countA.has_value() && countB.has_value(),
            "tracked hash missing from a pruner's counts");
        BOOST_CHECK_EQUAL(*countB, *countA);
        BOOST_CHECK(prunerB.deadlineOf(hash) == prunerA.deadlineOf(hash));
    }
    BOOST_CHECK_EQUAL(prunerB.pendingCount(), prunerA.pendingCount());

    // B keeps the window guarantee on the blocks after the restart: more churn, and every
    // in-window root still proves.
    for (bcos::protocol::BlockNumber block = BLOCKS + 1; block <= BLOCKS + 5; ++block)
    {
        std::map<bcos::Address, std::optional<Account>> changes;
        auto const& address = pool[rng() % pool.size()];
        if (!accounts.contains(address))
        {
            versions[address] = 0;
        }
        changes[address] = makePruneAccount(++versions[address], 100 + rng() % 100);
        auto const delta =
            commitAccountBlock(backend, nodes, prunerB, accounts, root, changes, block);
        root = delta.stateRoot;
        roots[block] = root;
        accountsAt[block] = accounts;

        std::unordered_set<bcos::h256> live;
        for (bcos::protocol::BlockNumber v = block - N; v <= block; ++v)
        {
            auto versionLive = liveNodeHashes(nodes, roots[v]);
            live.insert(versionLive.begin(), versionLive.end());
            size_t proven = 0;
            for (auto const& [address, account] : accountsAt[v])
            {
                auto proof = bcos::task::syncWait(generateProof(
                    nodes, roots[v], address, std::span<bcos::h256 const>{}));
                BOOST_REQUIRE_MESSAGE(std::holds_alternative<EIP1186Proof>(proof),
                    "post-restart window root failed generateProof at block " << v);
                if (++proven >= 2)
                {
                    break;
                }
            }
        }
        BOOST_CHECK_EQUAL(countRowsInTable(backend, bcos::storage2::kMPTTable), live.size());
    }
}

BOOST_AUTO_TEST_CASE(RebuildDeletesSmallGarbageAtStartup)
{
    // Phase 3 with sweepGarbage=true: unreachable garbage is deleted during init — never
    // scheduled into the queue, never counted.
    PruneBackend backend;
    BackendNodeStorage nodes(backend);
    writeMptActivation(backend, /*activation=*/0);
    constexpr int64_t N = 2;
    Pruner prunerA(backend, N);

    std::map<bcos::Address, Account> accounts;
    std::map<bcos::protocol::BlockNumber, bcos::h256> roots;
    bcos::h256 root = emptyRootHash();
    for (bcos::protocol::BlockNumber block = 1; block <= 3; ++block)
    {
        std::map<bcos::Address, std::optional<Account>> changes;
        changes[makeAddress(static_cast<uint8_t>(block))] = makePruneAccount(0, 10 * block);
        auto const delta = commitAccountBlock(backend, nodes, prunerA, accounts, root, changes, block);
        root = delta.stateRoot;
        roots[block] = root;
    }

    // Three garbage rows no trie references (junk payloads — the sweep deletes by key and
    // never decodes them).
    std::vector<bcos::h256> const garbage{makeHash(0x61), makeHash(0x62), makeHash(0x63)};
    for (auto const& hash : garbage)
    {
        bcos::task::syncWait(nodes.writeOne(hash, bcos::bytes(8, 0x66)));
    }

    // Restart with the sweep enabled: init rebuilds and deletes the garbage on the spot.
    Pruner prunerB(backend, N);
    bcos::task::syncWait(prunerB.init(3, rootLookupOf(roots), /*sweepGarbage=*/true));
    for (auto const& hash : garbage)
    {
        BOOST_CHECK(!nodeRowExists(backend, hash));
        BOOST_CHECK(!prunerB.countOf(hash).has_value());
        BOOST_CHECK(!prunerB.deadlineOf(hash).has_value());
    }
    BOOST_CHECK_EQUAL(prunerB.lastSweepDeleted(), 3U);

    // The ordinary per-block consumption still works afterwards: at head 4 the window is
    // [2, 4], so root 1's unique nodes (deadline 4) leave with it. On disk remains exactly the
    // new window's live set (block 4 carried no delta, so its root equals block 3's).
    runEmptyBlocks(backend, prunerB, 4, 4);
    std::unordered_set<bcos::h256> windowLive;
    for (bcos::protocol::BlockNumber block = 2; block <= 3; ++block)
    {
        auto live = liveNodeHashes(nodes, roots[block]);
        windowLive.insert(live.begin(), live.end());
    }
    BOOST_CHECK_EQUAL(countRowsInTable(backend, bcos::storage2::kMPTTable), windowLive.size());
    // Everything still queued matures strictly after the consumed block.
    if (auto const earliest = prunerB.nextPendingDeadline())
    {
        BOOST_CHECK(*earliest > 4U);
    }
}

BOOST_AUTO_TEST_CASE(StartupSweepDisabled)
{
    // sweepGarbage=false (the config default): Phase 3 is SKIPPED outright — no full-table
    // "/mpt/" scan at boot, so the garbage count is not even known (the boot log carries the
    // unconditional hint that historical garbage may exist and how to enable the sweep). Not
    // one row is deleted; the garbage is only ever reclaimed by a sweep-enabled restart.
    PruneBackend backend;
    BackendNodeStorage nodes(backend);
    writeMptActivation(backend, /*activation=*/0);
    constexpr int64_t N = 2;

    std::map<bcos::Address, Account> accounts;
    std::map<bcos::protocol::BlockNumber, bcos::h256> roots;
    bcos::h256 root = emptyRootHash();
    {
        Pruner seeder(backend, N);
        std::map<bcos::Address, std::optional<Account>> changes;
        changes[makeAddress(0x01)] = makePruneAccount(0, 1);
        root = commitAccountBlock(backend, nodes, seeder, accounts, root, changes, 1).stateRoot;
        roots[1] = root;
    }
    // Garbage no trie references, including a MALFORMED row the Phase-3 scan would reject with
    // a WARNING — with the scan skipped, init passes over all of it untouched (and silently:
    // nothing decodes or even reads those rows).
    constexpr size_t garbageCount = 3;
    writeGarbageRows(backend, garbageCount);
    {
        bcos::storage::Entry entry;
        entry.set(bcos::bytes(4, 0x66));
        bcos::task::syncWait(bcos::storage2::writeOne(backend,
            bcos::executor_v1::StateKey{bcos::storage2::kMPTTable, std::string_view{"short"}},
            std::move(entry)));
    }
    auto const rowsBefore = countRowsInTable(backend, bcos::storage2::kMPTTable);

    Pruner pruner(backend, N);
    bcos::task::syncWait(pruner.init(1, rootLookupOf(roots), /*sweepGarbage=*/false));
    BOOST_CHECK_EQUAL(pruner.lastSweepDeleted(), 0U);
    BOOST_CHECK_EQUAL(countRowsInTable(backend, bcos::storage2::kMPTTable), rowsBefore);
    BOOST_CHECK(nodeRowExists(backend, garbageHash(0)));
    BOOST_CHECK(nodeRowExists(backend, garbageHash(garbageCount - 1)));
}

BOOST_AUTO_TEST_CASE(StartupSweepEnabled)
{
    // sweepGarbage=true: all garbage is deleted WHILE the scan runs, in SWEEP_DELETE_CHUNK
    // batches — the progress callback fires per batch (done cumulative, total = garbage found
    // so far, ending exactly at the final count) and observes the intermediate state: the
    // first chunk's rows already gone, the not-yet-scanned remainder still on disk.
    PruneBackend backend;
    BackendNodeStorage nodes(backend);
    writeMptActivation(backend, /*activation=*/0);
    constexpr int64_t N = 2;

    std::map<bcos::Address, Account> accounts;
    std::map<bcos::protocol::BlockNumber, bcos::h256> roots;
    bcos::h256 root = emptyRootHash();
    {
        Pruner seeder(backend, N);
        std::map<bcos::Address, std::optional<Account>> changes;
        changes[makeAddress(0x01)] = makePruneAccount(0, 1);
        root = commitAccountBlock(backend, nodes, seeder, accounts, root, changes, 1).stateRoot;
        roots[1] = root;
    }
    constexpr size_t garbageCount = Pruner::SWEEP_DELETE_CHUNK + 1;
    writeGarbageRows(backend, garbageCount);

    std::vector<std::pair<uint64_t, uint64_t>> progress;
    Pruner pruner(backend, N);
    bcos::task::syncWait(pruner.init(1, rootLookupOf(roots), /*sweepGarbage=*/true,
        [&progress, &backend, garbageCount](uint64_t done, uint64_t total) {
            progress.emplace_back(done, total);
            if (done == Pruner::SWEEP_DELETE_CHUNK)
            {
                // Mid-scan intermediate state: the first chunk (garbageHash 0..9999 — the
                // garbage keys order by their trailing counter) is already deleted, the last
                // garbage row is still on disk awaiting the scan.
                BOOST_CHECK(!nodeRowExists(backend, garbageHash(0)));
                BOOST_CHECK(
                    !nodeRowExists(backend, garbageHash(Pruner::SWEEP_DELETE_CHUNK - 1)));
                BOOST_CHECK(nodeRowExists(backend, garbageHash(garbageCount - 1)));
            }
        }));
    BOOST_REQUIRE_EQUAL(progress.size(), 2U);  // 10001 = one full chunk + a remainder of one
    BOOST_CHECK_EQUAL(progress[0].first, Pruner::SWEEP_DELETE_CHUNK);
    BOOST_CHECK_EQUAL(progress[0].second, Pruner::SWEEP_DELETE_CHUNK);
    BOOST_CHECK_EQUAL(progress[1].first, garbageCount);
    BOOST_CHECK_EQUAL(progress[1].second, garbageCount);
    BOOST_CHECK_EQUAL(pruner.lastSweepDeleted(), garbageCount);

    for (uint32_t i = 0; i < garbageCount; ++i)
    {
        BOOST_CHECK(!nodeRowExists(backend, garbageHash(i)));
    }
    // The live trie is untouched.
    BOOST_CHECK_NO_THROW(liveNodeHashes(nodes, root));
}

BOOST_AUTO_TEST_CASE(RebuildTruncatesAtScenarioAActivation)
{
    // Scenario A: feature_mpt_state_root activates at block 5, so the first MPT block is 6.
    // Roots of blocks <= 5 are legacy XOR roots — the rebuild must NOT walk them even when the
    // window reaches past the activation (a large N would otherwise try, and fail loudly on the
    // junk "root" below).
    PruneBackend backend;
    BackendNodeStorage nodes(backend);
    writeMptActivation(backend, /*activation=*/5);
    constexpr int64_t N = 100;  // window would reach block -93 without the activation cutoff
    Pruner prunerA(backend, N);

    std::map<bcos::Address, Account> accounts;
    std::map<bcos::protocol::BlockNumber, bcos::h256> roots;
    roots[5] = makeHash(0x5A);  // pre-activation "root": junk, no node row behind it
    bcos::h256 root = emptyRootHash();
    for (bcos::protocol::BlockNumber block = 6; block <= 8; ++block)
    {
        std::map<bcos::Address, std::optional<Account>> changes;
        changes[makeAddress(static_cast<uint8_t>(block))] = makePruneAccount(0, block);
        auto const delta = commitAccountBlock(backend, nodes, prunerA, accounts, root, changes, block);
        root = delta.stateRoot;
        roots[block] = root;
    }

    Pruner prunerB(backend, N);
    BOOST_CHECK_NO_THROW(bcos::task::syncWait(
        prunerB.init(8, rootLookupOf(roots), /*sweepGarbage=*/false)));

    // The rebuilt counts cover every node of blocks 6..8 — the whole post-activation history.
    std::unordered_set<bcos::h256> allLive;
    for (bcos::protocol::BlockNumber block = 6; block <= 8; ++block)
    {
        auto live = liveNodeHashes(nodes, roots[block]);
        allLive.insert(live.begin(), live.end());
    }
    BOOST_CHECK_EQUAL(prunerB.trackedCount(), allLive.size());
    for (auto const& hash : allLive)
    {
        BOOST_CHECK(prunerB.countOf(hash) == prunerA.countOf(hash));
        BOOST_CHECK(prunerB.deadlineOf(hash) == prunerA.deadlineOf(hash));
    }
}

BOOST_AUTO_TEST_CASE(RebuildAfterWindowWideningSkipsPrunedRoots)
{
    // Round-5 F2: the chain ran with a SMALLER window, so the roots below the old window are
    // already deleted (root(b)'s row leaves at b+1+N_old). Restarting with a LARGER N must not
    // fail the rebuild on those missing roots: Phase 2 probes each candidate root's row, stops
    // the downward walk at the first pruned root and treats the first SURVIVING root as the
    // effective window start — widening recovers only what is still on disk.
    PruneBackend backend;
    BackendNodeStorage nodes(backend);
    writeMptActivation(backend, /*activation=*/0);
    constexpr int64_t OLD_N = 2;
    Pruner prunerA(backend, OLD_N);

    std::map<bcos::Address, Account> accounts;
    std::map<bcos::protocol::BlockNumber, bcos::h256> roots;
    bcos::h256 root = emptyRootHash();
    constexpr bcos::protocol::BlockNumber HEAD = 6;
    for (bcos::protocol::BlockNumber block = 1; block <= HEAD; ++block)
    {
        std::map<bcos::Address, std::optional<Account>> changes;
        changes[makeAddress(static_cast<uint8_t>(block))] = makePruneAccount(0, 10 * block);
        auto const delta =
            commitAccountBlock(backend, nodes, prunerA, accounts, root, changes, block);
        root = delta.stateRoot;
        roots[block] = root;
    }
    // root(b) left at b+1+OLD_N: roots 1..3 are gone, 4..6 survive.
    for (bcos::protocol::BlockNumber block = 1; block <= 3; ++block)
    {
        BOOST_REQUIRE(!nodeRowExists(backend, roots[block]));
    }
    for (bcos::protocol::BlockNumber block = 4; block <= HEAD; ++block)
    {
        BOOST_REQUIRE(nodeRowExists(backend, roots[block]));
    }

    // Restart with a WIDER window: the rebuild walk would reach back to block 1, but probes the
    // pruned root of block 3 and stops there instead of throwing MPTInvariantViolation.
    constexpr int64_t NEW_N = 5;
    Pruner prunerB(backend, NEW_N);
    BOOST_CHECK_NO_THROW(
        bcos::task::syncWait(prunerB.init(HEAD, rootLookupOf(roots), /*sweepGarbage=*/false)));

    // The effective window starts at the first surviving root: every node of roots 4..6 is
    // tracked (counted or scheduled); nothing from the pruned roots can be resurrected.
    std::unordered_set<bcos::h256> windowLive;
    for (bcos::protocol::BlockNumber block = 4; block <= HEAD; ++block)
    {
        auto live = liveNodeHashes(nodes, roots[block]);
        windowLive.insert(live.begin(), live.end());
    }
    BOOST_CHECK_EQUAL(prunerB.trackedCount(), windowLive.size());

    // Deadlines carry the NEW window: the oldest surviving root's unique nodes mature at
    // 4+1+NEW_N — the first deadline in the queue.
    BOOST_REQUIRE(prunerB.nextPendingDeadline().has_value());
    BOOST_CHECK_EQUAL(*prunerB.nextPendingDeadline(), 4 + 1 + NEW_N);

    // The surviving in-window roots still prove after the widened restart.
    auto proof = bcos::task::syncWait(
        generateProof(nodes, roots[4], makeAddress(0x01), std::span<bcos::h256 const>{}));
    BOOST_CHECK(std::holds_alternative<EIP1186Proof>(proof));
}

BOOST_AUTO_TEST_CASE(ShrinkChurnWidenRebuildsCleanly)
{
    // Review-pinned regression (the analysis concluded the feared failure is unreachable; this
    // test nails that down): run with N=5, restart SHRUNK to N=2 right after state-unchanged
    // blocks (the last roots equal the head root), commit three churn blocks whose deletions
    // include nodes a WIDER window would still walk, then restart widened to N=5 again. The
    // churn that deleted a shared node X also deleted the churn-eve root row with the SAME
    // deadline, so Phase 2's newest-first root-row probe hits that pruned root first and stops
    // the walk before ever resolving X — the fail-loud deadlineWalk stays unreachable here.
    PruneBackend backend;
    BackendNodeStorage nodes(backend);
    writeMptActivation(backend, /*activation=*/0);

    std::map<bcos::Address, Account> accounts;
    std::map<bcos::protocol::BlockNumber, bcos::h256> roots;
    std::map<bcos::protocol::BlockNumber, std::map<bcos::Address, Account>> accountsAt;
    bcos::h256 root = emptyRootHash();

    // Phase A: N=5, six churn blocks — every block adds a fresh account and rewrites the
    // previous one (a new balance changes its leaf), so each root is a new trie version.
    constexpr int64_t WIDE_N = 5;
    {
        Pruner prunerA(backend, WIDE_N);
        for (bcos::protocol::BlockNumber block = 1; block <= 6; ++block)
        {
            std::map<bcos::Address, std::optional<Account>> changes;
            changes[makeAddress(static_cast<uint8_t>(block))] = makePruneAccount(0, 10 * block);
            if (block > 1)
            {
                changes[makeAddress(static_cast<uint8_t>(block - 1))] =
                    makePruneAccount(1, 10 * block);
            }
            root = commitAccountBlock(backend, nodes, prunerA, accounts, root, changes, block)
                       .stateRoot;
            roots[block] = root;
            accountsAt[block] = accounts;
        }
        // Two state-unchanged blocks: roots 7 and 8 equal the head root — the shrink restart
        // below sees a head whose newest roots carry no new trie version.
        runEmptyBlocks(backend, prunerA, 7, 8);
        roots[7] = root;
        roots[8] = root;
        accountsAt[7] = accounts;
        accountsAt[8] = accounts;
    }

    // Restart SHRUNK to N=2 at head 8: the rebuild walks only roots 6..8 (all the same hash) —
    // clean. Then three churn blocks under the shrunk window: block 9's rebuild obsoletes the
    // root-6 version (referenced by roots 6..8) — its root row and every node unique to it are
    // scheduled with the SAME deadline 9+2 = 11 and deleted together at block 11.
    constexpr int64_t SHRUNK_N = 2;
    {
        Pruner prunerB(backend, SHRUNK_N);
        BOOST_CHECK_NO_THROW(
            bcos::task::syncWait(prunerB.init(8, rootLookupOf(roots), /*sweepGarbage=*/false)));
        for (bcos::protocol::BlockNumber block = 9; block <= 11; ++block)
        {
            std::map<bcos::Address, std::optional<Account>> changes;
            changes[makeAddress(static_cast<uint8_t>(block))] = makePruneAccount(0, 10 * block);
            changes[makeAddress(static_cast<uint8_t>(block - 3))] =
                makePruneAccount(2, 10 * block);
            root = commitAccountBlock(backend, nodes, prunerB, accounts, root, changes, block)
                       .stateRoot;
            roots[block] = root;
            accountsAt[block] = accounts;
        }
    }
    // The churn-eve root row left on schedule — the widened walk below probes into it first.
    BOOST_REQUIRE(!nodeRowExists(backend, roots[6]));

    // Restart WIDENED back to N=5 at head 11: Phase 2 probes downward from block 10 — roots
    // 10..9 resolve, block 8's root (== root 6) is already pruned, so the walk stops there
    // instead of resolving (and failing loud on) the nodes block 11 deleted.
    Pruner prunerC(backend, WIDE_N);
    BOOST_CHECK_NO_THROW(
        bcos::task::syncWait(prunerC.init(11, rootLookupOf(roots), /*sweepGarbage=*/false)));

    // The effective window is the surviving roots 9..11: exactly their nodes are tracked (the
    // oldest one's unique nodes carry the WIDE deadline 9+1+5, the first in the queue) and
    // every in-window root still proves its accounts.
    std::unordered_set<bcos::h256> windowLive;
    for (bcos::protocol::BlockNumber block = 9; block <= 11; ++block)
    {
        auto live = liveNodeHashes(nodes, roots[block]);
        windowLive.insert(live.begin(), live.end());
        size_t proven = 0;
        for (auto const& [address, account] : accountsAt[block])
        {
            auto proof = bcos::task::syncWait(
                generateProof(nodes, roots[block], address, std::span<bcos::h256 const>{}));
            BOOST_REQUIRE_MESSAGE(std::holds_alternative<EIP1186Proof>(proof),
                "in-window root failed generateProof at block " << block);
            if (++proven >= 3)
            {
                break;
            }
        }
    }
    BOOST_CHECK_EQUAL(prunerC.trackedCount(), windowLive.size());
    BOOST_REQUIRE(prunerC.nextPendingDeadline().has_value());
    BOOST_CHECK_EQUAL(*prunerC.nextPendingDeadline(), 9 + 1 + WIDE_N);
}

BOOST_AUTO_TEST_CASE(RebuildSkippedBeforeActivationThenDeltaSeeds)
{
    // A chain whose MPT is not yet active at boot: init skips the rebuild and starts empty;
    // the activation block's first full build (FlatToMPT) emits every node as that block's
    // newNodes — the ordinary counting path seeds the counts.
    PruneBackend backend;
    BackendNodeStorage nodes(backend);
    constexpr int64_t N = 2;
    Pruner pruner(backend, N);

    // No feature rows at all (pre-activation head): nothing to rebuild.
    std::map<bcos::protocol::BlockNumber, bcos::h256> const emptyRoots;
    bcos::task::syncWait(pruner.init(2, rootLookupOf(emptyRoots), /*sweepGarbage=*/false));
    BOOST_CHECK_EQUAL(pruner.trackedCount(), 0U);
    BOOST_CHECK_EQUAL(pruner.pendingCount(), 0U);
    BOOST_CHECK_EQUAL(pruner.watermark(), 2);

    // Governance activates the feature at block 2 → block 3 is the first MPT block, a full
    // build from emptyRootHash() emitting every node.
    writeMptActivation(backend, /*activation=*/2);
    std::map<bcos::h256, std::optional<bcos::bytes>> changes;
    std::vector<bcos::Address> const addresses{makeAddress(0x01), makeAddress(0x02)};
    for (auto const& address : addresses)
    {
        changes[accountKeyHash(address)] = makePruneAccount(0, 42).encode();
    }
    auto result = bcos::task::syncWait(commitTrie(nodes, emptyRootHash(), changes));
    MPTDeltaLayer delta;
    delta.stateRoot = result.root;
    mergeNodeDelta(std::move(result), delta);
    bcos::task::syncWait(flushTrieNodes(nodes, delta.newNodes));
    commitPruneBlock(backend, pruner, 3, delta);

    auto const live = liveNodeHashes(nodes, delta.stateRoot);
    BOOST_REQUIRE(!live.empty());
    BOOST_CHECK_EQUAL(pruner.trackedCount(), live.size());
    for (auto const& hash : live)
    {
        BOOST_CHECK(pruner.countOf(hash) == std::optional<uint64_t>{1});
    }

    // And a later restart rebuilds the same counts from disk.
    Pruner rebuilt(backend, N);
    std::map<bcos::protocol::BlockNumber, bcos::h256> const roots{{3, delta.stateRoot}};
    bcos::task::syncWait(rebuilt.init(3, rootLookupOf(roots), /*sweepGarbage=*/false));
    BOOST_CHECK_EQUAL(rebuilt.trackedCount(), live.size());
    for (auto const& hash : live)
    {
        BOOST_CHECK(rebuilt.countOf(hash) == std::optional<uint64_t>{1});
    }
}

BOOST_AUTO_TEST_CASE(RebuildFailsLoudOnMissingNode)
{
    // The trie is the source of truth: a reachable node row missing from the backend violates
    // the window guarantee the rebuild relies on — init throws rather than rebuild a lie.
    PruneBackend backend;
    BackendNodeStorage nodes(backend);
    writeMptActivation(backend, /*activation=*/0);
    Pruner pruner(backend, /*pruneWindow=*/2);

    std::map<bcos::protocol::BlockNumber, bcos::h256> const roots{{1, makeHash(0x01)}};
    BOOST_CHECK_THROW(
        bcos::task::syncWait(pruner.init(1, rootLookupOf(roots), /*sweepGarbage=*/false)), MPTInvariantViolation);
}

BOOST_AUTO_TEST_CASE(NewNodesGuardKeepsQueueEntryForRecheck)
{
    // The belt-and-braces newNodes guard, unreachable under correct accounting: a matured
    // queue entry whose count is 0 with the matching deadline, but whose hash ALSO appears in
    // the expiry block's newNodes. The deletion is skipped WITHOUT consuming the queue entry —
    // the next block's prepare re-checks it and (with a clean delta) confirms it. Consuming
    // the entry anyway would orphan the count entry ({count 0, expired deadline}, referenced
    // by no bucket) until a restart.
    PruneBackend backend;
    BackendNodeStorage nodes(backend);
    Pruner pruner(backend, /*pruneWindow=*/2);
    auto const h1 = makeHash(0x01);
    bcos::task::syncWait(nodes.writeOne(h1, bcos::bytes{0x11}));

    // Counted history: created at block 1, obsoleted at block 2 → deadline 4.
    MPTDeltaLayer delta1;
    delta1.refCountDeltas[h1] = 1;
    delta1.newNodes[h1] = bcos::bytes{0x11};
    commitPruneBlock(backend, pruner, 1, delta1);
    MPTDeltaLayer delta2;
    delta2.refCountDeltas[h1] = -1;
    delta2.obsoletedNodes.insert(h1);
    commitPruneBlock(backend, pruner, 2, delta2);
    BOOST_REQUIRE(pruner.countOf(h1) == std::optional<uint64_t>{0});
    BOOST_REQUIRE(pruner.deadlineOf(h1) == std::optional<uint64_t>{4});
    BOOST_REQUIRE_EQUAL(pruner.pendingCount(), 1U);

    // Block 4, the expiry block: a hand-built delta whose tally nets h1 to no movement (the
    // count stays 0 — a fully EMPTY tally together with node changes would trip the fail-loud
    // wiring check) but whose newNodes lists h1 — the shape correct accounting never produces,
    // since an emission implies a positive post-block count. prepare must NOT delete, and must
    // NOT consume the queue entry.
    MPTDeltaLayer delta4;
    delta4.refCountDeltas[h1] = 0;
    delta4.newNodes[h1] = bcos::bytes{0x11};
    auto batch4 = bcos::task::syncWait(pruner.coPreparePruneRows(4, delta4));
    BOOST_CHECK(batch4.deletions.empty());
    pruner.onCommit(4, delta4);
    BOOST_CHECK(nodeRowExists(backend, h1));
    // The base entry is not orphaned: still {count 0, deadline 4}, still referenced by the
    // queue.
    BOOST_CHECK(pruner.countOf(h1) == std::optional<uint64_t>{0});
    BOOST_CHECK(pruner.deadlineOf(h1) == std::optional<uint64_t>{4});
    BOOST_CHECK_EQUAL(pruner.pendingCount(), 1U);

    // The next block's prepare re-checks the kept entry: with a clean delta the matured
    // deletion confirms normally.
    auto batch5 = bcos::task::syncWait(pruner.coPreparePruneRows(5, MPTDeltaLayer{}));
    BOOST_REQUIRE_EQUAL(batch5.deletions.size(), 1U);
    bcos::task::syncWait(bcos::storage2::removeSome(backend, std::move(batch5.deletions)));
    pruner.onCommit(5, MPTDeltaLayer{});
    BOOST_CHECK(!nodeRowExists(backend, h1));
    BOOST_CHECK(!pruner.countOf(h1).has_value());
    BOOST_CHECK_EQUAL(pruner.pendingCount(), 0U);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::ledger::mpt::test
