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
 * @file ProofSlotNotInMPTTest.cpp
 * @brief SlotNotInMPT: honest proofs for scenario-A cold slots (spec §5.9 / §4.4)
 */

#include "TestHelpers.h"
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-ledger/mpt/Account.h>
#include <bcos-ledger/mpt/Constants.h>
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

// Helper names carry a ColdSlot prefix: the unity build can merge this file with the other
// Proof*Test.cpp files into one TU, fusing their unnamed namespaces.
using ColdSlotMemStorage = bcos::storage2::memory_storage::MemoryStorage<bcos::h256, bcos::bytes>;

namespace
{

/// One account (nonce 7, balance 4242) whose storage trie holds @p slotValues; generates the
/// EIP-1186 proof for @p requestSlots under the given trie-completeness mode.
std::pair<bcos::h256, EIP1186Proof> makeColdSlotProof(
    std::vector<std::pair<bcos::h256, bcos::bytes>> const& slotValues,
    std::vector<bcos::h256> const& requestSlots, bool fullTrie)
{
    ColdSlotMemStorage storage;
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
    auto const stateRoot =
        seedTrieFlushed(storage, emptyRootHash(), {{accountKeyHash(addr), account.encode()}}).root;

    auto result = bcos::task::syncWait(generateProof(
        storage, stateRoot, addr, std::span<bcos::h256 const>(requestSlots), fullTrie));
    BOOST_REQUIRE(std::holds_alternative<EIP1186Proof>(result));
    return {stateRoot, std::get<EIP1186Proof>(std::move(result))};
}

bcos::h256 const coldSlotHot = makeHash(0x01);
bcos::h256 const coldSlotCold = makeHash(0x03);
bcos::bytes const coldSlotHotValue{0x2a};  // RLP(42): single byte < 0x80

}  // namespace

BOOST_AUTO_TEST_SUITE(ProofSlotNotInMPTSuite)

// Scenario A (fullTrie=false): the hot slot (in the trie) proves normally; the cold slot (absent
// from the trie) is marked inMPT=false with value AND proof empty — the flat-fill contract: the
// mpt layer asserts nothing about the value, the RPC layer supplies the flat-KV truth.
BOOST_AUTO_TEST_CASE(ScenarioAColdSlotMarkedNotInMPT)
{
    auto const proof = makeColdSlotProof(
        {{coldSlotHot, coldSlotHotValue}}, {coldSlotHot, coldSlotCold}, /*fullTrie=*/false)
                           .second;

    BOOST_REQUIRE_EQUAL(proof.storageProof.size(), 2);
    auto const& hot = proof.storageProof.at(0);
    BOOST_CHECK(hot.inMPT);
    BOOST_CHECK(hot.value == coldSlotHotValue);
    BOOST_CHECK(!hot.proof.empty());

    auto const& cold = proof.storageProof.at(1);
    BOOST_CHECK(!cold.inMPT);
    BOOST_CHECK(cold.value.empty());
    BOOST_CHECK(cold.proof.empty());
}

// Scenario A with an EMPTY storage trie: every requested slot is cold — inMPT=false, empty
// value, empty proof (under fullTrie=true the same shape means "provably zero", see below).
BOOST_AUTO_TEST_CASE(ScenarioAEmptyStorageTrieAllSlotsCold)
{
    auto const proof =
        makeColdSlotProof({}, {coldSlotHot, coldSlotCold}, /*fullTrie=*/false).second;

    BOOST_REQUIRE_EQUAL(proof.storageProof.size(), 2);
    for (auto const& entry : proof.storageProof)
    {
        BOOST_CHECK(!entry.inMPT);
        BOOST_CHECK(entry.value.empty());
        BOOST_CHECK(entry.proof.empty());
    }
}

// Scenario B (fullTrie=true): behavior unchanged — an absent slot yields a non-empty exclusion
// proof with inMPT=true (the complete trie makes the exclusion a provable zero), and the
// defaulted-argument call produces the identical result.
BOOST_AUTO_TEST_CASE(ScenarioBExclusionProofUnchanged)
{
    auto const proof = makeColdSlotProof(
        {{coldSlotHot, coldSlotHotValue}}, {coldSlotHot, coldSlotCold}, /*fullTrie=*/true)
                           .second;

    BOOST_REQUIRE_EQUAL(proof.storageProof.size(), 2);
    auto const& absent = proof.storageProof.at(1);
    BOOST_CHECK(absent.inMPT);
    BOOST_CHECK(absent.value.empty());
    BOOST_CHECK(!absent.proof.empty());

    // The defaulted call is scenario B: byte-identical storageProof entries.
    ColdSlotMemStorage storage;  // re-generate via the default argument on a fresh build
    {
        Account account;
        account.nonce = 7;
        account.balance = 4242;
        std::map<bcos::h256, bcos::bytes> const slotEntries{
            {slotKeyHash(coldSlotHot), coldSlotHotValue}};
        account.storageRoot = seedTrieFlushed(storage, emptyRootHash(), slotEntries).root;
        std::map<bcos::h256, bcos::bytes> const accountEntries{
            {accountKeyHash(makeAddress(0xab)), account.encode()}};
        auto const root = seedTrieFlushed(storage, emptyRootHash(), accountEntries).root;
        std::vector<bcos::h256> const slots{coldSlotHot, coldSlotCold};
        auto result = bcos::task::syncWait(
            generateProof(storage, root, makeAddress(0xab), std::span<bcos::h256 const>(slots)));
        BOOST_REQUIRE(std::holds_alternative<EIP1186Proof>(result));
        auto const& viaDefault = std::get<EIP1186Proof>(result);
        BOOST_REQUIRE_EQUAL(viaDefault.storageProof.size(), 2);
        for (size_t i = 0; i < 2; ++i)
        {
            BOOST_CHECK(viaDefault.storageProof.at(i).inMPT == proof.storageProof.at(i).inMPT);
            BOOST_CHECK(viaDefault.storageProof.at(i).value == proof.storageProof.at(i).value);
            BOOST_CHECK(viaDefault.storageProof.at(i).proof == proof.storageProof.at(i).proof);
        }
    }
}

