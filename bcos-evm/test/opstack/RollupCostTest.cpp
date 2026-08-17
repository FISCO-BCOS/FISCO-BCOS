#include "TestPrinters.h"
#include <bcos-evm/opstack/OpFeeParams.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/RollupCost.h>
#include <boost/test/unit_test.hpp>
#include <evmc/hex.hpp>
#include <vector>

using namespace bcos::evm::opstack;
using intx::operator""_u256;

namespace
{
// Real signed-transaction RLP bytes (formerly fixtures/opstack/*.bin). The FastLZ golden
// values below (31 / 202, from op-geth FlzCompressLen) depend on these exact bytes — do
// not edit them.
// Minimal legacy tx, empty calldata (was empty_tx.bin, 30 bytes).
const evmc::bytes kEmptyTx =
    evmc::from_hex("dd80808094095e7baea6a6c7c4c2dfeb977efac326af552d878080808080").value();
// EIP-1559 (type-2) contract call with ABI-encoded calldata + signature (was
// contract_call_tx.bin, 345 bytes).
const evmc::bytes kContractCallTx = evmc::from_hex(
    "02f901550a758302df1483be21b88304743f94f80e51afb613d764fa61751affd3313c190a86bb870151bd62fd"
    "12adb8e41ef24f3f000000000000000000000000000000000000000000000000000000000000006e0000000000"
    "00000000000000af88d065e77c8cc2239327c5edb3a432268e58310000000000000000000000000000000000000"
    "00000000000000000000003c1e50000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000a0000000000000000000000000000"
    "00000000000000000000000000000000000148c89ed219d02f1a5be012c689b4f5b731827bebe00000000000000"
    "0000000000c001a033fd89cb37c31b2cba46b6466e040c61fc9b2a3675a7f5f493ebd5ad77c497f8a07cdf65680"
    "e238392693019b4092f610222e71b7cec06449cb922b93b6a12744e")
                                        .value();

OpFeeParams feeParams(uint64_t l1Base, uint64_t blobBase, uint32_t baseScalar, uint32_t blobScalar,
    uint32_t opScalar = 0, uint64_t opConst = 0)
{
    return OpFeeParams{.l1_base_fee = intx::uint256{l1Base},
        .base_fee_scalar = baseScalar,
        .blob_base_fee_scalar = blobScalar,
        .blob_base_fee = intx::uint256{blobBase},
        .operator_fee_scalar = opScalar,
        .operator_fee_constant = opConst};
}

evmc::bytes_view view(const std::vector<uint8_t>& v)
{
    return {v.data(), v.size()};
}
}  // namespace

BOOST_AUTO_TEST_SUITE(RollupCostSuite)

BOOST_AUTO_TEST_CASE(FlzCompressLenMatchesOpGethVectors)
{
    BOOST_CHECK_EQUAL(flzCompressLen({}), 0u);
    std::vector<uint8_t> ones(1000, 0x01);
    BOOST_CHECK_EQUAL(flzCompressLen(view(ones)), 21u);
    std::vector<uint8_t> zeros(1000, 0x00);
    BOOST_CHECK_EQUAL(flzCompressLen(view(zeros)), 21u);
    BOOST_CHECK_EQUAL(flzCompressLen(kEmptyTx), 31u);
    BOOST_CHECK_EQUAL(flzCompressLen(kContractCallTx), 202u);
}

BOOST_AUTO_TEST_CASE(EstimatedDaSizeFloorsToMinimum)
{
    BOOST_CHECK_EQUAL(estimatedDaSizeScaled(0), 100000000_u256);
    BOOST_CHECK_EQUAL(estimatedDaSizeScaled(64), 100000000_u256);
    BOOST_CHECK_EQUAL(estimatedDaSizeScaled(200), 124714400_u256);
}

BOOST_AUTO_TEST_CASE(EmptyEnvelopeIsZeroL1Cost)
{
    BOOST_CHECK_EQUAL(
        computeL1Cost(feeParams(1000000000, 10000000, 2, 3), {}, fjordConfig()), intx::uint256{0});
}

BOOST_AUTO_TEST_CASE(FjordL1CostEmptyTxMatches3203000)
{
    const evmc::bytes_view env = kEmptyTx;
    BOOST_CHECK_EQUAL(
        computeL1Cost(feeParams(1000000000, 10000000, 2, 3), env, fjordConfig()), 3203000_u256);
}

BOOST_AUTO_TEST_CASE(BedrockCalldataGasUsedNoPlus68)
{
    // 3 个零字节 + 2 个非零字节 = 3*4 + 2*16 = 44；确认没有 pre-Regolith 的 +68。
    const std::vector<uint8_t> env{0x00, 0x00, 0x00, 0x11, 0x22};
    BOOST_CHECK_EQUAL(bedrockCalldataGasUsed({env.data(), env.size()}), 44u);
    BOOST_CHECK_NE(bedrockCalldataGasUsed({env.data(), env.size()}), 44u + 68u);
}

BOOST_AUTO_TEST_CASE(EcotoneL1DiffersFromFjordSameEnvelope)
{
    OpFeeParams fee{.l1_base_fee = 1000000000_u256,
        .base_fee_scalar = 2,
        .blob_base_fee_scalar = 3,
        .blob_base_fee = 10000000_u256,
        .operator_fee_scalar = 0,
        .operator_fee_constant = 0};
    std::vector<uint8_t> env(200, 0x11);
    const auto ecotone = computeL1Cost(fee, {env.data(), env.size()}, ecotoneConfig());
    const auto fjord = computeL1Cost(fee, {env.data(), env.size()}, fjordConfig());
    BOOST_CHECK_NE(ecotone, fjord);

    // Ecotone 公式钉死：calldataGas * (l1BaseFee*16*baseScalar + blobBaseFee*blobScalar) / 16e6
    const auto calldataGas = intx::uint256{bedrockCalldataGasUsed({env.data(), env.size()})};
    const auto expected = calldataGas *
                          (fee.l1_base_fee * 16 * intx::uint256{fee.base_fee_scalar} +
                              fee.blob_base_fee * intx::uint256{fee.blob_base_fee_scalar}) /
                          intx::uint256{16'000'000};
    BOOST_CHECK_EQUAL(ecotone, expected);
}

// The whole-slot fee reads (l1_base_fee slot 1, blob_base_fee slot 7) can hold values whose
// products cross 2^256. op-geth evaluates the formula in big.Int; a wrapping uint256 evaluation
// returned a small or zero fee for such values — l1BaseFee = 2^252 made l1BaseFee*scalar*16 a
// multiple of 2^256, collapsing the fee to exactly 0 on both formula paths. Reachable only
// through the L1 attributes deposit writing astronomical slot values — defence-in-depth, same
// class as the opValidate balance cap. Expected values are independently computed with
// arbitrary-precision arithmetic; a true fee >= 2^256 saturates to uint256 max (it exceeds any
// representable balance, so validation rejects either way).
BOOST_AUTO_TEST_CASE(L1CostDoesNotWrapOnWholeSlotFeeValues)
{
    const evmc::bytes_view env = kEmptyTx;  // flz 31 -> daScaled floors to 1e8; calldataGas 480

    OpFeeParams fee{.l1_base_fee = intx::uint256{1} << 252,
        .base_fee_scalar = 1368,
        .blob_base_fee_scalar = 0,
        .blob_base_fee = intx::uint256{0},
        .operator_fee_scalar = 0,
        .operator_fee_constant = 0};
    BOOST_CHECK_EQUAL(computeL1Cost(fee, env, fjordConfig()),
        0x2305532617c1bda5119ce075f6fd21ff2e48e8a71de69ad42c3c9eecbfb15b57_u256);
    BOOST_CHECK_EQUAL(computeL1Cost(fee, env, ecotoneConfig()),
        0xa8198f1d3ed527e52157689ca18bd66277c45cbbc2b94d940789613d31b9b66_u256);

    // A true fee >= 2^256 saturates instead of wrapping to a small value.
    fee.l1_base_fee = ~intx::uint256{0};
    BOOST_CHECK_EQUAL(computeL1Cost(fee, env, fjordConfig()), ~intx::uint256{0});

    // The blob term wraps independently of the calldata term.
    const OpFeeParams blobFee{.l1_base_fee = intx::uint256{0},
        .base_fee_scalar = 0,
        .blob_base_fee_scalar = 0xffffffff,
        .blob_base_fee = intx::uint256{1} << 250,
        .operator_fee_scalar = 0,
        .operator_fee_constant = 0};
    BOOST_CHECK_EQUAL(computeL1Cost(blobFee, env, fjordConfig()), ~intx::uint256{0});
    BOOST_CHECK_EQUAL(computeL1Cost(blobFee, env, ecotoneConfig()), ~intx::uint256{0});
}

