/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include "bcos-tars-protocol/protocol/BlockHeaderImpl.h"
#include <bcos-framework/engine/OpBaseFee.h>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <functional>
#include <optional>
#include <stdexcept>

using namespace bcostars::protocol;

namespace bcos::test
{
// Three guards share one exception type; match what() to tell them apart.
static void expectThrowMessage(const std::function<void()>& call, std::string_view expectedText)
{
    bool threw = false;
    try
    {
        call();
    }
    catch (std::invalid_argument const& e)
    {
        threw = true;
        BOOST_CHECK_MESSAGE(std::string_view(e.what()).find(expectedText) != std::string_view::npos,
            "expected \"" << expectedText << "\" in what(): " << e.what());
    }
    BOOST_CHECK_MESSAGE(
        threw, "expected std::invalid_argument containing \"" << expectedText << "\"");
}

namespace
{
// Build a parent header carrying the OP-Stack 1559 parameters in extraData:
//   Holocene: version(1) || denominator(u32 BE) || elasticity(u32 BE)          = 9 bytes
//   Jovian:   ... + minBaseFee(u64 BE)                                          = 17 bytes
// Passing an empty vector leaves the parameters defaulted (8/2) inside calcOpBaseFee.
BlockHeaderImpl makeParent(bcos::u256 gasLimit, bcos::u256 gasUsed, bcos::u256 baseFee,
    bcos::bytes extraData, std::optional<bcos::u256> blobGasUsed = std::nullopt)
{
    BlockHeaderImpl header;
    header.setGasLimit(gasLimit);
    header.setGasUsed(gasUsed);
    header.setBaseFee(baseFee);
    header.setExtraData(std::move(extraData));
    if (blobGasUsed.has_value())
    {
        header.setBlobGasUsed(*blobGasUsed);
    }
    return header;
}

// Holocene parameters: elasticity 2, denominator 8.
bcos::bytes holoceneParams()
{
    return {0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x02};
}

// Jovian parameters: Holocene tail plus an 8-byte minBaseFee floor.
bcos::bytes jovianParams(uint64_t minBaseFee)
{
    bcos::bytes out = holoceneParams();
    for (int i = 7; i >= 0; --i)
    {
        out.push_back(static_cast<bcos::byte>((minBaseFee >> (i * 8)) & 0xff));
    }
    return out;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(CalcOpBaseFeeTest)

// gasUsed == gasTarget: the fee is returned unchanged, whatever the extraData width.
BOOST_AUTO_TEST_CASE(ExactTargetReturnsParentBaseFee)
{
    auto const parent = makeParent(bcos::u256(30'000'000), bcos::u256(15'000'000),
        bcos::u256(1'000'000'000), holoceneParams());
    BOOST_CHECK_EQUAL(bcos::engine::calcOpBaseFee(parent, false), bcos::u256(1'000'000'000));

    // Same rule with the Jovian path (the DA footprint cannot push a target match over).
    auto const jovian = makeParent(
        bcos::u256(30'000'000), bcos::u256(15'000'000), bcos::u256(1'000'000'000), jovianParams(0));
    BOOST_CHECK_EQUAL(bcos::engine::calcOpBaseFee(jovian, true), bcos::u256(1'000'000'000));
}

// Over target: baseFee += max(1, parentBaseFee * delta / gasTarget / denominator).
BOOST_AUTO_TEST_CASE(OverTargetIncreasesByDeltaFee)
{
    // delta = 5M; 1e9 * 5e6 / 15e6 / 8 = 41,666,666 (integer division at both steps).
    auto const parent = makeParent(bcos::u256(30'000'000), bcos::u256(20'000'000),
        bcos::u256(1'000'000'000), holoceneParams());
    BOOST_CHECK_EQUAL(bcos::engine::calcOpBaseFee(parent, false), bcos::u256(1'041'666'666));
}

// Under target: baseFee -= parentBaseFee * delta / gasTarget / denominator.
BOOST_AUTO_TEST_CASE(UnderTargetDecreasesByDeltaFee)
{
    auto const parent = makeParent(bcos::u256(30'000'000), bcos::u256(10'000'000),
        bcos::u256(1'000'000'000), holoceneParams());
    BOOST_CHECK_EQUAL(bcos::engine::calcOpBaseFee(parent, false), bcos::u256(958'333'334));
}

// A parent without the 9-byte extraData tail gets the Holocene defaults (8/2) — the
// RPC can observe foreign/malformed headers and must not read out of bounds.
BOOST_AUTO_TEST_CASE(ShortExtraDataUsesHoloceneDefaults)
{
    auto const parent =
        makeParent(bcos::u256(30'000'000), bcos::u256(24'000'000), bcos::u256(2'000'000'000), {});
    // delta = 9M; 2e9 * 9e6 / 15e6 / 8 = 150,000,000.
    BOOST_CHECK_EQUAL(bcos::engine::calcOpBaseFee(parent, false), bcos::u256(2'150'000'000));
}

// Jovian minBaseFee floor: a decrease that would land below the floor is clamped up.
BOOST_AUTO_TEST_CASE(JovianMinBaseFeeFloorsTheResult)
{
    // Unfloored decrease would be 50e6 - (50e6 * 5e6 / 15e6 / 8 = 2,083,333) = 47,916,667.
    auto const parent = makeParent(bcos::u256(30'000'000), bcos::u256(10'000'000),
        bcos::u256(50'000'000), jovianParams(100'000'000));
    BOOST_CHECK_EQUAL(bcos::engine::calcOpBaseFee(parent, true), bcos::u256(100'000'000));

    // A decrease that stays above the floor is not touched.
    auto const above = makeParent(bcos::u256(30'000'000), bcos::u256(10'000'000),
        bcos::u256(1'000'000'000), jovianParams(100'000'000));
    BOOST_CHECK_EQUAL(bcos::engine::calcOpBaseFee(above, true), bcos::u256(958'333'334));
}

// Jovian DA footprint: when blobGasUsed exceeds gasUsed, the DA footprint meters the fee
// (an under-target gasUsed alone would have decreased the fee instead).
BOOST_AUTO_TEST_CASE(JovianDABlobGasUsedMetersTheIncrease)
{
    auto const parent = makeParent(bcos::u256(30'000'000), bcos::u256(5'000'000),
        bcos::u256(1'000'000'000), jovianParams(0), bcos::u256(20'000'000));
    // gasMetered = 20M (blob), delta = 5M — same arithmetic as OverTargetIncreasesByDeltaFee.
    BOOST_CHECK_EQUAL(bcos::engine::calcOpBaseFee(parent, true), bcos::u256(1'041'666'666));

    // The blob slot only counts under Jovian; the same header is a plain Holocene decrease:
    // delta = 15M - 5M = 10M; 1e9 * 1e7 / 15e6 / 8 = 83,333,333; 1e9 - 83,333,333.
    BOOST_CHECK_EQUAL(bcos::engine::calcOpBaseFee(parent, false), bcos::u256(916'666'667));
}

// Increase from a zero base fee must floor the increment to 1 (op-geth never emits 0).
BOOST_AUTO_TEST_CASE(IncreaseFromZeroBaseFeeYieldsOne)
{
    auto const parent =
        makeParent(bcos::u256(30'000'000), bcos::u256(20'000'000), bcos::u256(0), holoceneParams());
    BOOST_CHECK_EQUAL(bcos::engine::calcOpBaseFee(parent, false), bcos::u256(1));
}

// A decrease from a minimal base fee truncates to zero delta and keeps the parent fee
// (denominator-8 integer division). This is NOT a floor on the decrease arm itself: with a
// unit denominator the delta need not truncate and the result can reach 0 — see the
// DecreaseWithUnitDenominatorReachesZero fixture below (#5496 finding AQ).
BOOST_AUTO_TEST_CASE(DecreaseFromMinimalBaseFeeTruncatesToZero)
{
    auto const parent =
        makeParent(bcos::u256(30'000'000), bcos::u256(0), bcos::u256(1), holoceneParams());
    // deltaFee = 1 * 15e6 / 15e6 / 8 = 0 (integer division); result stays 1.
    BOOST_CHECK_EQUAL(bcos::engine::calcOpBaseFee(parent, false), bcos::u256(1));
}

// The decrease arm CAN reach 0: denominator=1 is governance-reachable (the parameter
// decoder only rejects 0), and with a 1-wei parent fee the un-truncated delta consumes it
// entirely. The Holocene arm has no minBaseFee floor, so the old "never drops below 1 on a
// decrease" invariant was false (#5496 finding AQ).
BOOST_AUTO_TEST_CASE(DecreaseWithUnitDenominatorReachesZero)
{
    bcos::bytes const unitDenominator{0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02};
    auto const parent =
        makeParent(bcos::u256(30'000'000), bcos::u256(0), bcos::u256(1), unitDenominator);
    // deltaFee = 1 * 15e6 / 15e6 / 1 = 1; deltaFee >= parentBaseFee -> result 0.
    BOOST_CHECK_EQUAL(bcos::engine::calcOpBaseFee(parent, false), bcos::u256(0));
}

BOOST_AUTO_TEST_CASE(InvalidFeeParametersFailClosed)
{
    auto zeroDenominator = holoceneParams();
    std::fill(zeroDenominator.begin() + 1, zeroDenominator.begin() + 5, 0);
    auto const denominatorParent = makeParent(bcos::u256(30'000'000), bcos::u256(20'000'000),
        bcos::u256(1'000'000'000), std::move(zeroDenominator));
    expectThrowMessage([&] { (void)bcos::engine::calcOpBaseFee(denominatorParent, false); },
        "zero denominator/elasticity");

    auto zeroElasticity = holoceneParams();
    std::fill(zeroElasticity.begin() + 5, zeroElasticity.end(), 0);
    auto const elasticityParent = makeParent(bcos::u256(30'000'000), bcos::u256(20'000'000),
        bcos::u256(1'000'000'000), std::move(zeroElasticity));
    expectThrowMessage([&] { (void)bcos::engine::calcOpBaseFee(elasticityParent, false); },
        "zero denominator/elasticity");

    auto const zeroTargetParent =
        makeParent(bcos::u256(1), bcos::u256(1), bcos::u256(1'000'000'000), holoceneParams());
    expectThrowMessage(
        [&] { (void)bcos::engine::calcOpBaseFee(zeroTargetParent, false); }, "zero gas target");
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
