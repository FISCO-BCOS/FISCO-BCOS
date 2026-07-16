#include <bcos-evm-ref/opstack/OpFeeParams.h>
#include <bcos-evm-ref/opstack/OpForkSchedule.h>
#include <bcos-evm-ref/opstack/OpReceiptMeta.h>
#include <bcos-evm-ref/opstack/RollupCost.h>
#include <gtest/gtest.h>
#include <vector>

using namespace bcos::evmref::opstack;
using intx::operator""_u256;

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
