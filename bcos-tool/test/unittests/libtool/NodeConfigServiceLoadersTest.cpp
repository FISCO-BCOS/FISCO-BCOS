/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include "NodeConfigLoaderProbe.h"
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <boost/test/unit_test.hpp>
#include <filesystem>
#include <fstream>
#include <string>

using namespace bcos;
using namespace bcos::tool;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(NodeConfigServiceLoadersTest)


BOOST_AUTO_TEST_CASE(rpcConfigDefaultsAndPopulated)
{
    LoaderProbe a;
    a.loadRpcConfig({});  // all defaults
    BOOST_CHECK_EQUAL(a.rpcListenIP(), "0.0.0.0");

    LoaderProbe b;
    auto pt = fromIni(
        "[rpc]\nlisten_ip=1.2.3.4\nlisten_port=12345\nthread_count=4\nsm_ssl=true\n"
        "enable_ssl=true\nfilter_timeout=10\nreturn_input_params=false\n");
    b.loadRpcConfig(pt);
    BOOST_CHECK_EQUAL(b.rpcListenIP(), "1.2.3.4");
    BOOST_CHECK_EQUAL(b.rpcListenPort(), 12345);
    BOOST_CHECK(!b.rpcDisableSsl());  // enable_ssl=true → disableSsl=false
}


BOOST_AUTO_TEST_CASE(web3RpcConfigDefaultsAndPopulated)
{
    LoaderProbe a;
    a.loadWeb3RpcConfig({});
    BOOST_CHECK(!a.enableWeb3Rpc());
    // blockTag depth defaults: Ethereum-consistent safe=1 / finalized=2.
    BOOST_CHECK_EQUAL(a.web3SafeBlockDepth(), 1U);
    BOOST_CHECK_EQUAL(a.web3FinalizedBlockDepth(), 2U);

    LoaderProbe b;
    auto pt = fromIni(
        "[web3_rpc]\nenable=true\nlisten_port=8545\nthread_count=2\nenable_cors=false\n"
        "cors_allowed_origins=http://x\nsync_transaction=true\n"
        "safe_block_depth=3\nfinalized_block_depth=5\n");
    b.loadWeb3RpcConfig(pt);
    BOOST_CHECK(b.enableWeb3Rpc());
    BOOST_CHECK_EQUAL(b.web3SafeBlockDepth(), 3U);
    BOOST_CHECK_EQUAL(b.web3FinalizedBlockDepth(), 5U);
}


BOOST_AUTO_TEST_CASE(gatewayConfigDefaultsAndPopulated)
{
    LoaderProbe a;
    a.loadGatewayConfig({});
    LoaderProbe b;
    auto pt =
        fromIni("[p2p]\nlisten_ip=0.0.0.0\nlisten_port=30300\nsm_ssl=true\nnodes_file=n.json\n");
    BOOST_CHECK_NO_THROW(b.loadGatewayConfig(pt));
}


BOOST_AUTO_TEST_CASE(certConfigDefaultsAndPopulated)
{
    LoaderProbe a;
    a.loadCertConfig({});  // pure string concatenation, no file IO
    LoaderProbe b;
    auto pt = fromIni("[cert]\nca_path=/tmp\nca_cert=ca.crt\nnode_cert=n.crt\nnode_key=n.key\n");
    BOOST_CHECK_NO_THROW(b.loadCertConfig(pt));
}


BOOST_AUTO_TEST_CASE(serviceConfigWithoutTars)
{
    LoaderProbe a;  // without_tars_framework=false → skips tars proxy load
    BOOST_CHECK_NO_THROW(a.loadServiceConfig(
        fromIni("[service]\nrpc=app.rpc\ngateway=app.gw\nwithout_tars_framework=false\n")));
    LoaderProbe b;
    BOOST_CHECK_NO_THROW(
        b.loadWithoutTarsFrameworkConfig(fromIni("[service]\nwithout_tars_framework=true\n")));
}


