#include <bcos-evm/opstack/OpFeeParams.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpReceiptMeta.h>
#include <bcos-evm/opstack/RollupCost.h>
#include <gtest/gtest.h>
#include <vector>

using namespace bcos::evm::opstack;
using intx::operator""_u256;

// These tests pin deriveOpReceiptMeta (execution layer). The RLP wire-codec round-trip cases that
// used to live here were deleted with the OpReceiptMetaCodec (Task 4): the receipt meta now travels
// as a typed bcos::protocol::OpStackReceiptMeta struct (tars hex-string fields) instead of an RLP
// byte string, and its tars round-trip is covered by OpReceiptMapTest/OpSchedulerImplTest.

TEST(OpReceiptMeta, IsthmusHasFeesWithoutDa)
{
    OpFeeParams fee{};
    fee.l1_base_fee = 1000_u256;
    fee.base_fee_scalar = 7;
    fee.blob_base_fee = 2000_u256;
    fee.blob_base_fee_scalar = 9;
    fee.operator_fee_scalar = 11;
    fee.operator_fee_constant = 13;
    std::vector<uint8_t> env{0x02};
    const auto m =
        deriveOpReceiptMeta(isthmusConfig(), fee, flzCompressLen({env.data(), env.size()}),
            /*l1=*/100_u256, /*opUsed=*/50_u256, /*fill_operator_scalars=*/true);
    ASSERT_TRUE(m.l1_fee.has_value());
    EXPECT_EQ(*m.l1_fee, 100_u256);
    ASSERT_TRUE(m.l1_gas_price.has_value());
    EXPECT_EQ(*m.l1_gas_price, 1000_u256);
    EXPECT_EQ(*m.l1_blob_base_fee, 2000_u256);
    EXPECT_EQ(*m.l1_base_fee_scalar, 7u);
    EXPECT_EQ(*m.l1_blob_base_fee_scalar, 9u);
    ASSERT_TRUE(m.operator_fee.has_value());
    EXPECT_EQ(*m.operator_fee, 50_u256);
    EXPECT_TRUE(m.operator_fee_scalar.has_value());
    EXPECT_FALSE(m.da_footprint.has_value());
}

TEST(OpReceiptMeta, OperatorScalarsOmittedWhenBothZero)
{
    OpFeeParams fee{};  // operator scalar/constant both 0
    std::vector<uint8_t> env{0x02};
    const auto m = deriveOpReceiptMeta(isthmusConfig(), fee,
        flzCompressLen({env.data(), env.size()}), 0_u256, 0_u256, /*fill_operator_scalars=*/true);
    EXPECT_TRUE(m.operator_fee.has_value());          // 值始终填（FISCO 扩展）
    EXPECT_FALSE(m.operator_fee_scalar.has_value());  // 守卫：全 0 不填 scalar/constant
    EXPECT_FALSE(m.operator_fee_constant.has_value());
}

TEST(OpReceiptMeta, JovianFillsDaFootprint)
{
    OpFeeParams fee{};
    fee.da_footprint_gas_scalar = 2;
    std::vector<uint8_t> env(50, 0x11);
    const auto size = estimatedDaSize({env.data(), env.size()});
    const auto m = deriveOpReceiptMeta(
        jovianConfig(), fee, flzCompressLen({env.data(), env.size()}), 0_u256, 0_u256, false);
    ASSERT_TRUE(m.da_footprint_gas_scalar.has_value());
    EXPECT_EQ(*m.da_footprint_gas_scalar, 2u);
    ASSERT_TRUE(m.da_footprint.has_value());
    EXPECT_EQ(*m.da_footprint, size * 2u);
}

TEST(OpReceiptMeta, JovianFillsL1GasUsed)
{
    OpFeeParams fee{};
    fee.da_footprint_gas_scalar = 2;
    std::vector<uint8_t> env(50, 0x11);
    const auto flz = flzCompressLen({env.data(), env.size()});
    const auto m = deriveOpReceiptMeta(jovianConfig(), fee, flz, 0_u256, 0_u256, false);
    ASSERT_TRUE(m.l1_gas_used.has_value());
    // Fjord l1GasUsed = estimatedDaSizeScaled(flz) * 16 / 1e6 (op-geth rollup_cost.go:623-624).
    const auto expected = static_cast<uint64_t>(
        estimatedDaSizeScaled(flz) * intx::uint256{16} / intx::uint256{1000000});
    EXPECT_EQ(*m.l1_gas_used, expected);
}

TEST(OpReceiptMeta, NoDaNoL1GasUsed)
{
    // isthmusConfig() has has_da_footprint = false (no DA footprint), so derive fills neither
    // da_footprint nor l1_gas_used — this pins the has_da_footprint gate, not "pre-Fjord".
    std::vector<uint8_t> env{0x02};
    const auto m = deriveOpReceiptMeta(isthmusConfig(), OpFeeParams{},
        flzCompressLen({env.data(), env.size()}), 0_u256, 0_u256, false);
    EXPECT_FALSE(m.l1_gas_used.has_value());
}
