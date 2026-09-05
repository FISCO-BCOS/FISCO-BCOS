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
 * @brief MPTPruner: refcount transition rules, cross-trie sharing, windowed deletion
 *        invariants over random workloads, the startup guard, genesis seeding and the
 *        tombstone path's manual counting (spec §4.8)
 */
#include "TestHelpers.h"
#include <bcos-framework/ledger/Features.h>
#include <bcos-framework/ledger/FeaturesStorage.h>
#include <bcos-ledger/GenesisStateRoot.h>
#include <bcos-ledger/mpt/CommitObserver.h>
#include <bcos-ledger/mpt/MPTPruner.h>
#include <bcos-ledger/mpt/Proof.h>
#include <bcos-ledger/mpt/PruneMetadata.h>
#include <bcos-ledger/mpt/Trie.h>
#include <bcos-storage/KeyPrefixes.h>
#include <bcos-tool/Exceptions.h>
#include <boost/test/unit_test.hpp>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace bcos::ledger::mpt::test
{

BOOST_AUTO_TEST_SUITE(MPTPrunerSuite)

namespace
{
/// The committed-state backend: ordered (the deletion pass prefix-scans the queue table with
/// RANGE_SEEK) and physically deleting (no LOGICAL_DELETION attribute), matching RocksDB.
using PruneBackend = bcos::storage2::memory_storage::MemoryStorage<bcos::executor_v1::StateKey,
    bcos::executor_v1::StateValue, bcos::storage2::memory_storage::ORDERED>;

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

std::optional<PruneRefCount> readRefCountRow(PruneBackend& backend, bcos::h256 const& hash)
{
    auto entry = bcos::task::syncWait(bcos::storage2::readOne(backend, pruneRefKey(hash)));
    if (!entry)
    {
        return std::nullopt;
    }
    auto raw = entry->get();
    return decodeRefCount(
        bcos::bytesConstRef(reinterpret_cast<bcos::byte const*>(raw.data()), raw.size()));
}

std::optional<uint64_t> readWatermarkRow(PruneBackend& backend)
{
    auto entry = bcos::task::syncWait(bcos::storage2::readOne(backend, watermarkKey()));
    if (!entry)
    {
        return std::nullopt;
    }
    auto raw = entry->get();
    return decodeWatermark(
        bcos::bytesConstRef(reinterpret_cast<bcos::byte const*>(raw.data()), raw.size()));
}

std::optional<uint64_t> readWindowRow(PruneBackend& backend)
{
    auto entry = bcos::task::syncWait(bcos::storage2::readOne(backend, windowKey()));
    if (!entry)
    {
        return std::nullopt;
    }
    auto raw = entry->get();
    return decodeWatermark(
        bcos::bytesConstRef(reinterpret_cast<bcos::byte const*>(raw.data()), raw.size()));
}

bool nodeRowExists(PruneBackend& backend, bcos::h256 const& hash)
{
    return bcos::task::syncWait(
        bcos::storage2::existsOne(backend, bcos::ledger::mptNodeStateKey(hash)));
}

Account makePruneAccount(uint64_t nonce, uint64_t balance)
{
    Account account;  // storageRoot=emptyRootHash(), codeHash=emptyCodeHash() by default
    account.nonce = nonce;
    account.balance = balance;
    return account;
}

/// Prepare block @p blockNumber's pruning batch over @p delta and apply it to @p backend in
/// the commit flow's order (upserts then deletions — the stand-in for the block's single
/// WriteBatch, BaselineScheduler-tpp.h's writeSome + removeSome onto prewriteStorage).
void commitPruneBlock(PruneBackend& backend, MPTPruner<PruneBackend>& pruner,
    bcos::protocol::BlockNumber blockNumber, MPTDeltaLayer const& delta)
{
    auto batch = bcos::task::syncWait(pruner.coPreparePruneRows(blockNumber, delta));
    bcos::task::syncWait(bcos::storage2::writeSome(backend, std::move(batch.rows)));
    if (!batch.deletions.empty())
    {
        bcos::task::syncWait(bcos::storage2::removeSome(backend, std::move(batch.deletions)));
    }
}

/// Blocks with no trie delta: the watermark row still lands with each one, and the delete
/// queue is consumed up to each block — how a matured deletion or a stale queue row drains
/// without new account churn.
void runEmptyBlocks(PruneBackend& backend, MPTPruner<PruneBackend>& pruner,
    bcos::protocol::BlockNumber from, bcos::protocol::BlockNumber to)
{
    for (auto block = from; block <= to; ++block)
    {
        commitPruneBlock(backend, pruner, block, MPTDeltaLayer{});
    }
}

/// One block of the account-trie chain the pruning tests drive, in the production commit order:
/// build the trie delta → flush its nodes → prepare + apply the pruning batch (metadata rows
/// AND the deletions of expired nodes, the prewriteStorage stand-in). Returns the delta so the
/// caller can feed onCommit itself.
MPTDeltaLayer commitAccountBlock(PruneBackend& backend, BackendNodeStorage& nodes,
    MPTPruner<PruneBackend>& pruner, std::map<bcos::Address, Account>& accounts,
    bcos::h256 priorRoot,
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
    BOOST_CHECK(batch.rows.empty());
    BOOST_CHECK(batch.deletions.empty());
}

BOOST_AUTO_TEST_CASE(NewNodesCreateRefCountRowsAndWatermark)
{
    PruneBackend backend;
    BackendNodeStorage nodes(backend);
    MPTPruner<PruneBackend> pruner(backend, /*pruneWindow=*/5);

    auto const h1 = makeHash(0x01);
    auto const h2 = makeHash(0x02);
    MPTDeltaLayer delta;
    delta.newNodes[h1] = bcos::bytes{0x11};
    delta.newNodes[h2] = bcos::bytes{0x22};

    // Hand-built delta: refCountDeltas is empty, so the set-based fallback (+1 per newNodes
    // hash) is what runs here.
    auto batch = bcos::task::syncWait(pruner.coPreparePruneRows(7, delta));
    // 2 refcount rows + the watermark row + the window fingerprint row; no queue rows, nothing
    // expired to delete.
    BOOST_CHECK_EQUAL(batch.rows.size(), 4U);
    BOOST_CHECK(batch.deletions.empty());
    bcos::task::syncWait(bcos::storage2::writeSome(backend, std::move(batch.rows)));

    BOOST_CHECK((readRefCountRow(backend, h1) == PruneRefCount{.count = 1}));
    BOOST_CHECK((readRefCountRow(backend, h2) == PruneRefCount{.count = 1}));
    BOOST_REQUIRE(readWatermarkRow(backend).has_value());
    BOOST_CHECK_EQUAL(*readWatermarkRow(backend), 7U);
    BOOST_CHECK_EQUAL(countRowsInTable(backend, kPruneQueueTable), 0U);
}

BOOST_AUTO_TEST_CASE(ObsoletionToZeroQueuesDeletion)
{
    PruneBackend backend;
    BackendNodeStorage nodes(backend);
    MPTPruner<PruneBackend> pruner(backend, /*pruneWindow=*/5);
    auto const h1 = makeHash(0x01);

    MPTDeltaLayer delta1;
    delta1.newNodes[h1] = bcos::bytes{0x11};
    commitPruneBlock(backend, pruner, 1, delta1);

    MPTDeltaLayer delta2;
    delta2.obsoletedNodes.insert(h1);
    commitPruneBlock(backend, pruner, 2, delta2);

    // Count went 1→0: scheduled at 2+5=7, one queue row, watermark advanced.
    BOOST_CHECK((readRefCountRow(backend, h1) == PruneRefCount{.count = 0, .pendingDeleteAt = 7}));
    BOOST_CHECK_EQUAL(countRowsInTable(backend, kPruneQueueTable), 1U);
    BOOST_CHECK(nodeRowExists(backend, h1) == false);  // this test never wrote a node row
    BOOST_CHECK_EQUAL(*readWatermarkRow(backend), 2U);
}

BOOST_AUTO_TEST_CASE(SharedRefCountSurvivesSingleObsoletion)
{
    PruneBackend backend;
    BackendNodeStorage nodes(backend);
    MPTPruner<PruneBackend> pruner(backend, /*pruneWindow=*/5);
    auto const h1 = makeHash(0x01);

    // Two referencing tries, one block each: +1 twice.
    for (bcos::protocol::BlockNumber block = 1; block <= 2; ++block)
    {
        MPTDeltaLayer delta;
        delta.newNodes[h1] = bcos::bytes{0x11};
        commitPruneBlock(backend, pruner, block, delta);
    }
    BOOST_CHECK((readRefCountRow(backend, h1) == PruneRefCount{.count = 2}));

    MPTDeltaLayer delta3;
    delta3.obsoletedNodes.insert(h1);
    commitPruneBlock(backend, pruner, 3, delta3);

    // 2→1: still referenced — no queue row, no pendingDeleteAt.
    BOOST_CHECK((readRefCountRow(backend, h1) == PruneRefCount{.count = 1}));
    BOOST_CHECK_EQUAL(countRowsInTable(backend, kPruneQueueTable), 0U);
}

BOOST_AUTO_TEST_CASE(RevivalRevokesPendingDeleteAndStaleQueueRowIsCleaned)
{
    PruneBackend backend;
    BackendNodeStorage nodes(backend);
    MPTPruner<PruneBackend> pruner(backend, /*pruneWindow=*/5);
    auto const h1 = makeHash(0x01);

    // Block 1: created. Block 2: obsoleted → queued at 7. The node row sits on disk.
    MPTDeltaLayer delta1;
    delta1.newNodes[h1] = bcos::bytes{0x11};
    commitPruneBlock(backend, pruner, 1, delta1);
    bcos::task::syncWait(nodes.writeOne(h1, bcos::bytes{0x11}));
    MPTDeltaLayer delta2;
    delta2.obsoletedNodes.insert(h1);
    commitPruneBlock(backend, pruner, 2, delta2);
    BOOST_CHECK_EQUAL(countRowsInTable(backend, kPruneQueueTable), 1U);

    // Block 3: re-created before its deletion ran → count 1, schedule revoked.
    MPTDeltaLayer delta3;
    delta3.newNodes[h1] = bcos::bytes{0x11};
    commitPruneBlock(backend, pruner, 3, delta3);
    BOOST_CHECK((readRefCountRow(backend, h1) == PruneRefCount{.count = 1}));

    // The queue consumption at/after the stale deadline must NOT delete the revived node; it
    // only cleans the stale queue row.
    runEmptyBlocks(backend, pruner, 4, 10);
    BOOST_CHECK(nodeRowExists(backend, h1));
    BOOST_CHECK_EQUAL(countRowsInTable(backend, kPruneQueueTable), 0U);
    BOOST_CHECK((readRefCountRow(backend, h1) == PruneRefCount{.count = 1}));
}

BOOST_AUTO_TEST_CASE(RevivalAtExpiryBlockIsNotDeleted)
{
    // The F2 race, closed by preparing deletions inside the commit coroutine: a node whose
    // deletion matures at block B and is revived BY block B itself must survive — the
    // consumption re-check reads the refcount AS UPDATED BY THIS BLOCK (the postBlock overlay),
    // not the pre-block count 0 on disk.
    PruneBackend backend;
    BackendNodeStorage nodes(backend);
    MPTPruner<PruneBackend> pruner(backend, /*pruneWindow=*/2);
    auto const h1 = makeHash(0x01);

    // Block 1: created (node row on disk). Block 2: obsoleted → queued at 4.
    MPTDeltaLayer delta1;
    delta1.newNodes[h1] = bcos::bytes{0x11};
    commitPruneBlock(backend, pruner, 1, delta1);
    bcos::task::syncWait(nodes.writeOne(h1, bcos::bytes{0x11}));
    MPTDeltaLayer delta2;
    delta2.obsoletedNodes.insert(h1);
    commitPruneBlock(backend, pruner, 2, delta2);
    BOOST_REQUIRE((readRefCountRow(backend, h1) == PruneRefCount{.count = 0, .pendingDeleteAt = 4}));
    BOOST_REQUIRE_EQUAL(countRowsInTable(backend, kPruneQueueTable), 1U);

    // Block 4 — the expiry block itself — re-creates the node (0→1, schedule revoked). The
    // expired queue row IS consumed this block: the re-check must see the revival.
    MPTDeltaLayer delta4;
    delta4.newNodes[h1] = bcos::bytes{0x11};
    commitPruneBlock(backend, pruner, 4, delta4);

    BOOST_CHECK(nodeRowExists(backend, h1));
    BOOST_CHECK((readRefCountRow(backend, h1) == PruneRefCount{.count = 1}));
    BOOST_CHECK_EQUAL(countRowsInTable(backend, kPruneQueueTable), 0U);
}

BOOST_AUTO_TEST_CASE(UntrackedObsoletionSaturatesAtZeroAndQueues)
{
    // Genesis-prewrite shape: a node row that never passed any delta has no refcount row.
    PruneBackend backend;
    BackendNodeStorage nodes(backend);
    MPTPruner<PruneBackend> pruner(backend, /*pruneWindow=*/5);
    auto const h1 = makeHash(0x01);
    bcos::task::syncWait(nodes.writeOne(h1, bcos::bytes{0x11}));

    MPTDeltaLayer delta;
    delta.obsoletedNodes.insert(h1);
    commitPruneBlock(backend, pruner, 4, delta);

    // Saturating 0→0, still queued at 4+5=9.
    BOOST_CHECK((readRefCountRow(backend, h1) == PruneRefCount{.count = 0, .pendingDeleteAt = 9}));
    BOOST_CHECK_EQUAL(countRowsInTable(backend, kPruneQueueTable), 1U);

    runEmptyBlocks(backend, pruner, 5, 8);
    BOOST_CHECK(nodeRowExists(backend, h1));  // not yet due
    runEmptyBlocks(backend, pruner, 9, 9);
    BOOST_CHECK(!nodeRowExists(backend, h1));
    // Metadata of the consumed deletion is cleaned with it.
    BOOST_CHECK(!readRefCountRow(backend, h1).has_value());
    BOOST_CHECK_EQUAL(countRowsInTable(backend, kPruneQueueTable), 0U);
}

BOOST_AUTO_TEST_CASE(IntraBlockObsoletionNetsToZero)
{
    PruneBackend backend;
    BackendNodeStorage nodes(backend);
    MPTPruner<PruneBackend> pruner(backend, /*pruneWindow=*/5);
    auto const h1 = makeHash(0x01);

    MPTDeltaLayer delta1;
    delta1.newNodes[h1] = bcos::bytes{0x11};
    commitPruneBlock(backend, pruner, 1, delta1);

    // Produced AND obsoleted within block 2, with a prior count of 1: +1 −1 nets to 0 and the
    // node stays counted — it is referenced by the new version.
    MPTDeltaLayer delta2;
    delta2.newNodes[h1] = bcos::bytes{0x11};
    delta2.intraBlockObsoleted.insert(h1);
    commitPruneBlock(backend, pruner, 2, delta2);
    BOOST_CHECK((readRefCountRow(backend, h1) == PruneRefCount{.count = 1}));
    BOOST_CHECK_EQUAL(countRowsInTable(backend, kPruneQueueTable), 0U);

    // The same shape with NO prior count (untracked history): the node is on disk (it was in
    // newNodes) and referenced by nothing the counter can see — queue it like any 0→0
    // obsoletion.
    auto const h2 = makeHash(0x02);
    MPTDeltaLayer delta3;
    delta3.newNodes[h2] = bcos::bytes{0x22};
    delta3.intraBlockObsoleted.insert(h2);
    commitPruneBlock(backend, pruner, 3, delta3);
    BOOST_CHECK((readRefCountRow(backend, h2) == PruneRefCount{.count = 0, .pendingDeleteAt = 8}));
    BOOST_CHECK_EQUAL(countRowsInTable(backend, kPruneQueueTable), 1U);
}

BOOST_AUTO_TEST_CASE(CrossTrieSharingSurvivesUntilLastReferenceDrops)
{
    // Two accounts whose storage tries encode identically resolve to the same node hashes —
    // the content-addressed sharing MPTDeltaLayer warns about. Both builds happen in ONE block,
    // so newNodes deduplicates them and only refCountDeltas still carries the true count of 2.
    PruneBackend backend;
    BackendNodeStorage nodes(backend);
    constexpr int64_t N = 3;
    MPTPruner<PruneBackend> pruner(backend, N);

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
        BOOST_CHECK((readRefCountRow(backend, hash) == PruneRefCount{.count = 2}));
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
        BOOST_CHECK((readRefCountRow(backend, hash) == PruneRefCount{.count = 1}));
    }
    // Re-emitted nodes: still in newNodes (flushed, still live), but their refcount is unmoved.
    for (auto const& hash : delta2.newNodes | std::views::keys)
    {
        if (liveAfterBlock1.contains(hash))
        {
            BOOST_CHECK_EQUAL(delta2.refCountDeltas.at(hash), 0);
            BOOST_CHECK((readRefCountRow(backend, hash) == PruneRefCount{.count = 2}));
        }
    }
    // Well past 2+N, but B still references those nodes: nothing may be deleted. (Nothing was
    // ever queued — the 2→1 drops carry no pendingDeleteAt — so the consumption only rewrites
    // the watermark; the node rows are the assertion that matters.)
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
        BOOST_CHECK((readRefCountRow(backend, hash) ==
                    PruneRefCount{.count = 0, .pendingDeleteAt = blockB + N}));
    }
    for (auto const& hash : delta3.newNodes | std::views::keys)
    {
        BOOST_CHECK((readRefCountRow(backend, hash) == PruneRefCount{.count = 2}));
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
    MPTPruner<PruneBackend> pruner(backend, N);

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

        // Every surviving queue row is future-dated (past-due rows are consumed above).
        auto iterator = bcos::task::syncWait(
            bcos::storage2::range(backend, bcos::storage2::RANGE_SEEK,
                bcos::executor_v1::StateKey{kPruneQueueTable, std::string_view{}}));
        while (auto item = bcos::task::syncWait(iterator.next()))
        {
            bcos::executor_v1::StateKeyView const keyView{std::get<0>(*item)};
            if (keyView.m_table != kPruneQueueTable)
            {
                break;
            }
            auto const& value = std::get<1>(*item);
            if (!std::get_if<bcos::storage::Entry>(std::addressof(value)))
            {
                continue;
            }
            auto const [targetBlock, hash] = decodeQueueKeyPart(keyView.m_key);
            BOOST_CHECK_MESSAGE(targetBlock > static_cast<uint64_t>(block),
                "past-due queue row survived the deletion pass");
        }
    }
}

