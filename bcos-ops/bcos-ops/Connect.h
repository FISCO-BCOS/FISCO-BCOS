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
 * @brief source selection shared by status / tx / attach / log-level:
 *        --rpc host:port → WebSocket; else --node-dir → local socket when it exists and
 *        connects (source=attach), else that node's plaintext RPC (source=rpc); TLS → exit 1.
 * @file Connect.h
 */
#pragma once

#include "Args.h"
#include "RpcCall.h"
#include <optional>
#include <string>

namespace bcos::ops
{
struct ConnectOptions
{
    std::string nodeDir = ".";
    std::optional<std::string> rpc;  // host:port
    int connectTimeoutMs = 3000;
    int requestTimeoutMs = 15000;
    bool allowIpc = true;
    bool allowRpc = true;
};

ConnectOptions connectOptionsFrom(Args const& _args);
/// Keeps a transport alive for the rest of the process. The SDK's WsService cannot be torn down
/// from the main thread while its io thread is still draining callbacks (closing the socket there
/// races the reader), and a one-shot CLI has nothing to gain from the teardown: runOps flushes
/// stdout/stderr and leaves through _Exit, so the retained transports are never destroyed.
void retainTransport(std::shared_ptr<void> _transport);
/// parses "host:port"; throws OpsError{1} on a malformed value
std::pair<std::string, uint16_t> parseHostPort(std::string const& _endpoint);
Connection connect(ConnectOptions const& _options);
}  // namespace bcos::ops
