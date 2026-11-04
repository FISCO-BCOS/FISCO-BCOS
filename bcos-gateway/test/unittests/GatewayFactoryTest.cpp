/**
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @brief test for GatewayFactory
 * @file GatewayFactoryTest.cpp
 * @author: octopus
 * @date 2021-05-17
 */

#include "bcos-gateway/GatewayFactory.h"
#include "bcos-gateway/GatewayConfig.h"
#include "bcos-gateway/libnetwork/Host.h"
#include "bcos-utilities/testutils/TestPromptFixture.h"
#include <bcos-boostssl/context/Common.h>
#include <bcos-boostssl/context/ContextBuilder.h>
#include <bcos-boostssl/context/NtlsCompatibility.h>
#include <boost/algorithm/string.hpp>
#include <boost/test/unit_test.hpp>
#include <memory>
#include <vector>
using namespace bcos;
using namespace gateway;
using namespace bcos::test;

BOOST_FIXTURE_TEST_SUITE(GatewayFactoryTest, TestPromptFixture)

BOOST_AUTO_TEST_CASE(test_certPubHexHandler)
{
    auto factory = std::make_shared<GatewayFactory>("", "");
    {
        // sm cert
        std::string cert = "../../../bcos-gateway/test/unittests/data/sm_ca/sm_node.crt";
        std::string pubHex;
        auto r = factory->certPubHexHandler()(cert, pubHex);
        BOOST_CHECK(r);
        BOOST_CHECK_EQUAL(boost::to_lower_copy(pubHex),
            R"(045a0d065954bbc96dba0e9eea163d970a9187c3e5f1a6329daf2898acb888ac2d668f4e3b34b538dcd1be7839d86a0869ca6478913cfd4e46c1517586f9c0b3c0)");
    }

    {
        // RSA cert
        std::string cert("../../../bcos-gateway/test/unittests/data/ca/node.crt");
        std::string pubHex;
        auto r = factory->certPubHexHandler()(cert, pubHex);
        BOOST_CHECK(r);
    }
}

