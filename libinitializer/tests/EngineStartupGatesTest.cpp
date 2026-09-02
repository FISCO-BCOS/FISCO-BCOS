/// Pure decision tests for the Engine API startup gates (R21): the OP-mode-without-engine-
/// production refusal and the [op_engine_rpc]-on-v1-executor refusal/escape. These gates run
/// inside Initializer::init at startup; the pure function is tested here so every refuse
/// combination and the v1 escape hatch are pinned without booting a node.
#include <bcos-utilities/Exceptions.h>
#include <libinitializer/EngineStartupGates.h>
#include <boost/exception/get_error_info.hpp>
#include <boost/test/unit_test.hpp>
#include <string>

namespace bcos::initializer
{
namespace
{
std::string lastComment(std::exception const& e)
{
    auto const* comment = boost::get_error_info<bcos::errinfo_comment>(e);
    return comment ? std::string(*comment) : std::string();
}
}  // namespace

BOOST_AUTO_TEST_SUITE(EngineStartupGates)

/// OP mode (executor_version >= 3) without engine-driven block production is refused:
/// the legacy PBFT pipeline would drive the OpScheduler slot with non-engine flow.
BOOST_AUTO_TEST_CASE(opModeWithoutEngineProductionIsRefused)
{
    for (bool opEngineRpc : {false, true})
    {
        BOOST_CHECK_EXCEPTION(checkEngineStartupGates(3, /*engineDriven=*/false, opEngineRpc,
                                  /*allowV1=*/false),
            bcos::tool::InvalidConfig, [](auto const& e) {
                return lastComment(e).find("requires engine-driven block production") !=
                       std::string::npos;
            });
        BOOST_CHECK_EXCEPTION(checkEngineStartupGates(4, false, opEngineRpc, false),
            bcos::tool::InvalidConfig, [](auto const& e) {
                return lastComment(e).find("requires engine-driven block production") !=
                       std::string::npos;
            });
    }
}

/// OP mode with either engine-driven production path passes the gate.
BOOST_AUTO_TEST_CASE(opModeWithEngineProductionPasses)
{
    BOOST_CHECK(checkEngineStartupGates(3, /*engineDriven=*/true, /*opEngineRpc=*/true,
                    /*allowV1=*/false) == V1ExecutorEscape::NotApplicable);
    BOOST_CHECK(checkEngineStartupGates(3, true, false, false) == V1ExecutorEscape::NotApplicable);
}

/// [op_engine_rpc] on a v1 executor (executor_version < 2) is refused unless the explicit
/// test-only escape hatch is set.
BOOST_AUTO_TEST_CASE(opEngineRpcOnV1Executor)
{
    for (int version : {0, 1})
    {
        BOOST_CHECK_EXCEPTION(checkEngineStartupGates(version, /*engineDriven=*/false,
                                  /*opEngineRpc=*/true, /*allowV1=*/false),
            bcos::tool::InvalidConfig, [](auto const& e) {
                return lastComment(e).find("unsafe_allow_v1_executor=true") != std::string::npos;
            });
        // Escape hatch: allowed, but the caller must log the harness warning.
        BOOST_CHECK(checkEngineStartupGates(version, false, true, /*allowV1=*/true) ==
                    V1ExecutorEscape::AllowedWithWarning);
        // Without op_engine_rpc the v1 executor needs no escape hatch at all.
        BOOST_CHECK(checkEngineStartupGates(version, false, false, false) ==
                    V1ExecutorEscape::NotApplicable);
    }
}

/// executor_version == 2 is exactly the v2 boundary: engine_api is not v1-only there, and
/// the OP gate does not apply (2 < 3) — a plain v2 chain without engine production is legal.
BOOST_AUTO_TEST_CASE(v2BoundaryAndPlainV2Chain)
{
    BOOST_CHECK(checkEngineStartupGates(2, /*engineDriven=*/false, /*opEngineRpc=*/true, false) ==
                V1ExecutorEscape::NotApplicable);
    BOOST_CHECK(checkEngineStartupGates(2, false, false, false) == V1ExecutorEscape::NotApplicable);
}

/// The OP gate reports first: an OP-mode config that also carries op_engine_rpc on a v1
/// executor answers the engine-driven-production refusal, not the v1-executor one.
BOOST_AUTO_TEST_CASE(opGateHasPriorityOverV1Gate)
{
    BOOST_CHECK_EXCEPTION(checkEngineStartupGates(3, /*engineDriven=*/false,
                              /*opEngineRpc=*/true, /*allowV1=*/true),
        bcos::tool::InvalidConfig, [](auto const& e) {
            return lastComment(e).find("requires engine-driven block production") !=
                   std::string::npos;
        });
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::initializer
