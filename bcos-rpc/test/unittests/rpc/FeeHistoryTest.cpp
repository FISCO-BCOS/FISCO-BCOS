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
 * @file FeeHistoryTest.cpp
 * @brief eth_feeHistory helpers: EIP-1559 / OP base-fee prediction and DA-cap wiring.
 */

#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-framework/testutils/faker/FakeLedger.h>
#include <bcos-rpc/jsonrpc/Common.h>
#include <bcos-rpc/web3jsonrpc/utils/FeeHistory.h>
#include <bcos-tars-protocol/protocol/BlockFactoryImpl.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-task/Wait.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::rpc;
using namespace bcostars::protocol;

namespace bcos::test
{
namespace
{
BlockHeaderImpl makeLondonParent(bcos::u256 gasLimit, bcos::u256 gasUsed, bcos::u256 baseFee)
{
    BlockHeaderImpl header;
    header.setEthBlockVersion(bcos::protocol::EthBlockVersion::LONDON);
    header.setGasLimit(gasLimit);
    header.setGasUsed(gasUsed);
    header.setBaseFee(baseFee);
    return header;
}

/// OP-Stack headers: ethBlockVersion stays NON_ETH (rebuildOpEthHeader deliberately
/// skips setEthBlockVersion) but every Shanghai+ fork field is stamped.
BlockHeaderImpl makeOpHeader(bcos::u256 baseFee)
{
    BlockHeaderImpl header;
    header.setEthBlockVersion(bcos::protocol::EthBlockVersion::NON_ETH);
    header.setWithdrawalsRoot(bcos::crypto::HashType(1));
    header.setBaseFee(baseFee);
    return header;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(FeeHistoryTest)

BOOST_AUTO_TEST_CASE(opHeaderBaseFeeIsReportedNotZeroed)
{
    // Regression: a NON_ETH short-circuit in the base-fee read made eth_feeHistory report
    // 0x0 for every OP block (and compute rewards against a zero base fee), because OP
    // headers are NON_ETH yet carry a real base fee.
    auto const opHeader = makeOpHeader(7);
    BOOST_CHECK_EQUAL(blockBaseFee(opHeader), bcos::u256(7));

    // A native FISCO NON_ETH header (no withdrawalsRoot / baseFee) still reads 0.
    BlockHeaderImpl nativeHeader;
    nativeHeader.setEthBlockVersion(bcos::protocol::EthBlockVersion::NON_ETH);
    BOOST_CHECK_EQUAL(blockBaseFee(nativeHeader), bcos::u256(0));

    // Pre-London Eth header: the field is not part of the shape, so 0 even if set.
    BlockHeaderImpl preLondon;
    preLondon.setEthBlockVersion(bcos::protocol::EthBlockVersion::PRE_LONDON);
    preLondon.setBaseFee(bcos::u256(9));
    BOOST_CHECK_EQUAL(blockBaseFee(preLondon), bcos::u256(0));

    // London Eth header: the header's own value.
    auto const london = makeLondonParent(30'000'000, 15'000'000, 11);
    BOOST_CHECK_EQUAL(blockBaseFee(london), bcos::u256(11));
}

BOOST_AUTO_TEST_CASE(ethNextBaseFeeHoldsAtTarget)
{
    auto parent = makeLondonParent(30'000'000, 15'000'000, 1'000'000'000);
    auto const next = calcEthNextBaseFee(parent);
    BOOST_CHECK_EQUAL(next, bcos::u256(1'000'000'000));
}

BOOST_AUTO_TEST_CASE(opNextBaseFeeFallsBackWithoutHoloceneExtraData)
{
    auto parent = makeLondonParent(30'000'000, 0, 1'000'000'000);
    auto const next = calcOpNextBaseFee(parent);
    BOOST_CHECK_EQUAL(next, bcos::u256(1'000'000'000));
}

/// A Holocene-shaped parent is NOT genesis-adjacent: calcOpBaseFee's own fail-closed
/// errors must surface instead of being swallowed into the fallback. The previous
/// catch (...) returned the parent fee here, hiding a corrupt header.
BOOST_AUTO_TEST_CASE(opNextBaseFeeThrowsOnHoloceneShapedParentMissingBaseFee)
{
    // Same shape as makeLondonParent but with baseFee left UNENGAGED (setBaseFee(0) would
    // engage it): calcOpBaseFee must throw, not degrade to the parent fee.
    BlockHeaderImpl parent;
    parent.setEthBlockVersion(bcos::protocol::EthBlockVersion::LONDON);
    parent.setGasLimit(30'000'000);
    parent.setGasUsed(0);
    // 9-byte Holocene extraData: version 0x00 || denominator(250) || elasticity(6).
    bcos::bytes extra;
    extra.push_back(0x00);
    for (int shift : {24, 16, 8, 0})
        extra.push_back(static_cast<bcos::byte>((250U >> shift) & 0xff));
    for (int shift : {24, 16, 8, 0})
        extra.push_back(static_cast<bcos::byte>((6U >> shift) & 0xff));
    parent.setExtraData(extra);
    // InvalidEngineEncoding carries several distinct fail-closed messages, so pin the reason:
    // a wrong guard that throws the same type for another shape must not pass.
    BOOST_CHECK_EXCEPTION((void)calcOpNextBaseFee(parent), bcos::engine::InvalidEngineEncoding,
        [](bcos::engine::InvalidEngineEncoding const& e) {
            return std::string(e.what()).find("missing baseFee") != std::string::npos;
        });
}

BOOST_AUTO_TEST_CASE(effectivePriorityFeePerGasBranches)
{
    using bcostars::protocol::TransactionImpl;
    auto feeTx = [](std::string priority, std::string gasPrice, std::string maxFee) {
        TransactionImpl tx;
        tx.mutableInner().data.maxPriorityFeePerGas = std::move(priority);
        tx.mutableInner().data.gasPrice = std::move(gasPrice);
        tx.mutableInner().data.maxFeePerGas = std::move(maxFee);
        return tx;
    };
    // Explicit priority below the cap: the tip is the priority.
    BOOST_CHECK_EQUAL(
        effectivePriorityFeePerGas(feeTx("0x2", "", "0xa"), bcos::u256(1)), bcos::u256(2));
    // Priority above maxFee - baseFee: capped by the fee cap.
    BOOST_CHECK_EQUAL(
        effectivePriorityFeePerGas(feeTx("0x9", "", "0x5"), bcos::u256(1)), bcos::u256(4));
    // Legacy gasPrice above baseFee, no priority: tip = gasPrice - baseFee.
    BOOST_CHECK_EQUAL(
        effectivePriorityFeePerGas(feeTx("", "0x7", ""), bcos::u256(2)), bcos::u256(5));
    // gasPrice at or below baseFee: no tip.
    BOOST_CHECK_EQUAL(
        effectivePriorityFeePerGas(feeTx("", "0x1", ""), bcos::u256(2)), bcos::u256(0));
}

BOOST_AUTO_TEST_CASE(pickRewardPercentilesKeepsZeroTipSamples)
{
    // geth keeps zero-tip transactions in the weighted sample set (it sorts every tx in the
    // block); the percentile boundary can land on one, so the helper must not filter them.
    std::vector<GasWeightedPriorityFee> samples{{0, 21'000}, {5, 21'000}};
    auto rewards = pickRewardPercentiles(samples, std::vector<double>{10.0});
    BOOST_REQUIRE_EQUAL(rewards.size(), 1);
    BOOST_CHECK_EQUAL(rewards[0], 0);
}

BOOST_AUTO_TEST_CASE(pickRewardPercentilesGasWeighted)
{
    // Equal gas: geth walks cumulative gas, not tx-count index (75th -> highest tip).
    std::vector<GasWeightedPriorityFee> equalGas{{1, 21'000}, {3, 21'000}, {9, 21'000}};
    std::vector<double> percentiles{25.0, 50.0, 75.0};
    auto rewards = pickRewardPercentiles(equalGas, percentiles);
    BOOST_REQUIRE_EQUAL(rewards.size(), 3);
    BOOST_CHECK_EQUAL(rewards[0], 1);
    BOOST_CHECK_EQUAL(rewards[1], 3);
    BOOST_CHECK_EQUAL(rewards[2], 9);

    // Unequal gas: the 50th percentile lands on the high-gas low-tip tx.
    std::vector<GasWeightedPriorityFee> skewed{{1, 10'000}, {3, 90'000}};
    std::vector<double> halfPercentile{50.0};
    auto skewedRewards = pickRewardPercentiles(skewed, halfPercentile);
    BOOST_REQUIRE_EQUAL(skewedRewards.size(), 1);
    BOOST_CHECK_EQUAL(skewedRewards[0], 3);
}

BOOST_AUTO_TEST_CASE(pickRewardPercentilesSingleSweepMatchesBoundaries)
{
    // Boundaries exactly on a cumulative sum, plus 0 / 100: the prefix-sum + binary-search
    // rewrite must return the same samples the previous per-percentile rescan did.
    // gas 10k / 20k / 70k (total 100k) -> cumulative 10k / 30k / 100k.
    std::vector<GasWeightedPriorityFee> samples{{7, 10'000}, {5, 20'000}, {3, 70'000}};
    std::vector<double> percentiles{0.0, 10.0, 30.0, 30.1, 99.0, 100.0};
    auto rewards = pickRewardPercentiles(samples, percentiles);
    BOOST_REQUIRE_EQUAL(rewards.size(), 6);
    BOOST_CHECK_EQUAL(rewards[0], 7);  // 0% -> first sample
    BOOST_CHECK_EQUAL(rewards[1], 7);  // threshold 10k == first cumulative sum
    BOOST_CHECK_EQUAL(rewards[2], 5);  // threshold 30k == second cumulative sum
    BOOST_CHECK_EQUAL(rewards[3], 3);  // just past the second boundary
    BOOST_CHECK_EQUAL(rewards[4], 3);
    BOOST_CHECK_EQUAL(rewards[5], 3);  // 100% -> last sample

    // All-zero gas weights collapse to zero rewards instead of dividing by zero.
    std::vector<GasWeightedPriorityFee> zeroGas{{1, 0}, {2, 0}};
    auto zeroRewards = pickRewardPercentiles(zeroGas, std::vector<double>{25.0, 75.0});
    BOOST_REQUIRE_EQUAL(zeroRewards.size(), 2);
    BOOST_CHECK_EQUAL(zeroRewards[0], 0);
    BOOST_CHECK_EQUAL(zeroRewards[1], 0);
}

/// End-to-end weighting pin: rewards must follow the receipt's gasUsed, not the tx's
/// gasLimit. Two txs whose two weightings disagree: A has the low tip and a huge gasLimit
/// but consumes almost nothing; B has the higher tip and consumes its full limit.
/// gasUsed-weighting puts the 50th percentile on B; gasLimit-weighting would put it on A.
BOOST_AUTO_TEST_CASE(buildFeeHistoryWeightsRewardsByReceiptGasUsed)
{
    auto suite =
        std::make_shared<bcos::crypto::CryptoSuite>(std::make_shared<bcos::crypto::Keccak256>(),
            std::make_shared<bcos::crypto::Secp256k1Crypto>(), nullptr);
    auto blockFactory = std::make_shared<bcostars::protocol::BlockFactoryImpl>(suite,
        std::make_shared<bcostars::protocol::BlockHeaderFactoryImpl>(suite),
        std::make_shared<bcostars::protocol::TransactionFactoryImpl>(suite),
        std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(suite));
    auto ledger = std::make_shared<bcos::test::FakeLedger>(blockFactory, /*blocks=*/2, 0, 0);

    auto block = ledger->ledgerData()[1];
    BOOST_REQUIRE(block);
    auto makeTx = [&](std::string priority, int64_t gasLimit) {
        // version 1: the V0 branch of the factory zeroes every fee field.
        return blockFactory->transactionFactory()->createTransaction(1,
            "0x1234567890123456789012345678901234567890", bcos::bytes{}, "0x1", 100, "chain0",
            "group0", 0, /*abi=*/"", /*value=*/"0x0", /*gasPrice=*/"", gasLimit,
            /*maxFeePerGas=*/"0x4a817c800", std::move(priority));
    };
    auto makeReceipt = [&](bcos::u256 gasUsed) {
        return blockFactory->receiptFactory()->createReceipt(gasUsed,
            "0x1234567890123456789012345678901234567890", {}, /*status=*/0, bcos::bytesConstRef{},
            /*blockNumber=*/1);
    };
    // A: tip 1, gasLimit 1'000'000, gasUsed 10'000.
    block->appendTransaction(makeTx("0x1", 1'000'000));
    block->appendReceipt(makeReceipt(bcos::u256(10'000)));
    // B: tip 2, gasLimit 21'000, gasUsed 21'000.
    block->appendTransaction(makeTx("0x2", 21'000));
    block->appendReceipt(makeReceipt(bcos::u256(21'000)));

    auto result = bcos::task::syncWait(buildFeeHistory(*ledger, /*newestBlock=*/1,
        /*blockCount=*/1, std::vector<double>{50.0}, /*opStackMode=*/false));
    BOOST_REQUIRE(result.isMember("reward"));
    BOOST_REQUIRE_EQUAL(result["reward"].size(), 1U);
    BOOST_REQUIRE_EQUAL(result["reward"][0U].size(), 1U);
    // 50% of 31'000 gasUsed = 15'500: past A (10'000) and inside B -> B's tip (2).
    BOOST_CHECK_EQUAL(result["reward"][0U][0U].asString(), toQuantity(bcos::u256(2)));
}

/// The reward path loads full block bodies (txs + receipts) per block, so it is capped tighter
/// than the header-only path: geth's 1024 assumes a fee-history cache this implementation does
/// not have. An out-of-range request is rejected (op-geth's resolveBlockRange errors on
/// blocks < 1 or > maxQueryLimit) instead of being silently shortened, and blockCount 0 must
/// never answer a shape-violating empty object.
BOOST_AUTO_TEST_CASE(buildFeeHistoryRejectsOutOfRangeBlockCount)
{
    auto suite =
        std::make_shared<bcos::crypto::CryptoSuite>(std::make_shared<bcos::crypto::Keccak256>(),
            std::make_shared<bcos::crypto::Secp256k1Crypto>(), nullptr);
    auto blockFactory = std::make_shared<bcostars::protocol::BlockFactoryImpl>(suite,
        std::make_shared<bcostars::protocol::BlockHeaderFactoryImpl>(suite),
        std::make_shared<bcostars::protocol::TransactionFactoryImpl>(suite),
        std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(suite));
    auto ledger = std::make_shared<bcos::test::FakeLedger>(blockFactory, /*blocks=*/201, 0, 0);

    // Rewards: 1000 blocks is over the 128-block body-loading bound -> rejected, not shortened.
    BOOST_CHECK_THROW(bcos::task::syncWait(buildFeeHistory(*ledger, /*newestBlock=*/200,
                          /*blockCount=*/1000, std::vector<double>{50.0}, /*opStackMode=*/false)),
        JsonRpcException);

    // The 128-block bound itself is served: newest 200, 128 blocks -> oldest 200 - 127 = 73.
    auto atBound = bcos::task::syncWait(buildFeeHistory(*ledger, /*newestBlock=*/200,
        /*blockCount=*/128, std::vector<double>{50.0}, /*opStackMode=*/false));
    BOOST_CHECK_EQUAL(atBound["oldestBlock"].asString(), toQuantity(bcos::u256(73)));
    BOOST_CHECK_EQUAL(atBound["baseFeePerGas"].size(), 129U);  // 128 blocks + trailing fee

    // Headers only: 1000 is under the 1024 query limit -> served.
    auto headersOnly = bcos::task::syncWait(buildFeeHistory(*ledger, /*newestBlock=*/200,
        /*blockCount=*/1000, /*rewardPercentiles=*/{}, /*opStackMode=*/false));
    BOOST_CHECK_EQUAL(headersOnly["oldestBlock"].asString(), toQuantity(bcos::u256(0)));
    BOOST_CHECK_EQUAL(headersOnly["baseFeePerGas"].size(), 202U);  // 201 blocks + trailing fee
    BOOST_CHECK(!headersOnly.isMember("reward"));

    // Over the geth query limit, and a zero count, are both InvalidParams.
    BOOST_CHECK_THROW(
        bcos::task::syncWait(buildFeeHistory(*ledger, /*newestBlock=*/200, /*blockCount=*/1025,
            /*rewardPercentiles=*/{}, /*opStackMode=*/false)),
        JsonRpcException);
    BOOST_CHECK_THROW(
        bcos::task::syncWait(buildFeeHistory(*ledger, /*newestBlock=*/200, /*blockCount=*/0,
            /*rewardPercentiles=*/{}, /*opStackMode=*/false)),
        JsonRpcException);
}

BOOST_AUTO_TEST_CASE(pickRewardPercentilesTruncatesThresholdLikeOpGeth)
{
    // op-geth truncates the threshold to uint64 before the comparison
    // (uint64(float64(blockGasUsed) * p / 100)); with total = 101 and p = 50 that is 50, not
    // 50.5, so the sample whose cumulative gas is exactly 50 is the boundary.
    std::vector<GasWeightedPriorityFee> samples{{1, 50}, {2, 51}};
    auto rewards = pickRewardPercentiles(samples, std::vector<double>{50.0});
    BOOST_REQUIRE_EQUAL(rewards.size(), 1);
    BOOST_CHECK_EQUAL(rewards[0], 1);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
