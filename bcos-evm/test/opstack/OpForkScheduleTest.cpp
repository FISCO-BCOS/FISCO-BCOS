#include "TestPrinters.h"
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpPrecompiles.h>
#include <bcos-framework/ledger/GenesisConfig.h>
#include <boost/test/unit_test.hpp>
#include <limits>

using namespace bcos::evm::opstack;

namespace
{
constexpr uint64_t kNever = std::numeric_limits<uint64_t>::max();
/// Genesis fork schedule in SECONDS, the shape [op_fork_timestamps] produces.
bcos::ledger::OpForkSchedule sched(uint64_t jovianTime, uint64_t karstTime)
{
    return bcos::ledger::OpForkSchedule{.m_jovianTime = jovianTime, .m_karstTime = karstTime};
}
}  // namespace

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

    // Karst keeps every Jovian fee/receipt rule and changes exactly two fields: the EVM base
    // moves to Osaka and the precompile table becomes Karst's own.
    const auto& k = karstConfig();
    BOOST_CHECK_EQUAL(k.fork, OpFork::Karst);
    BOOST_CHECK_EQUAL(k.rev, EVMC_OSAKA);
    BOOST_CHECK_NE(k.rev, j.rev);
    BOOST_CHECK_EQUAL(k.has_operator_fee, j.has_operator_fee);
    BOOST_CHECK_EQUAL(k.has_jovian_operator_formula, j.has_jovian_operator_formula);
    BOOST_CHECK_EQUAL(k.has_da_footprint, j.has_da_footprint);
    BOOST_CHECK_EQUAL(k.disable_prague_requests, j.disable_prague_requests);
    BOOST_CHECK_EQUAL(k.precompiles, &karstPrecompileOverrides());
    BOOST_CHECK_NE(k.precompiles, j.precompiles);
}

// At and above karst_time configAt hands back the same static config karstConfig() does.
BOOST_AUTO_TEST_CASE(ConfigAtSelectsKarst)
{
    // Value copies, not references: configAt returns a reference to a static config, but the
    // schedule argument is a prvalue temporary — GCC-14 -Wdangling-reference flags the
    // reference binding as potentially dangling (false positive; the returned ref never
    // aliases the argument). Copy the ~32B config instead.
    const auto karst = configAt(sched(1000, 2000), 2000);
    BOOST_CHECK_EQUAL(karst.fork, OpFork::Karst);
    BOOST_CHECK_EQUAL(karst.rev, EVMC_OSAKA);
    BOOST_CHECK_EQUAL(karst.precompiles, &karstPrecompileOverrides());
    BOOST_CHECK(karst.has_da_footprint);
    BOOST_CHECK(karst.has_jovian_operator_formula);
    BOOST_CHECK_EQUAL(&configAt(sched(1000, 2000), 2000), &karstConfig());

    // Karst is a superset of Jovian: a schedule that activates both at the same second is
    // Karst, and one that (illegally, NodeConfig rejects it) leaves jovian unscheduled must
    // still not downgrade a Karst block to Isthmus.
    BOOST_CHECK_EQUAL(configAt(sched(0, 0), 0).fork, OpFork::Karst);
    BOOST_CHECK_EQUAL(configAt(sched(kNever, 2000), 2000).fork, OpFork::Karst);
}

// The whole ladder on one schedule, at the exact boundary seconds. op-node's IsX(ts) is
// `ts >= *Time`, so the activation second itself is already inside the fork.
BOOST_AUTO_TEST_CASE(ConfigAtIsKeyedOnTheBlockTimestamp)
{
    const auto schedule = sched(1000, 2000);
    BOOST_CHECK_EQUAL(configAt(schedule, 0).fork, OpFork::Isthmus);
    BOOST_CHECK_EQUAL(configAt(schedule, 999).fork, OpFork::Isthmus);
    BOOST_CHECK_EQUAL(configAt(schedule, 1000).fork, OpFork::Jovian);
    BOOST_CHECK_EQUAL(configAt(schedule, 1999).fork, OpFork::Jovian);
    BOOST_CHECK_EQUAL(configAt(schedule, 2000).fork, OpFork::Karst);
    BOOST_CHECK_EQUAL(configAt(schedule, 2001).fork, OpFork::Karst);
}

// UINT64_MAX is op-node's nil: the fork is not scheduled and never activates, not even at
// the largest representable timestamp.
BOOST_AUTO_TEST_CASE(UnscheduledForksNeverActivate)
{
    BOOST_CHECK_EQUAL(configAt(sched(kNever, kNever), 0).fork, OpFork::Isthmus);
    BOOST_CHECK_EQUAL(configAt(sched(kNever, kNever), kNever - 1).fork, OpFork::Isthmus);
    BOOST_CHECK_EQUAL(configAt(sched(0, kNever), kNever - 1).fork, OpFork::Jovian);
}

// A genesis-activated fork is active for block 0 itself (timestamp 0 >= 0).
BOOST_AUTO_TEST_CASE(ZeroMeansActiveFromGenesis)
{
    BOOST_CHECK_EQUAL(configAt(sched(0, kNever), 0).fork, OpFork::Jovian);
    BOOST_CHECK_EQUAL(configAt(sched(0, 0), 0).fork, OpFork::Karst);
}

BOOST_AUTO_TEST_CASE(IsthmusDisablesJovianFlags)
{
    const auto& i = isthmusConfig();
    BOOST_CHECK(!(i.has_jovian_operator_formula));
    BOOST_CHECK(!(i.has_da_footprint));
}

// Covers the remaining field has_ecotone_l1_formula (Ecotone bills L1 by calldataGas, Fjord+ by
// FastLZ) and the reference stability of configAt's three branches.
BOOST_AUTO_TEST_CASE(EcotoneFormulaFlagAndConfigAtBranches)
{
    BOOST_CHECK(ecotoneConfig().has_ecotone_l1_formula);
    BOOST_CHECK(!(fjordConfig().has_ecotone_l1_formula));
    BOOST_CHECK(!(graniteConfig().has_ecotone_l1_formula));
    BOOST_CHECK(!(holoceneConfig().has_ecotone_l1_formula));
    BOOST_CHECK(!(isthmusConfig().has_ecotone_l1_formula));
    BOOST_CHECK(!(jovianConfig().has_ecotone_l1_formula));

    BOOST_CHECK(!(karstConfig().has_ecotone_l1_formula));

    // configAt's three branches; each returns a reference to the same static config.
    BOOST_CHECK_EQUAL(&configAt(sched(1000, 2000), 999), &isthmusConfig());
    BOOST_CHECK_EQUAL(&configAt(sched(1000, 2000), 1000), &jovianConfig());
    BOOST_CHECK_EQUAL(&configAt(sched(1000, 2000), 2000), &karstConfig());
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
