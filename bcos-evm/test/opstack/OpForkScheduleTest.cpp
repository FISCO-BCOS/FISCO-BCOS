#include "TestPrinters.h"
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpPrecompiles.h>
#include <boost/test/unit_test.hpp>

using namespace bcos::evm::opstack;

BOOST_AUTO_TEST_SUITE(OpForkScheduleSuite)

BOOST_AUTO_TEST_CASE(IsthmusMapsToPrague)
{
    const auto& cfg = isthmusConfig();
    BOOST_CHECK_EQUAL(cfg.fork, OpFork::Isthmus);
    BOOST_CHECK_EQUAL(cfg.rev, EVMC_PRAGUE);
    BOOST_CHECK(cfg.disable_prague_requests);
    BOOST_CHECK(cfg.has_operator_fee);
}

BOOST_AUTO_TEST_CASE(JovianAndKarstConfigs)
{
    const auto& j = jovianConfig();
    BOOST_CHECK_EQUAL(j.fork, OpFork::Jovian);
    BOOST_CHECK_EQUAL(j.rev, EVMC_PRAGUE);
    BOOST_CHECK(j.has_operator_fee);
    BOOST_CHECK(j.has_jovian_operator_formula);
    BOOST_CHECK(j.has_da_footprint);
    BOOST_CHECK(j.disable_prague_requests);
    BOOST_CHECK((j.precompiles) != nullptr);

    const auto& k = karstConfig();
    BOOST_CHECK_EQUAL(k.fork, OpFork::Karst);
    BOOST_CHECK_EQUAL(k.rev, j.rev);
    BOOST_CHECK_EQUAL(k.has_operator_fee, j.has_operator_fee);
    BOOST_CHECK_EQUAL(k.has_jovian_operator_formula, j.has_jovian_operator_formula);
    BOOST_CHECK_EQUAL(k.has_da_footprint, j.has_da_footprint);
    BOOST_CHECK_EQUAL(k.precompiles, j.precompiles);
}

BOOST_AUTO_TEST_CASE(IsthmusDisablesJovianFlags)
{
    const auto& i = isthmusConfig();
    BOOST_CHECK(!(i.has_jovian_operator_formula));
    BOOST_CHECK(!(i.has_da_footprint));
}

// Feature-flag fork selection (feature_op_jovian replaces the former timestamp thresholds):
// OFF → Isthmus baseline, ON → Jovian semantics.
BOOST_AUTO_TEST_CASE(ConfigAtSelectsForkByFeatureFlag)
{
    // Value copies, not references: configAt returns a reference to a static config, but the
    // OpForkFlags{...} argument is a prvalue temporary — GCC-14 -Wdangling-reference flags the
    // reference binding as potentially dangling (false positive; the returned ref never aliases
    // the flags argument). Copy the ~32B config instead.
    const auto ist = configAt(OpForkFlags{.jovianActive = false});
    BOOST_CHECK_EQUAL(ist.fork, OpFork::Isthmus);
    BOOST_CHECK(!ist.has_jovian_operator_formula);
    BOOST_CHECK(!ist.has_da_footprint);

    const auto jov = configAt(OpForkFlags{.jovianActive = true});
    BOOST_CHECK_EQUAL(jov.fork, OpFork::Jovian);
    BOOST_CHECK(jov.has_jovian_operator_formula);
    BOOST_CHECK(jov.has_da_footprint);
}

// 覆盖剩余字段 has_ecotone_l1_formula（Ecotone 用 calldataGas、Fjord+ 用 FastLZ）
// 与 configAt 永不返回 karstConfig()（Karst 是 op-reth-only 占位，无真实语义）。
BOOST_AUTO_TEST_CASE(EcotoneFormulaFlagAndKarstUnreachable)
{
    BOOST_CHECK(ecotoneConfig().has_ecotone_l1_formula);
    BOOST_CHECK(!(fjordConfig().has_ecotone_l1_formula));
    BOOST_CHECK(!(graniteConfig().has_ecotone_l1_formula));
    BOOST_CHECK(!(holoceneConfig().has_ecotone_l1_formula));
    BOOST_CHECK(!(isthmusConfig().has_ecotone_l1_formula));
    BOOST_CHECK(!(jovianConfig().has_ecotone_l1_formula));

    // configAt 只有 Isthmus/Jovian 两分支（引用稳定性：返回指向同一 static config）。
    BOOST_CHECK_EQUAL(&configAt(OpForkFlags{.jovianActive = false}), &isthmusConfig());
    BOOST_CHECK_EQUAL(&configAt(OpForkFlags{.jovianActive = true}), &jovianConfig());
}

BOOST_AUTO_TEST_CASE(PreIsthmusConfigsPinned)
{
    for (const auto* cfg : {&ecotoneConfig(), &fjordConfig(), &graniteConfig(), &holoceneConfig()})
    {
        BOOST_CHECK_EQUAL(cfg->rev, EVMC_CANCUN);
        BOOST_CHECK(cfg->disable_prague_requests);
        BOOST_CHECK(!(cfg->has_operator_fee));
        BOOST_CHECK(!(cfg->has_jovian_operator_formula));
        BOOST_CHECK(!(cfg->has_da_footprint));
    }
    BOOST_CHECK_EQUAL(ecotoneConfig().precompiles, nullptr);  // Ecotone 早于 Fjord/Granite/Holocene
                                                              // 表
    for (const auto* cfg : {&fjordConfig(), &graniteConfig(), &holoceneConfig()})
    {
        BOOST_CHECK((cfg->precompiles) != nullptr);  // 明细由 FjordOnward* 用例覆盖
    }
    BOOST_CHECK_EQUAL(ecotoneConfig().fork, OpFork::Ecotone);
    BOOST_CHECK_EQUAL(fjordConfig().fork, OpFork::Fjord);
    BOOST_CHECK_EQUAL(graniteConfig().fork, OpFork::Granite);
    BOOST_CHECK_EQUAL(holoceneConfig().fork, OpFork::Holocene);
    BOOST_CHECK(ecotoneConfig().has_ecotone_l1_formula);
    BOOST_CHECK(!(fjordConfig().has_ecotone_l1_formula));
    BOOST_CHECK(!(graniteConfig().has_ecotone_l1_formula));
    BOOST_CHECK(!(holoceneConfig().has_ecotone_l1_formula));
}

BOOST_AUTO_TEST_CASE(IsthmusPlusDisableEcotoneL1Formula)
{
    BOOST_CHECK(!(isthmusConfig().has_ecotone_l1_formula));
    BOOST_CHECK(!(jovianConfig().has_ecotone_l1_formula));
    BOOST_CHECK(!(karstConfig().has_ecotone_l1_formula));
}

// D-15：op-geth 自 Fjord 起 0x100 P256VERIFY 活跃（contracts.go:193，gas 3450 params:183）；
// D-11：bn256Pairing 112687 上限自 Granite 起（params:172，Holocene 沿用）
BOOST_AUTO_TEST_CASE(FjordOnwardCarryP256VerifyAndGraniteCapsBn256)
{
    BOOST_CHECK_EQUAL(ecotoneConfig().precompiles, nullptr);  // Ecotone 早于两者

    for (const auto* cfg : {&fjordConfig(), &graniteConfig(), &holoceneConfig()})
    {
        BOOST_REQUIRE((cfg->precompiles) != nullptr);
        const auto* p256 = cfg->precompiles->find(evmc::address{0x100});
        BOOST_REQUIRE((p256) != nullptr);
        BOOST_CHECK_EQUAL(p256->gas_cost_override, 3450);
    }
    BOOST_CHECK(!(fjordConfig().precompiles->contains(evmc::address{0x08})));  // cap 是 Granite 的
    for (const auto* cfg : {&graniteConfig(), &holoceneConfig()})
    {
        const auto* bn256 = cfg->precompiles->find(evmc::address{0x08});
        BOOST_REQUIRE((bn256) != nullptr);
        BOOST_CHECK_EQUAL(bn256->max_input_size, 112687u);
        BOOST_CHECK_EQUAL(bn256->gas_cost_override, -1);
        BOOST_CHECK(!(cfg->precompiles->contains(evmc::address{0x0c})));  // BLS 是 PRAGUE 的
    }
}

BOOST_AUTO_TEST_SUITE_END()