BOOST_AUTO_TEST_CASE(tarsProxyConfigFromFile)
{
    auto path = std::filesystem::temp_directory_path() / "nc_tars_proxy_test.ini";
    {
        std::ofstream out(path);
        out << "[front]\nproxy.0=127.0.0.1:1234\n"
               "[rpc]\nproxy.0=127.0.0.1:1235\n"
               "[gateway]\nproxy.0=127.0.0.1:1236\n";
    }
    LoaderProbe cfg;
    cfg.setWithoutTarsFramework(true);
    cfg.loadTarsProxyConfig(path.string());  // parses sections → string2TarsEndPoint

    std::vector<tars::TC_Endpoint> eps;
    cfg.getTarsClientProxyEndpoints("front", eps);
    BOOST_CHECK_EQUAL(eps.size(), 1U);

    std::vector<tars::TC_Endpoint> missing;
    BOOST_CHECK_THROW(
        cfg.getTarsClientProxyEndpoints("no_such_service", missing), bcos::InvalidParameter);
    std::filesystem::remove(path);

    // a non-existent proxy file path → read_ini fails → wrapped InvalidParameter
    LoaderProbe bad;
    BOOST_CHECK_THROW(bad.loadTarsProxyConfig("/nonexistent/path/xyz.ini"), bcos::InvalidParameter);
}


BOOST_AUTO_TEST_CASE(nodeServiceConfigValidAndInvalid)
{
    LoaderProbe a;
    BOOST_CHECK_NO_THROW(a.loadNodeServiceConfig(
        "node0", fromIni("[service]\nnode_name=node0\nwithout_tars_framework=false\n"), false));

    LoaderProbe b;  // non-alnum node name → throws
    BOOST_CHECK_THROW(
        b.loadNodeServiceConfig("n", fromIni("[service]\nnode_name=bad!name\n"), false),
        bcos::tool::InvalidConfig);
}


BOOST_AUTO_TEST_CASE(checkServiceValidAndInvalid)
{
    LoaderProbe cfg;
    BOOST_CHECK_NO_THROW(cfg.checkService("service.rpc", "app.server"));
    BOOST_CHECK_THROW(cfg.checkService("service.rpc", ""), bcos::tool::InvalidConfig);
    BOOST_CHECK_THROW(cfg.checkService("service.rpc", "oneword"), bcos::tool::InvalidConfig);
    BOOST_CHECK_THROW(cfg.checkService("service.rpc", "bad!.name"), bcos::tool::InvalidConfig);
}


BOOST_AUTO_TEST_CASE(getServiceNameRequireAndNot)
{
    LoaderProbe cfg;
    // require=false → returns the raw name
    BOOST_CHECK_EQUAL(
        cfg.getServiceName(fromIni("[service]\nrpc=raw.name\n"), "service.rpc", "Obj", "", false),
        "raw.name");
    // require=true with a valid app.server name → checkService + getPrxDesc
    BOOST_CHECK_NO_THROW(
        cfg.getServiceName(fromIni("[service]\nrpc=app.server\n"), "service.rpc", "Obj", "", true));
}


BOOST_AUTO_TEST_CASE(certSettersRoundTrip)
{
    LoaderProbe cfg;
    cfg.setCertPath("/p");
    cfg.setCaCert("ca");
    cfg.setNodeCert("nc");
    cfg.setNodeKey("nk");
    cfg.setSmCaCert("smca");
    cfg.setSmNodeCert("smnc");
    cfg.setSmNodeKey("smnk");
    cfg.setEnSmNodeCert("ensmnc");
    cfg.setEnSmNodeKey("ensmnk");
    cfg.setWithoutTarsFramework(true);
    BOOST_CHECK_EQUAL(cfg.caCert(), "ca");
    BOOST_CHECK_EQUAL(cfg.nodeKey(), "nk");
    BOOST_CHECK_EQUAL(cfg.smCaCert(), "smca");
    BOOST_CHECK_EQUAL(cfg.enSmNodeKey(), "ensmnk");
    BOOST_CHECK(cfg.withoutTarsFramework());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
