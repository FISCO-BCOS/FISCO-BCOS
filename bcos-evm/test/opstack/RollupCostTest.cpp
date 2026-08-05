#include <bcos-evm/opstack/OpFeeParams.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/RollupCost.h>
#include <gtest/gtest.h>
#include <fstream>
#include <vector>

using namespace bcos::evm::opstack;
using intx::operator""_u256;

namespace
{
std::vector<uint8_t> readFixture(const char* name)
{
    const std::string path = std::string(EVM_REF_OPSTACK_FIXTURES_DIR) + "/" + name;
    std::ifstream in(path, std::ios::binary);
    EXPECT_TRUE(in.is_open()) << "missing fixture: " << path;
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
}

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

TEST(RollupCost, FlzCompressLenMatchesOpGethVectors)
{
    EXPECT_EQ(flzCompressLen({}), 0u);
    std::vector<uint8_t> ones(1000, 0x01);
    EXPECT_EQ(flzCompressLen(view(ones)), 21u);
    std::vector<uint8_t> zeros(1000, 0x00);
    EXPECT_EQ(flzCompressLen(view(zeros)), 21u);
    EXPECT_EQ(flzCompressLen(view(readFixture("empty_tx.bin"))), 31u);
    EXPECT_EQ(flzCompressLen(view(readFixture("contract_call_tx.bin"))), 202u);
}

TEST(RollupCost, EstimatedDaSizeFloorsToMinimum)
{
    EXPECT_EQ(estimatedDaSizeScaled(0), 100000000_u256);
    EXPECT_EQ(estimatedDaSizeScaled(64), 100000000_u256);
    EXPECT_EQ(estimatedDaSizeScaled(200), 124714400_u256);
}

TEST(RollupCost, EmptyEnvelopeIsZeroL1Cost)
{
    EXPECT_EQ(
        computeL1Cost(feeParams(1000000000, 10000000, 2, 3), {}, fjordConfig()), intx::uint256{0});
}

TEST(RollupCost, FjordL1CostEmptyTxMatches3203000)
{
    const auto env = readFixture("empty_tx.bin");
    EXPECT_EQ(computeL1Cost(feeParams(1000000000, 10000000, 2, 3), view(env), fjordConfig()),
        3203000_u256);
}

TEST(RollupCost, BedrockCalldataGasUsedNoPlus68)
{
    // 3 个零字节 + 2 个非零字节 = 3*4 + 2*16 = 44；确认没有 pre-Regolith 的 +68。
    const std::vector<uint8_t> env{0x00, 0x00, 0x00, 0x11, 0x22};
    EXPECT_EQ(bedrockCalldataGasUsed({env.data(), env.size()}), 44u);
    EXPECT_NE(bedrockCalldataGasUsed({env.data(), env.size()}), 44u + 68u);
}

TEST(RollupCost, EcotoneL1DiffersFromFjordSameEnvelope)
{
    OpFeeParams fee{.l1_base_fee = 1000000000_u256,
        .base_fee_scalar = 2,
        .blob_base_fee_scalar = 3,
        .blob_base_fee = 10000000_u256};
    std::vector<uint8_t> env(200, 0x11);
    const auto ecotone = computeL1Cost(fee, {env.data(), env.size()}, ecotoneConfig());
    const auto fjord = computeL1Cost(fee, {env.data(), env.size()}, fjordConfig());
    EXPECT_NE(ecotone, fjord);

    // Ecotone 公式钉死：calldataGas * (l1BaseFee*16*baseScalar + blobBaseFee*blobScalar) / 16e6
    const auto calldataGas = intx::uint256{bedrockCalldataGasUsed({env.data(), env.size()})};
    const auto expected = calldataGas *
                          (fee.l1_base_fee * 16 * intx::uint256{fee.base_fee_scalar} +
                              fee.blob_base_fee * intx::uint256{fee.blob_base_fee_scalar}) /
                          intx::uint256{16'000'000};
    EXPECT_EQ(ecotone, expected);
}

TEST(RollupCost, OperatorCostIsthmus)
{
    const auto p = feeParams(0, 0, 0, 0, /*opScalar=*/2000000, /*opConst=*/500);
    EXPECT_EQ(computeOperatorCost(p, 1000, isthmusConfig()),
        intx::uint256{1000ull * 2000000 / 1000000 + 500});
}

TEST(RollupCost, OperatorCostJovianUsesTimes100)
{
    const auto p = feeParams(0, 0, 0, 0, /*opScalar=*/2000000, /*opConst=*/500);
    // Isthmus: 1000*2000000/1e6 + 500 = 2500
    EXPECT_EQ(computeOperatorCost(p, 1000, isthmusConfig()), intx::uint256{2500});
    // Jovian: 1000*2000000*100 + 500 = 200000000500
    EXPECT_EQ(computeOperatorCost(p, 1000, jovianConfig()),
        intx::uint256{1000ull * 2000000ull * 100ull + 500});
    EXPECT_EQ(
        computeOperatorCost(p, 1000, karstConfig()), computeOperatorCost(p, 1000, jovianConfig()));
}

TEST(RollupCost, EstimatedDaSizeDividesScaledBy1e6)
{
    EXPECT_EQ(estimatedDaSize({}), 0u);
    // fastlz 0 → scaled floor 100e6 → size 100
    EXPECT_EQ(estimatedDaSizeScaled(0) / 1000000_u256, intx::uint256{100});
    std::vector<uint8_t> empty;
    EXPECT_EQ(estimatedDaSize(view(empty)), 0u);
    const auto env = readFixture("empty_tx.bin");
    const auto scaled = estimatedDaSizeScaled(flzCompressLen(view(env)));
    EXPECT_EQ(estimatedDaSize(view(env)), static_cast<uint64_t>(scaled / 1000000_u256));
}

// D-14b：FastLZ 只压一次——分解 API 与原 API 等价
TEST(RollupCost, FromFlzVariantsMatchEnvelopeVariants)
{
    std::vector<uint8_t> envBytes(120);
    for (size_t i = 0; i < envBytes.size(); ++i)
        envBytes[i] = static_cast<uint8_t>(i * 7 + 3);
    const evmc::bytes_view env{envBytes.data(), envBytes.size()};
    const auto flz = flzCompressLen(env);
    ASSERT_GT(flz, 0u);
    EXPECT_EQ(estimatedDaSizeFromFlz(flz), estimatedDaSize(env));
    EXPECT_EQ(estimatedDaSizeFromFlz(0), 0u);

    OpFeeParams fee{};
    fee.l1_base_fee = intx::uint256{1000};
    fee.base_fee_scalar = 11;
    fee.blob_base_fee = intx::uint256{5};
    fee.blob_base_fee_scalar = 7;
    EXPECT_EQ(
        computeL1CostFromFlz(fee, flz, fjordConfig()), computeL1Cost(fee, env, fjordConfig()));
    EXPECT_EQ(computeL1CostFromFlz(fee, 0, fjordConfig()), intx::uint256{0});
}

TEST(RollupCost, EstimatedL1GasUsedFromFlz)
{
    using bcos::evm::opstack::estimatedDaSizeScaled;
    using bcos::evm::opstack::estimatedL1GasUsedFromFlz;
    // flzLen == 0 -> 0 (same guard as estimatedDaSizeFromFlz).
    EXPECT_EQ(estimatedL1GasUsedFromFlz(0), 0u);
    // Multiply-then-divide: for a range of inputs, equals scaled*16/1e6 with no rounding gap.
    for (uint32_t flz : {1u, 64u, 200u, 1000000u, 4000000000u})
    {
        const auto scaled = estimatedDaSizeScaled(flz);
        const auto expected =
            static_cast<uint64_t>(scaled * intx::uint256{16} / intx::uint256{1000000});
        EXPECT_EQ(estimatedL1GasUsedFromFlz(flz), expected);
    }
}
