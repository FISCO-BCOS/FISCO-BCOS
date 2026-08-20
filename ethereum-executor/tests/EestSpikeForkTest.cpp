/**
 * @file EestSpikeForkTest.cpp
 * @brief Unit tests for the opTransition spike-track fork selector (EestSpikeFork.h).
 *
 * Compiled into the standalone eest-json-tests target (links only jsoncpp_static and
 * Boost::unit_test_framework) alongside EestFailuresJsonTest.cpp — it must not include
 * EESTRunner.cpp. The predicate is dependency-free inline in the header, so the test
 * exercises the exact function the runner calls.
 */
#include "EestSpikeFork.h"

#include <boost/test/unit_test.hpp>

namespace bcos::test
{

BOOST_AUTO_TEST_CASE(spikeForkSelectAdmitsOnlyPurePrague)
{
    // The post key EEST actually emits is capitalized.
    BOOST_CHECK(spikeForkSelect("Prague"));
    // Normalized lowercase form is admitted too (the predicate is case-insensitive).
    BOOST_CHECK(spikeForkSelect("prague"));
    BOOST_CHECK(spikeForkSelect("PRAGUE"));
}

BOOST_AUTO_TEST_CASE(spikeForkSelectRejectsNonPrague)
{
    BOOST_CHECK(!spikeForkSelect("Osaka"));
    BOOST_CHECK(!spikeForkSelect("Cancun"));
    BOOST_CHECK(!spikeForkSelect("London"));
    BOOST_CHECK(!spikeForkSelect("Shanghai"));
    // Fork transitions: the "to" fork would resolve to EVMC_OSAKA under forkNameToRevision
    // and be wrongly admitted if the predicate were not an exact-match on "prague".
    BOOST_CHECK(!spikeForkSelect("CancunToPragueAtTime15000"));
    BOOST_CHECK(!spikeForkSelect("CancunToPragueAtTime15k"));
    BOOST_CHECK(!spikeForkSelect("PragueToOsakaAtTime"));
    // Empty / degenerate inputs.
    BOOST_CHECK(!spikeForkSelect(""));
    BOOST_CHECK(!spikeForkSelect("PragueWithSuffix"));
}

}  // namespace bcos::test
