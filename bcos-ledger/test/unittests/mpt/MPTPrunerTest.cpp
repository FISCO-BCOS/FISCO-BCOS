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
 *        invariants over random workloads, crash replay and deletion idempotence (spec §4.8)
 */
#include "TestHelpers.h"
#include <bcos-ledger/GenesisStateRoot.h>
#include <bcos-ledger/mpt/CommitObserver.h>
#include <bcos-ledger/mpt/MPTPruner.h>
#include <bcos-ledger/mpt/Proof.h>
#include <bcos-ledger/mpt/PruneMetadata.h>
#include <bcos-ledger/mpt/Trie.h>
#include <bcos-storage/KeyPrefixes.h>
#include <boost/test/unit_test.hpp>
#include <deque>
#include <map>
#include <optional>
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

/// One block of the account-trie chain the pruning tests drive, in the production commit order:
/// build the trie delta → flush its nodes → prepare + write the pruning metadata rows (the
/// prewriteStorage stand-in) → optionally run the deletion pass synchronously. Returns the
/// delta so the caller can feed onCommit itself.
MPTDeltaLayer commitAccountBlock(PruneBackend& backend, BackendNodeStorage& nodes,
    MPTPruner<PruneBackend>& pruner, std::map<bcos::Address, Account>& accounts,
    bcos::h256 priorRoot,
    std::map<bcos::Address, std::optional<Account>> const& accountChanges,
    bcos::protocol::BlockNumber blockNumber, bool runDeletion)
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
    auto rows = bcos::task::syncWait(pruner.coPreparePruneRows(blockNumber, delta));
    bcos::task::syncWait(bcos::storage2::writeSome(backend, rows));
    if (runDeletion)
    {
        bcos::task::syncWait(pruner.coDeleteExpired(blockNumber));
    }
    return delta;
}
}  // namespace

