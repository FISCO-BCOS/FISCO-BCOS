/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include "ExceptionCheck.h"
#include "NodeConfigLoaderProbe.h"
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::tool;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(NodeConfigOpForkScheduleTest)

BOOST_AUTO_TEST_CASE(parsesNormalizedCanonical)
{
    LoaderProbe probe;
    probe.loadOpForkSchedule(
        fromIni("[op_fork_schedule]\n"
                "canonical=0:isthmus,1764691201:jovian\n"));
    BOOST_REQUIRE(probe.genesisConfig().m_opstackForkSchedule.has_value());
    BOOST_CHECK_EQUAL(*probe.genesisConfig().m_opstackForkSchedule, "0:isthmus,1764691201:jovian");
}

BOOST_AUTO_TEST_CASE(acceptsKarstAfterJovian)
{
    LoaderProbe probe;
    probe.loadOpForkSchedule(
        fromIni("[op_fork_schedule]\n"
                "canonical=0:jovian,1:karst\n"));
    BOOST_REQUIRE(probe.genesisConfig().m_opstackForkSchedule.has_value());
    BOOST_CHECK_EQUAL(*probe.genesisConfig().m_opstackForkSchedule, "0:jovian,1:karst");
}

// Karst after Isthmus is still rejected, now because the schedule skips Jovian
// rather than because of a Karst-specific rule.
BOOST_AUTO_TEST_CASE(rejectsSkippedFork)
{
    LoaderProbe probe;
    BOOST_CHECK_EXCEPTION(probe.loadOpForkSchedule(fromIni("[op_fork_schedule]\n"
                                                           "canonical=0:isthmus,1:karst\n")),
        InvalidConfig, [](auto const& e) { return errinfoContains(e, "protocol order"); });
}

BOOST_AUTO_TEST_CASE(acceptsFullOfficialChain)
{
    LoaderProbe probe;
    probe.loadOpForkSchedule(
        fromIni("[op_fork_schedule]\n"
                "canonical=0:regolith,1000:canyon,2000:ecotone,3000:fjord,4000:granite,"
                "5000:holocene,6000:isthmus,7000:jovian,8000:karst\n"));
    BOOST_REQUIRE(probe.genesisConfig().m_opstackForkSchedule.has_value());
    BOOST_CHECK_EQUAL(*probe.genesisConfig().m_opstackForkSchedule,
        "0:regolith,1000:canyon,2000:ecotone,3000:fjord,4000:granite,"
        "5000:holocene,6000:isthmus,7000:jovian,8000:karst");
}

BOOST_AUTO_TEST_CASE(acceptsRegolithBaseline)
{
    LoaderProbe probe;
    probe.loadOpForkSchedule(
        fromIni("[op_fork_schedule]\n"
                "canonical=0:regolith\n"));
    BOOST_REQUIRE(probe.genesisConfig().m_opstackForkSchedule.has_value());
    BOOST_CHECK_EQUAL(*probe.genesisConfig().m_opstackForkSchedule, "0:regolith");
}

BOOST_AUTO_TEST_CASE(emptyCanonicalFailsClosed)
{
    LoaderProbe probe;
    BOOST_CHECK_EXCEPTION(probe.loadOpForkSchedule(fromIni("[op_fork_schedule]\n"
                                                           "canonical=\n")),
        InvalidConfig, [](auto const& e) { return errinfoContains(e, "op_fork_schedule"); });
}

BOOST_AUTO_TEST_CASE(whitespaceCanonicalFailsClosed)
{
    LoaderProbe probe;
    BOOST_CHECK_EXCEPTION(probe.loadOpForkSchedule(fromIni("[op_fork_schedule]\n"
                                                           "canonical=   \n")),
        InvalidConfig, [](auto const& e) { return errinfoContains(e, "op_fork_schedule"); });
}

BOOST_AUTO_TEST_CASE(sectionWithoutCanonicalFailsClosed)
{
    // boost::read_ini drops empty sections, so "[op_fork_schedule]\\n" never
    // reaches the loader. Build the child explicitly: section present, no key.
    boost::property_tree::ptree pt;
    pt.put_child("op_fork_schedule", boost::property_tree::ptree{});
    LoaderProbe probe;
    BOOST_CHECK_EXCEPTION(probe.loadOpForkSchedule(pt), InvalidConfig,
        [](auto const& e) { return errinfoContains(e, "canonical"); });
}

BOOST_AUTO_TEST_CASE(missingSectionLeavesScheduleUnset)
{
    LoaderProbe probe;
    BOOST_CHECK_NO_THROW(probe.loadOpForkSchedule(fromIni("[chain]\nchain_id=1\n")));
    BOOST_CHECK(!probe.genesisConfig().m_opstackForkSchedule.has_value());
}

BOOST_AUTO_TEST_CASE(reloadWithoutSectionClearsPreviousSchedule)
{
    LoaderProbe probe;
    probe.loadOpForkSchedule(
        fromIni("[op_fork_schedule]\n"
                "canonical=0:isthmus,1764691201:jovian\n"));
    BOOST_REQUIRE(probe.genesisConfig().m_opstackForkSchedule.has_value());

    BOOST_CHECK_NO_THROW(probe.loadOpForkSchedule(fromIni("[chain]\nchain_id=1\n")));
    BOOST_CHECK(!probe.genesisConfig().m_opstackForkSchedule.has_value());
}

// The canonical gen_official_genesis.py emits for mainnet/base (registry pin
// 9cf0456a…, produced by the tool's own run): a regolith baseline plus the eight
// pinned EL activations, with delta skipped. This joins the generator to the loader —
// each side is pinned alone elsewhere, so only this case proves the seam.
BOOST_AUTO_TEST_CASE(acceptsGeneratedBaseSchedule)
{
    constexpr auto* generated =
        "0:regolith,1704992401:canyon,1710374401:ecotone,1720627201:fjord,"
        "1726070401:granite,1736445601:holocene,1746806401:isthmus,1764691201:jovian";
    LoaderProbe probe;
    probe.loadOpForkSchedule(
        fromIni(std::string("[op_fork_schedule]\ncanonical=") + generated + "\n"));
    BOOST_REQUIRE(probe.genesisConfig().m_opstackForkSchedule.has_value());
    BOOST_CHECK_EQUAL(*probe.genesisConfig().m_opstackForkSchedule, generated);

    // Negative control: dropping one activation from the same schedule is exactly what
    // the generator's contiguity rule exists to prevent, and the loader must reject it
    // (otherwise the case above would pass for a loader that ignores order entirely).
    constexpr auto* gap = "0:regolith,1704992401:canyon,1720627201:fjord";
    BOOST_CHECK_EXCEPTION(probe.loadOpForkSchedule(
                              fromIni(std::string("[op_fork_schedule]\ncanonical=") + gap + "\n")),
        InvalidConfig, [](auto const& e) { return errinfoContains(e, "protocol order"); });
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
