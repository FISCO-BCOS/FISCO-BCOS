/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */
// Regression tests for the [op_engine_rpc] / enable_single_node_consensus assembly gate:
// parsing of the [op_engine_rpc] section and the startup fail-fast when both the built-in
// single-node driver and the external op-node Engine RPC are enabled at once (K0).

#include "NodeConfigLoaderProbe.h"
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::tool;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(NodeConfigOpEngineGateTest)

BOOST_AUTO_TEST_CASE(opEngineRpcDefaults)
{
    LoaderProbe probe;
    probe.loadOpEngineRpcConfig({});
    BOOST_CHECK(!probe.enableOpEngineRpc());
    BOOST_CHECK_EQUAL(probe.opEngineRpcListenIP(), "127.0.0.1");
    BOOST_CHECK_EQUAL(probe.opEngineRpcListenPort(), 8551);
    BOOST_CHECK_EQUAL(probe.opEngineJwtSecretFile(), "conf/op-engine/jwt.hex");
}

BOOST_AUTO_TEST_CASE(opEngineRpcPopulated)
{
    LoaderProbe probe;
    auto pt = fromIni(
        "[op_engine_rpc]\nenable=true\nlisten_ip=0.0.0.0\nlisten_port=9551\n"
        "jwt_secret_file=conf/custom-jwt.hex\nclock_skew_secs=30\n");
    probe.loadOpEngineRpcConfig(pt);
    BOOST_CHECK(probe.enableOpEngineRpc());
    BOOST_CHECK_EQUAL(probe.opEngineRpcListenIP(), "0.0.0.0");
    BOOST_CHECK_EQUAL(probe.opEngineRpcListenPort(), 9551);
    BOOST_CHECK_EQUAL(probe.opEngineJwtSecretFile(), "conf/custom-jwt.hex");
    BOOST_CHECK_EQUAL(probe.opEngineClockSkewSecs(), 30);
}

BOOST_AUTO_TEST_CASE(singleNodeConsensusAloneAccepted)
{
    LoaderProbe probe;
    auto pt = fromIni("[consensus]\nenable_single_node_consensus=true\n");
    probe.loadOpEngineRpcConfig(pt);
    BOOST_CHECK_NO_THROW(probe.loadSingleNodeConsensusConfig(pt));
    BOOST_CHECK(probe.enableSingleNodeConsensus());
    BOOST_CHECK(!probe.enableOpEngineRpc());
}

BOOST_AUTO_TEST_CASE(opEngineRpcAloneAccepted)
{
    LoaderProbe probe;
    auto pt = fromIni("[op_engine_rpc]\nenable=true\n");
    probe.loadOpEngineRpcConfig(pt);
    BOOST_CHECK_NO_THROW(probe.loadSingleNodeConsensusConfig(pt));
    BOOST_CHECK(!probe.enableSingleNodeConsensus());
    BOOST_CHECK(probe.enableOpEngineRpc());
}

BOOST_AUTO_TEST_CASE(bothDriversEnabledRejectedAtStartup)
{
    // Same load order as NodeConfig::loadConfig: loadOpEngineRpcConfig first, then
    // loadSingleNodeConsensusConfig, which fail-fasts on the conflicting combination.
    LoaderProbe probe;
    auto pt = fromIni(
        "[consensus]\nenable_single_node_consensus=true\n"
        "[op_engine_rpc]\nenable=true\n");
    probe.loadOpEngineRpcConfig(pt);
    BOOST_CHECK_THROW(probe.loadSingleNodeConsensusConfig(pt), InvalidConfig);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
