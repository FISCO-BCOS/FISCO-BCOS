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
 * @brief jwt tests
 * @file JwtTest.cpp
 * @date 2026.05.20
 */

#include <chrono>
#include <bcos-rpc/jwtAuth/JwtConfig.h>
#include <bcos-rpc/jwtAuth/JwtErrors.h>
#include <bcos-rpc/jwtAuth/JwtToken.h>
#include <bcos-rpc/jwtAuth/JwtVerifier.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <jwt-cpp/traits/kazuho-picojson/defaults.h>
#include <boost/test/unit_test.hpp>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>
#include <atomic>

using namespace bcos;
using namespace bcos::rpc;

namespace
{
std::string buildJwt(std::string_view _alg, std::optional<std::string> _typ,
    std::optional<int64_t> _iat, std::optional<std::string> _id, std::optional<std::string> _clv,
    std::string_view _secretHex)
{
    auto secretBytes = fromHex(std::string(_secretHex));
    std::string secret(
        reinterpret_cast<const char*>(secretBytes.data()), secretBytes.size());
    auto builder = ::jwt::create().set_algorithm(std::string(_alg));
    if (_typ.has_value())
    {
        builder.set_type(_typ.value());
    }
    if (_iat.has_value())
    {
        builder.set_issued_at(std::chrono::system_clock::time_point{std::chrono::seconds{
            _iat.value()}});
    }
    if (_id.has_value())
    {
        builder.set_payload_claim("id", ::jwt::claim(_id.value()));
    }
    if (_clv.has_value())
    {
        builder.set_payload_claim("clv", ::jwt::claim(_clv.value()));
    }
    return builder.sign(::jwt::algorithm::hs256{std::move(secret)});
}

// Build a JWT with alg=none by manually constructing the compact format.
// This helper is used to test that the verifier rejects alg:none tokens.
std::string buildJwtNone()
{
    auto header = ::jwt::builder<::jwt::traits::kazuho_picojson>()
                      .set_algorithm("none")
                      .set_type("JWT");
    auto token = header.sign(::jwt::algorithm::none{});
    // Replace signature part with empty (jwt-cpp's none algorithm may leave a signature)
    auto dotPos = token.rfind('.');
    if (dotPos != std::string::npos)
    {
        token = token.substr(0, dotPos + 1);
    }
    return token;
}

std::string writeSecretFile(std::string const& _hexSecret)
{
    auto tempDir = std::filesystem::temp_directory_path();
    // Use nanosecond timestamp + atomic counter to guarantee uniqueness: JwtTest
    // creates multiple secret files in quick succession and utcTime() (ms) can
    // collide, causing one test to read another's secret file.
    static std::atomic<uint64_t> counter{0};
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch())
                  .count();
    auto path = tempDir /
                ("fisco-bcos-jwt-secret-" + std::to_string(ns) + "-" +
                    std::to_string(counter.fetch_add(1)) + ".hex");
    std::ofstream ofs(path);
    ofs << _hexSecret;
    ofs.close();
    return path.string();
}
}  // namespace

BOOST_AUTO_TEST_SUITE(JwtTest)

BOOST_AUTO_TEST_CASE(testJwtTokenDecode)
{
    auto jwt = buildJwt("HS256", "JWT", 1710000000, "client1", "1.0",
        "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");

    auto decoded = ::jwt::decode(std::string(jwt));
    auto token = JwtToken::decode(decoded);
    BOOST_CHECK_EQUAL(token.header().alg, "HS256");
    BOOST_CHECK_EQUAL(token.header().typ, "JWT");
    BOOST_CHECK_EQUAL(token.claims().iat, 1710000000);
    BOOST_CHECK(token.claims().id.has_value());
    BOOST_CHECK_EQUAL(token.claims().id.value(), "client1");
    BOOST_CHECK(token.claims().clv.has_value());
    BOOST_CHECK_EQUAL(token.claims().clv.value(), "1.0");
}

BOOST_AUTO_TEST_CASE(testJwtVerifierSuccess)
{
    auto secretHex = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
    auto secretFile = writeSecretFile(secretHex);

    auto config = std::make_shared<JwtConfig>();
    config->setSecretFile(secretFile);
    config->setClockSkewSecs(60);
    config->setAllowedAlgorithms("HS256");

    JwtVerifier verifier(config);
    auto jwt = buildJwt("HS256", "JWT", static_cast<int64_t>(utcTime() / 1000), "client1",
        "1.0",
        secretHex);

    auto result = verifier.verify("Bearer " + jwt);
    BOOST_CHECK(result);
    BOOST_CHECK_EQUAL(result.error, JwtError::Ok);
    BOOST_CHECK_GT(result.token.claims().iat, 0);
    BOOST_CHECK_EQUAL(result.token.header().alg, "HS256");
}

