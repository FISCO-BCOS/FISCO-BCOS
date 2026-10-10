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
#include <bcos-framework/engine/OpEip1559Params.h>
#include <bcos-ledger/LedgerMethods.h>
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

u256 bcos::rpc::nextOpBaseFee(
    protocol::BlockHeader const& parent, std::optional<engine::OpEip1559Params> eip1559)
{
    auto const extra = parent.extraData();
    if (!extra.empty())
    {
        auto const jovian = extra.size() == engine::c_jovianExtraDataBytes &&
                            extra[0] == engine::c_jovianExtraDataVersion;
        return engine::calcOpBaseFee(parent, jovian);
    }
    // Pre-Holocene parent: the step sizes from the chain's DECLARED triple (the
    // op_eip1559_params SYS_CONFIG row written at genesis from [op_eip1559]) — the
    // same numbers the engine's zero-attribute-params substitution uses, so the
    // prediction cannot drift from what the next block actually carries. Undeclared
    // chains fall back to c_legacyOpEip1559Params (bcos-framework's
    // engine/OpEip1559Params.h — effectiveOpEip1559), which is
    // exactly the pair those chains have always priced with.
    auto const params = engine::effectiveOpEip1559(eip1559);
    // The prediction's child is the NEXT block — its exact timestamp is unknown here, so
    // the Canyon arm keys on the parent's header shape (Canyon+ parents carry a
    // withdrawalsRoot). A one-boundary-block approximation the engine and the sync
    // validator do not share: they resolve the child through the fork schedule. The
    // formula itself is the shared helper (OpBaseFee.h).
    auto const childIsCanyon = parent.withdrawalsRoot().has_value();
    return engine::calcOpBaseFeePreHolocene(parent.gasLimit(), parent.gasUsed(),
        parent.baseFee().value_or(0), childIsCanyon, params);
}

task::Task<Json::Value> bcos::rpc::buildOpFeeHistory(ledger::LedgerInterface& ledger,
    protocol::BlockNumber newest, protocol::BlockNumber head, std::uint64_t blockCount,
    std::vector<double> const& percentiles)
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
        auto const baseFee = blockBaseFee(*last);
        baseFees.append(toQuantity(baseFee));
        ratios.append(gasUsedRatio(*last));
        blobFees.append(last->excessBlobGas() ? "0x1" : "0x0");
        blobRatios.append(0.0);
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
    // The trailing prediction prices the block AFTER `newest`: feed it the chain's
    // declared triple so a pre-Holocene tail cannot price with the legacy preset on a
    // chain that declared its own pair (the same substitution the engine performs).
    auto const declared = co_await ledger::getLedgerConfig(ledger);
    baseFees.append(toQuantity(nextOpBaseFee(
        *last, declared ? declared->opEip1559Params() : std::nullopt)));
    result["baseFeePerGas"] = std::move(baseFees);
    result["gasUsedRatio"] = std::move(ratios);
    if (wantRewards)
    {
        result["reward"] = std::move(rewards);
    }
    // op-geth always emits the blob series. OP carries no blob schedule: a header with
    // excessBlobGas (Ecotone+) prices blobs at minBlobGasPrice = 1 (eip4844.CalcBlobFee), one
    // without at 0, and blobGasUsedRatio is 0 (MaxBlobGasPerBlock = 0).
    result["baseFeePerBlobGas"] = std::move(blobFees);
    result["baseFeePerBlobGas"].append(last->excessBlobGas() ? "0x1" : "0x0");
    result["blobGasUsedRatio"] = std::move(blobRatios);
    co_return result;
}
