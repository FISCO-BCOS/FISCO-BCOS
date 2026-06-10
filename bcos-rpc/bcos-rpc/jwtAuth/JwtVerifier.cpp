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
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FileUtility.h>
#include <jwt-cpp/traits/kazuho-picojson/defaults.h>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <algorithm>
#include <cctype>
#include <vector>

namespace bcos::rpc
{
namespace
{
JwtVerifyResult makeError(JwtError _error, std::string _message)
{
    return JwtVerifyResult{false, _error, std::move(_message), {}};
}
}

JwtVerifyResult JwtVerifier::verify(std::string_view _authorizationHeader) const
{
    if (_authorizationHeader.empty())
    {
        return makeError(
            JwtError::MissingAuthorization, std::string(toString(JwtError::MissingAuthorization)));
    }

    if (!boost::istarts_with(_authorizationHeader, "Bearer "))
    {
        return makeError(
            JwtError::InvalidBearerFormat, std::string(toString(JwtError::InvalidBearerFormat)));
    }

    auto jwtCompact = _authorizationHeader.substr(std::string_view("Bearer ").size());
    return verifyToken(jwtCompact);
}

JwtVerifyResult JwtVerifier::verifyToken(std::string_view _jwtCompact) const
{
    JwtToken token;
    try
    {
        token = JwtToken::decode(_jwtCompact);
    }
    catch (...)
    {
        return makeError(
            JwtError::InvalidTokenFormat, std::string(toString(JwtError::InvalidTokenFormat)));
    }

    if (token.header().alg.empty() || !verifyAlgorithm(token.header().alg))
    {
        return makeError(
            JwtError::UnsupportedAlgorithm, std::string(toString(JwtError::UnsupportedAlgorithm)));
    }

    if (token.claims().iat == 0 || !verifyIat(token.claims().iat))
    {
        return makeError(
            JwtError::InvalidIssuedAt, std::string(toString(JwtError::InvalidIssuedAt)));
    }

    auto secret = readSecretRaw();
    if (secret.empty())
    {
        return makeError(
            JwtError::SecretReadFailed, std::string(toString(JwtError::SecretReadFailed)));
    }

    try
    {
        auto decoded = ::jwt::decode(std::string(_jwtCompact));
        auto verifier = ::jwt::verify().allow_algorithm(::jwt::algorithm::hs256{secret});
        verifier.verify(decoded);
    }
    catch (std::exception const& e)
    {
        (void)e;
        return makeError(
            JwtError::InvalidSignature, std::string(toString(JwtError::InvalidSignature)));
    }

    JwtVerifyResult result;
    result.ok = true;
    result.error = JwtError::Ok;
    result.token = std::move(token);
    return result;
}

bool JwtVerifier::verifyAlgorithm(std::string_view _alg) const
{
    if (_alg.empty())
    {
        return false;
    }

    auto allowedAlgorithms = m_config ? m_config->allowedAlgorithms() : std::string();
    if (allowedAlgorithms.empty())
    {
        return true;
    }

    std::vector<std::string> algorithms;
    boost::split(algorithms, allowedAlgorithms, boost::is_any_of(","));
    for (auto& algorithm : algorithms)
    {
        boost::algorithm::trim(algorithm);
        if (algorithm.empty())
        {
            continue;
        }
        if (algorithm == "*" || algorithm == _alg)
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
    return (_iat >= (now - skew)) && (_iat <= (now + skew));
}

std::string JwtVerifier::readSecret() const
{
    return readSecretRaw();
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

std::string JwtVerifier::readSecretRaw() const
{
    if (!m_config || m_config->secretFile().empty())
    {
        return {};
    }

    auto secretContent = readContentsToString(m_config->secretFile());
    if (!secretContent || secretContent->empty())
    {
        return {};
    }

    auto secret = boost::algorithm::trim_copy(*secretContent);
    if (secret.empty())
    {
        return {};
    }

    if (boost::algorithm::starts_with(secret, "0x") || boost::algorithm::starts_with(secret, "0X"))
    {
        secret = secret.substr(2);
    }

    if (!validateSecret(secret))
    {
        return {};
    }

    std::string decoded;
    decoded.resize(secret.size() / 2);
    try
    {
        boost::algorithm::unhex(secret.begin(), secret.end(), decoded.begin());
    }
    catch (...)
    {
        return {};
    }
    return decoded;
}
}  // namespace bcos::rpc
