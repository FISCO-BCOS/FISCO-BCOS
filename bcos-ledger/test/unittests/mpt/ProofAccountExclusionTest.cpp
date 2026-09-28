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
 * @file ProofAccountExclusionTest.cpp
 * @brief EIP-1186 exclusion proofs for accounts absent from a complete state trie (the
 *        kona-host fault-proof host asks for them whenever a block creates an account).
 */

#include "TestHelpers.h"
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-ledger/mpt/Account.h>
#include <bcos-ledger/mpt/Constants.h>
#include <bcos-ledger/mpt/HashBuilder.h>
#include <bcos-ledger/mpt/MPTReadView.h>
#include <bcos-ledger/mpt/Proof.h>
#include <bcos-task/Wait.h>
#include <boost/test/unit_test.hpp>
#include <span>
#include <variant>

namespace bcos::ledger::mpt::test
{
using ExclusionMemStorage = bcos::storage2::memory_storage::MemoryStorage<bcos::h256, bcos::bytes>;

struct AccountExclusionFixture
{
    AccountExclusionFixture()
    {
        Account account;
        account.nonce = 3;
        account.balance = 77;
        // Two accounts, so the absent address dead-ends below a branch, not at the root.
        stateRoot = seedStateTrieFlushed(
            storage, {{makeAddress(0xab), account}, {makeAddress(0x12), account}});
    }

    EIP1186Proof prove(
        bcos::Address const& address, bcos::h256 root, std::span<bcos::h256 const> slots = {})
    {
        auto result = bcos::task::syncWait(generateProof(storage, root, address, slots));
        BOOST_REQUIRE(std::holds_alternative<EIP1186Proof>(result));
        return std::get<EIP1186Proof>(std::move(result));
    }

    ExclusionMemStorage storage;
    bcos::h256 stateRoot;
};

BOOST_FIXTURE_TEST_SUITE(ProofAccountExclusionTest, AccountExclusionFixture)

// The previously refused case (AccountNotInMPT under a complete trie) is now an exclusion proof:
// the empty account, and the dead-end walk as accountProof, which the verifier accepts.
BOOST_AUTO_TEST_CASE(AbsentAccountProvesEmptyAccount)
{
    bcos::h256 const slot(5U);
    auto const proof = prove(makeAddress(0xcd), stateRoot, std::span<bcos::h256 const>(&slot, 1));
    BOOST_CHECK_EQUAL(proof.nonce, 0);
    BOOST_CHECK_EQUAL(proof.balance, 0);
    BOOST_CHECK(proof.codeHash == emptyCodeHash());
    BOOST_CHECK(proof.storageHash == emptyRootHash());
    BOOST_CHECK(!proof.accountProof.empty());
    BOOST_REQUIRE_EQUAL(proof.storageProof.size(), 1U);
    BOOST_CHECK(proof.storageProof[0].value.empty() && proof.storageProof[0].proof.empty());

    auto const verified = verifyProof(stateRoot, proof);
    BOOST_CHECK(verified.accountValid);
    BOOST_CHECK(verified.storageValid.at(0));
}

// The verifier still binds the claim to the proof: an exclusion cannot prove a non-empty account,
// and it is anchored to its own root.
BOOST_AUTO_TEST_CASE(ExclusionRejectsTamperedClaims)
{
    auto const proof = prove(makeAddress(0xcd), stateRoot);
    auto tampered = proof;
    tampered.balance = 1;
    BOOST_CHECK(!verifyProof(stateRoot, tampered).accountValid);
    BOOST_CHECK(!verifyProof(emptyRootHash(), proof).accountValid);
    // The empty-proof shortcut belongs to the empty root only; truncated and padded exclusion
    // chains are rejected like inclusion chains.
    auto emptied = proof;
    emptied.accountProof.clear();
    BOOST_CHECK(!verifyProof(stateRoot, emptied).accountValid);
    BOOST_REQUIRE(!proof.accountProof.empty());
    auto truncated = proof;
    truncated.accountProof.pop_back();
    BOOST_CHECK(!verifyProof(stateRoot, truncated).accountValid);
    auto padded = proof;
    padded.accountProof.push_back(proof.accountProof.front());
    BOOST_CHECK(!verifyProof(stateRoot, padded).accountValid);
    // An exclusion walk for one address is not an exclusion proof for another.
    auto other = proof;
    other.address = makeAddress(0xab);
    BOOST_CHECK(!verifyProof(stateRoot, other).accountValid);
}

// The empty state root: no nodes, an empty accountProof, still the empty account.
BOOST_AUTO_TEST_CASE(EmptyStateRootProvesEmptyAccount)
{
    auto const proof = prove(makeAddress(0xab), emptyRootHash());
    BOOST_CHECK(proof.accountProof.empty());
    BOOST_CHECK(verifyProof(emptyRootHash(), proof).accountValid);
}

// A present account keeps its inclusion proof, and scenario A keeps refusing absent accounts.
BOOST_AUTO_TEST_CASE(PresentAccountAndScenarioAUnchanged)
{
    auto const present = prove(makeAddress(0xab), stateRoot);
    BOOST_CHECK_EQUAL(present.nonce, 3);
    BOOST_CHECK_EQUAL(present.balance, 77);
    BOOST_CHECK(verifyProof(stateRoot, present).accountValid);

    auto dormant = bcos::task::syncWait(generateProof(
        storage, stateRoot, makeAddress(0xcd), std::span<bcos::h256 const>{}, /*fullTrie=*/false));
    BOOST_REQUIRE(std::holds_alternative<ProofErrorCode>(dormant));
    BOOST_CHECK(std::get<ProofErrorCode>(dormant) == ProofErrorCode::AccountNotInMPT);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ledger::mpt::test
