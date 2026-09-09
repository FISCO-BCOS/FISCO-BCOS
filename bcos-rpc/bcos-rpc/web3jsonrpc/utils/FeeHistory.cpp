/**
 * Copyright (C) 2026 FISCO BCOS.
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * @file FeeHistory.cpp
 * @brief eth_feeHistory: EIP-1559 history on Ethereum-mode chains, OP base-fee rules on OP Stack.
 */

#include "FeeHistory.h"

#include <bcos-framework/engine/OpBaseFee.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/protocol/Protocol.h>
#include <bcos-ledger/LedgerMethods.h>
#include <bcos-rpc/jsonrpc/Common.h>
#include <bcos-rpc/web3jsonrpc/utils/util.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/Exceptions.h>
#include <algorithm>
#include <cmath>
#include <limits>

using namespace bcos;
using namespace bcos::rpc;

namespace
{
constexpr std::size_t c_maxFeeHistoryBlocks = 1024;
// Reward requests load full block bodies per block; see buildFeeHistory for why this is
// tighter than the header-only cap.
constexpr std::size_t c_maxRewardHistoryBlocks = 128;
constexpr std::uint32_t c_eth1559Elasticity = 2;
constexpr std::uint32_t c_eth1559Denominator = 8;

bool versionAtLeast(bcos::protocol::EthBlockVersion version, bcos::protocol::EthBlockVersion fork)
{
    return static_cast<std::uint8_t>(version) >= static_cast<std::uint8_t>(fork);
}

bool isJovianOpParent(bcos::protocol::BlockHeader const& parent)
{
    auto const& extra = parent.extraData();
    return extra.size() == bcos::engine::c_jovianExtraDataBytes && !extra.empty() &&
           extra[0] == bcos::engine::c_jovianExtraDataVersion;
}

double gasUsedRatio(bcos::protocol::BlockHeader const& header)
{
    auto const limit = header.gasLimit();
    if (limit == 0)
    {
        return 0.0;
    }
    // Saturate rather than truncate: a value beyond u64_t is not producible by the engine,
    // and if gasUsed exceeds it the ratio is >= 1 (clamped) anyway.
    if (!bcos::u256FitsUint64(limit) || !bcos::u256FitsUint64(header.gasUsed()))
    {
        return 1.0;
    }
    auto const used = static_cast<double>(static_cast<std::uint64_t>(header.gasUsed()));
    auto const cap = static_cast<double>(static_cast<std::uint64_t>(limit));
    auto const ratio = used / cap;
    return std::clamp(ratio, 0.0, 1.0);
}

std::vector<GasWeightedPriorityFee> collectPriorityFeeSamples(
    bcos::protocol::Block const& block, bcos::u256 baseFee)
{
    // geth weights each sample by the receipt's gasUsed over EVERY transaction in the block,
    // zero-tip included (eth/gasprice/feehistory.go: sorter[i] = {gasUsed: receipts[i].GasUsed,
    // reward: reward}). Weighting by the gas limit, or dropping zero-tip txs, shifts every
    // percentile boundary away from the reference. Receipts are fetched alongside the
    // transactions (see the getBlockData flags in buildFeeHistory).
    std::vector<GasWeightedPriorityFee> samples;
    auto receipts = block.receipts();  // any_view: size()/operator[] need a non-const view
    std::size_t index = 0;
    for (auto const& tx : block.transactions())
    {
        std::uint64_t gasUsed = 0;
        if (index < receipts.size())
        {
            auto const used = receipts[index]->gasUsed();
            gasUsed = bcos::u256FitsUint64(used) ? static_cast<std::uint64_t>(used) : 0;
        }
        ++index;
        samples.push_back(
            GasWeightedPriorityFee{effectivePriorityFeePerGas(*tx, baseFee), gasUsed});
    }
    std::sort(samples.begin(), samples.end(),
        [](GasWeightedPriorityFee const& left, GasWeightedPriorityFee const& right) {
            return left.tip < right.tip;
        });
    return samples;
}
}  // namespace

bcos::u256 bcos::rpc::blockBaseFee(bcos::protocol::BlockHeader const& header)
{
    // OP-Stack headers are NON_ETH yet carry a real base fee (rebuildOpEthHeader
    // deliberately leaves ethBlockVersion NON_ETH). Check that case before the NON_ETH
    // short-circuit, which is for native FISCO headers that have no base fee at all.
    if (isOpEthereumBlock(header))
    {
        return header.baseFee().value_or(0);
    }
    if (!versionAtLeast(header.ethBlockVersion(), bcos::protocol::EthBlockVersion::LONDON))
    {
        return 0;
    }
    return header.baseFee().value_or(0);
}