BOOST_AUTO_TEST_CASE(DefaultObserverHookReturnsNoRows)
{
    NoopCommitObserver observer;
    MPTDeltaLayer delta;
    delta.newNodes[makeHash(0x01)] = bcos::bytes{0x01};
    auto rows = bcos::task::syncWait(observer.coPreparePruneRows(3, delta));
    BOOST_CHECK(rows.empty());
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
    auto rows = bcos::task::syncWait(pruner.coPreparePruneRows(7, delta));
    // 2 refcount rows + the watermark row; no queue rows.
    BOOST_CHECK_EQUAL(rows.size(), 3U);
    bcos::task::syncWait(bcos::storage2::writeSome(backend, rows));

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
    bcos::task::syncWait(
        bcos::storage2::writeSome(backend, bcos::task::syncWait(pruner.coPreparePruneRows(1, delta1))));

    MPTDeltaLayer delta2;
    delta2.obsoletedNodes.insert(h1);
    auto rows = bcos::task::syncWait(pruner.coPreparePruneRows(2, delta2));
    bcos::task::syncWait(bcos::storage2::writeSome(backend, rows));

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
        bcos::task::syncWait(bcos::storage2::writeSome(
            backend, bcos::task::syncWait(pruner.coPreparePruneRows(block, delta))));
    }
    BOOST_CHECK((readRefCountRow(backend, h1) == PruneRefCount{.count = 2}));

    MPTDeltaLayer delta3;
    delta3.obsoletedNodes.insert(h1);
    bcos::task::syncWait(
        bcos::storage2::writeSome(backend, bcos::task::syncWait(pruner.coPreparePruneRows(3, delta3))));

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
    bcos::task::syncWait(
        bcos::storage2::writeSome(backend, bcos::task::syncWait(pruner.coPreparePruneRows(1, delta1))));
    bcos::task::syncWait(nodes.writeOne(h1, bcos::bytes{0x11}));
    MPTDeltaLayer delta2;
    delta2.obsoletedNodes.insert(h1);
    bcos::task::syncWait(
        bcos::storage2::writeSome(backend, bcos::task::syncWait(pruner.coPreparePruneRows(2, delta2))));
    BOOST_CHECK_EQUAL(countRowsInTable(backend, kPruneQueueTable), 1U);

    // Block 3: re-created before its deletion ran → count 1, schedule revoked.
    MPTDeltaLayer delta3;
    delta3.newNodes[h1] = bcos::bytes{0x11};
    bcos::task::syncWait(
        bcos::storage2::writeSome(backend, bcos::task::syncWait(pruner.coPreparePruneRows(3, delta3))));
    BOOST_CHECK((readRefCountRow(backend, h1) == PruneRefCount{.count = 1}));

    // The deletion pass at/after the stale deadline must NOT delete the revived node; it only
    // cleans the stale queue row.
    bcos::task::syncWait(pruner.coDeleteExpired(10));
    BOOST_CHECK(nodeRowExists(backend, h1));
    BOOST_CHECK_EQUAL(countRowsInTable(backend, kPruneQueueTable), 0U);
    BOOST_CHECK((readRefCountRow(backend, h1) == PruneRefCount{.count = 1}));
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
    bcos::task::syncWait(
        bcos::storage2::writeSome(backend, bcos::task::syncWait(pruner.coPreparePruneRows(4, delta))));

    // Saturating 0→0, still queued at 4+5=9.
    BOOST_CHECK((readRefCountRow(backend, h1) == PruneRefCount{.count = 0, .pendingDeleteAt = 9}));
    BOOST_CHECK_EQUAL(countRowsInTable(backend, kPruneQueueTable), 1U);

    bcos::task::syncWait(pruner.coDeleteExpired(8));
    BOOST_CHECK(nodeRowExists(backend, h1));  // not yet due
    bcos::task::syncWait(pruner.coDeleteExpired(9));
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
    bcos::task::syncWait(
        bcos::storage2::writeSome(backend, bcos::task::syncWait(pruner.coPreparePruneRows(1, delta1))));

    // Produced AND obsoleted within block 2, with a prior count of 1: +1 −1 nets to 0 and the
    // node stays counted — it is referenced by the new version.
    MPTDeltaLayer delta2;
    delta2.newNodes[h1] = bcos::bytes{0x11};
    delta2.intraBlockObsoleted.insert(h1);
    bcos::task::syncWait(bcos::storage2::writeSome(
        backend, bcos::task::syncWait(pruner.coPreparePruneRows(2, delta2))));
    BOOST_CHECK((readRefCountRow(backend, h1) == PruneRefCount{.count = 1}));
    BOOST_CHECK_EQUAL(countRowsInTable(backend, kPruneQueueTable), 0U);

    // The same shape with NO prior count (untracked history): the node is on disk (it was in
    // newNodes) and referenced by nothing the counter can see — queue it like any 0→0
    // obsoletion.
    auto const h2 = makeHash(0x02);
    MPTDeltaLayer delta3;
    delta3.newNodes[h2] = bcos::bytes{0x22};
    delta3.intraBlockObsoleted.insert(h2);
    bcos::task::syncWait(
        bcos::storage2::writeSome(backend, bcos::task::syncWait(pruner.coPreparePruneRows(3, delta3))));
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
    bcos::task::syncWait(
        bcos::storage2::writeSome(backend, bcos::task::syncWait(pruner.coPreparePruneRows(1, delta1))));

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
    bcos::task::syncWait(
        bcos::storage2::writeSome(backend, bcos::task::syncWait(pruner.coPreparePruneRows(2, delta2))));

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
    // Well past 2+N, but B still references those nodes: nothing may be deleted.
    bcos::task::syncWait(pruner.coDeleteExpired(2 + N));
    for (auto const& hash : delta2.obsoletedNodes)
    {
        BOOST_CHECK_MESSAGE(nodeRowExists(backend, hash),
            "shared node deleted while a second trie still references it");
    }
    BOOST_CHECK(liveNodeHashes(nodes, sharedRoot).size() == liveAfterBlock1.size());

    // Block 3: B's trie is rebuilt with the SAME change — the last reference to the old nodes
    // drops (1→0, queued at 3+N), and B's fresh nodes byte-match A's block-2 emission (counted
    // +1 there), so the re-emission makes them count 2: both new tries reference them.
    auto resultB2 = bcos::task::syncWait(commitTrie(nodes, sharedRoot, changesA));
    BOOST_REQUIRE(resultB2.root == delta2.stateRoot);
    MPTDeltaLayer delta3;
    delta3.stateRoot = resultB2.root;
    mergeNodeDelta(std::move(resultB2), delta3);
    bcos::task::syncWait(flushTrieNodes(nodes, delta3.newNodes));
    bcos::task::syncWait(
        bcos::storage2::writeSome(backend, bcos::task::syncWait(pruner.coPreparePruneRows(3, delta3))));

    for (auto const& hash : delta2.obsoletedNodes)
    {
        BOOST_CHECK((readRefCountRow(backend, hash) ==
                    PruneRefCount{.count = 0, .pendingDeleteAt = 3 + N}));
    }
    for (auto const& hash : delta3.newNodes | std::views::keys)
    {
        BOOST_CHECK((readRefCountRow(backend, hash) == PruneRefCount{.count = 2}));
    }

    // Deadline discipline: present at 3+N−1, gone at 3+N.
    bcos::task::syncWait(pruner.coDeleteExpired(3 + N - 1));
    for (auto const& hash : delta2.obsoletedNodes)
    {
        BOOST_CHECK(nodeRowExists(backend, hash));
    }
    bcos::task::syncWait(pruner.coDeleteExpired(3 + N));
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
        auto const delta =
            commitAccountBlock(backend, nodes, pruner, accounts, root, changes, block, true);
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

        // (b) + (c): deletion has caught up to head, so on-disk rows == window live set.
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

