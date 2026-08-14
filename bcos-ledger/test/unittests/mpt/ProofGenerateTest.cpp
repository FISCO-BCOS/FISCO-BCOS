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
 * @file ProofGenerateTest.cpp
 * @brief Unit tests for EIP-1186 proof generation (spec §5.9)
 */

#include "TestHelpers.h"
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-ledger/mpt/Account.h>
#include <bcos-ledger/mpt/Constants.h>
#include <bcos-ledger/mpt/HashBuilder.h>
#include <bcos-ledger/mpt/MPTReadView.h>
#include <bcos-ledger/mpt/NodeDecoder.h>
#include <bcos-ledger/mpt/Proof.h>
#include <bcos-ledger/mpt/StorageValueCodec.h>
#include <bcos-task/Task.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <boost/test/unit_test.hpp>
#include <optional>
#include <variant>
#include <vector>

namespace bcos::ledger::mpt::test
{

using MemStorage = bcos::storage2::memory_storage::MemoryStorage<bcos::h256, bcos::bytes>;

namespace
{

bcos::h256 keccak(bcos::bytes const& data)
{
    bcos::crypto::hasher::openssl::OpenSSL_Keccak256_Hasher hasher;
    bcos::h256 out;
    bcos::crypto::hasher::hash(hasher, bcos::ref(data), out);
    return out;
}

/// Result of the in-test structural proof verifier (the oracle).
struct ChainCheck
{
    bool hashChainOk{false};   ///< every proof item's keccak matched the hash that referenced it
    bool allNodesUsed{false};  ///< the walk consumed exactly the whole proof vector (no siblings)
    std::optional<bcos::bytes> value;  ///< the value found at the end of the walk, if present
};

/// Independent re-walk of a proof: recompute each node's keccak, check it matches the reference
/// from the previous node (proof[0] must hash to @p root), descend along keccak-path @p keyHash
/// through hash refs AND inline children, and record the terminal value. This is a different
/// algorithm from Proof.h's storage-backed walk — it reads only the proof vector — so it acts as
/// a self-verification oracle against the root.
ChainCheck verifyProofChain(
    std::vector<bcos::bytes> const& proof, bcos::h256 const& root, bcos::h256 const& keyHash)
{
    ChainCheck res;
    if (proof.empty())
    {
        return res;
    }
    auto const path = bytesToNibbles(keyHash.ref());
    size_t pos = 0;
    size_t idx = 0;
    bcos::h256 expected = root;

    auto finish = [&]() {
        res.hashChainOk = true;
        res.allNodesUsed = (idx == proof.size());
        return res;
    };

    while (true)
    {
        if (idx >= proof.size() || keccak(proof.at(idx)) != expected)
        {
            return res;  // chain runs past the end, or a hash mismatch: not a valid proof
        }
        TrieNode node = decodeNode(bcos::ref(proof.at(idx)));
        ++idx;

        // Descend through this proof item, expanding any inline children embedded in it, until
        // the walk terminates or reaches the next hash-referenced node.
        bool needNextItem = false;
        while (!needNextItem)
        {
            if (std::holds_alternative<EmptyNode>(node))
            {
                return finish();  // dead end
            }
            if (auto const* leaf = std::get_if<LeafNode>(&node))
            {
                if (path.size() - pos == leaf->keyNibbles.size() &&
                    std::equal(
                        leaf->keyNibbles.begin(), leaf->keyNibbles.end(), path.begin() + pos))
                {
                    res.value = leaf->value;
                }
                return finish();
            }
            if (auto const* ext = std::get_if<ExtensionNode>(&node))
            {
                if (path.size() - pos < ext->sharedNibbles.size() ||
                    !std::equal(
                        ext->sharedNibbles.begin(), ext->sharedNibbles.end(), path.begin() + pos))
                {
                    return finish();  // dead end inside the extension
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
                    TrieNode next = decodeNode(bcos::ref(ext->child));
                    node = std::move(next);
                }
                continue;
            }
            auto const& branch = std::get<BranchNode>(node);
            if (pos == path.size())
            {
                if (!branch.value.empty())
                {
                    res.value = branch.value;
                }
                return finish();
            }
            NodeRef const& child = branch.children.at(path.at(pos));
            ++pos;
            if (child.kind() == NodeRef::Kind::Hash)
            {
                expected = child.hash();
                needNextItem = true;
            }
            else if (child.isAbsent())
            {
                return finish();  // absent child: dead end
            }
            else
            {
                TrieNode next = decodeNode(child.inlineRef());
                node = std::move(next);
            }
        }
    }
}

}  // namespace

BOOST_AUTO_TEST_SUITE(ProofGenerateSuite)

// A single-account trie: the proof is a chain whose first node keccaks to the state root and
// whose last node is the account leaf. The structural verifier is the oracle.
BOOST_AUTO_TEST_CASE(SingleAccountProofSelfVerifies)
{
    MemStorage storage;
    bcos::Address const addr = makeAddress(0xab);
    Account account;
    account.nonce = 1;
    account.balance = 100;
    auto const stateRoot = seedStateTrieFlushed(storage, {{addr, account}});

    auto result = bcos::task::syncWait(
        generateProof(storage, stateRoot, addr, std::span<bcos::h256 const>{}));
    BOOST_REQUIRE(std::holds_alternative<EIP1186Proof>(result));
    auto const& proof = std::get<EIP1186Proof>(result);

    BOOST_CHECK_EQUAL(proof.address, addr);
    BOOST_CHECK_EQUAL(proof.balance, 100);
    BOOST_CHECK_EQUAL(proof.nonce, 1);
    BOOST_CHECK_EQUAL(proof.codeHash, emptyCodeHash());
    BOOST_CHECK_EQUAL(proof.storageHash, emptyRootHash());
    BOOST_CHECK(proof.storageProof.empty());

    // Single account → the trie is exactly one leaf node, and it hashes to the root.
    BOOST_REQUIRE_EQUAL(proof.accountProof.size(), 1);
    BOOST_CHECK_EQUAL(keccak(proof.accountProof.front()), stateRoot);

    auto const check = verifyProofChain(proof.accountProof, stateRoot, accountKeyHash(addr));
    BOOST_CHECK(check.hashChainOk);
    BOOST_CHECK(check.allNodesUsed);
    BOOST_REQUIRE(check.value.has_value());
    BOOST_CHECK(*check.value == account.encode());
    auto const decoded = Account::decode(bcos::ref(*check.value));
    BOOST_CHECK_EQUAL(decoded.nonce, 1);
    BOOST_CHECK_EQUAL(decoded.balance, 100);
}

// ≥17 accounts force branch nodes. Proofs for three different accounts each self-verify, and the
// allNodesUsed check proves sibling data does not leak into the proof beyond the walked path.
BOOST_AUTO_TEST_CASE(MultiAccountProofsSelfVerify)
{
    MemStorage storage;
    std::vector<std::pair<bcos::Address, Account>> accounts;
    for (uint8_t i = 1; i <= 20; ++i)
    {
        Account account;
        account.nonce = i;
        account.balance = 1000 + i;
        accounts.emplace_back(makeAddress(i), account);
    }
    auto const stateRoot = seedStateTrieFlushed(storage, accounts);

    for (uint8_t i : {uint8_t{1}, uint8_t{7}, uint8_t{20}})
    {
        bcos::Address const addr = makeAddress(i);
        auto result = bcos::task::syncWait(
            generateProof(storage, stateRoot, addr, std::span<bcos::h256 const>{}));
        BOOST_REQUIRE(std::holds_alternative<EIP1186Proof>(result));
        auto const& proof = std::get<EIP1186Proof>(result);

        BOOST_CHECK_EQUAL(proof.nonce, i);
        BOOST_CHECK_EQUAL(proof.balance, 1000 + i);
        // 20 accounts cannot fit a single node: at least a root branch plus a leaf.
        BOOST_CHECK_GE(proof.accountProof.size(), 2);

        auto const check = verifyProofChain(proof.accountProof, stateRoot, accountKeyHash(addr));
        BOOST_CHECK(check.hashChainOk);
        BOOST_CHECK(check.allNodesUsed);
        BOOST_REQUIRE(check.value.has_value());
        auto const decoded = Account::decode(bcos::ref(*check.value));
        BOOST_CHECK_EQUAL(decoded.nonce, i);
        BOOST_CHECK_EQUAL(decoded.balance, 1000 + i);
    }
}

// An account with a storage trie: two present slots self-verify and yield the stored RLP values;
// an absent slot yields an empty (zero) value plus a non-empty dead-end (exclusion) proof.
BOOST_AUTO_TEST_CASE(StorageSlotProofsIncludingExclusion)
{
    MemStorage storage;
    bcos::h256 const slotA = makeHash(0x01);
    bcos::h256 const slotB = makeHash(0x02);
    bcos::h256 const slotMissing = makeHash(0x03);
    bcos::bytes const valueA{0x2a};              // RLP(42): single byte < 0x80
    bcos::bytes const valueB{0x82, 0x13, 0x37};  // RLP(0x1337): 2-byte string

    // Build the storage trie into the SAME node storage; its root becomes account.storageRoot.
    auto const storageRoot = seedTrieFlushed(storage, emptyRootHash(),
        {{slotKeyHash(slotA), valueA},
            {slotKeyHash(slotB),
                valueB}}).root;

    bcos::Address const addr = makeAddress(0xab);
    Account account;
    account.nonce = 5;
    account.balance = 777;
    account.storageRoot = storageRoot;
    auto const stateRoot = seedStateTrieFlushed(storage, {{addr, account}});

    std::vector<bcos::h256> const slots{slotA, slotB, slotMissing};
    auto result = bcos::task::syncWait(generateProof(storage, stateRoot, addr, slots));
    BOOST_REQUIRE(std::holds_alternative<EIP1186Proof>(result));
    auto const& proof = std::get<EIP1186Proof>(result);

    BOOST_CHECK_EQUAL(proof.storageHash, storageRoot);
    BOOST_REQUIRE_EQUAL(proof.storageProof.size(), 3);

    // Present slots: values match what was stored, and each proof self-verifies.
    for (size_t i : {size_t{0}, size_t{1}})
    {
        auto const& entry = proof.storageProof.at(i);
        BOOST_CHECK_EQUAL(entry.key, slots.at(i));
        BOOST_CHECK(entry.value == (i == 0 ? valueA : valueB));
        auto const check = verifyProofChain(entry.proof, storageRoot, slotKeyHash(entry.key));
        BOOST_CHECK(check.hashChainOk);
        BOOST_CHECK(check.allNodesUsed);
        BOOST_REQUIRE(check.value.has_value());
        BOOST_CHECK(*check.value == entry.value);
    }

    // Absent slot: empty (zero) value, non-empty exclusion proof that dead-ends cleanly.
    auto const& absent = proof.storageProof.at(2);
    BOOST_CHECK_EQUAL(absent.key, slotMissing);
    BOOST_CHECK(absent.value.empty());
    BOOST_CHECK(!absent.proof.empty());
    auto const check = verifyProofChain(absent.proof, storageRoot, slotKeyHash(slotMissing));
    BOOST_CHECK(check.hashChainOk);
    BOOST_CHECK(check.allNodesUsed);
    BOOST_CHECK(!check.value.has_value());
}

// An unknown address on a populated trie, and any address on the empty root, both yield
// AccountNotInMPT. A root absent from storage yields BlockNotCommitted.
BOOST_AUTO_TEST_CASE(AccountNotInMPTErrors)
{
    MemStorage storage;
    bcos::Address const known = makeAddress(0xab);
    Account account;
    account.nonce = 1;
    auto const stateRoot = seedStateTrieFlushed(storage, {{known, account}});

    bcos::Address const unknown = makeAddress(0xcd);
    auto missing = bcos::task::syncWait(
        generateProof(storage, stateRoot, unknown, std::span<bcos::h256 const>{}));
    BOOST_REQUIRE(std::holds_alternative<ProofErrorCode>(missing));
    BOOST_CHECK(std::get<ProofErrorCode>(missing) == ProofErrorCode::AccountNotInMPT);

    auto emptyRoot = bcos::task::syncWait(
        generateProof(storage, emptyRootHash(), known, std::span<bcos::h256 const>{}));
    BOOST_REQUIRE(std::holds_alternative<ProofErrorCode>(emptyRoot));
    BOOST_CHECK(std::get<ProofErrorCode>(emptyRoot) == ProofErrorCode::AccountNotInMPT);

    auto unknownRoot = bcos::task::syncWait(
        generateProof(storage, makeHash(0x99), known, std::span<bcos::h256 const>{}));
    BOOST_REQUIRE(std::holds_alternative<ProofErrorCode>(unknownRoot));
    BOOST_CHECK(std::get<ProofErrorCode>(unknownRoot) == ProofErrorCode::BlockNotCommitted);
}

// The hasher is a template parameter with a keccak256 default; the guomi door stays open:
// the SM3 instantiation is well-formed and the injection form actually uses the injected hasher.
BOOST_AUTO_TEST_CASE(HasherParameterizationKeepsSm3Possible)
{
    // Compile-time coverage: taking the address forces full instantiation with SM3.
    [[maybe_unused]] auto* sm3Instantiation =
        &generateProof<MemStorage, bcos::crypto::hasher::openssl::OpenSSL_SM3_Hasher>;

    bcos::h256 const slot = makeHash(0x01);
    bcos::crypto::hasher::openssl::OpenSSL_SM3_Hasher sm3;
    auto const sm3Key = slotKeyHash(slot, sm3);
    BOOST_CHECK(sm3Key != slotKeyHash(slot));  // keccak default != SM3 injection

    bcos::crypto::hasher::openssl::OpenSSL_Keccak256_Hasher keccakHasher;
    BOOST_CHECK(slotKeyHash(slot, keccakHasher) == slotKeyHash(slot));
}

// Generating the same proof twice yields byte-identical output.
BOOST_AUTO_TEST_CASE(DeterministicOutput)
{
    MemStorage storage;
    bcos::h256 const slot = makeHash(0x01);
    bcos::bytes const slotValue{0x2a};

    auto const storageRoot =
        seedTrieFlushed(storage, emptyRootHash(), {{slotKeyHash(slot), slotValue}}).root;

    std::vector<std::pair<bcos::Address, Account>> accounts;
    for (uint8_t i = 1; i <= 20; ++i)
    {
        Account account;
        account.nonce = i;
        if (i == 3)
        {
            account.storageRoot = storageRoot;
        }
        accounts.emplace_back(makeAddress(i), account);
    }
    auto const stateRoot = seedStateTrieFlushed(storage, accounts);

    bcos::Address const addr = makeAddress(3);
    std::vector<bcos::h256> const slots{slot, makeHash(0x02)};
    auto first = bcos::task::syncWait(generateProof(storage, stateRoot, addr, slots));
    auto second = bcos::task::syncWait(generateProof(storage, stateRoot, addr, slots));
    BOOST_REQUIRE(std::holds_alternative<EIP1186Proof>(first));
    BOOST_REQUIRE(std::holds_alternative<EIP1186Proof>(second));
    auto const& lhs = std::get<EIP1186Proof>(first);
    auto const& rhs = std::get<EIP1186Proof>(second);

    BOOST_CHECK(lhs.accountProof == rhs.accountProof);
    BOOST_REQUIRE_EQUAL(lhs.storageProof.size(), rhs.storageProof.size());
    for (size_t i = 0; i < lhs.storageProof.size(); ++i)
    {
        BOOST_CHECK(lhs.storageProof.at(i).key == rhs.storageProof.at(i).key);
        BOOST_CHECK(lhs.storageProof.at(i).value == rhs.storageProof.at(i).value);
        BOOST_CHECK(lhs.storageProof.at(i).proof == rhs.storageProof.at(i).proof);
    }
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::ledger::mpt::test
