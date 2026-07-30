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
 * @file MPTSubsequentTouchP99Gate.cpp
 * @brief Release-blocker latency gates (M10.3, spec design3 §11):
 *          - "单块 commit 关键路径延迟 p99 < 200ms（1000 账户改动规模、subsequent-touch 路径）"
 *          - "first-touch 大账户（本块写 1 slot）的 commit 延迟 p99 ≤ subsequent-touch 量级"
 *        One scenario-A chain: a big dormant account is pre-filled under XOR, the flag
 *        activates, GATE_ACCOUNTS accounts warm into the MPT, then every measured block
 *        rewrites one storage slot on each of them (subsequent-touch). The final block
 *        first-touches the big dormant account with a single slot write.
 *
 *        The HARD assertions arm ONLY under MPT_ACCEPTANCE=1 — the environment the ctest
 *        "acceptance"-labeled registration sets (tests/CMakeLists.txt). The per-PR CI run
 *        (plain ctest) executes a reduced-scale measurement pass that reports the same
 *        numbers without failing, so a slow shared runner cannot turn this into flaky CI.
 *        Distribution measurement (no gate) lives in MPTFirstTouchLatencyBench.cpp.
 */
#include "FullChainFixture.h"
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <random>
#include <vector>

namespace
{
using namespace bcos;
using namespace bcos::test::fullchain;

constexpr std::string_view c_mptFlagName = "feature_mpt_state_root";
constexpr double c_subsequentTouchP99GateMs = 200.0;  // spec §11: p99 < 200ms
// "First-touch ≤ subsequent-touch 量级" (same order of magnitude): 10x the measured
// subsequent-touch p99, with an absolute floor so a very fast subsequent-touch baseline
// does not turn scheduling jitter into a false red.
constexpr double c_firstTouchMagnitudeFactor = 10.0;
constexpr double c_firstTouchFloorMs = 50.0;

bool gateArmed()
{
    char const* env = std::getenv("MPT_ACCEPTANCE");
    return env != nullptr && env[0] == '1';
}

Address gateAddress(uint32_t index)
{
    Address address{};
    address.data()[0] = static_cast<uint8_t>(index >> 24U);
    address.data()[1] = static_cast<uint8_t>(index >> 16U);
    address.data()[2] = static_cast<uint8_t>(index >> 8U);
    address.data()[3] = static_cast<uint8_t>(index);
    address.data()[19] = 0x99;  // disjoint from other suites' address spaces
    return address;
}

h256 gateH256(uint64_t value)
{
    h256 out{};
    for (size_t i = 0; i < sizeof(value); ++i)
    {
        out.data()[31 - i] = static_cast<uint8_t>(value >> (8U * i));
    }
    return out;
}

BOOST_AUTO_TEST_SUITE(MPTSubsequentTouchP99GateSuite)

BOOST_AUTO_TEST_CASE(SubsequentTouchP99AndBigFirstTouchGates)
{
    bool const armed = gateArmed();
    // Armed (ctest -L acceptance): spec scale — 1000-account change set per block, 100
    // measured blocks, 100k-flat-slot big account. Default: reduced measurement pass.
    size_t const accounts = armed ? 1'000 : 250;
    size_t const measureBlocks = armed ? 100 : 20;
    size_t const bigAccountSlots = armed ? 100'000 : 5'000;
    size_t const prefillRowsPerBlock = 10'000;
    constexpr size_t warmupBlocks = 2;

    FullChainFixture fixture{"subsequent_touch_gate"};
    fixture.buildGenesis(FullChainFixture::baseGenesis());

    // --- XOR-era pre-fill: the big dormant account ----------------------------------------
    auto const bigAccount = gateAddress(0xFFFFFF00U);
    protocol::BlockNumber nextBlock = 1;
    {
        std::vector<FCRowOp> pending;
        pending.reserve(prefillRowsPerBlock + 1);
        pending.push_back(FullChainFixture::balanceRow(bigAccount, "7777"));
        for (size_t slot = 0; slot < bigAccountSlots; ++slot)
        {
            pending.push_back(FullChainFixture::slotRow(bigAccount, gateH256(slot), gateH256(1)));
            if (pending.size() >= prefillRowsPerBlock)
            {
                fixture.planBlock(nextBlock, std::move(pending));
                fixture.runBlock(nextBlock);
                ++nextBlock;
                pending.clear();
            }
        }
        if (!pending.empty())
        {
            fixture.planBlock(nextBlock, std::move(pending));
            fixture.runBlock(nextBlock);
            ++nextBlock;
        }
    }

    // --- Activation + warmup: all measured accounts enter the MPT -------------------------
    auto const activationBlock = nextBlock;
    fixture.enableFeatureFromBlock(c_mptFlagName, activationBlock);
    fixture.planBlock(activationBlock, {FullChainFixture::balanceRow(gateAddress(1), "1")});
    fixture.runBlock(activationBlock);  // activation block itself: still XOR
    BOOST_REQUIRE(
        fixture.featuresAt(activationBlock).get(ledger::Features::Flag::feature_mpt_state_root));

    nextBlock = activationBlock + 1;
    for (size_t warmup = 0; warmup < warmupBlocks; ++warmup)
    {
        std::vector<FCRowOp> rows;
        rows.reserve(accounts);
        for (size_t account = 0; account < accounts; ++account)
        {
            rows.push_back(FullChainFixture::balanceRow(
                gateAddress(static_cast<uint32_t>(account)), std::to_string(1000 + warmup)));
        }
        fixture.planBlock(nextBlock, std::move(rows));
        fixture.runBlock(nextBlock);
        ++nextBlock;
    }
    BOOST_REQUIRE_GT(fixture.backendNodeCount(), 0);  // warmed accounts really are in the MPT

    // --- Measured subsequent-touch blocks: one slot rewrite per account per block ---------
    std::vector<double> latenciesMs;
    latenciesMs.reserve(measureBlocks);
    std::mt19937_64 rng{12345};
    for (size_t measured = 0; measured < measureBlocks; ++measured)
    {
        std::vector<FCRowOp> rows;
        rows.reserve(accounts);
        for (size_t account = 0; account < accounts; ++account)
        {
            rows.push_back(FullChainFixture::slotRow(gateAddress(static_cast<uint32_t>(account)),
                gateH256(rng() % 64), gateH256((rng() % 255) + 1)));
        }
        fixture.planBlock(nextBlock, std::move(rows));
        auto start = std::chrono::steady_clock::now();
        fixture.runBlock(nextBlock);
        latenciesMs.push_back(
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count());
        ++nextBlock;
    }

    // --- Big dormant account: single first-touch block ------------------------------------
    fixture.planBlock(
        nextBlock, {FullChainFixture::balanceRow(bigAccount, "8888"),
                       FullChainFixture::slotRow(bigAccount, gateH256(0xF17570CB), gateH256(1))});
    auto bigStart = std::chrono::steady_clock::now();
    fixture.runBlock(nextBlock);
    double const bigTouchMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - bigStart)
            .count();

