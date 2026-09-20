#include <bcos-framework/ledger/OpForkScheduleCodec.h>
#include <boost/test/tree/decorator.hpp>
#include <boost/test/unit_test.hpp>
#include <string>
#include <string_view>

using bcos::ledger::canonicalOpForkSchedule;
using bcos::ledger::foldOpForkShorthand;
using bcos::ledger::InvalidOpForkSchedule;
using bcos::ledger::parseOpForkSchedule;

BOOST_AUTO_TEST_SUITE(OpForkScheduleCodecSuite)

// clang-format off
BOOST_AUTO_TEST_CASE(AcceptsIsthmusJovianOnly, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    auto acts = parseOpForkSchedule("0:isthmus,1764691201:jovian");
    BOOST_REQUIRE_EQUAL(acts.size(), 2u);
    BOOST_CHECK_EQUAL(acts[0].forkName, "isthmus");
    BOOST_CHECK_EQUAL(acts[0].timestamp, 0u);
    BOOST_CHECK_EQUAL(acts[1].forkName, "jovian");
    BOOST_CHECK_EQUAL(acts[1].timestamp, 1764691201u);
}

// clang-format off
BOOST_AUTO_TEST_CASE(AcceptsKarstAfterJovian, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    auto acts = parseOpForkSchedule("0:isthmus,1764691201:jovian,1783526401:karst");
    BOOST_REQUIRE_EQUAL(acts.size(), 3u);
    BOOST_CHECK_EQUAL(acts[2].forkName, "karst");
}

// clang-format off
BOOST_AUTO_TEST_CASE(AcceptsAllNineElForkBaselines, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    for (auto const* name : {"regolith", "canyon", "ecotone", "fjord", "granite", "holocene",
             "isthmus", "jovian", "karst"})
    {
        auto acts = parseOpForkSchedule(std::string("0:") + name);
        BOOST_REQUIRE_EQUAL(acts.size(), 1u);
        BOOST_CHECK_EQUAL(acts[0].forkName, name);
    }
}

// clang-format off
BOOST_AUTO_TEST_CASE(AcceptsFullOfficialChain, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    auto acts = parseOpForkSchedule(
        "0:regolith,1000:canyon,2000:ecotone,3000:fjord,4000:granite,"
        "5000:holocene,6000:isthmus,7000:jovian,8000:karst");
    BOOST_REQUIRE_EQUAL(acts.size(), 9u);
    BOOST_CHECK_EQUAL(acts[8].forkName, "karst");
    BOOST_CHECK_EQUAL(acts[8].timestamp, 8000u);
}

// clang-format off
BOOST_AUTO_TEST_CASE(NormalizesCaseAndWhitespace, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    auto acts = parseOpForkSchedule("0: Regolith ,1000:Canyon");
    BOOST_REQUIRE_EQUAL(acts.size(), 2u);
    BOOST_CHECK_EQUAL(acts[0].forkName, "regolith");
    BOOST_CHECK_EQUAL(acts[1].forkName, "canyon");
}

// The timestamp side of the colon trims too: a blank after the comma must not turn
// " 1000" into an "invalid timestamp" rejection.
// clang-format off
BOOST_AUTO_TEST_CASE(TrimsTimestampWhitespace, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    auto acts = parseOpForkSchedule("0:regolith, 1000:canyon");
    BOOST_REQUIRE_EQUAL(acts.size(), 2u);
    BOOST_CHECK_EQUAL(acts[1].timestamp, 1000u);
    BOOST_CHECK_EQUAL(acts[1].forkName, "canyon");
}

// Baseline is any known EL fork. A gap is still rejected by the general contiguity
// rule — with ONE exception: foldOpForkShorthand merges a simultaneous jovian/karst
// activation into the later fork, so isthmus -> karst (jovian skipped) is the legal
// folded shape and must parse.
// clang-format off
BOOST_AUTO_TEST_CASE(RejectsSkippedFork, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    const auto isGap = [](InvalidOpForkSchedule const& e) {
        return std::string_view{e.what()}.find("forks out of protocol order") !=
               std::string_view::npos;
    };
    BOOST_CHECK_EXCEPTION(
        parseOpForkSchedule("0:regolith,100:ecotone"), InvalidOpForkSchedule, isGap);
    // A skip is not made legal by ending at karst's neighbour either: only the folded
    // isthmus -> karst shape (jovian skipped) parses.
    BOOST_CHECK_EXCEPTION(
        parseOpForkSchedule("0:holocene,100:jovian"), InvalidOpForkSchedule, isGap);

    const auto folded = parseOpForkSchedule("0:isthmus,1783526401:karst");
    BOOST_REQUIRE_EQUAL(folded.size(), 2u);
    BOOST_CHECK_EQUAL(folded[0].forkName, "isthmus");
    BOOST_CHECK_EQUAL(folded[1].forkName, "karst");
    BOOST_CHECK_EQUAL(folded[1].timestamp, 1783526401u);
}