// The verifier treats an inMPT=false entry as UNVERIFIABLE: not accepted as proven
// (storageValid=false) and not a response-level failure (accountValid and the other slots
// still verify). The RPC layer fills the value from flat KV, so a non-empty value with an
// empty proof is the expected honest shape.
BOOST_AUTO_TEST_CASE(VerifierTreatsSlotNotInMPTAsUnverifiable)
{
    auto [stateRoot, proof] = makeColdSlotProof(
        {{coldSlotHot, coldSlotHotValue}}, {coldSlotHot, coldSlotCold}, /*fullTrie=*/false);

    // Simulate the RPC layer's flat fill: an authoritative non-zero value without a proof.
    proof.storageProof.at(1).value = bcos::bytes{0x13, 0x37};

    auto const res = verifyProof(stateRoot, proof);
    BOOST_CHECK(res.accountValid);
    BOOST_REQUIRE_EQUAL(res.storageValid.size(), 2);
    BOOST_REQUIRE_EQUAL(res.storageStatus.size(), 2);

    BOOST_CHECK(res.storageValid.at(0));
    BOOST_CHECK(res.storageStatus.at(0) == SlotProofStatus::Verified);

    BOOST_CHECK(!res.storageValid.at(1));
    BOOST_CHECK(res.storageStatus.at(1) == SlotProofStatus::Unverifiable);
    BOOST_CHECK(res.recoveredStorageValues.at(1).empty());
}

// An inMPT=false entry carrying proof bytes is malformed — whatever those bytes prove, the
// generator's contract says no proof exists for the slot. The verifier rejects it as Invalid
// (not Unverifiable), even when the smuggled bytes are a genuine exclusion chain.
BOOST_AUTO_TEST_CASE(VerifierRejectsNotInMPTWithProofBytes)
{
    // Generate the genuine exclusion chain for the cold slot under scenario B...
    auto [stateRoot, proofB] =
        makeColdSlotProof({{coldSlotHot, coldSlotHotValue}}, {coldSlotCold}, /*fullTrie=*/true);
    BOOST_REQUIRE(!proofB.storageProof.at(0).proof.empty());

    // ...then claim inMPT=false while keeping those proof bytes attached.
    proofB.storageProof.at(0).inMPT = false;

    auto const res = verifyProof(stateRoot, proofB);
    BOOST_CHECK(res.accountValid);
    BOOST_REQUIRE_EQUAL(res.storageStatus.size(), 1);
    BOOST_CHECK(!res.storageValid.at(0));
    BOOST_CHECK(res.storageStatus.at(0) == SlotProofStatus::Invalid);
}

// storageStatus mirrors storageValid for inMPT=true entries: Verified for a good chain,
// Invalid for a tampered value — the tri-state adds Unverifiable without disturbing the
// existing bool contract.
BOOST_AUTO_TEST_CASE(StatusMirrorsBoolForInMPTEntries)
{
    auto [stateRoot, proof] =
        makeColdSlotProof({{coldSlotHot, coldSlotHotValue}}, {coldSlotHot}, /*fullTrie=*/true);

    auto const good = verifyProof(stateRoot, proof);
    BOOST_CHECK(good.storageValid.at(0));
    BOOST_CHECK(good.storageStatus.at(0) == SlotProofStatus::Verified);

    proof.storageProof.at(0).value = bcos::bytes{0x2b};  // claim a different value
    auto const bad = verifyProof(stateRoot, proof);
    BOOST_CHECK(!bad.storageValid.at(0));
    BOOST_CHECK(bad.storageStatus.at(0) == SlotProofStatus::Invalid);
}

// Account-side failure short-circuits every slot to Invalid, including inMPT=false entries:
// with no established account leaf, even "unverifiable" would be too generous.
BOOST_AUTO_TEST_CASE(AccountFailureLeavesColdSlotsInvalid)
{
    auto [stateRoot, proof] = makeColdSlotProof(
        {{coldSlotHot, coldSlotHotValue}}, {coldSlotHot, coldSlotCold}, /*fullTrie=*/false);

    auto const res = verifyProof(makeHash(0x99), proof);  // wrong root: account chain fails
    BOOST_CHECK(!res.accountValid);
    BOOST_REQUIRE_EQUAL(res.storageStatus.size(), 2);
    BOOST_CHECK(res.storageStatus.at(0) == SlotProofStatus::Invalid);
    BOOST_CHECK(res.storageStatus.at(1) == SlotProofStatus::Invalid);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::ledger::mpt::test