bcos::u256 bcos::rpc::calcEthNextBaseFee(bcos::protocol::BlockHeader const& parent)
{
    if (!versionAtLeast(parent.ethBlockVersion(), bcos::protocol::EthBlockVersion::LONDON))
    {
        return 0;
    }
    auto const parentBase = parent.baseFee().value_or(0);
    // Refuse to compute on an over-wide header rather than truncating the gas fields.
    if (!bcos::u256FitsUint64(parent.gasLimit()) || !bcos::u256FitsUint64(parent.gasUsed()))
    {
        return parentBase;
    }
    auto const gasLimit = static_cast<std::uint64_t>(parent.gasLimit());
    auto const gasUsed = static_cast<std::uint64_t>(parent.gasUsed());
    if (gasLimit == 0)
    {
        return parentBase;
    }
    auto const gasTarget = gasLimit / c_eth1559Elasticity;
    if (gasTarget == 0)
    {
        return parentBase;
    }
    if (gasUsed == gasTarget)
    {
        return parentBase;
    }
    if (gasUsed > gasTarget)
    {
        auto const delta = gasUsed - gasTarget;
        bcos::u256 deltaFee = parentBase * bcos::u256(delta);
        deltaFee /= gasTarget;
        deltaFee /= c_eth1559Denominator;
        if (deltaFee == 0)
        {
            deltaFee = 1;
        }
        return parentBase + deltaFee;
    }
    auto const delta = gasTarget - gasUsed;
    bcos::u256 deltaFee = parentBase * bcos::u256(delta);
    deltaFee /= gasTarget;
    deltaFee /= c_eth1559Denominator;
    return deltaFee < parentBase ? parentBase - deltaFee : bcos::u256(0);
}

bcos::u256 bcos::rpc::calcOpNextBaseFee(bcos::protocol::BlockHeader const& parent)
{
    // Pre-check the parent's shape instead of catching everything: a genesis-adjacent OP
    // parent (empty or not-yet-Holocene extraData) has no EIP-1559 parameters to decode, so
    // keep its base fee. calcOpBaseFee's own fail-closed errors (missing baseFee, a Jovian
    // parent missing blobGasUsed, u256 overflow) must surface rather than silently degrading
    // to the parent fee — a bare catch (...) hid them all.
    auto const& extra = parent.extraData();
    auto const extraSpan = std::span<const bcos::byte>(
        reinterpret_cast<const bcos::byte*>(extra.data()), extra.size());
    if (extra.empty() ||
        bcos::engine::validateOpExtraDataShape(extraSpan, /*allowEmpty=*/true).has_value())
    {
        return blockBaseFee(parent);
    }
    // parentIsJovian is derived from the parent's extraData shape (17-byte Jovian form):
    // this RPC path has no fork schedule, and the header shape is the only signal available
    // here. It agrees with m_scheduler.isJovianActive() for headers this node produced.
    return bcos::engine::calcOpBaseFee(parent, isJovianOpParent(parent));
}

bcos::u256 bcos::rpc::effectivePriorityFeePerGas(
    bcos::protocol::Transaction const& tx, bcos::u256 const& baseFee)
{
    bcos::u256 tip = 0;
    if (auto const priority = tx.maxPriorityFeePerGas(); priority.has_value())
    {
        tip = *priority;
    }
    else if (auto const gasPrice = tx.gasPrice(); gasPrice.has_value() && *gasPrice > baseFee)
    {
        tip = *gasPrice - baseFee;
    }
    bcos::u256 maxFee = tx.maxFeePerGas().value_or(tx.gasPrice().value_or(0));
    bcos::u256 cap = maxFee > baseFee ? maxFee - baseFee : bcos::u256(0);
    return tip < cap ? tip : cap;
}