// The shorthand fold itself: equal jovian/karst times merge into the later fork
// (op-geth CheckConfigForkOrder compares with `>`), everything else keeps its ladder
// shape, and the two illegal shapes name their keys.
// clang-format off
BOOST_AUTO_TEST_CASE(FoldShorthandMergesSimultaneousIntoLaterFork, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    constexpr uint64_t kUnset = bcos::ledger::c_opForkTimeUnset;
    const auto fold = [](uint64_t jovianTime, uint64_t karstTime) {
        return canonicalOpForkSchedule(foldOpForkShorthand(jovianTime, karstTime));
    };
    BOOST_CHECK_EQUAL(fold(kUnset, kUnset), "0:isthmus");
    BOOST_CHECK_EQUAL(fold(0, kUnset), "0:jovian");
    BOOST_CHECK_EQUAL(fold(100, kUnset), "0:isthmus,100:jovian");
    BOOST_CHECK_EQUAL(fold(0, 0), "0:karst");
    BOOST_CHECK_EQUAL(fold(100, 100), "0:isthmus,100:karst");
    BOOST_CHECK_EQUAL(fold(0, 50), "0:jovian,50:karst");
    BOOST_CHECK_EQUAL(fold(100, 200), "0:isthmus,100:jovian,200:karst");

    const auto nonDecreasing = [](InvalidOpForkSchedule const& e) {
        return std::string_view{e.what()}.find("fork activation times must be non-decreasing") !=
               std::string_view::npos;
    };
    BOOST_CHECK_EXCEPTION(fold(2000, 1000), InvalidOpForkSchedule, nonDecreasing);
    BOOST_CHECK_EXCEPTION(fold(kUnset, 1000), InvalidOpForkSchedule, nonDecreasing);

    // karst set with jovian unscheduled names both keys and never prints the UNSET
    // sentinel as if it were a configured time.
    try
    {
        (void)fold(kUnset, 1000);
        BOOST_FAIL("expected InvalidOpForkSchedule");
    }
    catch (InvalidOpForkSchedule const& e)
    {
        const std::string_view what{e.what()};
        BOOST_CHECK(what.find("jovian_time") != std::string_view::npos);
        BOOST_CHECK(what.find("karst_time") != std::string_view::npos);
        BOOST_CHECK(what.find("18446744073709551615") == std::string_view::npos);
    }
}

// clang-format off
BOOST_AUTO_TEST_CASE(RejectsTimestampOverflow, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    const auto isOverflow = [](InvalidOpForkSchedule const& e) {
        return std::string_view{e.what()}.find("timestamp overflow") != std::string_view::npos;
    };
    BOOST_CHECK_EXCEPTION(
        parseOpForkSchedule("18446744073709551617:isthmus"), InvalidOpForkSchedule, isOverflow);
    BOOST_CHECK_EXCEPTION(
        parseOpForkSchedule("25000000000000000000:isthmus"), InvalidOpForkSchedule, isOverflow);
}

// clang-format off
BOOST_AUTO_TEST_CASE(RejectsTooManyActivations, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    const auto isTooMany = [](InvalidOpForkSchedule const& e) {
        return std::string_view{e.what()}.find("too many activations") != std::string_view::npos;
    };
    // cap 16：第 17 条在 push 前抛。
    std::string canonical = "0:regolith";
    for (uint64_t i = 1; i <= 16; ++i)
    {
        canonical += "," + std::to_string(i) + ":regolith";
    }
    BOOST_CHECK_EXCEPTION(parseOpForkSchedule(canonical), InvalidOpForkSchedule, isTooMany);
}

// clang-format off
BOOST_AUTO_TEST_CASE(RejectsEmptyMissingBaselineAndOrder, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    BOOST_CHECK_EXCEPTION(
        parseOpForkSchedule("1:isthmus"), InvalidOpForkSchedule, [](auto const& e) {
            return std::string_view{e.what()}.find("missing timestamp-0 baseline") !=
                   std::string_view::npos;
        });
    BOOST_CHECK_EXCEPTION(
        parseOpForkSchedule("0:notafork"), InvalidOpForkSchedule, [](auto const& e) {
            return std::string_view{e.what()}.find("invalid baseline") != std::string_view::npos;
        });
    BOOST_CHECK_EXCEPTION(parseOpForkSchedule(""), InvalidOpForkSchedule, [](auto const& e) {
        return std::string_view{e.what()}.find("empty schedule") != std::string_view::npos;
    });
    BOOST_CHECK_EXCEPTION(
        parseOpForkSchedule("0:jovian,1:isthmus"), InvalidOpForkSchedule, [](auto const& e) {
            return std::string_view{e.what()}.find("forks out of protocol order") !=
                   std::string_view::npos;
        });
    // An unknown name past the baseline keeps the non-baseline branch covered.
    BOOST_CHECK_EXCEPTION(
        parseOpForkSchedule("0:isthmus,1:notafork"), InvalidOpForkSchedule, [](auto const& e) {
            return std::string_view{e.what()}.find("unknown") != std::string_view::npos;
        });
}

// clang-format off
BOOST_AUTO_TEST_CASE(RejectsDuplicateTimestamp, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    const auto isDup = [](InvalidOpForkSchedule const& e) {
        return std::string_view{e.what()}.find("duplicate timestamp") != std::string_view::npos;
    };
    // 第 3 条 ts 与前一条相同：先于连续检查报错。
    BOOST_CHECK_EXCEPTION(
        parseOpForkSchedule("0:isthmus,100:jovian,100:karst"), InvalidOpForkSchedule, isDup);
}

// clang-format off
BOOST_AUTO_TEST_CASE(RejectsTrailingComma, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    const auto isTrailing = [](InvalidOpForkSchedule const& e) {
        return std::string_view{e.what()}.find("trailing comma") != std::string_view::npos;
    };
    BOOST_CHECK_EXCEPTION(parseOpForkSchedule("0:isthmus,"), InvalidOpForkSchedule, isTrailing);
    BOOST_CHECK_EXCEPTION(
        parseOpForkSchedule("0:isthmus,1764691201:jovian,"), InvalidOpForkSchedule, isTrailing);
}

BOOST_AUTO_TEST_SUITE_END()
