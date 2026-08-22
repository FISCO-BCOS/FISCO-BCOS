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
 * @file BaselineSchedulerMPTL1UpgradeTest.cpp
 * @brief Scenario-A activation transition, end to end over the real stack (M6.5): a chain
 *        genesis'd by the real Ledger on real RocksDB activates feature_mpt_state_root
 *        mid-chain through a real SYS_CONFIG {value, enableNumber} row — no feature
 *        injection hook anywhere. Blocks up to and INCLUDING the activation block N stay on
 *        the legacy XOR root; block N+1 starts the MPT from the EMPTY trie (the activation
 *        boundary — pre-activation accounts stay dormant); N+2 extends it incrementally via
 *        the parent header. First-touch of a dormant account commits its touched rows only:
 *        account metadata is read from flat state, but slots written before activation stay
 *        outside the storage trie (spec design3 — the 2026-07-09 revision removed the
 *        first-touch prefix-scan bootstrap deliberately).
 */
#include "FullChainFixture.h"
#include "bcos-ledger/mpt/Account.h"

#include <boost/test/unit_test.hpp>
namespace
{
using namespace bcos;
using namespace bcos::test::fullchain;
namespace mpt = bcos::ledger::mpt;

constexpr std::string_view c_mptFlagName = "feature_mpt_state_root";

h256 l1uSlot(uint8_t lastByte)
{
    h256 slot{};
    slot.data()[31] = lastByte;
    return slot;
}

h256 l1uValue(uint8_t lastByte)
{
    h256 value{};
    value.data()[31] = lastByte;
    return value;
}

BOOST_AUTO_TEST_SUITE(BaselineSchedulerMPTL1UpgradeSuite)

// Activation at N=3. Blocks 1..2 (pre-activation) and block 3 (the activation block itself,
// strictly-greater rule) commit XOR roots and write zero "/mpt/" rows. Block 4 commits an
// MPT root that equals a from-scratch build over ONLY the accounts written at block 4 —
// proving the parent root was emptyRootHash() and pre-activation accounts stayed out. Block
// 5 extends the trie incrementally (parent root from block 4's committed header).
BOOST_AUTO_TEST_CASE(ActivationBlockKeepsXorNextBlockStartsMPT)
{
    FullChainFixture fixture{"l1u_transition"};
    fixture.buildGenesis(FullChainFixture::baseGenesis());
    fixture.enableFeatureFromBlock(c_mptFlagName, 3);

    auto const addressA = FullChainFixture::makeAddress(0xAA);  // XOR-era only
    auto const addressB = FullChainFixture::makeAddress(0xBB);  // first MPT block
    auto const addressC = FullChainFixture::makeAddress(0xCC);  // second MPT block

    std::vector<FCRowOp> rows1{
        FullChainFixture::balanceRow(addressA, "1000"), FullChainFixture::nonceRow(addressA, "1")};
    std::vector<FCRowOp> rows2{FullChainFixture::balanceRow(addressA, "2000")};
    std::vector<FCRowOp> rows3{FullChainFixture::nonceRow(addressA, "2")};
    std::vector<FCRowOp> rows4{
        FullChainFixture::balanceRow(addressB, "7"), FullChainFixture::nonceRow(addressB, "3")};
    std::vector<FCRowOp> rows5{FullChainFixture::balanceRow(addressC, "13")};
    fixture.planBlock(1, rows1);
    fixture.planBlock(2, rows2);
    fixture.planBlock(3, rows3);
    fixture.planBlock(4, rows4);
    fixture.planBlock(5, rows5);

    // Blocks 1-2: the flag row's enableNumber=3 is invisible below 3 — pure XOR era.
    auto header1 = fixture.runBlock(1);
    BOOST_CHECK(!fixture.featuresAt(1).get(ledger::Features::Flag::feature_mpt_state_root));
    BOOST_CHECK_EQUAL(header1->stateRoot(), fixture.xorOracle(rows1, fixture.featuresAt(1)));
    auto header2 = fixture.runBlock(2);
    BOOST_CHECK_EQUAL(header2->stateRoot(), fixture.xorOracle(rows2, fixture.featuresAt(2)));
    BOOST_CHECK_EQUAL(fixture.backendNodeCount(), 0);

    // Block 3 = activation block N: the flag IS active (activationBlockOf == 3), but the
    // strictly-greater rule keeps this block on XOR; still no "/mpt/" row anywhere.
    auto features3 = fixture.featuresAt(3);
    BOOST_REQUIRE(features3.get(ledger::Features::Flag::feature_mpt_state_root));
    BOOST_CHECK_EQUAL(
        features3.activationBlockOf(ledger::Features::Flag::feature_mpt_state_root), 3);
    auto header3 = fixture.runBlock(3);
    BOOST_CHECK_EQUAL(header3->stateRoot(), fixture.xorOracle(rows3, features3));
    BOOST_CHECK_EQUAL(fixture.backendNodeCount(), 0);

    // Block 4 = N+1: first MPT root. Equals the from-scratch oracle over {B} ALONE — the
    // parent root was the empty trie and XOR-era account A did not enter.
    auto header4 = fixture.runBlock(4);
    mpt::Account accountB;
    accountB.balance = 7;
    accountB.nonce = 3;
    BOOST_CHECK_EQUAL(header4->stateRoot(), FullChainFixture::mptOracle({{addressB, accountB}}));
    BOOST_CHECK_GT(fixture.backendNodeCount(), 0);
    // Discriminating control: a trie that DID absorb the pre-activation account would have a
    // different root.
    mpt::Account accountA;
    accountA.balance = 2000;
    accountA.nonce = 2;
    BOOST_CHECK_NE(header4->stateRoot(),
        FullChainFixture::mptOracle({{addressA, accountA}, {addressB, accountB}}));

    // Block 5 = N+2: incremental chain — parent root resolves from block 4's committed
    // header, trie now covers {B, C}.
    auto header5 = fixture.runBlock(5);
    mpt::Account accountC;
    accountC.balance = 13;
    BOOST_CHECK_EQUAL(header5->stateRoot(),
        FullChainFixture::mptOracle({{addressB, accountB}, {addressC, accountC}}));

    // The committed chain agrees with the executed headers.
    BOOST_CHECK_EQUAL(fixture.headerOnChain(3)->stateRoot(), header3->stateRoot());
    BOOST_CHECK_EQUAL(fixture.headerOnChain(5)->stateRoot(), header5->stateRoot());
}

// A dormant account (created with a storage slot BEFORE activation) is first touched after
// the transition: the touch commits the touched rows — balance from the delta, nonce
// inherited from flat state — and the storage trie covers ONLY the newly-touched slot. The
// pre-activation slot stays outside (no first-touch bootstrap scan), pinned by a
// discriminating negative oracle.
BOOST_AUTO_TEST_CASE(DormantAccountFirstTouchCommitsTouchedRowsOnly)
{
    FullChainFixture fixture{"l1u_dormant_first_touch"};
    fixture.buildGenesis(FullChainFixture::baseGenesis());
    fixture.enableFeatureFromBlock(c_mptFlagName, 2);

    auto const dormant = FullChainFixture::makeAddress(0xDD);
    auto const active = FullChainFixture::makeAddress(0xEE);
    auto const oldSlot = l1uSlot(0x01);
    auto const oldValue = l1uValue(0x2A);
    auto const newSlot = l1uSlot(0x05);
    auto const newValue = l1uValue(0x63);

    // Block 1 (XOR era): dormant account gets balance, nonce and one storage slot.
    std::vector<FCRowOp> rows1{FullChainFixture::balanceRow(dormant, "77"),
        FullChainFixture::nonceRow(dormant, "5"),
        FullChainFixture::slotRow(dormant, oldSlot, oldValue)};
    fixture.planBlock(1, rows1);
    // Block 2 (activation block, XOR): unrelated account.
    std::vector<FCRowOp> rows2{FullChainFixture::balanceRow(active, "1")};
    fixture.planBlock(2, rows2);
    // Block 3 (first MPT block): only the unrelated account — dormant stays out.
    fixture.planBlock(3, {FullChainFixture::balanceRow(active, "2")});
    // Block 4: first touch of the dormant account — balance update + one NEW slot.
    fixture.planBlock(4, {FullChainFixture::balanceRow(dormant, "88"),
                             FullChainFixture::slotRow(dormant, newSlot, newValue)});

    fixture.runBlock(1);
    fixture.runBlock(2);

    auto header3 = fixture.runBlock(3);
    mpt::Account accountActive;
    accountActive.balance = 2;
    BOOST_CHECK_EQUAL(header3->stateRoot(), FullChainFixture::mptOracle({{active, accountActive}}));

    auto header4 = fixture.runBlock(4);
    mpt::Account accountDormant;
    accountDormant.balance = 88;  // from the block-4 delta
    accountDormant.nonce = 5;     // first-touch metadata read from the flat rows of block 1
    accountDormant.storageRoot =
        FullChainFixture::storageTrieOracle({{newSlot, bytes(newValue.begin(), newValue.end())}});
    BOOST_CHECK_EQUAL(header4->stateRoot(),
        FullChainFixture::mptOracle({{active, accountActive}, {dormant, accountDormant}}));

    // Discriminating control: had the pre-activation slot been bootstrapped into the storage
    // trie, the root would differ.
    mpt::Account accountBootstrapped = accountDormant;
    accountBootstrapped.storageRoot =
        FullChainFixture::storageTrieOracle({{oldSlot, bytes(oldValue.begin(), oldValue.end())},
            {newSlot, bytes(newValue.begin(), newValue.end())}});
    BOOST_CHECK_NE(header4->stateRoot(),
        FullChainFixture::mptOracle({{active, accountActive}, {dormant, accountBootstrapped}}));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace
