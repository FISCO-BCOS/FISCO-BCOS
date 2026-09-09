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
 * @file PathPositionTest.cpp
 * @brief Position addresses, hash verifies: what happens when the two disagree (pathdb spec §8.3)
 */
#include "TestHelpers.h"
#include <bcos-crypto/hasher/OpenSSLHasher.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-ledger/mpt/Account.h>
#include <bcos-ledger/mpt/Constants.h>
#include <bcos-ledger/mpt/Errors.h>
#include <bcos-ledger/mpt/HashBuilder.h>
#include <bcos-ledger/mpt/MPTAccount.h>
#include <bcos-ledger/mpt/MPTReadView.h>
#include <bcos-ledger/mpt/Nibble.h>
#include <bcos-ledger/mpt/PathKey.h>
#include <bcos-ledger/mpt/Proof.h>
#include <bcos-ledger/mpt/StorageValueCodec.h>
#include <bcos-ledger/mpt/Trie.h>
#include <bcos-ledger/mpt/history/HistoryCommit.h>
#include <bcos-ledger/mpt/history/HistoryRead.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <boost/test/unit_test.hpp>
#include <map>
#include <optional>
#include <span>
#include <variant>

namespace bcos::ledger::mpt::test
{

BOOST_AUTO_TEST_SUITE(PathPositionSuite)

namespace
{
using NodeStorage = bcos::ledger::mpt::test::NodeMemoryStorage;

/// An h256 whose first nibble is @p firstNibble and whose last byte is @p tail.
bcos::h256 keyAtNibble(bcos::byte firstNibble, bcos::byte tail)
{
    bcos::h256 out{};
    out.data()[0] = static_cast<bcos::byte>(firstNibble << NIBBLE_BITS);
    out.data()[bcos::h256::SIZE - 1] = tail;
    return out;
}
}  // namespace

// A branch's child hash is no longer an address — but it is still a commitment. Put the RIGHT
// node at the WRONG position (swap two siblings) and the walk must refuse it: the bytes it finds
// where the path says to look do not hash to what the parent recorded.
BOOST_AUTO_TEST_CASE(ChildAtTheWrongPositionFailsVerification)
{
    NodeStorage storage;
    auto const keyA = keyAtNibble(0x01, 0xAA);
    auto const keyB = keyAtNibble(0x02, 0xBB);
    bcos::bytes const valueA(40, 0x11);
    bcos::bytes const valueB(40, 0x22);
    auto const root =
        seedTrieFlushed(storage, emptyRootHash(), {{keyA, valueA}, {keyB, valueB}}).root;

    PathKey const positionA{.scope = TrieScope::account(), .position = bcos::bytes{0x01}};
    PathKey const positionB{.scope = TrieScope::account(), .position = bcos::bytes{0x02}};
    auto const nodeA = bcos::task::syncWait(bcos::storage2::readOne(storage, positionA));
    auto const nodeB = bcos::task::syncWait(bcos::storage2::readOne(storage, positionB));
    BOOST_REQUIRE(nodeA.has_value() && nodeB.has_value());
    BOOST_REQUIRE(*nodeA != *nodeB);

    // Both nodes are genuine trie nodes of THIS trie — only their positions are swapped.
    bcos::task::syncWait(bcos::storage2::writeOne(storage, positionA, *nodeB));
    bcos::task::syncWait(bcos::storage2::writeOne(storage, positionB, *nodeA));

    Trie<NodeStorage> trie(storage, TrieScope::account(), root);
    BOOST_CHECK_THROW(bcos::task::syncWait(trie.get(keyA)), MPTInvariantViolation);
    BOOST_CHECK_THROW(bcos::task::syncWait(trie.get(keyB)), MPTInvariantViolation);
}

// Same failure through the proof walk: a proof must never be assembled from bytes that do not
// hash to what the path they were fetched along committed to.
BOOST_AUTO_TEST_CASE(ProofWalkRejectsAMisplacedNode)
{
    NodeStorage storage;
    auto const addrA = makeAddress(0x11);
    auto const addrB = makeAddress(0x22);
    Account accountA;
    accountA.nonce = 1;
    accountA.balance = 1000;
    Account accountB;
    accountB.nonce = 2;
    accountB.balance = 2000;
    auto const root = seedStateTrieFlushed(storage, {{addrA, accountA}, {addrB, accountB}});

    // Find the two account leaves' positions by walking the account keys one nibble at a time
    // until a row exists there.
    auto positionForKey = [&](bcos::h256 const& keyHash) {
        auto const nibbles = bytesToNibbles(keyHash.ref());
        for (size_t length = nibbles.size(); length > 0; --length)
        {
            PathKey candidate{.scope = TrieScope::account(),
                .position = bcos::bytes(
                    nibbles.begin(), nibbles.begin() + static_cast<std::ptrdiff_t>(length))};
            if (bcos::task::syncWait(bcos::storage2::readOne(storage, candidate)))
            {
                return candidate;
            }
        }
        BOOST_FAIL("no row on the account's path");
        return PathKey{};
    };
    auto const leafA = positionForKey(accountKeyHash(addrA));
    auto const leafB = positionForKey(accountKeyHash(addrB));
    BOOST_REQUIRE(!(leafA == leafB));

    auto const nodeA = bcos::task::syncWait(bcos::storage2::readOne(storage, leafA));
    auto const nodeB = bcos::task::syncWait(bcos::storage2::readOne(storage, leafB));
    BOOST_REQUIRE(nodeA.has_value() && nodeB.has_value());
    bcos::task::syncWait(bcos::storage2::writeOne(storage, leafA, *nodeB));
    bcos::task::syncWait(bcos::storage2::writeOne(storage, leafB, *nodeA));

    BOOST_CHECK_THROW(
        bcos::task::syncWait(generateProof(storage, root, addrA, std::span<bcos::h256 const>{})),
        MPTInvariantViolation);
}

// A trie root is found at a FIXED key, so "wrong root" and "wrong version" are the same
// question, and the answer must not be silence. Reading at a root the store does not hold is
// MPTHistoryUnavailable, never a fabricated absence.
BOOST_AUTO_TEST_CASE(UnknownRootIsReportedNotAnswered)
{
    NodeStorage storage;
    auto const key = keyAtNibble(0x01, 0xAA);
    auto const root =
        seedTrieFlushed(storage, emptyRootHash(), {{key, bcos::bytes(40, 0x11)}}).root;

    Trie<NodeStorage> const stranger(storage, TrieScope::account(), makeHash(0xDD));
    BOOST_CHECK_THROW(bcos::task::syncWait(stranger.get(key)), MPTHistoryUnavailable);

    // An owner with no storage trie at all: nothing at position "" either.
    Trie<NodeStorage> const noSuchTrie(storage, TrieScope::storage(makeHash(0xEE)), makeHash(0xEE));
    BOOST_CHECK_THROW(bcos::task::syncWait(noSuchTrie.get(key)), MPTHistoryUnavailable);

    // The real root still reads, so the throws above are about the ROOT and not the store.
    Trie<NodeStorage> const real(storage, TrieScope::account(), root);
    BOOST_CHECK(bcos::task::syncWait(real.get(key)).has_value());
    BOOST_CHECK(bcos::task::syncWait(holdsTrieRoot(storage, TrieScope::account(), root)));
    BOOST_CHECK(
        !bcos::task::syncWait(holdsTrieRoot(storage, TrieScope::account(), makeHash(0xDD))));
}

// A position holds ONE version, so the node rows alone can only answer for the tip: block N's
// root is not a thing they can be read at. The trie-node reverse history is what puts that back
// — every reader keeps working, unchanged, over a plane that resolves each position to the
// version it held at block N (pathdb spec §10.2).
//
// Both halves are asserted here: the raw node store still refuses the old root, and the same
// readers over HistoricalNodeStorage answer it — with the OLD bytes, not today's.
BOOST_AUTO_TEST_CASE(SupersededRootReadsThroughTheTrieHistory)
{
    using HistoryBackend =
        bcos::storage2::memory_storage::MemoryStorage<bcos::executor_v1::StateKey,
            bcos::storage::Entry, bcos::storage2::memory_storage::ORDERED>;
    namespace history = bcos::ledger::mpt::history;

    NodeStorage storage;
    HistoryBackend historyBackend;
    auto const keyA = keyAtNibble(0x01, 0xAA);
    auto const keyB = keyAtNibble(0x02, 0xBB);
    auto const rootN =
        seedTrieFlushed(storage, emptyRootHash(), {{keyA, bcos::bytes(40, 0x11)}}).root;

    // Block N+1 rewrites the trie; its pre-images become block N+1's trie history, which is
    // exactly what the commit path records (HistoryCommit.h).
    auto const blockN1 = seedTrieFlushed(storage, rootN, {{keyB, bcos::bytes(40, 0x22)}});
    auto const rootN1 = blockN1.root;
    BOOST_REQUIRE(rootN != rootN1);
    BOOST_REQUIRE(!blockN1.preimages.empty());
    bcos::task::syncWait(history::commitBlockHistory(historyBackend, historyBackend,
        /*block*/ 1, {}, blockN1.preimages, {.state = 0, .proof = 128}));

    // The raw node store: the tip reads, the superseded root does not.
    Trie<NodeStorage> const tip(storage, TrieScope::account(), rootN1);
    BOOST_CHECK(bcos::task::syncWait(tip.get(keyA)).has_value());
    Trie<NodeStorage> const historical(storage, TrieScope::account(), rootN);
    BOOST_CHECK_THROW(bcos::task::syncWait(historical.get(keyA)), MPTHistoryUnavailable);
    MPTReadView<NodeStorage> const view(storage, rootN);
    BOOST_CHECK_THROW(
        bcos::task::syncWait(view.readAccount(makeAddress(0x11))), MPTHistoryUnavailable);
    BOOST_CHECK(!bcos::task::syncWait(holdsTrieRoot(storage, TrieScope::account(), rootN)));

    // Through the history plane at block 0, every one of them answers again — and the value is
    // block 0's, which is what makes this more than "it stopped throwing".
    using HistoricalNodes = history::HistoricalNodeStorage<NodeStorage, HistoryBackend>;
    HistoricalNodes atBlock0(storage, historyBackend, /*block*/ 0, /*tip*/ 1, /*depth*/ 128);
    BOOST_CHECK(bcos::task::syncWait(holdsTrieRoot(atBlock0, TrieScope::account(), rootN)));
    Trie<HistoricalNodes> const historicalTrie(atBlock0, TrieScope::account(), rootN);
    auto const valueAt0 = bcos::task::syncWait(historicalTrie.get(keyA));
    BOOST_REQUIRE(valueAt0.has_value());
    BOOST_CHECK(*valueAt0 == bcos::bytes(40, 0x11));
    // keyB did not exist at block 0 — its leaf position reads as absent, not as today's node.
    BOOST_CHECK(!bcos::task::syncWait(historicalTrie.get(keyB)).has_value());

    // Out of window: the guard fires before the seek and the read is refused, rather than
    // silently degrading into the current version (spec B.3, G5).
    HistoricalNodes outOfWindow(storage, historyBackend, /*block*/ 0, /*tip*/ 1, /*depth*/ 1);
    Trie<HistoricalNodes> const prunedTrie(outOfWindow, TrieScope::account(), rootN);
    BOOST_CHECK_THROW(bcos::task::syncWait(prunedTrie.get(keyA)), history::HistoryPruned);
}

// Two accounts with byte-identical storage tries get their own rows. Deleting one owner's rows
// cannot reach the other's — the property that makes A.6's fourth delete source safe, and the
// one a content-addressed store could not offer.
BOOST_AUTO_TEST_CASE(IdenticalTriesDoNotShareRowsAcrossOwners)
{
    NodeStorage storage;
    auto const ownerA = makeHash(0xA1);
    auto const ownerB = makeHash(0xB2);
    std::map<bcos::h256, bcos::bytes> const entries{
        {keyAtNibble(0x01, 0xAA), bcos::bytes(40, 0x11)},
        {keyAtNibble(0x02, 0xBB), bcos::bytes(40, 0x22)}};

    auto const rootA =
        seedTrieFlushed(storage, emptyRootHash(), entries, TrieScope::storage(ownerA)).root;
    auto const rootB =
        seedTrieFlushed(storage, emptyRootHash(), entries, TrieScope::storage(ownerB)).root;
    BOOST_CHECK(rootA == rootB);  // identical content, identical root

    auto const rowsA = scanTrieNodes(storage, TrieScope::storage(ownerA));
    auto const rowsB = scanTrieNodes(storage, TrieScope::storage(ownerB));
    BOOST_CHECK(rowsA == rowsB);  // ...and identical bytes at identical positions
    BOOST_CHECK(!rowsA.empty());

    // Yet the rows are distinct: removing every one of A's leaves B's intact.
    for (auto const& [position, raw] : rowsA)
    {
        bcos::task::syncWait(bcos::storage2::removeOne(
            storage, PathKey{.scope = TrieScope::storage(ownerA), .position = position}));
    }
    BOOST_CHECK(scanTrieNodes(storage, TrieScope::storage(ownerA)).empty());
    BOOST_CHECK(scanTrieNodes(storage, TrieScope::storage(ownerB)) == rowsB);
    Trie<NodeStorage> const trieB(storage, TrieScope::storage(ownerB), rootB);
    BOOST_CHECK(bcos::task::syncWait(trieB.get(keyAtNibble(0x01, 0xAA))).has_value());
}

// The read path verifies with ITS OWN HasherT, not with a hard-coded keccak. That matters
// because the hash a walk uses for verification and the hash a caller uses for the key transform
// must be the same algorithm: mixing them means locating nodes along one algorithm's paths and
// checking them against another's digests. A keccak trie read through a non-keccak instantiation
// must therefore REFUSE at the very first node, and a proof over it must not be produced at all.
//
// The discriminator is that refusal. If node verification were pinned to keccak while HasherT
// only drove the key transform, the walk below would succeed against the keccak trie and
// generateProof would hand back a proof whose slot path was computed with a different hash —
// the silently-wrong-proof shape, which no root comparison catches.
BOOST_AUTO_TEST_CASE(NodeVerificationFollowsTheInstantiatedHasher)
{
    using SM3 = bcos::crypto::hasher::openssl::OpenSSL_SM3_Hasher;

    NodeStorage storage;
    auto const addr = makeAddress(0x11);
    Account account;
    account.nonce = 3;
    account.balance = 99;
    // Built with the default (keccak) hasher, like every trie this chain produces today.
    auto const root = seedStateTrieFlushed(storage, {{addr, account}});

    // Keccak reads it.
    Trie<NodeStorage> const keccakTrie(storage, TrieScope::account(), root);
    BOOST_CHECK(bcos::task::syncWait(keccakTrie.get(accountKeyHash(addr))).has_value());
    BOOST_CHECK(bcos::task::syncWait(holdsTrieRoot(storage, TrieScope::account(), root)));

    // SM3 does not: the bytes at position "" do not hash to `root` under SM3.
    Trie<NodeStorage, SM3> const sm3Trie(storage, TrieScope::account(), root);
    BOOST_CHECK_THROW(
        bcos::task::syncWait(sm3Trie.get(accountKeyHash(addr))), MPTHistoryUnavailable);
    BOOST_CHECK(!bcos::task::syncWait(
        holdsTrieRoot<NodeStorage, SM3>(storage, TrieScope::account(), root)));
    MPTReadView<NodeStorage, SM3> const sm3View(storage, root);
    BOOST_CHECK_THROW(bcos::task::syncWait(sm3View.readAccount(addr)), MPTHistoryUnavailable);

    // MPTAccount drives its OWN trie walks — the account leaf, then that account's storage trie
    // — so it has to forward the hasher to BOTH. The storage walk is where a half-threaded
    // version hides, and reaching it takes some care: the leaf walk runs first, so under a
    // mismatched hasher it refuses before the storage walk is ever entered, and a naive test
    // passes for the wrong reason.
    //
    // So this isolates the storage walk. The store below is deliberately IMPOSSIBLE on a real
    // chain: an account trie SM3 can walk, holding a leaf whose storageRoot is a keccak-built
    // storage trie. The account trie is a single leaf, so its only row is at position "" and the
    // walk verifies nothing but that row — hand it the SM3 digest of those same bytes as the
    // state root and the leaf resolves under SM3. Everything after that is the storage walk,
    // alone.
    //
    // A storage walk that defaults to keccak then passes root verification (the storage trie IS
    // keccak-built) and dead-ends on an SM3 slot path — reporting the slot ABSENT, which a caller
    // reads as a legitimate zero. That is the one wrong answer indistinguishable from a right
    // one, and it is why the assertion below is "throws", not "returns something else".
    {
        NodeStorage mixed;
        FlatBackendStorage flat;
        auto const contract = makeAddress(0x33);
        bcos::h256 const slot{};
        bcos::h256 slotValue{};
        slotValue.data()[bcos::h256::SIZE - 1] = 0x2a;

        auto const storageRoot = seedTrieFlushed(mixed, emptyRootHash(),
            {{slotKeyHash(slot), encodeStorageValue(slotValue.ref())}},
            TrieScope::storage(accountKeyHash(contract)))
                                     .root;
        Account contractAccount;
        contractAccount.nonce = 1;
        contractAccount.storageRoot = storageRoot;
        auto const keccakRoot = seedTrieFlushed(
            mixed, emptyRootHash(), {{accountKeyHash(contract), contractAccount.encode()}})
                                    .root;

        // Same bytes at position "", a different digest: the state root an SM3 walk accepts.
        auto const rootRow =
            bcos::task::syncWait(bcos::storage2::readOne(mixed, accountRootPathKey()));
        BOOST_REQUIRE(rootRow.has_value());
        SM3 sm3;
        bcos::h256 sm3Root;
        bcos::crypto::hasher::hash(sm3, bcos::ref(*rootRow), sm3Root);
        BOOST_REQUIRE(sm3Root != keccakRoot);

        // Positive anchor: the keccak instantiation reads the slot back through both walks.
        MPTAccount<FlatBackendStorage, NodeStorage, FlatBackendStorage> keccakAccount{
            flat, mixed, flat, contract, /*binaryAddress*/ false};
        auto const keccakRead = bcos::task::syncWait(
            keccakAccount.storage(bcos::ledger::account::toEvmcBytes32(slot), keccakRoot));
        BOOST_CHECK_EQUAL(bcos::ledger::account::toH256(keccakRead), slotValue);

        // SM3 gets past the leaf (by construction) and must then REFUSE on the storage trie.
        MPTAccount<FlatBackendStorage, NodeStorage, FlatBackendStorage, SM3> sm3Account{
            flat, mixed, flat, contract, /*binaryAddress*/ false};
        BOOST_REQUIRE_MESSAGE(bcos::task::syncWait(sm3Account.exists(sm3Root)),
            "the SM3 leaf walk must succeed, or this case would not reach the storage walk");
        BOOST_CHECK_THROW(bcos::task::syncWait(sm3Account.storage(
                              bcos::ledger::account::toEvmcBytes32(slot), sm3Root)),
            MPTHistoryUnavailable);
    }

    // ...and no proof is produced: the walk reports the root as unavailable rather than
    // assembling one from nodes it never verified.
    auto const sm3Proof = bcos::task::syncWait(
        generateProof<NodeStorage, SM3>(storage, root, addr, std::span<bcos::h256 const>{}));
    BOOST_REQUIRE(std::holds_alternative<ProofErrorCode>(sm3Proof));
    BOOST_CHECK(std::get<ProofErrorCode>(sm3Proof) == ProofErrorCode::BlockNotCommitted);
    // Positive anchor: the keccak instantiation over the same trie DOES produce one.
    auto const keccakProof =
        bcos::task::syncWait(generateProof(storage, root, addr, std::span<bcos::h256 const>{}));
    BOOST_CHECK(std::holds_alternative<EIP1186Proof>(keccakProof));
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::ledger::mpt::test
