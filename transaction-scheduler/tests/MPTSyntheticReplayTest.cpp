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
 * @file MPTSyntheticReplayTest.cpp
 * @brief Scenario-B 100-block deterministic replay (M10.1, degraded form of spec §11 #1):
 *        an L2 chain genesis'd from allocs runs 100 blocks of seeded pseudo-random state
 *        deltas — account creation, balance/nonce updates, slot writes, slot overwrites and
 *        slot DELETIONS — and after EVERY block the incrementally-built committed stateRoot
 *        must equal an independent from-scratch trie build over the full expected state.
 *        A single divergence stops the replay at that block (REQUIRE), exactly the
 *        chain-break semantics the mainnet-replay card specified: any node-encoding / RLP /
 *        keccak-padding bug surfaces as the first mismatching block.
 *
 *        Degraded from "100 real Ethereum mainnet blocks" deliberately: replaying real
 *        mainnet blocks needs (a) per-block state diffs only an archive node's
 *        debug_traceBlock can provide (eth_getBlockByNumber carries none), and (b) a
 *        mainnet-equivalent EVM including withdrawal processing — while this fixture
 *        replaces the EVM with a delta-replaying scheduler by design. What the card is
 *        actually after — long-chain incremental-vs-from-scratch equivalence over a rich op
 *        mix — is exactly what this test pins. Real-vector fidelity against op-geth is
 *        separately pinned by test_GenesisStateRoot / BaselineSchedulerMPTGenesisTest.
 */
#include "FullChainFixture.h"
#include "bcos-ledger/mpt/Account.h"
#include <boost/algorithm/hex.hpp>
#include <boost/test/unit_test.hpp>
#include <iostream>
#include <map>
#include <random>
#include <vector>

namespace
{
using namespace bcos;
using namespace bcos::test::fullchain;
namespace mpt = bcos::ledger::mpt;

constexpr std::string_view c_replayEoa1 = "1100000000000000000000000000000000000011";
constexpr std::string_view c_replayEoa2 = "2200000000000000000000000000000000000022";
constexpr size_t c_replayBlocks = 100;

Address replayHexAddress(std::string_view hex)
{
    Address address;
    boost::algorithm::unhex(hex.begin(), hex.end(), address.data());
    return address;
}

Address replayAddress(uint32_t index)
{
    Address address{};
    address.data()[0] = static_cast<uint8_t>(index >> 24U);
    address.data()[1] = static_cast<uint8_t>(index >> 16U);
    address.data()[2] = static_cast<uint8_t>(index >> 8U);
    address.data()[3] = static_cast<uint8_t>(index);
    address.data()[19] = 0x34;  // disjoint from other suites' address spaces
    return address;
}

h256 replayH256(uint64_t value)
{
    h256 out{};
    for (size_t i = 0; i < sizeof(value); ++i)
    {
        out.data()[31 - i] = static_cast<uint8_t>(value >> (8U * i));
    }
    return out;
}

/// Test-side model of one account's expected state (EOA-shaped: no code).
struct ReplayAccount
{
    u256 balance;
    uint64_t nonce{};
    std::map<h256, h256> slots;
};

BOOST_AUTO_TEST_SUITE(MPTSyntheticReplaySuite)

BOOST_AUTO_TEST_CASE(HundredBlockReplayIncrementalMatchesFromScratchOracle)
{
    FullChainFixture fixture{"synthetic_replay"};
    auto genesis = FullChainFixture::baseGenesis();
    genesis.m_features.push_back(
        ledger::FeatureSet{ledger::Features::Flag::feature_l2_ethereum_compat, 1});
    genesis.m_allocs.push_back(ledger::Alloc{.address = std::string(c_replayEoa1),
        .balance = u256(1000),
        .nonce = "0",
        .code = "",
        .storage = {}});
    genesis.m_allocs.push_back(ledger::Alloc{.address = std::string(c_replayEoa2),
        .balance = u256(2000),
        .nonce = "0",
        .code = "",
        .storage = {}});
    fixture.buildGenesis(genesis);

    // Expected-state model, seeded with the genesis allocs.
    std::map<Address, ReplayAccount> expected;
    expected[replayHexAddress(c_replayEoa1)] = {.balance = u256(1000), .slots = {}};
    expected[replayHexAddress(c_replayEoa2)] = {.balance = u256(2000), .slots = {}};
    std::vector<Address> known{replayHexAddress(c_replayEoa1), replayHexAddress(c_replayEoa2)};

    // Genesis stateRoot must already match the from-scratch oracle over the allocs.
    auto oracleRoot = [&] {
        std::map<Address, mpt::Account> accounts;
        for (auto const& [address, model] : expected)
        {
            mpt::Account account;
            account.balance = model.balance;
            account.nonce = model.nonce;
            if (!model.slots.empty())
            {
                std::map<h256, bytes> slots;
                for (auto const& [slot, value] : model.slots)
                {
                    slots[slot] = bytes(value.begin(), value.end());
                }
                account.storageRoot = FullChainFixture::storageTrieOracle(slots);
            }
            accounts[address] = account;
        }
        return FullChainFixture::mptOracle(accounts);
    };
    BOOST_REQUIRE_EQUAL(fixture.headerOnChain(0)->stateRoot().hex(), oracleRoot().hex());

    std::mt19937_64 rng{0x5EED2026};
    uint32_t nextNewAccount = 0;
    size_t slotDeletes = 0;
    size_t slotWrites = 0;

    for (size_t blockIndex = 1; blockIndex <= c_replayBlocks; ++blockIndex)
    {
        std::vector<FCRowOp> rows;
        size_t const ops = 1 + rng() % 4;
        for (size_t op = 0; op < ops; ++op)
        {
            auto action = rng() % 100;
            if (action < 25)
            {
                // Create a brand-new account: balance + nonce + one storage slot.
                auto address = replayAddress(nextNewAccount++);
                auto slot = replayH256(rng() % 16);
                auto value = replayH256((rng() % 255) + 1);
                rows.push_back(FullChainFixture::balanceRow(address, "500"));
                rows.push_back(FullChainFixture::nonceRow(address, "1"));
                rows.push_back(FullChainFixture::slotRow(address, slot, value));
                expected[address] = {.balance = u256(500), .nonce = 1, .slots = {{slot, value}}};
                known.push_back(address);
                ++slotWrites;
            }
            else if (action < 50)
            {
                // Balance update on a known account.
                auto const& address = known[rng() % known.size()];
                auto balance = 1 + rng() % 1'000'000;
                rows.push_back(FullChainFixture::balanceRow(address, std::to_string(balance)));
                expected[address].balance = u256(balance);
            }
            else if (action < 85)
            {
                // Slot write / overwrite on a known account (small slot space on purpose:
                // repeated keys force overwrites and shared trie paths).
                auto const& address = known[rng() % known.size()];
                auto slot = replayH256(rng() % 16);
                auto value = replayH256((rng() % 255) + 1);
                rows.push_back(FullChainFixture::slotRow(address, slot, value));
                expected[address].slots[slot] = value;
                ++slotWrites;
            }
            else
            {
                // Slot DELETE: drop one existing slot from a random account holding any
                // (logical deletion row -> trie node removal on the incremental path).
                auto start = rng() % known.size();
                for (size_t probe = 0; probe < known.size(); ++probe)
                {
                    auto const& address = known[(start + probe) % known.size()];
                    auto& slots = expected[address].slots;
                    if (slots.empty())
                    {
                        continue;
                    }
                    auto victim = slots.begin();
                    std::advance(victim, rng() % slots.size());
                    rows.push_back({bcos::ledger::mpt::accountTableName(address),
                        std::string(
                            reinterpret_cast<char const*>(victim->first.data()), h256::SIZE),
                        std::nullopt});
                    slots.erase(victim);
                    ++slotDeletes;
                    break;
                }
            }
        }
        // Deduplicate conflicting writes within the block is unnecessary: later rows win in
        // both the storage layer and the expected model (map assignment order == row order).
        auto blockNumber = static_cast<protocol::BlockNumber>(blockIndex);
        fixture.planBlock(blockNumber, std::move(rows));
        auto header = fixture.runBlock(blockNumber);

        // Chain-break semantics: the FIRST diverging block stops the replay (REQUIRE), so a
        // real encoding bug reports its exact block instead of 99 cascading mismatches.
        BOOST_REQUIRE_MESSAGE(header->stateRoot().hex() == oracleRoot().hex(),
            "block " + std::to_string(blockIndex) + " stateRoot mismatch: committed=" +
                header->stateRoot().hex() + " oracle=" + oracleRoot().hex());
    }

    // Anti-false-green: the op mix really exercised every path.
    BOOST_REQUIRE_GE(slotDeletes, 5);
    BOOST_REQUIRE_GE(slotWrites, 30);
    BOOST_REQUIRE_GE(known.size(), 10);
    BOOST_REQUIRE_GT(fixture.backendNodeCount(), 0);
    BOOST_CHECK_EQUAL(fixture.headerOnChain(c_replayBlocks)->stateRoot().hex(), oracleRoot().hex());
    std::cout << "[MPTSyntheticReplay] " << c_replayBlocks << "/" << c_replayBlocks
              << " blocks matched; accounts=" << known.size() << " slotWrites=" << slotWrites
              << " slotDeletes=" << slotDeletes << std::endl;
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace
