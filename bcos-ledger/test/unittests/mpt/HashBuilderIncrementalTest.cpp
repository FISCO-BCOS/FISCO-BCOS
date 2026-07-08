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
 * @file HashBuilderIncrementalTest.cpp
 * @brief Incremental (non-empty prior root) HashBuilder rebuild — equivalence, readback and
 *        pruning-safety oracles (spec §5.3 path 1, §5.4)
 */
#include "TestHelpers.h"
#include <bcos-crypto/hasher/AnyHasher.h>
#include <bcos-crypto/hasher/OpenSSLHasher.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-ledger/mpt/Constants.h>
#include <bcos-ledger/mpt/HashBuilder.h>
#include <bcos-ledger/mpt/NodeDecoder.h>
#include <bcos-ledger/mpt/Trie.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <boost/test/unit_test.hpp>
#include <map>
#include <optional>
#include <random>
#include <unordered_set>
#include <vector>

namespace bcos::ledger::mpt::test
{

BOOST_AUTO_TEST_SUITE(HashBuilderIncrementalSuite)

namespace
{
using NodeStorage = bcos::storage2::memory_storage::MemoryStorage<bcos::h256, bcos::bytes>;
using ChangeMap = std::map<bcos::h256, std::optional<bcos::bytes>>;
using KeyValueMap = std::map<bcos::h256, bcos::bytes>;

// Build the base trie from @p base into @p storage and return its root.
bcos::h256 buildBase(NodeStorage& storage, KeyValueMap const& base)
{
    HashBuilder builder(storage, emptyRootHash());
    for (auto const& [key, value] : base)
    {
        bcos::task::syncWait(builder.put(key, value));
    }
    return bcos::task::syncWait(builder.commit());
}

// The expected post-change key set: base with puts applied and deletes removed.
KeyValueMap applyChanges(KeyValueMap base, ChangeMap const& changes)
{
    for (auto const& [key, valueOpt] : changes)
    {
        if (valueOpt.has_value())
        {
            base[key] = *valueOpt;
        }
        else
        {
            base.erase(key);
        }
    }
    return base;
}

// Run the incremental commit and return {newRoot, builder-after-commit} drains via out-params.
bcos::h256 incrementalCommit(NodeStorage& storage, bcos::h256 priorRoot, ChangeMap const& changes,
    std::unordered_map<bcos::h256, bcos::bytes>& outNewNodes,
    std::unordered_set<bcos::h256>& outObsoleted)
{
    HashBuilder builder(storage, priorRoot);
    for (auto const& [key, valueOpt] : changes)
    {
        if (valueOpt.has_value())
        {
            bcos::task::syncWait(builder.put(key, *valueOpt));
        }
        else
        {
            bcos::task::syncWait(builder.remove(key));
        }
    }
    auto root = bcos::task::syncWait(builder.commit());
    outNewNodes = builder.drainNewNodes();
    outObsoleted = builder.drainObsoletedNodes();
    return root;
}

// Every key in @p expected reads back its value through a Trie over @p storage at @p root, and
// every key in @p absent reads back nothing.
void checkReadback(NodeStorage& storage, bcos::h256 root, KeyValueMap const& expected,
    std::vector<bcos::h256> const& absent)
{
    Trie<NodeStorage> trie(storage, root);
    for (auto const& [key, value] : expected)
    {
        auto got = bcos::task::syncWait(trie.get(key));
        BOOST_REQUIRE_MESSAGE(got.has_value(), "missing key after incremental commit");
        BOOST_CHECK(*got == value);
    }
    for (auto const& key : absent)
    {
        auto got = bcos::task::syncWait(trie.get(key));
        BOOST_CHECK_MESSAGE(!got.has_value(), "deleted key still readable");
    }
}

// Collect the hashes of every hash-addressed node reachable from @p root (the live node set of
// this trie version). Extension children and branch children are followed; inline children live
// inside their parent's encoding and have no hash of their own.
std::unordered_set<bcos::h256> reachableHashes(NodeStorage& storage, bcos::h256 root)
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
                if (child.kind == NodeRef::Kind::Hash)
                {
                    queue.push_back(child.hash);
                }
            }
        }
    }
    return out;
}

