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
 * @brief RpcCall over the plaintext WebSocket RPC port, through bcos-cpp-sdk's WsService client
 * @file RpcClient.h
 */
#pragma once

#include "RpcCall.h"
#include <cstdint>
#include <string>

namespace bcos::ops
{
/// Raw TCP probe: sends a JSON-RPC POST and looks at the first reply byte. 0x15 (TLS alert) or 0x16
/// (TLS handshake) means the port speaks TLS. Throws OpsError{1} when TCP connect fails.
bool probeTls(std::string const& _host, uint16_t _port, int _timeoutMs);

/// _connectTimeoutMs bounds the TCP probe and the websocket handshake; _requestTimeoutMs bounds
/// each RpcCall (sendTransaction waits for the receipt, which can take a view change). Throws
/// OpsError{1} on a TLS port or a connection failure. The returned Connection has source="rpc"
/// and an empty group: the caller fills it.
constexpr int c_defaultConnectTimeoutMs = 3000;
constexpr int c_defaultRequestTimeoutMs = 15000;
Connection makeWsRpcCall(std::string const& _host, uint16_t _port,
    int _connectTimeoutMs = c_defaultConnectTimeoutMs,
    int _requestTimeoutMs = c_defaultRequestTimeoutMs);
}  // namespace bcos::ops