BOOST_AUTO_TEST_CASE(test_buildSSLContext)
{
    auto factory = std::make_shared<GatewayFactory>("", "");

    {
        // SM SSLContext
        std::string configIni("../../../bcos-gateway/test/unittests/data/config/config_ipv6.ini");
        auto config = std::make_shared<GatewayConfig>();
        config->initConfig(configIni);

        {
            factory->buildSSLContext(true, config->sslServerMode(), config->smCertConfig());
        }

        {
            factory->buildSSLContext(false, config->sslClientMode(), config->smCertConfig());
        }
    }

    {
        // SSLContext
        std::string configIni("../../../bcos-gateway/test/unittests/data/config/config_ipv4.ini");
        auto config = std::make_shared<GatewayConfig>();
        config->initConfig(configIni);

        {
            factory->buildSSLContext(true, config->sslServerMode(), config->certConfig());
        }

        {
            factory->buildSSLContext(false, config->sslClientMode(), config->certConfig());
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()

namespace
{
namespace context = bcos::boostssl::context;
struct TestHost : Host
{
    using Host::Host;
    using Host::newVerifyCallback;
};

struct NodeIdProbe
{
    std::function<bool(bool, boost::asio::ssl::verify_context&)> callback;
    std::vector<X509*> depth0Certificates;
    std::shared_ptr<std::string> nodeId;
};

int nodeIdProbeIndex()
{
    static const int index = SSL_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
    return index;
}

int captureNodeId(int preverified, X509_STORE_CTX* storeContext)
{
    auto* ssl = static_cast<SSL*>(
        X509_STORE_CTX_get_ex_data(storeContext, SSL_get_ex_data_X509_STORE_CTX_idx()));
    auto* probe = static_cast<NodeIdProbe*>(SSL_get_ex_data(ssl, nodeIdProbeIndex()));
    if (probe && X509_STORE_CTX_get_error_depth(storeContext) == 0)
    {
        probe->depth0Certificates.push_back(X509_STORE_CTX_get_current_cert(storeContext));
    }
    boost::asio::ssl::verify_context verifyContext(storeContext);
    return probe && probe->callback(preverified != 0, verifyContext) ? 1 : 0;
}

using Ssl = std::unique_ptr<SSL, decltype(&SSL_free)>;
using StoreContext = std::unique_ptr<X509_STORE_CTX, decltype(&X509_STORE_CTX_free)>;
using Certificate = std::unique_ptr<X509, decltype(&X509_free)>;
struct StackDeleter
{
    void operator()(STACK_OF(X509) * stack) const { sk_X509_free(stack); }
};
using CertificateStack = std::unique_ptr<STACK_OF(X509), StackDeleter>;

Certificate loadCertificate(context::ContextBuilder& builder, const std::string& path)
{
    return Certificate(bcos::boostssl::toX509(builder.readFileContent(path).c_str()), &X509_free);
}

std::string verifyLegacyNodeId(const std::vector<X509*>& certificates, X509* sign)
{
    const std::string dir = BCOS_NTLS_TEST_CERT_DIR;
    context::ContextBuilder builder;
    context::ContextConfig::SMCertConfig cert;
    cert.caCert = dir + "sm_ca.crt";
    cert.nodeCert = dir + "sm_sdk.crt";
    cert.nodeKey = dir + "sm_sdk.key";
    cert.enNodeCert = dir + "sm_ensdk.crt";
    cert.enNodeKey = dir + "sm_ensdk.key";
    cert.enableLegacyChain = true;
    context::ContextConfig config;
    config.setSslType("sm_ssl");
    config.setIsCertPath(true);
    config.setSmCertConfig(cert);
    auto sslContext = builder.buildSslContext(true, config);

    GatewayFactory factory("", "");
    auto host = std::make_shared<TestHost>(nullptr, nullptr, nullptr);
    host->setSSLContextPubHandler(factory.sslContextPubHandler());
    host->setSSLContextPubHandlerWithoutExtInfo(factory.sslContextPubHandlerWithoutExtInfo());
    host->setEnableSslVerify(true);
    std::string signingNodeId;
    BOOST_REQUIRE(factory.sslContextPubHandlerWithoutExtInfo()(sign, signingNodeId));
    host->setPeerWhitelist(PeerBlackWhitelist(PeerBlackWhitelist::Type::Whitelist,
        std::set<std::string>{boost::to_upper_copy(signingNodeId)}, true));
    auto nodeId = std::make_shared<std::string>();
    NodeIdProbe probe{host->newVerifyCallback(nodeId), {}, nodeId};
    Ssl ssl(SSL_new(sslContext->native_handle()), &SSL_free);
    BOOST_REQUIRE(ssl);
    BOOST_REQUIRE_GT(nodeIdProbeIndex(), -1);
    BOOST_REQUIRE_EQUAL(SSL_set_ex_data(ssl.get(), nodeIdProbeIndex(), &probe), 1);
    SSL_set_verify(ssl.get(), SSL_VERIFY_PEER, captureNodeId);

    CertificateStack chain(sk_X509_new_null());
    BOOST_REQUIRE(chain);
    for (auto* certificate : certificates)
    {
        BOOST_REQUIRE_NE(sk_X509_push(chain.get(), certificate), 0);
    }
    BOOST_REQUIRE_NE(sk_X509_push(chain.get(), sk_X509_shift(chain.get())), 0);
    StoreContext storeContext(X509_STORE_CTX_new(), &X509_STORE_CTX_free);
    BOOST_REQUIRE(storeContext);
    BOOST_REQUIRE_EQUAL(
        X509_STORE_CTX_init(storeContext.get(), SSL_CTX_get_cert_store(sslContext->native_handle()),
            sk_X509_value(chain.get(), 0), chain.get()),
        1);
    BOOST_REQUIRE_EQUAL(X509_STORE_CTX_set_default(storeContext.get(), "ssl_server"), 1);
    BOOST_REQUIRE_EQUAL(X509_STORE_CTX_set_ex_data(
                            storeContext.get(), SSL_get_ex_data_X509_STORE_CTX_idx(), ssl.get()),
        1);
    BOOST_REQUIRE_EQUAL(context::verifyLegacyNtlsChain(storeContext.get(), nullptr), 1);
    BOOST_REQUIRE(sk_X509_unshift(chain.get(), sk_X509_pop(chain.get())) != 0);
    BOOST_REQUIRE_EQUAL(context::verifyLegacyNtlsChain(storeContext.get(), nullptr), 1);
    BOOST_REQUIRE(!probe.depth0Certificates.empty());
    for (auto* certificate : probe.depth0Certificates)
    {
        BOOST_CHECK_EQUAL(certificate, sign);
    }
    return *probe.nodeId;
}
}  // namespace

BOOST_FIXTURE_TEST_SUITE(GatewayFactoryLegacyNtlsTest, TestPromptFixture)

BOOST_AUTO_TEST_CASE(nodeIdComesFromSigningCertificate)
{
    context::ContextBuilder builder;
    const std::string dir = BCOS_NTLS_TEST_CERT_DIR;
    auto sign = loadCertificate(builder, dir + "sm_sdk.crt");
    auto enc = loadCertificate(builder, dir + "sm_ensdk.crt");
    auto ca = loadCertificate(builder, dir + "sm_ca.crt");
    BOOST_REQUIRE(sign && enc && ca);

    GatewayFactory factory("", "");
    std::string expected;
    std::string encNodeId;
    BOOST_REQUIRE(factory.sslContextPubHandler()(sign.get(), expected));
    BOOST_REQUIRE(factory.sslContextPubHandler()(enc.get(), encNodeId));
    for (const auto& certificates : {std::vector<X509*>{sign.get(), ca.get(), enc.get()},
             std::vector<X509*>{ca.get(), enc.get(), sign.get()}})
    {
        const auto nodeId = verifyLegacyNodeId(certificates, sign.get());
        BOOST_CHECK(nodeId.starts_with(expected));
        BOOST_CHECK(!nodeId.starts_with(encNodeId));
    }
}

BOOST_AUTO_TEST_SUITE_END()