// A pseudorandom h256/value pair stream with a fixed seed.
bcos::h256 randomHash(std::mt19937& rng)
{
    bcos::h256 out;
    for (auto& byte : out)
    {
        byte = static_cast<bcos::byte>(rng() & 0xFFU);
    }
    return out;
}

bcos::bytes randomValue(std::mt19937& rng)
{
    bcos::bytes out(1 + (rng() % 48), 0);
    for (auto& byte : out)
    {
        byte = static_cast<bcos::byte>(rng() & 0xFFU);
    }
    return out;
}

// h256 from a 64-char hex string (for hand-crafted prefix-sharing keys).
bcos::h256 hexKey(std::string_view hex)
{
    return bcos::h256(std::string(hex), bcos::h256::StringDataType::FromHex);
}

// Full oracle bundle for one incremental step: equivalence against BOTH independent from-scratch
// builders, readback through Trie, and pruning safety (no obsoleted node is still referenced by
// the new version; live nodes survive dropping the obsoleted set).
void checkIncrementalStep(
    NodeStorage& storage, bcos::h256 priorRoot, KeyValueMap const& base, ChangeMap const& changes)
{
    std::unordered_map<bcos::h256, bcos::bytes> newNodes;
    std::unordered_set<bcos::h256> obsoleted;
    auto const newRoot = incrementalCommit(storage, priorRoot, changes, newNodes, obsoleted);

    // Equivalence: the incremental root must match a from-scratch build of the merged key set —
    // once via the stateless production core, once via the independent reference trie.
    auto const expected = applyChanges(base, changes);
    BOOST_CHECK(newRoot == computeTrieRoot(expected).root);
    BOOST_CHECK(newRoot == referenceRoot({expected.begin(), expected.end()}));

    // Readback: every surviving key resolves through the storage the commit flushed into.
    std::vector<bcos::h256> absent;
    for (auto const& [key, valueOpt] : changes)
    {
        if (!valueOpt.has_value())
        {
            absent.push_back(key);
        }
    }
    checkReadback(storage, newRoot, expected, absent);

    // Pruning safety: obsoleted nodes are exactly dead weight — nothing the new version
    // references may be in the obsoleted set...
    auto const live = reachableHashes(storage, newRoot);
    for (auto const& hash : obsoleted)
    {
        BOOST_CHECK_MESSAGE(!live.contains(hash), "obsoleted node still referenced by new root");
    }
    // ...and dropping (prior nodes ∩ obsoleted) while keeping newNodes must leave every live
    // node resolvable: rebuild a pruned storage and re-run the readback.
    NodeStorage pruned;
    for (auto const& hash : live)
    {
        auto raw = bcos::task::syncWait(bcos::storage2::readOne(storage, hash));
        BOOST_REQUIRE(raw.has_value());
        BOOST_REQUIRE_MESSAGE(!obsoleted.contains(hash), "live node would be pruned");
        bcos::task::syncWait(bcos::storage2::writeOne(pruned, hash, *raw));
    }
    checkReadback(pruned, newRoot, expected, absent);
}
}  // namespace

BOOST_AUTO_TEST_CASE(UpdateSingleLeafValue)
{
    NodeStorage storage;
    KeyValueMap const base{{makeHash(0x11), bcos::bytes{0x01}}};
    auto const priorRoot = buildBase(storage, base);
    checkIncrementalStep(storage, priorRoot, base, ChangeMap{{makeHash(0x11), bcos::bytes{0x02}}});
}

BOOST_AUTO_TEST_CASE(NoOpPutKeepsRootAndObsoletesNothing)
{
    NodeStorage storage;
    KeyValueMap const base{
        {makeHash(0x11), bcos::bytes{0x01}}, {makeHash(0x22), bcos::bytes{0x02}}};
    auto const priorRoot = buildBase(storage, base);

    std::unordered_map<bcos::h256, bcos::bytes> newNodes;
    std::unordered_set<bcos::h256> obsoleted;
    auto const newRoot = incrementalCommit(
        storage, priorRoot, ChangeMap{{makeHash(0x11), bcos::bytes{0x01}}}, newNodes, obsoleted);

    BOOST_CHECK(newRoot == priorRoot);
    BOOST_CHECK(obsoleted.empty());
}

