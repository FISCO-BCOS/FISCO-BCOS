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
 * @file FeeHistory.cpp
 * @brief eth_feeHistory on the OP lane (geth eth/gasprice/feehistory.go shape).
 */

#include "FeeHistory.h"
#include <bcos-framework/engine/OpBaseFee.h>
#include <bcos-ledger/LedgerMethods.h>
#include <bcos-rlp-protocol/EthPoSHeaderValidation.h>  // protocol::fakeExponential
#include <bcos-rpc/jsonrpc/Common.h>
#include <bcos-rpc/web3jsonrpc/utils/RpcChainPolicy.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <algorithm>
#include <limits>

using namespace bcos;
using namespace bcos::rpc;

namespace
{
std::uint64_t saturatedUint64(u256 const& value)
{
    return u256FitsUint64(value) ? static_cast<std::uint64_t>(value) :
                                   std::numeric_limits<std::uint64_t>::max();
}

double gasUsedRatio(protocol::BlockHeader const& header)
{
    auto const limit = saturatedUint64(header.gasLimit());
    return limit == 0 ?
               0.0 :
               static_cast<double>(saturatedUint64(header.gasUsed())) / static_cast<double>(limit);
}
}  // namespace

u256 bcos::rpc::effectivePriorityFee(protocol::Transaction const& tx, u256 const& baseFee)
{
    auto const tip = tx.maxPriorityFeePerGas().value_or(tx.gasPrice().value_or(0));
    auto const maxFee = tx.maxFeePerGas().value_or(tx.gasPrice().value_or(0));
    auto const cap = maxFee > baseFee ? maxFee - baseFee : u256(0);
    return std::min(tip, cap);
}

std::vector<u256> bcos::rpc::rewardPercentiles(std::vector<RewardSample> samples,
    std::span<double const> percentiles, std::uint64_t blockGasUsed)
{
    std::vector<u256> row(percentiles.size(), 0);
    if (samples.empty() || blockGasUsed == 0)
    {
        return row;
    }
    std::stable_sort(samples.begin(), samples.end(),
        [](RewardSample const& left, RewardSample const& right) { return left.tip < right.tip; });
    std::size_t index = 0;
    auto sumGasUsed = samples[0].gasUsed;
    for (std::size_t i = 0; i < percentiles.size(); ++i)
    {
        // uint64(float64(gasUsed) * p / 100), as geth truncates it.
        auto const scaled = static_cast<double>(blockGasUsed) * percentiles[i] / 100.0;
        auto const threshold =
            scaled >= static_cast<double>(std::numeric_limits<std::uint64_t>::max()) ?
                std::numeric_limits<std::uint64_t>::max() :
                static_cast<std::uint64_t>(scaled);
        while (sumGasUsed < threshold && index + 1 < samples.size())
        {
            sumGasUsed += samples[++index].gasUsed;
        }
        row[i] = samples[index].tip;
    }
    return row;
}

u256 bcos::rpc::nextOpBaseFee(protocol::BlockHeader const& parent)
{
    auto const extra = parent.extraData();
    if (!extra.empty())
    {
        auto const jovian = extra.size() == engine::c_jovianExtraDataBytes &&
                            extra[0] == engine::c_jovianExtraDataVersion;
        return engine::calcOpBaseFee(parent, jovian);
    }
    auto const childIsCanyon = parent.withdrawalsRoot().has_value();
    return engine::calcOpBaseFeeFromFields(parent.gasLimit(), parent.gasUsed(),
        parent.baseFee().value_or(0), parent.blobGasUsed(), {}, /*parentIsHolocene=*/false,
        /*parentIsJovian=*/false,
        childIsCanyon ? engine::c_eip1559DenominatorCanyon : engine::c_eip1559DenominatorBedrock,
        engine::c_eip1559ElasticityCanyon);
}

