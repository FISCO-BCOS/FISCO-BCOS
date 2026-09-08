/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
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
constexpr std::uint32_t c_eth1559Elasticity = 2;
constexpr std::uint32_t c_eth1559Denominator = 8;

bool versionAtLeast(bcos::protocol::EthBlockVersion version, bcos::protocol::EthBlockVersion fork)
{
    return static_cast<std::uint8_t>(version) >= static_cast<std::uint8_t>(fork);
}

bcos::u256 headerBaseFee(bcos::protocol::BlockHeader const& header)
{
    if (header.ethBlockVersion() == bcos::protocol::EthBlockVersion::NON_ETH)
    {
        return 0;
    }
    if (!versionAtLeast(header.ethBlockVersion(), bcos::protocol::EthBlockVersion::LONDON))
    {
        return 0;
    }
    return header.baseFee().value_or(0);
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
    auto const used = static_cast<double>(static_cast<std::uint64_t>(header.gasUsed()));
    auto const cap = static_cast<double>(static_cast<std::uint64_t>(limit));
    auto const ratio = used / cap;
    return std::clamp(ratio, 0.0, 1.0);
}

std::vector<bcos::u256> collectPriorityFees(bcos::protocol::Block const& block, bcos::u256 baseFee)
{
    std::vector<bcos::u256> tips;
    for (auto const& tx : block.transactions())
    {
        auto const tip = effectivePriorityFeePerGas(*tx, baseFee);
        if (tip > 0)
        {
            tips.push_back(tip);
        }
    }
    std::sort(tips.begin(), tips.end());
    return tips;
}
}  // namespace

bcos::u256 bcos::rpc::calcEthNextBaseFee(bcos::protocol::BlockHeader const& parent)
{
    if (!versionAtLeast(parent.ethBlockVersion(), bcos::protocol::EthBlockVersion::LONDON))
    {
        return 0;
    }
    auto const parentBase = parent.baseFee().value_or(0);
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
    try
    {
        return bcos::engine::calcOpBaseFee(parent, isJovianOpParent(parent));
    }
    catch (...)
    {
        // Genesis-adjacent OP parents may not yet carry Holocene extraData; keep the last base
        // fee instead of failing the whole eth_feeHistory call.
        return headerBaseFee(parent);
    }
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
    std::vector<bcos::u256> const& sortedTips, std::span<double const> percentiles)
{
    std::vector<bcos::u256> rewards;
    rewards.reserve(percentiles.size());
    if (sortedTips.empty())
    {
        rewards.assign(percentiles.size(), 0);
        return rewards;
    }
    for (double percentile : percentiles)
    {
        auto const clamped = std::clamp(percentile, 0.0, 100.0);
        auto const idx = static_cast<std::size_t>((sortedTips.size() - 1) * clamped / 100.0);
        rewards.push_back(sortedTips[idx]);
    }
    return rewards;
}

bcos::task::Task<Json::Value> bcos::rpc::buildFeeHistory(bcos::ledger::LedgerInterface& ledger,
    bcos::protocol::BlockNumber newestBlock, std::size_t blockCount,
    std::vector<double> const& rewardPercentiles, bool opStackMode)
{
    blockCount = std::min(blockCount, c_maxFeeHistoryBlocks);
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
    const bool wantRewards = !rewardPercentiles.empty();

    std::shared_ptr<bcos::protocol::BlockHeader> lastHeader;
    for (auto number = oldestBlock; number <= newestBlock; ++number)
    {
        auto const block = co_await ledger::getBlockData(
            ledger, number, bcos::ledger::HEADER | (wantRewards ? bcos::ledger::TRANSACTIONS : 0));
        if (!block || !block->blockHeader())
        {
            BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams, "Block not found"));
        }
        auto const& header = *block->blockHeader();
        lastHeader = block->blockHeader();

        baseFees.append(toQuantity(headerBaseFee(header)));
        gasRatios.append(gasUsedRatio(header));

        if (wantRewards)
        {
            auto const tips = collectPriorityFees(*block, headerBaseFee(header));
            auto const row = pickRewardPercentiles(tips, rewardPercentiles);
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