BOOST_AUTO_TEST_CASE(DeleteNonexistentKeyIsNoop)
{
    NodeStorage storage;
    KeyValueMap const base{
        {makeHash(0x11), bcos::bytes{0x01}}, {makeHash(0x22), bcos::bytes{0x02}}};
    auto const priorRoot = buildBase(storage, base);

    std::unordered_map<bcos::h256, bcos::bytes> newNodes;
    std::unordered_set<bcos::h256> obsoleted;
    auto const newRoot = incrementalCommit(
        storage, priorRoot, ChangeMap{{makeHash(0x33), std::nullopt}}, newNodes, obsoleted);

    BOOST_CHECK(newRoot == priorRoot);
    BOOST_CHECK(newNodes.empty());
    BOOST_CHECK(obsoleted.empty());
}

BOOST_AUTO_TEST_CASE(DeleteLastKeyYieldsEmptyRoot)
{
    NodeStorage storage;
    KeyValueMap const base{{makeHash(0x11), bcos::bytes{0x01}}};
    auto const priorRoot = buildBase(storage, base);

    std::unordered_map<bcos::h256, bcos::bytes> newNodes;
    std::unordered_set<bcos::h256> obsoleted;
    auto const newRoot = incrementalCommit(
        storage, priorRoot, ChangeMap{{makeHash(0x11), std::nullopt}}, newNodes, obsoleted);

    BOOST_CHECK(newRoot == emptyRootHash());
    BOOST_CHECK(newNodes.empty());
    BOOST_CHECK(obsoleted == std::unordered_set<bcos::h256>{priorRoot});
}

BOOST_AUTO_TEST_CASE(LeafSplitDeepSharedPrefix)
{
    // Two keys sharing 62 nibbles force a deep extension+branch chain out of a single-leaf trie.
    NodeStorage storage;
    KeyValueMap const base{
        {hexKey("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa01"),
            bcos::bytes{0x01}}};
    auto const priorRoot = buildBase(storage, base);
    checkIncrementalStep(storage, priorRoot, base,
        ChangeMap{{hexKey("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa42"),
            bcos::bytes{0x02}}});
}

BOOST_AUTO_TEST_CASE(ExtensionSplitAtDivergence)
{
    // Base: two keys sharing 8 nibbles → ext(8 nibbles)+branch. The put diverges after 4 nibbles,
    // splitting the extension while the original subtree is re-referenced, not reloaded.
    NodeStorage storage;
    KeyValueMap const base{
        {hexKey("abcdef1100000000000000000000000000000000000000000000000000000001"),
            bcos::bytes{0x01}},
        {hexKey("abcdef2200000000000000000000000000000000000000000000000000000002"),
            bcos::bytes{0x02}}};
    auto const priorRoot = buildBase(storage, base);
    checkIncrementalStep(storage, priorRoot, base,
        ChangeMap{{hexKey("abcd990000000000000000000000000000000000000000000000000000000003"),
            bcos::bytes{0x03}}});
}

BOOST_AUTO_TEST_CASE(BranchCollapseToLeaf)
{
    NodeStorage storage;
    KeyValueMap const base{
        {makeHash(0x11), bcos::bytes{0x01}}, {makeHash(0x22), bcos::bytes{0x02}}};
    auto const priorRoot = buildBase(storage, base);
    checkIncrementalStep(storage, priorRoot, base, ChangeMap{{makeHash(0x22), std::nullopt}});
}

