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
 * @brief the one error type every ops command throws; exitCode is the process exit code
 * @file OpsError.h
 */
#pragma once

#include <stdexcept>
#include <string>

namespace bcos::ops
{
/// 0: normal; 1: usage or connection error; 2: node reachable but a check failed / auth denied
constexpr int c_exitOk = 0;
constexpr int c_exitUsage = 1;
constexpr int c_exitChecksFailed = 2;

struct OpsError : public std::runtime_error
{
    OpsError(int _exitCode, std::string _msg)
      : std::runtime_error(std::move(_msg)), exitCode(_exitCode)
    {}
    int exitCode;
};

/// a JSON-RPC `error` member: the node refused the request (a txpool admission rejection carries
/// the protocol::TransactionStatus value as `code`). Exit code 1 like every other OpsError so
/// callers that do not care keep their behaviour; tx commands catch it to print the code.
struct OpsRpcError : public OpsError
{
    OpsRpcError(std::string _method, int _code, std::string _message)
      : OpsError(
            c_exitUsage, _method + " failed: code=" + std::to_string(_code) + " msg=" + _message),
        method(std::move(_method)),
        code(_code),
        message(std::move(_message))
    {}
    std::string method;
    int code;
    std::string message;
};
}  // namespace bcos::ops