BOOST_AUTO_TEST_CASE(StartupGuardAcceptsMatchingWatermark)
{
    // A restart onto a chain whose watermark matches the ledger's current block: pruning was
    // never interrupted, nothing to refuse.
    PruneBackend backend;
    BackendNodeStorage nodes(backend);
    {
        MPTPruner<PruneBackend> pruner(backend, 5);
        MPTDeltaLayer delta1;
        delta1.newNodes[makeHash(0x01)] = bcos::bytes{0x11};
        commitPruneBlock(backend, pruner, 1, delta1);
        commitPruneBlock(backend, pruner, 2, MPTDeltaLayer{});
    }
    BOOST_REQUIRE(readWatermarkRow(backend).has_value());

    MPTPruner<PruneBackend> recovered(backend, 5);
    BOOST_CHECK_NO_THROW(bcos::task::syncWait(recovered.init(2)));
    BOOST_CHECK_EQUAL(recovered.watermark(), 2);
}

BOOST_AUTO_TEST_CASE(StartupGuardRejectsWatermarkMismatch)
{
    // "Disabled for a while, re-enabled": blocks committed while pruning was off left an
    // uncounted gap between the persisted watermark and the current block — refuse, in BOTH
    // directions (a watermark AHEAD of the ledger is just as impossible under honest operation).
    PruneBackend backend;
    BackendNodeStorage nodes(backend);
    {
        MPTPruner<PruneBackend> pruner(backend, 5);
        commitPruneBlock(backend, pruner, 1, MPTDeltaLayer{});
        commitPruneBlock(backend, pruner, 2, MPTDeltaLayer{});
    }

    MPTPruner<PruneBackend> behind(backend, 5);
    BOOST_CHECK_THROW(bcos::task::syncWait(behind.init(5)), bcos::tool::InvalidConfig);
    MPTPruner<PruneBackend> ahead(backend, 5);
    BOOST_CHECK_THROW(bcos::task::syncWait(ahead.init(1)), bcos::tool::InvalidConfig);
}