BOOST_AUTO_TEST_CASE(BranchCollapseKeepsUntouchedBranchUnrebuilt)
{
    // Root branch: nibble 0 → one lone leaf, nibble 1 → a 16-way branch (via 17 keys). Deleting
    // the lone leaf collapses the root into ext{1}+branch; the surviving branch is re-referenced
    // by hash — it must be neither obsoleted nor re-emitted.
    NodeStorage storage;
    KeyValueMap base{{hexKey("0000000000000000000000000000000000000000000000000000000000000000"),
        bcos::bytes{0xAA}}};
    for (uint8_t nib = 0; nib < NIBBLE_RANGE; ++nib)
    {
        bcos::h256 key{};
        key.data()[0] = static_cast<bcos::byte>(0x10U | nib);  // paths 1,0.. 1,1.. … 1,f..
        base[key] = bcos::bytes{static_cast<bcos::byte>(nib + 1)};
    }
    auto const priorRoot = buildBase(storage, base);

    // Identify the untouched subtree's node hash: the root branch's child at nibble 1.
    auto rootRaw = bcos::task::syncWait(bcos::storage2::readOne(storage, priorRoot));
    BOOST_REQUIRE(rootRaw.has_value());
    auto const rootNode = decodeNode(bcos::ref(*rootRaw));
    auto const& rootBranch = std::get<BranchNode>(rootNode);
    BOOST_REQUIRE(rootBranch.children[1].kind == NodeRef::Kind::Hash);
    auto const untouchedChild = rootBranch.children[1].hash;

    std::unordered_map<bcos::h256, bcos::bytes> newNodes;
    std::unordered_set<bcos::h256> obsoleted;
    auto const newRoot = incrementalCommit(storage, priorRoot,
        ChangeMap{{hexKey("0000000000000000000000000000000000000000000000000000000000000000"),
            std::nullopt}},
        newNodes, obsoleted);

    KeyValueMap expected = base;
    expected.erase(hexKey("0000000000000000000000000000000000000000000000000000000000000000"));
    BOOST_CHECK(newRoot == computeTrieRoot(expected).root);
    BOOST_CHECK(!obsoleted.contains(untouchedChild));
    BOOST_CHECK(!newNodes.contains(untouchedChild));
    // Only the new root extension should have been emitted for this shape.
    BOOST_CHECK_EQUAL(newNodes.size(), 1U);
}

BOOST_AUTO_TEST_CASE(SequentialCommitChain)
{
    // Three incremental commits stacked on each other, each verified against the full oracle.
    auto rng = seededRng(20260702);
    NodeStorage storage;
    KeyValueMap base;
    for (size_t i = 0; i < 40; ++i)
    {
        base[randomHash(rng)] = randomValue(rng);
    }
    auto root = buildBase(storage, base);

    for (size_t step = 0; step < 3; ++step)
    {
        ChangeMap changes;
        size_t index = 0;
        for (auto const& [key, value] : base)
        {
            if (index % 3 == step % 3)
            {
                changes[key] =
                    (index % 2 == 0) ? std::optional<bcos::bytes>{randomValue(rng)} : std::nullopt;
            }
            ++index;
        }
        for (size_t i = 0; i < 10; ++i)
        {
            changes[randomHash(rng)] = randomValue(rng);
        }
        checkIncrementalStep(storage, root, base, changes);
        base = applyChanges(base, changes);
        root = computeTrieRoot(base).root;
    }
}

BOOST_AUTO_TEST_CASE(RandomizedEquivalenceAgainstFromScratch)
{
    auto rng = seededRng(0x5EED1234);
    for (size_t round = 0; round < 120; ++round)
    {
        NodeStorage storage;
        KeyValueMap base;
        size_t const baseSize = rng() % 90;
        for (size_t i = 0; i < baseSize; ++i)
        {
            base[randomHash(rng)] = randomValue(rng);
        }
        auto const priorRoot = buildBase(storage, base);

        ChangeMap changes;
        for (auto const& [key, value] : base)
        {
            switch (rng() % 4)
            {
            case 0:  // update
                changes[key] = randomValue(rng);
                break;
            case 1:  // delete
                changes[key] = std::nullopt;
                break;
            default:  // leave untouched
                break;
            }
        }
        size_t const inserts = rng() % 30;
        for (size_t i = 0; i < inserts; ++i)
        {
            changes[randomHash(rng)] = randomValue(rng);
        }
        if (rng() % 4 == 0)
        {
            changes[randomHash(rng)] = std::nullopt;  // delete of an absent key
        }
        if (changes.empty())
        {
            continue;
        }
        checkIncrementalStep(storage, priorRoot, base, changes);
    }
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::ledger::mpt::test
