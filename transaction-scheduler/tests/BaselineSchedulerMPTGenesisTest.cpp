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
 * @file BaselineSchedulerMPTGenesisTest.cpp
 * @brief Scenario-B (L2) genesis, end to end over the real stack (M6.5): the real Ledger
 *        genesis on real RocksDB pins the op-geth-compatible alloc root into the genesis
 *        header and persists the genesis trie nodes (cc51df624); block 1 then builds the
 *        MPT INCREMENTALLY on top of that root through the real BaselineScheduler — the
 *        first full-stack verification that the persisted genesis nodes actually feed the
 *        block-1 build. Scenario-B semantics take priority over scenario A's activation
 *        rule (l2 flag checked first in shouldBuildMPT).
 */
#include "FullChainFixture.h"
// Test-asset helper shared with the bcos-ledger genesis tests: computes the
// feature_flags Entry slot Ledger's genesis verification requires in the
// SystemConfig predeploy alloc (quoted include resolves relative to this file).
#include "../../bcos-ledger/test/unittests/ledger/GenesisFeatureFlagsHelper.h"
#include "bcos-ledger/mpt/Constants.h"
#include "bcos-ledger/mpt/HashBuilder.h"
#include <bcos-codec/rlp/RLPEncode.h>
#include <boost/algorithm/hex.hpp>