BOOST_AUTO_TEST_CASE(testJwtVerifierBadBearer)
{
    auto config = std::make_shared<JwtConfig>();

    JwtVerifier verifier(config);
    auto result = verifier.verify("Basic abc");
    BOOST_CHECK(!result);
    BOOST_CHECK_EQUAL(result.error, JwtError::InvalidBearerFormat);
    BOOST_CHECK_EQUAL(toJsonRpcJwtErrorCode(result.error), bcos::rpc::JwtUnauthorized);
}

BOOST_AUTO_TEST_CASE(testJwtVerifierUnsupportedAlg)
{
    auto secretHex = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
    auto secretFile = writeSecretFile(secretHex);

    auto config = std::make_shared<JwtConfig>();
    config->setSecretFile(secretFile);
    config->setAllowedAlgorithms("HS256");

    JwtVerifier verifier(config);
    auto jwt = buildJwt("HS512", "JWT", static_cast<int64_t>(utcTime() / 1000), std::nullopt,
        std::nullopt, secretHex);

    auto result = verifier.verify("Bearer " + jwt);
    BOOST_CHECK(!result);
    BOOST_CHECK_EQUAL(result.error, JwtError::UnsupportedAlgorithm);
    BOOST_CHECK_EQUAL(toJsonRpcJwtErrorCode(result.error), bcos::rpc::JwtForbidden);
}

BOOST_AUTO_TEST_CASE(testJwtVerifierInvalidSignature)
{
    auto secretHex = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
    auto secretFile = writeSecretFile(secretHex);

    auto config = std::make_shared<JwtConfig>();
    config->setSecretFile(secretFile);
    config->setAllowedAlgorithms("HS256");

    JwtVerifier verifier(config);
    auto jwt = buildJwt("HS256", "JWT", static_cast<int64_t>(utcTime() / 1000), std::nullopt,
        std::nullopt, "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");

    auto result = verifier.verify("Bearer " + jwt);
    BOOST_CHECK(!result);
    BOOST_CHECK_EQUAL(result.error, JwtError::InvalidSignature);
    BOOST_CHECK_EQUAL(toJsonRpcJwtErrorCode(result.error), bcos::rpc::JwtUnauthorized);
}

BOOST_AUTO_TEST_CASE(testJwtVerifierExpiredIat)
{
    auto secretHex = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
    auto secretFile = writeSecretFile(secretHex);

    auto config = std::make_shared<JwtConfig>();
    config->setSecretFile(secretFile);
    config->setClockSkewSecs(1);
    config->setAllowedAlgorithms("HS256");

    JwtVerifier verifier(config);
    auto jwt = buildJwt("HS256", "JWT", 1, std::nullopt, std::nullopt, secretHex);

    auto result = verifier.verify("Bearer " + jwt);
    BOOST_CHECK(!result);
    BOOST_CHECK_EQUAL(result.error, JwtError::InvalidIssuedAt);
    BOOST_CHECK_EQUAL(toJsonRpcJwtErrorCode(result.error), bcos::rpc::JwtUnauthorized);
}

BOOST_AUTO_TEST_CASE(testJwtVerifierMissingAuthorization)
{
    auto config = std::make_shared<JwtConfig>();
    JwtVerifier verifier(config);

    auto result = verifier.verify("");
    BOOST_CHECK(!result);
    BOOST_CHECK_EQUAL(result.error, JwtError::MissingAuthorization);
    BOOST_CHECK_EQUAL(toJsonRpcJwtErrorCode(result.error), bcos::rpc::JwtUnauthorized);
}

BOOST_AUTO_TEST_CASE(testJwtVerifierNoneAlg)
{
    auto secretHex = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
    auto secretFile = writeSecretFile(secretHex);

    auto config = std::make_shared<JwtConfig>();
    config->setSecretFile(secretFile);
    config->setAllowedAlgorithms("HS256");

    JwtVerifier verifier(config);
    auto jwt = buildJwtNone();

    auto result = verifier.verify("Bearer " + jwt);
    BOOST_CHECK(!result);
    // alg:none is rejected by verifyAlgorithm() because "none" is not in the allowedAlgorithms list
    BOOST_CHECK_EQUAL(result.error, JwtError::UnsupportedAlgorithm);
}

BOOST_AUTO_TEST_CASE(testJwtVerifierFutureIat)
{
    auto secretHex = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
    auto secretFile = writeSecretFile(secretHex);

    auto config = std::make_shared<JwtConfig>();
    config->setSecretFile(secretFile);
    config->setClockSkewSecs(1);  // only 1s tolerance
    config->setAllowedAlgorithms("HS256");

    JwtVerifier verifier(config);
    // Set iat to 300s in the future (far beyond 1s skew)
    auto futureIat = static_cast<int64_t>(utcTime() / 1000) + 300;
    auto jwt = buildJwt("HS256", "JWT", futureIat, std::nullopt, std::nullopt, secretHex);

    auto result = verifier.verify("Bearer " + jwt);
    BOOST_CHECK(!result);
    BOOST_CHECK_EQUAL(result.error, JwtError::InvalidIssuedAt);
}

BOOST_AUTO_TEST_CASE(testJwtVerifierSecretInvalidLength)
{
    auto secretHex = "00010203";  // only 8 chars, not 64
    auto secretFile = writeSecretFile(secretHex);

    auto config = std::make_shared<JwtConfig>();
    config->setSecretFile(secretFile);
    config->setAllowedAlgorithms("HS256");

    JwtVerifier verifier(config);
    auto jwt = buildJwt("HS256", "JWT", static_cast<int64_t>(utcTime() / 1000), std::nullopt,
        std::nullopt, secretHex);

    auto result = verifier.verify("Bearer " + jwt);
    BOOST_CHECK(!result);
    BOOST_CHECK_EQUAL(result.error, JwtError::SecretReadFailed);
}

BOOST_AUTO_TEST_CASE(testJwtVerifierSecretNonHex)
{
    // 64 chars but not valid hex
    auto secretHex = "zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz";
    auto secretFile = writeSecretFile(secretHex);

    auto config = std::make_shared<JwtConfig>();
    config->setSecretFile(secretFile);
    config->setAllowedAlgorithms("HS256");

    JwtVerifier verifier(config);
    auto jwt = buildJwt("HS256", "JWT", static_cast<int64_t>(utcTime() / 1000), std::nullopt,
        std::nullopt, "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");

    auto result = verifier.verify("Bearer " + jwt);
    BOOST_CHECK(!result);
    BOOST_CHECK_EQUAL(result.error, JwtError::SecretReadFailed);
}

// The secret is loaded once in the constructor and is read-only afterwards.
// Verify that concurrent verify() calls from multiple threads are safe.
BOOST_AUTO_TEST_CASE(testJwtVerifierConcurrent)
{
    auto secretHex = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
    auto secretFile = writeSecretFile(secretHex);

    auto config = std::make_shared<JwtConfig>();
    config->setSecretFile(secretFile);
    config->setClockSkewSecs(600);
    config->setAllowedAlgorithms("HS256");

    JwtVerifier verifier(config);
    auto validJwt = buildJwt("HS256", "JWT", static_cast<int64_t>(utcTime() / 1000), "client1",
        "1.0", secretHex);
    auto invalidJwt = buildJwt("HS256", "JWT", static_cast<int64_t>(utcTime() / 1000), std::nullopt,
        std::nullopt, "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");

    // Sanity check the invalid token alone first.
    {
        auto sanity = verifier.verify("Bearer " + invalidJwt);
        BOOST_TEST_MESSAGE("sanity invalidJwt ok=" << sanity.ok
                                                  << " error=" << static_cast<int>(sanity.error));
        BOOST_CHECK(!sanity.ok);
        BOOST_CHECK_EQUAL(sanity.error, JwtError::InvalidSignature);
    }

    constexpr int kThreads = 8;
    constexpr int kIterations = 200;
    std::atomic<int> successCount{0};
    std::atomic<int> badCount{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t)
    {
        threads.emplace_back([&]() {
            for (int i = 0; i < kIterations; ++i)
            {
                auto ok = verifier.verify("Bearer " + validJwt);
                if (ok.ok && ok.error == JwtError::Ok)
                {
                    ++successCount;
                }
                auto bad = verifier.verify("Bearer " + invalidJwt);
                if (!bad.ok && bad.error == JwtError::InvalidSignature)
                {
                    ++successCount;
                    ++badCount;
                }
            }
        });
    }
    for (auto& thread : threads)
    {
        thread.join();
    }
    BOOST_TEST_MESSAGE("badCount=" << badCount << " successCount=" << successCount);
    // All verify() calls must have returned the expected result.
    BOOST_CHECK_EQUAL(successCount, kThreads * kIterations * 2);
}

BOOST_AUTO_TEST_SUITE_END()
