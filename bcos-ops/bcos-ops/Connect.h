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
#include <set>
#include <string>

namespace bcos::ops
{
struct ConnectOptions
{
    std::string nodeDir = ".";
    std::optional<std::string> rpc;  // host:port
    int connectTimeoutMs = 3000;
    int requestTimeoutMs = 15000;
    std::optional<std::string> mode;  // --mode: overrides the node dir's [ethereum].mode
    std::optional<std::string> web3;  // --web3 host:port: overrides the node dir's [web3_rpc]
    bool allowIpc = true;
    bool allowRpc = true;
};

/// the option names connectOptionsFrom reads; every command accepts them
inline std::set<std::string> const c_connectOptions = {
    "node-dir", "rpc", "connect-timeout", "timeout", "mode", "web3"};
ConnectOptions connectOptionsFrom(Args const& _args);
/// Keeps a transport alive for the rest of the process. The SDK's WsService cannot be torn down
/// from the main thread while its io thread is still draining callbacks (closing the socket there
/// races the reader), and a one-shot CLI has nothing to gain from the teardown: runOps flushes
/// stdout/stderr and leaves through _Exit, so the retained transports are never destroyed.
void retainTransport(std::shared_ptr<void> _transport);
/// parses "host:port"; throws OpsError{1} on a malformed value
std::pair<std::string, uint16_t> parseHostPort(std::string const& _endpoint);
/// --mode wins, then the node dir's [ethereum].mode; a bare --rpc is "unknown" (Mode.h): the
/// endpoint cannot tell, and getConsensusStatus would stop an EL node. Throws OpsError{1} on
/// a value other than none / el / opstack-el, naming where it came from
std::string connectionMode(ConnectOptions const& _options, NodeDir const* _node);
Connection connect(ConnectOptions const& _options);
}  // namespace bcos::ops