u256 bcos::rpc::nextEthBaseFee(protocol::BlockHeader const& parent)
{
    // Standard London EIP-1559 (elasticity 2, denominator 8): the pure-Ethereum lane carries no
    // OP extraData, so the fixed pair applies and blobGasUsed does not meter the base fee (it
    // prices blobs through excessBlobGas). Reuse the shared EIP-1559 core with
    // parentIsHolocene/parentIsJovian both false so no extraData is read.
    //
    // A parent with no base fee has not run a fee-bearing block yet. geth's
    // eip1559.CalcBaseFee does not scale a zero base: when it is unset (the pre-London shape)
    // it answers params.InitialBaseFee, 1 gwei. Doing the arithmetic from 0 instead would
    // answer 0 or 1 wei, which no client expects.
    if (!parent.baseFee().has_value())
        return u256{c_initialBaseFee};
    // A parent with no gas limit has no fee history either (genesis of a chain that has not
    // produced a payload, or a ledger whose header is not fully populated). The OP core rejects
    // gasLimit / elasticity == 0 as invalid encoding; geth answers InitialBaseFee for the same
    // "no fee data yet" case, so do that instead of failing the request.
    if (parent.gasLimit() == 0)
        return u256{c_initialBaseFee};
    return engine::calcOpBaseFeeFromFields(parent.gasLimit(), parent.gasUsed(), *parent.baseFee(),
        /*parentBlobGasUsed=*/std::nullopt, /*parentExtraData=*/{},
        /*parentIsHolocene=*/false, /*parentIsJovian=*/false,
        /*fallbackDenominator=*/8, /*fallbackElasticity=*/2);
}

uint64_t bcos::rpc::blobBaseFeeUpdateFraction(
    std::optional<protocol::BlobScheduleConfig> const& stamped,
    protocol::BlobForkTimes const& forks, std::optional<evmc_revision> rev,
    uint64_t timestampSeconds)
{
    if (stamped && stamped->maxBlobs != 0)
    {
        return stamped->baseFeeUpdateFraction;
    }
    if (auto const resolved = protocol::blobScheduleForTimestamp(forks, timestampSeconds);
        resolved.maxBlobs != 0)
    {
        return resolved.baseFeeUpdateFraction;
    }
    // Last resort: a revision some caller happens to have for the block.
    if (rev.has_value())
    {
        if (*rev >= EVMC_PRAGUE)
            return protocol::PRAGUE_BLOB_SCHEDULE.baseFeeUpdateFraction;
        if (*rev == EVMC_CANCUN)
            return protocol::CANCUN_BLOB_SCHEDULE.baseFeeUpdateFraction;
    }
    return 0;
}

