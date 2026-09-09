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
 * @file PathTreeAudit.h
 * @brief Offline audit of the current path-addressed node rows: path continuity, parent/child
 *        hash agreement, and account leaf storageRoot vs the owner's storage trie
 *        (pathdb spec §13, appendix A.0)
 */
#pragma once

#include "../Account.h"
#include "../Constants.h"
#include "../Errors.h"
#include "../Nibble.h"
#include "../NodeDecoder.h"
#include "../PathKey.h"
#include "../Trie.h"
// detail::refFromRawBytes — the child-ref parser the merge engine uses, Yellow Paper 32-byte
// rule included — and detail::childPosition, the spec A.1 position arithmetic. Both are taken
// from the merge engine rather than re-derived here: the engine walks down them while rebuilding
// and this audit walks down them while checking, and those two must agree by construction.
#include "../TrieMerge.h"
#include "../TrieNode.h"
// history::SeekableStateStorage ("can seek to a key and walk forward") and
// history::detail::asStateValue ("the row's bytes, or nullptr for a deletion sentinel") say
// exactly what this scan needs of a storage. They are declared next to the reverse-history store
// because that is where they were first needed, not because they are history-specific — so this
// header reuses them rather than declaring a second copy.
#include "../history/ReverseHistoryStore.h"
#include <bcos-crypto/hasher/AnyHasher.h>
#include <bcos-crypto/hasher/OpenSSLHasher.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/FixedBytes.h>
#include <boost/throw_exception.hpp>
#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace bcos::ledger::mpt::audit
{

/// What one auditPathTree() pass found. Counts only — every ERROR leaves as an exception naming
/// the offending position, so a returned report always describes a tree that passed.
struct PathTreeAuditReport
{
    /// Node rows in "/mptp/a".
    std::size_t accountNodes{};
    /// Node rows in "/mptp/s", summed over every owner.
    std::size_t storageNodes{};
    /// Owners that have at least one storage node row.
    std::size_t storageTries{};
    /// Account leaves reached by the account-trie walk.
    std::size_t accounts{};
    /// Parent -> child edges checked, both the hash-referenced and the inline ones.
    std::size_t verifiedEdges{};
    /// Rows no walk reached. UNREACHABLE, not missing: spec §14's asymmetry is that deleting one
    /// position too few leaves waste while deleting one too many stops the chain, so an orphan is
    /// a warning and a hole is an exception.
    std::size_t orphans{};
    /// The subset of `orphans` that has a known cause rather than merely being unreferenced: rows
    /// under an owner whose account leaf commits to the EMPTY storage root. They are as harmless
    /// to a reader as any other orphan, but they can only come from a storage-trie drop that did
    /// not delete everything it enumerated, so a caller may want to treat them differently.
    std::size_t suspectOrphans{};
    /// HasherT of the bytes at position "" of the account trie — the state root this node store
    /// currently holds. emptyRootHash() when the account table has no rows at all, which is what
    /// a chain with no accounts commits to.
    bcos::h256 accountRoot{};
    /// Whether accountRoot was compared against a root the CALLER supplied. False means the audit
    /// proved the tree internally consistent and nothing more: a store that is coherent but holds
    /// the wrong tree — or holds no account rows at all — passes an unchecked audit.
    bool rootChecked{};
    /// One line per warning, in discovery order. Capped: the count above is exact, this list is
    /// for a human reading a terminal.
    std::vector<std::string> warnings;
};

/// The most warning lines a report carries. An audit of a store that lost a whole subtree would
/// otherwise print millions of them.
inline constexpr std::size_t kMaxAuditWarnings = 64;

namespace detail
{

/// One trie's live rows, position -> raw RLP.
///
/// std::map, and its ordering is load-bearing in one direction only: iterating it is pre-order,
/// because a parent's position is a prefix of every position in its subtree. The PHYSICAL row
/// order is NOT pre-order — compactPath puts the parity flag in the header byte, so every
/// even-length position (header 0x00) sorts before every odd-length one (header 0x1n), and a
/// node can therefore land after its own children on disk. That is why the scan buffers a trie
/// before checking it instead of validating rows as they stream past (see auditPathTree).
using NodeRows = std::map<bcos::bytes, bcos::bytes>;

/// A position, spelled the way an operator can grep for it.
inline std::string describe(TrieScope const& scope, bcos::bytes const& position)
{
    std::string out{scope.kind == TrieKind::Account ? kMPTAccountTable : kMPTStorageTable};
    if (scope.kind == TrieKind::Storage)
    {
        out += " owner " + scope.owner.hex();
    }
    out += " position 0x" + bcos::toHex(position);
    return out;
}

/// What walking one trie produced.
struct TrieWalkResult
{
    /// HasherT of the bytes at position "".
    bcos::h256 root;
    /// Positions the walk reached that HAVE a row. Inline children are not in here: they have no
    /// row of their own, so a row at such a position would be an orphan.
    std::set<bcos::bytes> visited;
    std::size_t edges{};
};

/// Walk one trie in pre-order from position "", verifying every edge.
///
/// The walk is in-memory: @p rows is the trie, already scanned. Three things are checked per
/// edge, and each failure throws with the position it happened at:
///   - a hash-referenced child HAS a row at the position its parent's node type implies (A.1);
///   - that row's HasherT digest equals the hash the parent recorded — under path addressing the
///     hash no longer addresses the child, so proving the bytes at the position are the committed
///     ones is its whole remaining job (G2);
///   - no two parents claim one position, which a tree cannot produce and a corrupt store can.
///
/// An INLINE child (a subtree whose complete RLP is under 32 bytes and therefore lives inside its
/// parent's encoding) has no row. The walk descends into its bytes so leaves inside it are still
/// enumerated, but records no visit — so a row that nevertheless exists at that position is
/// reported as an orphan rather than being silently accepted.
///
/// @param accountStorageRoots when non-null, this is the ACCOUNT trie: each leaf is decoded as an
///        Account and (owner, storageRoot) recorded, where owner is the walked position
///        concatenated with the leaf suffix (spec §8.3 — the storage trie's name falls out of the
///        account walk, no lookup).
/// @throws MPTInvariantViolation on a hole, a digest disagreement, a duplicated position, or an
///         account leaf whose full key is not 64 nibbles.
template <bcos::crypto::hasher::Hasher HasherT>
TrieWalkResult walkTrie(TrieScope const& scope, NodeRows const& rows,
    std::map<bcos::h256, bcos::h256>* accountStorageRoots)
{
    HasherT hasher;
    TrieWalkResult result;

    auto const rootIterator = rows.find(bcos::bytes{});
    if (rootIterator == rows.end())
    {
        BOOST_THROW_EXCEPTION(
            MPTInvariantViolation{} << bcos::errinfo_comment(
                "path-tree audit: " + describe(scope, {}) +
                " holds node rows but nothing at position \"\"; the trie has no root"));
    }
    result.root = mpt::detail::nodeDigest(hasher, bcos::ref(rootIterator->second));

    /// A node still to be checked. The bytes are OWNED rather than referenced: an inline child's
    /// bytes live inside its parent's decoded NodeRef, which dies as soon as the parent frame is
    /// popped.
    struct Frame
    {
        bcos::bytes position;
        bcos::bytes raw;
    };

    std::vector<Frame> stack;
    stack.push_back(Frame{.position = {}, .raw = rootIterator->second});
    result.visited.insert(bcos::bytes{});

    auto pushHashChild = [&](bcos::bytes const& parentPosition, bcos::bytes childPosition,
                             bcos::h256 const& expected) {
        auto const child = rows.find(childPosition);
        if (child == rows.end())
        {
            BOOST_THROW_EXCEPTION(
                MPTInvariantViolation{} << bcos::errinfo_comment(
                    "path-tree audit: the node at " + describe(scope, parentPosition) +
                    " references a child at " + describe(scope, childPosition) +
                    " but no row exists there; the trie has a hole"));
        }
        if (auto const digest = mpt::detail::nodeDigest(hasher, bcos::ref(child->second));
            digest != expected)
        {
            BOOST_THROW_EXCEPTION(
                MPTInvariantViolation{} << bcos::errinfo_comment(
                    "path-tree audit: the node at " + describe(scope, childPosition) +
                    " hashes to " + digest.hex() + " but its parent at " +
                    describe(scope, parentPosition) + " records " + expected.hex()));
        }
        if (!result.visited.insert(childPosition).second)
        {
            BOOST_THROW_EXCEPTION(MPTInvariantViolation{} << bcos::errinfo_comment(
                                      "path-tree audit: two parents reach " +
                                      describe(scope, childPosition) + "; that is not a tree"));
        }
        ++result.edges;
        stack.push_back(Frame{.position = std::move(childPosition), .raw = child->second});
    };
    auto pushInlineChild = [&](bcos::bytes childPosition, bcos::bytesConstRef raw) {
        ++result.edges;
        stack.push_back(Frame{.position = std::move(childPosition), .raw = raw.toBytes()});
    };

    while (!stack.empty())
    {
        auto frame = std::move(stack.back());  // moved, not copied: Frame owns two buffers
        stack.pop_back();
        TrieNode const node = decodeNode(bcos::ref(frame.raw));

        if (auto const* leaf = std::get_if<LeafNode>(&node))
        {
            if (accountStorageRoots == nullptr)
            {
                continue;  // a storage-trie leaf carries a slot value; nothing to cross-check
            }
            bcos::bytes fullKey = frame.position;
            fullKey.insert(fullKey.end(), leaf->keyNibbles.begin(), leaf->keyNibbles.end());
            if (fullKey.size() != bcos::h256::SIZE * NIBBLES_PER_BYTE)
            {
                BOOST_THROW_EXCEPTION(
                    MPTInvariantViolation{} << bcos::errinfo_comment(
                        "path-tree audit: the account leaf at " + describe(scope, frame.position) +
                        " carries a " + std::to_string(fullKey.size()) +
                        "-nibble key; every account key is 64 nibbles"));
            }
            auto const ownerBytes = nibblesToBytes(bcos::ref(fullKey));
            bcos::h256 const owner{bcos::bytesConstRef{ownerBytes.data(), ownerBytes.size()}};
            auto const account = Account::decode(bcos::ref(leaf->value));
            accountStorageRoots->emplace(owner, account.storageRoot);
            continue;
        }

        if (auto const* extension = std::get_if<ExtensionNode>(&node))
        {
            // An extension consumes its whole shared prefix: child position = P || shared. The
            // ref is parsed by the SAME function the merge engine parses it with, so the Yellow
            // Paper's 32-byte rule is enforced here too: a stored ref that is neither a 33-byte
            // hash string nor a sub-32-byte inline node is malformed, and refFromRawBytes throws.
            // A hand-rolled "hash ref, else inline" test would silently accept it — and at the
            // account-trie root there is no parent digest that would catch it afterwards.
            auto childPosition =
                mpt::detail::childPosition(frame.position, bcos::ref(extension->sharedNibbles));
            auto const childRef = mpt::detail::refFromRawBytes(extension->child);
            if (childRef.kind() == NodeRef::Kind::Hash)
            {
                pushHashChild(frame.position, std::move(childPosition), childRef.hash());
            }
            else
            {
                pushInlineChild(std::move(childPosition), childRef.inlineRef());
            }
            continue;
        }

        if (auto const* branch = std::get_if<BranchNode>(&node))
        {
            // A branch's own value would belong to a key that ends at this position. Account and
            // slot keys are both a fixed 64 nibbles, so a branch that carried one would have to
            // sit at depth 64 and have no children — a shape the builder never emits. Nothing is
            // cross-checked from it, and the leaf case above is where every key is accounted for.
            for (std::uint8_t nibble = 0; nibble < NIBBLE_RANGE; ++nibble)
            {
                auto const& child = branch->children.at(nibble);
                if (child.isAbsent())
                {
                    continue;
                }
                // A branch consumes one nibble: child position = P || nibble.
                auto childPosition = mpt::detail::childPosition(frame.position, nibble);
                if (child.kind() == NodeRef::Kind::Hash)
                {
                    pushHashChild(frame.position, std::move(childPosition), child.hash());
                }
                else
                {
                    pushInlineChild(std::move(childPosition), child.inlineRef());
                }
            }
            continue;
        }

        // EmptyNode: only ever the encoding of an empty trie, which has no rows at all, so
        // finding one AT a position means the row is not a node of this tree.
        BOOST_THROW_EXCEPTION(MPTInvariantViolation{} << bcos::errinfo_comment(
                                  "path-tree audit: the row at " + describe(scope, frame.position) +
                                  " decodes to the empty node; an empty trie stores no rows"));
    }

    return result;
}

/// Record one warning: the count is always exact, the text is capped (kMaxAuditWarnings).
inline void addWarning(PathTreeAuditReport& report, std::string text)
{
    if (report.warnings.size() < kMaxAuditWarnings)
    {
        report.warnings.push_back(std::move(text));
    }
}

/// Count and describe the rows of @p trie that the walk never reached.
inline void collectOrphans(PathTreeAuditReport& report, TrieScope const& scope,
    NodeRows const& rows, std::set<bcos::bytes> const& visited)
{
    for (auto const& [position, raw] : rows)
    {
        if (visited.contains(position))
        {
            continue;
        }
        ++report.orphans;
        addWarning(report, "orphan node row at " + describe(scope, position) +
                               ": no live node references this position");
    }
}

}  // namespace detail

/// Audit every current node row in @p storage (pathdb spec §13, first two bullets).
///
/// TWO seek-scans, and that is the whole I/O plan: one over "/mptp/a" for the chain's single
/// account trie, one over "/mptp/s" for every storage trie. The second scan needs no per-owner
/// seek because a storage node's row key is `owner || compactPath`, so one owner's whole trie is
/// a contiguous run and an owner change marks its end — the scan settles each owner as its run
/// closes and holds only that one trie in memory.
///
/// A trie IS buffered before it is checked. Spec A.0 claims a prefix scan arrives in pre-order,
/// which would allow validating each row as it streams past; that is not true of the encoding
/// that shipped (see detail::NodeRows), so the scan collects a trie's rows into position order
/// first and walks that. Peak memory is therefore the account trie plus the largest single
/// storage trie.
///
/// @param expectedRoot the state root the CHAIN commits to for the version this store is supposed
///        to hold — a block header's stateRoot, or an operator's `--expect-root`. Supplying it is
///        what turns "these rows agree with each other" into "these rows are the right rows":
///        without it a store that is internally perfect but holds a different tree passes, and so
///        does a store whose account table was wiped (no rows to walk, every storage trie an
///        orphan, no error). Compared right after the account walk, before the storage scan, so a
///        wrong tree is reported as a wrong tree rather than as a hundred storage mismatches.
///
/// Errors throw, naming the position (a hole, a digest disagreement, a storageRoot that does not
/// match the owner's trie). Orphan rows do not: a row nothing references is unreachable and
/// harmless, which is the asymmetry spec §14 rests on, so they are counted and warned about.
///
/// @tparam HasherT the hash the chain's tries were BUILT with. It must be the same one the chain
///         uses for accountKeyHash, or every edge in the store will look corrupt.
/// @throws MPTInvariantViolation on any of the above; MPTDecodeError when a row in a node table
///         has a malformed row key or undecodable RLP.
template <history::SeekableStateStorage Storage,
    bcos::crypto::hasher::Hasher HasherT = bcos::crypto::hasher::openssl::OpenSSL_Keccak256_Hasher>
bcos::task::Task<PathTreeAuditReport> auditPathTree(
    Storage& storage, std::optional<bcos::h256> expectedRoot = std::nullopt)
{
    PathTreeAuditReport report;
    /// owner -> the storageRoot the owner's account leaf commits to. Filled by the account walk,
    /// consumed by the storage scan.
    std::map<bcos::h256, bcos::h256> expectedStorageRoots;

    // ---- the account trie: one seek, one walk ----
    detail::NodeRows accountRows;
    {
        auto iterator = co_await bcos::storage2::range(
            storage, bcos::storage2::RANGE_SEEK, executor_v1::StateKey{kMPTAccountTable, ""});
        while (true)
        {
            auto row = co_await iterator.next();
            if (!row)
            {
                break;
            }
            auto const& [rowKey, rowValue] = *row;
            auto parsed = parsePathNodeStateKey(rowKey);
            if (!parsed || parsed->scope.kind != TrieKind::Account)
            {
                break;  // walked off the end of "/mptp/a"
            }
            auto const* entry = history::detail::asStateValue(rowValue);
            if (entry == nullptr)
            {
                continue;  // a deletion sentinel on a mutable layer: the row is gone
            }
            auto const raw = entry->get();
            accountRows.emplace(std::move(parsed->position), bcos::bytes(raw.begin(), raw.end()));
        }
    }
    // An account table with no rows is not a missing answer: an empty trie stores no nodes, so
    // "no rows" IS the empty root. Saying so explicitly is what lets the comparison below catch a
    // wiped account table instead of comparing against a default-constructed zero.
    report.accountRoot = emptyRootHash<HasherT>();
    if (!accountRows.empty())
    {
        auto const walk = detail::walkTrie<HasherT>(
            TrieScope::account(), accountRows, std::addressof(expectedStorageRoots));
        report.accountRoot = walk.root;
        report.accountNodes = accountRows.size();
        report.accounts = expectedStorageRoots.size();
        report.verifiedEdges += walk.edges;
        detail::collectOrphans(report, TrieScope::account(), accountRows, walk.visited);
    }
    if (expectedRoot)
    {
        report.rootChecked = true;
        if (report.accountRoot != *expectedRoot)
        {
            BOOST_THROW_EXCEPTION(
                MPTInvariantViolation{} << bcos::errinfo_comment(
                    "path-tree audit: the node store holds state root " + report.accountRoot.hex() +
                    " but the chain commits to " + expectedRoot->hex() +
                    (accountRows.empty() ? "; the account node table is empty" : "")));
        }
    }

    // ---- the storage tries: one seek, one walk per owner ----
    std::set<bcos::h256> ownersWithRows;
    {
        std::optional<bcos::h256> currentOwner;
        detail::NodeRows ownerRows;

        // Settle one owner's run. Not a coroutine: everything it needs is already in memory, and
        // keeping it a plain lambda is what lets the run boundary and the end of the scan share
        // one code path.
        auto settleOwner = [&]() {
            if (!currentOwner || ownerRows.empty())
            {
                return;
            }
            auto const owner = *currentOwner;
            auto const scope = TrieScope::storage(owner);
            ownersWithRows.insert(owner);
            report.storageNodes += ownerRows.size();

            auto const expected = expectedStorageRoots.find(owner);
            if (expected == expectedStorageRoots.end())
            {
                // Nothing in the account trie names this owner, so nothing can reach these rows.
                // Same class as an unvisited row inside a trie: waste, not a hole.
                report.orphans += ownerRows.size();
                detail::addWarning(report, "orphan storage trie for owner " + owner.hex() + ": " +
                                               std::to_string(ownerRows.size()) +
                                               " rows, but no account leaf references this owner");
                ownerRows.clear();
                return;
            }
            if (expected->second == emptyRootHash<HasherT>())
            {
                // The account leaf commits to the empty trie while rows sit under its owner. A
                // reader short-circuits on the empty root and never sees them, so these rows are
                // unreachable — the same class as the case just above, and G4 says one delete too
                // few is waste, not a hole. Counted and warned about, and the scan goes on: an
                // exception here would abort the whole storage pass and hide every later owner.
                //
                // It is still worth telling apart from an ordinary orphan, because it has a
                // specific cause — a storage-trie drop that did not delete everything it
                // enumerated — so it also lands in suspectOrphans for a caller that wants to act
                // on it.
                report.orphans += ownerRows.size();
                report.suspectOrphans += ownerRows.size();
                detail::addWarning(report, "owner " + owner.hex() +
                                               " commits to the empty storage root but " +
                                               std::to_string(ownerRows.size()) +
                                               " rows remain — incomplete storage-trie drop");
                ownerRows.clear();
                return;
            }
            auto const walk = detail::walkTrie<HasherT>(scope, ownerRows, nullptr);
            if (walk.root != expected->second)
            {
                BOOST_THROW_EXCEPTION(
                    MPTInvariantViolation{} << bcos::errinfo_comment(
                        "path-tree audit: the storage trie of owner " + owner.hex() + " roots at " +
                        walk.root.hex() + " but its account leaf records " +
                        expected->second.hex()));
            }
            ++report.storageTries;
            report.verifiedEdges += walk.edges;
            detail::collectOrphans(report, scope, ownerRows, walk.visited);
            ownerRows.clear();
        };

        auto iterator = co_await bcos::storage2::range(
            storage, bcos::storage2::RANGE_SEEK, executor_v1::StateKey{kMPTStorageTable, ""});
        while (true)
        {
            auto row = co_await iterator.next();
            if (!row)
            {
                break;
            }
            auto const& [rowKey, rowValue] = *row;
            auto parsed = parsePathNodeStateKey(rowKey);
            if (!parsed || parsed->scope.kind != TrieKind::Storage)
            {
                break;  // walked off the end of "/mptp/s"
            }
            auto const* entry = history::detail::asStateValue(rowValue);
            if (entry == nullptr)
            {
                continue;
            }
            if (!currentOwner || *currentOwner != parsed->scope.owner)
            {
                settleOwner();
                currentOwner = parsed->scope.owner;
            }
            auto const raw = entry->get();
            ownerRows.emplace(std::move(parsed->position), bcos::bytes(raw.begin(), raw.end()));
        }
        settleOwner();
    }

    // An account that commits to a non-empty storage root must have a trie to back it. The
    // mirror of the check inside settleOwner, for the owners the storage scan never reached.
    for (auto const& [owner, storageRoot] : expectedStorageRoots)
    {
        if (storageRoot == emptyRootHash<HasherT>() || ownersWithRows.contains(owner))
        {
            continue;
        }
        BOOST_THROW_EXCEPTION(MPTInvariantViolation{} << bcos::errinfo_comment(
                                  "path-tree audit: the account leaf for owner " + owner.hex() +
                                  " records storage root " + storageRoot.hex() +
                                  " but no storage node row exists for it"));
    }

    co_return report;
}

}  // namespace bcos::ledger::mpt::audit