    std::sort(latenciesMs.begin(), latenciesMs.end());
    auto percentile = [&](size_t numerator) {
        return latenciesMs[std::min(latenciesMs.size() * numerator / 100, latenciesMs.size() - 1)];
    };
    double const p50 = percentile(50);
    double const p99 = percentile(99);
    double const maxMs = latenciesMs.back();
    double const firstTouchCeilingMs =
        std::max(c_firstTouchMagnitudeFactor * p99, c_firstTouchFloorMs);

    std::cout << "[MPTSubsequentTouchP99Gate] mode=" << (armed ? "GATE (armed)" : "report-only")
              << " accounts/block=" << accounts << " samples=" << latenciesMs.size() << "\n"
              << "[MPTSubsequentTouchP99Gate] subsequent-touch: p50=" << p50 << "ms p99=" << p99
              << "ms max=" << maxMs << "ms (gate: p99 < " << c_subsequentTouchP99GateMs << "ms)\n"
              << "[MPTSubsequentTouchP99Gate] big-account (" << bigAccountSlots
              << " flat slots) first-touch: " << bigTouchMs
              << "ms (gate: <= " << firstTouchCeilingMs << "ms)" << std::endl;

    if (armed)
    {
        // spec §11: subsequent-touch p99 < 200ms at the 1000-account change scale.
        BOOST_CHECK_LT(p99, c_subsequentTouchP99GateMs);
        // spec §11: big-account first-touch stays within the subsequent-touch magnitude —
        // pins the absence of any prefix-scan bootstrap over the 100k dormant flat slots.
        BOOST_CHECK_LE(bigTouchMs, firstTouchCeilingMs);
    }
    else
    {
        BOOST_TEST_MESSAGE("MPT_ACCEPTANCE not set: measurement pass only, gates not armed");
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace
