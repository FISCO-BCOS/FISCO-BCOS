/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */
// [executor] eest_replay_mode: the switch that selects the EESTReplay admission column, and the
// startup gate that keeps it off a chain producing blocks through consensus.

#include "NodeConfigLoaderProbe.h"
#include <boost/exception/diagnostic_information.hpp>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::tool;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(NodeConfigEestReplayTest)

BOOST_AUTO_TEST_CASE(offByDefault)
{
    LoaderProbe probe;
    probe.loadOthersConfig({});
    BOOST_CHECK(!probe.eestReplayMode());
}

// The gate. On a consensus chain a node that stopped checking balances would fill its pool with
// transactions no leader can execute, so this is a startup failure rather than a silently
// ignored key -- found once, at the node that set it.
BOOST_AUTO_TEST_CASE(refusedWithoutEngineDrivenBlockProduction)
{
    LoaderProbe probe;
    auto pt = fromIni("[executor]\neest_replay_mode=true\n");
    BOOST_CHECK_EXCEPTION(probe.loadOthersConfig(pt), InvalidConfig, [](InvalidConfig const& e) {
        return boost::diagnostic_information(e).find("requires engine-driven block production") !=
               std::string::npos;
    });
}

BOOST_AUTO_TEST_CASE(acceptedUnderSingleNodeConsensus)
{
    LoaderProbe probe;
    auto pt = fromIni(
        "[consensus]\nenable_single_node_consensus=true\n[executor]\neest_replay_mode=true\n");
    // Same order as NodeConfig::loadConfig: the driver flags are parsed before loadOthersConfig
    // reads this one, which is what lets the gate see them.
    probe.loadOpEngineRpcConfig(pt);
    probe.loadSingleNodeConsensusConfig(pt);
    BOOST_CHECK_NO_THROW(probe.loadOthersConfig(pt));
    BOOST_CHECK(probe.eestReplayMode());
}

BOOST_AUTO_TEST_CASE(acceptedUnderOpEngineRpc)
{
    LoaderProbe probe;
    auto pt = fromIni("[op_engine_rpc]\nenable=true\n[executor]\neest_replay_mode=true\n");
    probe.loadOpEngineRpcConfig(pt);
    probe.loadSingleNodeConsensusConfig(pt);
    BOOST_CHECK_NO_THROW(probe.loadOthersConfig(pt));
    BOOST_CHECK(probe.eestReplayMode());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
