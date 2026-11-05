/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */
// Tests for the config.ini loaders of the Engine API listener ([engine_rpc]) and the
// [ethereum] network keys. Which lanes run the listener is NodeConfigBlockDriverTest.

#include "ExceptionCheck.h"
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
    BOOST_CHECK_EQUAL(probe.engineApiListener().listenIP, "127.0.0.1");
    BOOST_CHECK_EQUAL(probe.engineApiListener().listenPort, 8551);
    BOOST_CHECK_EQUAL(probe.engineApiListener().jwtSecretFile, "conf/engine/jwt.hex");
    BOOST_CHECK(!probe.engineApiListener().enableMinerApi);
}

BOOST_AUTO_TEST_CASE(engineRpcPopulated)
{
    LoaderProbe probe;
    auto pt = fromIni(
        "[engine_rpc]\nlisten_ip=0.0.0.0\nlisten_port=9551\n"
        "jwt_secret_file=conf/custom-jwt.hex\nclock_skew_secs=30\n");
    probe.loadEngineRpcConfig(pt);
    BOOST_CHECK_EQUAL(probe.engineApiListener().listenIP, "0.0.0.0");
    BOOST_CHECK_EQUAL(probe.engineApiListener().listenPort, 9551);
    BOOST_CHECK_EQUAL(probe.engineApiListener().jwtSecretFile, "conf/custom-jwt.hex");
    BOOST_CHECK_EQUAL(probe.engineApiListener().clockSkewSecs, 30);
}

// The OP miner namespace (miner_setMaxDASize) is opt-in: it writes the node-wide DA caps, so
// a listener that has not enabled it must not register it at all. Default off is load-bearing —
// an OP node creates the caps, so the runtime MethodNotFound guard would not stop a public caller.
BOOST_AUTO_TEST_CASE(minerApiDefaultsOff)
{
    LoaderProbe probe;
    probe.loadWeb3RpcConfig(fromIni("[web3_rpc]\nenable=true\nlisten_port=8545\n"));
    BOOST_CHECK(!probe.enableMinerApi());
}

// The two listeners configure the miner namespace independently: the [engine_rpc] key must
// not touch the web3 (8545) switch and vice versa — enabling the batcher handshake on the
// private port can never expose it on the public one. (That the [engine_rpc] key takes
// effect on the OP lane only is NodeConfigBlockDriverTest.)
BOOST_AUTO_TEST_CASE(minerApiScopedPerListener)
{
    LoaderProbe probe;
    probe.loadEngineRpcConfig(fromIni("[engine_rpc]\nenable_miner_api=true\n"));
    probe.loadWeb3RpcConfig(fromIni("[web3_rpc]\nenable=true\nlisten_port=8545\n"));
    BOOST_CHECK(probe.engineApiListener().enableMinerApi);
    BOOST_CHECK(!probe.enableMinerApi());

    LoaderProbe probe2;
    probe2.loadEngineRpcConfig({});
    probe2.loadWeb3RpcConfig(
        fromIni("[web3_rpc]\nenable=true\nlisten_port=8545\nenable_miner_api=true\n"));
    BOOST_CHECK(probe2.enableMinerApi());
    BOOST_CHECK(!probe2.engineApiListener().enableMinerApi);
}

// [ethereum] mempool_max_txs_per_sender: default 64, 0 lifts the cap, out-of-range values are
// config errors at load time.
BOOST_AUTO_TEST_CASE(ethereumMempoolMaxTxsPerSender)
{
    {
        LoaderProbe probe;
        BOOST_CHECK_NO_THROW(probe.loadEthereumConfig(fromIni("[ethereum]\n")));
        BOOST_CHECK_EQUAL(probe.ethereumMempoolMaxTxsPerSender(), 64);
    }
    {
        LoaderProbe probe;
        BOOST_CHECK_NO_THROW(
            probe.loadEthereumConfig(fromIni("[ethereum]\nmempool_max_txs_per_sender=0\n")));
        BOOST_CHECK_EQUAL(probe.ethereumMempoolMaxTxsPerSender(), 0);
    }
    {
        LoaderProbe probe;
        BOOST_CHECK_NO_THROW(
            probe.loadEthereumConfig(fromIni("[ethereum]\nmempool_max_txs_per_sender=16\n")));
        BOOST_CHECK_EQUAL(probe.ethereumMempoolMaxTxsPerSender(), 16);
    }
    for (auto const* bad : {"-1", "10000001"})
    {
        LoaderProbe probe;
        BOOST_CHECK_EXCEPTION(
            probe.loadEthereumConfig(
                fromIni(std::string("[ethereum]\nmempool_max_txs_per_sender=") + bad + "\n")),
            InvalidConfig, [](InvalidConfig const& e) {
                return errinfoContains(e, "mempool_max_txs_per_sender must be in [0, 10000000]");
            });
    }
}

BOOST_AUTO_TEST_CASE(ethereumDepositContractAddress)
{
    // Default (key absent or empty): the Ethereum mainnet deposit contract.
    constexpr std::string_view c_mainnetDeposit = "00000000219ab540356cbb839cbe05303d7705fa";
    {
        LoaderProbe probe;
        auto pt = fromIni("[ethereum]\n");
        BOOST_CHECK_NO_THROW(probe.loadEthereumConfig(pt));
        BOOST_CHECK_EQUAL(probe.ethereumDepositContractAddress().hex(), c_mainnetDeposit);
    }
    {
        LoaderProbe probe;
        auto pt = fromIni("[ethereum]\ndeposit_contract_address=\n");
        BOOST_CHECK_NO_THROW(probe.loadEthereumConfig(pt));
        BOOST_CHECK_EQUAL(probe.ethereumDepositContractAddress().hex(), c_mainnetDeposit);
    }
    // A custom value (here: Sepolia) is honored, case-insensitively.
    {
        LoaderProbe probe;
        auto pt = fromIni(
            "[ethereum]\ndeposit_contract_address="
            "0x7f02c3E3c98b133055b8b348b2ac625669182295\n");
        BOOST_CHECK_NO_THROW(probe.loadEthereumConfig(pt));
        BOOST_CHECK_EQUAL(probe.ethereumDepositContractAddress().hex(),
            "7f02c3e3c98b133055b8b348b2ac625669182295");
    }
    // Non-hex and wrong-length values are rejected.
    {
        LoaderProbe probe;
        auto pt = fromIni(
            "[ethereum]\ndeposit_contract_address="
            "0xzz02c3e3c98b133055b8b348b2ac625669182295\n");
        BOOST_CHECK_THROW(probe.loadEthereumConfig(pt), InvalidConfig);
    }
    {
        LoaderProbe probe;
        auto pt =
            fromIni("[ethereum]\ndeposit_contract_address=0x7f02c3e3c98b133055b8b348b2ac6256\n");
        BOOST_CHECK_THROW(probe.loadEthereumConfig(pt), InvalidConfig);
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
