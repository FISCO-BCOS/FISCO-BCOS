/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include <bcos-tool/Exceptions.h>
#include <bcos-tool/NodeConfig.h>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/test/unit_test.hpp>
#include <sstream>

using namespace bcos;
using namespace bcos::tool;

namespace bcos::test
{
namespace
{
struct ValidationProbe : public NodeConfig
{
    using NodeConfig::loadFailOverConfig;
    using NodeConfig::loadTxPoolConfig;
    using NodeConfig::NodeConfig;
};

boost::property_tree::ptree fromIni(std::string const& ini)
{
    boost::property_tree::ptree pt;
    std::stringstream stream(ini);
    boost::property_tree::read_ini(stream, pt);
    return pt;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(NodeConfigValidationTest)

// failover disabled: the loader returns before reading any other key.
BOOST_AUTO_TEST_CASE(failOverDisabledReturnsEarly)
{
    ValidationProbe probe;
    BOOST_CHECK_NO_THROW(probe.loadFailOverConfig(fromIni("[failover]\nenable=false\n"), true));
}

// enabled + enforceMemberID + empty member_id is rejected.
BOOST_AUTO_TEST_CASE(failOverEnforcesMemberId)
{
    ValidationProbe probe;
    BOOST_CHECK_THROW(probe.loadFailOverConfig(fromIni("[failover]\nenable=true\n"), true),
        bcos::tool::InvalidConfig);
}

// lease_ttl below the 3-second floor is rejected.
BOOST_AUTO_TEST_CASE(failOverRejectsSmallLeaseTtl)
{
    ValidationProbe probe;
    BOOST_CHECK_THROW(probe.loadFailOverConfig(
                          fromIni("[failover]\nenable=true\nmember_id=m\nlease_ttl=1\n"), false),
        bcos::tool::InvalidConfig);
}

// enabled with a valid member_id and lease_ttl is accepted.
BOOST_AUTO_TEST_CASE(failOverValidAccepted)
{
    ValidationProbe probe;
    BOOST_CHECK_NO_THROW(probe.loadFailOverConfig(
        fromIni("[failover]\nenable=true\nmember_id=m\nlease_ttl=10\n"), false));
}

// checkAndGetValue rejects a non-numeric value (here via txpool.limit) with
// InvalidConfig rather than letting the bad lexical_cast escape.
BOOST_AUTO_TEST_CASE(checkAndGetValueRejectsNonNumeric)
{
    ValidationProbe probe;
    BOOST_CHECK_THROW(
        probe.loadTxPoolConfig(fromIni("[txpool]\nlimit=notanumber\n")), bcos::tool::InvalidConfig);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
