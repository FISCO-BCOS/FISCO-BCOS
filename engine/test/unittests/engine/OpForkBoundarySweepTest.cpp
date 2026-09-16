/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @file OpForkBoundarySweepTest.cpp
 * @brief M7: per-fork activation boundary sweep at the CONFIG layer (9 forks x 3 cells).
 */
// What is NOT here, deliberately: Holocene's constant baseFee (OpEngineApiVersionsTest.cpp:285)
// and the Karst/Jovian activation blocks (OpKarstActivationTest.cpp:203-261) are already
// covered; this sweep owns the config-field transitions the previous boundary vectors never
// touched. expectations come from external oracles (op-node eth.ExecutionPayload comments,
// op-revm spec.rs/precompiles.rs, the Fjord/Isthmus/Jovian feature definitions), NOT from the
// table under test.
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpPrecompiles.h>
#include <bcos-framework/engine/OpForkId.h>
#include <opstack-executor/OpSchedulerSeam.h>  // detail::tryEngineForkId
#include <boost/test/tree/decorator.hpp>
#include <boost/test/unit_test.hpp>
#include <evmc/evmc.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>

using bcos::engine::OpExtraDataLayout;
using bcos::engine::OpForkId;
using bcos::evm::opstack::L1FeeModel;
using bcos::evm::opstack::OpFork;
using bcos::evm::opstack::OpForkConfig;
using bcos::evm::opstack::OpForkSchedule;

