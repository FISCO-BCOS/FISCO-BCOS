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
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>

using namespace bcostars::protocol;

namespace bcos::test
{
// The fee helpers' guards all throw the domain-typed InvalidEngineEncoding, so match
// what() rather than the type: the message is what distinguishes one guard from another.
static void expectThrowMessage(const std::function<void()>& call, std::string_view expectedText)
{
    bool threw = false;
    try
    {
        call();
    }
    catch (std::exception const& e)
    {
        threw = true;
        BOOST_CHECK_MESSAGE(std::string_view(e.what()).find(expectedText) != std::string_view::npos,
            "expected \"" << expectedText << "\" in what(): " << e.what());
    }
    BOOST_CHECK_MESSAGE(threw, "expected an exception containing \"" << expectedText << "\"");
}

namespace
{
// Build a parent header carrying the OP-Stack 1559 parameters in extraData:
//   Holocene: version(1) || denominator(u32 BE) || elasticity(u32 BE)          = 9 bytes
//   Jovian:   ... + minBaseFee(u64 BE)                                          = 17 bytes
// extraData must be the Holocene (9) or Jovian (17) layout; short/zero params fail closed.
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

// Jovian parameters: Jovian version byte (0x01), Holocene denominator/elasticity tail,
// plus an 8-byte minBaseFee floor — the layout the engine stamps and validates (finding S4).
bcos::bytes jovianParams(uint64_t minBaseFee)
{
    bcos::bytes out = holoceneParams();
    out[0] = 0x01;
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
    auto const jovian = makeParent(bcos::u256(30'000'000), bcos::u256(15'000'000),
        bcos::u256(1'000'000'000), jovianParams(0), bcos::u256(15'000'000));
    BOOST_CHECK_EQUAL(bcos::engine::calcOpBaseFee(jovian, true), bcos::u256(1'000'000'000));
}

// Finding BT: the exact-target arm is NOT exempt from the Jovian minBaseFee floor — a
// parent whose base fee sits below a raised floor must clamp, so the quote never falls
// below the protocol floor.
BOOST_AUTO_TEST_CASE(ExactTargetStillClampsToJovianMinBaseFee)
{
    // parent base fee 100 << minBaseFee 1_000, usage exactly at target.
    auto const belowFloor = makeParent(bcos::u256(30'000'000), bcos::u256(15'000'000),
        bcos::u256(100), jovianParams(1'000), bcos::u256(15'000'000));
    BOOST_CHECK_EQUAL(bcos::engine::calcOpBaseFee(belowFloor, true), bcos::u256(1'000));

    // Steady state: a floor below the parent fee stays a no-op on exact target.
    auto const aboveFloor = makeParent(bcos::u256(30'000'000), bcos::u256(15'000'000),
        bcos::u256(1'000'000'000), jovianParams(1'000), bcos::u256(15'000'000));
    BOOST_CHECK_EQUAL(bcos::engine::calcOpBaseFee(aboveFloor, true), bcos::u256(1'000'000'000));
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

// R84: the version byte must match the length — 9B with 0x01 and 17B with 0x00 are
// both rejected (a regression dropping this check would stay green without these cells).
BOOST_AUTO_TEST_CASE(VersionByteMismatchIsRejected)
{
    auto badJovian = holoceneParams();
    badJovian[0] = 0x01;  // 9 bytes claiming Jovian
    auto const parent9 = makeParent(bcos::u256(30'000'000), bcos::u256(24'000'000),
        bcos::u256(2'000'000'000), std::move(badJovian));
    expectThrowMessage([&] { (void)bcos::engine::calcOpBaseFee(parent9, false); },
        "version byte does not match length");

    // 17 bytes carrying version 0x00 (Holocene claim on a Jovian-length tail).
    bcos::bytes seventeen = holoceneParams();
    seventeen[0] = 0x00;
    for (int i = 0; i < 8; ++i)
    {
        seventeen.push_back(0x00);  // minBaseFee tail, version byte stays 0x00
    }
    auto const parent17 = makeParent(bcos::u256(30'000'000), bcos::u256(24'000'000),
        bcos::u256(2'000'000'000), std::move(seventeen));
    expectThrowMessage([&] { (void)bcos::engine::calcOpBaseFee(parent17, false); },
        "version byte does not match length");
}

// Short extraData is fail-closed (no Holocene 8/2 default) so a missing parent
// tail cannot mint a silent consensus-divergent baseFee.
BOOST_AUTO_TEST_CASE(ShortExtraDataIsRejected)
{
    auto const parent =
        makeParent(bcos::u256(30'000'000), bcos::u256(24'000'000), bcos::u256(2'000'000'000), {});
    expectThrowMessage(
        [&] { (void)bcos::engine::calcOpBaseFee(parent, false); }, "9 (Holocene) or 17 (Jovian)");
}

// Holocene/Jovian N-1/N+1 lengths must fail closed (empty-only was not enough).
BOOST_AUTO_TEST_CASE(ExtraDataLengthBoundariesAreRejected)
{
    auto rejectLen = [&](std::size_t n) {
        bcos::bytes extra(n, bcos::byte{0});
        auto const parent = makeParent(
            bcos::u256(30'000'000), bcos::u256(24'000'000), bcos::u256(2'000'000'000), extra);
        expectThrowMessage([&] { (void)bcos::engine::calcOpBaseFee(parent, false); },
            "9 (Holocene) or 17 (Jovian)");
    };
    rejectLen(8);
    rejectLen(10);
    rejectLen(16);
    rejectLen(18);
}

// Jovian minBaseFee floor: a decrease that would land below the floor is clamped up.
BOOST_AUTO_TEST_CASE(JovianMinBaseFeeFloorsTheResult)
{
    // Unfloored decrease would be 50e6 - (50e6 * 5e6 / 15e6 / 8 = 2,083,333) = 47,916,667.
    auto const parent = makeParent(bcos::u256(30'000'000), bcos::u256(10'000'000),
        bcos::u256(50'000'000), jovianParams(100'000'000), bcos::u256(10'000'000));
    BOOST_CHECK_EQUAL(bcos::engine::calcOpBaseFee(parent, true), bcos::u256(100'000'000));

    // A decrease that stays above the floor is not touched.
    auto const above = makeParent(bcos::u256(30'000'000), bcos::u256(10'000'000),
        bcos::u256(1'000'000'000), jovianParams(100'000'000), bcos::u256(10'000'000));
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
// DecreaseWithUnitDenominatorReachesZero fixture below.
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
// decrease" invariant was false.
BOOST_AUTO_TEST_CASE(DecreaseWithUnitDenominatorReachesZero)
{
    bcos::bytes const unitDenominator{0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02};
    auto const parent =
        makeParent(bcos::u256(30'000'000), bcos::u256(0), bcos::u256(1), unitDenominator);
    // deltaFee = 1 * 15e6 / 15e6 / 1 = 1; deltaFee >= parentBaseFee -> result 0.
    BOOST_CHECK_EQUAL(bcos::engine::calcOpBaseFee(parent, false), bcos::u256(0));
}

BOOST_AUTO_TEST_CASE(ZeroFeeParametersAreRejected)
{
    auto zeroDenominator = holoceneParams();
    std::fill(zeroDenominator.begin() + 1, zeroDenominator.begin() + 5, 0);
    auto const denominatorParent = makeParent(bcos::u256(30'000'000), bcos::u256(20'000'000),
        bcos::u256(1'000'000'000), std::move(zeroDenominator));
    expectThrowMessage([&] { (void)bcos::engine::calcOpBaseFee(denominatorParent, false); },
        "non-zero EIP-1559 denominator and elasticity");

    auto zeroElasticity = holoceneParams();
    std::fill(zeroElasticity.begin() + 5, zeroElasticity.end(), 0);
    auto const elasticityParent = makeParent(bcos::u256(30'000'000), bcos::u256(20'000'000),
        bcos::u256(1'000'000'000), std::move(zeroElasticity));
    expectThrowMessage([&] { (void)bcos::engine::calcOpBaseFee(elasticityParent, false); },
        "non-zero EIP-1559 denominator and elasticity");

    auto const zeroTargetParent =
        makeParent(bcos::u256(1), bcos::u256(1), bcos::u256(1'000'000'000), holoceneParams());
    expectThrowMessage(
        [&] { (void)bcos::engine::calcOpBaseFee(zeroTargetParent, false); }, "zero gas target");
}

// op-geth dereferences parent.BaseFee and panics on nil; a Holocene+ parent without a
// base fee is a corrupt header — fail closed instead of silently pricing at 0.
BOOST_AUTO_TEST_CASE(MissingBaseFeeIsRejected)
{
    BlockHeaderImpl header;
    header.setGasLimit(bcos::u256(30'000'000));
    header.setGasUsed(bcos::u256(20'000'000));
    header.setExtraData(holoceneParams());  // baseFee deliberately left unset
    expectThrowMessage(
        [&] { (void)bcos::engine::calcOpBaseFee(header, false); }, "missing baseFee");
}

// op-geth's Jovian meter dereferences header.BlobGasUsed; a Jovian parent without it
// is corrupt — fail closed rather than under-counting the DA footprint. The same
// header passes on the Holocene path, which never reads the blob slot.
BOOST_AUTO_TEST_CASE(JovianMissingBlobGasUsedIsRejected)
{
    auto const parent = makeParent(
        bcos::u256(30'000'000), bcos::u256(20'000'000), bcos::u256(1'000'000'000), jovianParams(0));
    expectThrowMessage(
        [&] { (void)bcos::engine::calcOpBaseFee(parent, true); }, "missing blobGasUsed");
    BOOST_CHECK_NO_THROW((void)bcos::engine::calcOpBaseFee(parent, false));
}

// op-geth computes the delta multiply with unbounded big.Int; extreme parent headers
// must fail closed instead of wrapping mod 2^256. Increase-arm multiply: delta 3 with
// parentBaseFee 2^255 exceeds u256Max/parentBaseFee = 2.
BOOST_AUTO_TEST_CASE(OverTargetMultiplyOverflowIsRejected)
{
    auto const parent = makeParent(bcos::u256(20), bcos::u256(13), bcos::u256{1} << 255,
        holoceneParams());  // gasTarget = 20/2 = 10, delta = 3
    expectThrowMessage([&] { (void)bcos::engine::calcOpBaseFee(parent, false); },
        "delta computation overflows u256");
}

// Decrease-arm multiply: same guard on the gasTarget - gasMetered delta.
BOOST_AUTO_TEST_CASE(UnderTargetMultiplyOverflowIsRejected)
{
    auto const parent = makeParent(bcos::u256(20), bcos::u256(7), bcos::u256{1} << 255,
        holoceneParams());  // gasTarget = 10, delta = 3
    expectThrowMessage([&] { (void)bcos::engine::calcOpBaseFee(parent, false); },
        "delta computation overflows u256");
}

// The multiply guard cannot see the final add: parentBaseFee = u256Max with delta 1
// passes the multiply check but parentBaseFee + deltaFee wraps — the increase must
// fail closed there too.
BOOST_AUTO_TEST_CASE(IncreaseAddOverflowIsRejected)
{
    bcos::u256 const u256Max = ~bcos::u256(0);
    auto const parent = makeParent(
        bcos::u256(20), bcos::u256(11), u256Max, holoceneParams());  // gasTarget 10, delta 1
    expectThrowMessage(
        [&] { (void)bcos::engine::calcOpBaseFee(parent, false); }, "increase overflows u256");
}

// Built-in driver gas limit: configured value passes through, 0 falls back to 30M.
BOOST_AUTO_TEST_CASE(ResolveDriverGasLimit)
{
    using bcos::engine::c_defaultDriverGasLimit;
    using bcos::engine::resolveDriverGasLimit;
    BOOST_CHECK_EQUAL(c_defaultDriverGasLimit, std::uint64_t{30'000'000});
    BOOST_CHECK_EQUAL(resolveDriverGasLimit(0), c_defaultDriverGasLimit);
    BOOST_CHECK_EQUAL(resolveDriverGasLimit(45'000'000), std::uint64_t{45'000'000});
    BOOST_CHECK_EQUAL(resolveDriverGasLimit(1), std::uint64_t{1});
}

// The extraData shape follows the fork that produced the block, so the engine picks
// a layout from the timestamp and this checks the block against it: empty before
// Holocene, exactly 9 bytes at Holocene/Isthmus, 17 at Jovian+.
BOOST_AUTO_TEST_CASE(ValidateExtraDataByLayout)
{
    using L = bcos::engine::OpExtraDataLayout;
    BOOST_CHECK(!bcos::engine::validateOpExtraDataForLayout({}, L::Empty));
    BOOST_CHECK(
        bcos::engine::validateOpExtraDataForLayout(bcos::bytes{0x00}, L::Empty).has_value());

    bcos::bytes holocene{0x00, 0x00, 0x00, 0x00, 0xfa, 0x00, 0x00, 0x00, 0x06};
    BOOST_CHECK(!bcos::engine::validateOpExtraDataForLayout(holocene, L::Holocene9));
    BOOST_CHECK(bcos::engine::validateOpExtraDataForLayout({}, L::Holocene9).has_value());
    BOOST_CHECK(bcos::engine::validateOpExtraDataForLayout(holocene, L::Empty).has_value());

    bcos::bytes jovian = holocene;
    jovian[0] = 0x01;
    jovian.insert(jovian.end(), 8, 0x00);
    BOOST_CHECK(!bcos::engine::validateOpExtraDataForLayout(jovian, L::Jovian17));
    BOOST_CHECK(bcos::engine::validateOpExtraDataForLayout(holocene, L::Jovian17).has_value());
}

// Two clocks (op-geth CalcBaseFee): the pre-Holocene path uses the chain's configured
// EIP-1559 triple (defaults 6/50/250; denominator 50 / 250 chosen by the NEW block's time),
// while a Holocene parent switches to its own extraData. Parent: gasLimit 30M, gasUsed 20M,
// baseFee 1e9 -> gasTarget 5M, delta 15M -> 3e9/denom.
BOOST_AUTO_TEST_CASE(NextBlockBaseFeeTwoClocks)
{
    // Pre-Holocene parent: empty extraData, so the chain's triple applies.
    auto const pre =
        makeParent(bcos::u256(30'000'000), bcos::u256(20'000'000), bcos::u256(1'000'000'000), {});
    BOOST_CHECK_EQUAL(
        bcos::engine::calcOpNextBlockBaseFee(
            pre, {.parentIsHolocene = false, .parentIsJovian = false, .newBlockIsCanyon = false}),
        bcos::u256(1'060'000'000));  // denom 50
    BOOST_CHECK_EQUAL(
        bcos::engine::calcOpNextBlockBaseFee(
            pre, {.parentIsHolocene = false, .parentIsJovian = false, .newBlockIsCanyon = true}),
        bcos::u256(1'012'000'000));  // denom 250

    // Holocene activation block: it will carry 9-byte extraData itself, but its
    // PARENT is pre-Holocene, so the clock below is deliberately identical to the
    // call above — the parent, not the new block, selects the time source. The
    // caller is what must pass parentIsHolocene=false here (reth#13060).
    BOOST_CHECK_EQUAL(
        bcos::engine::calcOpNextBlockBaseFee(
            pre, {.parentIsHolocene = false, .parentIsJovian = false, .newBlockIsCanyon = true}),
        bcos::u256(1'012'000'000));

    // Holocene parent: decode the parent's 9-byte extraData (250/6) instead.
    auto const holoceneParent =
        makeParent(bcos::u256(30'000'000), bcos::u256(20'000'000), bcos::u256(1'000'000'000),
            bcos::bytes{0x00, 0x00, 0x00, 0x00, 0xfa, 0x00, 0x00, 0x00, 0x06});
    BOOST_CHECK_EQUAL(
        bcos::engine::calcOpNextBlockBaseFee(holoceneParent,
            {.parentIsHolocene = true, .parentIsJovian = false, .newBlockIsCanyon = true}),
        bcos::u256(1'012'000'000));
}

// The chain's own EIP-1559 triple, not a hardcoded OP-mainnet preset. Golden values produced by
// op-geth at pin e8800cffe calling eip1559.CalcBaseFee on THIS parent shape (gasLimit 30M,
// gasUsed 20M, baseFee 1e9 -> gasTarget 5M, delta 15M -> 3e9/denominator) with elasticity 6 and
// denominatorCanyon 250, child time 1 (pre-Canyon):
//   denominator  8  -> 1_375_000_000   (devnet.toml:42 and the C2 e2e intent both declare 8)
//   denominator 50  -> 1_060_000_000   (the legacy preset: the cases above)
//   denominator 250 -> 1_012_000_000
//   gasUsed == gasTarget, either denominator -> 1_000_000_000 (the step is zero, so NO
//   denominator is observable — which is why a gasLimit/6 parent never caught the hardcoding)
BOOST_AUTO_TEST_CASE(PreCanyonBaseFeeUsesTheChainsDenominator)
{
    auto const pre =
        makeParent(bcos::u256(30'000'000), bcos::u256(20'000'000), bcos::u256(1'000'000'000), {});

    // A denom-8 chain: the value op-geth produces for it.
    BOOST_CHECK_EQUAL(
        bcos::engine::calcOpNextBlockBaseFee(
            pre, {.parentIsHolocene = false,
                     .parentIsJovian = false,
                     .newBlockIsCanyon = false,
                     .eip1559 = {.elasticity = 6, .denominator = 8, .denominatorCanyon = 250}}),
        bcos::u256(1'375'000'000));

    // Same parent, declared legacy triple: unchanged behaviour (the compatibility pin).
    BOOST_CHECK_EQUAL(bcos::engine::calcOpNextBlockBaseFee(
                          pre, {.parentIsHolocene = false,
                                   .parentIsJovian = false,
                                   .newBlockIsCanyon = false,
                                   .eip1559 = bcos::engine::kLegacyOpEip1559Params}),
        bcos::u256(1'060'000'000));

    // Control: at the exact gas target the step is zero, so no denominator can show up.
    auto const atTarget =
        makeParent(bcos::u256(30'000'000), bcos::u256(5'000'000), bcos::u256(1'000'000'000), {});
    for (auto const denominator : {8U, 50U})
    {
        BOOST_CHECK_EQUAL(
            bcos::engine::calcOpNextBlockBaseFee(atTarget, {.parentIsHolocene = false,
                                                               .parentIsJovian = false,
                                                               .newBlockIsCanyon = false,
                                                               .eip1559 = {.elasticity = 6,
                                                                   .denominator = denominator,
                                                                   .denominatorCanyon = 250}}),
            bcos::u256(1'000'000'000));
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
