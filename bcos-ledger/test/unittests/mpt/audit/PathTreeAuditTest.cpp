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
 * @file PathTreeAuditTest.cpp
 * @brief auditPathTree over a real built tree, plus one injected corruption per invariant it
 *        claims to catch (pathdb spec §13, §14)
 */

#include "AuditTestHelpers.h"
#include <bcos-ledger/mpt/Account.h>
#include <bcos-ledger/mpt/Errors.h>
#include <bcos-ledger/mpt/MPTReadView.h>
#include <bcos-ledger/mpt/NodeEncoder.h>
#include <bcos-ledger/mpt/PathKey.h>
#include <bcos-ledger/mpt/audit/PathTreeAudit.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/test/unit_test.hpp>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace bcos::ledger::mpt::audit::test
{

BOOST_AUTO_TEST_SUITE(PathTreeAuditSuite)

/// One account trie over four accounts, two of which carry a storage trie. Everything the audit
/// reads is produced by the ordinary builder and then landed on the flat plane.
struct AuditTreeFixture
{
    mpt::test::NodeMemoryStorage nodes;
    AuditFlatStorage flat;
    std::vector<std::pair<bcos::Address, Account>> accounts;
    bcos::h256 stateRoot;
    bcos::Address storageOwnerAddress = mpt::test::makeAddress(0x02);

    AuditTreeFixture()
    {
        std::map<bcos::h256, bcos::bytes> slots;
        slots[mpt::test::makeHash(0x11)] = bcos::bytes(32, bcos::byte{0x07});
        slots[mpt::test::makeHash(0x22)] = bcos::bytes(32, bcos::byte{0x09});

        accounts.emplace_back(mpt::test::makeAddress(0x01),
            makeAccountWithStorage(nodes, mpt::test::makeAddress(0x01), {}));
        accounts.emplace_back(
            storageOwnerAddress, makeAccountWithStorage(nodes, storageOwnerAddress, slots));
        accounts.emplace_back(mpt::test::makeAddress(0x03),
            makeAccountWithStorage(nodes, mpt::test::makeAddress(0x03), {}));
        accounts.emplace_back(mpt::test::makeAddress(0x04),
            makeAccountWithStorage(nodes, mpt::test::makeAddress(0x04),
                {{mpt::test::makeHash(0x33), bcos::bytes(32, bcos::byte{0x05})}}));

        stateRoot = mpt::test::seedStateTrieFlushed(nodes, accounts);
        landNodeRowsOnFlat(nodes, flat);
    }

    /// The account-trie positions that hold a row, in pre-order.
    [[nodiscard]] std::map<bcos::bytes, bcos::bytes> accountRows()
    {
        return mpt::test::scanTrieNodes(nodes, TrieScope::account());
    }

    /// The shortest non-empty account position — a direct child of the root, whichever node type
    /// the root turned out to be.
    [[nodiscard]] bcos::bytes firstChildPosition()
    {
        for (auto const& [position, raw] : accountRows())
        {
            if (!position.empty())
            {
                return position;
            }
        }
        return {};
    }

    [[nodiscard]] bcos::h256 storageOwner() const { return accountKeyHash(storageOwnerAddress); }
};

/// A predicate for BOOST_CHECK_EXCEPTION: the thrown message mentions @p fragment.
bool auditMessageMentions(boost::exception const& error, std::string const& fragment)
{
    std::string const message = boost::diagnostic_information(error);
    BOOST_TEST_MESSAGE("audit threw: " << message);
    return message.find(fragment) != std::string::npos;
}

BOOST_AUTO_TEST_CASE(cleanTreePasses)
{
    AuditTreeFixture fixture;
    auto const report = runPathTreeAudit(fixture.flat);

    // The audit reproduces the state root from the bytes at position "" alone — no header, no
    // hash-keyed lookup.
    BOOST_CHECK_EQUAL(report.accountRoot.hex(), fixture.stateRoot.hex());
    BOOST_CHECK_EQUAL(report.accounts, 4);
    BOOST_CHECK_EQUAL(report.storageTries, 2);
    BOOST_CHECK_EQUAL(report.orphans, 0);
    BOOST_CHECK(report.warnings.empty());
    BOOST_CHECK_EQUAL(report.accountNodes, fixture.accountRows().size());
    BOOST_CHECK_GT(report.storageNodes, 0);
    // Every row except the three trie roots (one account trie, two storage tries) was reached
    // across an edge; inline children add further edges on top.
    BOOST_CHECK(report.verifiedEdges >= report.accountNodes + report.storageNodes - 3);
}

BOOST_AUTO_TEST_CASE(cleanTreeWithNoStorageTriesPasses)
{
    mpt::test::NodeMemoryStorage nodes;
    AuditFlatStorage flat;
    std::vector<std::pair<bcos::Address, Account>> accounts;
    for (uint8_t index = 1; index <= 6; ++index)
    {
        accounts.emplace_back(mpt::test::makeAddress(index), Account{});
    }
    auto const root = mpt::test::seedStateTrieFlushed(nodes, accounts);
    landNodeRowsOnFlat(nodes, flat);

    auto const report = runPathTreeAudit(flat);
    BOOST_CHECK_EQUAL(report.accountRoot.hex(), root.hex());
    BOOST_CHECK_EQUAL(report.accounts, 6);
    BOOST_CHECK_EQUAL(report.storageTries, 0);
    BOOST_CHECK_EQUAL(report.storageNodes, 0);
    BOOST_CHECK_EQUAL(report.orphans, 0);
}

BOOST_AUTO_TEST_CASE(emptyStoreIsVacuouslyClean)
{
    AuditFlatStorage flat;
    auto const report = runPathTreeAudit(flat);
    BOOST_CHECK_EQUAL(report.accountNodes, 0);
    BOOST_CHECK_EQUAL(report.storageNodes, 0);
    BOOST_CHECK_EQUAL(report.orphans, 0);
    // No rows IS the empty root, not an unknown root.
    BOOST_CHECK_EQUAL(report.accountRoot.hex(), emptyRootHash().hex());
    BOOST_CHECK(!report.rootChecked);
    BOOST_CHECK_NO_THROW(runPathTreeAudit(flat, emptyRootHash()));
}

/// The audit on its own only proves the rows agree with EACH OTHER. Handed the root the chain
/// commits to, it proves they are the right rows.
BOOST_AUTO_TEST_CASE(treeMatchingTheCommittedRootPasses)
{
    AuditTreeFixture fixture;
    auto const report = runPathTreeAudit(fixture.flat, fixture.stateRoot);
    BOOST_CHECK(report.rootChecked);
    BOOST_CHECK_EQUAL(report.accountRoot.hex(), fixture.stateRoot.hex());
}

/// NEGATIVE CONTROL — an internally perfect tree that is simply not the one the chain committed to.
/// Nothing inside the store can tell; only the header can.
BOOST_AUTO_TEST_CASE(treeDisagreeingWithTheCommittedRootThrows)
{
    AuditTreeFixture fixture;
    BOOST_CHECK_NO_THROW(runPathTreeAudit(fixture.flat));  // internally consistent

    auto const committed = mpt::test::makeHash(0xC0);
    BOOST_CHECK_EXCEPTION(runPathTreeAudit(fixture.flat, committed), MPTInvariantViolation,
        [&](MPTInvariantViolation const& error) {
            return auditMessageMentions(error, committed.hex()) &&
                   auditMessageMentions(error, fixture.stateRoot.hex()) &&
                   auditMessageMentions(error, "but the chain commits to");
        });
}

/// NEGATIVE CONTROL — a wiped account table. Every walk is skipped, so nothing is ever checked and
/// every storage trie becomes an orphan: without the committed root this store audits clean.
BOOST_AUTO_TEST_CASE(emptyAccountTableUnderANonEmptyCommittedRootThrows)
{
    AuditTreeFixture fixture;
    for (auto const& [position, raw] : fixture.accountRows())
    {
        removeNodeRow(fixture.flat, PathKey{.scope = TrieScope::account(), .position = position});
    }
    auto const wiped = runPathTreeAudit(fixture.flat);
    BOOST_CHECK_EQUAL(wiped.accountNodes, 0);
    BOOST_CHECK_GT(wiped.orphans, 0);

    BOOST_CHECK_EXCEPTION(runPathTreeAudit(fixture.flat, fixture.stateRoot), MPTInvariantViolation,
        [&](MPTInvariantViolation const& error) {
            return auditMessageMentions(error, "the account node table is empty");
        });
}

/// NEGATIVE CONTROL — G4's fatal half: one delete too many punches a hole, and the audit must say
/// WHERE.
BOOST_AUTO_TEST_CASE(holeInTheAccountTrieThrowsWithThePosition)
{
    AuditTreeFixture fixture;
    auto const victim = fixture.firstChildPosition();
    BOOST_REQUIRE(!victim.empty());
    removeNodeRow(fixture.flat, PathKey{.scope = TrieScope::account(), .position = victim});

    auto const expected = "position 0x" + bcos::toHex(victim);
    BOOST_CHECK_EXCEPTION(runPathTreeAudit(fixture.flat), MPTInvariantViolation,
        [&](MPTInvariantViolation const& error) {
            return auditMessageMentions(error, expected) && auditMessageMentions(error, "hole");
        });
}

/// NEGATIVE CONTROL — the child hash a parent records is the ONLY thing still tying a position's
/// bytes to the commitment (G2). Flip one byte of it and the audit must reject the child.
BOOST_AUTO_TEST_CASE(flippedChildHashInAParentThrows)
{
    AuditTreeFixture fixture;
    auto const childPosition = fixture.firstChildPosition();
    BOOST_REQUIRE(!childPosition.empty());

    auto const childRaw = readNodeRow(
        fixture.flat, PathKey{.scope = TrieScope::account(), .position = childPosition});
    BOOST_REQUIRE(childRaw.has_value());
    auto const rootRaw = readNodeRow(fixture.flat, accountRootPathKey());
    BOOST_REQUIRE(rootRaw.has_value());

    // Corrupt exactly the 32 bytes the root records for that child, leaving the RLP structure and
    // every other field of the root untouched.
    auto const offset = findDigest(*rootRaw, digestOf(*childRaw));
    BOOST_REQUIRE_NE(offset, kDigestNotFound);
    writeNodeRow(fixture.flat, accountRootPathKey(), flipByte(*rootRaw, offset));

    auto const expected = "position 0x" + bcos::toHex(childPosition);
    BOOST_CHECK_EXCEPTION(runPathTreeAudit(fixture.flat), MPTInvariantViolation,
        [&](MPTInvariantViolation const& error) {
            return auditMessageMentions(error, expected) &&
                   auditMessageMentions(error, "hashes to");
        });
}

/// NEGATIVE CONTROL — a child ref that is neither a 33-byte hash string nor a sub-32-byte inline
/// node violates the Yellow Paper §D rule. Accepting it as "inline" would let a forged subtree ride
/// inside a parent's bytes, and at the ROOT of the account trie there is no parent digest to catch
/// it afterwards — which is why the audit parses refs with the merge engine's refFromRawBytes
/// rather than its own "is it a hash? else inline" test.
///
/// The forged ref is a perfectly well-formed leaf carrying a valid Account, so nothing downstream
/// would have complained: without the 32-byte guard this store audits clean.
BOOST_AUTO_TEST_CASE(oversizedInlineChildRefThrows)
{
    AuditFlatStorage flat;

    LeafNode smuggled;
    smuggled.keyNibbles = bcos::bytes(63, bcos::byte{0x0b});
    smuggled.value = Account{}.encode();
    auto const forgedRef = encodeRaw(TrieNode{smuggled});
    BOOST_REQUIRE_GE(forgedRef.size(), bcos::h256::SIZE);
    BOOST_REQUIRE_NE(forgedRef.size(), HASH_REF_ENCODED_SIZE);

    ExtensionNode root;
    root.sharedNibbles = bcos::bytes{bcos::byte{0x0b}};
    root.child = forgedRef;
    writeNodeRow(flat, accountRootPathKey(), encodeRaw(TrieNode{root}));

    BOOST_CHECK_EXCEPTION(
        runPathTreeAudit(flat), MPTInvariantViolation, [&](MPTInvariantViolation const& error) {
            return auditMessageMentions(error, "inline child ref of >= 32 bytes");
        });
}

/// NEGATIVE CONTROL — the cross-trie edge: an account leaf's storageRoot must be the digest of the
/// bytes at position "" of that owner's storage trie (spec A.0 step 5).
BOOST_AUTO_TEST_CASE(storageRootDisagreeingWithTheOwnersTrieThrows)
{
    mpt::test::NodeMemoryStorage nodes;
    AuditFlatStorage flat;

    // ONE account, so its leaf IS the account trie root: rewriting it does not break any
    // parent/child hash, which isolates the storageRoot check from the edge check above.
    auto const address = mpt::test::makeAddress(0x0a);
    auto const account = makeAccountWithStorage(
        nodes, address, {{mpt::test::makeHash(0x55), bcos::bytes(32, bcos::byte{0x03})}});
    mpt::test::seedStateTrieFlushed(nodes, {{address, account}});
    landNodeRowsOnFlat(nodes, flat);
    BOOST_CHECK_NO_THROW(runPathTreeAudit(flat));

    Account tampered = account;
    tampered.storageRoot = mpt::test::makeHash(0xEE);
    auto const rootRaw = readNodeRow(flat, accountRootPathKey());
    BOOST_REQUIRE(rootRaw.has_value());
    // Rebuild the same leaf around the tampered account: same position, same key nibbles, one
    // different field.
    LeafNode leaf;
    leaf.keyNibbles = bytesToNibbles(accountKeyHash(address).ref());
    leaf.value = tampered.encode();
    writeNodeRow(flat, accountRootPathKey(), encodeRaw(TrieNode{leaf}));

    BOOST_CHECK_EXCEPTION(
        runPathTreeAudit(flat), MPTInvariantViolation, [&](MPTInvariantViolation const& error) {
            return auditMessageMentions(error, "storage trie of owner") &&
                   auditMessageMentions(error, "account leaf records");
        });
}

/// NEGATIVE CONTROL — an account that promises a storage trie but has none.
BOOST_AUTO_TEST_CASE(missingStorageTrieThrows)
{
    AuditTreeFixture fixture;
    auto const owner = fixture.storageOwner();
    for (auto const& [position, raw] :
        mpt::test::scanTrieNodes(fixture.nodes, TrieScope::storage(owner)))
    {
        removeNodeRow(
            fixture.flat, PathKey{.scope = TrieScope::storage(owner), .position = position});
    }

    BOOST_CHECK_EXCEPTION(runPathTreeAudit(fixture.flat), MPTInvariantViolation,
        [&](MPTInvariantViolation const& error) {
            return auditMessageMentions(error, owner.hex()) &&
                   auditMessageMentions(error, "no storage node row exists");
        });
}

/// Storage rows under an owner whose account commits to the empty root. Unreachable — a reader
/// short-circuits on the empty root — so G4 puts them on the benign side with every other orphan,
/// and the scan must carry on rather than abort. They keep their own count because, unlike a
/// nameless orphan trie, they have one possible cause: a storage-trie drop that stopped early.
BOOST_AUTO_TEST_CASE(rowsUnderAnEmptyStorageRootAreSuspectOrphans)
{
    AuditTreeFixture fixture;
    // Account 0x01 has no storage trie; give it one row anyway.
    auto const owner = accountKeyHash(mpt::test::makeAddress(0x01));
    LeafNode leaf;
    leaf.keyNibbles = bytesToNibbles(mpt::test::makeHash(0x77).ref());
    leaf.value = bcos::bytes(32, bcos::byte{0x01});
    writeNodeRow(fixture.flat, storageRootPathKey(owner), encodeRaw(TrieNode{leaf}));

    auto const report = runPathTreeAudit(fixture.flat);
    BOOST_CHECK_EQUAL(report.orphans, 1);
    BOOST_CHECK_EQUAL(report.suspectOrphans, 1);
    BOOST_REQUIRE_EQUAL(report.warnings.size(), 1);
    BOOST_CHECK(report.warnings.front().find(owner.hex()) != std::string::npos);
    BOOST_CHECK(report.warnings.front().find("incomplete storage-trie drop") != std::string::npos);
    // The scan did not abort: the two legitimate storage tries were still checked.
    BOOST_CHECK_EQUAL(report.storageTries, 2);
    BOOST_CHECK_EQUAL(report.accountRoot.hex(), fixture.stateRoot.hex());
}

/// The benign half of G4: one delete too FEW leaves a row nothing references. Warned about,
/// counted, not thrown — spec §14's asymmetry.
BOOST_AUTO_TEST_CASE(orphanRowIsAWarningNotAnError)
{
    AuditTreeFixture fixture;
    // A position no live node points at: 63 nibbles of 0x0f followed by one more, i.e. a leaf
    // depth that the four seeded accounts do not reach.
    bcos::bytes const orphanPosition(8, bcos::byte{0x0f});
    BOOST_REQUIRE(!fixture.accountRows().contains(orphanPosition));
    LeafNode leaf;
    leaf.keyNibbles = bcos::bytes(56, bcos::byte{0x0f});
    leaf.value = bcos::bytes(32, bcos::byte{0x02});
    writeNodeRow(fixture.flat, PathKey{.scope = TrieScope::account(), .position = orphanPosition},
        encodeRaw(TrieNode{leaf}));

    auto const report = runPathTreeAudit(fixture.flat);
    BOOST_CHECK_EQUAL(report.orphans, 1);
    BOOST_REQUIRE_EQUAL(report.warnings.size(), 1);
    BOOST_CHECK(report.warnings.front().find("position 0x" + bcos::toHex(orphanPosition)) !=
                std::string::npos);
    // The state root is unaffected: an orphan is not part of the commitment.
    BOOST_CHECK_EQUAL(report.accountRoot.hex(), fixture.stateRoot.hex());
}

/// A whole storage trie under an owner no account names is the same benign class.
BOOST_AUTO_TEST_CASE(orphanStorageTrieIsAWarningNotAnError)
{
    AuditTreeFixture fixture;
    auto const strangerOwner = mpt::test::makeHash(0xBE);
    LeafNode leaf;
    leaf.keyNibbles = bytesToNibbles(mpt::test::makeHash(0x99).ref());
    leaf.value = bcos::bytes(32, bcos::byte{0x04});
    writeNodeRow(fixture.flat, storageRootPathKey(strangerOwner), encodeRaw(TrieNode{leaf}));

    auto const report = runPathTreeAudit(fixture.flat);
    BOOST_CHECK_EQUAL(report.orphans, 1);
    // No known cause: nothing names this owner at all, so it is a plain orphan.
    BOOST_CHECK_EQUAL(report.suspectOrphans, 0);
    BOOST_REQUIRE_EQUAL(report.warnings.size(), 1);
    BOOST_CHECK(report.warnings.front().find(strangerOwner.hex()) != std::string::npos);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::ledger::mpt::audit::test
