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
 * @brief jwt token structure
 * @file JwtToken.h
 * @date 2026.05.19
 */

#pragma once

#include <jwt-cpp/traits/kazuho-picojson/defaults.h>

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace bcos::rpc
{
struct JwtHeader
{
    std::string alg;
    std::string typ;
};

struct JwtClaims
{
    // The specification of JWT is from https://github.com/ethereum/execution-apis/blob/main/src/engine/authentication.md
    // Required: iat (issued-at) claim. The execution layer client SHOULD only accept iat timestamps which are within +-60 seconds from the current time.
    // Optional: id claim. The consensus layer client MAY use this to communicate a unique identifier for the individual consensus layer client.
    // Optional: clv claim. The consensus layer client MAY use this to communicate the consensus layer client type/version.
    int64_t iat{0};
    std::optional<std::string> id;
    std::optional<std::string> clv;
};

class JwtToken
{
public:
    using Ptr = std::shared_ptr<JwtToken>;

    JwtToken() = default;
    JwtToken(JwtHeader _header, JwtClaims _claims, std::string _signature)
      : m_header(std::move(_header)),
        m_claims(std::move(_claims)),
        m_signature(std::move(_signature))
    {}
    static JwtToken decode(
        const ::jwt::decoded_jwt<::jwt::traits::kazuho_picojson>& decoded)
    {
        JwtHeader header;
        if (decoded.has_algorithm())
        {
            header.alg = decoded.get_algorithm();
        }
        if (decoded.has_type())
        {
            header.typ = decoded.get_type();
        }

        JwtClaims claims;
        if (decoded.has_issued_at())
        {
            claims.iat = std::chrono::duration_cast<std::chrono::seconds>(
                             decoded.get_issued_at().time_since_epoch())
                             .count();
        }
        if (decoded.has_payload_claim("id"))
        {
            claims.id = decoded.get_payload_claim("id").as_string();
        }
        if (decoded.has_payload_claim("clv"))
        {
            claims.clv = decoded.get_payload_claim("clv").as_string();
        }

        return JwtToken(std::move(header), std::move(claims), decoded.get_signature_base64());
    }

    const JwtHeader& header() const { return m_header; }
    const JwtClaims& claims() const { return m_claims; }
    const std::string& signature() const { return m_signature; }

private:
    JwtHeader m_header;
    JwtClaims m_claims;
    std::string m_signature;
};
}  // namespace bcos::rpc