#include <boost/test/unit_test.hpp>
namespace
{
using namespace bcos;
using namespace bcos::test::fullchain;
namespace mpt = bcos::ledger::mpt;

constexpr std::string_view c_genContractAddress = "43000000000000000000000000000000000000c0";
constexpr std::string_view c_genContractCode = "6080604052";
constexpr std::string_view c_genEoaAddress = "1100000000000000000000000000000000000011";

std::string genSlotHex(char lastNibble)
{
    return std::string(63, '0') + lastNibble;
}

ledger::Alloc genContractAlloc()
{
    return ledger::Alloc{.address = std::string(c_genContractAddress),
        .balance = u256(500),
        .nonce = "1",
        .code = std::string(c_genContractCode),
        .storage = {{genSlotHex('0'), std::string(60, '0') + "0385"},
            {genSlotHex('2'), std::string(62, '0') + "77"}}};
}

ledger::Alloc genEoaAlloc()
{
    return ledger::Alloc{.address = std::string(c_genEoaAddress),
        .balance = u256(1000),
        .nonce = "0",
        .code = "",
        .storage = {}};
}

ledger::GenesisConfig genL2Genesis()
{
    auto genesis = FullChainFixture::baseGenesis();
    genesis.m_features.push_back(
        ledger::FeatureSet{ledger::Features::Flag::feature_l2_ethereum_compat, 1});
    genesis.m_allocs.push_back(genContractAlloc());
    genesis.m_allocs.push_back(genEoaAlloc());
    // The contract sits at the SystemConfig predeploy address: Ledger verifies
    // the feature_flags Entry slot is IN the alloc (P0: the state root commits
    // it), so the fixture must carry it like real build-allocs.py output.
    bcos::test::appendGenesisFeatureFlagsSlot(genesis);
    return genesis;
}

h256 genKeccak(bytes const& data)
{
    return crypto::keccak256Hash(bcos::ref(data));
}

Address genAddress(std::string_view hex)
{
    Address address;
    boost::algorithm::unhex(hex.begin(), hex.end(), address.data());
    return address;
}

/// The contract's storage-trie root over the genesis allocs plus @p extraSlots
/// (slot32 -> raw 32-byte value), via the independent computeTrieRoot core.
h256 genContractStorageRoot(std::map<h256, bytes> const& extraSlots = {})
{
    std::map<h256, bytes> entries;
    // Iterate the ACTUAL genesis alloc storage (includes the feature_flags
    // Entry slot appended in genL2Genesis), not a bare genContractAlloc() —
    // the oracle must cover exactly what genesis committed.
    auto const genesisForOracle = genL2Genesis();
    for (auto const& [slotHexKey, valueHex] : genesisForOracle.m_allocs[0].storage)
    {
        auto slotBytes = bcos::fromHex(slotHexKey);
        auto valueBytes = bcos::fromHex(valueHex);
        entries[genKeccak(slotBytes)] = mpt::encodeStorageValue(bcos::ref(valueBytes));
    }
    for (auto const& [slot, rawValue] : extraSlots)
    {
        auto slotBytes = bytes(slot.begin(), slot.end());
        entries[genKeccak(slotBytes)] =
            mpt::encodeStorageValue(bytesConstRef(rawValue.data(), rawValue.size()));
    }
    return mpt::computeTrieRoot(entries).root;
}

/// From-scratch state-trie oracle over explicit Ethereum account leaves:
/// (address, nonce, balance, storageRoot, codeHash).
struct GenAccountLeaf
{
    std::string_view addressHex;
    uint64_t nonce{};
    u256 balance;
    h256 storageRoot;
    h256 codeHash;
};

h256 genStateRootOracle(std::vector<GenAccountLeaf> const& leaves)
{
    std::map<h256, bytes> accountEntries;
    for (auto const& leaf : leaves)
    {
        bytes rlp;
        codec::rlp::encode(rlp, leaf.nonce, leaf.balance, leaf.storageRoot, leaf.codeHash);
        auto addrBytes = bcos::fromHex(std::string(leaf.addressHex));
        accountEntries[genKeccak(addrBytes)] = std::move(rlp);
    }
    return mpt::computeTrieRoot(accountEntries).root;
}

h256 genContractCodeHash()
{
    return genKeccak(bcos::fromHex(std::string(c_genContractCode)));
}

BOOST_AUTO_TEST_SUITE(BaselineSchedulerMPTGenesisSuite)

// The genesis header's stateRoot equals the op-geth-compatible root over the allocs,
// computed here by an independent from-scratch oracle (secure tries, RLP account leaves) —
// and the trie nodes behind it are committed "/mpt/" rows in the RocksDB backend.
BOOST_AUTO_TEST_CASE(GenesisRootMatchesOpGethOracle)
{
    FullChainFixture fixture{"gen_root_oracle"};
    fixture.buildGenesis(genL2Genesis());

    auto expectedRoot = genStateRootOracle({
        {c_genContractAddress, 1, u256(500), genContractStorageRoot(), genContractCodeHash()},
        {c_genEoaAddress, 0, u256(1000), mpt::emptyRootHash(), mpt::emptyCodeHash()},
    });

    auto genesisHeader = fixture.headerOnChain(0);
    BOOST_CHECK_EQUAL(genesisHeader->stateRoot().hex(), expectedRoot.hex());
    BOOST_CHECK_NE(genesisHeader->stateRoot(), mpt::emptyRootHash());
    BOOST_CHECK_GT(fixture.backendNodeCount(), 0);
}

// Block 1 builds INCREMENTALLY on the genesis root through the real scheduler: the build
// reads the persisted genesis nodes (account leaf + storage sub-trie) to merge a balance
// update and a NEW storage slot into the existing tries, and the result matches the
// from-scratch oracle over the expected post-block state. Block 2 keeps chaining. This is
// the full-stack closure of the cc51df624 genesis-node persistence.
BOOST_AUTO_TEST_CASE(BlockOneIncrementalBuildOverGenesisRoot)
{
    FullChainFixture fixture{"gen_block1_incremental"};
    fixture.buildGenesis(genL2Genesis());
    auto genesisRoot = fixture.headerOnChain(0)->stateRoot();
    auto genesisNodeCount = fixture.backendNodeCount();
    BOOST_REQUIRE_GT(genesisNodeCount, 0);

    auto const contract = genAddress(c_genContractAddress);
    auto const eoa = genAddress(c_genEoaAddress);
    h256 newSlot{};
    newSlot.data()[31] = 0x05;
    h256 newValue{};
    newValue.data()[31] = 0x99;

    // Block 1: subsequent-touch of the contract — balance update + one NEW slot. The merge
    // must resolve the genesis account leaf AND the genesis storage sub-trie nodes through
    // the persisted "/mpt/" rows; a missing node would abort the execute loudly.
    fixture.planBlock(1, {FullChainFixture::balanceRow(contract, "777"),
                             FullChainFixture::slotRow(contract, newSlot, newValue)});
    auto header1 = fixture.runBlock(1);

    auto expectedRoot1 = genStateRootOracle({
        {c_genContractAddress, 1, u256(777),
            genContractStorageRoot({{newSlot, bytes(newValue.begin(), newValue.end())}}),
            genContractCodeHash()},
        {c_genEoaAddress, 0, u256(1000), mpt::emptyRootHash(), mpt::emptyCodeHash()},
    });
    BOOST_CHECK_EQUAL(header1->stateRoot().hex(), expectedRoot1.hex());
    BOOST_CHECK_NE(header1->stateRoot(), genesisRoot);
    BOOST_CHECK_GT(fixture.backendNodeCount(), genesisNodeCount);

    // Block 2: the chain keeps extending incrementally (parent root from block 1's header).
    fixture.planBlock(2, {FullChainFixture::balanceRow(eoa, "2000")});
    auto header2 = fixture.runBlock(2);
    auto expectedRoot2 = genStateRootOracle({
        {c_genContractAddress, 1, u256(777),
            genContractStorageRoot({{newSlot, bytes(newValue.begin(), newValue.end())}}),
            genContractCodeHash()},
        {c_genEoaAddress, 0, u256(2000), mpt::emptyRootHash(), mpt::emptyCodeHash()},
    });
    BOOST_CHECK_EQUAL(header2->stateRoot().hex(), expectedRoot2.hex());
    BOOST_CHECK_EQUAL(fixture.headerOnChain(2)->stateRoot(), header2->stateRoot());
}

// Scenario-B priority over scenario A: even with a feature_mpt_state_root activation row
// pointing far in the future (block 100), the l2 flag — checked FIRST in shouldBuildMPT —
// keeps every block on the MPT path. Block 1 commits an MPT root, not an XOR root.
BOOST_AUTO_TEST_CASE(L2PriorityOverScenarioAActivation)
{
    FullChainFixture fixture{"gen_l2_priority"};
    fixture.buildGenesis(genL2Genesis());
    fixture.enableFeatureFromBlock("feature_mpt_state_root", 100);

    auto const eoa = genAddress(c_genEoaAddress);
    std::vector<FCRowOp> rows{FullChainFixture::balanceRow(eoa, "4242")};
    fixture.planBlock(1, rows);
    auto header1 = fixture.runBlock(1);

    // At block 1 the scenario-A flag is NOT yet visible (enableNumber 100)…
    BOOST_CHECK(!fixture.featuresAt(1).get(ledger::Features::Flag::feature_mpt_state_root));
    // …but the block still commits the MPT root (and NOT the XOR fold of its delta).
    auto expectedRoot = genStateRootOracle({
        {c_genContractAddress, 1, u256(500), genContractStorageRoot(), genContractCodeHash()},
        {c_genEoaAddress, 0, u256(4242), mpt::emptyRootHash(), mpt::emptyCodeHash()},
    });
    BOOST_CHECK_EQUAL(header1->stateRoot().hex(), expectedRoot.hex());
    BOOST_CHECK_NE(header1->stateRoot(), fixture.xorOracle(rows, fixture.featuresAt(1)));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace
