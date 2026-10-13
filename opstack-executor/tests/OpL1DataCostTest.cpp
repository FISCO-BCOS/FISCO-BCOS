// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// OpL1DataCostTest -- the four-way fork selection lifted out of OpPolicy::additionalMaxCost
// into opL1DataCost, pinned branch by branch to the values the inline selection produced
// (the same op-geth vectors OpRollupCostGoldenTest anchors), and opTotalRollupCost pinned to
// op-geth's pool-side TotalTxCost shape: L1 fee plus, Isthmus on, the operator fee at gas limit.

#include <opstack-executor/OpEthReceipt.h>  // intxToBcosU256
#include <opstack-executor/OpFeeParams.h>
#include <opstack-executor/OpForkSpec.h>
#include <opstack-executor/OpRollupCost.h>

#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>
#include <evmc/bytes.hpp>

using namespace bcos::executor_v1::opstack;

namespace
{
// The 30-byte legacy envelope of OpRollupCostGoldenTest (op-geth rollup_cost_test.go emptyTx).
const bcos::bytes kLegacyEnvelope =
    bcos::fromHex("dd80808094095e7baea6a6c7c4c2dfeb977efac326af552d878080808080");

evmc::bytes_view legacyEnvelope()
{
    return {kLegacyEnvelope.data(), kLegacyEnvelope.size()};
}

/// Bedrock-era slots (overhead 50, scalar 7e6) AND Ecotone slots (scalars 2 / 3, blob base fee
/// 1e7), so one parameter set drives every branch and only the spec decides which formula runs.
OpFeeParams allSlots()
{
    OpFeeParams fee{};
    fee.l1_base_fee = intx::uint256{1000000000};
    fee.l1_fee_overhead = intx::uint256{50};
    fee.l1_fee_scalar = intx::uint256{7000000};
    fee.base_fee_scalar = 2;
    fee.blob_base_fee_scalar = 3;
    fee.blob_base_fee = intx::uint256{10000000};
    fee.operator_fee_scalar = 1000000;  // x1 under Isthmus' 1e6 divisor
    fee.operator_fee_constant = 5;
    return fee;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OpL1DataCostSuite)

// Bedrock: legacy formula, pre-Regolith +68 phantom bytes -> gasUsed 1618, fee 11326000000000.
BOOST_AUTO_TEST_CASE(BedrockTakesTheLegacyFormulaWithoutRegolith)
{
    const auto cost = opL1DataCost(allSlots(), legacyEnvelope(), OP_BEDROCK_SPEC);
    BOOST_CHECK_EQUAL(intxToBcosU256(cost.fee), bcos::u256(11326000000000));
    BOOST_REQUIRE(cost.legacy_l1_gas_used.has_value());
    BOOST_CHECK_EQUAL(*cost.legacy_l1_gas_used, 1618u);
    BOOST_CHECK_EQUAL(cost.flz_len, 0u);
}

// Regolith through Delta: legacy formula, no phantom bytes -> gasUsed 530, fee 3710000000000.
BOOST_AUTO_TEST_CASE(RegolithToDeltaTakeTheLegacyFormulaWithRegolith)
{
    for (auto const* spec : {&OP_REGOLITH_SPEC, &OP_CANYON_SPEC, &OP_DELTA_SPEC})
    {
        const auto cost = opL1DataCost(allSlots(), legacyEnvelope(), *spec);
        BOOST_CHECK_EQUAL(intxToBcosU256(cost.fee), bcos::u256(3710000000000));
        BOOST_REQUIRE(cost.legacy_l1_gas_used.has_value());
        BOOST_CHECK_EQUAL(*cost.legacy_l1_gas_used, 530u);
    }
}

// Ecotone with the Ecotone slots written: the calldata-gas formula, no legacy by-products.
BOOST_AUTO_TEST_CASE(EcotoneTakesTheCalldataGasFormula)
{
    const auto fee = allSlots();
    const auto cost = opL1DataCost(fee, legacyEnvelope(), OP_ECOTONE_SPEC);
    BOOST_CHECK(cost.fee == computeL1Cost(fee, legacyEnvelope(), OP_ECOTONE_SPEC));
    // 480 calldata gas * (1e9*16*2 + 1e7*3) / 16e6 = 960900
    BOOST_CHECK_EQUAL(intxToBcosU256(cost.fee), bcos::u256(960900));
    BOOST_CHECK(!cost.legacy_l1_gas_used.has_value());
    BOOST_CHECK_EQUAL(cost.flz_len, 0u);
}

// The first Ecotone block: the fork is active but slots 3/7 still read zero, so the legacy
// formula applies with Regolith on -- checked before the Ecotone/Fjord split.
BOOST_AUTO_TEST_CASE(EcotoneWithUnsetSlotsFallsBackToRegolithLegacy)
{
    auto fee = allSlots();
    fee.base_fee_scalar = 0;
    fee.blob_base_fee_scalar = 0;
    fee.blob_base_fee = 0;
    for (auto const* spec : {&OP_ECOTONE_SPEC, &OP_FJORD_SPEC, &OP_ISTHMUS_SPEC})
    {
        const auto cost = opL1DataCost(fee, legacyEnvelope(), *spec);
        BOOST_CHECK_EQUAL(intxToBcosU256(cost.fee), bcos::u256(3710000000000));
        BOOST_REQUIRE(cost.legacy_l1_gas_used.has_value());
        BOOST_CHECK_EQUAL(*cost.legacy_l1_gas_used, 530u);
    }
}

// Fjord on: FastLZ (flz 31 over these bytes) -> the golden 3203000, and flz_len is reported.
BOOST_AUTO_TEST_CASE(FjordOnwardsTakeFastLZ)
{
    for (auto const* spec : {&OP_FJORD_SPEC, &OP_HOLOCENE_SPEC, &OP_ISTHMUS_SPEC, &OP_KARST_SPEC})
    {
        const auto cost = opL1DataCost(allSlots(), legacyEnvelope(), *spec);
        BOOST_CHECK_EQUAL(intxToBcosU256(cost.fee), bcos::u256(3203000));
        BOOST_CHECK_EQUAL(cost.flz_len, 31u);
        BOOST_CHECK(!cost.legacy_l1_gas_used.has_value());
    }
}

// op-geth NewTotalRollupCostFunc: before Isthmus the total is the L1 fee alone; from Isthmus the
// operator fee at the gas LIMIT is added (100000 * 1 + 5 here); Jovian switches its formula.
BOOST_AUTO_TEST_CASE(TotalRollupCostAddsTheOperatorFeeFromIsthmus)
{
    const auto fee = allSlots();
    constexpr uint64_t gasLimit = 100000;
    BOOST_CHECK(opTotalRollupCost(fee, legacyEnvelope(), gasLimit, OP_HOLOCENE_SPEC) ==
                intx::uint512{3203000});
    BOOST_CHECK(opTotalRollupCost(fee, legacyEnvelope(), gasLimit, OP_ISTHMUS_SPEC) ==
                intx::uint512{3203000} + intx::uint512{100005});
    BOOST_CHECK(
        opTotalRollupCost(fee, legacyEnvelope(), gasLimit, OP_JOVIAN_SPEC) ==
        intx::uint512{3203000} + intx::uint512{computeOperatorCost(fee, gasLimit, OP_JOVIAN_SPEC)});
}

BOOST_AUTO_TEST_SUITE_END()
