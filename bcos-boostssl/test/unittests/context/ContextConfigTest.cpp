/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include <bcos-boostssl/context/ContextConfig.h>
#include <boost/test/unit_test.hpp>
#include <cstdio>
#include <fstream>

using namespace bcos::boostssl::context;

namespace bcos::test
{
namespace
{
void touchFile(const std::string& path)
{
    std::ofstream out(path);
    out << "x";
    out.close();
}

std::string writeTempIni(const std::string& content)
{
    auto dir =
        boost::filesystem::temp_directory_path() / boost::filesystem::unique_path("ctxcfg-%%%%");
    boost::filesystem::create_directories(dir);
    std::string path = (dir / "config.ini").string();
    std::ofstream out(path);
    out << content;
    out.close();
    return path;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(ContextConfigTest)

BOOST_AUTO_TEST_CASE(settersAndGettersRoundTrip)
{
    ContextConfig cfg;
    cfg.setIsCertPath(false);
    BOOST_CHECK(!cfg.isCertPath());
    cfg.setSslType("sm_ssl");
    BOOST_CHECK_EQUAL(cfg.sslType(), "sm_ssl");

    ContextConfig::CertConfig cert;
    cert.caCert = "ca";
    cert.nodeCert = "nc";
    cert.nodeKey = "nk";
    cfg.setCertConfig(cert);
    BOOST_CHECK_EQUAL(cfg.certConfig().caCert, "ca");
    BOOST_CHECK_EQUAL(cfg.certConfig().nodeCert, "nc");

    ContextConfig::SMCertConfig sm;
    sm.caCert = "sca";
    sm.enNodeCert = "enc";
    cfg.setSmCertConfig(sm);
    BOOST_CHECK_EQUAL(cfg.smCertConfig().caCert, "sca");
    BOOST_CHECK_EQUAL(cfg.smCertConfig().enNodeCert, "enc");
}

BOOST_AUTO_TEST_CASE(initConfigSslType)
{
    auto path = writeTempIni(
        "[common]\nssl_type=ssl\n[cert]\nca_path=.\nca_cert=ca.crt\nnode_cert=n.crt\nnode_key=n."
        "key\n");
    auto dir = boost::filesystem::path(path).parent_path();
    // initCertConfig calls checkFileExist on each cert — create them. ca_path "."
    // resolves relative to CWD, so write the files into the cwd.
    touchFile("ca.crt");
    touchFile("n.crt");
    touchFile("n.key");

    ContextConfig cfg;
    BOOST_REQUIRE_NO_THROW(cfg.initConfig(path));
    BOOST_CHECK_EQUAL(cfg.sslType(), "ssl");
    BOOST_CHECK_EQUAL(cfg.certConfig().caCert, "./ca.crt");
    BOOST_CHECK_EQUAL(cfg.certConfig().nodeKey, "./n.key");

    std::remove("ca.crt");
    std::remove("n.crt");
    std::remove("n.key");
}

BOOST_AUTO_TEST_CASE(initConfigSmSslType)
{
    auto path = writeTempIni(
        "[common]\nssl_type=sm_ssl\n[cert]\nca_path=.\nsm_ca_cert=sca.crt\nsm_node_cert=sn.crt\nsm_"
        "node_key=sn.key\nsm_ennode_cert=en.crt\nsm_ennode_key=en.key\n");
    for (auto* f : {"sca.crt", "sn.crt", "sn.key", "en.crt", "en.key"})
    {
        touchFile(f);
    }

    ContextConfig cfg;
    BOOST_REQUIRE_NO_THROW(cfg.initConfig(path));
    BOOST_CHECK_EQUAL(cfg.sslType(), "sm_ssl");
    BOOST_CHECK_EQUAL(cfg.smCertConfig().caCert, "./sca.crt");
    BOOST_CHECK_EQUAL(cfg.smCertConfig().enNodeCert, "./en.crt");

    for (auto* f : {"sca.crt", "sn.crt", "sn.key", "en.crt", "en.key"})
    {
        std::remove(f);
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
