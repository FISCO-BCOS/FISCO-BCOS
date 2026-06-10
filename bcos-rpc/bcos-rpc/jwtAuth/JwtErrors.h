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
 * @brief jwt error codes
 * @file JwtErrors.h
 * @date 2026.05.19
 */

#pragma once

#include <bcos-rpc/jsonrpc/Common.h>
#include <magic_enum/magic_enum.hpp>

#include <cctype>
#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>

namespace bcos::rpc
{
enum class JwtError
{
    Ok = 0,
    MissingAuthorization,
    InvalidBearerFormat,
    InvalidTokenFormat,
    InvalidBase64Url,
    InvalidJson,
    UnsupportedAlgorithm,
    InvalidSignature,
    InvalidIssuedAt,
    Expired,
    SecretReadFailed
};

inline std::string toString(JwtError _error)
{
    auto errorName = std::string(magic_enum::enum_name(_error));
    if (errorName.empty())
    {
        return "unknownJWTError";
    }
    return errorName;
}

inline int32_t toJsonRpcJwtErrorCode(JwtError _error)
{
    switch (_error)
    {
    case JwtError::UnsupportedAlgorithm:
    case JwtError::Expired:
    case JwtError::SecretReadFailed:
        return JwtForbidden;
    case JwtError::MissingAuthorization:
    case JwtError::InvalidBearerFormat:
    case JwtError::InvalidTokenFormat:
    case JwtError::InvalidBase64Url:
    case JwtError::InvalidJson:
    case JwtError::InvalidSignature:
    case JwtError::InvalidIssuedAt:
        return JwtUnauthorized;
    case JwtError::Ok:
    default:
        return InternalError;
    }
}

inline std::ostream& operator<<(std::ostream& _out, JwtError _error)
{
    _out << toString(_error);
    return _out;
}
}  // namespace bcos::rpc
