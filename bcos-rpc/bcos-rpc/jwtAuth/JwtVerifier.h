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
 * @file JwtVerifier.h
 * @date 2026.05.19
 */

#pragma once

#include "JwtConfig.h"
#include "JwtErrors.h"
#include "JwtToken.h"
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace bcos::rpc
{
struct JwtVerifyResult
{
    bool ok{false};
    JwtError error{JwtError::Ok};
    std::string errorMessage;
    JwtToken token;

    explicit operator bool() const { return ok; }
};

class JwtVerifier
{
public:
    using Ptr = std::shared_ptr<JwtVerifier>;

    explicit JwtVerifier(JwtConfig::Ptr _config) : m_config(std::move(_config)) {}

    ~JwtVerifier() = default;

    JwtVerifyResult verify(std::string_view authorizationHeader) const;
    JwtVerifyResult verifyToken(std::string_view jwtCompact) const;

    bool verifyAlgorithm(std::string_view alg) const;
    bool verifyIat(int64_t iat) const;

    bool validateSecret(std::string_view secret) const;

private:
    std::optional<std::string> readSecret() const;

    JwtConfig::Ptr m_config;
    mutable std::optional<std::string> m_secret;
};
}  // namespace bcos::rpc
