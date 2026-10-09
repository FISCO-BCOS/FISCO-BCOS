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
 * @brief `tx smoke`: deploy HelloWorld → set("Hello, FISCO BCOS") → get; stops at the first
 *        failed step. A deploy refused by the chain's auth policy is reported as
 *        reason=auth_denied and exit code 2 (the chain works, the account is not allowed).
 * @file Smoke.h
 */
#pragma once

#include "TxSender.h"
#include <json/json.h>
#include <string>
#include <vector>

namespace bcos::ops
{
constexpr std::string_view c_smokeMessage = "Hello, FISCO BCOS";

struct SmokeStep
{
    std::string name;  // deploy | set | get
    bool ok = false;
    std::string txHash;
    int32_t status = -1;
    std::string detail;  // contract address, gasUsed, or the decoded value
    Json::Value toJson() const;
};

struct SmokeResult
{
    std::vector<SmokeStep> steps;
    bool ok = false;
    std::string reason;  // "" | auth_denied | <step>_failed
    int exitCode() const;
    Json::Value toJson() const;
};

/// _authCheck: the chain's auth switch when known; it only sharpens the auth_denied verdict
SmokeResult runSmoke(Sender& _sender, bool _sm, std::optional<bool> _authCheck);
}  // namespace bcos::ops
