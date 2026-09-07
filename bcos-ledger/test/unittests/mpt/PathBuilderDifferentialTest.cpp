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
 * @file PathBuilderDifferentialTest.cpp
 * @brief The differential judge for path addressing: after every block, the incrementally
 *        maintained node store must be row-for-row identical to a from-empty build of the same
 *        state (pathdb spec appendix A)
 */
#include "TestHelpers.h"
#include <bcos-framework/storage2/Storage.h>
#include <bcos-ledger/mpt/Account.h>
#include <bcos-ledger/mpt/Classify.h>
#include <bcos-ledger/mpt/Constants.h>
#include <bcos-ledger/mpt/HashBuilder.h>
#include <bcos-ledger/mpt/MPTBuilder.h>
#include <bcos-ledger/mpt/MPTReadView.h>
#include <bcos-ledger/mpt/PathKey.h>
#include <bcos-ledger/mpt/StorageValueCodec.h>
#include <bcos-ledger/mpt/Trie.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <map>
#include <random>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace bcos::ledger::mpt::test
{

BOOST_AUTO_TEST_SUITE(PathBuilderDifferentialSuite)

namespace
{
using NodeStorage = bcos::ledger::mpt::test::NodeMemoryStorage;

/// The state the test believes the chain holds, independent of any trie: exactly the flat rows a
/// block writes, remembered. Everything the judge compares against is derived from THIS.
struct AccountModel
{
    bcos::u256 nonce;
    bcos::u256 balance;
    bcos::h256 codeHash;
    /// slot -> its 32-byte value; a slot written to zero is erased, matching what the builder
    /// does with a value that RLP-trims to nothing.
    std::map<bcos::h256, bcos::h256> slots;
};
using Model = std::map<bcos::Address, AccountModel>;

/// A 32-byte slot key for index i.
bcos::h256 slotAt(size_t index)
{
    bcos::h256 out{};
    out.data()[bcos::h256::SIZE - 1] = static_cast<bcos::byte>(index & 0xFFU);
    out.data()[bcos::h256::SIZE - 2] = static_cast<bcos::byte>((index >> 8U) & 0xFFU);
    return out;
}

/// A slot value wide enough that some leaves exceed the 32-byte inline threshold and some do not
/// — the inline-threshold delete source (spec A.6-3) only fires when both sides occur.
bcos::h256 slotValue(std::mt19937& rng)
{
    bcos::h256 out{};
    // Half the values are a single low byte (tiny leaf, inlined into its parent), half fill the
    // whole word (large leaf, its own row).
    if ((rng() % 2U) == 0U)
    {
        out.data()[bcos::h256::SIZE - 1] = static_cast<bcos::byte>(1U + (rng() % 250U));
        return out;
    }
    for (auto& byte : out)
    {
        byte = static_cast<bcos::byte>(1U + (rng() % 250U));
    }
    return out;
}

/// The storage trie one modelled account should have, built from empty.
TrieBuildResult storageTrieOf(AccountModel const& account)
{
    std::map<bcos::h256, bcos::bytes> entries;
    for (auto const& [slot, value] : account.slots)
    {
        auto encoded = encodeStorageValue(value.ref());
        if (!encoded.empty())
        {
            entries[slotKeyHash(slot)] = std::move(encoded);
        }
    }
    return computeTrieRoot(entries);
}

/// The account trie the model implies, built from empty. Returns the root and, per scope, every
/// row that trie version must consist of — the oracle both the root check and the row check use.
struct Oracle
{
    bcos::h256 root;
    std::map<TrieScope, std::map<bcos::bytes, bcos::bytes>> rows;
};

Oracle buildOracle(Model const& model)
{
    Oracle out;
    std::map<bcos::h256, bcos::bytes> accountEntries;
    for (auto const& [address, account] : model)
    {
        auto storageTrie = storageTrieOf(account);
        auto const owner = accountKeyHash(address);
        if (!storageTrie.newNodes.empty())
        {
            out.rows.emplace(TrieScope::storage(owner), std::move(storageTrie.newNodes));
        }
        Account leaf;
        leaf.nonce = account.nonce;
        leaf.balance = account.balance;
        leaf.codeHash = account.codeHash;
        leaf.storageRoot = storageTrie.root;
        accountEntries[owner] = leaf.encode();
    }
    auto accountTrie = computeTrieRoot(accountEntries);
    out.root = accountTrie.root;
    if (!accountTrie.newNodes.empty())
    {
        out.rows.emplace(TrieScope::account(), std::move(accountTrie.newNodes));
    }
    return out;
}

/// Fold a finished block's delta layer into the flat backend, so the NEXT block reads the parent
/// state through it (first-touch metadata) exactly as production does after mergeBackStorage.
void applyDeltaToBackend(FlatStateView& view, FlatBackendStorage& backend)
{
    bcos::task::syncWait(
        [](FlatStateView& view, FlatBackendStorage& backend) -> bcos::task::Task<void> {
            auto iterator = co_await bcos::storage2::range(mutableStorage(view));
            while (true)
            {
                auto keyValue = co_await iterator.next();
                if (!keyValue)
                {
                    break;
                }
                auto const& [key, value] = *keyValue;
                if (auto const* entry = std::get_if<bcos::storage::Entry>(std::addressof(value)))
                {
                    co_await bcos::storage2::writeOne(backend, key, *entry);
                }
                else
                {
                    co_await bcos::storage2::removeOne(backend, key);
                }
            }
            co_return;
        }(view, backend));
}

/// Write @p value into @p view as the flat row the executor would produce for a slot.
void writeSlotRow(FlatStateView& view, bcos::Address const& address, bcos::h256 const& slot,
    bcos::h256 const& value)
{
    writeFlatRow(view, accountSlotKey(address, slot),
        makeEntry(std::string_view{reinterpret_cast<char const*>(value.data()), bcos::h256::SIZE}));
}

/// The lowest slot index >= @p from whose slotKeyHash begins with @p prefixNibbles.
///
/// Trie SHAPE is a function of keccak, so a test that needs a particular shape has to search for
/// keys that produce it. A one-byte prefix is one in 256, so this returns in microseconds.
bcos::h256 slotWithHashPrefix(
    std::vector<bcos::byte> const& prefixNibbles, size_t from, size_t& found)
{
    for (size_t index = from; index < from + 100000; ++index)
    {
        auto const slot = slotAt(index);
        auto const nibbles = bytesToNibbles(slotKeyHash(slot).ref());
        if (std::equal(prefixNibbles.begin(), prefixNibbles.end(), nibbles.begin()))
        {
            found = index;
            return slot;
        }
    }
    BOOST_FAIL("no slot found with the requested hash prefix");
    return {};
}

void writeCodeHashRow(FlatStateView& view, bcos::Address const& address, bcos::h256 const& codeHash)
{
    writeFlatRow(view, accountFieldKey(address, ROW_CODE_HASH),
        makeEntry(
            std::string_view{reinterpret_cast<char const*>(codeHash.data()), bcos::h256::SIZE}));
}
}  // namespace

// ── The judge ──────────────────────────────────────────────────────────────────────────────────
//
// 200 seeded-random blocks over 40 accounts, mixing account creation, balance/nonce updates, slot
// writes, slot deletions and SELFDESTRUCT. After EVERY block:
//
//   (a) the incremental state root equals a from-empty build over the model's flat state;
//   (b) the rows on disk, found by prefix scan of each trie, are byte-for-byte the rows that
//       from-empty build produces — same positions, same encodings;
//   (c) therefore no orphan positions: (b) is an equality, so a row the from-empty build does not
//       produce is a failure whether or not anything still points at it;
//   (d) every account reads back through MPTReadView at the new root, and every live slot through
//       its account's storage trie.
//
// (b) is the property that makes deletes provable at all. A hash-keyed store could only ever ask
// "is this node still reachable"; a path-keyed one can ask "is the row set exactly right", and
// that catches a missed delete (an orphan) and an over-delete (a hole) with the same assertion.
BOOST_AUTO_TEST_CASE(TwoHundredRandomBlocksStayIdenticalToAFromEmptyBuild)
{
    constexpr size_t BLOCK_COUNT = 200;
    constexpr size_t ACCOUNT_COUNT = 40;
    constexpr size_t SLOT_COUNT = 8;

    auto rng = seededRng(0x9A7CD8E1);
    NodeStorage nodeStorage;
    FlatBackendStorage flatBackend;
    Model model;
    std::set<bcos::h256> everSeenOwners;
    bcos::h256 parentRoot = emptyRootHash();

    for (size_t blockNumber = 1; blockNumber <= BLOCK_COUNT; ++blockNumber)
    {
        auto view = makeFlatView(flatBackend);
        Model expected = model;

        // 1..4 account actions per block.
        size_t const actions = 1 + (rng() % 4U);
        std::set<bcos::Address> touched;
        for (size_t action = 0; action < actions; ++action)
        {
            auto const address = makeAddress(static_cast<uint8_t>(1U + (rng() % ACCOUNT_COUNT)));
            if (!touched.insert(address).second)
            {
                continue;  // one run per account per block: that is the shape the scan assumes
            }
            bool const exists = expected.contains(address);

            // Tombstone a live account roughly one action in eight.
            if (exists && (rng() % 8U) == 0U)
            {
                deleteFlatRowLogically(view, accountFieldKey(address, ROW_NONCE));
                deleteFlatRowLogically(view, accountFieldKey(address, ROW_BALANCE));
                deleteFlatRowLogically(view, accountFieldKey(address, ROW_CODE_HASH));
                for (auto const& [slot, value] : expected.at(address).slots)
                {
                    deleteFlatRowLogically(view, accountSlotKey(address, slot));
                }
                expected.erase(address);
                continue;
            }

            auto& account = expected[address];
            if (!exists)
            {
                // Birth: all three core fields written, exactly as CREATE produces them.
                account.nonce = bcos::u256(1);
                account.balance = bcos::u256(rng() % 100000U);
                account.codeHash =
                    (rng() % 2U == 0U) ? emptyCodeHash() : makeHash(static_cast<uint8_t>(rng()));
                writeFlatRow(view, accountFieldKey(address, ROW_NONCE),
                    makeEntry(account.nonce.str({}, {})));
                writeFlatRow(view, accountFieldKey(address, ROW_BALANCE),
                    makeEntry(account.balance.str({}, {})));
                writeCodeHashRow(view, address, account.codeHash);
                everSeenOwners.insert(accountKeyHash(address));
            }
            else if ((rng() % 2U) == 0U)
            {
                // A plain balance move: one core row, the parent leaf supplies the rest.
                account.balance += bcos::u256(1 + (rng() % 1000U));
                writeFlatRow(view, accountFieldKey(address, ROW_BALANCE),
                    makeEntry(account.balance.str({}, {})));
            }
            else
            {
                account.nonce += 1;
                writeFlatRow(view, accountFieldKey(address, ROW_NONCE),
                    makeEntry(account.nonce.str({}, {})));
            }

            // 0..3 slot operations on this account.
            size_t const slotOps = rng() % 4U;
            for (size_t op = 0; op < slotOps; ++op)
            {
                auto const slot = slotAt(rng() % SLOT_COUNT);
                switch (rng() % 4U)
                {
                case 0:  // storage-level delete (removeSome on the delta layer)
                    account.slots.erase(slot);
                    deleteFlatRowLogically(view, accountSlotKey(address, slot));
                    break;
                case 1:  // write zero: RLP-trims to nothing, so the trie drops the leaf too
                    account.slots.erase(slot);
                    writeSlotRow(view, address, slot, bcos::h256{});
                    break;
                default:
                {
                    auto const value = slotValue(rng);
                    account.slots[slot] = value;
                    writeSlotRow(view, address, slot, value);
                    break;
                }
                }
            }
        }

        auto output =
            bcos::task::syncWait(buildAndCollect(nodeStorage, parentRoot, view, /*l2Mode=*/false));
        applyDeltaToBackend(view, flatBackend);
        model = std::move(expected);

        // Spec A.7: every touched position carries a preimage, a newly occupied one included
        // (recorded as "nothing was here"). A missing entry would let a reader walking backwards
        // conclude the node had always been there.
        for (auto const& [key, raw] : output.upserts)
        {
            BOOST_REQUIRE_MESSAGE(output.preimages.contains(key),
                "block " << blockNumber << ": upsert at position 0x" << bcos::toHex(key.position)
                         << " has no preimage");
        }
        for (auto const& key : output.deletes)
        {
            BOOST_REQUIRE_MESSAGE(output.preimages.contains(key),
                "block " << blockNumber << ": delete at position 0x" << bcos::toHex(key.position)
                         << " has no preimage");
        }
        // ...and the two sets never name the same row.
        for (auto const& key : output.deletes)
        {
            BOOST_REQUIRE_MESSAGE(!output.upserts.contains(key),
                "block " << blockNumber << ": position 0x" << bcos::toHex(key.position)
                         << " is both written and deleted");
        }

        // (a) the root the chain would sign.
        auto const oracle = buildOracle(model);
        BOOST_REQUIRE_MESSAGE(output.stateRoot == oracle.root,
            "state root diverged at block " << blockNumber << ": incremental "
                                            << output.stateRoot.hex() << " vs from-empty "
                                            << oracle.root.hex());

        // (b)+(c) the rows on disk are exactly the rows a from-empty build produces, per trie.
        auto const accountRows = scanTrieNodes(nodeStorage, TrieScope::account());
        auto const expectedAccountRows = oracle.rows.contains(TrieScope::account()) ?
                                             oracle.rows.at(TrieScope::account()) :
                                             std::map<bcos::bytes, bcos::bytes>{};
        BOOST_REQUIRE_MESSAGE(accountRows == expectedAccountRows,
            "account trie rows diverged at block " << blockNumber << " (" << accountRows.size()
                                                   << " on disk vs " << expectedAccountRows.size()
                                                   << " expected)");
        for (auto const& owner : everSeenOwners)
        {
            auto const scope = TrieScope::storage(owner);
            auto const rows = scanTrieNodes(nodeStorage, scope);
            auto const expectedRows = oracle.rows.contains(scope) ?
                                          oracle.rows.at(scope) :
                                          std::map<bcos::bytes, bcos::bytes>{};
            BOOST_REQUIRE_MESSAGE(rows == expectedRows,
                "storage trie rows diverged at block "
                    << blockNumber << " for owner " << owner.hex() << " (" << rows.size()
                    << " on disk vs " << expectedRows.size() << " expected)");
        }

        // (d) the rows actually serve reads: every account and every live slot resolves.
        MPTReadView<NodeStorage> readView(nodeStorage, output.stateRoot);
        for (auto const& [address, account] : model)
        {
            auto const stored = bcos::task::syncWait(readView.readAccount(address));
            BOOST_REQUIRE_MESSAGE(stored.has_value(),
                "account " << address.hex() << " unreadable at block " << blockNumber);
            BOOST_CHECK(stored->nonce == account.nonce);
            BOOST_CHECK(stored->balance == account.balance);
            BOOST_CHECK(stored->codeHash == account.codeHash);
            if (account.slots.empty())
            {
                BOOST_CHECK(stored->storageRoot == emptyRootHash());
                continue;
            }
            Trie<NodeStorage> trie(
                nodeStorage, TrieScope::storage(accountKeyHash(address)), stored->storageRoot);
            for (auto const& [slot, value] : account.slots)
            {
                auto const leaf = bcos::task::syncWait(trie.get(slotKeyHash(slot)));
                BOOST_REQUIRE_MESSAGE(leaf.has_value(),
                    "slot missing at block " << blockNumber << " for " << address.hex());
                BOOST_CHECK(*leaf == encodeStorageValue(value.ref()));
            }
        }

        parentRoot = output.stateRoot;
    }

    // The run must have exercised the shapes it was built for, or the judge proved nothing.
    BOOST_CHECK_MESSAGE(model.size() < ACCOUNT_COUNT,
        "no account was destroyed over the whole run: the tombstone path went untested");
    BOOST_CHECK(everSeenOwners.size() == ACCOUNT_COUNT);
}

// ── The shape the random workload cannot reach ─────────────────────────────────────────────────
//
// A branch collapsing onto a surviving BRANCH, after an erase MISS already descended into that
// survivor, is the one delete shape whose bookkeeping is special (TrieMerge's mergeNormalize
// hands the survivor back to disk by hash, so nothing under it is re-emitted). Random slot writes
// over fixed keys never produce it — the shape needs three specific keccak prefixes and a
// specific change ORDER — so the judge above cannot guard it, and it is built here by hand and
// fed through exactly the same row-equality oracle.
//
// The trie: one account's storage, three slots whose hashes begin 00 / 01 / f. That makes a root
// branch with two children, the one at nibble 0 being itself a branch. The block then writes ZERO
// to a fourth slot that never existed and whose hash also begins 00 (the executor's shape for
// "slot cleared" — it reaches the builder as a delete of an absent key, i.e. a MISS that resolves
// the survivor and its leaf) and ZERO to the slot at nibble f (the real delete that collapses the
// root onto the survivor). Change order is slotKeyHash order, so the miss goes first.
BOOST_AUTO_TEST_CASE(CollapseOntoASurvivingBranchAfterAMissKeepsItsSubtree)
{
    NodeStorage nodeStorage;
    FlatBackendStorage flatBackend;
    auto const address = makeAddress(0x5b);
    auto const owner = accountKeyHash(address);
    auto const scope = TrieScope::storage(owner);

    size_t cursor = 0;
    auto const slot00 = slotWithHashPrefix({0x00, 0x00}, 0, cursor);
    auto const slot01 = slotWithHashPrefix({0x00, 0x01}, cursor + 1, cursor);
    auto const slotF = slotWithHashPrefix({0x0f}, cursor + 1, cursor);
    auto const slotMiss = slotWithHashPrefix({0x00, 0x00}, cursor + 1, cursor);
    BOOST_REQUIRE(slotKeyHash(slotMiss) != slotKeyHash(slot00));
    BOOST_REQUIRE(slotKeyHash(slotMiss) < slotKeyHash(slotF));  // the miss is applied first

    // Block 1: the account and its three slots.
    Model model;
    auto& account = model[address];
    account.nonce = bcos::u256(1);
    account.balance = bcos::u256(500);
    account.codeHash = makeHash(0x7c);
    bcos::h256 parentRoot;
    {
        auto view = makeFlatView(flatBackend);
        writeFlatRow(
            view, accountFieldKey(address, ROW_NONCE), makeEntry(account.nonce.str({}, {})));
        writeFlatRow(
            view, accountFieldKey(address, ROW_BALANCE), makeEntry(account.balance.str({}, {})));
        writeCodeHashRow(view, address, account.codeHash);
        for (auto const& slot : {slot00, slot01, slotF})
        {
            bcos::h256 value{};
            value.data()[0] = 0x11;
            value.data()[bcos::h256::SIZE - 1] = slot.data()[bcos::h256::SIZE - 1];
            account.slots[slot] = value;
            writeSlotRow(view, address, slot, value);
        }
        auto output = bcos::task::syncWait(
            buildAndCollect(nodeStorage, emptyRootHash(), view, /*l2Mode=*/false));
        applyDeltaToBackend(view, flatBackend);
        parentRoot = output.stateRoot;
    }

    // The shape this case exists for: root branch, a branch below nibble 0, a lone leaf at f.
    auto const seededRows = scanTrieNodes(nodeStorage, scope);
    BOOST_REQUIRE_MESSAGE(
        seededRows.contains(bcos::bytes{}) && seededRows.contains(bcos::bytes{0x00}) &&
            seededRows.contains(bcos::bytes{0x00, 0x00}) &&
            seededRows.contains(bcos::bytes{0x00, 0x01}) && seededRows.contains(bcos::bytes{0x0f}),
        "the seeded storage trie does not have the branch-under-nibble-0 shape");

    // Block 2: zero the never-written slot (a miss into the survivor) and zero the lone slot.
    {
        auto view = makeFlatView(flatBackend);
        writeSlotRow(view, address, slotMiss, bcos::h256{});
        writeSlotRow(view, address, slotF, bcos::h256{});
        account.slots.erase(slotF);
        auto output =
            bcos::task::syncWait(buildAndCollect(nodeStorage, parentRoot, view, /*l2Mode=*/false));
        applyDeltaToBackend(view, flatBackend);

        auto const oracle = buildOracle(model);
        BOOST_CHECK(output.stateRoot == oracle.root);
        // THE assertion: the survivor's subtree is still on disk, whole. A state root alone would
        // not notice — the root is computed from the survivor's unchanged hash, so it matches even
        // when the rows beneath it have been deleted.
        BOOST_CHECK(scanTrieNodes(nodeStorage, scope) == oracle.rows.at(scope));
        BOOST_CHECK(scanTrieNodes(nodeStorage, TrieScope::account()) ==
                    oracle.rows.at(TrieScope::account()));

        // ...and the reads that would hit the hole actually run.
        MPTReadView<NodeStorage> readView(nodeStorage, output.stateRoot);
        auto const stored = bcos::task::syncWait(readView.readAccount(address));
        BOOST_REQUIRE(stored.has_value());
        Trie<NodeStorage> const trie(nodeStorage, scope, stored->storageRoot);
        for (auto const& [slot, value] : model.at(address).slots)
        {
            auto const leaf = bcos::task::syncWait(trie.get(slotKeyHash(slot)));
            BOOST_REQUIRE_MESSAGE(leaf.has_value(), "slot unreadable after the collapse");
            BOOST_CHECK(*leaf == encodeStorageValue(value.ref()));
        }
        BOOST_CHECK(!bcos::task::syncWait(trie.get(slotKeyHash(slotF))).has_value());
    }
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::ledger::mpt::test
