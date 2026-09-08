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
 * @brief eth_feeHistory: EIP-1559 history on Ethereum-mode chains, OP base-fee rules on OP Stack.
 */

#pragma once

#include <bcos-framework/ledger/LedgerInterface.h>
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-framework/protocol/Transaction.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <json/json.h>
#include <cstddef>
#include <span>
#include <vector>

namespace bcos::rpc
{

/// Base fee a block header carries, as the eth_feeHistory history array reports it.
/// 0 for native FISCO NON_ETH headers and for pre-London Eth headers. OP-Stack headers
/// are NON_ETH yet carry a real base fee (see isOpEthereumBlock), so they must NOT take
/// the NON_ETH short-circuit — that reported 0x0 for every OP block.
bcos::u256 blockBaseFee(bcos::protocol::BlockHeader const& header);

/// Ethereum L1 next-block base fee (EIP-1559, elasticity 2, denominator 8).
bcos::u256 calcEthNextBaseFee(bcos::protocol::BlockHeader const& parent);

/// OP Stack next-block base fee (op-geth CalcBaseFee). Returns parent base fee when the parent
/// header is not yet Holocene-shaped (genesis-adjacent OP chains).
bcos::u256 calcOpNextBaseFee(bcos::protocol::BlockHeader const& parent);

/// Effective priority fee per gas for one transaction at a given block base fee.
bcos::u256 effectivePriorityFeePerGas(
    bcos::protocol::Transaction const& tx, bcos::u256 const& baseFee);

/// Pick reward percentiles from sorted priority fees (geth-compatible indexing).
std::vector<bcos::u256> pickRewardPercentiles(
    std::vector<bcos::u256> const& sortedTips, std::span<double const> percentiles);

/// Build the eth_feeHistory result object. `opStackMode` selects OP vs Ethereum base-fee
/// prediction for the trailing entry (and OP parent metering on Jovian parents).
bcos::task::Task<Json::Value> buildFeeHistory(bcos::ledger::LedgerInterface& ledger,
    bcos::protocol::BlockNumber newestBlock, std::size_t blockCount,
    std::vector<double> const& rewardPercentiles, bool opStackMode);

}  // namespace bcos::rpc
