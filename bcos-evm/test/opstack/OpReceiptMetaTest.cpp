#include "TestPrinters.h"
#include <bcos-evm/opstack/OpFeeParams.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpReceipt.h>
#include <bcos-evm/opstack/RollupCost.h>
#include <boost/test/unit_test.hpp>
#include <vector>

using namespace bcos::evm::opstack;
using intx::operator""_u256;

BOOST_AUTO_TEST_SUITE(OpReceiptMetaSuite)

BOOST_AUTO_TEST_CASE(IsthmusHasFeesWithoutDa)
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
    BOOST_REQUIRE(m.l1_fee.has_value());
    BOOST_CHECK_EQUAL(*m.l1_fee, 100_u256);
    BOOST_REQUIRE(m.l1_gas_price.has_value());
    BOOST_CHECK_EQUAL(*m.l1_gas_price, 1000_u256);
    BOOST_CHECK_EQUAL(*m.l1_blob_base_fee, 2000_u256);
    BOOST_CHECK_EQUAL(*m.l1_base_fee_scalar, 7u);
    BOOST_CHECK_EQUAL(*m.l1_blob_base_fee_scalar, 9u);
    BOOST_REQUIRE(m.operator_fee.has_value());
    BOOST_CHECK_EQUAL(*m.operator_fee, 50_u256);
    BOOST_CHECK(m.operator_fee_scalar.has_value());
    BOOST_CHECK(!(m.da_footprint.has_value()));
}

BOOST_AUTO_TEST_CASE(OperatorScalarsOmittedWhenBothZero)
{
    OpFeeParams fee{};  // operator scalar/constant both 0
    std::vector<uint8_t> env{0x02};
    const auto m = deriveOpReceiptMeta(isthmusConfig(), fee,
        flzCompressLen({env.data(), env.size()}), 0_u256, 0_u256, /*fill_operator_scalars=*/true);
    BOOST_CHECK(m.operator_fee.has_value());            // 值始终填（FISCO 扩展）
    BOOST_CHECK(!(m.operator_fee_scalar.has_value()));  // 守卫：全 0 不填 scalar/constant
    BOOST_CHECK(!(m.operator_fee_constant.has_value()));
}

BOOST_AUTO_TEST_CASE(JovianFillsDaFootprint)
{
    OpFeeParams fee{};
    fee.da_footprint_gas_scalar = 2;
    std::vector<uint8_t> env(50, 0x11);
    const auto size = estimatedDaSize({env.data(), env.size()});
    const auto m = deriveOpReceiptMeta(
        jovianConfig(), fee, flzCompressLen({env.data(), env.size()}), 0_u256, 0_u256, false);
    BOOST_REQUIRE(m.da_footprint_gas_scalar.has_value());
    BOOST_CHECK_EQUAL(*m.da_footprint_gas_scalar, 2u);
    BOOST_REQUIRE(m.da_footprint.has_value());
    BOOST_CHECK_EQUAL(*m.da_footprint, size * 2u);
}

BOOST_AUTO_TEST_SUITE_END()