BOOST_AUTO_TEST_CASE(StartupGuardRejectsL2MidChainEnable)
{
    // An L2 chain's MPT has been building since block 1: with no watermark on disk, enabling
    // pruning at any later block would prune over uncounted history — refuse.
    PruneBackend backend;
    bcos::ledger::Features features;
    features.set(bcos::ledger::Features::Flag::feature_l2_ethereum_compat);
    bcos::task::syncWait(features.writeToStorage(backend, /*blockNumber=*/0));

    MPTPruner<PruneBackend> pruner(backend, 5);
    BOOST_CHECK_THROW(bcos::task::syncWait(pruner.init(3)), bcos::tool::InvalidConfig);
}

BOOST_AUTO_TEST_CASE(StartupGuardScenarioAActivationBoundary)
{
    // feature_mpt_state_root activates at block 100: enabling pruning AT or BEFORE the
    // activation block is safe (no MPT block has committed yet); one block past it is not.
    PruneBackend backend;
    bcos::ledger::Features features;
    features.set(bcos::ledger::Features::Flag::feature_mpt_state_root);
    bcos::task::syncWait(features.writeToStorage(backend, /*blockNumber=*/100));

    MPTPruner<PruneBackend> atActivation(backend, 5);
    BOOST_CHECK_NO_THROW(bcos::task::syncWait(atActivation.init(100)));
    MPTPruner<PruneBackend> preActivation(backend, 5);
    BOOST_CHECK_NO_THROW(bcos::task::syncWait(preActivation.init(99)));
    MPTPruner<PruneBackend> pastActivation(backend, 5);
    BOOST_CHECK_THROW(bcos::task::syncWait(pastActivation.init(101)), bcos::tool::InvalidConfig);
}