task::Task<Json::Value> bcos::rpc::buildOpFeeHistory(ledger::LedgerInterface& ledger,
    protocol::BlockNumber newest, protocol::BlockNumber head, std::uint64_t blockCount,
    std::vector<double> const& percentiles, bool isOpStackLane,
    protocol::BlobForkTimes const& blobForks)
{
    Json::Value result(Json::objectValue);
    result["oldestBlock"] = "0x0";
    if (blockCount == 0)
    {
        co_return result;
    }
    if (newest > head)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(
            InvalidParams, "request beyond head block: requested " + std::to_string(newest) +
                               ", head " + std::to_string(head)));
    }
    auto const blocks = std::min<std::uint64_t>(
        {blockCount, c_maxFeeHistoryBlocks, static_cast<std::uint64_t>(newest) + 1});
    auto const oldest = newest + 1 - static_cast<protocol::BlockNumber>(blocks);
    result["oldestBlock"] = toQuantity(oldest);

    auto const wantRewards = !percentiles.empty();
    Json::Value baseFees(Json::arrayValue);
    Json::Value ratios(Json::arrayValue);
    Json::Value rewards(Json::arrayValue);
    Json::Value blobFees(Json::arrayValue);
    Json::Value blobRatios(Json::arrayValue);
    protocol::BlockHeader::Ptr last;
    for (auto number = oldest; number <= newest; ++number)
    {
        auto const block = co_await ledger::getBlockData(ledger, number,
            ledger::HEADER | (wantRewards ? (ledger::TRANSACTIONS | ledger::RECEIPTS) : 0));
        last = block->blockHeader();
        auto const baseFee = protocol::blockBaseFee(*last);
        baseFees.append(toQuantity(baseFee));
        ratios.append(gasUsedRatio(*last));
        if (isOpStackLane)
        {
            // OP carries no blob schedule: minBlobGasPrice is 1 when the header has
            // excessBlobGas (Ecotone+) and 0 otherwise, and MaxBlobGasPerBlock is 0.
            blobFees.append(last->excessBlobGas() ? "0x1" : "0x0");
            blobRatios.append(0.0);
        }
        else
        {
            // Ethereum lane: the same EIP-4844 pricing eth_blobBaseFee answers, from the same
            // fraction resolver, so one artifact cannot have two different blob base fees.
            auto const timestampSeconds = static_cast<uint64_t>(last->timestamp() / 1000);
            auto const fraction =
                blobBaseFeeUpdateFraction(std::nullopt, blobForks, std::nullopt, timestampSeconds);
            if (fraction != 0 && last->excessBlobGas().has_value())
            {
                blobFees.append(toQuantity(
                    protocol::fakeExponential(u256{1}, *last->excessBlobGas(), u256{fraction})));
            }
            else
            {
                blobFees.append("0x0");
            }
            // blobGasUsedRatio = blobGasUsed / MaxBlobGasPerBlock for that block's schedule.
            auto const schedule = protocol::blobScheduleForTimestamp(blobForks, timestampSeconds);
            auto const maxBlobGas = u256{schedule.maxBlobs} * u256{protocol::BLOB_GAS_PER_BLOB};
            if (maxBlobGas != 0 && last->blobGasUsed().has_value())
            {
                blobRatios.append(
                    static_cast<double>(*last->blobGasUsed()) / static_cast<double>(maxBlobGas));
            }
            else
            {
                blobRatios.append(0.0);
            }
        }
        if (!wantRewards)
        {
            continue;
        }
        std::vector<RewardSample> samples;
        auto receipts = block->receipts();
        std::size_t index = 0;
        for (auto const& tx : block->transactions())
        {
            auto const gasUsed = index < receipts.size() ? receipts[index]->gasUsed() : u256(0);
            ++index;
            // A deposit is sampled with tip 0 and its own gas (op-geth calcEffectiveGasTip
            // answers 0 for DepositTxType; feehistory.go samples every transaction).
            samples.push_back({tx->isDepositTx() ? u256(0) : effectivePriorityFee(*tx, baseFee),
                saturatedUint64(gasUsed)});
        }
        Json::Value row(Json::arrayValue);
        for (auto const& tip :
            rewardPercentiles(std::move(samples), percentiles, saturatedUint64(last->gasUsed())))
        {
            row.append(toQuantity(tip));
        }
        rewards.append(std::move(row));
    }
    baseFees.append(toQuantity(isOpStackLane ? nextOpBaseFee(*last) : nextEthBaseFee(*last)));
    result["baseFeePerGas"] = std::move(baseFees);
    result["gasUsedRatio"] = std::move(ratios);
    if (wantRewards)
    {
        result["reward"] = std::move(rewards);
    }
    // op-geth always emits the blob series, on both lanes; how each entry is priced depends on
    // the lane (see the per-block branch above).
    // The next block's blob base fee is a function of the parent's excess only, so its entry
    // equals the parent's. Read that before the array is moved out below.
    auto const nextBlobFee =
        isOpStackLane ? Json::Value(last->excessBlobGas() ? "0x1" : "0x0") :
                        (blobFees.empty() ? Json::Value("0x0") : blobFees[blobFees.size() - 1]);
    result["baseFeePerBlobGas"] = std::move(blobFees);
    result["baseFeePerBlobGas"].append(nextBlobFee);
    result["blobGasUsedRatio"] = std::move(blobRatios);
    co_return result;
}