std::vector<bcos::u256> bcos::rpc::pickRewardPercentiles(
    std::vector<GasWeightedPriorityFee> const& samples, std::span<double const> percentiles)
{
    std::vector<bcos::u256> rewards;
    rewards.reserve(percentiles.size());
    if (samples.empty())
    {
        rewards.assign(percentiles.size(), 0);
        return rewards;
    }
    // One prefix sum over cumulative gas, then a binary search per percentile: the boundary is
    // the tip of the first sample whose cumulative gas reaches the threshold. The previous
    // version restarted the scan inside the percentile loop (O(percentiles x samples)); with up
    // to 100 percentiles and 1024 blocks per request that dominated the response cost.
    std::vector<std::uint64_t> cumulativeGas;
    cumulativeGas.reserve(samples.size());
    std::uint64_t totalGas = 0;
    for (auto const& sample : samples)
    {
        totalGas += sample.gas;
        cumulativeGas.push_back(totalGas);
    }
    for (double percentile : percentiles)
    {
        // The RPC boundary rejects values outside [0, 100], so no clamp is needed here; a
        // clamp would only mask a caller that skipped that validation.
        if (totalGas == 0)
        {
            rewards.push_back(0);
            continue;
        }
        auto const threshold = static_cast<double>(totalGas) * percentile / 100.0;
        auto const boundary =
            std::lower_bound(cumulativeGas.begin(), cumulativeGas.end(), threshold);
        auto const index = boundary == cumulativeGas.end() ?
                               samples.size() - 1 :
                               static_cast<std::size_t>(boundary - cumulativeGas.begin());
        rewards.push_back(samples[index].tip);
    }
    return rewards;
}

bcos::task::Task<Json::Value> bcos::rpc::buildFeeHistory(bcos::ledger::LedgerInterface& ledger,
    bcos::protocol::BlockNumber newestBlock, std::size_t blockCount,
    std::vector<double> const& rewardPercentiles, bool opStackMode)
{
    // The reward path loads each block's full body (transactions + receipts) and is reachable
    // unauthenticated on the public listener; the header-only path is cheap. geth's 1024 cap
    // assumes its fee-history cache, which this implementation does not have, so the
    // body-loading path gets a tighter bound on what one request can deserialise.
    bool const wantRewards = !rewardPercentiles.empty();
    blockCount =
        std::min(blockCount, wantRewards ? c_maxRewardHistoryBlocks : c_maxFeeHistoryBlocks);
    if (blockCount == 0)
    {
        co_return Json::Value(Json::objectValue);
    }

    auto const oldestBlock = static_cast<bcos::protocol::BlockNumber>(
        (std::max)(static_cast<bcos::protocol::BlockNumber>(newestBlock + 1 - blockCount),
            bcos::protocol::BlockNumber{0}));

    Json::Value result(Json::objectValue);
    result["oldestBlock"] = toQuantity(oldestBlock);

    Json::Value baseFees(Json::arrayValue);
    Json::Value gasRatios(Json::arrayValue);
    Json::Value rewards(Json::arrayValue);

    std::shared_ptr<bcos::protocol::BlockHeader> lastHeader;
    for (auto number = oldestBlock; number <= newestBlock; ++number)
    {
        auto const block = co_await ledger::getBlockData(ledger, number,
            bcos::ledger::HEADER |
                (wantRewards ? (bcos::ledger::TRANSACTIONS | bcos::ledger::RECEIPTS) : 0));
        if (!block || !block->blockHeader())
        {
            BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams, "Block not found"));
        }
        auto const& header = *block->blockHeader();
        lastHeader = block->blockHeader();

        baseFees.append(toQuantity(blockBaseFee(header)));
        gasRatios.append(gasUsedRatio(header));

        if (wantRewards)
        {
            auto const samples = collectPriorityFeeSamples(*block, blockBaseFee(header));
            auto const row = pickRewardPercentiles(samples, rewardPercentiles);
            Json::Value rewardRow(Json::arrayValue);
            for (auto const& tip : row)
            {
                rewardRow.append(toQuantity(tip));
            }
            rewards.append(std::move(rewardRow));
        }
    }

    bcos::u256 trailingBaseFee = 0;
    if (lastHeader)
    {
        if (opStackMode)
        {
            trailingBaseFee = calcOpNextBaseFee(*lastHeader);
        }
        else if (versionAtLeast(
                     lastHeader->ethBlockVersion(), bcos::protocol::EthBlockVersion::LONDON))
        {
            trailingBaseFee = calcEthNextBaseFee(*lastHeader);
        }
    }
    baseFees.append(toQuantity(trailingBaseFee));

    result["baseFeePerGas"] = std::move(baseFees);
    result["gasUsedRatio"] = std::move(gasRatios);
    if (wantRewards)
    {
        result["reward"] = std::move(rewards);
    }
    co_return result;
}