BOOST_AUTO_TEST_CASE(StartupGuardFreshNonL2ChainAllowed)
{
    // Block 0 on a chain without the L2 flag: no genesis trie to seed, nothing to check.
    PruneBackend backend;
    MPTPruner<PruneBackend> pruner(backend, 5);
    BOOST_CHECK_NO_THROW(bcos::task::syncWait(pruner.init(0)));
}

BOOST_AUTO_TEST_CASE(StartupGuardFreshL2RequiresSeededGenesis)
{
    // A fresh L2 chain whose genesis predates refcount seeding has no seed marker — refuse
    // rather than prune live genesis nodes; the seeded genesis passes.
    PruneBackend backend;
    bcos::ledger::Features features;
    features.set(bcos::ledger::Features::Flag::feature_l2_ethereum_compat);
    bcos::task::syncWait(features.writeToStorage(backend, /*blockNumber=*/0));

    MPTPruner<PruneBackend> unseeded(backend, 5);
    BOOST_CHECK_THROW(bcos::task::syncWait(unseeded.init(0)), bcos::tool::InvalidConfig);

    bcos::storage::Entry marker;
    marker.set(std::string{});
    bcos::task::syncWait(bcos::storage2::writeOne(backend, seedMarkerKey(), std::move(marker)));
    MPTPruner<PruneBackend> seeded(backend, 5);
    BOOST_CHECK_NO_THROW(bcos::task::syncWait(seeded.init(0)));
}

