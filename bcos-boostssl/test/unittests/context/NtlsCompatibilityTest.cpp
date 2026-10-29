/**
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @file NtlsCompatibilityTest.cpp
 * @brief Tests for legacy NTLS certificate-chain verification.
 */
#include <bcos-boostssl/context/Common.h>
#include <bcos-boostssl/context/ContextBuilder.h>
#include <bcos-boostssl/context/NtlsCompatibility.h>
#include <boost/test/unit_test.hpp>
#include <memory>
#include <vector>

using namespace bcos::boostssl::context;

namespace
{
constexpr const char* NTLS_CERT_DIR = "../bcos-boostssl/test/unittests/data/sm_ca/";

struct NtlsSslDeleter
{
    void operator()(SSL* ssl) const { SSL_free(ssl); }
};
using SslPtr = std::unique_ptr<SSL, NtlsSslDeleter>;
using StoreContext = std::unique_ptr<X509_STORE_CTX, decltype(&X509_STORE_CTX_free)>;
struct StackDeleter
{
    void operator()(STACK_OF(X509) * stack) const { sk_X509_free(stack); }
};
using CertificateStack = std::unique_ptr<STACK_OF(X509), StackDeleter>;
using Certificate = std::unique_ptr<X509, decltype(&X509_free)>;

Certificate loadCertificate(const char* file)
{
    return Certificate(
        bcos::boostssl::toX509(
            ContextBuilder().readFileContent(std::string(NTLS_CERT_DIR) + file).c_str()),
        &X509_free);
}

struct VerifyFixture
{
    explicit VerifyFixture(bool trusted = true)
    {
        ContextConfig::SMCertConfig cert;
        cert.caCert = std::string(NTLS_CERT_DIR) + "sm_ca.crt";
        cert.nodeCert = std::string(NTLS_CERT_DIR) + "sm_sdk.crt";
        cert.nodeKey = std::string(NTLS_CERT_DIR) + "sm_sdk.key";
        cert.enNodeCert = std::string(NTLS_CERT_DIR) + "sm_ensdk.crt";
        cert.enNodeKey = std::string(NTLS_CERT_DIR) + "sm_ensdk.key";
        cert.enableLegacyChain = true;
        ContextConfig config;
        config.setSslType("sm_ssl");
        config.setIsCertPath(true);
        config.setSmCertConfig(cert);
        sslContext = ContextBuilder().buildSslContext(true, config);
        if (!trusted)
        {
            SSL_CTX_set_cert_store(sslContext->native_handle(), X509_STORE_new());
        }
        ssl.reset(SSL_new(sslContext->native_handle()));
        BOOST_REQUIRE(ssl);
    }

