#include <bcos-boostssl/context/ContextBuilder.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <boost/test/unit_test.hpp>
#include <array>
#include <cstring>
#include <memory>
#include <string>

using namespace bcos::boostssl::context;

namespace
{
constexpr const char* CERT_DIR = "../bcos-boostssl/test/unittests/data/sm_ca/";

ContextConfig makeConfig(bool content)
{
    ContextConfig::SMCertConfig cert;
    if (content)
    {
        ContextBuilder builder;
        cert.caCert = builder.readFileContent(std::string(CERT_DIR) + "sm_ca.crt");
        cert.nodeCert = builder.readFileContent(std::string(CERT_DIR) + "sm_sdk.crt");
        cert.nodeKey = builder.readFileContent(std::string(CERT_DIR) + "sm_sdk.key");
        cert.enNodeCert = builder.readFileContent(std::string(CERT_DIR) + "sm_ensdk.crt");
        cert.enNodeKey = builder.readFileContent(std::string(CERT_DIR) + "sm_ensdk.key");
    }
    else
    {
        cert.caCert = std::string(CERT_DIR) + "sm_ca.crt";
        cert.nodeCert = std::string(CERT_DIR) + "sm_sdk.crt";
        cert.nodeKey = std::string(CERT_DIR) + "sm_sdk.key";
        cert.enNodeCert = std::string(CERT_DIR) + "sm_ensdk.crt";
        cert.enNodeKey = std::string(CERT_DIR) + "sm_ensdk.key";
    }

    ContextConfig config;
    config.setSslType("sm_ssl");
    config.setIsCertPath(!content);
    config.setSmCertConfig(cert);
    return config;
}

struct SslDeleter
{
    void operator()(SSL* ssl) const { SSL_free(ssl); }
};
using SslPtr = std::unique_ptr<SSL, SslDeleter>;

void pump(BIO* source, BIO* destination)
{
    std::array<char, 16384> buffer{};
    int read = 0;
    while ((read = BIO_read(source, buffer.data(), buffer.size())) > 0)
    {
        BIO_write(destination, buffer.data(), read);
    }
}

void advanceHandshake(SSL* ssl)
{
    const int result = SSL_do_handshake(ssl);
    if (result == 1)
    {
        return;
    }
    const int error = SSL_get_error(ssl, result);
    if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE)
    {
        ERR_print_errors_fp(stderr);
    }
    BOOST_REQUIRE_MESSAGE(error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE,
        "SSL_do_handshake failed: " << error);
}

void verifyHandshake(bool content, const char* cipher)
{
    ContextBuilder builder;
    auto config = makeConfig(content);
    auto serverContext = builder.buildSslContext(true, config);
    auto clientContext = builder.buildSslContext(false, config);
    SslPtr server(SSL_new(serverContext->native_handle()));
    SslPtr client(SSL_new(clientContext->native_handle()));
    BOOST_REQUIRE(server);
    BOOST_REQUIRE(client);
    BOOST_REQUIRE_EQUAL(SSL_set_cipher_list(server.get(), cipher), 1);
    BOOST_REQUIRE_EQUAL(SSL_set_cipher_list(client.get(), cipher), 1);

    BIO* serverRead = BIO_new(BIO_s_mem());
    BIO* serverWrite = BIO_new(BIO_s_mem());
    BIO* clientRead = BIO_new(BIO_s_mem());
    BIO* clientWrite = BIO_new(BIO_s_mem());
    BOOST_REQUIRE(serverRead);
    BOOST_REQUIRE(serverWrite);
    BOOST_REQUIRE(clientRead);
    BOOST_REQUIRE(clientWrite);
    SSL_set_bio(server.get(), serverRead, serverWrite);
    SSL_set_bio(client.get(), clientRead, clientWrite);
    SSL_set_accept_state(server.get());
    SSL_set_connect_state(client.get());

    bool serverDone = false;
    bool clientDone = false;
    for (size_t i = 0; i < 100 && (!serverDone || !clientDone); ++i)
    {
        if (!clientDone)
        {
            advanceHandshake(client.get());
        }
        pump(clientWrite, serverRead);
        if (!serverDone)
        {
            advanceHandshake(server.get());
        }
        pump(serverWrite, clientRead);
        clientDone = SSL_is_init_finished(client.get()) != 0;
        serverDone = SSL_is_init_finished(server.get()) != 0;
    }
    BOOST_REQUIRE(clientDone);
    BOOST_REQUIRE(serverDone);
    BOOST_CHECK_EQUAL(SSL_get_verify_result(client.get()), X509_V_OK);
    BOOST_CHECK_EQUAL(SSL_get_verify_result(server.get()), X509_V_OK);
    BOOST_CHECK_EQUAL(std::string(SSL_get_cipher(client.get())), cipher);
    BOOST_CHECK_EQUAL(std::string(SSL_get_cipher(server.get())), cipher);
    X509* clientPeer = SSL_get_peer_certificate(client.get());
    X509* serverPeer = SSL_get_peer_certificate(server.get());
    BOOST_CHECK(clientPeer);
    BOOST_CHECK(serverPeer);
    X509_free(clientPeer);
    X509_free(serverPeer);

    const std::array<char, 4> request{'p', 'i', 'n', 'g'};
    const std::array<char, 4> response{'p', 'o', 'n', 'g'};
    BOOST_REQUIRE_EQUAL(SSL_write(client.get(), request.data(), request.size()), request.size());
    pump(clientWrite, serverRead);
    std::array<char, 4> received{};
    BOOST_REQUIRE_EQUAL(SSL_read(server.get(), received.data(), received.size()), received.size());
    BOOST_CHECK_EQUAL(std::memcmp(received.data(), request.data(), request.size()), 0);

    BOOST_REQUIRE_EQUAL(SSL_write(server.get(), response.data(), response.size()), response.size());
    pump(serverWrite, clientRead);
    received.fill(0);
    BOOST_REQUIRE_EQUAL(SSL_read(client.get(), received.data(), received.size()), received.size());
    BOOST_CHECK_EQUAL(std::memcmp(received.data(), response.data(), response.size()), 0);
}

BOOST_AUTO_TEST_SUITE(ContextBuilderTest)

BOOST_AUTO_TEST_CASE(ntlsHandshakeFromFiles)
{
    verifyHandshake(false, "ECC-SM2-SM4-CBC-SM3");
    verifyHandshake(false, "ECDHE-SM2-SM4-CBC-SM3");
}

BOOST_AUTO_TEST_CASE(ntlsHandshakeFromContents)
{
    verifyHandshake(true, "ECC-SM2-SM4-CBC-SM3");
    verifyHandshake(true, "ECDHE-SM2-SM4-CBC-SM3");
}

BOOST_AUTO_TEST_CASE(rejectsMismatchedAndWrongUsageCertificates)
{
    ContextBuilder builder;
    auto mismatched = makeConfig(true);
    auto mismatchedCert = mismatched.smCertConfig();
    mismatchedCert.enNodeKey = mismatchedCert.nodeKey;
    mismatched.setSmCertConfig(mismatchedCert);
    BOOST_CHECK_THROW(builder.buildSslContext(true, mismatched), std::exception);

    auto wrongUsage = makeConfig(true);
    auto wrongUsageCert = wrongUsage.smCertConfig();
    wrongUsageCert.enNodeCert = wrongUsageCert.nodeCert;
    wrongUsage.setSmCertConfig(wrongUsageCert);
    BOOST_CHECK_THROW(builder.buildSslContext(true, wrongUsage), std::exception);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace
