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
 * @file FullChainFixtureSmokeTest.cpp
 * @brief Self-checks for FullChainFixture (M6.4a): the real-RocksDB + real-Ledger harness
 *        itself runs a chain — before BaselineSchedulerMPTL1UpgradeTest /
 *        BaselineSchedulerMPTGenesisTest / EthGetProofIntegrationTest build on it.
 */
#include "FullChainFixture.h"

#include <boost/test/unit_test.hpp>
namespace
{
using namespace bcos;
using namespace bcos::test::fullchain;

BOOST_AUTO_TEST_SUITE(FullChainFixtureSmokeSuite)

// A plain (no MPT flags) chain: genesis over the real Ledger, one block through the real
// BaselineScheduler execute/commit path. The stateRoot is the legacy XOR root (pinned
// against the independent oracle), no "/mpt/" row reaches the RocksDB backend, and the
// committed header reads back through the real Ledger with the same root.
BOOST_AUTO_TEST_CASE(XorChainOneBlockRoundTrip)
{
    FullChainFixture fixture{"smoke_xor_one_block"};
    fixture.buildGenesis(FullChainFixture::baseGenesis());

    auto const address = FullChainFixture::makeAddress(0xAA);
    std::vector<FCRowOp> rows{
        FullChainFixture::balanceRow(address, "1000"), FullChainFixture::nonceRow(address, "1")};
    fixture.planBlock(1, rows);

    auto header = fixture.runBlock(1);
    BOOST_CHECK_NE(header->stateRoot(), h256{});
    BOOST_CHECK_EQUAL(header->stateRoot(), fixture.xorOracle(rows, fixture.featuresAt(1)));
    BOOST_CHECK_EQUAL(fixture.backendNodeCount(), 0);

    auto onChain = fixture.headerOnChain(1);
    BOOST_CHECK_EQUAL(onChain->number(), 1);
    BOOST_CHECK_EQUAL(onChain->stateRoot(), header->stateRoot());
}

// The fixture's L2 genesis path really reaches RocksDB: an alloc-carrying genesis with
// feature_l2_ethereum_compat persists a non-zero op-geth-compatible stateRoot in the genesis
// header AND its trie nodes as committed "/mpt/" rows (cc51df624), readable back through the
// storage2 backend — the wiring PR-20's scenario-B suite depends on.
BOOST_AUTO_TEST_CASE(L2GenesisNodesReachBackend)
{
    FullChainFixture fixture{"smoke_l2_genesis"};
    auto genesis = FullChainFixture::baseGenesis();
    genesis.m_features.push_back(
        ledger::FeatureSet{ledger::Features::Flag::feature_l2_ethereum_compat, 1});
    genesis.m_allocs.push_back(ledger::Alloc{.address = "1100000000000000000000000000000000000011",
        .balance = u256(1000),
        .nonce = "0",
        .code = "",
        .storage = {}});
    fixture.buildGenesis(genesis);

    auto genesisHeader = fixture.headerOnChain(0);
    BOOST_CHECK_NE(genesisHeader->stateRoot(), h256{});
    BOOST_CHECK_GT(fixture.backendNodeCount(), 0);

    // The genesis root's own node row is present and hashes back to the root.
    auto rootNode = fixture.backendNode(genesisHeader->stateRoot());
    BOOST_REQUIRE(rootNode.has_value());
    BOOST_CHECK_EQUAL(
        crypto::keccak256Hash(bcos::ref(*rootNode)).hex(), genesisHeader->stateRoot().hex());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace
