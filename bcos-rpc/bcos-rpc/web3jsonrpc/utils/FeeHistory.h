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
 * @file FeeHistory.h
 * @brief eth_feeHistory on the OP lane (geth eth/gasprice/feehistory.go shape).
 */

#pragma once

#include <bcos-framework/ledger/LedgerInterface.h>
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-framework/protocol/Transaction.h>
#include <bcos-task/Task.h>
#include <json/json.h>
#include <cstdint>
#include <span>
#include <vector>

namespace bcos::rpc
{
/// geth's maxHeaderHistory / maxBlockHistory default: a larger blockCount is clamped, not
/// rejected.
inline constexpr std::uint64_t c_maxFeeHistoryBlocks = 1024;

/// geth EffectiveGasTip: min(maxPriorityFeePerGas, maxFeePerGas - baseFee), floored at 0. A
/// legacy web3 tx carries its gas price in both fields (Web3TxHandler), which yields
/// gasPrice - baseFee.
u256 effectivePriorityFee(protocol::Transaction const& tx, u256 const& baseFee);

struct RewardSample
{
    u256 tip;
    std::uint64_t gasUsed = 0;
};

/// One block's reward row (geth processBlock): samples stable-sorted by tip, each percentile
/// picks the first sample whose cumulative gasUsed reaches blockGasUsed * p / 100 (truncated to
/// uint64). No samples, or a block with zero gasUsed, gives a zero row.
std::vector<u256> rewardPercentiles(std::vector<RewardSample> samples,
    std::span<double const> percentiles, std::uint64_t blockGasUsed);

/// The next block's base fee after @p parent under the OP EIP-1559 rule (op-geth CalcBaseFee).
/// The era comes from the parent header's shape, which a validated OP chain fixes: non-empty
/// extraData = Holocene+ parent (parameters decoded from it; 17 bytes / 0x01 = Jovian, which
/// adds the DA-footprint metering and minBaseFee floor); empty extraData = pre-Holocene, with
/// elasticity 6 and denominator 250 when the child is Canyon, else 50. The child counts as
/// Canyon when the parent carries withdrawalsRoot (every Canyon+ OP header does), so only the
/// Canyon activation block itself is predicted with the Bedrock denominator.
u256 nextOpBaseFee(protocol::BlockHeader const& parent);

/// eth_feeHistory for blocks [newest - blockCount + 1, newest] of an OP-lane ledger whose head
/// is @p head. blockCount 0 answers {"oldestBlock": "0x0"}; above c_maxFeeHistoryBlocks it is
/// clamped; newest > head is InvalidParams. Deposits enter the rewards with tip 0.
task::Task<Json::Value> buildOpFeeHistory(ledger::LedgerInterface& ledger,
    protocol::BlockNumber newest, protocol::BlockNumber head, std::uint64_t blockCount,
    std::vector<double> const& percentiles);
}  // namespace bcos::rpc