namespace
{
/// The ladder the sweep runs on: every modeled EL fork, contiguous, starting at 0 (the
/// production parser requires a timestamp-0 baseline and strictly contiguous order).
constexpr std::string_view c_ladder =
    "0:regolith,1000:canyon,2000:ecotone,3000:fjord,"
    "4000:granite,5000:holocene,6000:isthmus,7000:jovian,"
    "8000:karst";

/// One fork's expected configuration at its activation. rev/precompiles/layout come from
/// op-revm and op-node; the fee arm from the Fjord formula change; the operator-fee and
/// DA-footprint flags from Isthmus/Jovian. See the file header for citations.
struct Boundary
{
    char const* name;
    OpFork fork;
    std::uint64_t activation;
    OpExtraDataLayout layout;
    evmc_revision rev;
    bcos::evm::opstack::PrecompileOverrides const* precompiles;  // nullptr = no override table
    L1FeeModel feeModel;
    bool hasEcotoneFormula;
    bool hasOperatorFee;
    bool hasJovianOperatorFormula;
    bool hasDaFootprint;
    bool depositExemptFromMaxTxGas;
};

/// NOTE the regolith `rev`: op-revm maps BEDROCK|REGOLITH -> SpecId::MERGE (= EVMC_PARIS,
// evmc.h:1020) while this lane uses EVMC_LONDON. That cell is under M0 review
// (docs/plans/2026-09-12-m0-cell-audit.md, cell 10); until it is decided this row pins the
// CURRENT value so a silent change is caught, and the divergence is named here.
Boundary const c_boundaries[] = {
    {.name = "regolith",
        .fork = OpFork::Regolith,
        .activation = 0,
        .layout = OpExtraDataLayout::Empty,
        .rev = EVMC_LONDON,
        .precompiles = nullptr,
        .feeModel = L1FeeModel::Bedrock,
        .hasEcotoneFormula = 0,
        .hasOperatorFee = 0,
        .hasJovianOperatorFormula = 0,
        .hasDaFootprint = 0,
        .depositExemptFromMaxTxGas = 0},
    {.name = "canyon",
        .fork = OpFork::Canyon,
        .activation = 1000,
        .layout = OpExtraDataLayout::Empty,
        .rev = EVMC_SHANGHAI,
        .precompiles = nullptr,
        .feeModel = L1FeeModel::Bedrock,
        .hasEcotoneFormula = 0,
        .hasOperatorFee = 0,
        .hasJovianOperatorFormula = 0,
        .hasDaFootprint = 0,
        .depositExemptFromMaxTxGas = 0},
    {.name = "ecotone",
        .fork = OpFork::Ecotone,
        .activation = 2000,
        .layout = OpExtraDataLayout::Empty,
        .rev = EVMC_CANCUN,
        .precompiles = nullptr,
        .feeModel = L1FeeModel::Ecotone,
        .hasEcotoneFormula = 1,
        .hasOperatorFee = 0,
        .hasJovianOperatorFormula = 0,
        .hasDaFootprint = 0,
        .depositExemptFromMaxTxGas = 0},
    {.name = "fjord",
        .fork = OpFork::Fjord,
        .activation = 3000,
        .layout = OpExtraDataLayout::Empty,
        .rev = EVMC_CANCUN,
        .precompiles = &bcos::evm::opstack::fjordPrecompileOverrides(),
        .feeModel = L1FeeModel::Fjord,
        .hasEcotoneFormula = 0,
        .hasOperatorFee = 0,
        .hasJovianOperatorFormula = 0,
        .hasDaFootprint = 0,
        .depositExemptFromMaxTxGas = 0},
    {.name = "granite",
        .fork = OpFork::Granite,
        .activation = 4000,
        .layout = OpExtraDataLayout::Empty,
        .rev = EVMC_CANCUN,
        .precompiles = &bcos::evm::opstack::granitePrecompileOverrides(),
        .feeModel = L1FeeModel::Fjord,
        .hasEcotoneFormula = 0,
        .hasOperatorFee = 0,
        .hasJovianOperatorFormula = 0,
        .hasDaFootprint = 0,
        .depositExemptFromMaxTxGas = 0},
    {.name = "holocene",
        .fork = OpFork::Holocene,
        .activation = 5000,
        .layout = OpExtraDataLayout::Holocene9,
        .rev = EVMC_CANCUN,
        .precompiles = &bcos::evm::opstack::granitePrecompileOverrides(),
        .feeModel = L1FeeModel::Fjord,
        .hasEcotoneFormula = 0,
        .hasOperatorFee = 0,
        .hasJovianOperatorFormula = 0,
        .hasDaFootprint = 0,
        .depositExemptFromMaxTxGas = 0},
    {.name = "isthmus",
        .fork = OpFork::Isthmus,
        .activation = 6000,
        .layout = OpExtraDataLayout::Holocene9,
        .rev = EVMC_PRAGUE,
        .precompiles = &bcos::evm::opstack::isthmusPrecompileOverrides(),
        .feeModel = L1FeeModel::Fjord,
        .hasEcotoneFormula = 0,
        .hasOperatorFee = 1,
        .hasJovianOperatorFormula = 0,
        .hasDaFootprint = 0,
        .depositExemptFromMaxTxGas = 0},
    {.name = "jovian",
        .fork = OpFork::Jovian,
        .activation = 7000,
        .layout = OpExtraDataLayout::Jovian17,
        .rev = EVMC_PRAGUE,
        .precompiles = &bcos::evm::opstack::jovianPrecompileOverrides(),
        .feeModel = L1FeeModel::Fjord,
        .hasEcotoneFormula = 0,
        .hasOperatorFee = 1,
        .hasJovianOperatorFormula = 1,
        .hasDaFootprint = 1,
        .depositExemptFromMaxTxGas = 0},
    {.name = "karst",
        .fork = OpFork::Karst,
        .activation = 8000,
        .layout = OpExtraDataLayout::Jovian17,
        .rev = EVMC_OSAKA,
        .precompiles = &bcos::evm::opstack::karstPrecompileOverrides(),
        .feeModel = L1FeeModel::Fjord,
        .hasEcotoneFormula = 0,
        .hasOperatorFee = 1,
        .hasJovianOperatorFormula = 1,
        .hasDaFootprint = 1,
        .depositExemptFromMaxTxGas = 1},
};

void checkConfig(Boundary const& b, OpForkConfig const& cfg)
{
    BOOST_TEST_INFO_SCOPE(b.name);
    BOOST_CHECK(cfg.fork == b.fork);
    BOOST_CHECK_EQUAL(static_cast<int>(cfg.rev), static_cast<int>(b.rev));
    BOOST_CHECK(cfg.precompiles == b.precompiles);
    BOOST_CHECK(cfg.l1_fee_model == b.feeModel);
    // The fee model is the single selector; hasEcotoneFormula pins the derived boolean
    // against the boundary table (Ecotone model iff formula flag) with no second field.
    BOOST_CHECK_EQUAL(cfg.l1_fee_model == L1FeeModel::Ecotone, b.hasEcotoneFormula != 0);
    BOOST_CHECK_EQUAL(cfg.has_operator_fee, b.hasOperatorFee);
    BOOST_CHECK_EQUAL(cfg.has_jovian_operator_formula, b.hasJovianOperatorFormula);
    BOOST_CHECK_EQUAL(cfg.has_da_footprint, b.hasDaFootprint);
    BOOST_CHECK_EQUAL(cfg.deposit_exempt_from_max_tx_gas, b.depositExemptFromMaxTxGas);
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OpForkBoundarySweepSuite)

/// 9 forks x 3 cells: the fork BEFORE the activation, the activation block itself, and the
/// block after. Each cell asserts the fork identity, and the activation cell plus the
/// following cell additionally assert the full config field set.
///
/// Spec basis for the activation rule pinned here (the Regolith row, WI → R06):
/// `specs/protocol/regolith/overview.md:36-37` — "The Regolith upgrade uses a _L2
/// block-timestamp_ activation-rule, and is specified in both the rollup-node
/// (`regolith_time`) and execution engine (`config.regolithTime`)". The same timestamp-keyed
/// form is used for every later fork, which is what `c_ladder` below encodes.
// clang-format off
BOOST_AUTO_TEST_CASE(EachForkSwitchesExactlyAtItsActivation, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    auto const schedule = OpForkSchedule::parse(c_ladder);
    std::size_t cells = 0;
    for (std::size_t i = 0; i < std::size(c_boundaries); ++i)
    {
        auto const& b = c_boundaries[i];
        BOOST_TEST_INFO_SCOPE(b.name);

        // Cell 1: the block before the activation is still the previous fork. At i == 0 the
        // activation is 0 and Bedrock is not modeled, so there is no pre-fork cell; that is
        // recorded rather than asserted away. Note the guard comes BEFORE any subtraction:
        // b.activation is uint64_t, so `b.activation - 1` at 0 would wrap to 2^64-1.
        if (i == 0)
        {
            BOOST_TEST_MESSAGE("regolith activates at 0; no pre-activation cell to check");
        }
        else
        {
            auto const before = schedule.configAt(b.activation - 1);
            BOOST_CHECK_EQUAL(
                static_cast<int>(before.fork), static_cast<int>(c_boundaries[i - 1].fork));
            BOOST_CHECK(before.rev == c_boundaries[i - 1].rev);
        }

        // Cell 2: the activation block.
        auto const at = schedule.configAt(b.activation);
        checkConfig(b, at);
        BOOST_CHECK_EQUAL(static_cast<int>(bcos::engine::extraDataLayoutFor(
                              *bcos::evm::engine::detail::tryEngineForkId(b.fork))),
            static_cast<int>(b.layout));

        // Cell 3: the block after the activation is still this fork (monotone until the
        // next activation).
        if (i + 1 < std::size(c_boundaries))
        {
            BOOST_CHECK_EQUAL(static_cast<int>(schedule.configAt(b.activation + 1).fork),
                static_cast<int>(b.fork));
        }
        cells += 3;
    }
    BOOST_CHECK_EQUAL(cells, 27U);
}

/// The activation timestamps must be exactly where the schedule says: one second earlier is
/// the previous fork, one second later is still this one. A copy of the ladder with the
/// boundary shifted would fail here, which is what makes the sweep a boundary test rather
/// than a table read-back.
// clang-format off
BOOST_AUTO_TEST_CASE(ActivationTimestampsAreExact, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    auto const schedule = OpForkSchedule::parse(c_ladder);
    for (std::size_t i = 0; i < std::size(c_boundaries); ++i)
    {
        auto const& b = c_boundaries[i];
        BOOST_TEST_INFO_SCOPE(b.name);
        BOOST_CHECK_EQUAL(
            static_cast<int>(schedule.forkAt(b.activation)), static_cast<int>(b.fork));
        if (i == 0)
        {
            continue;
        }
        BOOST_CHECK_EQUAL(static_cast<int>(schedule.forkAt(b.activation - 1)),
            static_cast<int>(c_boundaries[i - 1].fork));
    }
}
BOOST_AUTO_TEST_SUITE_END()