BOOST_AUTO_TEST_CASE(CrashReplayFromWatermarkAndIdempotentDeletion)
{
    // Six blocks of churn with the deletion pass NEVER run (the crash-before-worker window);
    // a fresh pruner over the same backend replays from the persisted watermark on init().
    constexpr int64_t N = 2;

    PruneBackend backend;
    BackendNodeStorage nodes(backend);
    std::map<bcos::Address, Account> accounts;
    std::map<bcos::Address, uint64_t> versions;
    std::deque<std::pair<bcos::protocol::BlockNumber, bcos::h256>> history;

    bcos::h256 root = emptyRootHash();
    {
        MPTPruner<PruneBackend> pruner(backend, N);
        for (bcos::protocol::BlockNumber block = 1; block <= 6; ++block)
        {
            std::map<bcos::Address, std::optional<Account>> changes;
            auto const address = makeAddress(static_cast<uint8_t>(block));
            versions[address] = 0;
            changes[address] = makePruneAccount(0, 100 * block);
            if (block > 2)
            {
                // Rewrite an earlier account: its old path nodes become obsoleted.
                auto const older = makeAddress(static_cast<uint8_t>(block - 2));
                changes[older] = makePruneAccount(++versions[older], 100 * block);
            }
            root = commitAccountBlock(
                backend, nodes, pruner, accounts, root, changes, block, /*runDeletion=*/false)
                       .stateRoot;
            history.emplace_back(block, root);
        }
        BOOST_CHECK_EQUAL(*readWatermarkRow(backend), 6U);
    }  // the first pruner is gone — the "crash"

    // Restart: init() reads the watermark and replays every deletion it covers.
    MPTPruner<PruneBackend> recovered(backend, N);
    bcos::task::syncWait(recovered.init());
    BOOST_CHECK_EQUAL(recovered.watermark(), 6);
    recovered.waitForIdle();

    // The replay deleted exactly what a live pruner would have by block 6: on-disk nodes are
    // the live set of the window [6−N, 6].
    std::unordered_set<bcos::h256> windowLive;
    for (auto const& [number, versionRoot] : history)
    {
        if (number >= 6 - N)
        {
            auto live = liveNodeHashes(nodes, versionRoot);
            windowLive.insert(live.begin(), live.end());
        }
    }
    BOOST_CHECK_EQUAL(countRowsInTable(backend, bcos::storage2::kMPTTable), windowLive.size());
    // Every surviving queue row is future-dated (blocks 5 and 6 obsoleted nodes queued at 7
    // and 8 — past-due rows were consumed by the replay).
    auto iterator = bcos::task::syncWait(bcos::storage2::range(backend, bcos::storage2::RANGE_SEEK,
        bcos::executor_v1::StateKey{kPruneQueueTable, std::string_view{}}));
    size_t futureRows = 0;
    while (auto item = bcos::task::syncWait(iterator.next()))
    {
        bcos::executor_v1::StateKeyView const keyView{std::get<0>(*item)};
        if (keyView.m_table != kPruneQueueTable)
        {
            break;
        }
        if (!std::get_if<bcos::storage::Entry>(std::addressof(std::get<1>(*item))))
        {
            continue;
        }
        auto const [targetBlock, hash] = decodeQueueKeyPart(keyView.m_key);
        BOOST_CHECK(targetBlock > 6U);
        ++futureRows;
    }
    BOOST_CHECK(futureRows > 0U);  // the window's obsoleted nodes are queued, not deleted

    // Idempotence: replaying the same horizon again changes nothing and throws nothing.
    bcos::task::syncWait(recovered.coDeleteExpired(6));
    BOOST_CHECK_EQUAL(countRowsInTable(backend, bcos::storage2::kMPTTable), windowLive.size());
    BOOST_CHECK_EQUAL(countRowsInTable(backend, kPruneQueueTable), futureRows);
    bcos::task::syncWait(recovered.coDeleteExpired(6));
    BOOST_CHECK_EQUAL(countRowsInTable(backend, bcos::storage2::kMPTTable), windowLive.size());
}

BOOST_AUTO_TEST_CASE(OnCommitDrivesDeletionAsynchronously)
{
    // The production path: coPreparePruneRows rows land with the block's WriteBatch, onCommit
    // hands deletion to the worker thread. waitForIdle() drains the strand for the assertions.
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
        auto const delta = commitAccountBlock(
            backend, nodes, pruner, accounts, root, changes, block, /*runDeletion=*/false);
        root = delta.stateRoot;
        history.emplace_back(block, root);
        pruner.onCommit(block, delta);
    }
    pruner.waitForIdle();
    BOOST_CHECK_EQUAL(pruner.watermark(), 5);

    // The worker ran to watermark 5: on-disk nodes are exactly the live set of the window
    // [5−N, 5] — the same end state the synchronous deletion pass produces.
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

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::ledger::mpt::test