    std::shared_ptr<boost::asio::ssl::context> sslContext;
    SslPtr ssl;
};

int callbackCalls = 0;
X509* lastCertificate = nullptr;
int recordCallback(int preverified, X509_STORE_CTX* context)
{
    ++callbackCalls;
    lastCertificate = X509_STORE_CTX_get_current_cert(context);
    return preverified;
}

void verifyRoundTrip(VerifyFixture& fixture, std::vector<X509*> certificates, X509* sign, X509* enc,
    bool expectSwapped = false, bool expectSuccess = true)
{
    CertificateStack chain(sk_X509_new_null());
    BOOST_REQUIRE(chain);
    for (auto* cert : certificates)
    {
        BOOST_REQUIRE(sk_X509_push(chain.get(), cert) != 0);
    }
    BOOST_REQUIRE(sk_X509_push(chain.get(), sk_X509_shift(chain.get())) != 0);

    StoreContext context(X509_STORE_CTX_new(), &X509_STORE_CTX_free);
    BOOST_REQUIRE(context);
    BOOST_REQUIRE_EQUAL(X509_STORE_CTX_init(context.get(),
                            SSL_CTX_get_cert_store(fixture.sslContext->native_handle()),
                            sk_X509_value(chain.get(), 0), chain.get()),
        1);
    BOOST_REQUIRE_EQUAL(X509_STORE_CTX_set_default(context.get(), "ssl_server"), 1);
    BOOST_REQUIRE_EQUAL(X509_STORE_CTX_set_ex_data(
                            context.get(), SSL_get_ex_data_X509_STORE_CTX_idx(), fixture.ssl.get()),
        1);

    const auto firstResult = verifyLegacyNtlsChain(context.get(), nullptr);
    if (!expectSuccess)
    {
        BOOST_CHECK_EQUAL(firstResult, 0);
        return;
    }
    BOOST_REQUIRE_EQUAL(firstResult, 1);
    BOOST_REQUIRE(sk_X509_unshift(chain.get(), sk_X509_pop(chain.get())) != 0);
    BOOST_REQUIRE_EQUAL(verifyLegacyNtlsChain(context.get(), nullptr), 1);

    BOOST_REQUIRE_EQUAL(sk_X509_num(chain.get()), static_cast<int>(certificates.size()));
    BOOST_CHECK_EQUAL(sk_X509_value(chain.get(), 0), expectSwapped ? enc : sign);
    BOOST_CHECK_EQUAL(sk_X509_value(chain.get(), 1), expectSwapped ? sign : enc);
    if (certificates.size() == 3)
    {
        BOOST_CHECK_GT(X509_check_ca(sk_X509_value(chain.get(), 2)), 0);
    }
}

BOOST_AUTO_TEST_SUITE(NtlsCompatibilityTest)

BOOST_AUTO_TEST_CASE(acceptsStandardAndLegacyChainOrders)
{
    Certificate sign = loadCertificate("sm_sdk.crt");
    Certificate enc = loadCertificate("sm_ensdk.crt");
    Certificate ca = loadCertificate("sm_ca.crt");
    BOOST_REQUIRE(sign && enc && ca);

    for (auto certificates : {std::vector<X509*>{sign.get(), enc.get()},
             std::vector<X509*>{sign.get(), enc.get(), ca.get()},
             std::vector<X509*>{sign.get(), ca.get(), enc.get()},
             std::vector<X509*>{ca.get(), sign.get(), enc.get()}})
    {
        VerifyFixture fixture;
        verifyRoundTrip(fixture, certificates, sign.get(), enc.get());
    }
}

BOOST_AUTO_TEST_CASE(preservesSwappedPairForNativeRoleRejection)
{
    Certificate sign = loadCertificate("sm_sdk.crt");
    Certificate enc = loadCertificate("sm_ensdk.crt");
    Certificate ca = loadCertificate("sm_ca.crt");
    BOOST_REQUIRE(sign && enc && ca);

    for (auto certificates : {std::vector<X509*>{enc.get(), sign.get()},
             std::vector<X509*>{enc.get(), ca.get(), sign.get()}})
    {
        VerifyFixture fixture;
        verifyRoundTrip(fixture, certificates, sign.get(), enc.get(), true);
    }
}

BOOST_AUTO_TEST_CASE(rejectsUntrustedChainAndAmbiguousCertificates)
{
    Certificate sign = loadCertificate("sm_sdk.crt");
    Certificate enc = loadCertificate("sm_ensdk.crt");
    Certificate ca = loadCertificate("sm_ca.crt");
    BOOST_REQUIRE(sign && enc && ca);

    VerifyFixture untrusted(false);
    verifyRoundTrip(
        untrusted, {ca.get(), enc.get(), sign.get()}, sign.get(), enc.get(), false, false);

    VerifyFixture ambiguous;
    verifyRoundTrip(ambiguous, {sign.get(), sign.get()}, sign.get(), enc.get(), false, false);
}

BOOST_AUTO_TEST_CASE(rejectsInvalidSigningCertificateOnFirstInvocation)
{
    Certificate sign = loadCertificate("sm_sdk.crt");
    Certificate enc = loadCertificate("sm_ensdk.crt");
    Certificate ca = loadCertificate("sm_ca.crt");
    BOOST_REQUIRE(sign && enc && ca);
    const auto key = ContextBuilder().readFileContent(std::string(NTLS_CERT_DIR) + "sm_ensdk.key");
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> wrongKey(
        bcos::boostssl::toEvpPkey(key.c_str()), &EVP_PKEY_free);
    BOOST_REQUIRE(wrongKey);
    BOOST_REQUIRE_GT(X509_sign(sign.get(), wrongKey.get(), EVP_sm3()), 0);

    VerifyFixture fixture;
    verifyRoundTrip(
        fixture, {sign.get(), enc.get(), ca.get()}, sign.get(), enc.get(), false, false);
}

BOOST_AUTO_TEST_CASE(invokesExistingVerifyCallback)
{
    Certificate sign = loadCertificate("sm_sdk.crt");
    Certificate enc = loadCertificate("sm_ensdk.crt");
    Certificate ca = loadCertificate("sm_ca.crt");
    BOOST_REQUIRE(sign && enc && ca);

    VerifyFixture fixture;
    callbackCalls = 0;
    lastCertificate = nullptr;
    SSL_set_verify(fixture.ssl.get(), SSL_VERIFY_PEER, recordCallback);
    verifyRoundTrip(fixture, {sign.get(), ca.get(), enc.get()}, sign.get(), enc.get());
    BOOST_CHECK_GT(callbackCalls, 0);
    BOOST_CHECK_EQUAL(lastCertificate, sign.get());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace
