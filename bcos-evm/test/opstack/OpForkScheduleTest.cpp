#include "TestPrinters.h"
#include "support/KarstScheduleFixtures.h"
#include "support/OpForkFlagsCompat.h"
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpPrecompiles.h>
#include <bcos-framework/ledger/GenesisConfig.h>
#include <bcos-framework/ledger/OpForkScheduleCodec.h>
#include <boost/test/tree/decorator.hpp>
#include <boost/test/unit_test.hpp>
#include <cstddef>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

using namespace bcos::evm::opstack;
using bcos::ledger::InvalidOpForkSchedule;

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

// clang-format off
BOOST_AUTO_TEST_CASE(IsthmusMapsToPrague, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    const auto& cfg = isthmusConfig();
    BOOST_CHECK_EQUAL(cfg.fork, OpFork::Isthmus);
    BOOST_CHECK_EQUAL(cfg.rev, EVMC_PRAGUE);
    BOOST_CHECK(cfg.disable_prague_requests);
    BOOST_CHECK(cfg.has_operator_fee);
}

// clang-format off
BOOST_AUTO_TEST_CASE(JovianAndKarstConfigs, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
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

// clang-format off
BOOST_AUTO_TEST_CASE(IsthmusDisablesJovianFlags, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    const auto& i = isthmusConfig();
    BOOST_CHECK(!(i.has_jovian_operator_formula));
    BOOST_CHECK(!(i.has_da_footprint));
}

// Feature-flag fork selection (feature_op_jovian replaces the former timestamp thresholds):
// OFF → Isthmus baseline, ON → Jovian semantics.
// clang-format off
BOOST_AUTO_TEST_CASE(ConfigAtSelectsForkByFeatureFlag, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
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

// 覆盖剩余字段 l1_fee_model（Ecotone 用 calldataGas、Fjord+ 用 FastLZ），
// 以及 configAt 三个分支的引用稳定性（timestamp 版）。
// 测试专用 configAt(OpForkFlags) 只有 Isthmus/Jovian 两分支，选不出 Karst。
// 生产路径是 timestamp OpForkSchedule::configAt，Karst 时间戳返回 karstConfig()/Osaka。
// clang-format off
BOOST_AUTO_TEST_CASE(EcotoneFeeModelAndFlagsWrapperDoesNotSelectKarst, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    BOOST_CHECK(ecotoneConfig().l1_fee_model == L1FeeModel::Ecotone);
    BOOST_CHECK(fjordConfig().l1_fee_model == L1FeeModel::Fjord);
    BOOST_CHECK(graniteConfig().l1_fee_model == L1FeeModel::Fjord);
    BOOST_CHECK(holoceneConfig().l1_fee_model == L1FeeModel::Fjord);
    BOOST_CHECK(isthmusConfig().l1_fee_model == L1FeeModel::Fjord);
    BOOST_CHECK(jovianConfig().l1_fee_model == L1FeeModel::Fjord);

    BOOST_CHECK(karstConfig().l1_fee_model == L1FeeModel::Fjord);

    // configAt's three branches; each returns a reference to the same static config.
    BOOST_CHECK_EQUAL(&configAt(sched(1000, 2000), 999), &isthmusConfig());
    BOOST_CHECK_EQUAL(&configAt(sched(1000, 2000), 1000), &jovianConfig());
    BOOST_CHECK_EQUAL(&configAt(sched(1000, 2000), 2000), &karstConfig());

    BOOST_CHECK_EQUAL(&configAt(OpForkFlags{.jovianActive = false}), &isthmusConfig());
    BOOST_CHECK_EQUAL(&configAt(OpForkFlags{.jovianActive = true}), &jovianConfig());
}

// clang-format off
BOOST_AUTO_TEST_CASE(L1FeeModelPinnedOnExistingConfigs, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    BOOST_CHECK(ecotoneConfig().l1_fee_model == L1FeeModel::Ecotone);
    BOOST_CHECK(fjordConfig().l1_fee_model == L1FeeModel::Fjord);
    BOOST_CHECK(graniteConfig().l1_fee_model == L1FeeModel::Fjord);
    BOOST_CHECK(holoceneConfig().l1_fee_model == L1FeeModel::Fjord);
    BOOST_CHECK(isthmusConfig().l1_fee_model == L1FeeModel::Fjord);
    BOOST_CHECK(jovianConfig().l1_fee_model == L1FeeModel::Fjord);
    BOOST_CHECK(karstConfig().l1_fee_model == L1FeeModel::Fjord);
    BOOST_CHECK(ecotoneConfig().l1_fee_model == L1FeeModel::Ecotone);
    BOOST_CHECK(fjordConfig().l1_fee_model == L1FeeModel::Fjord);
}

// clang-format off
BOOST_AUTO_TEST_CASE(RegolithCanyonConfigsAndTimestampSelect, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    BOOST_CHECK_EQUAL(regolithConfig().rev, EVMC_LONDON);
    BOOST_CHECK(regolithConfig().l1_fee_model == L1FeeModel::Bedrock);
    BOOST_CHECK(!regolithConfig().has_operator_fee);
    BOOST_CHECK(!regolithConfig().has_da_footprint);
    BOOST_CHECK_EQUAL(canyonConfig().rev, EVMC_SHANGHAI);
    BOOST_CHECK(canyonConfig().l1_fee_model == L1FeeModel::Bedrock);

    OpForkSchedule sched(
        {
            {OpFork::Regolith, 0},
            {OpFork::Canyon, 100},
            {OpFork::Ecotone, 200},
            {OpFork::Fjord, 300},
            {OpFork::Holocene, 400},
            {OpFork::Isthmus, 500},
        },
        OpForkSchedule::TestBypass{});

    BOOST_CHECK_EQUAL(sched.forkAt(0), OpFork::Regolith);
    BOOST_CHECK_EQUAL(sched.forkAt(99), OpFork::Regolith);
    BOOST_CHECK_EQUAL(sched.forkAt(100), OpFork::Canyon);
    BOOST_CHECK_EQUAL(sched.forkAt(200), OpFork::Ecotone);
    BOOST_CHECK_EQUAL(&sched.configAt(0), &regolithConfig());
    BOOST_CHECK_EQUAL(&sched.configAt(150), &canyonConfig());
    BOOST_CHECK_EQUAL(&sched.configAt(200), &ecotoneConfig());
    BOOST_CHECK(sched.jovianAndLaterActivations().empty());
}

// The codec accepts any EL fork as a baseline, so production parse must map the
// name instead of rejecting it.
// clang-format off
BOOST_AUTO_TEST_CASE(ParseAcceptsRegolithBaseline, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    auto s = OpForkSchedule::parse("0:regolith");
    BOOST_CHECK_EQUAL(s.forkAt(0), OpFork::Regolith);
    BOOST_CHECK_EQUAL(&s.configAt(0), &regolithConfig());
}

// OpFork's enumerator order is the codec table's index order. The oracle below is
// an explicit {literal name, literal ordinal, named enumerator} list written down
// independently of both the table and the enum. Reordering either side (even a
// same-size swap of two middle entries) breaks a different row:
//   * table swap  -> c_opForkNames[ordinal] != name and parse() picks the wrong fork
//   * enum  swap  -> static_cast<int>(enumerator) != ordinal and parse() picks it too
// A plain parse(name[i]).forkAt(0) == static_cast<OpFork>(i) loop cannot fail, since
// parse() returns static_cast<OpFork>(forkOrder(name)) by construction.
// clang-format off
BOOST_AUTO_TEST_CASE(ForkNameEnumRoundTripsAllNine, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    using bcos::ledger::detail::c_opForkNames;
    // Protocol order, spelled out: these literals are the oracle, not the table.
    struct ForkNameOrdinal
    {
        std::string_view name;
        int ordinal;
        OpFork fork;
    };
    constexpr ForkNameOrdinal c_expected[] = {
        {"regolith", 0, OpFork::Regolith},
        {"canyon", 1, OpFork::Canyon},
        {"ecotone", 2, OpFork::Ecotone},
        {"fjord", 3, OpFork::Fjord},
        {"granite", 4, OpFork::Granite},
        {"holocene", 5, OpFork::Holocene},
        {"isthmus", 6, OpFork::Isthmus},
        {"jovian", 7, OpFork::Jovian},
        {"karst", 8, OpFork::Karst},
    };
    // The codec table infers its own size, so the protocol cardinality is pinned
    // here: exactly the nine op-geth EL forks, with no delta entry.
    BOOST_REQUIRE_EQUAL(c_opForkNames.size(), 9u);
    for (const auto& expected : c_expected)
    {
        // Enum side: the named enumerator must actually hold this literal ordinal.
        BOOST_CHECK_EQUAL(static_cast<int>(expected.fork), expected.ordinal);
        // Table side: the codec table at this literal ordinal must carry this name.
        BOOST_CHECK_EQUAL(c_opForkNames[static_cast<std::size_t>(expected.ordinal)], expected.name);
        // Runtime path: parsing the name must select this exact enumerator.
        auto s = OpForkSchedule::parse("0:" + std::string(expected.name));
        BOOST_CHECK_EQUAL(s.forkAt(0), expected.fork);
    }
}

// clang-format off
BOOST_AUTO_TEST_CASE(PreIsthmusConfigsPinned, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
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
    BOOST_CHECK(ecotoneConfig().l1_fee_model == L1FeeModel::Ecotone);
    BOOST_CHECK(fjordConfig().l1_fee_model == L1FeeModel::Fjord);
    BOOST_CHECK(graniteConfig().l1_fee_model == L1FeeModel::Fjord);
    BOOST_CHECK(holoceneConfig().l1_fee_model == L1FeeModel::Fjord);
}

// clang-format off
BOOST_AUTO_TEST_CASE(IsthmusPlusUseFjordFeeModel, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    BOOST_CHECK(isthmusConfig().l1_fee_model == L1FeeModel::Fjord);
    BOOST_CHECK(jovianConfig().l1_fee_model == L1FeeModel::Fjord);
    BOOST_CHECK(karstConfig().l1_fee_model == L1FeeModel::Fjord);
}

// D-15：op-geth 自 Fjord 起 0x100 P256VERIFY 活跃（contracts.go:193，gas 3450 params:183）；
// D-11：bn256Pairing 112687 上限自 Granite 起（params:172，Holocene 沿用）
// clang-format off
BOOST_AUTO_TEST_CASE(FjordOnwardCarryP256VerifyAndGraniteCapsBn256, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
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

// clang-format off
BOOST_AUTO_TEST_CASE(ConfigAtTimestampSelectsIsthmusThenJovian, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    auto schedule = OpForkSchedule::parse("0:isthmus,1764691201:jovian");
    BOOST_CHECK_EQUAL(schedule.forkAt(0), OpFork::Isthmus);
    BOOST_CHECK_EQUAL(schedule.forkAt(1764691200), OpFork::Isthmus);
    BOOST_CHECK_EQUAL(schedule.forkAt(1764691201), OpFork::Jovian);
    BOOST_CHECK_EQUAL(schedule.configAt(1764691200).rev, EVMC_PRAGUE);
    BOOST_CHECK(!schedule.configAt(1764691200).has_da_footprint);
    BOOST_CHECK(schedule.configAt(1764691201).has_da_footprint);
    BOOST_CHECK_THROW(OpForkSchedule::parse("0:isthmus,1:karst"), InvalidOpForkSchedule);
}

// clang-format off
BOOST_AUTO_TEST_CASE(LegacyFlagsStillSelectIsthmusOrJovian, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    BOOST_CHECK_EQUAL(OpForkSchedule::legacy(false).forkAt(0), OpFork::Isthmus);
    BOOST_CHECK_EQUAL(OpForkSchedule::legacy(true).forkAt(0), OpFork::Jovian);
}

// clang-format off
BOOST_AUTO_TEST_CASE(EmptyScheduleRejected, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    BOOST_CHECK_THROW(OpForkSchedule::parse(""), InvalidOpForkSchedule);
    BOOST_CHECK_THROW(OpForkSchedule{{}}, InvalidOpForkSchedule);

    auto schedule = OpForkSchedule::legacy(false);
    auto kept = std::move(schedule);
    BOOST_CHECK_EQUAL(kept.forkAt(0), OpFork::Isthmus);
    BOOST_CHECK_THROW(static_cast<void>(schedule.forkAt(0)), InvalidOpForkSchedule);
    BOOST_CHECK_THROW(static_cast<void>(schedule.configAt(0)), InvalidOpForkSchedule);
}

// clang-format off
BOOST_AUTO_TEST_CASE(ConfigAtTimestampMatchesForkAndStaticConfigs, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    auto schedule = OpForkSchedule::parse("0:isthmus,1764691201:jovian");
    BOOST_CHECK_EQUAL(schedule.configAt(1764691200).fork, schedule.forkAt(1764691200));
    BOOST_CHECK_EQUAL(schedule.configAt(1764691201).fork, schedule.forkAt(1764691201));
    BOOST_CHECK_EQUAL(&schedule.configAt(1764691200), &isthmusConfig());
    BOOST_CHECK_EQUAL(&schedule.configAt(1764691201), &jovianConfig());
}

// clang-format off
BOOST_AUTO_TEST_CASE(KarstConfigIsOsakaNotJovianAlias, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    const auto& k = karstConfig();
    BOOST_CHECK_EQUAL(k.fork, OpFork::Karst);
    BOOST_CHECK_EQUAL(k.rev, EVMC_OSAKA);
    BOOST_CHECK(k.deposit_exempt_from_max_tx_gas);
    BOOST_CHECK(k.precompiles != jovianConfig().precompiles);
}

// clang-format off
BOOST_AUTO_TEST_CASE(KarstImpliesOsakaConfig, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    auto s = OpForkSchedule::parse("0:jovian,1783526401:karst");
    BOOST_CHECK_EQUAL(s.configAt(1783526401).rev, EVMC_OSAKA);
    BOOST_CHECK(s.configAt(1783526401).deposit_exempt_from_max_tx_gas);
}

// clang-format off
BOOST_AUTO_TEST_CASE(JovianAndLaterActivationsAreNamedForks, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    auto isthmusOnly = OpForkSchedule::parse("0:isthmus");
    BOOST_CHECK(isthmusOnly.jovianAndLaterActivations().empty());

    auto withJovian = OpForkSchedule::parse("0:isthmus,1764691201:jovian");
    auto const jovianSlice = withJovian.jovianAndLaterActivations();
    BOOST_REQUIRE_EQUAL(jovianSlice.size(), 1);
    BOOST_CHECK(jovianSlice[0].fork == OpFork::Jovian);

    auto withKarst = OpForkSchedule::parse("0:jovian,1783526401:karst");
    auto const karstSlice = withKarst.jovianAndLaterActivations();
    BOOST_REQUIRE_EQUAL(karstSlice.size(), 2);
    BOOST_CHECK(karstSlice[0].fork == OpFork::Jovian);
    BOOST_CHECK(karstSlice[1].fork == OpFork::Karst);

    auto preIsthmus =
        OpForkSchedule{{{OpFork::Ecotone, 0}, {OpFork::Jovian, 10}}, OpForkSchedule::TestBypass{}};
    auto const named = preIsthmus.jovianAndLaterActivations();
    BOOST_REQUIRE_EQUAL(named.size(), 1);
    BOOST_CHECK(named[0].fork == OpFork::Jovian);
}

// clang-format off
BOOST_AUTO_TEST_CASE(TestBypassScheduleCanNameKarst, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    auto s =
        OpForkSchedule{{{OpFork::Jovian, 0}, {OpFork::Karst, 100}}, OpForkSchedule::TestBypass{}};
    BOOST_CHECK_EQUAL(s.forkAt(99), OpFork::Jovian);
    BOOST_CHECK_EQUAL(s.forkAt(100), OpFork::Karst);
    BOOST_CHECK_EQUAL(s.configAt(100).rev, EVMC_OSAKA);

    const auto only = karstOnly();
    BOOST_CHECK_EQUAL(only.forkAt(2), OpFork::Karst);
    BOOST_CHECK_EQUAL(only.configAt(2).rev, EVMC_OSAKA);
}

BOOST_AUTO_TEST_CASE(CanonicalTextFoldsLedgerShorthand, *boost::unit_test::label("fork-isthmus") *
                                                            boost::unit_test::label("fork-jovian") *
                                                            boost::unit_test::label("fork-karst"))
// clang-format on
{
    // Both forks scheduled: Isthmus baseline, then the jovian/karst activations.
    BOOST_CHECK_EQUAL(OpForkSchedule::fromLedgerSchedule(sched(100, 200)).canonicalText(),
        "0:isthmus,100:jovian,200:karst");
    // jovian_time == 0 makes Jovian the baseline itself.
    BOOST_CHECK_EQUAL(
        OpForkSchedule::fromLedgerSchedule(sched(0, 50)).canonicalText(), "0:jovian,50:karst");
    // Unscheduled jovian is the all-Isthmus legacy chain.
    BOOST_CHECK_EQUAL(
        OpForkSchedule::fromLedgerSchedule(sched(kNever, kNever)).canonicalText(), "0:isthmus");
    // Karst alone cannot be configured (NodeConfig binds it to jovian), but the fold
    // must still drop an unscheduled karst rather than emit a never-activating row.
    BOOST_CHECK_EQUAL(OpForkSchedule::fromLedgerSchedule(sched(100, kNever)).canonicalText(),
        "0:isthmus,100:jovian");
}

BOOST_AUTO_TEST_CASE(CanonicalTextRoundTripsThroughParse,
    *boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") *
        boost::unit_test::label("fork-karst"))
// clang-format on
{
    const auto folded = OpForkSchedule::fromLedgerSchedule(sched(100, 200));
    const auto reparsed = OpForkSchedule::parse(folded.canonicalText());
    // Same dispatch decisions at the boundaries and past both activations.
    BOOST_CHECK_EQUAL(reparsed.forkAt(0), OpFork::Isthmus);
    BOOST_CHECK_EQUAL(reparsed.forkAt(99), OpFork::Isthmus);
    BOOST_CHECK_EQUAL(reparsed.forkAt(100), folded.forkAt(100));
    BOOST_CHECK_EQUAL(reparsed.forkAt(200), folded.forkAt(200));
    BOOST_CHECK_EQUAL(&reparsed.configAt(300), &karstConfig());
}

BOOST_AUTO_TEST_SUITE_END()
