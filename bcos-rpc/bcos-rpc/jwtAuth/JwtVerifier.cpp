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
 * @brief jwt verifier
 * @file JwtVerifier.cpp
 * @date 2026.05.19
 */

#include "JwtVerifier.h"
#include <bcos-rpc/Common.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FileUtility.h>
#include <jwt-cpp/traits/kazuho-picojson/defaults.h>
#include <boost/algorithm/hex.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <algorithm>
#include <cctype>
#include <vector>

namespace bcos::rpc
{
inline JwtVerifyResult makeError(JwtError _error)
{
    return JwtVerifyResult{
        .ok = false,
        .error = _error,
        .errorMessage = std::string(toString(_error)),
        .token = {}
    };
}

JwtVerifier::JwtVerifier(JwtConfig::Ptr _config) : m_config(std::move(_config))
{
    if (!readSecret().has_value())
    {
        RPC_LOG(WARNING) << "JwtVerifier: JWT secret is not available at construction time; "
                         << "it will be re-attempted lazily on the first verify() call";
    }
}

JwtVerifyResult JwtVerifier::verify(std::string_view _authorizationHeader) const
{
    if (_authorizationHeader.empty())
    {
        return makeError(JwtError::MissingAuthorization);
    }

    if (!boost::istarts_with(_authorizationHeader, "Bearer "))
    {
        return makeError(JwtError::InvalidBearerFormat);
    }

    auto jwtCompact = _authorizationHeader.substr(std::string_view("Bearer ").size());
    return verifyToken(jwtCompact);
}

JwtVerifyResult JwtVerifier::verifyToken(std::string_view _jwtCompact) const
{
    auto secret = readSecret();
    if (!secret.has_value())
    {
        return makeError(JwtError::SecretReadFailed);
    }

    try
    {
        auto decoded = ::jwt::decode(std::string(_jwtCompact));

        auto token = JwtToken::decode(decoded);

        if (token.header().alg.empty() || !verifyAlgorithm(token.header().alg))
        {
            return makeError(JwtError::UnsupportedAlgorithm);
        }

        if (token.claims().iat == 0 || !verifyIat(token.claims().iat))
        {
            return makeError(JwtError::InvalidIssuedAt);
        }

        // Only verify the signature via jwt-cpp; skip default claim checks (iat/exp/nbf)
        // since we do those ourselves (algorithm + iat validation above).
        // TODO: support dynamic algorithm selection based on token header alg
        // Currently only HS256 is supported per Ethereum Engine API spec
        auto verifier = ::jwt::verify()
                            .allow_algorithm(::jwt::algorithm::hs256{*secret})
                            .with_claim("iat", [](const auto&, std::error_code&) {})
                            .with_claim("exp", [](const auto&, std::error_code&) {})
                            .with_claim("nbf", [](const auto&, std::error_code&) {});
        verifier.verify(decoded);

        JwtVerifyResult result;
        result.ok = true;
        result.error = JwtError::Ok;
        result.token = std::move(token);
        return result;
    }
    catch (::jwt::error::signature_verification_exception const& e)
    {
        RPC_LOG(WARNING) << "JWT signature verification failed: " << e.what();
        return makeError(JwtError::InvalidSignature);
    }
    catch (std::exception const& e)
    {
        RPC_LOG(WARNING) << "JWT decode failed: " << e.what();
        return makeError(JwtError::InvalidTokenFormat);
    }
}

bool JwtVerifier::verifyAlgorithm(std::string_view _alg) const
{
    if (_alg.empty())
    {
        return false;
    }

    auto allowedAlgorithms = m_config ? m_config->allowedAlgorithms() : std::string();

    std::vector<std::string> algorithms;
    boost::split(algorithms, allowedAlgorithms, boost::is_any_of(","));
    for (auto& algorithm : algorithms)
    {
        boost::algorithm::trim(algorithm);
        if (algorithm.empty())
        {
            continue;
        }
        if (algorithm == _alg)
        {
            return true;
        }
    }
    return false;
}

bool JwtVerifier::verifyIat(int64_t _iat) const
{
    // JWT iat follows NumericDate in seconds, while utcTime() returns milliseconds.
    auto now = static_cast<int64_t>(utcTime() / 1000);
    auto skew = m_config ? m_config->clockSkewSecs() : 0;
    // Defensive overflow/underflow protection
    auto lower = (skew > now) ? INT64_MIN : (now - skew);
    auto upper = (skew > INT64_MAX - now) ? INT64_MAX : (now + skew);
    return (_iat >= lower) && (_iat <= upper);
}

bool JwtVerifier::validateSecret(std::string_view _secret) const
{
    if (_secret.size() != 64)
    {
        return false;
    }

    return std::all_of(_secret.begin(), _secret.end(), [](unsigned char ch) {
        return std::isxdigit(ch) != 0;
    });
}

std::optional<std::string> JwtVerifier::readSecret() const
{
    // The secret is read once at construction and cached in m_secret.
    // NOTE: key rotation / hot-reload is intentionally deferred — a future design
    // should revisit secret refresh without blocking RPC IO threads on file I/O.
    if (m_secret.has_value())
    {
        return m_secret;
    }

    if (!m_config || m_config->secretFile().empty())
    {
        return std::nullopt;
    }

    std::string secretContent;
    try
    {
        secretContent = readContentsToString(m_config->secretFile());
    }
    catch (std::exception const& e)
    {
        RPC_LOG(WARNING) << "Failed to read JWT secret file: " << e.what();
        return std::nullopt;
    }
    if (secretContent.empty())
    {
        return std::nullopt;
    }

    auto secret = boost::algorithm::trim_copy(secretContent);
    if (secret.empty())
    {
        return std::nullopt;
    }

    if (secret.starts_with("0x") || secret.starts_with("0X"))
    {
        secret = secret.substr(2);
    }

    if (!validateSecret(secret))
    {
        return std::nullopt;
    }

    std::string decoded;
    decoded.resize(secret.size() / 2);
    try
    {
        boost::algorithm::unhex(secret.begin(), secret.end(), decoded.begin());
    }
    catch (std::exception const& e)
    {
        RPC_LOG(WARNING) << "JWT secret hex decode failed: " << e.what();
        return std::nullopt;
    }

    // Publish the successfully decoded secret into the cache. Multiple threads
    // may race here after a constructor-time failure; writing the same value is
    // benign (the optional write is the only mutation, and all writers write an
    // identical string).
    m_secret = std::move(decoded);
    return m_secret;
}
}  // namespace bcos::rpc
