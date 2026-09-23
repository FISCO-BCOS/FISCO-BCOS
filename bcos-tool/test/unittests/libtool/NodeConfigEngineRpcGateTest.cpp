/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */
// Tests for the [engine_rpc] section (the EL-mode Engine API listener): parsing, and the
// assembly gates that bind it to ethereum.mode=el and keep it off the OP/single-node lanes.

#include "NodeConfigLoaderProbe.h"
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::tool;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(NodeConfigEngineRpcGateTest)

BOOST_AUTO_TEST_CASE(engineRpcDefaults)
{
    LoaderProbe probe;
    probe.loadEngineRpcConfig({});
    BOOST_CHECK(!probe.enableEngineRpc());
    BOOST_CHECK_EQUAL(probe.engineRpcListenIP(), "127.0.0.1");
    BOOST_CHECK_EQUAL(probe.engineRpcListenPort(), 8551);
    BOOST_CHECK_EQUAL(probe.engineJwtSecretFile(), "conf/engine/jwt.hex");
}

BOOST_AUTO_TEST_CASE(engineRpcPopulated)
{
    LoaderProbe probe;
    auto pt = fromIni(
        "[engine_rpc]\nenable=true\nlisten_ip=0.0.0.0\nlisten_port=9551\n"
        "jwt_secret_file=conf/custom-jwt.hex\nclock_skew_secs=30\n");
    probe.loadEngineRpcConfig(pt);
    BOOST_CHECK(probe.enableEngineRpc());
    BOOST_CHECK_EQUAL(probe.engineRpcListenIP(), "0.0.0.0");
    BOOST_CHECK_EQUAL(probe.engineRpcListenPort(), 9551);
    BOOST_CHECK_EQUAL(probe.engineJwtSecretFile(), "conf/custom-jwt.hex");
    BOOST_CHECK_EQUAL(probe.engineClockSkewSecs(), 30);
}

BOOST_AUTO_TEST_CASE(engineRpcWithELModeAccepted)
{
    // Same load order as NodeConfig::loadConfig: [engine_rpc] first, [ethereum] second.
    LoaderProbe probe;
    auto pt = fromIni("[engine_rpc]\nenable=true\n[ethereum]\nmode=el\n");
    probe.loadEngineRpcConfig(pt);
    BOOST_CHECK_NO_THROW(probe.loadEthereumConfig(pt));
    BOOST_CHECK(probe.enableEngineRpc());
    BOOST_CHECK(probe.ethereumELModeEnabled());
}

BOOST_AUTO_TEST_CASE(engineRpcWithoutELModeRejected)
{
    LoaderProbe probe;
    auto pt = fromIni("[engine_rpc]\nenable=true\n");
    probe.loadEngineRpcConfig(pt);
    BOOST_CHECK_THROW(probe.loadEthereumConfig(pt), InvalidConfig);
}

BOOST_AUTO_TEST_CASE(engineRpcAndOpEngineRpcRejected)
{
    LoaderProbe probe;
    auto pt = fromIni("[engine_rpc]\nenable=true\n[op_engine_rpc]\nenable=true\n");
    probe.loadEngineRpcConfig(pt);
    BOOST_CHECK_THROW(probe.loadOpEngineRpcConfig(pt), InvalidConfig);

    // Reverse load order: the symmetric check in loadEngineRpcConfig fires.
    LoaderProbe probe2;
    probe2.loadOpEngineRpcConfig(pt);
    BOOST_CHECK_THROW(probe2.loadEngineRpcConfig(pt), InvalidConfig);
}

BOOST_AUTO_TEST_CASE(engineRpcAndSingleNodeConsensusRejected)
{
    LoaderProbe probe;
    auto pt = fromIni("[engine_rpc]\nenable=true\n[consensus]\nenable_single_node_consensus=true\n");
    probe.loadEngineRpcConfig(pt);
    BOOST_CHECK_THROW(probe.loadSingleNodeConsensusConfig(pt), InvalidConfig);

    LoaderProbe probe2;
    probe2.loadSingleNodeConsensusConfig(pt);
    BOOST_CHECK_THROW(probe2.loadEngineRpcConfig(pt), InvalidConfig);
}

BOOST_AUTO_TEST_CASE(elModeWithOpEngineRpcStillRejected)
{
    // Opening EL mode to [engine_rpc] must not loosen the existing EL <-> op_engine_rpc and
    // EL <-> single-node-consensus exclusions.
    LoaderProbe probe;
    auto pt = fromIni("[ethereum]\nmode=el\n[op_engine_rpc]\nenable=true\n");
    probe.loadOpEngineRpcConfig(pt);
    BOOST_CHECK_THROW(probe.loadEthereumConfig(pt), InvalidConfig);

    LoaderProbe probe2;
    auto pt2 = fromIni("[ethereum]\nmode=el\n[consensus]\nenable_single_node_consensus=true\n");
    probe2.loadSingleNodeConsensusConfig(pt2);
    BOOST_CHECK_THROW(probe2.loadEthereumConfig(pt2), InvalidConfig);
}

BOOST_AUTO_TEST_CASE(ethereumDepositContractAddress)
{
    // Default (key absent or empty): the Ethereum mainnet deposit contract.
    constexpr std::string_view c_mainnetDeposit = "00000000219ab540356cbb839cbe05303d7705fa";
    {
        LoaderProbe probe;
        auto pt = fromIni("[ethereum]\nmode=none\n");
        BOOST_CHECK_NO_THROW(probe.loadEthereumConfig(pt));
        BOOST_CHECK_EQUAL(probe.ethereumDepositContractAddress().hex(), c_mainnetDeposit);
    }
    {
        LoaderProbe probe;
        auto pt = fromIni("[ethereum]\nmode=el\ndeposit_contract_address=\n");
        BOOST_CHECK_NO_THROW(probe.loadEthereumConfig(pt));
        BOOST_CHECK_EQUAL(probe.ethereumDepositContractAddress().hex(), c_mainnetDeposit);
    }
    // A custom value (here: Sepolia) is honored, case-insensitively.
    {
        LoaderProbe probe;
        auto pt = fromIni(
            "[ethereum]\nmode=el\ndeposit_contract_address=0x7f02c3E3c98b133055b8b348b2ac625669182295\n");
        BOOST_CHECK_NO_THROW(probe.loadEthereumConfig(pt));
        BOOST_CHECK_EQUAL(
            probe.ethereumDepositContractAddress().hex(), "7f02c3e3c98b133055b8b348b2ac625669182295");
    }
    // Non-hex and wrong-length values are rejected.
    {
        LoaderProbe probe;
        auto pt = fromIni(
            "[ethereum]\nmode=el\ndeposit_contract_address=0xzz02c3e3c98b133055b8b348b2ac625669182295\n");
        BOOST_CHECK_THROW(probe.loadEthereumConfig(pt), InvalidConfig);
    }
    {
        LoaderProbe probe;
        auto pt = fromIni(
            "[ethereum]\nmode=el\ndeposit_contract_address=0x7f02c3e3c98b133055b8b348b2ac6256\n");
        BOOST_CHECK_THROW(probe.loadEthereumConfig(pt), InvalidConfig);
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
