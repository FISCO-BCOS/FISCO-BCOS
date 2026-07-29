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
        deriveOpReceiptMeta(fee, flzCompressLen({env.data(), env.size()}), /*l1=*/100_u256,
            /*opUsed=*/50_u256, /*fill_operator_scalars=*/true,
            /*has_operator_fee=*/isthmusConfig().has_operator_fee,
            /*has_da_footprint=*/isthmusConfig().has_da_footprint);
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
    const auto m = deriveOpReceiptMeta(fee, flzCompressLen({env.data(), env.size()}), 0_u256,
        0_u256, /*fill_operator_scalars=*/true,
        /*has_operator_fee=*/isthmusConfig().has_operator_fee,
        /*has_da_footprint=*/isthmusConfig().has_da_footprint);
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
    const auto m = deriveOpReceiptMeta(fee, flzCompressLen({env.data(), env.size()}), 0_u256,
        0_u256, /*fill_operator_scalars=*/false,
        /*has_operator_fee=*/jovianConfig().has_operator_fee,
        /*has_da_footprint=*/jovianConfig().has_da_footprint);
    BOOST_REQUIRE(m.da_footprint_gas_scalar.has_value());
    BOOST_CHECK_EQUAL(*m.da_footprint_gas_scalar, 2u);
    BOOST_REQUIRE(m.da_footprint.has_value());
    BOOST_CHECK_EQUAL(*m.da_footprint, size * 2u);
}

// 回执必须描述交易「实际被按哪个分叉定价/收费」，而不是生成回执时手头那份 cfg —— 两者在
// validate/transition 跨分叉边界时会不一致。这条对 DA footprint 钉住该性质，与
// OperatorFeeConservesWhenCfgDisagreesWithProps 对 operator fee 钉住的是同一条纪律。
//
// 具体场景：opValidate 跑在 Ecotone（has_da_footprint=false，且 Ecotone 走另一条 L1 公式，
// 所以 props.flz_len 被置 0），opTransition 跑在 Jovian（cfg.has_da_footprint=true）。若这里
// 读 cfg，回执会为一笔按 Ecotone 定价的交易报出 da_footprint_gas_scalar，而 da_footprint 由
// flz_len=0 算出恒为 0。deriveOpReceiptMeta 现在根本不收 cfg，该形态已不可表达。
BOOST_AUTO_TEST_CASE(DaFootprintFollowsSnapshotNotTransitionCfg)
{
    OpFeeParams fee{};
    fee.da_footprint_gas_scalar = 2;

    // 前置条件：两个 config 在这一位上确实相反，否则本用例无判别力。
    BOOST_REQUIRE(!ecotoneConfig().has_da_footprint);
    BOOST_REQUIRE(jovianConfig().has_da_footprint);

    // 按 Ecotone 定价（快照 false，flz_len 为 0）→ 即使 transition 期身处 Jovian，
    // 回执也不得出现 DA footprint 字段。
    const auto pricedUnderEcotone = deriveOpReceiptMeta(fee, /*flzLen=*/0, 0_u256, 0_u256,
        /*fill_operator_scalars=*/false,
        /*has_operator_fee=*/false,
        /*has_da_footprint=*/ecotoneConfig().has_da_footprint);
    BOOST_CHECK(!pricedUnderEcotone.da_footprint_gas_scalar.has_value());
    BOOST_CHECK(!pricedUnderEcotone.da_footprint.has_value());

    // 反向：按 Jovian 定价的交易，即使 transition 期 cfg 已回到无 DA footprint 的分叉，
    // 字段仍须存在——快照说了算。
    std::vector<uint8_t> env(50, 0x11);
    const auto flz = flzCompressLen({env.data(), env.size()});
    const auto pricedUnderJovian = deriveOpReceiptMeta(fee, flz, 0_u256, 0_u256,
        /*fill_operator_scalars=*/false,
        /*has_operator_fee=*/false,
        /*has_da_footprint=*/jovianConfig().has_da_footprint);
    BOOST_REQUIRE(pricedUnderJovian.da_footprint_gas_scalar.has_value());
    BOOST_CHECK_EQUAL(*pricedUnderJovian.da_footprint_gas_scalar, 2u);
    BOOST_REQUIRE(pricedUnderJovian.da_footprint.has_value());
    BOOST_CHECK_EQUAL(
        *pricedUnderJovian.da_footprint, estimatedDaSize({env.data(), env.size()}) * 2u);
}

BOOST_AUTO_TEST_SUITE_END()
