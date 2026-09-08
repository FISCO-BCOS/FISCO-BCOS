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
 * @file FeeHistoryTest.cpp
 * @brief eth_feeHistory helpers: EIP-1559 / OP base-fee prediction and DA-cap wiring.
 */

#include <bcos-rpc/jsonrpc/Common.h>
#include <bcos-rpc/web3jsonrpc/utils/FeeHistory.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
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

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