BOOST_AUTO_TEST_CASE(GenesisSeedCountsSharedSubtree)
{
    // Two genesis accounts with byte-identical storage: their storage sub-tries emit the SAME
    // node hashes, and computeGenesisStateTrie must keep each emission's multiplicity (2) so
    // the seeded refcounts match the true reference count — otherwise the FIRST referencing
    // account's rebuild would drop a shared node to 0 and schedule a live node for deletion.
    std::string const slotHex(64, '1');       // slot key 0x11…11
    std::string const valueHex = std::string(63, '0') + "1";  // value 1, a full 32-byte word
    bcos::ledger::GenesisConfig genesis{};
    genesis.m_allocs = {
        bcos::ledger::Alloc{.address = "1111111111111111111111111111111111111111",
            .balance = 100,
            .nonce = "0",
            .code = "",
            .storage = {{slotHex, valueHex}}},
        bcos::ledger::Alloc{.address = "2222222222222222222222222222222222222222",
            .balance = 200,
            .nonce = "0",
            .code = "",
            .storage = {{slotHex, valueHex}}},
    };
    auto const trie = bcos::task::syncWait(bcos::ledger::computeGenesisStateTrie(genesis));

    std::vector<bcos::h256> shared;
    for (auto const& [hash, count] : trie.nodeCounts)
    {
        if (count == 2)
        {
            shared.push_back(hash);
        }
        else
        {
            BOOST_CHECK_EQUAL(count, 1);  // account-trie nodes: emitted exactly once
        }
    }
    BOOST_REQUIRE(!shared.empty());  // else the identical storage tries shared nothing — vacuous

    // Seeding writes one refcount row per node at its multiplicity, plus the marker.
    PruneBackend backend;
    BackendNodeStorage nodes(backend);
    bcos::task::syncWait(writePruneSeedRows(backend, trie.nodeCounts));
    for (auto const& [hash, count] : trie.nodeCounts)
    {
        BOOST_CHECK((readRefCountRow(backend, hash) == PruneRefCount{.count = count}));
    }
    BOOST_CHECK(bcos::task::syncWait(bcos::storage2::existsOne(backend, seedMarkerKey())));

    constexpr int64_t N = 3;
    MPTPruner<PruneBackend> pruner(backend, N);
    auto const sharedNode = shared.front();
    bcos::task::syncWait(nodes.writeOne(sharedNode, bcos::bytes{0xAA}));  // node row on disk

    // The FIRST referencing account's storage rebuild obsoletes the shared node: seeded 2 → 1,
    // no queue row, and the node rides out what WOULD have been its deletion deadline had the
    // seed under-counted (an unseeded 0→0 saturates and queues — the UntrackedObsoletion case).
    MPTDeltaLayer delta1;
    delta1.refCountDeltas[sharedNode] = -1;
    delta1.obsoletedNodes.insert(sharedNode);
    commitPruneBlock(backend, pruner, 1, delta1);
    BOOST_CHECK((readRefCountRow(backend, sharedNode) == PruneRefCount{.count = 1}));
    BOOST_CHECK_EQUAL(countRowsInTable(backend, kPruneQueueTable), 0U);
    runEmptyBlocks(backend, pruner, 2, 1 + N + 1);
    BOOST_CHECK(nodeRowExists(backend, sharedNode));

    // The second (last) reference dropping at block 6 schedules the deletion at 6+N, consumed
    // by block 6+N's prepare.
    commitPruneBlock(backend, pruner, 6, delta1);
    BOOST_CHECK((readRefCountRow(backend, sharedNode) ==
                PruneRefCount{.count = 0, .pendingDeleteAt = static_cast<uint64_t>(6 + N)}));
    runEmptyBlocks(backend, pruner, 7, 6 + N - 1);
    BOOST_CHECK(nodeRowExists(backend, sharedNode));
    runEmptyBlocks(backend, pruner, 6 + N, 6 + N);
    BOOST_CHECK(!nodeRowExists(backend, sharedNode));
    BOOST_CHECK(!readRefCountRow(backend, sharedNode).has_value());
    BOOST_CHECK_EQUAL(countRowsInTable(backend, kPruneQueueTable), 0U);
}

