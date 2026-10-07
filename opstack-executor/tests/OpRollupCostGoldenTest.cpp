// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// OpRollupCostGoldenTest — the externally anchored op-geth vectors of the retired
// bcos-evm RollupCostTest (deleted with the old layer in the OP rewrite), re-pinned
// on the new-layer API (opstack-executor/OpRollupCost.h). Every constant below is
// anchored outside this repo: the FastLZ lengths come from op-geth FlzCompressLen
// over the same real transaction bytes, the Bedrock/Regolith gas/fee pairs from
// op-geth core/types/rollup_cost_test.go TestBedrockL1CostFunc, and the Fjord
// empty-tx fee from op-geth's Fjord cost function (the former
// FjordL1CostEmptyTxMatches3203000).

#include <opstack-executor/OpEthReceipt.h>  // intxToBcosU256 (intx::uint256 is not Boost.Test-printable)
#include <opstack-executor/OpFeeParams.h>
#include <opstack-executor/OpForkSpec.h>
#include <opstack-executor/OpRollupCost.h>

#include <boost/test/unit_test.hpp>
#include <evmc/bytes.hpp>

#include <cstdint>
#include <vector>
#ifdef FISCO_WITH_CXX_MODULES
import bcos.utilities;
#else
#include <bcos-utilities/DataConvertUtility.h>
#endif

using namespace bcos::executor_v1::opstack;
using intx::operator""_u256;

namespace
{
// Real signed-transaction RLP bytes (formerly fixtures/opstack/*.bin). The FastLZ golden
// values below (31 / 202, from op-geth FlzCompressLen) depend on these exact bytes — do
// not edit them.
// Minimal legacy tx, empty calldata (was empty_tx.bin, 30 bytes).
const bcos::bytes kEmptyTx =
    bcos::fromHex("dd80808094095e7baea6a6c7c4c2dfeb977efac326af552d878080808080");
// EIP-1559 (type-2) contract call with ABI-encoded calldata + signature (was
// contract_call_tx.bin, 345 bytes).
const bcos::bytes kContractCallTx = bcos::fromHex(
    "02f901550a758302df1483be21b88304743f94f80e51afb613d764fa61751affd3313c190a86bb870151bd62fd"
    "12adb8e41ef24f3f000000000000000000000000000000000000000000000000000000000000006e0000000000"
    "00000000000000af88d065e77c8cc2239327c5edb3a432268e58310000000000000000000000000000000000000"
    "00000000000000000000003c1e50000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000a0000000000000000000000000000"
    "00000000000000000000000000000000000148c89ed219d02f1a5be012c689b4f5b731827bebe00000000000000"
    "0000000000c001a033fd89cb37c31b2cba46b6466e040c61fc9b2a3675a7f5f493ebd5ad77c497f8a07cdf65680"
    "e238392693019b4092f610222e71b7cec06449cb922b93b6a12744e");

OpFeeParams feeParams(uint64_t l1Base, uint64_t blobBase, uint32_t baseScalar, uint32_t blobScalar)
{
    return OpFeeParams{.l1_base_fee = intx::uint256{l1Base},
        .base_fee_scalar = baseScalar,
        .blob_base_fee_scalar = blobScalar,
        .blob_base_fee = intx::uint256{blobBase},
        .operator_fee_scalar = 0,
        .operator_fee_constant = 0};
}

evmc::bytes_view viewOf(bcos::bytes const& v)
{
    return {v.data(), v.size()};
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OpRollupCostGoldenSuite)

// op-geth FlzCompressLen over the same inputs (core/types/rollup_cost_test.go
// TestFlzCompressLen): empty -> 0; 1000 equal bytes -> 21; the two real
// envelopes -> 31 / 202.
BOOST_AUTO_TEST_CASE(FlzCompressLenMatchesOpGethVectors)
{
    BOOST_REQUIRE_EQUAL(kEmptyTx.size(), 30u);
    BOOST_REQUIRE_EQUAL(kContractCallTx.size(), 345u);
    BOOST_CHECK_EQUAL(flzCompressLen({}), 0u);
    std::vector<uint8_t> ones(1000, 0x01);
    BOOST_CHECK_EQUAL(flzCompressLen(viewOf(ones)), 21u);
    std::vector<uint8_t> zeros(1000, 0x00);
    BOOST_CHECK_EQUAL(flzCompressLen(viewOf(zeros)), 21u);
    BOOST_CHECK_EQUAL(flzCompressLen(viewOf(kEmptyTx)), 31u);
    BOOST_CHECK_EQUAL(flzCompressLen(viewOf(kContractCallTx)), 202u);
}

// op-geth Fjord L1 cost over the empty-calldata envelope (baseFee=1e9,
// blobBaseFee=1e7, baseScalar=2, blobScalar=3): flz 31 -> daScaled floors to
// 1e8 -> fee = 1e8 * (1e9*16*2 + 1e7*3) / 1e12 = 3203000.
BOOST_AUTO_TEST_CASE(FjordL1CostEmptyTxMatches3203000)
{
    BOOST_CHECK_EQUAL(intxToBcosU256(computeL1Cost(
                          feeParams(1000000000, 10000000, 2, 3), viewOf(kEmptyTx), OP_FJORD_SPEC)),
        bcos::u256(3203000));
}

// Golden values ported from op-geth core/types/rollup_cost_test.go TestBedrockL1CostFunc:
// same emptyTx bytes (kEmptyTx), same params (baseFee=1e9, overhead=50, scalar=7e6).
//   Bedrock (pre-Regolith): gasUsed=1618 = 480(calldata) + 68*16(+68 fix) + 50(overhead)
//                           fee = 1618 * 1e9 * 7e6 / 1e6 = 11326000000000
//   Regolith:               gasUsed=530  = 480 + 50; fee = 530 * 7e9 = 3710000000000
BOOST_AUTO_TEST_CASE(LegacyL1CostMatchesOpGethBedrockVectors)
{
    OpFeeParams fee{};
    fee.l1_base_fee = intx::uint256{1000000000};  // 1e9
    fee.l1_fee_overhead = intx::uint256{50};
    fee.l1_fee_scalar = intx::uint256{7000000};  // 7e6

    // Prerequisite anchor: the envelope's calldata gas (zeroes*4 + ones*16) = 480
    // (op-geth ecotoneGas over the same bytes).
    BOOST_REQUIRE_EQUAL(bedrockCalldataGasUsed(viewOf(kEmptyTx)), 480u);

    const auto bedrock = computeLegacyL1Cost(fee, viewOf(kEmptyTx), /*regolithActive=*/false);
    BOOST_CHECK_EQUAL(bedrock.gas_used, 1618u);
    BOOST_CHECK_EQUAL(intxToBcosU256(bedrock.fee), bcos::u256(11326000000000));

    const auto regolith = computeLegacyL1Cost(fee, viewOf(kEmptyTx), /*regolithActive=*/true);
    BOOST_CHECK_EQUAL(regolith.gas_used, 530u);
    BOOST_CHECK_EQUAL(intxToBcosU256(regolith.fee), bcos::u256(3710000000000));
}

BOOST_AUTO_TEST_SUITE_END()
