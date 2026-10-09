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
 * @brief RpcCall over the node's unix socket (newline-delimited JSON-RPC), source="attach"
 * @file IpcClient.h
 */
#pragma once

#include "RpcCall.h"
#include <string>

namespace bcos::ops
{
/// true when the file exists and a connection succeeds within _timeoutMs
bool ipcReachable(std::string const& _path, int _timeoutMs);
/// one blocking connection reused for every call; throws OpsError{1} when it cannot connect
Connection makeIpcRpcCall(std::string const& _path, int _timeoutMs = 15000);
}  // namespace bcos::ops