BOOST_AUTO_TEST_CASE(OperatorCostIsthmus)
{
    const auto p = feeParams(0, 0, 0, 0, /*opScalar=*/2000000, /*opConst=*/500);
    BOOST_CHECK_EQUAL(computeOperatorCost(p, 1000, isthmusConfig()),
        intx::uint256{1000ull * 2000000 / 1000000 + 500});
}

BOOST_AUTO_TEST_CASE(OperatorCostJovianUsesTimes100)
{
    const auto p = feeParams(0, 0, 0, 0, /*opScalar=*/2000000, /*opConst=*/500);
    // Isthmus: 1000*2000000/1e6 + 500 = 2500
    BOOST_CHECK_EQUAL(computeOperatorCost(p, 1000, isthmusConfig()), intx::uint256{2500});
    // Jovian: 1000*2000000*100 + 500 = 200000000500
    BOOST_CHECK_EQUAL(computeOperatorCost(p, 1000, jovianConfig()),
        intx::uint256{1000ull * 2000000ull * 100ull + 500});
    BOOST_CHECK_EQUAL(
        computeOperatorCost(p, 1000, karstConfig()), computeOperatorCost(p, 1000, jovianConfig()));
}

// 门封装:has_operator_fee=false(fjord/granite/holocene)→ 恒 0,即使 opScalar/opConst 非零
BOOST_AUTO_TEST_CASE(OperatorFeeGateOffReturnsZero)
{
    const auto p = feeParams(0, 0, 0, 0, /*opScalar=*/2000000, /*opConst=*/500);
    BOOST_CHECK_EQUAL(computeChargedOperatorCost(p, 1000, fjordConfig()), intx::uint256{0});
    BOOST_CHECK_EQUAL(computeChargedOperatorCost(p, 1000, graniteConfig()), intx::uint256{0});
    BOOST_CHECK_EQUAL(computeChargedOperatorCost(p, 1000, holoceneConfig()), intx::uint256{0});
    // 门开(isthmus/jovian)→ 透传 computeOperatorCost 公式
    BOOST_CHECK_EQUAL(computeChargedOperatorCost(p, 1000, isthmusConfig()), intx::uint256{2500});
    BOOST_CHECK_EQUAL(
        computeChargedOperatorCost(p, 1000, jovianConfig()), intx::uint256{200000000500ull});
    // karst 别名 == jovian
    BOOST_CHECK_EQUAL(computeChargedOperatorCost(p, 1000, karstConfig()),
        computeChargedOperatorCost(p, 1000, jovianConfig()));
}

BOOST_AUTO_TEST_CASE(EstimatedDaSizeDividesScaledBy1e6)
{
    BOOST_CHECK_EQUAL(estimatedDaSize({}), 0u);
    // fastlz 0 → scaled floor 100e6 → size 100
    BOOST_CHECK_EQUAL(estimatedDaSizeScaled(0) / 1000000_u256, intx::uint256{100});
    std::vector<uint8_t> empty;
    BOOST_CHECK_EQUAL(estimatedDaSize(view(empty)), 0u);
    const evmc::bytes_view env = kEmptyTx;
    const auto scaled = estimatedDaSizeScaled(flzCompressLen(env));
    BOOST_CHECK_EQUAL(estimatedDaSize(env), static_cast<uint64_t>(scaled / 1000000_u256));
}

// D-14b：FastLZ 只压一次——分解 API 与原 API 等价
BOOST_AUTO_TEST_CASE(FromFlzVariantsMatchEnvelopeVariants)
{
    std::vector<uint8_t> envBytes(120);
    for (size_t i = 0; i < envBytes.size(); ++i)
        envBytes[i] = static_cast<uint8_t>(i * 7 + 3);
    const evmc::bytes_view env{envBytes.data(), envBytes.size()};
    const auto flz = flzCompressLen(env);
    BOOST_REQUIRE_GT(flz, 0u);
    BOOST_CHECK_EQUAL(estimatedDaSizeFromFlz(flz), estimatedDaSize(env));
    BOOST_CHECK_EQUAL(estimatedDaSizeFromFlz(0), 0u);

    OpFeeParams fee{};
    fee.l1_base_fee = intx::uint256{1000};
    fee.base_fee_scalar = 11;
    fee.blob_base_fee = intx::uint256{5};
    fee.blob_base_fee_scalar = 7;
    BOOST_CHECK_EQUAL(
        computeL1CostFromFlz(fee, flz, fjordConfig()), computeL1Cost(fee, env, fjordConfig()));
    BOOST_CHECK_EQUAL(computeL1CostFromFlz(fee, 0, fjordConfig()), intx::uint256{0});
}

BOOST_AUTO_TEST_SUITE_END()
