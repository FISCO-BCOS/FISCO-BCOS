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
 * @file ProofVerifyTest.cpp
 * @brief Cross-validation of generateProof against the independent verifyProof
 *        (spec §5.9, §7.1)
 */

#include "TestHelpers.h"
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-ledger/mpt/Account.h>
#include <bcos-ledger/mpt/Constants.h>
#include <bcos-ledger/mpt/HashBuilder.h>
#include <bcos-ledger/mpt/MPTReadView.h>
#include <bcos-ledger/mpt/Proof.h>
#include <bcos-ledger/mpt/StorageValueCodec.h>
#include <bcos-task/Task.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <boost/test/unit_test.hpp>
#include <map>
#include <span>
#include <utility>
#include <variant>
#include <vector>

namespace bcos::ledger::mpt::test
{

// Helper names carry a Verify prefix/suffix: the unity build can merge this file and
// ProofGenerateTest.cpp into one TU, fusing their unnamed namespaces.
using VerifyMemStorage = bcos::storage2::memory_storage::MemoryStorage<bcos::h256, bcos::bytes>;

namespace
{

/// Build a state trie holding @p accounts into @p storage; returns the state root.
bcos::h256 buildVerifyStateTrie(
    VerifyMemStorage& storage, std::vector<std::pair<bcos::Address, Account>> const& accounts)
{
    std::map<bcos::h256, bcos::bytes> entries;
    for (auto const& [addr, account] : accounts)
    {
        entries[accountKeyHash(addr)] = account.encode();
    }
    return seedTrieFlushed(storage, emptyRootHash(), entries).root;
}

/// A generated proof plus the state root it was generated against.
struct ProofSetup
{
    bcos::h256 stateRoot;
    EIP1186Proof proof;
};

/// One account (nonce 7, balance 4242) whose storage trie holds @p slotValues; generates the
/// EIP-1186 proof for the account and @p requestSlots.
ProofSetup makeAccountWithSlots(std::vector<std::pair<bcos::h256, bcos::bytes>> const& slotValues,
    std::vector<bcos::h256> const& requestSlots)
{
    VerifyMemStorage storage;
    bcos::Address const addr = makeAddress(0xab);
    Account account;
    account.nonce = 7;
    account.balance = 4242;
    if (!slotValues.empty())
    {
        std::map<bcos::h256, bcos::bytes> slotEntries;
        for (auto const& [slot, value] : slotValues)
        {
            slotEntries[slotKeyHash(slot)] = value;
        }
        account.storageRoot = seedTrieFlushed(storage, emptyRootHash(), slotEntries).root;
    }
    auto const stateRoot = buildVerifyStateTrie(storage, {{addr, account}});

    auto result = bcos::task::syncWait(
        generateProof(storage, stateRoot, addr, std::span<bcos::h256 const>(requestSlots)));
    BOOST_REQUIRE(std::holds_alternative<EIP1186Proof>(result));
    return ProofSetup{stateRoot, std::get<EIP1186Proof>(std::move(result))};
}

}  // namespace

BOOST_AUTO_TEST_SUITE(ProofVerifySuite)

// The generate → verify round trip through two independent implementations: verifyProof reads
// only the proof byte arrays (never storage) and must accept what generateProof emitted.
BOOST_AUTO_TEST_CASE(GeneratedProofPassesIndependentVerifier)
{
    bcos::h256 const slot = makeHash(0x01);
    auto const setup = makeAccountWithSlots({{slot, bcos::bytes{0x2a}}}, {slot});

    auto const res = verifyProof(setup.stateRoot, setup.proof);
    BOOST_CHECK(res.accountValid);
    BOOST_CHECK_EQUAL(res.recoveredNonce, setup.proof.nonce);
    BOOST_CHECK_EQUAL(res.recoveredBalance, setup.proof.balance);
    BOOST_CHECK_EQUAL(res.recoveredCodeHash, setup.proof.codeHash);
    BOOST_CHECK_EQUAL(res.recoveredStorageRoot, setup.proof.storageHash);
    BOOST_REQUIRE_EQUAL(res.storageValid.size(), 1);
    BOOST_CHECK(res.storageValid.at(0));
    BOOST_REQUIRE_EQUAL(res.recoveredStorageValues.size(), 1);
    BOOST_CHECK(res.recoveredStorageValues.at(0) == setup.proof.storageProof.at(0).value);
}

// One flipped byte anywhere in a proof node breaks its hash link. Account-side tampering kills
// accountValid; storage-side tampering kills only that slot while the account side stays valid.
BOOST_AUTO_TEST_CASE(TamperedProofFailsVerification)
{
    bcos::h256 const slot = makeHash(0x01);
    auto const setup = makeAccountWithSlots({{slot, bcos::bytes{0x2a}}}, {slot});

    {
        auto tampered = setup.proof;
        BOOST_REQUIRE(!tampered.accountProof.empty());
        tampered.accountProof.at(0).at(5) ^= 0x01;
        auto const res = verifyProof(setup.stateRoot, tampered);
        BOOST_CHECK(!res.accountValid);
    }
    {
        auto tampered = setup.proof;
        BOOST_REQUIRE(!tampered.storageProof.at(0).proof.empty());
        tampered.storageProof.at(0).proof.at(0).at(5) ^= 0x01;
        auto const res = verifyProof(setup.stateRoot, tampered);
        BOOST_CHECK(res.accountValid);
        BOOST_REQUIRE_EQUAL(res.storageValid.size(), 1);
        BOOST_CHECK(!res.storageValid.at(0));
    }
}

// An untampered proof presented against a different root cannot anchor its first node.
BOOST_AUTO_TEST_CASE(WrongStateRootFailsVerification)
{
    auto const setup = makeAccountWithSlots({}, {});
    auto const res = verifyProof(makeHash(0x99), setup.proof);
    BOOST_CHECK(!res.accountValid);
}

// Proof bytes intact, but the claimed top-level field disagrees with the account leaf the chain
// actually proves: the field-consistency check must reject it.
BOOST_AUTO_TEST_CASE(TamperedFieldFailsVerification)
{
    auto const setup = makeAccountWithSlots({}, {});
    auto tampered = setup.proof;
    tampered.balance += 1;
    auto const res = verifyProof(setup.stateRoot, tampered);
    BOOST_CHECK(!res.accountValid);

    // Sanity: the unmodified proof passes.
    BOOST_CHECK(verifyProof(setup.stateRoot, setup.proof).accountValid);
}

// Ten present slots all verify with non-empty recovered values; a requested ABSENT slot yields a
// verifying exclusion (storageValid true, recovered value empty).
BOOST_AUTO_TEST_CASE(MultiSlotProofAllVerify)
{
    std::vector<std::pair<bcos::h256, bcos::bytes>> slotValues;
    std::vector<bcos::h256> request;
    for (uint8_t i = 1; i <= 10; ++i)
    {
        // Single bytes < 0x80: each is its own one-byte RLP encoding, like the stored leaf form.
        slotValues.emplace_back(makeHash(i), bcos::bytes{static_cast<bcos::byte>(0x20 + i)});
        request.push_back(makeHash(i));
    }
    bcos::h256 const absentSlot = makeHash(0x77);
    request.push_back(absentSlot);

    auto const setup = makeAccountWithSlots(slotValues, request);
    auto const res = verifyProof(setup.stateRoot, setup.proof);
    BOOST_CHECK(res.accountValid);
    BOOST_REQUIRE_EQUAL(res.storageValid.size(), 11);
    BOOST_REQUIRE_EQUAL(res.recoveredStorageValues.size(), 11);
    for (size_t i = 0; i < 10; ++i)
    {
        BOOST_CHECK(res.storageValid.at(i));
        BOOST_CHECK(!res.recoveredStorageValues.at(i).empty());
        BOOST_CHECK(res.recoveredStorageValues.at(i) == setup.proof.storageProof.at(i).value);
    }
    BOOST_CHECK(res.storageValid.at(10));
    BOOST_CHECK(res.recoveredStorageValues.at(10).empty());
}

// A slot requested on an account whose storage trie is EMPTY: generateProof emits empty value +
// empty proof, and the verifier accepts exactly that shape (and only that shape).
BOOST_AUTO_TEST_CASE(EmptyStorageTrieSlotVerifies)
{
    auto const setup = makeAccountWithSlots({}, {makeHash(0x05)});
    BOOST_REQUIRE_EQUAL(setup.proof.storageProof.size(), 1);
    BOOST_CHECK(setup.proof.storageProof.at(0).value.empty());
    BOOST_CHECK(setup.proof.storageProof.at(0).proof.empty());

    auto const res = verifyProof(setup.stateRoot, setup.proof);
    BOOST_CHECK(res.accountValid);
    BOOST_REQUIRE_EQUAL(res.storageValid.size(), 1);
    BOOST_CHECK(res.storageValid.at(0));
    BOOST_CHECK(res.recoveredStorageValues.at(0).empty());

    // A claimed value against the empty storage root is unprovable and must be rejected.
    auto tampered = setup.proof;
    tampered.storageProof.at(0).value = bcos::bytes{0x2a};
    BOOST_CHECK(!verifyProof(setup.stateRoot, tampered).storageValid.at(0));
}

// A dormant (never-written) account has no leaf to prove: the boundary is documented at the
// generate level — generateProof refuses with AccountNotInMPT, so no proof reaches the verifier.
BOOST_AUTO_TEST_CASE(ExclusionAtGenerateLevel)
{
    VerifyMemStorage storage;
    Account account;
    account.nonce = 1;
    auto const stateRoot = buildVerifyStateTrie(storage, {{makeAddress(0xab), account}});

    auto missing = bcos::task::syncWait(
        generateProof(storage, stateRoot, makeAddress(0xcd), std::span<bcos::h256 const>{}));
    BOOST_REQUIRE(std::holds_alternative<ProofErrorCode>(missing));
    BOOST_CHECK(std::get<ProofErrorCode>(missing) == ProofErrorCode::AccountNotInMPT);
}

// Extra trailing nodes after the walk concluded must be rejected: a padded proof is not the
// minimal chain the root committed to.
BOOST_AUTO_TEST_CASE(PaddedProofRejected)
{
    auto const setup = makeAccountWithSlots({}, {});
    BOOST_CHECK(verifyProof(setup.stateRoot, setup.proof).accountValid);

    auto padded = setup.proof;
    padded.accountProof.push_back(bcos::bytes{0x01, 0x02, 0x03});
    BOOST_CHECK(!verifyProof(setup.stateRoot, padded).accountValid);

    // Padding with a copy of a legitimate node is no better than junk.
    auto duplicated = setup.proof;
    duplicated.accountProof.push_back(duplicated.accountProof.front());
    BOOST_CHECK(!verifyProof(setup.stateRoot, duplicated).accountValid);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::ledger::mpt::test