BOOST_AUTO_TEST_CASE(TombstoneObsoletionCountedManually)
{
    // The MPTBuilder tombstone path is the ONE producer that writes refCountDeltas by hand (the
    // prior storage root's −1, MPTBuilder.h's finalizeAccount): drive prepare with exactly that
    // shape and verify the pruner schedules the storage root like any other obsoletion.
    PruneBackend backend;
    BackendNodeStorage nodes(backend);
    MPTPruner<PruneBackend> pruner(backend, /*pruneWindow=*/2);
    auto const storageRoot = makeHash(0x77);
    bcos::task::syncWait(nodes.writeOne(storageRoot, bcos::bytes{0xAA}));

    // The storage trie's own build counted its root at creation (+1 via the set fallback).
    MPTDeltaLayer delta1;
    delta1.newNodes[storageRoot] = bcos::bytes{0xAA};
    commitPruneBlock(backend, pruner, 1, delta1);
    BOOST_CHECK((readRefCountRow(backend, storageRoot) == PruneRefCount{.count = 1}));

    // Block 2: the account SELFDESTRUCTs — the tombstone's manual −1 with the obsoletion.
    MPTDeltaLayer delta2;
    delta2.refCountDeltas[storageRoot] = -1;
    delta2.obsoletedNodes.insert(storageRoot);
    commitPruneBlock(backend, pruner, 2, delta2);
    BOOST_CHECK(
        (readRefCountRow(backend, storageRoot) == PruneRefCount{.count = 0, .pendingDeleteAt = 4}));
    runEmptyBlocks(backend, pruner, 3, 3);
    BOOST_CHECK(nodeRowExists(backend, storageRoot));  // inside the window
    runEmptyBlocks(backend, pruner, 4, 4);
    BOOST_CHECK(!nodeRowExists(backend, storageRoot));
    BOOST_CHECK(!readRefCountRow(backend, storageRoot).has_value());

    // The saturating 0→0 of the same hand-built shape (no counted history at all): still
    // queued, still deleted at the deadline.
    auto const orphan = makeHash(0x78);
    bcos::task::syncWait(nodes.writeOne(orphan, bcos::bytes{0xBB}));
    MPTDeltaLayer delta3;
    delta3.refCountDeltas[orphan] = -1;
    delta3.obsoletedNodes.insert(orphan);
    commitPruneBlock(backend, pruner, 5, delta3);
    BOOST_CHECK((readRefCountRow(backend, orphan) == PruneRefCount{.count = 0, .pendingDeleteAt = 7}));
    runEmptyBlocks(backend, pruner, 6, 7);
    BOOST_CHECK(!nodeRowExists(backend, orphan));
    BOOST_CHECK_EQUAL(countRowsInTable(backend, kPruneQueueTable), 0U);
}

