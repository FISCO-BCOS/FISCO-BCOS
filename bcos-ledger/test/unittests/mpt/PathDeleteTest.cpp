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
 * @file PathDeleteTest.cpp
 * @brief The four ways a position stops holding a node (pathdb spec A.6), one minimal case each,
 *        plus the over-delete control that proves the asymmetry is real
 */
#include "TestHelpers.h"
#include <bcos-framework/storage2/Storage.h>
#include <bcos-ledger/mpt/Account.h>
#include <bcos-ledger/mpt/Classify.h>
#include <bcos-ledger/mpt/Constants.h>
#include <bcos-ledger/mpt/Errors.h>
#include <bcos-ledger/mpt/HashBuilder.h>
#include <bcos-ledger/mpt/MPTBuilder.h>
#include <bcos-ledger/mpt/MPTReadView.h>
#include <bcos-ledger/mpt/NodeDecoder.h>
#include <bcos-ledger/mpt/PathKey.h>
#include <bcos-ledger/mpt/StorageValueCodec.h>
#include <bcos-ledger/mpt/Trie.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <boost/test/unit_test.hpp>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <variant>

namespace bcos::ledger::mpt::test
{

BOOST_AUTO_TEST_SUITE(PathDeleteSuite)

namespace
{
using NodeStorage = bcos::ledger::mpt::test::NodeMemoryStorage;
using ChangeMap = std::map<bcos::h256, std::optional<bcos::bytes>>;

/// An h256 whose leading nibbles are @p prefix (one nibble per element), the rest zero, with
/// @p tail as its last byte — enough control to build a specific trie shape.
bcos::h256 keyWithPrefix(std::initializer_list<bcos::byte> prefix, bcos::byte tail)
{
    bcos::h256 out{};
    size_t index = 0;
    for (auto const nibble : prefix)
    {
        auto& target = out.data()[index / 2];
        target =
            static_cast<bcos::byte>((index % 2 == 0) ? (nibble << NIBBLE_BITS) : (target | nibble));
        ++index;
    }
    out.data()[bcos::h256::SIZE - 1] = tail;
    return out;
}

/// The first @p count nibbles of @p key — a position, spelled the way the walk builds it.
bcos::bytes positionOf(bcos::h256 const& key, size_t count)
{
    auto const nibbles = bytesToNibbles(key.ref());
    return bcos::bytes(nibbles.begin(), nibbles.begin() + static_cast<std::ptrdiff_t>(count));
}

/// The positions currently holding a node in the account trie.
std::set<bcos::bytes> livePositions(NodeStorage& storage)
{
    std::set<bcos::bytes> out;
    for (auto const& [position, raw] : scanTrieNodes(storage, TrieScope::account()))
    {
        out.insert(position);
    }
    return out;
}

/// The positions a diff deletes, with the scope stripped (every trie here is the account trie).
std::set<bcos::bytes> deletedPositions(PathMergeResult const& result)
{
    std::set<bcos::bytes> out;
    for (auto const& key : result.deletes)
    {
        out.insert(key.position);
    }
    return out;
}

bcos::bytes const POSITION_ROOT{};
}  // namespace

// ── Source 1: a branch collapses and absorbs its surviving LEAF (spec A.6, mergeNormalize) ──────
//
// Two keys diverging at the first nibble make a root branch with two leaf children. Deleting one
// leaves a single child, which folds into the parent as a leaf — so the child's position stops
// existing while the parent's is rewritten.
BOOST_AUTO_TEST_CASE(BranchCollapseAbsorbsSurvivingLeaf)
{
    NodeStorage storage;
    auto const keyA = keyWithPrefix({0x01}, 0xAA);
    auto const keyB = keyWithPrefix({0x02}, 0xBB);
    bcos::bytes const payload(40, 0x5a);  // large enough that each leaf owns a row

    auto const seeded =
        seedTrieFlushed(storage, emptyRootHash(), {{keyA, payload}, {keyB, payload}});
    BOOST_REQUIRE(livePositions(storage) ==
                  (std::set<bcos::bytes>{POSITION_ROOT, bcos::bytes{0x01}, bcos::bytes{0x02}}));

    auto const collapsed = commitTrieFlushed(storage, seeded.root, ChangeMap{{keyB, std::nullopt}});

    // The survivor's position is gone; only the (rewritten) root remains.
    BOOST_CHECK(deletedPositions(collapsed) ==
                (std::set<bcos::bytes>{bcos::bytes{0x01}, bcos::bytes{0x02}}));
    BOOST_CHECK(livePositions(storage) == std::set<bcos::bytes>{POSITION_ROOT});
    // ...and the result is the trie a from-empty build would have produced.
    BOOST_CHECK(collapsed.root == computeTrieRoot({{keyA, payload}}).root);
    BOOST_CHECK(scanTrieNodes(storage, TrieScope::account()) ==
                computeTrieRoot({{keyA, payload}}).newNodes);
}

// ── Source 2: an extension merges with its child (spec A.6, mergeNormalize's extension arm) ─────
//
// Three keys sharing a first nibble: root extension over "0", a branch below it, leaves under
// that. Deleting one of the two leaves under a sub-branch collapses that sub-branch, and the
// extension above then absorbs what it collapsed into.
BOOST_AUTO_TEST_CASE(ExtensionAbsorbsItsCollapsedChild)
{
    NodeStorage storage;
    auto const keyA = keyWithPrefix({0x00, 0x01}, 0xAA);
    auto const keyB = keyWithPrefix({0x00, 0x02}, 0xBB);
    bcos::bytes const payload(40, 0x5a);

    auto const seeded =
        seedTrieFlushed(storage, emptyRootHash(), {{keyA, payload}, {keyB, payload}});
    auto const before = livePositions(storage);
    BOOST_REQUIRE(before.contains(POSITION_ROOT));

    auto const collapsed = commitTrieFlushed(storage, seeded.root, ChangeMap{{keyB, std::nullopt}});

    // One key left: the whole trie is a single leaf at the root position, and every other
    // position the two-key trie used is gone.
    BOOST_CHECK(collapsed.root == computeTrieRoot({{keyA, payload}}).root);
    BOOST_CHECK(livePositions(storage) == std::set<bcos::bytes>{POSITION_ROOT});
    for (auto const& position : before)
    {
        if (position != POSITION_ROOT)
        {
            BOOST_CHECK_MESSAGE(deletedPositions(collapsed).contains(position),
                "position 0x" << bcos::toHex(position) << " vanished without being deleted");
        }
    }
    BOOST_CHECK(scanTrieNodes(storage, TrieScope::account()) ==
                computeTrieRoot({{keyA, payload}}).newNodes);
}

// ── Source 3: crossing the 32-byte inline threshold, in BOTH directions (spec A.6) ──────────────
//
// A node whose RLP is under 32 bytes has no row of its own — it lives inside its parent's
// encoding. So shrinking a leaf past that boundary must DELETE its row, and growing one past it
// must CREATE one. This is the source that is easy to miss, because nothing about the trie's
// shape changes: only the size of a value.
BOOST_AUTO_TEST_CASE(CrossingTheInlineThresholdBothWays)
{
    NodeStorage storage;
    // Two keys diverging only in their LAST nibble: the leaves sit 64 nibbles down with an empty
    // suffix, so their encoding is (almost) just the value — the one place a 32-byte key's trie
    // can produce a node small enough to inline.
    bcos::h256 keyA{};
    bcos::h256 keyB{};
    for (size_t i = 0; i < bcos::h256::SIZE; ++i)
    {
        keyA.data()[i] = 0xaa;
        keyB.data()[i] = 0xaa;
    }
    keyA.data()[bcos::h256::SIZE - 1] = 0xa1;
    keyB.data()[bcos::h256::SIZE - 1] = 0xa2;

    bcos::bytes const large(40, 0x5a);  // leaf RLP > 32 bytes: its own row
    bcos::bytes const small{0x07};      // leaf RLP < 32 bytes: inlined into the branch above it

    auto const rootPosition = POSITION_ROOT;
    auto const branchPosition = positionOf(keyA, 63);
    auto const leafAPosition = positionOf(keyA, 64);
    auto const leafBPosition = positionOf(keyB, 64);

    // Both leaves large: root extension, the branch below it, and one row per leaf.
    auto const seeded = seedTrieFlushed(storage, emptyRootHash(), {{keyA, large}, {keyB, large}});
    BOOST_REQUIRE(livePositions(storage) == (std::set<bcos::bytes>{rootPosition, branchPosition,
                                                leafAPosition, leafBPosition}));

    // Shrink one leaf below the threshold: it becomes an inline child of the branch, and its own
    // row must go — the trie's SHAPE did not change at all, only a value's size.
    auto const shrunk = commitTrieFlushed(storage, seeded.root, ChangeMap{{keyB, small}});
    BOOST_CHECK(deletedPositions(shrunk) == std::set<bcos::bytes>{leafBPosition});
    BOOST_CHECK(livePositions(storage) ==
                (std::set<bcos::bytes>{rootPosition, branchPosition, leafAPosition}));
    BOOST_CHECK(scanTrieNodes(storage, TrieScope::account()) ==
                computeTrieRoot({{keyA, large}, {keyB, small}}).newNodes);

    // Grow it back: the row reappears, and nothing is deleted for it.
    auto const regrown = commitTrieFlushed(storage, shrunk.root, ChangeMap{{keyB, large}});
    BOOST_CHECK(regrown.deletes.empty());
    BOOST_CHECK(regrown.upserts.contains(
        PathKey{.scope = TrieScope::account(), .position = leafBPosition}));
    BOOST_CHECK(livePositions(storage) == (std::set<bcos::bytes>{rootPosition, branchPosition,
                                              leafAPosition, leafBPosition}));
    BOOST_CHECK(regrown.root == seeded.root);
    BOOST_CHECK(scanTrieNodes(storage, TrieScope::account()) ==
                computeTrieRoot({{keyA, large}, {keyB, large}}).newNodes);
}

// ── Source 4: a whole storage trie disappears with its account (spec A.6) ───────────────────────
//
// The one source that is not a single trie's rebuild. Under path addressing an account's storage
// nodes are a contiguous key range, so "delete the whole trie" is a prefix walk — no reachability
// analysis, and no risk of touching another account's identical-looking nodes.
BOOST_AUTO_TEST_CASE(DestroyedAccountDropsItsWholeStorageTrie)
{
    NodeStorage storage;
    auto const victim = makeAddress(0x71);
    auto const bystander = makeAddress(0x72);

    // Both accounts hold the SAME storage: byte-identical tries that a content-addressed store
    // would have shared. Here each owner has its own rows, which is what makes the drop safe.
    std::map<bcos::h256, bcos::bytes> slots;
    for (uint8_t index = 0; index < 6; ++index)
    {
        bcos::h256 slot{};
        slot.data()[bcos::h256::SIZE - 1] = index;
        bcos::h256 value{};
        value.data()[0] = static_cast<bcos::byte>(index + 1);
        slots[slotKeyHash(slot)] = encodeStorageValue(value.ref());
    }
    auto const victimRoot =
        seedTrieFlushed(storage, emptyRootHash(), slots, TrieScope::storage(accountKeyHash(victim)))
            .root;
    auto const bystanderRoot = seedTrieFlushed(
        storage, emptyRootHash(), slots, TrieScope::storage(accountKeyHash(bystander)))
                                   .root;
    BOOST_REQUIRE(victimRoot == bystanderRoot);

    Account victimAccount;
    victimAccount.nonce = 1;
    victimAccount.storageRoot = victimRoot;
    Account bystanderAccount;
    bystanderAccount.nonce = 2;
    bystanderAccount.storageRoot = bystanderRoot;
    auto const parentRoot = seedTrieFlushed(storage, emptyRootHash(),
        {{accountKeyHash(victim), victimAccount.encode()},
            {accountKeyHash(bystander), bystanderAccount.encode()}})
                                .root;

    auto const bystanderRowsBefore =
        scanTrieNodes(storage, TrieScope::storage(accountKeyHash(bystander)));
    BOOST_REQUIRE(!bystanderRowsBefore.empty());

    FlatBackendStorage flatBackend;
    auto view = makeFlatView(flatBackend);
    deleteFlatRowLogically(view, accountFieldKey(victim, ROW_NONCE));
    deleteFlatRowLogically(view, accountFieldKey(victim, ROW_BALANCE));
    deleteFlatRowLogically(view, accountFieldKey(victim, ROW_CODE_HASH));

    auto const output =
        bcos::task::syncWait(buildAndCollect(storage, parentRoot, view, /*l2Mode=*/false));

    BOOST_CHECK(output.droppedStorageTries == std::vector<bcos::h256>{accountKeyHash(victim)});
    BOOST_CHECK(scanTrieNodes(storage, TrieScope::storage(accountKeyHash(victim))).empty());
    // The bystander's byte-identical trie is untouched: separate key spaces, no shared rows.
    BOOST_CHECK(scanTrieNodes(storage, TrieScope::storage(accountKeyHash(bystander))) ==
                bystanderRowsBefore);
    // Every dropped row's prior bytes were archived (spec A.7: deletes carry preimages too).
    for (auto const& key : output.deletes)
    {
        BOOST_CHECK_MESSAGE(output.preimages.contains(key),
            "deleted position 0x" << bcos::toHex(key.position) << " has no preimage");
    }
    // The survivor still reads back through the new root.
    MPTReadView<NodeStorage> readView(storage, output.stateRoot);
    auto const stored = bcos::task::syncWait(readView.readAccount(bystander));
    BOOST_REQUIRE(stored.has_value());
    BOOST_CHECK(stored->storageRoot == bystanderRoot);
}

// ── The survivor's SUBTREE, not just the survivor ───────────────────────────────────────────────
//
// mergeNormalize's third arm keeps an unmodified surviving BRANCH by reverting its slot to a hash
// reference — nothing under it is re-emitted, because nothing under it changed. The read ledger
// has to be un-recorded for that whole subtree, not only for the survivor's own position: a
// position resolved BELOW the survivor earlier in the same batch is equally "read and not
// re-emitted", and deleting it punches a hole into a subtree the survivor's unchanged hash still
// vouches for.
//
// The batch that gets there needs no exotic input: an erase MISS that happens to descend into the
// survivor, followed by an erase that collapses the parent onto it. Change order is std::map
// order, i.e. key order, so which of the two comes first is decided by the keys themselves.
BOOST_AUTO_TEST_CASE(BranchCollapseKeepsTheSurvivorsWholeSubtree)
{
    NodeStorage storage;
    // Root branch: nibble 0 -> a BRANCH (two leaves under it), nibble f -> a lone leaf.
    auto const keyA = keyWithPrefix({0x00, 0x00}, 0xAA);  // leaf at "00"
    auto const keyB = keyWithPrefix({0x00, 0x01}, 0xBB);  // leaf at "01"
    auto const lone = keyWithPrefix({0x0f}, 0xCC);        // leaf at "f"
    bcos::bytes const payload(40, 0x5a);

    auto const seeded = seedTrieFlushed(
        storage, emptyRootHash(), {{keyA, payload}, {keyB, payload}, {lone, payload}});
    BOOST_REQUIRE(livePositions(storage) ==
                  (std::set<bcos::bytes>{POSITION_ROOT, bcos::bytes{0x00}, bcos::bytes{0x00, 0x00},
                      bcos::bytes{0x00, 0x01}, bcos::bytes{0x0f}}));

    // One block, two changes. The miss sorts FIRST (it shares keyA's leading 0x00 byte, the lone
    // key starts 0x0f), so it resolves "", "0" and "00" while leaving every node clean; then the
    // real erase collapses the root onto the surviving branch at "0".
    auto const miss = keyWithPrefix({0x00, 0x00}, 0xDD);  // same path as keyA, different leaf
    BOOST_REQUIRE(miss < lone);
    auto const collapsed = commitTrieFlushed(
        storage, seeded.root, ChangeMap{{miss, std::nullopt}, {lone, std::nullopt}});

    // Only the lone leaf's position goes. "00" was read on the miss path and is still live under
    // the survivor's unchanged hash.
    BOOST_CHECK(deletedPositions(collapsed) == std::set<bcos::bytes>{bcos::bytes{0x0f}});
    BOOST_CHECK(livePositions(storage) == (std::set<bcos::bytes>{POSITION_ROOT, bcos::bytes{0x00},
                                              bcos::bytes{0x00, 0x00}, bcos::bytes{0x00, 0x01}}));
    BOOST_CHECK(collapsed.root == computeTrieRoot({{keyA, payload}, {keyB, payload}}).root);
    BOOST_CHECK(scanTrieNodes(storage, TrieScope::account()) ==
                computeTrieRoot({{keyA, payload}, {keyB, payload}}).newNodes);
    // A position kept out of `deletes` must not be archived as if it had gone.
    for (auto const& [key, prior] : collapsed.preimages)
    {
        BOOST_CHECK_MESSAGE(collapsed.upserts.contains(key) || collapsed.deletes.contains(key),
            "preimage for position 0x" << bcos::toHex(key.position)
                                       << " that is neither written nor deleted");
    }

    // The whole trie still reads: the hole this guards against only shows up on the NEXT read.
    Trie<NodeStorage> const trie(storage, TrieScope::account(), collapsed.root);
    for (auto const& key : {keyA, keyB})
    {
        auto const leaf = bcos::task::syncWait(trie.get(key));
        BOOST_REQUIRE_MESSAGE(leaf.has_value(), "key unreadable after the collapse");
        BOOST_CHECK(*leaf == payload);
    }
    BOOST_CHECK(!bcos::task::syncWait(trie.get(lone)).has_value());
}

// ── G4: the asymmetry ───────────────────────────────────────────────────────────────────────────
//
// Deleting one position too FEW leaves an unreachable row: waste, and nothing more — the next
// block overwrites or ignores it. Deleting one too MANY punches a hole in a live trie, and the
// next block that walks through it must STOP, loudly, rather than rebuild around the gap.
BOOST_AUTO_TEST_CASE(OneExtraDeleteStopsTheNextBlock)
{
    NodeStorage storage;
    std::map<bcos::h256, bcos::bytes> entries;
    for (uint8_t nibble = 0; nibble < NIBBLE_RANGE; ++nibble)
    {
        entries[keyWithPrefix({nibble}, 0xAA)] = bcos::bytes(40, nibble + 1);
    }
    auto const seeded = seedTrieFlushed(storage, emptyRootHash(), entries);

    // Benign direction: an EXTRA row nobody references changes nothing. The next block still
    // builds, and its root is the one the state implies.
    {
        NodeStorage benign = storage;
        bcos::task::syncWait(bcos::storage2::writeOne(benign,
            PathKey{.scope = TrieScope::account(), .position = bcos::bytes{0x0f, 0x0f, 0x0f}},
            bcos::bytes(40, 0xEE)));
        auto changes = ChangeMap{{keyWithPrefix({0x00}, 0xAA), bcos::bytes(40, 0x77)}};
        auto const next = commitTrieFlushed(benign, seeded.root, changes);
        auto expected = entries;
        expected[keyWithPrefix({0x00}, 0xAA)] = bcos::bytes(40, 0x77);
        BOOST_CHECK(next.root == computeTrieRoot(expected).root);
    }

    // Fatal direction: remove ONE live position and rebuild through it.
    {
        NodeStorage holed = storage;
        PathKey const victim{.scope = TrieScope::account(), .position = bcos::bytes{0x03}};
        BOOST_REQUIRE(bcos::task::syncWait(bcos::storage2::readOne(holed, victim)).has_value());
        bcos::task::syncWait(bcos::storage2::removeOne(holed, victim));

        auto changes = ChangeMap{{keyWithPrefix({0x03}, 0xAA), bcos::bytes(40, 0x77)}};
        BOOST_CHECK_THROW(commitTrieFlushed(holed, seeded.root, changes), MPTInvariantViolation);
    }
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::ledger::mpt::test
