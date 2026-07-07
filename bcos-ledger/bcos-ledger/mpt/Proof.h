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
 * @file Proof.h
 * @brief EIP-1186 account + storage proof generation over an MPT state root (spec §5.9)
 */
#pragma once

#include "Account.h"
#include "Constants.h"
#include "Errors.h"
#include "MPTReadView.h"
#include "Nibble.h"
#include "NodeDecoder.h"
#include "StorageValueCodec.h"
#include "TrieNode.h"
// AnyHasher.h defines the free bcos::crypto::hasher::hash(); OpenSSLHasher.h only defines the
// hasher type. A unity build can mask a missing include of the former, so keep both explicit.
#include <bcos-crypto/hasher/AnyHasher.h>
#include <bcos-crypto/hasher/OpenSSLHasher.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <boost/throw_exception.hpp>
#include <algorithm>
#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace bcos::ledger::mpt
{

/// Proof for one storage slot (spec §5.9). For an absent slot, EIP-1186 semantics apply: value is
/// empty (JSON 0x0) and proof holds the nodes visited until the walk dead-ended (exclusion proof).
struct StorageProof
{
    bcos::h256 key;  ///< the raw 32-byte slot key as requested (NOT keccak-transformed)
    /// The leaf value exactly as stored in the trie — the RLP encoding of the big-endian-trimmed
    /// slot value, matching go-ethereum. Empty when the slot is absent.
    bcos::bytes value;
    std::vector<bcos::bytes> proof;  ///< raw RLP node encodings, storage root first
};

/// EIP-1186 result: the account fields plus the Merkle paths proving them (spec §5.9).
struct EIP1186Proof
{
    bcos::Address address;
    bcos::u256 balance;
    bcos::u256 nonce;
    bcos::h256 codeHash;
    bcos::h256 storageHash;  ///< the account's storage trie root
    /// Raw RLP node encodings from the state root down to the account leaf. Only hash-referenced
    /// nodes appear; inline children are embedded in their parent's encoding (go-ethereum
    /// semantics).
    std::vector<bcos::bytes> accountProof;
    std::vector<StorageProof> storageProof;  ///< one entry per requested slot, in request order
};

/// Error outcomes of generateProof (spec §5.9). Returned as a variant alternative rather than
/// thrown: both cases are expected request-level outcomes (a dormant account under scenario A, an
/// unknown/uncommitted root), not programming errors — the RPC layer maps them to JSON-RPC error
/// codes. Genuine storage inconsistencies still throw MPTInvariantViolation, like Trie does.
enum class ProofErrorCode : uint8_t
{
    AccountNotInMPT,    ///< the walk from stateRoot dead-ends before an account leaf
    BlockNotCommitted,  ///< stateRoot itself is absent from node storage (unknown/uncommitted)
};

namespace detail
{
/// Outcome of one root→leaf proof walk (shared by the account and storage trie passes).
struct ProofWalk
{
    /// Raw RLP encoding of every hash-referenced node visited, root first. Inline children are
    /// NOT separate items — their bytes are embedded in the parent's encoding.
    std::vector<bcos::bytes> nodes;
    std::optional<bcos::bytes> value;  ///< the leaf/branch value when the key is present
    bool rootMissing{false};           ///< the root hash itself is absent from storage
};

/// Walk from @p root along @p path (nibble sequence), collecting each hash-referenced node's raw
/// RLP. Stops at the leaf holding the key, or at the node where the path dead-ends (exclusion
/// proof — the collected prefix is still returned). This descent intentionally does not reuse
/// Trie::get(): the proof needs every visited node's raw encoding, which Trie discards.
/// @throws MPTInvariantViolation when a non-root referenced node is missing from storage.
template <bcos::storage2::ReadableStorage<bcos::h256> Storage>
bcos::task::Task<ProofWalk> proofWalk(Storage& storage, bcos::h256 root, bcos::bytes path)
{
    ProofWalk out;
    auto rootRaw = co_await bcos::storage2::readOne(storage, root);
    if (!rootRaw)
    {
        out.rootMissing = true;
        co_return out;
    }
    out.nodes.push_back(std::move(*rootRaw));
    // decodeNode deep-copies into the TrieNode, so later push_back reallocations are harmless.
    TrieNode node = decodeNode(bcos::ref(out.nodes.back()));
    size_t pos = 0;  // nibbles consumed so far

    while (true)
    {
        if (std::holds_alternative<EmptyNode>(node))
        {
            co_return out;  // dead end
        }
        if (auto const* leaf = std::get_if<LeafNode>(&node))
        {
            if (path.size() - pos == leaf->keyNibbles.size() &&
                std::equal(leaf->keyNibbles.begin(), leaf->keyNibbles.end(), path.begin() + pos))
            {
                out.value = leaf->value;
            }
            co_return out;  // key found, or a diverging leaf: either way the walk stops here
        }
        if (auto const* ext = std::get_if<ExtensionNode>(&node))
        {
            if (path.size() - pos < ext->sharedNibbles.size() ||
                !std::equal(
                    ext->sharedNibbles.begin(), ext->sharedNibbles.end(), path.begin() + pos))
            {
                co_return out;  // path diverges inside the extension: dead end
            }
            pos += ext->sharedNibbles.size();
            // The child is a raw RLP ref: EITHER the 33-byte hash string (0xa0 + digest) OR the
            // inline child's complete RLP encoding (already part of this node's proof item).
            if (ext->child.size() == HASH_REF_ENCODED_SIZE && ext->child[0] == RLP_HASH_REF_PREFIX)
            {
                bcos::h256 const childHash(ext->child.data() + 1, bcos::h256::SIZE);
                auto childRaw = co_await bcos::storage2::readOne(storage, childHash);
                if (!childRaw)
                {
                    BOOST_THROW_EXCEPTION(MPTInvariantViolation{} << bcos::errinfo_comment(
                                              "proofWalk: storage lacks a referenced trie node"));
                }
                out.nodes.push_back(std::move(*childRaw));
                TrieNode next = decodeNode(bcos::ref(out.nodes.back()));
                node = std::move(next);
            }
            else
            {
                // Decode fully into a temporary BEFORE overwriting node: ext->child lives inside
                // the current alternative.
                TrieNode next = decodeNode(bcos::ref(ext->child));
                node = std::move(next);
            }
            continue;
        }
        // BranchNode
        auto const& branch = std::get<BranchNode>(node);
        if (pos == path.size())
        {
            // All nibbles consumed at a branch (cannot happen for fixed 64-nibble keccak paths,
            // handled for shape-completeness like Trie::get).
            if (!branch.value.empty())
            {
                out.value = branch.value;
            }
            co_return out;
        }
        NodeRef const& child = branch.children.at(path.at(pos));
        ++pos;
        if (child.kind == NodeRef::Kind::Hash)
        {
            bcos::h256 const childHash = child.hash;
            auto childRaw = co_await bcos::storage2::readOne(storage, childHash);
            if (!childRaw)
            {
                BOOST_THROW_EXCEPTION(MPTInvariantViolation{} << bcos::errinfo_comment(
                                          "proofWalk: storage lacks a referenced trie node"));
            }
            out.nodes.push_back(std::move(*childRaw));
            TrieNode next = decodeNode(bcos::ref(out.nodes.back()));
            node = std::move(next);
        }
        else if (child.inlineBytes.empty())
        {
            co_return out;  // absent child: dead end
        }
        else
        {
            // Inline child: embedded in this branch's encoding, not a separate proof item.
            TrieNode next = decodeNode(bcos::ref(child.inlineBytes));
            node = std::move(next);
        }
    }
}
}  // namespace detail

/// Generate an EIP-1186 proof for @p address (and @p slots) against @p stateRoot (spec §5.9).
/// Walks the state trie from stateRoot along accountKeyHash(address) collecting every
/// hash-referenced node's raw RLP; decodes the account leaf; then walks the account's storage
/// trie from Account::storageRoot along slotKeyHash(slot) for each requested slot the same way.
///
/// Outcomes:
///   - stateRoot absent from storage        → ProofErrorCode::BlockNotCommitted
///   - stateRoot == emptyRootHash(), or the account walk dead-ends
///                                           → ProofErrorCode::AccountNotInMPT
///   - a requested slot is absent            → StorageProof with empty value and the dead-end
///                                             prefix as exclusion proof (EIP-1186 value 0x0)
///
/// Proof generation is a read-only cold path: instantiate over a Storage without an extra cache
/// layer (e.g. the RocksDB-backed store directly) to avoid polluting the commit path's cache.
/// @throws MPTInvariantViolation on storage inconsistency (a committed root referencing a missing
/// node), MPTDecodeError on a malformed account leaf — programming/data errors, not request
/// errors.
///
/// @tparam HasherT the slot-key hash function (Hasher concept), owned here and reused across the
/// requested slots; keccak256 by default, SM3 for guomi deployments (same end-to-end caveat as
/// the builders: accountKeyHash and the trie the proof reads were produced with the chain's
/// hasher — parameterizing this function alone does not make an SM3 chain, it keeps the door
/// open for one).
template <bcos::storage2::ReadableStorage<bcos::h256> Storage,
    bcos::crypto::hasher::Hasher HasherT = bcos::crypto::hasher::openssl::OpenSSL_Keccak256_Hasher>
bcos::task::Task<std::variant<EIP1186Proof, ProofErrorCode>> generateProof(Storage& storage,
    bcos::h256 stateRoot, bcos::Address address, std::span<bcos::h256 const> slots)
{
    if (stateRoot == emptyRootHash())
    {
        co_return ProofErrorCode::AccountNotInMPT;  // empty trie holds no accounts
    }

    auto const addressKeyHash = accountKeyHash(address);
    auto accountWalk =
        co_await detail::proofWalk(storage, stateRoot, bytesToNibbles(addressKeyHash.ref()));
    if (accountWalk.rootMissing)
    {
        co_return ProofErrorCode::BlockNotCommitted;
    }
    if (!accountWalk.value)
    {
        co_return ProofErrorCode::AccountNotInMPT;
    }
    auto const account = Account::decode(bcos::ref(*accountWalk.value));

    EIP1186Proof out;
    out.address = address;
    out.balance = account.balance;
    out.nonce = account.nonce;
    out.codeHash = account.codeHash;
    out.storageHash = account.storageRoot;
    out.accountProof = std::move(accountWalk.nodes);

    out.storageProof.reserve(slots.size());
    HasherT hasher;  // one hash context, reused for every requested slot's key transform
    for (auto const& slot : slots)
    {
        StorageProof entry;
        entry.key = slot;
        if (account.storageRoot != emptyRootHash())
        {
            auto const slotHash = slotKeyHash(slot, hasher);
            auto slotWalk = co_await detail::proofWalk(
                storage, account.storageRoot, bytesToNibbles(slotHash.ref()));
            if (slotWalk.rootMissing)
            {
                // A committed account leaf references this root; its absence is corruption, not a
                // request-level outcome.
                BOOST_THROW_EXCEPTION(
                    MPTInvariantViolation{} << bcos::errinfo_comment(
                        "generateProof: account references a storage root absent from storage"));
            }
            entry.proof = std::move(slotWalk.nodes);
            if (slotWalk.value)
            {
                entry.value = std::move(*slotWalk.value);
            }
        }
        // else: empty storage trie — every slot is absent; empty value, empty proof.
        out.storageProof.push_back(std::move(entry));
    }
    co_return out;
}

/// Result of independently verifying an EIP1186Proof against a claimed state root (spec §7.1).
/// The recovered* fields are what the proof bytes actually commit to; they are only filled when
/// the account chain verified AND matched the proof's claimed top-level fields.
struct VerifyResult
{
    bool accountValid = false;
    bcos::u256 recoveredBalance{};
    bcos::u256 recoveredNonce{};
    bcos::h256 recoveredCodeHash{};
    bcos::h256 recoveredStorageRoot{};
    std::vector<bool> storageValid;  ///< parallel to proof.storageProof
    /// Raw leaf value bytes recovered from each slot's chain (empty = proven absent). Filled only
    /// for slots whose hash chain verified.
    std::vector<bcos::bytes> recoveredStorageValues;
};

namespace detail
{
/// Verify one root→leaf hash chain using ONLY @p proofNodes — no storage access. Re-derives every
/// hash link: proofNodes[0] must hash to @p expectedRoot, and each hash reference encountered
/// along @p path must match the hash of the next consumed proof item. Inline children are decoded
/// in place without consuming an item, mirroring how generateProof embeds them.
///
/// This walk is deliberately independent of proofWalk (spec §7.1 cross-validation): the two share
/// only the low-level decode helpers (decodeNode, nibble utils), so a traversal bug in one cannot
/// hide in the other.
///
/// @param path the full nibble path of the key being proven (64 nibbles for 32-byte key hashes)
/// @return nullopt   → the chain is INVALID (hash mismatch, malformed node, dangling hash ref,
///                     or trailing unused proof items — padded proofs are rejected)
///         empty     → valid EXCLUSION proof: the walk dead-ends, the key is absent
///         non-empty → the proven leaf/branch value
template <bcos::crypto::hasher::Hasher HasherT>
std::optional<bcos::bytes> verifyProofChain(bcos::h256 const& expectedRoot,
    std::span<bcos::bytes const> proofNodes, bcos::bytes const& path, HasherT& hasher)
{
    if (proofNodes.empty())
    {
        return std::nullopt;  // proving anything needs at least the root node
    }
    bcos::h256 expected = expectedRoot;
    size_t idx = 0;                        // next proof item to consume
    size_t pos = 0;                        // nibbles consumed so far
    std::optional<bcos::bytes> concluded;  // set once the walk terminates

    while (!concluded)
    {
        if (idx >= proofNodes.size())
        {
            return std::nullopt;  // a hash ref is pending but the proof ran out of items
        }
        bcos::h256 got;
        bcos::crypto::hasher::hash(hasher, bcos::ref(proofNodes[idx]), got);
        if (got != expected)
        {
            return std::nullopt;  // broken hash link
        }
        TrieNode node;
        try
        {
            node = decodeNode(bcos::ref(proofNodes[idx]));
        }
        catch (...)
        {
            return std::nullopt;  // malformed node: invalid, never an escaping exception
        }
        ++idx;

        // Descend through this proof item, expanding inline children embedded in it, until the
        // walk terminates or reaches the next hash-referenced node.
        bool needNextItem = false;
        while (!needNextItem && !concluded)
        {
            if (std::holds_alternative<EmptyNode>(node))
            {
                concluded.emplace();  // dead end: exclusion
                break;
            }
            if (auto const* leaf = std::get_if<LeafNode>(&node))
            {
                if (path.size() - pos == leaf->keyNibbles.size() &&
                    std::equal(
                        leaf->keyNibbles.begin(), leaf->keyNibbles.end(), path.begin() + pos))
                {
                    concluded = leaf->value;
                }
                else
                {
                    concluded.emplace();  // diverging leaf: exclusion
                }
                break;
            }
            if (auto const* ext = std::get_if<ExtensionNode>(&node))
            {
                if (path.size() - pos < ext->sharedNibbles.size() ||
                    !std::equal(
                        ext->sharedNibbles.begin(), ext->sharedNibbles.end(), path.begin() + pos))
                {
                    concluded.emplace();  // path diverges inside the extension: exclusion
                    break;
                }
                pos += ext->sharedNibbles.size();
                if (ext->child.size() == HASH_REF_ENCODED_SIZE &&
                    ext->child[0] == RLP_HASH_REF_PREFIX)
                {
                    expected = bcos::h256(ext->child.data() + 1, bcos::h256::SIZE);
                    needNextItem = true;
                }
                else
                {
                    TrieNode next;
                    try
                    {
                        next = decodeNode(bcos::ref(ext->child));
                    }
                    catch (...)
                    {
                        return std::nullopt;
                    }
                    node = std::move(next);
                }
                continue;
            }
            auto const& branch = std::get<BranchNode>(node);
            if (pos == path.size())
            {
                concluded = branch.value;  // empty branch value = exclusion
                break;
            }
            NodeRef const& child = branch.children.at(path.at(pos));
            ++pos;
            if (child.kind == NodeRef::Kind::Hash)
            {
                expected = child.hash;
                needNextItem = true;
            }
            else if (child.inlineBytes.empty())
            {
                concluded.emplace();  // absent child: exclusion
            }
            else
            {
                TrieNode next;
                try
                {
                    next = decodeNode(bcos::ref(child.inlineBytes));
                }
                catch (...)
                {
                    return std::nullopt;
                }
                node = std::move(next);
            }
        }
    }

    if (idx != proofNodes.size())
    {
        return std::nullopt;  // trailing unused items: reject padded proofs
    }
    return concluded;
}
}  // namespace detail

/// Independently verify an EIP-1186 proof against @p claimedRoot (spec §7.1). Pure and
/// synchronous: consumes ONLY the proof byte arrays — never storage — and re-derives every hash
/// link via detail::verifyProofChain, the deliberate second implementation cross-validating
/// generateProof's proofWalk.
///
/// accountValid requires all of: the account chain anchors at @p claimedRoot, ends at a present
/// account leaf, the leaf decodes, and the decoded fields EQUAL the proof's claimed
/// nonce/balance/codeHash/storageHash. Malformed leaf bytes yield accountValid=false, never an
/// exception. On any account-side failure the function returns immediately with every
/// storageValid false — slot chains hang off proof.storageHash, which an invalid account side
/// has not established.
///
/// Per slot i: against an empty storage root, valid iff value and proof are both empty (matching
/// generateProof's empty-trie output); otherwise the slot chain must verify against
/// proof.storageHash and its recovered value must equal storageProof[i].value (empty = proven
/// absence).
template <
    bcos::crypto::hasher::Hasher HasherT = bcos::crypto::hasher::openssl::OpenSSL_Keccak256_Hasher>
VerifyResult verifyProof(bcos::h256 claimedRoot, EIP1186Proof const& proof)
{
    VerifyResult out;
    out.storageValid.assign(proof.storageProof.size(), false);
    out.recoveredStorageValues.assign(proof.storageProof.size(), bcos::bytes{});

    HasherT hasher;  // one hash context, reused for the account chain and every slot
    auto const accountPath = bytesToNibbles(accountKeyHash(proof.address).ref());
    auto const accountLeaf = detail::verifyProofChain(
        claimedRoot, std::span<bcos::bytes const>(proof.accountProof), accountPath, hasher);
    if (!accountLeaf || accountLeaf->empty())
    {
        // Invalid chain, or an exclusion: an EIP-1186 proof for a present account must contain
        // the account leaf.
        return out;
    }

    Account decoded;
    try
    {
        decoded = Account::decode(bcos::ref(*accountLeaf));
    }
    catch (...)
    {
        return out;  // malformed account leaf: invalid, never an escaping exception
    }
    if (decoded.nonce != proof.nonce || decoded.balance != proof.balance ||
        decoded.codeHash != proof.codeHash || decoded.storageRoot != proof.storageHash)
    {
        return out;  // the chain proves a DIFFERENT account state than the proof claims
    }
    out.accountValid = true;
    out.recoveredNonce = decoded.nonce;
    out.recoveredBalance = decoded.balance;
    out.recoveredCodeHash = decoded.codeHash;
    out.recoveredStorageRoot = decoded.storageRoot;

    for (size_t i = 0; i < proof.storageProof.size(); ++i)
    {
        auto const& entry = proof.storageProof[i];
        if (proof.storageHash == emptyRootHash<HasherT>())
        {
            // Empty storage trie: generateProof emits empty value + empty proof for every slot.
            out.storageValid[i] = entry.value.empty() && entry.proof.empty();
            continue;
        }
        auto const slotPath = bytesToNibbles(slotKeyHash(entry.key, hasher).ref());
        auto recovered = detail::verifyProofChain(
            proof.storageHash, std::span<bcos::bytes const>(entry.proof), slotPath, hasher);
        if (!recovered)
        {
            continue;  // invalid slot chain: storageValid[i] stays false
        }
        out.recoveredStorageValues[i] = std::move(*recovered);
        out.storageValid[i] = (out.recoveredStorageValues[i] == entry.value);
    }
    return out;
}

}  // namespace bcos::ledger::mpt
