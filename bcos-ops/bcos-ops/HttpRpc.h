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
 * @brief JSON-RPC over plain HTTP for the node's web3 listener: the SDK's WebSocket transport
 *        (RpcClient) speaks the FISCO message framing that the web3 port does not
 * @file HttpRpc.h
 */
#pragma once

#include "bcos-ops/RpcCall.h"
#include <cstdint>
#include <string>

namespace bcos::ops
{
/// one HTTP POST per call (no connection kept), bounded by _timeoutMs end to end; transport
/// failures throw OpsError{1}, a JSON-RPC error throws OpsRpcError
bcos::ops::RpcCall makeHttpRpcCall(std::string _host, uint16_t _port, int _timeoutMs);
/// "host:port" form (Connection::web3Endpoint)
bcos::ops::RpcCall makeHttpRpcCall(std::string const& _endpoint, int _timeoutMs);
}  // namespace bcos::ops