BOOST_AUTO_TEST_CASE(OnCommitAdvancesWatermarkAfterBatchDeletion)
{
    // The production commit order: coPreparePruneRows' batch — metadata AND the deletions of
    // expired nodes — lands with the block's WriteBatch (commitPruneBlock applies it inline
    // here); onCommit afterwards only advances the in-memory watermark. The end state matches
    // what a deletion pass run to the same horizon produces.
    constexpr int64_t N = 2;
    PruneBackend backend;
    BackendNodeStorage nodes(backend);
    MPTPruner<PruneBackend> pruner(backend, N);
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
        pruner.onCommit(block, delta);
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

BOOST_AUTO_TEST_CASE(CorruptRefCountRowIsSkippedNotFatal)
{
    // Fail-safe decode (R2): a corrupted refcount row must not fail the block's commit. The
    // affected hash is skipped entirely — no row write, no queue entry, no overlay — while the
    // block's other hashes count normally.
    PruneBackend backend;
    BackendNodeStorage nodes(backend);
    MPTPruner<PruneBackend> pruner(backend, /*pruneWindow=*/5);
    auto const hBad = makeHash(0x0B);
    auto const hGood = makeHash(0x0C);

    // Sanity: the planted row really is undecodable.
    bcos::bytes const corrupt{0xFF};
    BOOST_CHECK_THROW(
        decodeRefCount(bcos::bytesConstRef(corrupt.data(), corrupt.size())), MPTDecodeError);
    bcos::storage::Entry badEntry;
    badEntry.set(corrupt);
    bcos::task::syncWait(
        bcos::storage2::writeOne(backend, pruneRefKey(hBad), std::move(badEntry)));

    MPTDeltaLayer delta;
    delta.newNodes[hBad] = bcos::bytes{0xBB};
    delta.newNodes[hGood] = bcos::bytes{0xCC};
    BOOST_CHECK_NO_THROW(commitPruneBlock(backend, pruner, 1, delta));

    // The corrupt row is still exactly what was planted (no overwrite), the skipped hash armed
    // no queue row, and the healthy hash counted normally.
    auto surviving = bcos::task::syncWait(bcos::storage2::readOne(backend, pruneRefKey(hBad)));
    BOOST_REQUIRE(surviving.has_value());
    auto raw = surviving->get();
    BOOST_CHECK_THROW(decodeRefCount(
                          bcos::bytesConstRef(
                              reinterpret_cast<bcos::byte const*>(raw.data()), raw.size())),
        MPTDecodeError);
    BOOST_CHECK((readRefCountRow(backend, hGood) == PruneRefCount{.count = 1}));
    BOOST_CHECK_EQUAL(countRowsInTable(backend, kPruneQueueTable), 0U);
}

BOOST_AUTO_TEST_CASE(CorruptQueueRowIsCleanedUp)
{
    // Fail-safe decode (R2): an undecodable queue row (key part not 40 bytes) has no readable
    // deadline; the consumption pass must evict it and keep scanning — never fail the commit,
    // never stop the scan. Rows on BOTH sides of a legitimate row in key order are exercised.
    PruneBackend backend;
    BackendNodeStorage nodes(backend);
    MPTPruner<PruneBackend> pruner(backend, /*pruneWindow=*/2);
    auto const h1 = makeHash(0x01);

    // A legitimately expiring node: created at block 1, obsoleted at block 2 → deadline 4.
    MPTDeltaLayer delta1;
    delta1.newNodes[h1] = bcos::bytes{0x11};
    commitPruneBlock(backend, pruner, 1, delta1);
    bcos::task::syncWait(nodes.writeOne(h1, bcos::bytes{0x11}));
    MPTDeltaLayer delta2;
    delta2.obsoletedNodes.insert(h1);
    commitPruneBlock(backend, pruner, 2, delta2);
    BOOST_REQUIRE_EQUAL(countRowsInTable(backend, kPruneQueueTable), 1U);

    // Plant two poisoned queue rows: "\x00\x00\x00" sorts BEFORE the legit row (a prefix of its
    // BE-u64 deadline), "bad" (0x62…) sorts AFTER it.
    auto writePoisonedQueueRow = [&backend](std::string keyPart) {
        bcos::storage::Entry entry;
        entry.set(std::string{});
        bcos::task::syncWait(bcos::storage2::writeOne(backend,
            bcos::executor_v1::StateKey{kPruneQueueTable, std::string_view{keyPart}},
            std::move(entry)));
    };
    writePoisonedQueueRow(std::string("\x00\x00\x00", 3));
    writePoisonedQueueRow("bad");
    BOOST_REQUIRE_EQUAL(countRowsInTable(backend, kPruneQueueTable), 3U);

    // Block 3's scan evicts the leading poisoned row and stops at the not-yet-due legit one;
    // block 4 consumes the legit deletion and evicts the trailing poisoned row behind it.
    BOOST_CHECK_NO_THROW(runEmptyBlocks(backend, pruner, 3, 4));
    BOOST_CHECK(!nodeRowExists(backend, h1));
    BOOST_CHECK(!readRefCountRow(backend, h1).has_value());
    BOOST_CHECK_EQUAL(countRowsInTable(backend, kPruneQueueTable), 0U);
}

BOOST_AUTO_TEST_CASE(CorruptRefCountAtRecheckBlocksDeletion)
{
    // Fail-safe decode (R2): the deletion re-check reads the refcount row from the backend for
    // a hash this block's delta did not touch. A corrupt row there leaves the deletion
    // UNCONFIRMED: the stale queue row is consumed but the node row and the refcount row
    // survive — a leak, never a deleted live node.
    PruneBackend backend;
    BackendNodeStorage nodes(backend);
    MPTPruner<PruneBackend> pruner(backend, /*pruneWindow=*/2);
    auto const h1 = makeHash(0x01);

    MPTDeltaLayer delta1;
    delta1.newNodes[h1] = bcos::bytes{0x11};
    commitPruneBlock(backend, pruner, 1, delta1);
    bcos::task::syncWait(nodes.writeOne(h1, bcos::bytes{0x11}));
    MPTDeltaLayer delta2;
    delta2.obsoletedNodes.insert(h1);
    commitPruneBlock(backend, pruner, 2, delta2);
    BOOST_REQUIRE(
        (readRefCountRow(backend, h1) == PruneRefCount{.count = 0, .pendingDeleteAt = 4}));

    // The refcount row corrupts after the node was queued.
    bcos::storage::Entry badEntry;
    badEntry.set(bcos::bytes{0xFF});
    bcos::task::syncWait(bcos::storage2::writeOne(backend, pruneRefKey(h1), std::move(badEntry)));

    // The deadline block's prepare: no throw, and the deletions carry ONLY the queue row.
    PruneRowBatch batch;
    BOOST_CHECK_NO_THROW(
        batch = bcos::task::syncWait(pruner.coPreparePruneRows(4, MPTDeltaLayer{})));
    BOOST_CHECK_EQUAL(batch.deletions.size(), 1U);
    bcos::task::syncWait(bcos::storage2::writeSome(backend, std::move(batch.rows)));
    bcos::task::syncWait(bcos::storage2::removeSome(backend, std::move(batch.deletions)));

    BOOST_CHECK(nodeRowExists(backend, h1));
    BOOST_CHECK(
        bcos::task::syncWait(bcos::storage2::existsOne(backend, pruneRefKey(h1))));
    BOOST_CHECK_EQUAL(countRowsInTable(backend, kPruneQueueTable), 0U);
}

BOOST_AUTO_TEST_CASE(StartupGuardRejectsWindowChange)
{
    // The window fingerprint persists with every block's batch; reconfiguring
    // storage.mpt_prune_window and restarting must fail loudly — the queue deadlines already on
    // disk were armed with the OLD window.
    PruneBackend backend;
    BackendNodeStorage nodes(backend);
    {
        MPTPruner<PruneBackend> pruner(backend, 5);
        commitPruneBlock(backend, pruner, 1, MPTDeltaLayer{});
        commitPruneBlock(backend, pruner, 2, MPTDeltaLayer{});
    }
    BOOST_REQUIRE(readWindowRow(backend).has_value());
    BOOST_CHECK_EQUAL(*readWindowRow(backend), 5U);

    MPTPruner<PruneBackend> changed(backend, 7);
    BOOST_CHECK_THROW(bcos::task::syncWait(changed.init(2)), bcos::tool::InvalidConfig);
    MPTPruner<PruneBackend> unchanged(backend, 5);
    BOOST_CHECK_NO_THROW(bcos::task::syncWait(unchanged.init(2)));
    BOOST_CHECK_EQUAL(unchanged.watermark(), 2);
}

BOOST_AUTO_TEST_CASE(StartupGuardAcceptsMissingWindowFingerprint)
{
    // A chain whose pruning ran on a binary predating the window fingerprint has a watermark
    // but no window row: the guard cannot check what was never recorded — accept (the watermark
    // guard still applies).
    PruneBackend backend;
    bcos::storage::Entry watermarkEntry;
    watermarkEntry.set(encodeWatermark(2));
    bcos::task::syncWait(
        bcos::storage2::writeOne(backend, watermarkKey(), std::move(watermarkEntry)));
    BOOST_REQUIRE(!readWindowRow(backend).has_value());

    MPTPruner<PruneBackend> pruner(backend, 5);
    BOOST_CHECK_NO_THROW(bcos::task::syncWait(pruner.init(2)));
    BOOST_CHECK_EQUAL(pruner.watermark(), 2);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::ledger::mpt::test
