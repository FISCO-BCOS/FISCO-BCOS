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
 * @file MPTFirstTouchLatencyBench.cpp
 * @brief Scenario-A first-touch latency distribution (M10.2, spec design3 §7.3): a chain
 *        pre-filled with dormant accounts under the XOR era activates feature_mpt_state_root;
 *        every measured block then first-touches accounts never seen by the MPT. Since the
 *        2026-07-09 revision first-touch reads O(touched rows) — no prefix-scan bootstrap —
 *        so the latency must not grow with the dormant flat footprint; a "big" dormant
 *        account (BIG_ACCOUNT_SLOTS pre-existing flat slots, one slot written on touch) is
 *        included to spotlight exactly that.
 *
 *        MEASUREMENT ONLY — no latency assertion here (the hard gate lives in
 *        MPTSubsequentTouchP99Gate.cpp). The p50/p95/p99/max numbers print to stdout for
 *        reviewer judgment. Env MPT_ACCEPTANCE=1 (set by the ctest "acceptance" label
 *        registrations in tests/CMakeLists.txt) switches to the full-scale workload; the
 *        default scale keeps the per-PR CI run short.
 */
#include "FullChainFixture.h"
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace
{
using namespace bcos;
using namespace bcos::test::fullchain;

constexpr std::string_view c_mptFlagName = "feature_mpt_state_root";

bool acceptanceScale()
{
    char const* env = std::getenv("MPT_ACCEPTANCE");
    return env != nullptr && env[0] == '1';
}

/// Distinct 20-byte addresses beyond makeAddress's 256-value range.
Address benchAddress(uint32_t index)
{
    Address address{};
    address.data()[0] = static_cast<uint8_t>(index >> 24U);
    address.data()[1] = static_cast<uint8_t>(index >> 16U);
    address.data()[2] = static_cast<uint8_t>(index >> 8U);
    address.data()[3] = static_cast<uint8_t>(index);
    address.data()[19] = 0xB1;  // disjoint from other suites' address spaces
    return address;
}

h256 benchH256(uint64_t value)
{
    h256 out{};
    for (size_t i = 0; i < sizeof(value); ++i)
    {
        out.data()[31 - i] = static_cast<uint8_t>(value >> (8U * i));
    }
    return out;
}

struct LatencyStats
{
    double p50Ms{};
    double p95Ms{};
    double p99Ms{};
    double maxMs{};
};

LatencyStats computeStats(std::vector<double> latenciesMs)
{
    std::sort(latenciesMs.begin(), latenciesMs.end());
    auto at = [&](size_t index) { return latenciesMs[std::min(index, latenciesMs.size() - 1)]; };
    return {.p50Ms = at(latenciesMs.size() / 2),
        .p95Ms = at(latenciesMs.size() * 95 / 100),
        .p99Ms = at(latenciesMs.size() * 99 / 100),
        .maxMs = latenciesMs.back()};
}

BOOST_AUTO_TEST_SUITE(MPTFirstTouchLatencyBenchSuite)

BOOST_AUTO_TEST_CASE(FirstTouchLatencyDistribution)
{
    bool const fullScale = acceptanceScale();
    // Full scale (ctest -L acceptance): 200 measured blocks x 2 fresh dormant accounts,
    // 100k-slot big account. Default: sized for the per-PR CI ctest run.
    size_t const measureBlocks = fullScale ? 200 : 30;
    size_t const touchesPerBlock = 2;
    size_t const dormantSlotsEach = fullScale ? 100 : 20;  // flat slots per dormant account
    size_t const bigAccountSlots = fullScale ? 100'000 : 5'000;
    size_t const prefillRowsPerBlock = 10'000;

    size_t const dormantAccounts = measureBlocks * touchesPerBlock;

    FullChainFixture fixture{"first_touch_bench"};
    fixture.buildGenesis(FullChainFixture::baseGenesis());

    // --- Phase 1 (XOR era): pre-fill dormant accounts + the big account -------------------
    auto const bigAccount = benchAddress(0xFFFFFF00U);
    std::vector<FCRowOp> pending;
    pending.reserve(prefillRowsPerBlock + dormantSlotsEach + 2);
    protocol::BlockNumber nextBlock = 1;
    auto flushPending = [&] {
        if (pending.empty())
        {
            return;
        }
        fixture.planBlock(nextBlock, std::move(pending));
        fixture.runBlock(nextBlock);
        ++nextBlock;
        pending.clear();
    };
    for (size_t account = 0; account < dormantAccounts; ++account)
    {
        auto address = benchAddress(static_cast<uint32_t>(account));
        pending.push_back(FullChainFixture::balanceRow(address, "1000"));
        pending.push_back(FullChainFixture::nonceRow(address, "1"));
        for (size_t slot = 0; slot < dormantSlotsEach; ++slot)
        {
            pending.push_back(
                FullChainFixture::slotRow(address, benchH256(slot), benchH256(slot + 1)));
        }
        if (pending.size() >= prefillRowsPerBlock)
        {
            flushPending();
        }
    }
    pending.push_back(FullChainFixture::balanceRow(bigAccount, "7777"));
    for (size_t slot = 0; slot < bigAccountSlots; ++slot)
    {
        pending.push_back(FullChainFixture::slotRow(bigAccount, benchH256(slot), benchH256(1)));
        if (pending.size() >= prefillRowsPerBlock)
        {
            flushPending();
        }
    }
    flushPending();

    // --- Phase 2: activate scenario A after the pre-fill ----------------------------------
    auto const activationBlock = nextBlock;
    fixture.enableFeatureFromBlock(c_mptFlagName, activationBlock);
    fixture.planBlock(activationBlock,
        {FullChainFixture::balanceRow(benchAddress(0xFFFFFF01U), "1")});  // still XOR
    fixture.runBlock(activationBlock);
    BOOST_REQUIRE(
        fixture.featuresAt(activationBlock).get(ledger::Features::Flag::feature_mpt_state_root));
    BOOST_REQUIRE_EQUAL(fixture.backendNodeCount(), 0);  // MPT starts strictly after activation

    // --- Phase 3: measured first-touch blocks ---------------------------------------------
    std::vector<double> latenciesMs;
    latenciesMs.reserve(measureBlocks);
    size_t nextAccount = 0;
    for (size_t measured = 0; measured < measureBlocks; ++measured)
    {
        auto blockNumber = activationBlock + 1 + static_cast<protocol::BlockNumber>(measured);
        std::vector<FCRowOp> rows;
        rows.reserve(touchesPerBlock * 2);
        for (size_t touch = 0; touch < touchesPerBlock; ++touch)
        {
            auto address = benchAddress(static_cast<uint32_t>(nextAccount++));
            // First touch: balance update + ONE new slot (the pre-activation slots stay
            // dormant — no bootstrap since the 2026-07-09 revision).
            rows.push_back(FullChainFixture::balanceRow(address, "2000"));
            rows.push_back(FullChainFixture::slotRow(
                address, benchH256(0xF000 + measured), benchH256(measured + 1)));
        }
        fixture.planBlock(blockNumber, std::move(rows));
        auto start = std::chrono::steady_clock::now();
        fixture.runBlock(blockNumber);
        latenciesMs.push_back(
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count());
    }
    BOOST_REQUIRE_GT(fixture.backendNodeCount(), 0);  // the MPT path really ran

    // --- Phase 4: the big dormant account's first touch (one slot) ------------------------
    auto bigBlock = activationBlock + 1 + static_cast<protocol::BlockNumber>(measureBlocks);
    fixture.planBlock(
        bigBlock, {FullChainFixture::balanceRow(bigAccount, "8888"),
                      FullChainFixture::slotRow(bigAccount, benchH256(0xF17570CB), benchH256(1))});
    auto bigStart = std::chrono::steady_clock::now();
    fixture.runBlock(bigBlock);
    double const bigTouchMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - bigStart)
            .count();

    auto stats = computeStats(latenciesMs);
    std::cout << "[MPTFirstTouchLatencyBench] scale=" << (fullScale ? "acceptance" : "default")
              << " samples=" << latenciesMs.size() << " touchesPerBlock=" << touchesPerBlock
              << " dormantSlotsEach=" << dormantSlotsEach << "\n"
              << "[MPTFirstTouchLatencyBench] first-touch block latency: p50=" << stats.p50Ms
              << "ms p95=" << stats.p95Ms << "ms p99=" << stats.p99Ms << "ms max=" << stats.maxMs
              << "ms\n"
              << "[MPTFirstTouchLatencyBench] big-account (" << bigAccountSlots
              << " pre-existing flat slots) single first-touch: " << bigTouchMs << "ms"
              << std::endl;

    // Measurement only: no latency assertion (see MPTSubsequentTouchP99Gate for the hard
    // gates). The REQUIREs above already pin that the numbers came from the MPT path.
    BOOST_CHECK_GE(latenciesMs.size(), measureBlocks);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace
